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
 * @startup_delay:	Start-up time in microseconds
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
	unsigned startup_delay;
	unsigned enabled_at_boot:1;
	struct regulator_init_data *init_data;
};

struct regulator_consumer_supply;

#if IS_ENABLED(CONFIG_REGULATOR)
struct platform_device *regulator_register_always_on(int id, const char *name,
		struct regulator_consumer_supply *supplies, int num_supplies, int uv);
#else
static inline struct platform_device *regulator_register_always_on(int id, const char *name,
		struct regulator_consumer_supply *supplies, int num_supplies, int uv)
{
	return NULL;
}
#endif

#define regulator_register_fixed(id, s, ns) regulator_register_always_on(id, \
						"fixed-dummy", s, ns, 0)

#endif
