/*
 * Copyright (C) 2019-2024 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDebug>
#include <QVariant>
#include <algorithm>
#include <functional>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

#include "datatypes.h"
#include "readerwriterqueue.h"
#include "syclock.h"

using namespace moodycamel;
using namespace Syntalos;

enum class CommonMetadataKey {
    SrcModType,
    SrcModName,
    SrcModPortTitle,
    DataNameProposal
};

inline uint qHash(CommonMetadataKey key, uint seed)
{
    return ::qHash(static_cast<uint>(key), seed);
}

// clang-format off
typedef QHash<CommonMetadataKey, QString> CommonMetadataKeyMap;
Q_GLOBAL_STATIC_WITH_ARGS(CommonMetadataKeyMap,
      _commonMetadataKeyMap,
      ({
          {CommonMetadataKey::SrcModType, QLatin1String("src_mod_type")},
          {CommonMetadataKey::SrcModName, QLatin1String("src_mod_name")},
          {CommonMetadataKey::SrcModPortTitle, QLatin1String("src_mod_port_title")},
          {CommonMetadataKey::DataNameProposal, QLatin1String("data_name_proposal")},
      })
);
// clang-format on

/**
 * @brief A function that can be used to process a variant value
 */
using ProcessVarFn = std::function<void(BaseDataType &)>;

class VariantStreamSubscription
{
public:
    virtual ~VariantStreamSubscription();
    virtual int dataTypeId() const = 0;
    virtual QString dataTypeName() const = 0;
    virtual bool callIfNextVar(const ProcessVarFn &fn) = 0;
    virtual bool unsubscribe() = 0;
    virtual bool active() const = 0;
    virtual bool hasPending() const = 0;
    virtual size_t approxPendingCount() const = 0;
    virtual int enableNotify() = 0;
    virtual void setThrottleItemsPerSec(uint itemsPerSec, bool allowMore = true) = 0;

    virtual void suspend() = 0;
    virtual void resume() = 0;
    virtual void clearPending() = 0;

    virtual QHash<QString, QVariant> metadata() const = 0;
    virtual QVariant metadataValue(const QString &key, const QVariant &defaultValue = QVariant()) const = 0;
    virtual QVariant metadataValue(CommonMetadataKey key, const QVariant &defaultValue = QVariant()) const = 0;

    // used internally by Syntalos
    virtual void forcePushNullopt() = 0;
};

class VariantDataStream
{
public:
    virtual ~VariantDataStream();
    virtual QString dataTypeName() const = 0;
    virtual int dataTypeId() const = 0;
    virtual std::shared_ptr<VariantStreamSubscription> subscribeVar() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool active() const = 0;
    virtual bool hasSubscribers() const = 0;
    virtual void pushRawData(int typeId, const void *data, size_t size) = 0;
    virtual QHash<QString, QVariant> metadata() = 0;
    virtual void setMetadata(const QHash<QString, QVariant> &metadata) = 0;
    virtual void setMetadataValue(const QString &key, const QVariant &value) = 0;
    virtual void setCommonMetadata(const QString &srcModType, const QString &srcModName, const QString &portTitle) = 0;
};

template<typename T>
class DataStream;

template<typename T>
class StreamSubscription : public VariantStreamSubscription
{
    friend DataStream<T>;

public:
    StreamSubscription(DataStream<T> *stream)
        : m_stream(stream),
          m_queue(BlockingReaderWriterQueue<std::optional<T>>(256)),
          m_eventfd(-1),
          m_notify(false),
          m_active(true),
          m_suspended(false),
          m_throttle(0),
          m_skippedElements(0)
    {
        m_lastItemTime = currentTimePoint();
        m_eventfd = eventfd(0, EFD_NONBLOCK);
        if (m_eventfd < 0) {
            qFatal("Unable to obtain eventfd for new stream subscription: %s", std::strerror(errno));
            assert(0);
        }
    }

    ~StreamSubscription()
    {
        m_active = false;
        unsubscribe();
        m_notify = false;
        close(m_eventfd);
    }

    /**
     * @brief Obtain next element from stream, block in case there is no new element
     * @return The obtained value, or std::nullopt in case the stream ended.
     */
    std::optional<T> next()
    {
        if (!m_active && m_queue.peek() == nullptr)
            return std::nullopt;
        std::optional<T> data;
        m_queue.wait_dequeue(data);
        return data;
    }

    /**
     * @brief Obtain the next stream element if there is any, otherwise return std::nullopt
     * This function behaves the same as next(), but does return immediately without blocking.
     * To see if the stream as ended, check the active() property on this subscription.
     */
    std::optional<T> peekNext()
    {
        if (!m_active && m_queue.peek() == nullptr)
            return std::nullopt;
        std::optional<T> data;

        if (!m_queue.try_dequeue(data))
            return std::nullopt;

        return data;
    }

