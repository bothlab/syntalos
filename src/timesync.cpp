/*
 * Copyright (C) 2019-2022 Matthias Klumpp <matthias@tenstral.net>
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

#include "timesync.h"

#include <QDebug>
#include <QDateTime>
#include "moduleapi.h"
#include <iostream>

#include "utils/misc.h"

namespace Syntalos {
    Q_LOGGING_CATEGORY(logTimeSync, "time.synchronizer")
}

using namespace Syntalos;
using namespace Eigen;

const QString Syntalos::timeSyncStrategyToHString(const TimeSyncStrategy &strategy)
{
    switch (strategy) {
    case TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD:
        return QStringLiteral("shift timestamps (fwd)");
    case TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD:
        return QStringLiteral("shift timestamps (bwd)");
    case TimeSyncStrategy::ADJUST_CLOCK:
        return QStringLiteral("align secondary clock");
    case TimeSyncStrategy::WRITE_TSYNCFILE:
        return QStringLiteral("write time-sync file");
    default:
        return QStringLiteral("invalid");
    }
}

const QString Syntalos::timeSyncStrategiesToHString(const TimeSyncStrategies &strategies)
{
    QStringList sl;

    if (strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD) && strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD)) {
        sl.append(QStringLiteral("shift timestamps"));
    } else {
        if (strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD))
            sl.append(timeSyncStrategyToHString(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD));
        if (strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD))
            sl.append(timeSyncStrategyToHString(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD));
    }
    if (strategies.testFlag(TimeSyncStrategy::ADJUST_CLOCK))
        sl.append(timeSyncStrategyToHString(TimeSyncStrategy::ADJUST_CLOCK));
    if (strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE))
        sl.append(timeSyncStrategyToHString(TimeSyncStrategy::WRITE_TSYNCFILE));

    return sl.join(" and ");
}

// -----------------------
// FreqCounterSynchronizer
// -----------------------

FreqCounterSynchronizer::FreqCounterSynchronizer(std::shared_ptr<SyncTimer> masterTimer,
                                                 AbstractModule *mod,
                                                 double frequencyHz,
                                                 const QString &id)
    : m_mod(mod),
      m_id(id),
      m_strategies(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD | TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD),
      m_lastOffsetEmission(0),
      m_syTimer(masterTimer),
      m_toleranceUsec(SECONDARY_CLOCK_TOLERANCE.count()),
      m_calibrationMaxBlockN(500),
      m_calibrationIdx(0),
      m_haveExpectedOffset(false),
      m_freq(frequencyHz),
      m_lastValidMasterTimestamp(0),
      m_tswriter(new TimeSyncFileWriter)
{
    if (m_id.isEmpty())
        m_id = createRandomString(4);

    // time one datapoint takes to acquire, if the frequency in Hz is accurate, in microseconds
    m_timePerPointUs = (1.0 / m_freq) * 1000.0 * 1000.0;

    // make our existence known to the system
    if (m_mod != nullptr)
        emit m_mod->synchronizerDetailsChanged(m_id, m_strategies, std::chrono::microseconds(m_toleranceUsec));
}

FreqCounterSynchronizer::~FreqCounterSynchronizer()
{
    stop();
}

int FreqCounterSynchronizer::indexOffset() const
{
    return m_indexOffset;
}

void FreqCounterSynchronizer::setCalibrationBlocksCount(int count)
{
    if (count <= 0)
        count = 24;
    m_calibrationMaxBlockN = count;
}

void FreqCounterSynchronizer::setTimeSyncBasename(const QString &fname, const QUuid &collectionId)
{
    m_collectionId = collectionId;
    m_tswriter->setFileName(fname);
    m_strategies = m_strategies.setFlag(TimeSyncStrategy::WRITE_TSYNCFILE, !fname.isEmpty());
}

/**
 * @brief Set the last known valid master timestamp.
 *
 * This is a hack in case the timestamp-generating module generates timestamps that
 * it may not write to disk.
 */
void FreqCounterSynchronizer::setLastValidMasterTimestamp(microseconds_t masterTimestamp)
{
    m_lastValidMasterTimestamp = masterTimestamp;
}

