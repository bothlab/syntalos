/*
 * Copyright (C) 2019-2024 Matthias Klumpp <matthias@tenstral.net>
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

#include <QWidget>
#include "vips8-q.h"

class ImageViewWidget;
class QLabel;

class CanvasWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasWindow(QWidget *parent = nullptr);

    void showImage(const vips::VImage &image);
    void setStatusText(const QString &text);

    bool highlightSaturation() const;
    void setHighlightSaturation(bool enabled);

private:
    ImageViewWidget *m_imgView;
    QLabel *m_statusLabel;
};
