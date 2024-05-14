/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform data for Arizona LDO1 regulator
 *
 * Copyright 2017 Cirrus Logic
 */

#ifndef ARIZONA_LDO1_H
#define ARIZONA_LDO1_H

struct regulator_init_data;

struct arizona_ldo1_pdata {
	/** Regulator configuration for LDO1 */
	const struct regulator_init_data *init_data;
};

#endif