microseconds_t FreqCounterSynchronizer::lastMasterAssumedAcqTS() const
{
    return m_lastMasterAssumedAcqTS;
}

bool FreqCounterSynchronizer::isCalibrated() const
{
    return m_haveExpectedOffset;
}

void FreqCounterSynchronizer::setStrategies(const TimeSyncStrategies &strategies)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected strategy change on active FreqCounter Synchronizer for"
                                         << m_mod->name();
        return;
    }
    m_strategies = strategies;
    if (m_mod != nullptr)
        emit m_mod->synchronizerDetailsChanged(m_id, m_strategies, std::chrono::microseconds(m_toleranceUsec));
}

void FreqCounterSynchronizer::setTolerance(const std::chrono::microseconds &tolerance)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected tolerance change on active FreqCounter Synchronizer for"
                                         << m_mod->name();
        return;
    }
    m_toleranceUsec = tolerance.count();
    if (m_mod != nullptr)
        emit m_mod->synchronizerDetailsChanged(m_id, m_strategies, std::chrono::microseconds(m_toleranceUsec));
}

bool FreqCounterSynchronizer::start()
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Restarting a FreqCounter Synchronizer that has already been used is not permitted. This is an issue in"
                                         << m_mod->name();
        return false;
    }
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        m_tswriter->setSyncMode(TSyncFileMode::SYNCPOINTS);
        m_tswriter->setTimeDataTypes(TSyncFileDataType::INT64, TSyncFileDataType::INT64);
        if (!m_tswriter->open(m_mod->name(), m_collectionId, microseconds_t(m_toleranceUsec))) {
            qCCritical(logTimeSync).noquote().nospace() << "Unable to open timesync file for " << m_mod->name() << "[" << m_id << "]: " << m_tswriter->lastError();
            return false;
        }
    }

    m_lastOffsetWithinTolerance = false;
    m_timeCorrectionOffset = microseconds_t(0);
    m_haveExpectedOffset = false;
    m_calibrationIdx = 0;
    m_expectedOffsetCalCount = 0;
    m_tsOffsetsUsec = VectorXl::Zero(m_calibrationMaxBlockN);
    m_lastTimeIndex = 0;
    m_indexOffset = 0;
    m_offsetChangeWaitBlocks = 0;
    m_applyIndexOffset = false;

    m_tsyncAvgCollectCount = 0;
    m_tsyncAvgVecSize = m_calibrationMaxBlockN / 6;
    if (m_tsyncAvgVecSize < 4)
        m_tsyncAvgVecSize = 4;
    m_tsyncAvgVecMaster = VectorXl::Zero(m_tsyncAvgVecSize);
    m_tsyncAvgVecDevice = VectorXl::Zero(m_tsyncAvgVecSize);

    m_lastSecondaryIdxUnandjusted = 0;
    m_lastMasterAssumedAcqTS = microseconds_t(0);

    // sanity checks for programming mistakes
    assert(m_tsyncAvgVecSize < m_calibrationMaxBlockN);
    assert(m_tsyncAvgVecMaster.size() == m_tsyncAvgVecDevice.size());
    assert(m_tsyncAvgVecDevice.size() == m_tsyncAvgVecSize);
    assert(m_calibrationMaxBlockN > 1);

    return true;
}

void FreqCounterSynchronizer::stop()
{
    // Write the last timestamp, even if it was not out of tolerance.
    // This (for the most part) removes some guesswork and extrapolation in post-processing
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        if (m_lastSecondaryIdxUnandjusted != 0 && m_lastMasterAssumedAcqTS.count() != 0) {
            auto offset = m_lastValidMasterTimestamp - m_lastMasterAssumedAcqTS;
            // we do not allow an to jump forward in time via the offset
            if (offset.count() > 0)
                offset = microseconds_t(0);
            else if (offset.count() != 0)
                qCDebug(logTimeSync).noquote() << "Cutting off" << offset.count() << "µs from timesync file to align endpoint for" << m_mod->name();

            m_tswriter->writeTimes(microseconds_t(std::lround((m_lastSecondaryIdxUnandjusted + 1) * m_timePerPointUs)) + offset,
                                   m_lastMasterAssumedAcqTS + offset);
        }
        m_lastValidMasterTimestamp = microseconds_t(0);
        m_lastMasterAssumedAcqTS = microseconds_t(0);
    }

    m_tswriter->close();
}

