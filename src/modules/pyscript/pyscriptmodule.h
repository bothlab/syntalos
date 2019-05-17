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

#ifndef PYSCRIPTMODULE_H
#define PYSCRIPTMODULE_H

#include <QObject>
#include <chrono>
#include "abstractmodule.h"

class ZmqServer;
class QProcess;
class QTextBrowser;
class MaFuncRelay;

namespace KTextEditor {
class View;
}

class PyScriptModule : public AbstractModule
{
    Q_OBJECT
public:
    friend MaFuncRelay;

    explicit PyScriptModule(QObject *parent = nullptr);
    ~PyScriptModule() override;

    QString id() const override;
    QString description() const override;
    QPixmap pixmap() const override;

    bool initialize(ModuleManager *manager) override;
    bool prepare(const QString& storageRootDir, const TestSubject& testSubject, HRTimer *timer) override;
    void start() override;
    bool runCycle() override;
    void stop() override;

private:
    ZmqServer *m_zserver;
    QString m_workerBinary;
    QProcess *m_process;
    MaFuncRelay *m_funcRelay;
    ModuleManager *m_modManager;
    bool m_running;

    QTextBrowser *m_pyoutWindow;
    KTextEditor::View *m_scriptView;
    QWidget *m_scriptWindow;
};

#endif // PYSCRIPTMODULE_H
