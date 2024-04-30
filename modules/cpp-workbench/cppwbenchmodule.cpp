/*
 * Copyright (C) 2022-2024 Matthias Klumpp <matthias@tenstral.net>
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

#include "cppwbenchmodule.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <qtermwidget5/qtermwidget.h>
#include <QTabWidget>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QDir>
#include <QFileSystemWatcher>
#include <QProcessEnvironment>

#include "mlinkmodule.h"
#include "porteditordialog.h"
#include "globalconfig.h"
#include "utils/style.h"

SYNTALOS_MODULE(CppWBenchModule);

namespace Syntalos
{
Q_LOGGING_CATEGORY(logCppWB, "mod.cpp-workbench")
}

class CppWBenchModule : public MLinkModule
{
    Q_OBJECT
public:
    explicit CppWBenchModule(QObject *parent = nullptr)
        : MLinkModule(parent),
          m_codeWindow(nullptr)
    {
        GlobalConfig gconf;
        m_cacheRoot = gconf.userCacheDir();

        // set up code editor
        auto editor = KTextEditor::Editor::instance();
        // create a new document
        auto cppDoc = editor->createDocument(this);

        // load templates
        QFile sampleCppRc(QStringLiteral(":/code/example-template.cpp"));
        if (sampleCppRc.open(QIODevice::ReadOnly))
            cppDoc->setText(sampleCppRc.readAll());
        sampleCppRc.close();

        QFile mesonDefTplRc(QStringLiteral(":/code/template.meson"));
        if (mesonDefTplRc.open(QIODevice::ReadOnly))
            m_mesonDefTmpl = mesonDefTplRc.readAll();
        else
            qCCritical(logCppWB, "Failed to load Meson template");
        mesonDefTplRc.close();

        QFile autoBuildTplRc(QStringLiteral(":/code/autobuild.sh"));
        if (autoBuildTplRc.open(QIODevice::ReadOnly))
            m_autobuildScript = autoBuildTplRc.readAll();
        else
            qCCritical(logCppWB, "Failed to load autobuild helper");
        autoBuildTplRc.close();

        // configure UI
        m_codeWindow = new QWidget;
        addDisplayWindow(m_codeWindow);

        m_codeWindow->setWindowIcon(QIcon(":/icons/generic-config"));
        m_codeWindow->setWindowTitle(QStringLiteral("%1 - Editor").arg(name()));

        m_codeView = cppDoc->createView(m_codeWindow);
        cppDoc->setHighlightingMode("C++");

        // set up program output area
        m_consoleTabWidget = new QTabWidget(m_codeWindow);
        m_consoleTabWidget->setTabPosition(QTabWidget::West);

        // configure console widget
        m_termWidget = new QTermWidget(0, m_consoleTabWidget);
        auto terminalTab = new QWidget(m_consoleTabWidget);
        auto terminalLayout = new QVBoxLayout(terminalTab);
        terminalLayout->setMargin(2);
        terminalLayout->addWidget(m_termWidget);
        terminalTab->setLayout(terminalLayout);
        m_consoleTabWidget->addTab(terminalTab, "Terminal");

        // add output box to console tab bar
        auto outputTab = new QWidget(m_consoleTabWidget);
        auto outputLayout = new QVBoxLayout(m_consoleTabWidget);
        outputLayout->setMargin(2);
        m_logWidget = new QTextBrowser(m_consoleTabWidget);
        outputLayout->addWidget(m_logWidget);
        outputTab->setLayout(outputLayout);
        m_consoleTabWidget->addTab(outputTab, "Output");
        m_logWidget->setFontFamily(QStringLiteral("Monospace"));
        auto pal = m_logWidget->palette();
        pal.setColor(QPalette::Text, SyColorWhite);
        pal.setColor(QPalette::Base, SyColorDark);
        m_logWidget->setPalette(pal);

        // combine the UI elements into the main layout
        auto splitter = new QSplitter(Qt::Vertical, m_codeWindow);
        splitter->addWidget(m_codeView);
        splitter->addWidget(m_consoleTabWidget);
        splitter->setStretchFactor(0, 8);
        splitter->setStretchFactor(1, 1);
        auto codeLayout = new QHBoxLayout(m_codeWindow);
        m_codeWindow->setLayout(codeLayout);
        codeLayout->setMargin(2);
        codeLayout->addWidget(splitter);

        // add ports dialog
        auto menuBar = new QMenuBar();
        auto portsMenu = new QMenu("Ports", menuBar);
        menuBar->addMenu(portsMenu);
        auto portEditAction = portsMenu->addAction("Edit");
        m_codeWindow->layout()->setMenuBar(menuBar);
        m_codeWindow->resize(800, 920);

        m_portsDialog = new PortEditorDialog(this, m_codeWindow);

        // connect UI events
        setOutputCaptured(true);
        connect(this, &MLinkModule::processOutputReceived, this, [this](const QString &data) {
            m_logWidget->append(data);
        });

        connect(portEditAction, &QAction::triggered, this, [this](bool) {
            m_portsDialog->updatePortLists();
            m_portsDialog->exec();
        });

        // add help menu
        auto helpMenu = new QMenu("Help", menuBar);
        menuBar->addMenu(helpMenu);
        auto docHelpAction = helpMenu->addAction("Documentation");
        connect(docHelpAction, &QAction::triggered, this, [](bool) {
            QDesktopServices::openUrl(
                QUrl("https://syntalos.readthedocs.io/latest/modules/cpp-workbench.html", QUrl::TolerantMode));
        });

        // FIXME: Dirty hack: This introduces a shortcut conflict between the KTextEditor-registered one
        // and this one. Ideally hitting Ctrl+S would save the Syntalos board, but instead it triggers
        // the KTextEditor save dialog, which confused some users. Now, we show an error instead, which
        // is also awful, but at least never leads to users doing the wrong thing. Long-term this needs
        // a proper fix (if KTextEditor doesn't have the needed API, I should contribute it...).
        auto shortcut = new QShortcut(QKeySequence(tr("Ctrl+S", "File|Save")), m_codeWindow);
        connect(shortcut, &QShortcut::activated, [=]() {});
    }

    ~CppWBenchModule() override
    {
        if (!m_wsDirPath.isEmpty()) {
            // clean up workspace
            QDir oldWsDir(m_wsDirPath);
            oldWsDir.removeRecursively();
        }
    }

    bool initialize() override
    {
        QString pkgConfPath, ldLibPath, incPath;
        findSyntalosLibraryPaths(pkgConfPath, ldLibPath, incPath);
        m_termWidget->setWorkingDirectory("/tmp");
        auto buildEnv = QProcessEnvironment::systemEnvironment();
        // disable writing commands to history file
        buildEnv.insert("HISTFILE", "");
        // find shared libraries and config files
        if (!pkgConfPath.isEmpty())
            buildEnv.insert("PKG_CONFIG_PATH", pkgConfPath);
        if (!ldLibPath.isEmpty()) {
            // set build-time environment
            buildEnv.insert("LIBRARY_PATH", ldLibPath);
            buildEnv.insert("LD_LIBRARY_PATH", ldLibPath);

            // set runtime environment as well
            auto modProcEnv = moduleBinaryEnv();
            modProcEnv.insert("LD_LIBRARY_PATH", ldLibPath);
            setModuleBinaryEnv(modProcEnv);
        }
        if (!incPath.isEmpty())
            buildEnv.insert("CPLUS_INCLUDE_PATH", incPath);

        m_termWidget->setEnvironment(buildEnv.toStringList());
        m_termWidget->startShellProgram();

        setInitialized();
        return true;
    }

    void setName(const QString &value) override
    {
        MLinkModule::setName(value);
        m_codeWindow->setWindowTitle(QStringLiteral("%1 - Editor").arg(name()));
    }

    bool performAutobuild(const QString &buildPath)
    {
        m_termWidget->changeDir(buildPath);
        QString statusFname = buildPath + "/autobuild.status";

        // remove existing status file
        if (QFile::exists(statusFname))
            QFile::remove(statusFname);

        // prepare file watcher and event loop
        QEventLoop loop;
        bool timeoutReached = false;
        QFileSystemWatcher watcher;
        watcher.addPath(statusFname);
        watcher.addPath(buildPath);

        // setup timeout
        QTimer timer;
        timer.setInterval(90 * 1000);
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, [&]() {
            timeoutReached = true;
            loop.quit();
        });

        // connect watcher
        connect(&watcher, &QFileSystemWatcher::fileChanged, [&](const QString &path) {
            if (path != statusFname)
                return;

            timer.stop();
            loop.quit();
        });
        connect(&watcher, &QFileSystemWatcher::directoryChanged, [&](const QString &path) {
            if (QFile::exists(statusFname)) {
                // the status file was added, we can exit
                timer.stop();
                loop.quit();
            }
        });

        timer.start();

        // start build
        m_termWidget->clear();
        m_termWidget->sendText(QStringLiteral("sh ../autobuild.sh\n"));

        // wait until timeout or file is created
        loop.exec();

        QFile file(statusFname);
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream stream(&file);
            int value;
            stream >> value;
            file.close();

            return value == 0;
        } else {
            // timeout reached or file did not exist
            return false;
        }
    }

    bool prepare(const TestSubject &testSubject) override
    {
        setStatusMessage("Preparing build...");
        setModuleBinary(QString());
        m_logWidget->clear();
        // switch to console view
        m_consoleTabWidget->setCurrentIndex(0);
        mainThreadProcessUiEvents();

        // basename of the executable that we are about to compile
        const auto exeName = QStringLiteral("%1-%2").arg(id()).arg(index());

        QDir wsDir(QStringLiteral("%1/%2").arg(m_cacheRoot, exeName));
        if (!wsDir.mkpath(QStringLiteral("."))) {
            raiseError(QStringLiteral("Failed to create workspace directory"));
            return false;
        }
        if (!m_wsDirPath.isEmpty() && wsDir.absolutePath() != m_wsDirPath) {
            // clean up old workspace
            QDir oldWsDir(m_wsDirPath);
            if (!oldWsDir.removeRecursively()) {
                raiseError(QStringLiteral("Failed to clean up old workspace directory"));
                return false;
            }
        }
        m_wsDirPath = wsDir.absolutePath();
        m_termWidget->changeDir(m_wsDirPath);

        // write C++ code to file
        QFile codeFile(wsDir.absoluteFilePath("main.cpp"));
        if (!codeFile.open(QIODevice::WriteOnly)) {
            raiseError(QStringLiteral("Failed to write code to file"));
            return false;
        }
        codeFile.write(m_codeView->document()->text().toUtf8());
        codeFile.close();

        // write Meson build definition to file
        QFile mesonDefFile(wsDir.absoluteFilePath("meson.build"));
        if (!mesonDefFile.exists()) {
            if (!mesonDefFile.open(QIODevice::WriteOnly)) {
                raiseError(QStringLiteral("Failed to write Meson build definition to file"));
                return false;
            }
            mesonDefFile.write(m_mesonDefTmpl.replace("@EXE_NAME@", exeName).toUtf8());
            mesonDefFile.close();
        }

        // write autobuild helper to file
        QFile autoBuildFile(wsDir.absoluteFilePath("autobuild.sh"));
        if (!autoBuildFile.open(QIODevice::WriteOnly)) {
            raiseError(QStringLiteral("Failed to write autobuild script to file"));
            return false;
        }
        autoBuildFile.write(m_autobuildScript.toUtf8());
        autoBuildFile.setPermissions(
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::WriteGroup
            | QFile::ExeGroup);
        autoBuildFile.close();

        // create build directory
        QDir buildDir(wsDir.absoluteFilePath("b"));
        if (!buildDir.mkpath(QStringLiteral("."))) {
            raiseError(QStringLiteral("Failed to create build directory"));
            return false;
        }

        setStatusMessage("Compiling...");
        if (!performAutobuild(buildDir.absolutePath())) {
            raiseError(QStringLiteral("Failed to compile C++ code. Check module console output for details."));
            return false;
        }

        // use our newly built executable as communication target
        setStatusMessage("Validating...");
        setModuleBinary(buildDir.absoluteFilePath(exeName));
        if (!QFileInfo::exists(moduleBinary())) {
            raiseError(QStringLiteral("No valid executable found after build"));
            return false;
        }
        if (!runProcess())
            return false;
        setStatusMessage(QString());

        // switch to output view
        m_consoleTabWidget->setCurrentIndex(1);
        mainThreadProcessUiEvents();

        return MLinkModule::prepare(testSubject);
    }

    void stop() override
    {
        MLinkModule::stop();
        terminateProcess();
    }

    void serializeSettings(const QString &, QVariantHash &settings, QByteArray &extraData) override
    {
        extraData = m_codeView->document()->text().toUtf8();

        QVariantList varInPorts;
        for (const auto &port : inPorts()) {
            QVariantHash po;
            po.insert("id", port->id());
            po.insert("title", port->title());
            po.insert("data_type", port->dataTypeName());
            varInPorts.append(po);
        }

        QVariantList varOutPorts;
        for (const auto &port : outPorts()) {
            QVariantHash po;
            po.insert("id", port->id());
            po.insert("title", port->title());
            po.insert("data_type", port->dataTypeName());
            varOutPorts.append(po);
        }

        settings.insert("ports_in", varInPorts);
        settings.insert("ports_out", varOutPorts);
    }

    bool loadSettings(const QString &, const QVariantHash &settings, const QByteArray &extraData) override
    {
        m_codeView->document()->setText(QString::fromUtf8(extraData));

        const auto varInPorts = settings.value("ports_in").toList();
        const auto varOutPorts = settings.value("ports_out").toList();

        for (const auto &pv : varInPorts) {
            const auto po = pv.toHash();
            registerInputPortByTypeId(
                BaseDataType::typeIdFromString(qPrintable(po.value("data_type").toString())),
                po.value("id").toString(),
                po.value("title").toString());
        }

        for (const auto &pv : varOutPorts) {
            const auto po = pv.toHash();
            registerOutputPortByTypeId(
                BaseDataType::typeIdFromString(qPrintable(po.value("data_type").toString())),
                po.value("id").toString(),
                po.value("title").toString());
        }

        // update port listing in UI
        m_portsDialog->updatePortLists();

        return true;
    }

private:
    QTabWidget *m_consoleTabWidget;
    QTextBrowser *m_logWidget;
    QTermWidget *m_termWidget;
    KTextEditor::View *m_codeView;
    PortEditorDialog *m_portsDialog;
    QWidget *m_codeWindow;

    QString m_mesonDefTmpl;
    QString m_autobuildScript;
    QString m_cacheRoot;
    QString m_wsDirPath;
};

QString CppWBenchModuleInfo::id() const
{
    return QStringLiteral("cpp-workbench");
}

QString CppWBenchModuleInfo::name() const
{
    return QStringLiteral("C++ Workbench");
}

QString CppWBenchModuleInfo::description() const
{
    return QStringLiteral("Quickly and safely write small C++ programs for data processing.");
}

AbstractModule *CppWBenchModuleInfo::createModule(QObject *parent)
{
    return new CppWBenchModule(parent);
}

#include "cppwbenchmodule.moc"