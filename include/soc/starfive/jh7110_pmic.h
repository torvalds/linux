/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PMIC driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 changhuang <changhuang.liang@starfivetech.com>
 */

#ifndef __SOC_STARFIVE_JH7110_PMIC_H__
#define __SOC_STARFIVE_JH7110_PMIC_H__

#include <linux/bits.h>
#include <linux/types.h>

#define PMIC_REG_BASE		0x80

enum pmic_reg {
	POWER_SW_0_REG = PMIC_REG_BASE+0x00,
	POWER_SW_1_REG = PMIC_REG_BASE+0x01,
};

enum pmic_power_domian {
	POWER_SW_0_VDD18_HDMI = 0,
	POWER_SW_0_VDD18_MIPITX,
	POWER_SW_0_VDD18_MIPIRX,
	POWER_SW_0_VDD09_HDMI,
	POWER_SW_0_VDD09_MIPITX,
	POWER_SW_0_VDD09_MIPIRX,
};

struct pmic_dev {
	struct i2c_client *i2c_client;
};

/**
 * @pmic_dev: pmic device.
 * @reg: see enum pmic_reg.
 * @domian: see enum pmic_power_domian.
 * @on: power swtich, 1 or 0.
 */
void pmic_set_domain(struct pmic_dev *pmic_dev, u8 reg,
		u8 domain, u8 on);

#endif
