/*
 * Driver model for leds and led triggers
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005 Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_LEDS_H_INCLUDED
#define __LINUX_LEDS_H_INCLUDED

struct device;
struct class_device;
/*
 * LED Core
 */

enum led_brightness {
	LED_OFF = 0,
	LED_HALF = 127,
	LED_FULL = 255,
};

struct led_classdev {
	const char *name;
	int brightness;
	int flags;
#define LED_SUSPENDED       (1 << 0)

	/* A function to set the brightness of the led */
	void (*brightness_set)(struct led_classdev *led_cdev,
				enum led_brightness brightness);

	struct class_device *class_dev;
	/* LED Device linked list */
	struct list_head node;

	/* Trigger data */
	char *default_trigger;
};

extern int led_classdev_register(struct device *parent,
				struct led_classdev *led_cdev);
extern void led_classdev_unregister(struct led_classdev *led_cdev);
extern void led_classdev_suspend(struct led_classdev *led_cdev);
extern void led_classdev_resume(struct led_classdev *led_cdev);

#endif		/* __LINUX_LEDS_H_INCLUDED */
