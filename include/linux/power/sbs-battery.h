/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Gas Gauge driver for SBS Compliant Gas Gauges
 *
 * Copyright (c) 2010, NVIDIA Corporation.
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
