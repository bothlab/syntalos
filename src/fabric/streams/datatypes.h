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

#include <QDataStream>
#include <QMetaType>
#include <memory>

#include "stream.h"
#include "syclock.h"

using namespace Syntalos;

Q_DECLARE_SMART_POINTER_METATYPE(std::shared_ptr)

/**
 * Helpers to (de)serialize enum classes into streams, in case
 * we are compiling with older versions of Qt.
 */
#if (QT_VERSION < QT_VERSION_CHECK(5, 14, 0))
template<typename T>
typename std::enable_if<std::is_enum<T>::value, QDataStream &>::type &operator<<(QDataStream &s, const T &t)
{
    return s << static_cast<typename std::underlying_type<T>::type>(t);
}

template<typename T>
typename std::enable_if<std::is_enum<T>::value, QDataStream &>::type &operator>>(QDataStream &s, T &t)
{
    return s >> reinterpret_cast<typename std::underlying_type<T>::type &>(t);
}
#endif

/**
 * @brief Connection heat level
 *
 * Warning level dependent on how full the buffer that is
 * repesented by a connection is.
 * A high heat means lots of pending stuff and potentially a
 * slow receiving module or not enough system resources.
 * This state is managed internally by Syntalos.
 */
enum class ConnectionHeatLevel {
    NONE,
    LOW,
    MEDIUM,
    HIGH
};
Q_DECLARE_METATYPE(ConnectionHeatLevel)

QString connectionHeatToHumanString(ConnectionHeatLevel heat);

/**
 * @brief The ModuleState enum
 *
 * Describes the state a module can be in. The state is usually displayed
 * to the user via a module indicator widget.
 */
enum class ModuleState : uint16_t {
    UNKNOWN,      /// Module is in an unknown state
    INITIALIZING, /// Module is initializing after being added
    IDLE,         /// Module is inactive and not started
    PREPARING,    /// Module is preparing a run
    READY,        /// Everything is prepared, we are ready to start
    RUNNING,      /// Module is running
    ERROR         /// Module failed to run / is in an error state
};
Q_DECLARE_METATYPE(ModuleState)

/**
 * @brief The ControlCommandKind enum
 *
 * Basic operations to control a module from another module.
 */
enum class ControlCommandKind {
    UNKNOWN,
    START, /// Start an operation
    PAUSE, /// Pause an operation, can be resumed with START
    STOP,  /// Stop an operation
    STEP,  /// Advance operation by one step
    CUSTOM
};
Q_DECLARE_METATYPE(ControlCommandKind)

/**
 * @brief A control command to a module.
 *
 * Generic data type to stream commands to other modules.
 */
struct ControlCommand {
    ControlCommandKind kind; /// The command type
    milliseconds_t duration; /// Duration of the command before resetting to the previous state (zero for infinite)
    QString command;         /// Custom command name, if in custom mode

    explicit ControlCommand()
        : duration(0)
    {
    }
    explicit ControlCommand(ControlCommandKind ckind)
        : kind(ckind),
          duration(0)
    {
    }

    void setDuration(ulong value)
    {
        duration = milliseconds_t(value);
    }

    ulong getDurationAsInt()
    {
        return duration.count();
    }

    friend QDataStream &operator<<(QDataStream &out, const ControlCommand &obj)
    {
        out << obj.kind << (quint64)obj.duration.count() << obj.command;
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, ControlCommand &obj)
    {
        quint64 durationValue;
        in >> obj.kind >> durationValue >> obj.command;
        obj.duration = milliseconds_t(durationValue);
        return in;
    }
};
Q_DECLARE_METATYPE(ControlCommand)

/**
 * @brief A new row  for a table
 *
 * Generic type emitted for adding a table row.
 */
using TableRow = QList<QString>;
Q_DECLARE_METATYPE(TableRow)

/**
 * @brief The FirmataCommandKind enum
 *
 * Set which type of change should be made on a Firmata interface.
 */
enum class FirmataCommandKind {
    UNKNOWN,
    NEW_DIG_PIN,
    NEW_ANA_PIN,
    IO_MODE,
    WRITE_ANALOG,
    WRITE_DIGITAL,
    WRITE_DIGITAL_PULSE,
    SYSEX /// not implemented
};
Q_DECLARE_METATYPE(FirmataCommandKind)

/**
 * @brief Commands to control Firmata output.
 */
struct FirmataControl {
    FirmataCommandKind command;
    uint8_t pinId;
    QString pinName;
    bool isOutput;
    bool isPullUp;
    uint16_t value;

    explicit FirmataControl()
        : isPullUp(false),
          value(0)
    {
    }

    explicit FirmataControl(FirmataCommandKind cmd)
        : command(cmd),
          isPullUp(false),
          value(0)
    {
    }

