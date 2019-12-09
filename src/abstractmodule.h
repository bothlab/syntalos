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

#ifndef ABSTRACTMODULE_H
#define ABSTRACTMODULE_H

#include <QObject>
#include <QList>
#include <QByteArray>
#include <QAction>
#include <QPixmap>

#include "utils.h"
#include "hrclock.h"
#include "modulemanager.h"

class AbstractModule;

/**
 * @brief The ModuleState enum
 *
 * Describes the state a module can be in. The state is usually displayed
 * to the user via a module indicator widget.
 */
enum class ModuleState {
    UNKNOWN,
    INITIALIZING,
    PREPARING,
    WAITING,
    READY,
    RUNNING,
    ERROR
};

/**
 * @brief The ModuleFeature flags
 * List of basic features this module may or may not support.
 */
enum class ModuleFeature {
    NONE = 0,
    SETTINGS = 1 << 0,
    DISPLAY  = 1 << 1,
    ACTIONS  = 1 << 2
};
Q_DECLARE_FLAGS(ModuleFeatures, ModuleFeature)
Q_DECLARE_OPERATORS_FOR_FLAGS(ModuleFeatures)

class ModuleInfo : public QObject
{
    Q_OBJECT
    friend class ModuleManager;
public:
    /**
     * @brief Name of this module used internally as unique identifier
     */
    virtual QString id() const;

    /**
     * @brief Name of this module displayed to the user
     */
    virtual QString name() const;

    /**
     * @brief Description of this module
     */
    virtual QString description() const;

    /**
     * @brief Additional licensing conditions that apply to this module.
     */
    virtual QString license() const;

    /**
     * @brief Icon of this module
     */
    virtual QPixmap pixmap() const;

    /**
     * @brief Returns true if only one instance of this module can exist.
     * @return True if singleton.
     */
    virtual bool singleton() const;

    /**
     * @brief Instantiate the actual module.
     * @return A new module of this type.
     */
    virtual AbstractModule *createModule(QObject *parent = nullptr) = 0;

    int count() const;

private:
    int m_count;
    void setCount(int count);
};

#define ModuleInfoInterface_iid "com.draguhnlab.MazeAmaze.ModuleInfoInterface"
Q_DECLARE_INTERFACE(ModuleInfo, ModuleInfoInterface_iid)

class AbstractModule : public QObject
{
    Q_OBJECT
    friend class ModuleManager;
public:
    explicit AbstractModule(QObject *parent = nullptr);

    ModuleState state() const;
    void setState(ModuleState state);

    /**
     * @brief Name of this module used internally as unique identifier
     */
    QString id() const;

    /**
     * @brief Name of this module displayed to the user
     */
    virtual QString name() const;
    virtual void setName(const QString& name);

    /**
     * @brief Return a bitfield of features this module supports.
     */
    virtual ModuleFeatures features() const;

    /**
     * @brief Initialize the module
     *
     * Initialize this module. This method is called once after construction.
     * @return true if success
     */
    virtual bool initialize(ModuleManager *manager) = 0;

    /**
     * @brief Prepare for an experiment run
     *
     * Prepare this module to run. This method is called once
     * prior to every experiment run.
     * @return true if success
     */
    virtual bool prepare(const QString& storageRootDir, const TestSubject& testSubject, HRTimer *timer) = 0;

    /**
     * @brief Run when the experiment is started and the HRTimer has an initial time set.
     * Switches the module into "Started" mode.
     */
    virtual void start();

    /**
     * @brief Execute tasks once per processing loop
     *
     * Run one iteration for this module. This function is called in a loop,
     * so make sure it never blocks.
     * @return true if no error
     */
    virtual bool runCycle();

    /**
     * @brief Stop running an experiment.
     * Stop execution of an experiment. This method is called after
     * prepare() was run.
     */
    virtual void stop() = 0;

    /**
     * @brief Finalize this module.
     * This method is called before the module itself is destroyed.
     */
    virtual void finalize();

    /**
     * @brief Show the display widgets of this module
     */
    virtual void showDisplayUi();
    virtual bool isDisplayUiVisible();

    /**
     * @brief Show the configuration UI of this module
     */
    virtual void showSettingsUi();
    virtual bool isSettingsUiVisible();

    /**
     * @brief Hide all display widgets of this module
     */
    virtual void hideDisplayUi();

    /**
     * @brief Hide the configuration UI of this module
     */
    virtual void hideSettingsUi();

    /**
     * @brief Get actions to add to the module's submenu
     * @return
     */
    virtual QList<QAction*> actions();

    /**
     * @brief Serialize the settings of this module to a byte array.
     */
    virtual QByteArray serializeSettings(const QString& confBaseDir);

    /**
     * @brief Load settings from previously stored data.
     * @return true if successful.
     */
    virtual bool loadSettings(const QString& confBaseDir, const QByteArray& data);

    /**
     * @brief Return last error
     * @return The last error message generated by this module
     */
    QString lastError() const;

    /**
     * @brief Check if the selected module can be removed.
     * This function is called by the module manager prior to removal of a module on
     * each active module. If False is returned, the module is prevented from being removed.
     * @return True if module can be removed, fals if removal should be prevented.
     */
    virtual bool canRemove(AbstractModule *mod);

    bool makeDirectory(const QString &dir);

    void setInitialized();
    bool initialized() const;

    QJsonValue serializeDisplayUiGeometry();
    void restoreDisplayUiGeomatry(QJsonObject info);

    void setStatusMessage(const QString& message);

signals:
    void actionsUpdated();
    void stateChanged(ModuleState state);
    void error(const QString& message);
    void statusMessage(const QString& message);
    void nameChanged(const QString& name);

protected:
    void raiseError(const QString& message);
    QByteArray jsonObjectToBytes(const QJsonObject& object);
    QJsonObject jsonObjectFromBytes(const QByteArray& data);

    QString m_name;
    QString m_storageDir;

    QList<QWidget*> m_displayWindows;
    QList<QWidget*> m_settingsWindows;

private:
    ModuleState m_state;
    QString m_lastError;
    QString m_id;
    bool m_initialized;

    void setId(const QString &id);
};

#endif // ABSTRACTMODULE_H
