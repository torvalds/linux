/*
 * TI Wakeup M3 remote processor platform data
 *
 * Copyright (C) 2014-2015 Texas Instruments, Inc.
 *
 * Dave Gerlach <d-gerlach@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
