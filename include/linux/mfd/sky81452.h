/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sky81452.h	SKY81452 MFD driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 */

#ifndef _SKY81452_H
#define _SKY81452_H

#include <linux/regulator/machine.h>

struct sky81452_platform_data {
	struct regulator_init_data *regulator_init_data;
};

#endif
