/*
 * include/linux/gpio-fan.h
 *
 * Platform data structure for GPIO fan driver
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __LINUX_GPIO_FAN_H
#define __LINUX_GPIO_FAN_H

struct gpio_fan_alarm {
	unsigned	gpio;
	unsigned	active_low;
};

struct gpio_fan_speed {
	int rpm;
	int ctrl_val;
};

struct gpio_fan_platform_data {
	int			num_ctrl;
	unsigned		*ctrl;	/* fan control GPIOs. */
	struct gpio_fan_alarm	*alarm;	/* fan alarm GPIO. */
	/*
	 * Speed conversion array: rpm from/to GPIO bit field.
	 * This array _must_ be sorted in ascending rpm order.
	 */
	int			num_speed;
	struct gpio_fan_speed	*speed;
};

#endif /* __LINUX_GPIO_FAN_H */
