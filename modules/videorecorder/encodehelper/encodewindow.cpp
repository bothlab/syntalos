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
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "encodewindow.h"
#include "ui_encodewindow.h"

#include <QCloseEvent>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMetaType>
#include <QMessageBox>
#include <QSettings>
#include <QSvgWidget>
#include <QThread>

#include "../videowriter.h"
#include "taskmanager.h"
#include "utils/style.h"

EncodeWindow::EncodeWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::EncodeWindow)
{
    ui->setupUi(this);
    setWindowTitle("Syntalos - Video Encoding Queue");
    setWindowIcon(QIcon(":/icons/videorecorder.svg"));

    m_queueModel = new QueueModel(ui->tasksTable);

    ui->tasksTable->setModel(m_queueModel);
    ui->tasksTable->setItemDelegateForColumn(2, new HtmlDelegate(this));
    ui->tasksTable->setItemDelegateForColumn(3, new ProgressBarDelegate(this));

    m_taskManager = new TaskManager(m_queueModel, this);
    QDBusConnection::sessionBus().registerObject("/", this);
    if (!QDBusConnection::sessionBus().registerService(EQUEUE_DBUS_SERVICE)) {
        fprintf(stderr, "%s\n", qPrintable(QDBusConnection::sessionBus().lastError().message()));
        exit(1);
    }

    // stretch out table columns
    for (int i = 0; i < ui->tasksTable->horizontalHeader()->count(); ++i) {
        if (i != 2)
            ui->tasksTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    ui->tasksTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->parallelTasksCountSpinBox->setMaximum(QThread::idealThreadCount() + 2);
    ui->parallelTasksCountSpinBox->setMinimum(1);
    ui->parallelTasksCountSpinBox->setValue(m_taskManager->parallelCount());
    connect(m_taskManager, &TaskManager::parallelCountChanged, [&](int count) {
        ui->parallelTasksCountSpinBox->setValue(count);
    });

    // enable the run button if new tasks are available
    connect(m_taskManager, &TaskManager::newTasksAvailable, [&]() {
        ui->runButton->setEnabled(true);
        ui->detailsWidget->setVisible(false);
        ui->tasksTable->scrollToBottom();
    });
    connect(m_taskManager, &TaskManager::encodingStarted, [&]() {
        ui->runButton->setEnabled(false);
    });
    ui->runButton->setEnabled(m_taskManager->tasksAvailable());

    // busy indicator
    m_busyIndicator = new QSvgWidget(ui->busyIndicatorContainer);
    m_busyIndicator->load(loadBusyAnimation(QStringLiteral("encoding.svg")));
    m_busyIndicator->setMaximumSize(QSize(40, 40));
    m_busyIndicator->setMinimumSize(QSize(40, 40));
    m_busyIndicator->hide();

    connect(m_taskManager, &TaskManager::encodingStarted, [&]() {
        m_busyIndicator->show();
    });
    connect(m_taskManager, &TaskManager::encodingFinished, [&]() {
        m_busyIndicator->hide();
    });

    // hide details display initially
    ui->detailsWidget->setVisible(false);
    ui->splitter->setStretchFactor(0, 4);

    // restore window geometry
    QSettings settings;
    restoreGeometry(settings.value("main/geometry").toByteArray());
}

EncodeWindow::~EncodeWindow()
{
    delete ui;
}

QByteArray EncodeWindow::loadBusyAnimation(const QString &name) const
{
    bool isDark = currentThemeIsDark();

    QFile f(QStringLiteral(":/animations/") + name);
    if (!f.open(QFile::ReadOnly | QFile::Text)) {
        qWarning().noquote() << "Failed to load busy animation" << name << ": " << f.errorString();
        return QByteArray();
    }

    QTextStream in(&f);
    auto svgData = in.readAll();
    if (!isDark)
        return svgData.toLocal8Bit();

    // adjust for dark theme
    return svgData.replace(QStringLiteral("#232629"), QStringLiteral("#eff0f1"))
        .replace(QStringLiteral("#4d4d4d"), QStringLiteral("#bdc3c7"))
        .toLocal8Bit();
}

void EncodeWindow::on_runButton_clicked()
{
    m_taskManager->processVideos();
}

void EncodeWindow::on_parallelTasksCountSpinBox_valueChanged(int value)
{
    m_taskManager->setParallelCount(value);
}

void EncodeWindow::on_tasksTable_activated(const QModelIndex &index)
{
    if (index.row() < 0)
        return;
    const auto item = m_queueModel->itemByIndex(index);
    if (item == nullptr)
        return;
    ui->detailsWidget->setVisible(true);

    const auto errorMsg = item->errorMessage();

    QString info = "<b>General</b>";
    QHashIterator<QString, QVariant> i(item->mdata());
    while (i.hasNext()) {
        i.next();
        auto key = i.key();
        QString value;
        if (key == "video-container")
            value = QString::fromStdString(videoContainerToString(static_cast<VideoContainer>(i.value().toInt())));
        else
            value = i.value().toString();
        info += QStringLiteral("<br/>%1 = %2").arg(key, value);
    }
    info += "<br/><br/><b>Encoder</b>";
    QHashIterator<QString, QVariant> ei(item->codecProps().toVariant());
    while (ei.hasNext()) {
        ei.next();
        auto key = ei.key();
        QString value;
        if (key == "codec")
            value = QString::fromStdString(videoCodecToString(static_cast<VideoCodec>(ei.value().toInt())));
        else
            value = ei.value().toString();
        info += QStringLiteral("<br/>%1 = %2").arg(key, value);
    }

    const auto text = QStringLiteral(
                          "<h3>Errors</h3><p>%1</p>"
                          "<h3>Technical Details</h3><p>%2</p>")
                          .arg(errorMsg.isEmpty() ? "None" : errorMsg, info);
    ui->detailsBrowser->setHtml(text);
}

void EncodeWindow::closeEvent(QCloseEvent *event)
{
    if (m_taskManager->allTasksCompleted()) {
        event->accept();

        QSettings settings;
        settings.setValue("main/geometry", saveGeometry());
        QApplication::quit();
    } else {
        QMessageBox::warning(
            this,
            QStringLiteral("Encoding in progress"),
            QStringLiteral("You can not close this tool while there are still encoding tasks ongoing or pending.\n"
                           "Please encode all videos before quitting."));
        event->ignore();
    }
}
