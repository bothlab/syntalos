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

#include "mlinkmodule.h"

#include "config.h"
#include <QEventLoop>
#include <iceoryx_hoofs/posix_wrapper/signal_watcher.hpp>
#include <iceoryx_posh/popo/subscriber.hpp>
#include <iceoryx_posh/runtime/posh_runtime.hpp>

#include "mlink/ipc-types-private.h"
#include "globalconfig.h"

namespace Syntalos
{
Q_LOGGING_CATEGORY(logMLinkMod, "mlink-master")
}

class MLinkModule::Private
{
public:
    Private() {}

    ~Private() {}

    iox::cxx::string<100> clientId;
    std::unique_ptr<iox::popo::Subscriber<ErrorEvent>> subError;
};

template<typename T>
std::unique_ptr<T> MLinkModule::makeSubscriber(const QString &eventName)
{
    iox::popo::SubscriberOptions subOptn;

    // hold 10 elements for processing by default
    subOptn.queueCapacity = 10U;

    // get the last 5 samples if for whatever reason we connected too late
    subOptn.historyRequest = 5U;

    const auto eventNameIox = iox::cxx::string<100>(iox::cxx::TruncateToCapacity, eventName.toStdString());
    return std::make_unique<T>(iox::capro::ServiceDescription{"SyntalosModule", d->clientId, eventNameIox}, subOptn);
}

MLinkModule::MLinkModule(QObject *parent)
    : AbstractModule(parent),
      d(new MLinkModule::Private)
{
    d->clientId = "test_1";

    d->subError = makeSubscriber<iox::popo::Subscriber<ErrorEvent>>("Error");
}

MLinkModule::~MLinkModule() {}

ModuleFeatures MLinkModule::features() const
{
    return ModuleFeature::SHOW_DISPLAY | ModuleFeature::SHOW_SETTINGS;
}

bool MLinkModule::prepare(const TestSubject &)
{

    return true;
}

void MLinkModule::testProcess()
{
    d->subError->take().and_then([](auto &error) {
        std::cout << "Got value: " << error->title << error->message << std::endl;
    });
}
