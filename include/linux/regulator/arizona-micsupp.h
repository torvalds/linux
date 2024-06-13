/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform data for Arizona micsupp regulator
 *
 * Copyright 2017 Cirrus Logic
 */

#ifndef ARIZONA_MICSUPP_H
#define ARIZONA_MICSUPP_H

struct regulator_init_data;

struct arizona_micsupp_pdata {
	/** Regulator configuration for micsupp */
	const struct regulator_init_data *init_data;
};

#endif
