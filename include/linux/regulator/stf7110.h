/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Starfive Technology Co., Ltd.
 * Author: Mason Huo <mason.huo@starfivetech.com>
 */

#ifndef __LINUX_REGULATOR_STF7110_H
#define __LINUX_REGULATOR_STF7110_H

#define STF7110_MAX_REGULATORS	7


enum stf7110_reg_id {
	STF7110_ID_LDO_REG1 = 0,
	STF7110_ID_LDO_REG2,
	STF7110_ID_LDO_REG3,
	STF7110_ID_LDO_REG4,
	STF7110_ID_LDO_REG5,
	STF7110_ID_LDO_REG6,
	STF7110_ID_LDO_REG7,
};


#endif /* __LINUX_REGULATOR_STF7110_H */
