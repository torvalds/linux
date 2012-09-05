
/* include/linux/regulator/act8931.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_REGULATOR_act8931_H
#define __LINUX_REGULATOR_act8931_H

#include <linux/regulator/machine.h>

//#define ACT8931_START 30

#define ACT8931_LDO1  0                     //(0+ACT8931_START)
#define ACT8931_LDO2  1                    // (1+ACT8931_START)
#define ACT8931_LDO3  2                  //(2+ACT8931_START)
#define ACT8931_LDO4  3                //(3+ACT8931_START)


#define ACT8931_DCDC1 4                //(4+ACT8931_START)
#define ACT8931_DCDC2 5                //(5+ACT8931_START)
#define ACT8931_DCDC3 6                //(6+ACT8931_START)


#define act8931_NUM_REGULATORS 7
struct act8931;

/*
 * Register definitions to all subdrivers
 */
static u8 act8931_reg_read(struct act8931 *act8931, u8 reg);
static int act8931_set_bits(struct act8931 *act8931, u8 reg, u16 mask, u16 val);


#define act8931_BUCK1_SET_VOL_BASE 0x20
#define act8931_BUCK2_SET_VOL_BASE 0x30
#define act8931_BUCK3_SET_VOL_BASE 0x40
#define act8931_LDO1_SET_VOL_BASE 0x50
#define act8931_LDO2_SET_VOL_BASE 0x54
#define act8931_LDO3_SET_VOL_BASE 0x60
#define act8931_LDO4_SET_VOL_BASE 0x64

#define act8931_BUCK1_CONTR_BASE 0x22
#define act8931_BUCK2_CONTR_BASE 0x32
#define act8931_BUCK3_CONTR_BASE 0x42
#define act8931_LDO1_CONTR_BASE 0x51
#define act8931_LDO2_CONTR_BASE 0x55
#define act8931_LDO3_CONTR_BASE 0x61
#define act8931_LDO4_CONTR_BASE 0x65

#define BUCK_VOL_MASK 0x3f
#define LDO_VOL_MASK 0x3f

#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

struct act8931_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct act8931_platform_data {
	int num_regulators;
	int (*set_init)(struct act8931 *act8931);
	struct act8931_regulator_subdev *regulators;
};


#endif

