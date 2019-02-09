/*
 * Platform data for Arizona micsupp regulator
 *
 * Copyright 2017 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARIZONA_MICSUPP_H
#define ARIZONA_MICSUPP_H

struct regulator_init_data;

struct arizona_micsupp_pdata {
	/** Regulator configuration for micsupp */
	const struct regulator_init_data *init_data;
};

#endif
