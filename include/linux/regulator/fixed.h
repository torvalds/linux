/*
 * fixed.h
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Copyright (c) 2009 Nokia Corporation
 * Roger Quadros <ext-roger.quadros@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef __REGULATOR_FIXED_H
#define __REGULATOR_FIXED_H

struct regulator_init_data;

/**
 * struct fixed_voltage_config - fixed_voltage_config structure
 * @supply_name:	Name of the regulator supply
 * @input_supply:	Name of the input regulator supply
 * @microvolts:		Output voltage of regulator
 * @gpio:		GPIO to use for enable control
 * 			set to -EINVAL if not used
 * @startup_delay:	Start-up time in microseconds
 * @gpio_is_open_drain: Gpio pin is open drain or normal type.
 *			If it is open drain type then HIGH will be set
 *			through PULL-UP with setting gpio as input
 *			and low will be set as gpio-output with driven
 *			to low. For non-open-drain case, the gpio will
 *			will be in output and drive to low/high accordingly.
 * @enable_high:	Polarity of enable GPIO
 *			1 = Active high, 0 = Active low
 * @enabled_at_boot:	Whether regulator has been enabled at
 * 			boot or not. 1 = Yes, 0 = No
 * 			This is used to keep the regulator at
 * 			the default state
 * @init_data:		regulator_init_data
 *
 * This structure contains fixed voltage regulator configuration
 * information that must be passed by platform code to the fixed
 * voltage regulator driver.
 */
struct fixed_voltage_config {
	const char *supply_name;
	const char *input_supply;
	int microvolts;
	int gpio;
	unsigned startup_delay;
	unsigned gpio_is_open_drain:1;
	unsigned enable_high:1;
	unsigned enabled_at_boot:1;
	struct regulator_init_data *init_data;
};

struct regulator_consumer_supply;

#if IS_ENABLED(CONFIG_REGULATOR)
struct platform_device *regulator_register_fixed(int id,
		struct regulator_consumer_supply *supplies, int num_supplies);
#else
static inline struct platform_device *regulator_register_fixed(int id,
		struct regulator_consumer_supply *supplies, int num_supplies)
{
	return NULL;
}
#endif

#endif
