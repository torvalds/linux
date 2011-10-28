/*
 * leds-regulator.h - platform data structure for regulator driven LEDs.
 *
 * Copyright (C) 2009 Antonio Ospite <ospite@studenti.unina.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_LEDS_REGULATOR_H
#define __LINUX_LEDS_REGULATOR_H

/*
 * Use "vled" as supply id when declaring the regulator consumer:
 *
 * static struct regulator_consumer_supply pcap_regulator_VVIB_consumers [] = {
 * 	{ .dev_name = "leds-regulator.0", .supply = "vled" },
 * };
 *
 * If you have several regulator driven LEDs, you can append a numerical id to
 * .dev_name as done above, and use the same id when declaring the platform
 * device:
 *
 * static struct led_regulator_platform_data a780_vibrator_data = {
 * 	.name   = "a780::vibrator",
 * };
 *
 * static struct platform_device a780_vibrator = {
 * 	.name = "leds-regulator",
 * 	.id   = 0,
 * 	.dev  = {
 * 		.platform_data = &a780_vibrator_data,
 * 	},
 * };
 */

#include <linux/leds.h>

struct led_regulator_platform_data {
	char *name;                     /* LED name as expected by LED class */
	enum led_brightness brightness; /* initial brightness value */
};

#endif /* __LINUX_LEDS_REGULATOR_H */
