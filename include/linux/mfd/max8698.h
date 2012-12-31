/*
 * max8698.h - Voltage regulator driver for the Maxim 8698
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
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
 *
 *  2010.10.25
 *  Modified by Taekki Kim <taekki.kim@samsung.com>
 */

#ifndef __LINUX_MFD_MAX8698_H
#define __LINUX_MFD_MAX8698_H

#include <linux/regulator/machine.h>

/* MAX 8698 regulator ids */
enum {
	MAX8698_LDO1 = 1,
	MAX8698_LDO2,
	MAX8698_LDO3,
	MAX8698_LDO4,
	MAX8698_LDO5,
	MAX8698_LDO6,
	MAX8698_LDO7,
	MAX8698_LDO8,
	MAX8698_LDO9,
	MAX8698_BUCK1,
	MAX8698_BUCK2,
	MAX8698_BUCK3,
};

/**
 * max8698_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct max8698_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
};

/**
 * struct max8698_board - packages regulator init data
 * @num_regulators: number of regultors used
 * @regulators: array of defined regulators
 */

struct max8698_platform_data {
	int				num_regulators;
	struct max8698_regulator_data	*regulators;

       int		dvsarm1;
       int		dvsarm2;
       int		dvsarm3;
       int		dvsarm4;
       int		dvsint1;
       int		dvsint2;
       int		set1;
       int		set2;
       int		set3;
};

#endif /*  __LINUX_MFD_MAX8698_H */
