/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TCA6507 LED chip driver.
 *
 * Copyright (C) 2011 Neil Brown <neil@brown.name>
 */

#ifndef __LINUX_TCA6507_H
#define __LINUX_TCA6507_H
#include <linux/leds.h>

struct tca6507_platform_data {
	struct led_platform_data leds;
#ifdef CONFIG_GPIOLIB
	int gpio_base;
	void (*setup)(unsigned gpio_base, unsigned ngpio);
#endif
};

#define	TCA6507_MAKE_GPIO 1
#endif /* __LINUX_TCA6507_H*/
