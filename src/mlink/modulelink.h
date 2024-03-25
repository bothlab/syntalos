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
#include <moduleapi.h>

namespace Syntalos
{

class ModuleLink : public QObject
{
    Q_GADGET
public:
    ~ModuleLink();

    void raiseError(const QString &message);
    void raiseError(const QString &title, const QString &message);

    void processMessages();
    void processMessagesForever();

    ModuleState state() const;
    void setState(ModuleState state);

    void setStatusMessage(const QString &message);

    int maxRealtimePriority() const;

    int registerInputPort(const QString &idstr, const QString &dataTypeName);
    int registerOutputPort(
        const QString &idstr,
        const QString &dataTypeName,
        const QVariantHash &metadata = QVariantHash());

Q_SIGNALS:

private:
    explicit ModuleLink(const QString &instanceId, QObject *parent = nullptr);
    friend std::unique_ptr<ModuleLink> initModuleLink();

private:
    class Private;
    Q_DISABLE_COPY(ModuleLink)
    QScopedPointer<Private> d;
};

std::unique_ptr<ModuleLink> initModuleLink();

} // namespace Syntalos