void FreqCounterSynchronizer::processTimestamps(const microseconds_t &blocksRecvTimestamp,
                                                int blockIndex, int blockCount, VectorXu &idxTimestamps)
{
    // basic input value sanity checks
    assert(blockCount >= 1);
    assert(blockIndex >= 0);
    assert(blockIndex < blockCount);

    // get last index value of vector before we made any adjustments to it
    const auto secondaryLastIdxUnadjusted = idxTimestamps[idxTimestamps.rows() - 1];
    m_lastSecondaryIdxUnandjusted = secondaryLastIdxUnadjusted;

    // adjust timestamp based on our current offset
    if (m_applyIndexOffset && (m_indexOffset != 0))
        idxTimestamps -= VectorXu::Ones(idxTimestamps.rows()) * m_indexOffset;

    // timestamp when (as far and well as we can guess...) the current block was actually acquired, in microseconds
    // and based on the master clock timestamp generated upon data receival.
    const microseconds_t masterAssumedAcqTS = blocksRecvTimestamp
                                                - microseconds_t(std::lround(m_timePerPointUs * ((blockCount - 1) * idxTimestamps.rows())))
                                                + microseconds_t(std::lround(m_timePerPointUs * (blockIndex * idxTimestamps.rows())));
    m_lastMasterAssumedAcqTS = masterAssumedAcqTS;

    // value of the last entry of the current block
    const auto secondaryLastIdx = idxTimestamps[idxTimestamps.rows() - 1];

    // Timestamp, in microseconds, when according to the device frequency the last datapoint of this block was acquired
    // since we assume a zero-indexed time series, we need to add one to the secondary index
    // If the index offset has already been applied, take the value as-is, otherwise apply our current offset even if
    // modifications to the data are not permitted (we need the corrected last timestamp here, even if we don't apply
    // it to the output data and are just writing a tsync file)
    const auto secondaryLastTS = m_applyIndexOffset?
                                    microseconds_t(std::lround((secondaryLastIdx + 1) * m_timePerPointUs)) :
                                    microseconds_t(std::lround((secondaryLastIdxUnadjusted + 1 - m_indexOffset) * m_timePerPointUs));

    // calculate time offset
    const int64_t curOffsetUsec = (secondaryLastTS - masterAssumedAcqTS).count();

    // add new datapoint to our "memory" vector
    m_tsOffsetsUsec[m_calibrationIdx++] = curOffsetUsec;
    if (m_calibrationIdx >= m_calibrationMaxBlockN)
        m_calibrationIdx = 0;

    // calculate offsets and offset expectation delta
    const int64_t avgOffsetUsec = m_tsOffsetsUsec.mean();
    const int64_t avgOffsetDeviationUsec = avgOffsetUsec - m_expectedOffset.count();

    // we do nothing more until we have enought measurements to estimate the "natural" timer offset
    // of the secondary clock and master clock
    if (!m_haveExpectedOffset) {
        m_expectedOffsetCalCount++;

        // we want a bit more values than needed for perpetual calibration, because the first
        // few values in the vector stem from the initialization phase of Syntalos and may have
        // a higher variance than actually expected during normal operation (as in the startup
        // phase, the system load is high and lots of external devices are starting up)
        if (m_expectedOffsetCalCount < (m_calibrationMaxBlockN * 2))
            return;

        m_expectedSD = sqrt(vectorVariance(m_tsOffsetsUsec));
        m_expectedOffset = microseconds_t(std::lround(vectorMedian(m_tsOffsetsUsec)));

        qCDebug(logTimeSync).noquote().nospace() << QTime::currentTime().toString() << "[" << m_id << "] "
                << "Determined expected time offset: " << m_expectedOffset.count() << "µs "
                << "SD: " << m_expectedSD;
        m_haveExpectedOffset = true;

        // send (possibly initial) offset info to the controller)
        if (m_mod != nullptr)
            emit m_mod->synchronizerOffsetChanged(m_id, microseconds_t(avgOffsetUsec - m_expectedOffset.count()));

        // If we are writing a timesync-file, always write the time when the very first
        // datapoint was acquired as first value.
        // We used the calibration phase to just guess the offset between the counting clock and master
        // clock by calculating backwards.
        if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE))
            m_tswriter->writeTimes(0,
                                   m_expectedOffset * -1);

        m_lastTimeIndex = secondaryLastIdx;
        return;
    }

    // we added a new block, so remove one from the wait counter that's used
    // to wait for new data after a time adjustment was made.
    if (m_offsetChangeWaitBlocks > 0)
        m_offsetChangeWaitBlocks--;

    // check if we are supposed to write an offset sync timepoint to the tsync file, and if so complete
    // the averaging vector for the final time and eventually write the data
    if (m_tsyncAvgCollectCount > 0) {
        m_tsyncAvgCollectCount--;
        m_tsyncAvgVecDevice[m_tsyncAvgCollectCount] = std::lround((secondaryLastIdxUnadjusted + 1) * m_timePerPointUs);
        m_tsyncAvgVecMaster[m_tsyncAvgCollectCount] = masterAssumedAcqTS.count();
        if (m_tsyncAvgCollectCount == 0)
            m_tswriter->writeTimes(std::lround(vectorMedianInplace(m_tsyncAvgVecDevice)),
                                   std::lround(vectorMedianInplace(m_tsyncAvgVecMaster)));
    }

    // do nothing if we have not enough average deviation from the norm
    if (abs(avgOffsetDeviationUsec) < m_toleranceUsec) {
        // we are within tolerance range!
        // share the good news with the controller! (immediately on change, or every 30sec otherwise)
        if ((blockIndex == 0) &&
                ((!m_lastOffsetWithinTolerance)
                 || (blocksRecvTimestamp.count() > (m_lastOffsetEmission.count() + (30 * 1000 * 1000))))) {
            if (m_mod != nullptr)
                emit m_mod->synchronizerOffsetChanged(m_id, microseconds_t(avgOffsetDeviationUsec));
            m_lastOffsetEmission = blocksRecvTimestamp;
        }

        // check if we would still be within half-tolerance if we did reset the index offset completely, and if that's the case
        // reset it as the external clock for some reason may be accurate again
        if ((m_indexOffset != 0) && (abs(avgOffsetDeviationUsec + m_timeCorrectionOffset.count()) < (m_toleranceUsec / 2))) {
            m_indexOffset = m_indexOffset / 2.0;

            if (m_indexOffset == 0)
                m_timeCorrectionOffset = microseconds_t(0);
            else
                m_timeCorrectionOffset = microseconds_t(static_cast<long>(floor(m_timeCorrectionOffset.count() / 2.0)));
        }

        m_lastOffsetWithinTolerance = true;
        m_lastTimeIndex = secondaryLastIdx;
        return;
    }
    m_lastOffsetWithinTolerance = false;

    const int64_t offsetsSD = sqrt(vectorVariance(m_tsOffsetsUsec, avgOffsetUsec));
    if (abs(avgOffsetUsec - curOffsetUsec) > offsetsSD) {
        // the current offset diff to the moving average offset is not within standard deviation range.
        // This means the data point we just added is likely a fluke, potentially due to a context switch
        // or system load spike. We just ignore those events completely and don't make time adjustments
        // to index offsets based on them.
        m_lastTimeIndex = secondaryLastIdx;
        return;
    }

    // Don't do even more adjustments until we have lived with the current one for a while.
    // Otherwise the system will rapidly shift the index around, often not reaching a stable
    // state anymore.
    if (m_offsetChangeWaitBlocks > 0) {
        m_lastTimeIndex = secondaryLastIdx;
        return;
    }

    // Emit offset information to the main controller about every 10sec or slower
    // in case we run at slower speeds
    if ((blockIndex == 0) && (masterAssumedAcqTS.count() > (m_lastOffsetEmission.count() + (10 * 1000 * 1000)))) {
        if (m_mod != nullptr)
            emit m_mod->synchronizerOffsetChanged(m_id, microseconds_t(avgOffsetDeviationUsec));
        m_lastOffsetEmission = blocksRecvTimestamp;
    }

    // calculate time-based correction offset, a bit less than half of the needed correction time
    m_timeCorrectionOffset = microseconds_t(static_cast<int64_t>(std::floor(avgOffsetDeviationUsec / 2.5)));

    // sanity check: we need to correct by at least one datapoint for any synchronization to occur at all
    if (abs(m_timeCorrectionOffset.count()) <= m_timePerPointUs)
        m_timeCorrectionOffset = microseconds_t(static_cast<int64_t>(ceil(m_timePerPointUs)));

    // translate the clock update offset to indices. We round up here as we are already below threshold,
    // and overshooting slightly appears to be the better solution than being too conservative
    const bool initialOffset = m_indexOffset == 0;
    const int newIndexOffset = static_cast<int64_t>((m_timeCorrectionOffset.count() / 1000.0 / 1000.0) * m_freq);

    // only make adjustments (and potentially write to a tsync file) if we actually changed
    // the index offset and not just the time value associated with it
    // (this may result in less accurate, but also within-tolerance and less noisy tsync files)
    if (m_indexOffset == newIndexOffset) {
        m_lastTimeIndex = secondaryLastIdx;
        return;
    }
    m_indexOffset = std::lround(((newIndexOffset * 2) + m_indexOffset) / 3.0);

    if (m_indexOffset != 0) {
        m_offsetChangeWaitBlocks = m_calibrationMaxBlockN * 1.2;

        m_applyIndexOffset = false;
        if (m_strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD)) {
            if (m_indexOffset > 0)
                m_applyIndexOffset = true;
        }
        if (m_strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD)) {
            if (m_indexOffset < 0)
                m_applyIndexOffset = true;
        }

        // already apply offset as gradient to the current vector, if we are permitted to make that change
        if (initialOffset && m_applyIndexOffset)
            idxTimestamps -= VectorXu::LinSpaced(idxTimestamps.rows(), 0, m_indexOffset);
    }

    // We're out of sync, if we are writing a tsync file we will have to record that fact.
    // Since the master time is subjected to imprecision due to e.g. USB communication and system load,
    // and since by using rare-ish sync points individual points matter a lot more than the would otherwise,
    // we record a bit more clock data and write the average master time to the tsync file later, instead of
    // writing the current master time immediately.
    // NOTE: we have to use the unadjusted time value for the device clock - since we didn't need that until now,
    // we calculate it here from the unadjusted last index value of the current block.
    // 1 is added to secondaryLastIdxUnadjusted becuse the timestamp reflects the time *after* a sample was acquired,
    // so the zero-index needs to be offset by one.
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        m_tsyncAvgCollectCount = m_tsyncAvgVecSize - 1;
        m_tsyncAvgVecDevice[m_tsyncAvgCollectCount] = std::lround((secondaryLastIdxUnadjusted + 1) * m_timePerPointUs);
        m_tsyncAvgVecMaster[m_tsyncAvgCollectCount] = masterAssumedAcqTS.count();
    }

    m_lastTimeIndex = secondaryLastIdx;
}