    friend QDataStream &operator<<(QDataStream &out, const FirmataControl &obj)
    {
        out << obj.command << obj.pinId << obj.pinName << obj.isOutput << obj.isPullUp << obj.value;
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, FirmataControl &obj)
    {
        in >> obj.command >> obj.pinId >> obj.pinName >> obj.isOutput >> obj.isPullUp >> obj.value;
        return in;
    }
};
Q_DECLARE_METATYPE(FirmataControl)

/**
 * @brief Output data returned from a Firmata device.
 */
struct FirmataData {
    uint8_t pinId;
    QString pinName;
    uint16_t value;
    bool isDigital;
    milliseconds_t time;

    friend QDataStream &operator<<(QDataStream &out, const FirmataData &obj)
    {
        out << obj.pinId << obj.pinName << obj.value << obj.isDigital << static_cast<quint32>(obj.time.count());
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, FirmataData &obj)
    {
        quint32 timeMs;
        in >> obj.pinId >> obj.pinName >> obj.value >> obj.isDigital >> timeMs;
        obj.time = milliseconds_t(timeMs);
        return in;
    }
};
Q_DECLARE_METATYPE(FirmataData)

/**
 * @brief Type of a signal from a signal source.
 *
 * This is usually set in the metadata of a data stream.
 */
enum class SignalDataType {
    Amplifier,
    AuxInput,
    SupplyVoltage,
    BoardAdc,
    BoardDigIn,
    BoardDigOut
};
Q_DECLARE_METATYPE(SignalDataType)

/**
 * @brief A block of integer signal data from a data source
 *
 * This signal data block contains data for up to 16 channels. It contains
 * data as integers and is usually used for digital inputs.
 */
class IntSignalBlock
{
public:
    explicit IntSignalBlock(uint sampleCount = 60, uint channelCount = 1)
    {
        Q_ASSERT(channelCount > 0);
        timestamps.resize(sampleCount);
        data.resize(sampleCount, channelCount);
    }

    size_t length() const
    {
        return timestamps.size();
    }

    size_t rows() const
    {
        return data.rows();
    }
    size_t cols() const
    {
        return data.cols();
    }

    VectorXu timestamps;
    MatrixXi data;

    friend QDataStream &operator<<(QDataStream &out, const IntSignalBlock &obj)
    {
        serializeEigen(out, obj.timestamps);
        serializeEigen(out, obj.data);
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, IntSignalBlock &obj)
    {
        obj.timestamps = deserializeEigen<VectorXu>(in);
        obj.data = deserializeEigen<MatrixXi>(in);
        return in;
    }
};
Q_DECLARE_METATYPE(IntSignalBlock)

/**
 * @brief A block of floating-point signal data from an analog data source
 *
 * This signal data block contains data for up to 16 channels. It usually contains
 * possibly preprocessed / prefiltered analog data.
 */
class FloatSignalBlock
{
public:
    explicit FloatSignalBlock(uint sampleCount = 60, uint channelCount = 1)
    {
        Q_ASSERT(channelCount > 0);
        timestamps.resize(sampleCount);
        data.resize(sampleCount, channelCount);
    }

    explicit FloatSignalBlock(const std::vector<float> &floatVec, uint timestamp)
    {
        timestamps.array() += timestamp;
        data.resize(1, floatVec.size());
        for (size_t i = 0; i < floatVec.size(); ++i)
            data(0, i) = floatVec[i];
    }

    size_t length() const
    {
        return timestamps.size();
    }

    size_t rows() const
    {
        return data.rows();
    }
    size_t cols() const
    {
        return data.cols();
    }

    VectorXu timestamps;
    MatrixXd data;

    friend QDataStream &operator<<(QDataStream &out, const FloatSignalBlock &obj)
    {
        serializeEigen(out, obj.timestamps);
        serializeEigen(out, obj.data);
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, FloatSignalBlock &obj)
    {
        obj.timestamps = deserializeEigen<VectorXu>(in);
        obj.data = deserializeEigen<MatrixXd>(in);
        return in;
    }
};
Q_DECLARE_METATYPE(FloatSignalBlock)

/**
 * @brief Helper function to register all meta types for stream data
 *
 * This function registers all types with the meta object system and also
 * creates a global map of all available stream types.
 */
void registerStreamMetaTypes();

/**
 * @brief Get a mapping of type names to their IDs.
 */
QMap<QString, int> streamTypeIdMap();

#ifndef NO_TID_PORTCONSTRUCTORS
#if !defined(DOXYGEN_SHOULD_SKIP_THIS)

class VariantDataStream;
namespace Syntalos
{
class VarStreamInputPort;
class AbstractModule;

/**
 * @brief Create a new DataStream for the type identified by the given ID.
 */
VariantDataStream *newStreamForType(int typeId);

/**
 * @brief Create a new Input Port for the type identified by the given ID.
 */
VarStreamInputPort *newInputPortForType(int typeId, AbstractModule *mod, const QString &id, const QString &title);

} // namespace Syntalos
#endif // DOXYGEN_SHOULD_SKIP_THIS
#endif
