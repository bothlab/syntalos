/*
 * Copyright (C) 2023-2024 Matthias Klumpp <matthias@tenstral.net>
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

#include "vips8-q.h"
#include "opencv2/core.hpp"

/**
 * @brief Transform a cv::Mat to a vips::VImage
 * @param mat The image matrix to transform
 * @return A copy of the image as VIPS image.
 */
vips::VImage cvMatToVips(const cv::Mat &mat);

/**
 * @brief Transform a VipsImage into a cv::Mat
 * @param vimg The image to transform
 * @return A copy of the image as cv::Mat
 */
cv::Mat vipsToCvMat(vips::VImage vimg);