// --------------------------
// SecondaryClockSynchronizer
// --------------------------

SecondaryClockSynchronizer::SecondaryClockSynchronizer(std::shared_ptr<SyncTimer> masterTimer,
                                                       AbstractModule *mod,
                                                       const QString &id)
    : m_mod(mod),
      m_id(id),
      m_strategies(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD | TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD),
      m_lastOffsetEmission(0),
      m_syTimer(masterTimer),
      m_toleranceUsec(SECONDARY_CLOCK_TOLERANCE.count()),
      m_calibrationMaxN(500),
      m_calibrationIdx(0),
      m_haveExpectedOffset(false),
      m_tswriter(new TimeSyncFileWriter)
{
    if (m_id.isEmpty())
        m_id = createRandomString(4);

    // make our existence known to the system
    emitSyncDetailsChanged();
}

SecondaryClockSynchronizer::~SecondaryClockSynchronizer()
{
    stop();
}

microseconds_t SecondaryClockSynchronizer::clockCorrectionOffset() const
{
    return m_clockCorrectionOffset;
}

void SecondaryClockSynchronizer::setCalibrationPointsCount(int timepointCount)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected calibration point count change on active Clock Synchronizer for" << m_mod->name();
        return;
    }
    if (timepointCount > 10)
        m_calibrationMaxN = timepointCount;
}

