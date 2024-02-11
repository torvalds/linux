/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GPIO_KEYS_H
#define _GPIO_KEYS_H

#include <linux/types.h>

struct device;

/**
 * struct gpio_keys_button - configuration parameters
 * @code:		input event code (KEY_*, SW_*)
 * @gpio:		%-1 if this key does not support gpio
 * @active_low:		%true indicates that button is considered
 *			depressed when gpio is low
 * @desc:		label that will be attached to button's gpio
 * @type:		input event type (%EV_KEY, %EV_SW, %EV_ABS)
 * @wakeup:		configure the button as a wake-up source
 * @wakeup_event_action:	event action to trigger wakeup
 * @debounce_interval:	debounce ticks interval in msecs
 * @can_disable:	%true indicates that userspace is allowed to
 *			disable button via sysfs
 * @value:		axis value for %EV_ABS
 * @irq:		Irq number in case of interrupt keys
 * @wakeirq:		Optional dedicated wake-up interrupt
 */
struct gpio_keys_button {
	unsigned int code;
	int gpio;
	int active_low;
	const char *desc;
	unsigned int type;
	int wakeup;
	int wakeup_event_action;
	int debounce_interval;
	bool can_disable;
	int value;
	unsigned int irq;
	unsigned int wakeirq;
};

/**
 * struct gpio_keys_platform_data - platform data for gpio_keys driver
 * @buttons:		pointer to array of &gpio_keys_button structures
 *			describing buttons attached to the device
 * @nbuttons:		number of elements in @buttons array
 * @poll_interval:	polling interval in msecs - for polling driver only
 * @rep:		enable input subsystem auto repeat
 * @enable:		platform hook for enabling the device
 * @disable:		platform hook for disabling the device
 * @name:		input device name
 */
struct gpio_keys_platform_data {
	const struct gpio_keys_button *buttons;
	int nbuttons;
	unsigned int poll_interval;
	unsigned int rep:1;
	int (*enable)(struct device *dev);
	void (*disable)(struct device *dev);
	const char *name;
};

#endif
