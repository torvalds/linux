/*
 * max1586.h  --  Voltage regulation for the Maxim 1586
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef REGULATOR_MAX1586
#define REGULATOR_MAX1586

#include <linux/regulator/machine.h>

#define MAX1586_V3 0
#define MAX1586_V6 1

/**
 * max1586_subdev_data - regulator data
 * @id: regulator Id (either MAX1586_V3 or MAX1586_V6)
 * @name: regulator cute name (example for V3: "vcc_core")
 * @platform_data: regulator init data (contraints, supplies, ...)
 */
struct max1586_subdev_data {
	int				id;
	char				*name;
	struct regulator_init_data	*platform_data;
};

/**
 * max1586_platform_data - platform data for max1586
 * @num_subdevs: number of regultors used (may be 1 or 2)
 * @subdevs: regulator used
 *           At most, there will be a regulator for V3 and one for V6 voltages.
 */
struct max1586_platform_data {
	int num_subdevs;
	struct max1586_subdev_data *subdevs;
};

#endif