void SecondaryClockSynchronizer::setExpectedClockFrequencyHz(double frequency)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected frequency change on active Clock Synchronizer for" << m_mod->name();
        return;
    }

    if (frequency <= 0) {
        qCWarning(logTimeSync).noquote() << "Rejected bogus frequency change to <= 0 for" << m_mod->name();
        return;
    }

    // the amount of datapoints needed is on a curve, approaching 5 sec (or minmal required time)
    // if we get a lot of points in a short time, we don't need to wait that long to calculate the
    // average offset, but with a low frequency of new points we need a bit more data to calculate
    // the averages and their SD reliably
    m_calibrationMaxN = frequency * (5 + (30 / ((0.02 * frequency) + 1.4)));

    // set tolerance of half the time one sample takes to be acquired
    m_toleranceUsec = std::lround(((1000.0 / frequency) / 2) * 1000.0);
    emitSyncDetailsChanged();
}

void SecondaryClockSynchronizer::setTimeSyncBasename(const QString &fname, const QUuid &collectionId)
{
    m_collectionId = collectionId;
    m_tswriter->setFileName(fname);
    m_strategies = m_strategies.setFlag(TimeSyncStrategy::WRITE_TSYNCFILE, !fname.isEmpty());
}

bool SecondaryClockSynchronizer::isCalibrated() const
{
    return m_haveExpectedOffset;
}

