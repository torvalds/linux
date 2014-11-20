/*
 * TAS2552 driver platform header
 *
 * Copyright (C) 2014 Texas Instruments Inc.
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef TAS2552_PLAT_H
#define TAS2552_PLAT_H

struct tas2552_platform_data {
	int enable_gpio;
};

#endif
