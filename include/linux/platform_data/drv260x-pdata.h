/*
 * Platform data for DRV260X haptics driver family
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * Copyright:   (C) 2014 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _LINUX_DRV260X_PDATA_H
#define _LINUX_DRV260X_PDATA_H

struct drv260x_platform_data {
	u32 library_selection;
	u32 mode;
	u32 vib_rated_voltage;
	u32 vib_overdrive_voltage;
};

#endif
