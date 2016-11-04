/*
 * Gas Gauge driver for SBS Compliant Gas Gauges
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __LINUX_POWER_SBS_BATTERY_H_
#define __LINUX_POWER_SBS_BATTERY_H_

#include <linux/power_supply.h>
#include <linux/types.h>

/**
 * struct sbs_platform_data - platform data for sbs devices
 * @i2c_retry_count:		# of times to retry on i2c IO failure
 * @poll_retry_count:		# of times to retry looking for new status after
 *				external change notification
 */
struct sbs_platform_data {
	u32 i2c_retry_count;
	u32 poll_retry_count;
};

#endif
