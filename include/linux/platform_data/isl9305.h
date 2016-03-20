/*
 * isl9305 - Intersil ISL9305 DCDC regulator
 *
 * Copyright 2014 Linaro Ltd
 *
 * Author: Mark Brown <broonie@kernel.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ISL9305_H
#define __ISL9305_H

#define ISL9305_DCD1 0
#define ISL9305_DCD2 1
#define ISL9305_LDO1 2
#define ISL9305_LDO2 3

#define ISL9305_MAX_REGULATOR ISL9305_LDO2

struct regulator_init_data;

struct isl9305_pdata {
	struct regulator_init_data *init_data[ISL9305_MAX_REGULATOR];
};

#endif