microseconds_t SecondaryClockSynchronizer::expectedOffsetToMaster() const
{
    return m_expectedOffset;
}

void SecondaryClockSynchronizer::setStrategies(const TimeSyncStrategies &strategies)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected strategy change on active Clock Synchronizer for" << m_mod->name();
        return;
    }
    m_strategies = strategies;
    emitSyncDetailsChanged();
}

void SecondaryClockSynchronizer::setTolerance(const microseconds_t &tolerance)
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Rejected tolerance change on active Clock Synchronizer for" << m_mod->name();
        return;
    }
    m_toleranceUsec = tolerance.count();
    emitSyncDetailsChanged();
}

bool SecondaryClockSynchronizer::start()
{
    if (m_haveExpectedOffset) {
        qCWarning(logTimeSync).noquote() << "Restarting a Clock Synchronizer that has already been used is not permitted. This is an issue in " << m_mod->name();
        return false;
    }
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        m_tswriter->setSyncMode(TSyncFileMode::SYNCPOINTS);
        m_tswriter->setTimeDataTypes(TSyncFileDataType::INT64, TSyncFileDataType::INT64);
        if (!m_tswriter->open(m_mod->name(), m_collectionId, microseconds_t(m_toleranceUsec))) {
            qCCritical(logTimeSync).noquote().nospace() << "Unable to open timesync file for " << m_mod->name() << "[" << m_id << "]: " << m_tswriter->lastError();
            return false;
        }
    }

    if (m_calibrationMaxN <= 4)
        qCCritical(logTimeSync).noquote().nospace() << "Clock synchronizer for " << m_mod->name() << "[" << m_id << "] uses a tiny calibration array (length <= 4)";
    assert(m_calibrationMaxN > 0);

    m_lastOffsetWithinTolerance = false;
    m_clockCorrectionOffset = microseconds_t(0);
    m_haveExpectedOffset = false;
    m_calibrationIdx = 0;
    m_expectedOffsetCalCount = 0;
    m_clockOffsetsUsec = VectorXl::Zero(m_calibrationMaxN);
    m_lastMasterTS = m_syTimer->timeSinceStartMsec();
    m_lastSecondaryAcqTS = microseconds_t(0);

    return true;
}

void SecondaryClockSynchronizer::stop()
{
    // write the last acquired timestamp pair, to simplify data post processing
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        if (m_lastSecondaryAcqTS.count() != 0)
            m_tswriter->writeTimes(m_lastSecondaryAcqTS, m_lastMasterTS);
    }

    m_tswriter->close();
}

