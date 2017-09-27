/*
 * gpio_backlight.h - Simple GPIO-controlled backlight
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __GPIO_BACKLIGHT_H__
#define __GPIO_BACKLIGHT_H__

struct device;

struct gpio_backlight_platform_data {
	struct device *fbdev;
	int gpio;
	int def_value;
	const char *name;
};

#endif
