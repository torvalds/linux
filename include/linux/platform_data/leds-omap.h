/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2006 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 */
#ifndef ASMARM_ARCH_LED_H
#define ASMARM_ARCH_LED_H

struct omap_led_config {
	struct led_classdev	cdev;
	s16			gpio;
};

struct omap_led_platform_data {
	s16			nr_leds;
	struct omap_led_config	*leds;
};

#endif
