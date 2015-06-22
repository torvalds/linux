/*
 * sky81452.h	SKY81452 MFD driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SKY81452_H
#define _SKY81452_H

#include <linux/platform_data/sky81452-backlight.h>
#include <linux/regulator/machine.h>

struct sky81452_platform_data {
	struct sky81452_bl_platform_data *bl_pdata;
	struct regulator_init_data *regulator_init_data;
};

#endif
