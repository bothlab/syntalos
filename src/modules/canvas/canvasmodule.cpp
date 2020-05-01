/*
 * Copyright (C) 2019-2020 Matthias Klumpp <matthias@tenstral.net>
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

#include "canvasmodule.h"

#include <QTimer>
#include <QTime>

#include "canvaswindow.h"
#include "streams/frametype.h"

class CanvasModule : public AbstractModule
{
private:
    std::shared_ptr<StreamInputPort<Frame>> m_framesIn;
    std::shared_ptr<StreamInputPort<ControlCommand>> m_ctlIn;

    std::shared_ptr<StreamSubscription<Frame>> m_frameSub;
    std::shared_ptr<StreamSubscription<ControlCommand>> m_ctlSub;

    CanvasWindow *m_cvView;
    QTimer *m_evTimer;

    double m_expectedFps;
    double m_currentFps;
    long m_lastFrameTime;
    double m_avgFrameTimeDiffMsec;

    double m_currentDisplayFps;
    symaster_timepoint m_lastDisplayTime;

    uint m_lastPendingCount;
    uint m_maxPendingFrames;
    uint m_maxThrottleAdjustWaitN;
    uint m_throttleAdjustFramesWaited;
    uint m_throttleCount;

public:
    explicit CanvasModule(QObject *parent = nullptr)
        : AbstractModule(parent)
    {
        m_framesIn = registerInputPort<Frame>(QStringLiteral("frames-in"), QStringLiteral("Frames"));
        m_ctlIn = registerInputPort<ControlCommand>(QStringLiteral("control"), QStringLiteral("Control"));

        m_cvView = new CanvasWindow;
        addDisplayWindow(m_cvView);
        m_evTimer = new QTimer(this);
        m_evTimer->setInterval(0);
        connect(m_evTimer, &QTimer::timeout, this, &CanvasModule::updateImage);
    }

    ModuleFeatures features() const override
    {
        return ModuleFeature::SHOW_DISPLAY;
    }

    bool prepare(const TestSubject &) override
    {
        m_frameSub.reset();
        m_ctlSub.reset();
        if (m_framesIn->hasSubscription())
            m_frameSub = m_framesIn->subscription();
        if (m_ctlIn->hasSubscription())
            m_ctlSub = m_ctlIn->subscription();

        // set some default values, these will be overridden immediately
        // with real values once we are displaying an image
        m_lastDisplayTime = currentTimePoint();
        m_currentDisplayFps = 60.0;

        return true;
    }

    void start() override
    {
        m_lastFrameTime = m_syTimer->timeSinceStartMsec().count();
        if (m_frameSub.get() == nullptr)
            return;

        // check framerate and throttle it, showing a remark in the latter
        // case so the user is aware that they're not seeing every single frame
        m_expectedFps = m_frameSub->metadata().value("framerate", 0).toDouble();

        // never try to display more than 120fps by default
        // the module will lower this on its own if too much data is queued
        m_throttleCount = 120;
        m_frameSub->setThrottleItemsPerSec(m_throttleCount);

        // permit a maximum of about 320msec of pending frames in queue
        m_maxPendingFrames = qRound(m_expectedFps / 3.125);
        if (m_maxPendingFrames < 5)
            m_maxPendingFrames = 5;
        m_lastPendingCount = 0;
        m_throttleAdjustFramesWaited = m_maxPendingFrames;

        // wait some amount of frames for throttle changes to take effect
        m_maxThrottleAdjustWaitN = qRound(m_expectedFps / 40.0);
        if (m_maxThrottleAdjustWaitN < 5)
            m_maxThrottleAdjustWaitN = 5;

        // assume perfect frame diff for now
        m_avgFrameTimeDiffMsec = 1000.0 / m_expectedFps;

        auto imgWinTitle = m_frameSub->metadataValue(CommonMetadataKey::SrcModName).toString();
        if (imgWinTitle.isEmpty())
            imgWinTitle = "Canvas";
        const auto portTitle = m_frameSub->metadataValue(CommonMetadataKey::SrcModPortTitle).toString();
        if (!portTitle.isEmpty())
            imgWinTitle = QStringLiteral("%1 - %2").arg(imgWinTitle).arg(portTitle);
        m_cvView->setWindowTitle(imgWinTitle);

        m_evTimer->start();
    }

    void stop() override
    {
        m_evTimer->stop();
    }

    void updateImage()
    {
        auto maybeFrame = m_frameSub->peekNext();
        if (!maybeFrame.has_value())
            return;
        const auto skippedFrames = m_frameSub->retrieveApproxSkippedElements();
        const auto framesPendingCount = m_frameSub->approxPendingCount();
        if (framesPendingCount > m_maxPendingFrames) {
            // we have too many frames pending in the queue, we may have to throttle
            // the subscription more

            if (m_throttleAdjustFramesWaited >= m_maxThrottleAdjustWaitN) {
                if (framesPendingCount >= m_lastPendingCount) {
                    m_throttleCount--;
                    if (m_throttleCount <= 4) {
                        // throttle to less then 4fps? This looks suspicious, terminate.
                        m_frameSub->suspend();
                        raiseError(QStringLiteral("Dropped below 4fps in display frequency, but we are still not able to display frames fast enough. "
                                                  "Either the displayed frames are excessively large, something is wrong with the display hardware, "
                                                  "or there is a bug in the display code."));
                        return;
                    }
                    m_frameSub->setThrottleItemsPerSec(m_throttleCount);
                }

                m_lastPendingCount = framesPendingCount;
                m_throttleAdjustFramesWaited = 0;
            }

            m_throttleAdjustFramesWaited++;
        }

        // get all timing info and show the image
        const auto frame = maybeFrame.value();
        m_cvView->showImage(frame.mat);
        const auto frameTime = frame.time.count();

        if (m_expectedFps == 0) {
            m_cvView->setStatusText(QTime::fromMSecsSinceStartOfDay(frameTime).toString("hh:mm:ss.zzz"));
        } else {
            // we use a moving average of the inter-frame-time over two seconds, as the framerate occasionally fluctuates
            // (especially when throttling the subscription) and we want to display a more steady (but accurate)
            // info to the user instead, without twitching around too much
            m_avgFrameTimeDiffMsec = ((m_avgFrameTimeDiffMsec * m_expectedFps * 2) + ((frameTime - m_lastFrameTime) / (skippedFrames + 1))) / (m_expectedFps * 2 + 1);
            m_lastFrameTime = frameTime;
            if (m_avgFrameTimeDiffMsec > 0)
                m_currentFps = (1000.0 / m_avgFrameTimeDiffMsec);
            else
                m_currentFps = m_expectedFps;

            const auto frameDisplayTime = currentTimePoint();
            m_currentDisplayFps = ((m_currentDisplayFps * 20) + (1000.0 / timeDiffMsec(frameDisplayTime, m_lastDisplayTime).count())) / (20 + 1);
            m_lastDisplayTime = frameDisplayTime;

            m_cvView->setStatusText(QStringLiteral("%1 | Stream: %2fps (of %4fps) | Display: %5fps")
                                    .arg(QTime::fromMSecsSinceStartOfDay(frameTime).toString("hh:mm:ss.zzz"))
                                    .arg(m_currentFps, 0, 'f', 1)
                                    .arg(m_expectedFps, 0, 'f', 1)
                                    .arg(m_currentDisplayFps, 0, 'f', 1));
        }
    }
};

QString CanvasModuleInfo::id() const
{
    return QStringLiteral("canvas");
}

QString CanvasModuleInfo::name() const
{
    return QStringLiteral("Canvas");
}

QString CanvasModuleInfo::description() const
{
    return QStringLiteral("Display any image or video sequence.");
}

QPixmap CanvasModuleInfo::pixmap() const
{
    return QPixmap(":/module/canvas");
}

AbstractModule *CanvasModuleInfo::createModule(QObject *parent)
{
    return new CanvasModule(parent);
}
