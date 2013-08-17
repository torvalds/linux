/*
 * max8660.h  --  Voltage regulation for the Maxim 8660/8661
 *
 * Copyright (C) 2009 Wolfram Sang, Pengutronix e.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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

#ifndef __LINUX_REGULATOR_MAX8660_H
#define __LINUX_REGULATOR_MAX8660_H

#include <linux/regulator/machine.h>

enum {
	MAX8660_V3,
	MAX8660_V4,
	MAX8660_V5,
	MAX8660_V6,
	MAX8660_V7,
	MAX8660_V_END,
};

/**
 * max8660_subdev_data - regulator subdev data
 * @id: regulator id
 * @name: regulator name
 * @platform_data: regulator init data
 */
struct max8660_subdev_data {
	int				id;
	char				*name;
	struct regulator_init_data	*platform_data;
};

/**
 * max8660_platform_data - platform data for max8660
 * @num_subdevs: number of regulators used
 * @subdevs: pointer to regulators used
 * @en34_is_high: if EN34 is driven high, regulators cannot be en-/disabled.
 */
struct max8660_platform_data {
	int num_subdevs;
	struct max8660_subdev_data *subdevs;
	unsigned en34_is_high:1;
};
#endif
