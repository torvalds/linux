/* s5m87xx.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __LINUX_MFD_S5M_PMIC_H
#define __LINUX_MFD_S5M_PMIC_H

#include <linux/regulator/machine.h>

/* S5M8767 regulator ids */
enum s5m8767_regulators {
	S5M8767_LDO1,
	S5M8767_LDO2,
	S5M8767_LDO3,
	S5M8767_LDO4,
	S5M8767_LDO5,
	S5M8767_LDO6,
	S5M8767_LDO7,
	S5M8767_LDO8,
	S5M8767_LDO9,
	S5M8767_LDO10,
	S5M8767_LDO11,
	S5M8767_LDO12,
	S5M8767_LDO13,
	S5M8767_LDO14,
	S5M8767_LDO15,
	S5M8767_LDO16,
	S5M8767_LDO17,
	S5M8767_LDO18,
	S5M8767_LDO19,
	S5M8767_LDO20,
	S5M8767_LDO21,
	S5M8767_LDO22,
	S5M8767_LDO23,
	S5M8767_LDO24,
	S5M8767_LDO25,
	S5M8767_LDO26,
	S5M8767_LDO27,
	S5M8767_LDO28,
	S5M8767_BUCK1,
	S5M8767_BUCK2,
	S5M8767_BUCK3,
	S5M8767_BUCK4,
	S5M8767_BUCK5,
	S5M8767_BUCK6,
	S5M8767_BUCK7,
	S5M8767_BUCK8,
	S5M8767_BUCK9,
	S5M8767_AP_EN32KHZ,
	S5M8767_CP_EN32KHZ,
	S5M8767_BT_EN32KHZ,

	S5M8767_REG_MAX,
};


#define S5M8767_PMIC_EN_SHIFT	6

/* S5M8763 regulator ids */
enum s5m8763_regulators {
	S5M8763_LDO1,
	S5M8763_LDO2,
	S5M8763_LDO3,
	S5M8763_LDO4,
	S5M8763_LDO5,
	S5M8763_LDO6,
	S5M8763_LDO7,
	S5M8763_LDO8,
	S5M8763_LDO9,
	S5M8763_LDO10,
	S5M8763_LDO11,
	S5M8763_LDO12,
	S5M8763_LDO13,
	S5M8763_LDO14,
	S5M8763_LDO15,
	S5M8763_LDO16,
	S5M8763_BUCK1,
	S5M8763_BUCK2,
	S5M8763_BUCK3,
	S5M8763_BUCK4,
	S5M8763_AP_EN32KHZ,
	S5M8763_CP_EN32KHZ,
	S5M8763_ENCHGVI,
	S5M8763_ESAFEUSB1,
	S5M8763_ESAFEUSB2,
};

/**
 * s5m87xx_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct s5m_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
};

struct s5m_opmode_data {
	int id;
	int mode;
};

enum s5m_opmode {
	S5M_OPMODE_NORMAL,
	S5M_OPMODE_LP,
	S5M_OPMODE_STANDBY,
};

#endif /*  __LINUX_MFD_S5M_PMIC_H */
