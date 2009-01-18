/*
 * A simple GPIO VBUS sensing driver for B peripheral only devices
 * with internal transceivers.
 * Optionally D+ pullup can be controlled by a second GPIO.
 *
 * Copyright (c) 2008 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/**
 * struct gpio_vbus_mach_info - configuration for gpio_vbus
 * @gpio_vbus: VBUS sensing GPIO
 * @gpio_pullup: optional D+ or D- pullup GPIO (else negative/invalid)
 * @gpio_vbus_inverted: true if gpio_vbus is active low
 * @gpio_pullup_inverted: true if gpio_pullup is active low
 *
 * The VBUS sensing GPIO should have a pulldown, which will normally be
 * part of a resistor ladder turning a 4.0V-5.25V level on VBUS into a
 * value the GPIO detects as active.  Some systems will use comparators.
 */
struct gpio_vbus_mach_info {
	int gpio_vbus;
	int gpio_pullup;
	bool gpio_vbus_inverted;
	bool gpio_pullup_inverted;
};
