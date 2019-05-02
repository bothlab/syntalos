/*
 * Copyright (C) 2016-2019 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRACEPLOTMODULE_H
#define TRACEPLOTMODULE_H

#include <QObject>
#include <chrono>
#include "abstractmodule.h"

class TracePlotProxy;
class TraceDisplay;

class TracePlotModule : public AbstractModule
{
    Q_OBJECT
public:
    explicit TracePlotModule(QObject *parent = nullptr);
    ~TracePlotModule();

    static QString id();
    QString displayName() const;
    QString description() const;
    QPixmap pixmap() const;
    bool singleton() const;

    bool initialize();
    bool prepare(const QString& storageRootDir, const QString& subjectId);
    void stop();

    void showDisplayUi();
    void hideDisplayUi();

private:
    TracePlotProxy *m_traceProxy;
    TraceDisplay *m_displayWindow;
};

#endif // TRACEPLOTMODULE_H
