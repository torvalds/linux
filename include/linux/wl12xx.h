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

/* Reference clock values */
enum {
	WL12XX_REFCLOCK_19	= 0, /* 19.2 MHz */
	WL12XX_REFCLOCK_26	= 1, /* 26 MHz */
	WL12XX_REFCLOCK_38	= 2, /* 38.4 MHz */
	WL12XX_REFCLOCK_52	= 3, /* 52 MHz */
	WL12XX_REFCLOCK_38_XTAL = 4, /* 38.4 MHz, XTAL */
	WL12XX_REFCLOCK_26_XTAL = 5, /* 26 MHz, XTAL */
};

/* TCXO clock values */
enum {
	WL12XX_TCXOCLOCK_19_2	= 0, /* 19.2MHz */
	WL12XX_TCXOCLOCK_26	= 1, /* 26 MHz */
	WL12XX_TCXOCLOCK_38_4	= 2, /* 38.4MHz */
	WL12XX_TCXOCLOCK_52	= 3, /* 52 MHz */
	WL12XX_TCXOCLOCK_16_368	= 4, /* 16.368 MHz */
	WL12XX_TCXOCLOCK_32_736	= 5, /* 32.736 MHz */
	WL12XX_TCXOCLOCK_16_8	= 6, /* 16.8 MHz */
	WL12XX_TCXOCLOCK_33_6	= 7, /* 33.6 MHz */
};

struct wl1251_platform_data {
	int power_gpio;
	/* SDIO only: IRQ number if WLAN_IRQ line is used, 0 for SDIO IRQs */
	int irq;
	bool use_eeprom;
};

struct wl12xx_platform_data {
	int irq;
	u32 irq_trigger;
	int board_ref_clock;
	int board_tcxo_clock;
	bool pwr_in_suspend;
};

#ifdef CONFIG_WILINK_PLATFORM_DATA

int wl12xx_set_platform_data(const struct wl12xx_platform_data *data);

struct wl12xx_platform_data *wl12xx_get_platform_data(void);

int wl1251_set_platform_data(const struct wl1251_platform_data *data);

struct wl1251_platform_data *wl1251_get_platform_data(void);

#else

static inline
int wl12xx_set_platform_data(const struct wl12xx_platform_data *data)
{
	return -ENOSYS;
}

static inline
struct wl12xx_platform_data *wl12xx_get_platform_data(void)
{
	return ERR_PTR(-ENODATA);
}

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
