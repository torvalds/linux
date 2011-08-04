/*
 * Gas Gauge driver for TI's BQ20Z75
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

#ifndef __LINUX_POWER_BQ20Z75_H_
#define __LINUX_POWER_BQ20Z75_H_

#include <linux/power_supply.h>
#include <linux/types.h>

/**
 * struct bq20z75_platform_data - platform data for bq20z75 devices
 * @battery_detect:		GPIO which is used to detect battery presence
 * @battery_detect_present:	gpio state when battery is present (0 / 1)
 * @i2c_retry_count:		# of times to retry on i2c IO failure
 * @poll_retry_count:		# of times to retry looking for new status after
 *				external change notification
 */
struct bq20z75_platform_data {
	int battery_detect;
	int battery_detect_present;
	int i2c_retry_count;
	int poll_retry_count;
};

#endif
