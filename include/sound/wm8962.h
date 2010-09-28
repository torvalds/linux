/*
 * wm8962.h  --  WM8962 Soc Audio driver platform data
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8962_PDATA_H
#define _WM8962_PDATA_H

#define WM8962_MAX_GPIO 6

/* Use to set GPIO default values to zero */
#define WM8962_GPIO_SET 0x10000

struct wm8962_pdata {
	u32 gpio_init[WM8962_MAX_GPIO];

	bool irq_active_low;

	bool spk_mono;   /* Speaker outputs tied together as mono */
};

#endif
