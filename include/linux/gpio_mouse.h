/*
 * Driver for simulating a mouse on GPIO lines.
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _GPIO_MOUSE_H
#define _GPIO_MOUSE_H

#define GPIO_MOUSE_POLARITY_ACT_HIGH	0x00
#define GPIO_MOUSE_POLARITY_ACT_LOW	0x01

#define GPIO_MOUSE_PIN_UP	0
#define GPIO_MOUSE_PIN_DOWN	1
#define GPIO_MOUSE_PIN_LEFT	2
#define GPIO_MOUSE_PIN_RIGHT	3
#define GPIO_MOUSE_PIN_BLEFT	4
#define GPIO_MOUSE_PIN_BMIDDLE	5
#define GPIO_MOUSE_PIN_BRIGHT	6
#define GPIO_MOUSE_PIN_MAX	7

/**
 * struct gpio_mouse_platform_data
 * @scan_ms: integer in ms specifying the scan periode.
 * @polarity: Pin polarity, active high or low.
 * @up: GPIO line for up value.
 * @down: GPIO line for down value.
 * @left: GPIO line for left value.
 * @right: GPIO line for right value.
 * @bleft: GPIO line for left button.
 * @bmiddle: GPIO line for middle button.
 * @bright: GPIO line for right button.
 *
 * This struct must be added to the platform_device in the board code.
 * It is used by the gpio_mouse driver to setup GPIO lines and to
 * calculate mouse movement.
 */
struct gpio_mouse_platform_data {
	int scan_ms;
	int polarity;

	union {
		struct {
			int up;
			int down;
			int left;
			int right;

			int bleft;
			int bmiddle;
			int bright;
		};
		int pins[GPIO_MOUSE_PIN_MAX];
	};
};

#endif /* _GPIO_MOUSE_H */
