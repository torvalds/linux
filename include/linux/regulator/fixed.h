/*
 * fixed.h
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef __REGULATOR_FIXED_H
#define __REGULATOR_FIXED_H

struct regulator_init_data;

struct fixed_voltage_config {
	const char *supply_name;
	int microvolts;
	struct regulator_init_data *init_data;
};

#endif
