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

#pragma once

#include <QObject>
#include <QVariantHash>
#include <QDataStream>
#include <iceoryx_hoofs/cxx/string.hpp>
#include <iceoryx_hoofs/cxx/vector.hpp>

#include "moduleapi.h"

namespace Syntalos
{

/**
 * @brief Information about an input port
 */
struct InputPortInfo {
    int id;
    QString idstr;
    QString dataTypeName;
    int workerDataTypeId;
    QVariantHash metadata;
    bool connected;

    friend QDataStream &operator<<(QDataStream &out, const InputPortInfo &info)
    {
        out << info.id;
        out << info.idstr;
        out << info.dataTypeName;
        out << info.workerDataTypeId;
        out << info.metadata;
        out << info.connected;

        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, InputPortInfo &info)
    {
        in >> info.id;
        in >> info.idstr;
        in >> info.dataTypeName;
        in >> info.workerDataTypeId;
        in >> info.metadata;
        in >> info.connected;

        return in;
    }
};

/**
 * @brief Information about an output port
 */
struct OutputPortInfo {
    int id;
    QString idstr;
    QString dataTypeName;
    int workerDataTypeId;
    QVariantHash metadata;
    bool connected;

    friend QDataStream &operator<<(QDataStream &out, const OutputPortInfo &info)
    {
        out << info.id;
        out << info.idstr;
        out << info.dataTypeName;
        out << info.workerDataTypeId;
        out << info.metadata;
        out << info.connected;

        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, OutputPortInfo &info)
    {
        in >> info.id;
        in >> info.idstr;
        in >> info.dataTypeName;
        in >> info.workerDataTypeId;
        in >> info.metadata;
        in >> info.connected;

        return in;
    }
};

/**
 * Generic response to a request.
 */
struct DoneResponse {
    bool success;
};

/**
 * Event indicating an error
 */
struct ErrorEvent {
    iox::cxx::string<128> title;
    iox::cxx::string<2048> message;
};

/**
 * Module state change event
 */
struct StateChangeEvent {
    ModuleState state;
};

/**
 * Event sending a status message to master.
 */
struct StatusMessageEvent {
    iox::cxx::string<512> text;
};

/**
 * Request to set the niceness of a worker
 */
struct SetNicenessRequest {
    int nice;
};

/**
 * Request to set the maximum realtime priority of a worker
 */
struct SetMaxRealtimePriority {
    int priority;
};

/**
 * Request to set the CPU affinity of a worker
 */
struct SetCPUAffinityRequest {
    iox::cxx::vector<uint, 256> cores;
};

/**
 * Request to reset the input and/or output ports
 */
struct ResetPortsRequest {
    bool resetInput;
    bool resetOutput;
};

#if 0
    SLOT(void setInputPortInfo(const QList<InputPortInfo> &ports));
    SLOT(void setOutputPortInfo(const QList<OutputPortInfo> &ports));
    SIGNAL(outPortMetadataUpdated(int outPortId, const QVariantHash &metadata));

    SLOT(bool loadPythonScript(const QString &script, const QString &wdir));

    SLOT(bool prepareStart(const QByteArray &settings));
    SLOT(void start(long startTimestampUsec));

    SLOT(bool prepareShutdown());
    SLOT(void shutdown());

    SIGNAL(sendOutput(int outPortId, const QVariant &argData));
    SLOT(bool receiveInput(int inPortId, const QVariant &argData));
    SIGNAL(inputThrottleItemsPerSecRequested(int inPortId, uint itemsPerSec, bool allowMore));

    SLOT(QByteArray changeSettings(const QByteArray &oldSettings));
#endif

} // namespace Syntalos
