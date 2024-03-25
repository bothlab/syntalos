/*
 * Copyright (C) 2016-2024 Matthias Klumpp <matthias@tenstral.net>
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

#include <QEventLoop>
#include <QObject>

#include "moduleapi.h"

namespace Syntalos
{
Q_DECLARE_LOGGING_CATEGORY(logMLinkMod)
}

/**
 * @brief Master link for out-of-process modules
 */
class MLinkModule : public AbstractModule
{
    Q_OBJECT
public:
    explicit MLinkModule(QObject *parent = nullptr);
    virtual ~MLinkModule() override;

    virtual ModuleFeatures features() const override;

    virtual bool prepare(const TestSubject &) override;

    void testProcess();

private:
    template<typename T>
    std::unique_ptr<T> makeSubscriber(const QString &eventName);

private:
    class Private;
    Q_DISABLE_COPY(MLinkModule)
    QScopedPointer<Private> d;
};
