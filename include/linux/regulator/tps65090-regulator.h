/*
 * Regulator driver interface for TI TPS65090 PMIC family
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.

 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __REGULATOR_TPS65090_H
#define __REGULATOR_TPS65090_H

#include <linux/regulator/machine.h>

#define tps65090_rails(_name) "tps65090_"#_name

enum {
	TPS65090_ID_DCDC1,
	TPS65090_ID_DCDC2,
	TPS65090_ID_DCDC3,
	TPS65090_ID_FET1,
	TPS65090_ID_FET2,
	TPS65090_ID_FET3,
	TPS65090_ID_FET4,
	TPS65090_ID_FET5,
	TPS65090_ID_FET6,
	TPS65090_ID_FET7,
};

/*
 * struct tps65090_regulator_platform_data
 *
 * @regulator: The regulator init data.
 * @slew_rate_uV_per_us: Slew rate microvolt per microsec.
 */

struct tps65090_regulator_platform_data {
	struct regulator_init_data regulator;
};

#endif	/* __REGULATOR_TPS65090_H */
