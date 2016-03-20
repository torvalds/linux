/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
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
