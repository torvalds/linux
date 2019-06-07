/*
 * Platform data for Arizona LDO1 regulator
 *
 * Copyright 2017 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARIZONA_LDO1_H
#define ARIZONA_LDO1_H

struct regulator_init_data;

struct arizona_ldo1_pdata {
	/** Regulator configuration for LDO1 */
	const struct regulator_init_data *init_data;
};

#endif
