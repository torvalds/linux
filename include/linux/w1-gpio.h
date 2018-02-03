/*
 * w1-gpio interface to platform code
 *
 * Copyright (C) 2007 Ville Syrjala <syrjala@sci.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */
#ifndef _LINUX_W1_GPIO_H
#define _LINUX_W1_GPIO_H

struct gpio_desc;

/**
 * struct w1_gpio_platform_data - Platform-dependent data for w1-gpio
 */
struct w1_gpio_platform_data {
	struct gpio_desc *gpiod;
	struct gpio_desc *pullup_gpiod;
	void (*enable_external_pullup)(int enable);
	unsigned int pullup_duration;
};

#endif /* _LINUX_W1_GPIO_H */
