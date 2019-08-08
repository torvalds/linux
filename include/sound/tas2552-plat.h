/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TAS2552 driver platform header
 *
 * Copyright (C) 2014 Texas Instruments Inc.
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 */

#ifndef TAS2552_PLAT_H
#define TAS2552_PLAT_H

struct tas2552_platform_data {
	int enable_gpio;
};

#endif