    /**
     * @brief Call function on the next element, if there is any.
     * @param fn The function to call with the next element.
     * @return True if an element was processed, false if there was no element.
     */
    bool callIfNextVar(const ProcessVarFn &fn) override
    {
        auto res = peekNext();
        if (res.has_value()) {
            fn(res.value());
            return true;
        }
        return false;
    }

    int dataTypeId() const override
    {
        return syDataTypeId<T>();
    }

    QString dataTypeName() const override
    {
        return BaseDataType::typeIdToString(syDataTypeId<T>());
    }

    QHash<QString, QVariant> metadata() const override
    {
        return m_metadata;
    }

    QVariant metadataValue(const QString &key, const QVariant &defaultValue = QVariant()) const override
    {
        return m_metadata.value(key, defaultValue);
    }

    QVariant metadataValue(CommonMetadataKey key, const QVariant &defaultValue = QVariant()) const override
    {
        return m_metadata.value(_commonMetadataKeyMap->value(key), defaultValue);
    }

    bool unsubscribe() override
    {
        // check if we are already unsubscribed
        if (m_stream == nullptr)
            return true;

        if (m_stream->unsubscribe(this)) {
            m_stream = nullptr;
            return true;
        }
        return false;
    }

    bool active() const override
    {
        return m_active;
    }

    /**
     * @brief Enable notifiucations on this stream subscription
     * @return An eventfd file descriptor that is written to when new data is received.
     */
    int enableNotify() override
    {
        m_notify = true;
        return m_eventfd;
    }

    /**
     * @brief Disable notifications via eventFD
     *
     * Do not disable notifications unless you know for sure that nothing is
     * listening on this subscription anymore.
     */
    void disableNotify()
    {
        m_notify = false;
    }

    /**
     * @brief Stop receiving data, but do not unsubscribe from the stream
     */
    void suspend() override
    {
        // suspend receiving new data
        m_suspended = true;

        // drop currently pending data
        while (m_queue.pop()) {
        }
    }

    /**
     * @brief Resume data transmission, reverses suspend()
     */
    void resume() override
    {
        m_suspended = false;
    }

    /**
     * @brief Clear all pending data from the subscription
     */
    void clearPending() override
    {
        m_suspended = true;
        while (m_queue.pop()) {
        }
        m_suspended = false;
    }

    size_t approxPendingCount() const override
    {
        return m_queue.size_approx();
    }

    bool hasPending() const override
    {
        return m_queue.size_approx() > 0;
    }

    uint throttleValue() const
    {
        return m_throttle;
    }

    uint retrieveApproxSkippedElements()
    {
        const uint si = m_skippedElements;
        m_skippedElements = 0;
        return si;
    }

    /**
     * @brief Set a throttle on the output frequency of this subscription
     * By setting a positive integer value, the output of this
     * subscription is effectively limited to the given integer value per second.
     * This will result in some values being thrown away.
     * By setting a throttle value of 0, all output is passed through and no limits apply.
     * Internally, the throttle value is represented as the minimum needed time in microseconds
     * between elements. This effectively means you can not throttle a connection over 1000000 items/sec.
     */
    void setThrottleItemsPerSec(uint itemsPerSec, bool allowMore = true) override
    {
        uint newThrottle;
        if (itemsPerSec == 0)
            newThrottle = 0;
        else
            newThrottle = allowMore ? std::floor((1000.0 / itemsPerSec) * 1000)
                                    : std::ceil((1000.0 / itemsPerSec) * 1000);

        // clear current queue contents quickly in case we throttle down the subscription
        // (this prevents clients from skipping elements too much if they are overeager
        // when adjusting the throttle value)
        if (newThrottle > m_throttle) {
            // suspending and immediately resuming efficiently clears the current buffer
            suspend();
            resume();
        }

        // apply
        m_throttle = newThrottle;
        m_skippedElements = 0;
    }

    void forcePushNullopt() override
    {
        std::optional<T> v;
        m_queue.enqueue(v);
    }

private:
    DataStream<T> *m_stream;
    BlockingReaderWriterQueue<std::optional<T>> m_queue;
    int m_eventfd;
    std::atomic_bool m_notify;
    std::atomic_bool m_active;
    std::atomic_bool m_suspended;
    std::atomic_uint m_throttle;
    std::atomic_uint m_skippedElements;

    // NOTE: These two variables are intentionally *not* threadsafe and are
    // only ever manipulated by the stream (in case of the time) or only
    // touched once when a stream is started (in case of the metadata).
    QHash<QString, QVariant> m_metadata;
    symaster_timepoint m_lastItemTime;

    void setMetadata(const QHash<QString, QVariant> &metadata)
    {
        m_metadata = metadata;
    }

