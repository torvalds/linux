/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI Wakeup M3 remote processor platform data
 *
 * Copyright (C) 2014-2015 Texas Instruments, Inc.
 *
 * Dave Gerlach <d-gerlach@ti.com>
 */

#ifndef _LINUX_PLATFORM_DATA_WKUP_M3_H
#define _LINUX_PLATFORM_DATA_WKUP_M3_H

struct platform_device;

struct wkup_m3_platform_data {
	const char *reset_name;

	int (*assert_reset)(struct platform_device *pdev, const char *name);
	int (*deassert_reset)(struct platform_device *pdev, const char *name);
};

#endif /* _LINUX_PLATFORM_DATA_WKUP_M3_H */