void SecondaryClockSynchronizer::processTimestamp(microseconds_t &masterTimestamp, const microseconds_t &secondaryAcqTimestamp)
{
    const long long curOffsetUsec = (secondaryAcqTimestamp - masterTimestamp).count();

    // calculate offsets without the new datapoint included
    const auto avgOffsetUsec = m_clockOffsetsUsec.mean();
    const auto avgOffsetDeviationUsec = avgOffsetUsec - m_expectedOffset.count();
    const auto offsetsSD = sqrt(vectorVariance(m_clockOffsetsUsec, avgOffsetUsec));

    // add new datapoint to our "memory" vector
    m_clockOffsetsUsec[m_calibrationIdx++] = curOffsetUsec;
    if (m_calibrationIdx >= m_calibrationMaxN)
        m_calibrationIdx = 0;

    // we do nothing more until we have enought measurements to estimate the "natural" timer offset
    // of the secondary clock and master clock
    if (!m_haveExpectedOffset) {
        m_expectedOffsetCalCount++;

        // always store initial offset in file, if we are writing a tsync file
        if (m_expectedOffsetCalCount == 1) {
            if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE))
                m_tswriter->writeTimes(secondaryAcqTimestamp, masterTimestamp);
        }

        // we want a bit more values than needed for perpetual calibration, because the first
        // few values in the vector stem from the initialization phase of Syntalos and may have
        // a higher variance than actually expected during normal operation (as in the startup
        // phase, the system load is high and lots of external devices are starting up)
        if (m_expectedOffsetCalCount < (m_calibrationMaxN * 2))
            return;

        m_expectedSD = sqrt(vectorVariance(m_clockOffsetsUsec));
        m_expectedOffset = microseconds_t(std::lround(vectorMedian(m_clockOffsetsUsec)));

        qCDebug(logTimeSync).noquote().nospace() << QTime::currentTime().toString() << "[" << m_id << "] "
                << "Determined expected time offset: " << m_expectedOffset.count() << "µs "
                << "SD: " << m_expectedSD;
        m_haveExpectedOffset = true;

        // if we are writing a timesync-file, write the initial two timestamps when we
        // calibrated the system to the file (as additional verification point)
        if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE))
            m_tswriter->writeTimes(secondaryAcqTimestamp, masterTimestamp);

        // remember the secondary clock timestamp & master timestamp for the next iteration
        m_lastSecondaryAcqTS = secondaryAcqTimestamp;
        m_lastMasterTS = masterTimestamp;
        return;
    }

    if (abs(avgOffsetUsec - curOffsetUsec) > offsetsSD) {
        // the current offset diff to the moving average offset is not within standard deviation range.
        // This means the data point we just added is likely a fluke, potentially due to a context switch
        // or system load spike. We correct those unconditionally.
        masterTimestamp = microseconds_t(std::lround(((secondaryAcqTimestamp.count() - m_expectedOffset.count())
                                                 + (secondaryAcqTimestamp.count() - avgOffsetUsec)) / 2.0));

        /*
        qCDebug(logTimeSync).noquote().nospace() << QTime::currentTime().toString() << "[" << m_id << "] "
                << "Offset deviation diff not within SD. Adjusted for fluke offset by adding " << avgOffsetUsec*-1 << "µs "
                << "to secondary clock time " << secondaryAcqTimestamp.count() << "µs "
                << " SD: " << offsetsSD;
        */
    } else {
        // everything is normal, and we assume here that all values are within tolerance. Recalculate
        // the master timestamp based on that assumption as average between expected master timestamp
        // based on expected offset and the actual, measured master timestamp.
        masterTimestamp = microseconds_t(std::lround(((secondaryAcqTimestamp.count() - m_expectedOffset.count()) + masterTimestamp.count()) / 2.0));
    }

    // ensure time doesn't run backwards - at this point, this event may
    // only happen if the secondary clock  gives us the exact same
    // timestamp twice in a row.
    if (masterTimestamp < m_lastMasterTS)
        masterTimestamp = m_lastMasterTS;

    // do nothing if we have not enough average deviation from the norm
    if (abs(avgOffsetDeviationUsec) < m_toleranceUsec) {
        // we are within tolerance range!
        // share the good news with the controller! (immediately on change, or every 30sec otherwise)
        if ((!m_lastOffsetWithinTolerance) || (masterTimestamp.count() > (m_lastOffsetEmission.count() + (30 * 1000 * 1000)))) {
            if (m_mod != nullptr)
                emit m_mod->synchronizerOffsetChanged(m_id, microseconds_t(avgOffsetDeviationUsec));
            m_lastOffsetEmission = masterTimestamp;
        }
        m_lastOffsetWithinTolerance = true;
        m_clockCorrectionOffset = microseconds_t(0);

        // remember the secondary clock timestamp & master timestamp for the next iteration
        m_lastSecondaryAcqTS = secondaryAcqTimestamp;
        m_lastMasterTS = masterTimestamp;
        return;
    }
    m_lastOffsetWithinTolerance = false;

    // Emit offset information to the main controller about every 10sec or slower
    // in case we run at slower speeds
    if (masterTimestamp.count() > (m_lastOffsetEmission.count() + (10 * 1000 * 1000))) {
        if (m_mod != nullptr)
            emit m_mod->synchronizerOffsetChanged(m_id, microseconds_t(avgOffsetDeviationUsec));
        m_lastOffsetEmission = masterTimestamp;
    }

    // try to adjust a potential external clock slowly (and also adjust our timestamps slowly)
    const auto newClockCorrectionOffset = microseconds_t(static_cast<long>(
                                                             std::floor(((m_clockCorrectionOffset.count() * 15)
                                                                         + avgOffsetDeviationUsec) / (15 + 1.0))
                                                        ));

    // write offset info to tsync file before we make any adjustments to the master timestamp
    if (m_strategies.testFlag(TimeSyncStrategy::WRITE_TSYNCFILE)) {
        if (newClockCorrectionOffset != m_clockCorrectionOffset)
            m_tswriter->writeTimes(secondaryAcqTimestamp, masterTimestamp);
    }

    m_clockCorrectionOffset = newClockCorrectionOffset;

    // the clock is out of sync, let's make adjustments!

    if (m_strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_BWD)) {
        if (m_clockCorrectionOffset.count() > 0)
            masterTimestamp = secondaryAcqTimestamp - microseconds_t(avgOffsetUsec) - microseconds_t(m_clockCorrectionOffset);
    }
    if (m_strategies.testFlag(TimeSyncStrategy::SHIFT_TIMESTAMPS_FWD)) {
        if (m_clockCorrectionOffset.count() < 0)
            masterTimestamp = secondaryAcqTimestamp - microseconds_t(avgOffsetUsec) - microseconds_t(m_clockCorrectionOffset);
    }

    /*
    qCDebug(logTimeSync).noquote().nospace() << "[" << m_id << "] "
            << "Clocks out of sync. Mean offset deviation: " << avgOffsetDeviationUsec << "µs, "
            << "Current: " << (curOffsetUsec - m_expectedOffset.count()) << "µs, "
            << "Active SD: " << offsetsSD << " "
            << "Correction offset: " << m_clockCorrectionOffset.count() << "µs";
    */

    // ensure time doesn't run backwards - this really shouldn't happen at this
    // point, but we prevent this just in case
    if (masterTimestamp < m_lastMasterTS) {
        qCWarning(logTimeSync).noquote().nospace() << "[" << m_id << "] "
                << "Timestamp moved backwards when calculating adjusted new time: "
                << masterTimestamp.count() << " !< " << m_lastMasterTS.count() << " (mitigated by reusing previous time)";
        masterTimestamp = m_lastMasterTS;
    }

    // remember the secondary clock timestamp & master timestamp for the next iteration
    m_lastSecondaryAcqTS = secondaryAcqTimestamp;
    m_lastMasterTS = masterTimestamp;
}

void SecondaryClockSynchronizer::emitSyncDetailsChanged()
{
    if (m_mod != nullptr)
        emit m_mod->synchronizerDetailsChanged(m_id, m_strategies, microseconds_t(m_toleranceUsec));
}
