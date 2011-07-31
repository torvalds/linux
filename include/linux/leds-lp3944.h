/*
 * leds-lp3944.h - platform data structure for lp3944 led controller
 *
 * Copyright (C) 2009 Antonio Ospite <ospite@studenti.unina.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_LEDS_LP3944_H
#define __LINUX_LEDS_LP3944_H

#define LP3944_LED0 0
#define LP3944_LED1 1
#define LP3944_LED2 2
#define LP3944_LED3 3
#define LP3944_LED4 4
#define LP3944_LED5 5
#define LP3944_LED6 6
#define LP3944_LED7 7
#define LP3944_LEDS_MAX 8

#define LP3944_LED_STATUS_MASK	0x03
enum lp3944_status {
	LP3944_LED_STATUS_OFF  = 0x0,
	LP3944_LED_STATUS_ON   = 0x1,
	LP3944_LED_STATUS_DIM0 = 0x2,
	LP3944_LED_STATUS_DIM1 = 0x3
};

enum lp3944_type {
	LP3944_LED_TYPE_NONE,
	LP3944_LED_TYPE_LED,
	LP3944_LED_TYPE_LED_INVERTED,
};

struct lp3944_led {
	char *name;
	enum lp3944_type type;
	enum lp3944_status status;
};

struct lp3944_platform_data {
	struct lp3944_led leds[LP3944_LEDS_MAX];
	u8 leds_size;
};

#endif /* __LINUX_LEDS_LP3944_H */
