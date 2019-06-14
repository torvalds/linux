/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#ifndef _LINUX_WL12XX_H
#define _LINUX_WL12XX_H

#include <linux/err.h>

struct wl1251_platform_data {
	int power_gpio;
	/* SDIO only: IRQ number if WLAN_IRQ line is used, 0 for SDIO IRQs */
	int irq;
	bool use_eeprom;
};

#ifdef CONFIG_WILINK_PLATFORM_DATA

int wl1251_set_platform_data(const struct wl1251_platform_data *data);

struct wl1251_platform_data *wl1251_get_platform_data(void);

#else

static inline
int wl1251_set_platform_data(const struct wl1251_platform_data *data)
{
	return -ENOSYS;
}

static inline
struct wl1251_platform_data *wl1251_get_platform_data(void)
{
	return ERR_PTR(-ENODATA);
}

#endif

#endif
