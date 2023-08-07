/*
 * Copyright (C) 2010 Matthias Klumpp <matthias@tenstral.net>
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

#include "moduleapi.h"
#include <QDialog>
#include <QWidget>

class QSpinBox;
class QPushButton;
class QLabel;

class FirmataOutputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FirmataOutputWidget(
        std::shared_ptr<DataStream<FirmataControl>> fmCtlStream,
        bool analog,
        QWidget *parent = nullptr);

    bool isAnalog() const;

    int pinId() const;
    void setPinId(int pinId);
    void submitNewPinCommand();

    int value() const;
    void setValue(int value);

private slots:
    void onPinIdChange(int value);

private:
    bool m_isAnalog;

    QPushButton *m_btnRemove;
    QSpinBox *m_sbPinId;

    QSpinBox *m_sbValue;
    QPushButton *m_btnPulse;
    QPushButton *m_btnSend;
    std::shared_ptr<DataStream<FirmataControl>> m_fmCtlStream;
};

class FirmataInputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FirmataInputWidget(
        std::shared_ptr<DataStream<FirmataControl>> fmCtlStream,
        bool analog,
        QWidget *parent = nullptr);

    bool isAnalog() const;

    int pinId() const;
    void setPinId(int pinId);
    void submitNewPinCommand();

    void setValue(int value);

private slots:
    void onPinIdChange(int value);

private:
    bool m_isAnalog;

    QPushButton *m_btnRemove;
    QLabel *m_lblType;
    QSpinBox *m_sbPinId;

    QLabel *m_lblValue;
    std::shared_ptr<DataStream<FirmataControl>> m_fmCtlStream;
};

namespace Ui
{
class FirmataCtlDialog;
}

class FirmataCtlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmataCtlDialog(std::shared_ptr<DataStream<FirmataControl>> fmCtlStream, QWidget *parent = nullptr);
    ~FirmataCtlDialog();

    void initializeAllPins();
    void pinValueChanged(const FirmataData &data);

    QVariantHash serializeSettings();
    void restoreFromSettings(const QVariantHash &settings);

private slots:
    void on_btnAddOutputControl_clicked();
    void on_btnAddInputWatch_clicked();

private:
    Ui::FirmataCtlDialog *ui;

    int m_lastPinId;
    std::shared_ptr<DataStream<FirmataControl>> m_fmCtlStream;
};