    void push(const T &data)
    {
        // don't accept any new data if we are suspended
        if (m_suspended)
            return;

        // check if we can throttle the enqueueing speed of data
        if (m_throttle != 0) {
            const auto timeNow = currentTimePoint();
            const auto durUsec = timeDiffUsec(timeNow, m_lastItemTime);
            if (durUsec.count() < m_throttle) {
                m_skippedElements++;
                return;
            }
            m_lastItemTime = timeNow;
        }

        // actually send the data to the subscriber
        m_queue.enqueue(std::optional<T>(data));

        // ping the eventfd, in case anyone is listening for messages
        if (m_notify) {
            const uint64_t buffer = 1;
            if (write(m_eventfd, &buffer, sizeof(buffer)) == -1)
                qWarning().noquote() << "Unable to write to eventfd in" << dataTypeName()
                                     << "data subscription. FD:" << m_eventfd << "Error:" << std::strerror(errno);
        }
    }

    void stop()
    {
        m_active = false;
        m_queue.enqueue(std::nullopt);
    }

    void reset()
    {
        m_suspended = false;
        m_active = true;
        m_throttle = 0;
        m_lastItemTime = currentTimePoint();
        while (m_queue.pop()) {
        } // ensure the queue is empty
    }
};

template<typename T>
class DataStream : public VariantDataStream
{
public:
    DataStream()
        : m_active(false)
    {
        m_ownerId = std::this_thread::get_id();
    }

    ~DataStream() override
    {
        terminate();
    }

    int dataTypeId() const override
    {
        return syDataTypeId<T>();
    }

    QString dataTypeName() const override
    {
        return BaseDataType::typeIdToString(syDataTypeId<T>());
    }

    QHash<QString, QVariant> metadata() override
    {
        return m_metadata;
    }

    void setMetadata(const QHash<QString, QVariant> &metadata) override
    {
        m_metadata = metadata;
    }

    void setMetadataValue(const QString &key, const QVariant &value) override
    {
        m_metadata[key] = value;
    }

    void setSuggestedDataName(const QString &value)
    {
        m_metadata[_commonMetadataKeyMap->value(CommonMetadataKey::DataNameProposal)] = value;
    }

    void removeMetadata(const QString &key)
    {
        m_metadata.remove(key);
    }

    void setCommonMetadata(const QString &srcModType, const QString &srcModName, const QString &portTitle) override
    {
        setMetadataValue(_commonMetadataKeyMap->value(CommonMetadataKey::SrcModType), srcModType);
        setMetadataValue(_commonMetadataKeyMap->value(CommonMetadataKey::SrcModName), srcModName);
        if (!portTitle.isEmpty())
            setMetadataValue(_commonMetadataKeyMap->value(CommonMetadataKey::SrcModPortTitle), portTitle);
    }

    std::shared_ptr<StreamSubscription<T>> subscribe()
    {
        // we don't permit subscriptions to an active stream
        assert(!m_active);
        if (m_active)
            return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        std::shared_ptr<StreamSubscription<T>> sub(new StreamSubscription<T>(this));
        sub->setMetadata(m_metadata);
        m_subs.push_back(sub);
        return sub;
    }

    std::shared_ptr<VariantStreamSubscription> subscribeVar() override
    {
        return subscribe();
    }

    bool unsubscribe(StreamSubscription<T> *sub)
    {
        // we don't permit unsubscribing from an active stream
        assert(!m_active);
        if (m_active)
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        for (uint i = 0; i < m_subs.size(); i++) {
            if (m_subs.at(i).get() == sub) {
                m_subs.erase(m_subs.begin() + i);
                return true;
            }
        }

        return false;
    }

    void start() override
    {
        m_ownerId = std::this_thread::get_id();
        for (auto const &sub : m_subs) {
            sub->reset();
            sub->setMetadata(m_metadata);
        }
        m_active = true;
    }

    void stop() override
    {
        for (auto const &sub : m_subs)
            sub->stop();
        m_active = false;
    }

    void push(const T &data)
    {
        if (!m_active)
            return;
        for (auto &sub : m_subs)
            sub->push(data);
    }

    void pushRawData(int typeId, const void *data, size_t size) override
    {
        if (!m_active)
            return;
        if (typeId != syDataTypeId<T>()) {
            qCritical().noquote() << "Invalid data type ID" << typeId << "for stream of type" << dataTypeName();
            return;
        }

        const T &entity = T::fromMemory(data, size);
        for (auto &sub : m_subs)
            sub->push(entity);
    }

    void terminate()
    {
        stop();

        // "unsubscribe" use forcefully from any active subscription,
        // as this stream is terminated.
        for (auto const &sub : m_subs)
            sub->m_stream = nullptr;
        m_subs.clear();
    }

    bool active() const override
    {
        return m_active;
    }

    bool hasSubscribers() const override
    {
        return !m_subs.empty();
    }

private:
    std::thread::id m_ownerId;
    std::atomic_bool m_active;
    std::mutex m_mutex;
    std::vector<std::shared_ptr<StreamSubscription<T>>> m_subs;
    QHash<QString, QVariant> m_metadata;
};
