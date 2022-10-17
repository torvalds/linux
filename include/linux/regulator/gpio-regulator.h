/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gpio-regulator.h
 *
 * Copyright 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * based on fixed.h
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Copyright (c) 2009 Nokia Corporation
 * Roger Quadros <ext-roger.quadros@nokia.com>
 */

#ifndef __REGULATOR_GPIO_H
#define __REGULATOR_GPIO_H

#include <linux/gpio/consumer.h>

struct regulator_init_data;

enum regulator_type;

/**
 * struct gpio_regulator_state - state description
 * @value:		microvolts or microamps
 * @gpios:		bitfield of gpio target-states for the value
 *
 * This structure describes a supported setting of the regulator
 * and the necessary gpio-state to achieve it.
 *
 * The n-th bit in the bitfield describes the state of the n-th GPIO
 * from the gpios-array defined in gpio_regulator_config below.
 */
struct gpio_regulator_state {
	int value;
	int gpios;
};

/**
 * struct gpio_regulator_config - config structure
 * @supply_name:	Name of the regulator supply
 * @input_supply:	Name of the input regulator supply
 * @enabled_at_boot:	Whether regulator has been enabled at
 *			boot or not. 1 = Yes, 0 = No
 *			This is used to keep the regulator at
 *			the default state
 * @startup_delay:	Start-up time in microseconds
 * @gflags:		Array of GPIO configuration flags for initial
 *			states
 * @ngpios:		Number of GPIOs and configurations available
 * @states:		Array of gpio_regulator_state entries describing
 *			the gpio state for specific voltages
 * @nr_states:		Number of states available
 * @regulator_type:	either REGULATOR_CURRENT or REGULATOR_VOLTAGE
 * @init_data:		regulator_init_data
 *
 * This structure contains gpio-voltage regulator configuration
 * information that must be passed by platform code to the
 * gpio-voltage regulator driver.
 */
struct gpio_regulator_config {
	const char *supply_name;
	const char *input_supply;

	unsigned enabled_at_boot:1;
	unsigned startup_delay;

	enum gpiod_flags *gflags;
	int ngpios;

	struct gpio_regulator_state *states;
	int nr_states;

	enum regulator_type type;
	struct regulator_init_data *init_data;
};

#endif
