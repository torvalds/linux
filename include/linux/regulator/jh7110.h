/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Starfive Technology Co., Ltd.
 * Author: Mason Huo <mason.huo@starfivetech.com>
 */

#ifndef __LINUX_REGULATOR_JH7110_H
#define __LINUX_REGULATOR_JH7110_H

#define JH7110_MAX_REGULATORS	7


enum jh7110_reg_id {
	JH7110_ID_LDO_REG1 = 0,
	JH7110_ID_LDO_REG2,
	JH7110_ID_LDO_REG3,
	JH7110_ID_LDO_REG4,
	JH7110_ID_LDO_REG5,
	JH7110_ID_LDO_REG6,
	JH7110_ID_LDO_REG7,
};


#endif /* __LINUX_REGULATOR_JH7110_H */
