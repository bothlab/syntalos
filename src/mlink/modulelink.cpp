/*
 * Copyright (C) 2020-2024 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "modulelink.h"

#include <QDebug>
#include <QBuffer>
#include <signal.h>
#include <sys/prctl.h>
#include <iceoryx_posh/runtime/posh_runtime.hpp>
#include <iceoryx_posh/popo/server.hpp>
#include <iceoryx_posh/popo/publisher.hpp>
#include <iceoryx_posh/popo/untyped_publisher.hpp>
#include <iceoryx_hoofs/posix_wrapper/signal_watcher.hpp>

#include "ipc-types-private.h"
#include "rtkit.h"
#include "cpuaffinity.h"

using namespace Syntalos;

namespace Syntalos
{

std::unique_ptr<ModuleLink> initModuleLink()
{
    auto syModuleId = qgetenv("SYNTALOS_MODULE_ID");
    if (syModuleId.isEmpty() || syModuleId.length() < 2)
        throw std::runtime_error("This module was not run by Syntalos, can not continue!");

    char rtName[100];
    const auto rtNameStr = QString::fromUtf8(syModuleId.right(100));
    strncpy(rtName, qPrintable(rtNameStr), sizeof(rtName) - 1);
    rtName[sizeof(rtName) - 1] = '\0';

    // connect to RouDi
    iox::runtime::PoshRuntime::initRuntime(rtName);

    // ensure we (try to) die if Syntalos, our parent, dies
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    return std::unique_ptr<ModuleLink>(new ModuleLink(rtNameStr));
}

class ModuleLink::Private
{
public:
    Private(const QString &instanceId)
    {
        modId = iox::cxx::string<100>(iox::cxx::TruncateToCapacity, instanceId.toStdString());

        // interfaces
        pubError = makePublisher<ErrorEvent>("Error");
        pubState = makePublisher<StateChangeEvent>("State");
        pubStatusMessage = makePublisher<StatusMessageEvent>("StatusMessage", false);
        pubNewInPort = makeUntypedPublisher("NewInPort");
        pubNewOutPort = makeUntypedPublisher("NewOutPort");

        reqSetNiceness = makeServer<SetNicenessRequest, DoneResponse>("SetNiceness");
        reqSetMaxRTPriority = makeServer<SetMaxRealtimePriority, DoneResponse>("SetMaxRealtimePriority");
        reqSetCPUAffinity = makeServer<SetCPUAffinityRequest, DoneResponse>("SetCPUAffinity");
    }

    ~Private() {}

    template<typename T>
    std::unique_ptr<iox::popo::Publisher<T>> makePublisher(
        const iox::cxx::string<100> &channelName,
        bool waitForConsumer = true)
    {
        iox::popo::PublisherOptions publisherOptn;

        // store the last 10 samples in queue
        publisherOptn.historyCapacity = 10U;

        if (waitForConsumer) {
            // allow the subscriber to block us, to ensure we don't lose data
            publisherOptn.subscriberTooSlowPolicy = iox::popo::ConsumerTooSlowPolicy::WAIT_FOR_CONSUMER;
        }

        return std::make_unique<iox::popo::Publisher<T>>(
            iox::capro::ServiceDescription{"SyntalosModule", modId, channelName}, publisherOptn);
    }

    std::unique_ptr<iox::popo::UntypedPublisher> makeUntypedPublisher(
        const iox::cxx::string<100> &channelName,
        bool waitForConsumer = true)
    {
        iox::popo::PublisherOptions publisherOptn;

        // store the last 10 samples in queue
        publisherOptn.historyCapacity = 10U;

        if (waitForConsumer) {
            // allow the subscriber to block us, to ensure we don't lose data
            publisherOptn.subscriberTooSlowPolicy = iox::popo::ConsumerTooSlowPolicy::WAIT_FOR_CONSUMER;
        }

        return std::make_unique<iox::popo::UntypedPublisher>(
            iox::capro::ServiceDescription{"SyntalosModule", modId, channelName}, publisherOptn);
    }

    template<typename Req, typename Res>
    std::unique_ptr<iox::popo::Server<Req, Res>> makeServer(const iox::cxx::string<100> &callName)
    {
        return std::make_unique<iox::popo::Server<Req, Res>>(
            iox::capro::ServiceDescription{"SyntalosModule", modId, callName});
    }

    iox::cxx::string<100> modId;

    std::unique_ptr<iox::popo::Publisher<ErrorEvent>> pubError;
    std::unique_ptr<iox::popo::Publisher<StateChangeEvent>> pubState;
    std::unique_ptr<iox::popo::Publisher<StatusMessageEvent>> pubStatusMessage;
    std::unique_ptr<iox::popo::UntypedPublisher> pubNewInPort;
    std::unique_ptr<iox::popo::UntypedPublisher> pubNewOutPort;
    std::unique_ptr<iox::popo::Server<SetNicenessRequest, DoneResponse>> reqSetNiceness;
    std::unique_ptr<iox::popo::Server<SetMaxRealtimePriority, DoneResponse>> reqSetMaxRTPriority;
    std::unique_ptr<iox::popo::Server<SetCPUAffinityRequest, DoneResponse>> reqSetCPUAffinity;

    ModuleState state;
    int maxRTPriority;
    std::vector<InputPortInfo> inPortInfo;
    std::vector<OutputPortInfo> outPortInfo;
};

ModuleLink::ModuleLink(const QString &instanceId, QObject *parent)
    : QObject(parent),
      d(new ModuleLink::Private(instanceId))
{
}

ModuleLink::~ModuleLink() {}

void ModuleLink::raiseError(const QString &title, const QString &message)
{
    d->pubError->loan().and_then([&](auto &error) {
        error->title = iox::cxx::string<128>(iox::cxx::TruncateToCapacity, title.toStdString());
        error->message = iox::cxx::string<2048>(iox::cxx::TruncateToCapacity, message.toStdString());
        error.publish();
    });
}

void ModuleLink::raiseError(const QString &message)
{
    d->pubError->loan().and_then([&](auto &error) {
        error->message = iox::cxx::string<2048>(iox::cxx::TruncateToCapacity, message.toStdString());
        error.publish();
    });
}

void ModuleLink::processMessages()
{
    // SetNiceness
    d->reqSetNiceness->take().and_then([&](const auto &request) {
        d->reqSetNiceness->loan(request)
            .and_then([&](auto &response) {
                // apply niceness request immediately to current thread
                response->success = setCurrentThreadNiceness(request->nice);
                if (!response->success)
                    raiseError("Could not set niceness to " + QString::number(request->nice));

                response.send().or_else([&](auto &error) {
                    std::cerr << "Could not respond to SetNiceness! Error: " << error << std::endl;
                });
            })
            .or_else([&](auto &error) {
                std::cerr << "Could not allocate response! Error: " << error << std::endl;
            });
    });

    // SetMaxRealtimePriority
    d->reqSetMaxRTPriority->take().and_then([&](const auto &request) {
        d->reqSetMaxRTPriority->loan(request)
            .and_then([&](auto &response) {
                d->maxRTPriority = request->priority;
                response->success = true;
                response.send().or_else([&](auto &error) {
                    std::cerr << "Could not respond to SetMaxRealtimePriority! Error: " << error << std::endl;
                });
            })
            .or_else([&](auto &error) {
                std::cerr << "Could not allocate response! Error: " << error << std::endl;
            });
    });

    // SetCPUAffinity
    d->reqSetCPUAffinity->take().and_then([&](const auto &request) {
        d->reqSetCPUAffinity->loan(request)
            .and_then([&](auto &response) {
                if (!request->cores.empty()) {
                    thread_set_affinity_from_vec(
                        pthread_self(), std::vector<uint>(request->cores.begin(), request->cores.end()));
                }

                response->success = true;
                response.send().or_else([&](auto &error) {
                    std::cerr << "Could not respond to SetCPUAffinity! Error: " << error << std::endl;
                });
            })
            .or_else([&](auto &error) {
                std::cerr << "Could not allocate response! Error: " << error << std::endl;
            });
    });
}
void ModuleLink::processMessagesForever()
{
    while (!iox::posix::hasTerminationRequested()) {
        processMessages();
    }
}
ModuleState ModuleLink::state() const
{
    return d->state;
}
void ModuleLink::setState(ModuleState state)
{
    d->pubState->loan().and_then([&](auto &sample) {
        sample->state = state;
        sample.publish();
    });

    d->state = state;
}
void ModuleLink::setStatusMessage(const QString &message)
{
    d->pubStatusMessage->loan().and_then([&](auto &sample) {
        sample->text = iox::cxx::string<512>(iox::cxx::TruncateToCapacity, message.toStdString());
        sample.publish();
    });
}
int ModuleLink::maxRealtimePriority() const
{
    return d->maxRTPriority;
}
int ModuleLink::registerInputPort(const QString &idstr, const QString &dataTypeName)
{
    // construct our reference for this port
    InputPortInfo iport;
    iport.idstr = idstr;
    iport.dataTypeName = dataTypeName;
    iport.workerDataTypeId = QMetaType::type(qPrintable(iport.dataTypeName));
    iport.id = static_cast<int>(d->inPortInfo.size());

    QByteArray iportData;
    QBuffer buffer(&iportData);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out << iport;

    // announce the new port to master
    bool haveError = false;
    d->pubNewInPort->loan(iportData.size())
        .and_then([&](auto &payload) {
            // we copy twice here - but this is a low-volume event, so it should be fine
            memcpy(payload, iportData.data(), iportData.size());
            d->pubNewInPort->publish(payload);
        })
        .or_else([&](auto &error) {
            std::cerr << "Unable to loan sample. Error: " << error << std::endl;
            haveError = true;
        });

    if (haveError) {
        return -1;
    } else {
        d->inPortInfo.push_back(iport);
        return iport.id;
    }
}
int ModuleLink::registerOutputPort(const QString &idstr, const QString &dataTypeName, const QVariantHash &metadata)
{
    // construct our reference for this port
    OutputPortInfo oport;
    oport.idstr = idstr;
    oport.dataTypeName = dataTypeName;
    oport.metadata = metadata;
    oport.workerDataTypeId = QMetaType::type(qPrintable(oport.dataTypeName));
    oport.id = static_cast<int>(d->outPortInfo.size());

    QByteArray oportData;
    QBuffer buffer(&oportData);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out << oport;

    // announce the new port to master
    bool haveError = false;
    d->pubNewOutPort->loan(oportData.size())
        .and_then([&](auto &payload) {
            // we copy twice here - but this is a low-volume event, so it should be fine
            memcpy(payload, oportData.data(), oportData.size());
            d->pubNewOutPort->publish(payload);
        })
        .or_else([&](auto &error) {
            std::cerr << "Unable to loan sample. Error: " << error << std::endl;
            haveError = true;
        });

    if (haveError) {
        return -1;
    } else {
        d->outPortInfo.push_back(oport);
        return oport.id;
    }
}

} // namespace Syntalos
