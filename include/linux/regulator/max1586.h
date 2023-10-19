/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * max1586.h  --  Voltage regulation for the Maxim 1586
 *
 * Copyright (C) 2008 Robert Jarzmik
 */

#ifndef REGULATOR_MAX1586
#define REGULATOR_MAX1586

#include <linux/regulator/machine.h>

#define MAX1586_V3 0
#define MAX1586_V6 1

/* precalculated values for v3_gain */
#define MAX1586_GAIN_NO_R24   1000000  /* 700000 .. 1475000 mV */
#define MAX1586_GAIN_R24_3k32 1051098  /* 735768 .. 1550369 mV */
#define MAX1586_GAIN_R24_5k11 1078648  /* 755053 .. 1591005 mV */
#define MAX1586_GAIN_R24_7k5  1115432  /* 780802 .. 1645262 mV */

/**
 * max1586_subdev_data - regulator data
 * @id: regulator Id (either MAX1586_V3 or MAX1586_V6)
 * @name: regulator cute name (example for V3: "vcc_core")
 * @platform_data: regulator init data (constraints, supplies, ...)
 */
struct max1586_subdev_data {
	int				id;
	const char			*name;
	struct regulator_init_data	*platform_data;
};

/**
 * max1586_platform_data - platform data for max1586
 * @num_subdevs: number of regulators used (may be 1 or 2)
 * @subdevs: regulator used
 *           At most, there will be a regulator for V3 and one for V6 voltages.
 * @v3_gain: gain on the V3 voltage output multiplied by 1e6.
 *           This can be calculated as ((1 + R24/R25 + R24/185.5kOhm) * 1e6)
 *           for an external resistor configuration as described in the
 *           data sheet (R25=100kOhm).
 */
struct max1586_platform_data {
	int num_subdevs;
	struct max1586_subdev_data *subdevs;
	int v3_gain;
};

#endif
