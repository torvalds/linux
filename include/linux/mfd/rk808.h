/*
 * rk808.h for Rockchip RK808
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __LINUX_REGULATOR_rk808_H
#define __LINUX_REGULATOR_rk808_H

#include <linux/regulator/machine.h>
#include <linux/regmap.h>

/*
 * rk808 Global Register Map.
 */

#define RK808_DCDC1	0 /* (0+RK808_START) */
#define RK808_LDO1	4 /* (4+RK808_START) */
#define RK808_NUM_REGULATORS   14

enum rk808_reg {
	RK808_ID_DCDC1,
	RK808_ID_DCDC2,
	RK808_ID_DCDC3,
	RK808_ID_DCDC4,
	RK808_ID_LDO1,
	RK808_ID_LDO2,
	RK808_ID_LDO3,
	RK808_ID_LDO4,
	RK808_ID_LDO5,
	RK808_ID_LDO6,
	RK808_ID_LDO7,
	RK808_ID_LDO8,
	RK808_ID_SWITCH1,
	RK808_ID_SWITCH2,
};

#define RK808_SECONDS_REG	0x00
#define RK808_MINUTES_REG	0x01
#define RK808_HOURS_REG		0x02
#define RK808_DAYS_REG		0x03
#define RK808_MONTHS_REG	0x04
#define RK808_YEARS_REG		0x05
#define RK808_WEEKS_REG		0x06
#define RK808_ALARM_SECONDS_REG	0x08
#define RK808_ALARM_MINUTES_REG	0x09
#define RK808_ALARM_HOURS_REG	0x0a
#define RK808_ALARM_DAYS_REG	0x0b
#define RK808_ALARM_MONTHS_REG	0x0c
#define RK808_ALARM_YEARS_REG	0x0d
#define RK808_RTC_CTRL_REG	0x10
#define RK808_RTC_STATUS_REG	0x11
#define RK808_RTC_INT_REG	0x12
#define RK808_RTC_COMP_LSB_REG	0x13
#define RK808_RTC_COMP_MSB_REG	0x14
#define RK808_CLK32OUT_REG	0x20
#define RK808_VB_MON_REG	0x21
#define RK808_THERMAL_REG	0x22
#define RK808_DCDC_EN_REG	0x23
#define RK808_LDO_EN_REG	0x24
#define RK808_SLEEP_SET_OFF_REG1	0x25
#define RK808_SLEEP_SET_OFF_REG2	0x26
#define RK808_DCDC_UV_STS_REG	0x27
#define RK808_DCDC_UV_ACT_REG	0x28
#define RK808_LDO_UV_STS_REG	0x29
#define RK808_LDO_UV_ACT_REG	0x2a
#define RK808_DCDC_PG_REG	0x2b
#define RK808_LDO_PG_REG	0x2c
#define RK808_VOUT_MON_TDB_REG	0x2d
#define RK808_BUCK1_CONFIG_REG		0x2e
#define RK808_BUCK1_ON_VSEL_REG		0x2f
#define RK808_BUCK1_SLP_VSEL_REG	0x30
#define RK808_BUCK1_DVS_VSEL_REG	0x31
#define RK808_BUCK2_CONFIG_REG		0x32
#define RK808_BUCK2_ON_VSEL_REG		0x33
#define RK808_BUCK2_SLP_VSEL_REG	0x34
#define RK808_BUCK2_DVS_VSEL_REG	0x35
#define RK808_BUCK3_CONFIG_REG		0x36
#define RK808_BUCK4_CONFIG_REG		0x37
#define RK808_BUCK4_ON_VSEL_REG		0x38
#define RK808_BUCK4_SLP_VSEL_REG	0x39
#define RK808_BOOST_CONFIG_REG		0x3a
#define RK808_LDO1_ON_VSEL_REG		0x3b
#define RK808_LDO1_SLP_VSEL_REG		0x3c
#define RK808_LDO2_ON_VSEL_REG		0x3d
#define RK808_LDO2_SLP_VSEL_REG		0x3e
#define RK808_LDO3_ON_VSEL_REG		0x3f
#define RK808_LDO3_SLP_VSEL_REG		0x40
#define RK808_LDO4_ON_VSEL_REG		0x41
#define RK808_LDO4_SLP_VSEL_REG		0x42
#define RK808_LDO5_ON_VSEL_REG		0x43
#define RK808_LDO5_SLP_VSEL_REG		0x44
#define RK808_LDO6_ON_VSEL_REG		0x45
#define RK808_LDO6_SLP_VSEL_REG		0x46
#define RK808_LDO7_ON_VSEL_REG		0x47
#define RK808_LDO7_SLP_VSEL_REG		0x48
#define RK808_LDO8_ON_VSEL_REG		0x49
#define RK808_LDO8_SLP_VSEL_REG		0x4a
#define RK808_DEVCTRL_REG	0x4b
#define RK808_INT_STS_REG1	0x4c
#define RK808_INT_STS_MSK_REG1	0x4d
#define RK808_INT_STS_REG2	0x4e
#define RK808_INT_STS_MSK_REG2	0x4f
#define RK808_IO_POL_REG	0x50

/* IRQ Definitions */
#define RK808_IRQ_VOUT_LO	0
#define RK808_IRQ_VB_LO		1
#define RK808_IRQ_PWRON		2
#define RK808_IRQ_PWRON_LP	3
#define RK808_IRQ_HOTDIE	4
#define RK808_IRQ_RTC_ALARM	5
#define RK808_IRQ_RTC_PERIOD	6
#define RK808_IRQ_PLUG_IN_INT	7
#define RK808_IRQ_PLUG_OUT_INT	8
#define RK808_NUM_IRQ		9

#define RK808_IRQ_VOUT_LO_MSK		BIT(0)
#define RK808_IRQ_VB_LO_MSK		BIT(1)
#define RK808_IRQ_PWRON_MSK		BIT(2)
#define RK808_IRQ_PWRON_LP_MSK		BIT(3)
#define RK808_IRQ_HOTDIE_MSK		BIT(4)
#define RK808_IRQ_RTC_ALARM_MSK		BIT(5)
#define RK808_IRQ_RTC_PERIOD_MSK	BIT(6)
#define RK808_IRQ_PLUG_IN_INT_MSK	BIT(0)
#define RK808_IRQ_PLUG_OUT_INT_MSK	BIT(1)

#define RK808_VBAT_LOW_2V8	0x00
#define RK808_VBAT_LOW_2V9	0x01
#define RK808_VBAT_LOW_3V0	0x02
#define RK808_VBAT_LOW_3V1	0x03
#define RK808_VBAT_LOW_3V2	0x04
#define RK808_VBAT_LOW_3V3	0x05
#define RK808_VBAT_LOW_3V4	0x06
#define RK808_VBAT_LOW_3V5	0x07
#define VBAT_LOW_VOL_MASK	(0x07 << 0)
#define EN_VABT_LOW_SHUT_DOWN	(0x00 << 4)
#define EN_VBAT_LOW_IRQ		(0x1 << 4)
#define VBAT_LOW_ACT_MASK	(0x1 << 4)

#define BUCK_ILMIN_MASK		(7 << 0)
#define BOOST_ILMIN_MASK	(7 << 0)
#define BUCK1_RATE_MASK		(3 << 3)
#define BUCK2_RATE_MASK		(3 << 3)
#define MASK_ALL	0xff

#define SWITCH2_EN	BIT(6)
#define SWITCH1_EN	BIT(5)
#define DEV_OFF_RST	BIT(3)

#define VB_LO_ACT		BIT(4)
#define VB_LO_SEL_3500MV	(7 << 0)

#define VOUT_LO_INT	BIT(0)
#define CLK32KOUT2_EN	BIT(0)

enum {
	BUCK_ILMIN_50MA,
	BUCK_ILMIN_100MA,
	BUCK_ILMIN_150MA,
	BUCK_ILMIN_200MA,
	BUCK_ILMIN_250MA,
	BUCK_ILMIN_300MA,
	BUCK_ILMIN_350MA,
	BUCK_ILMIN_400MA,
};

enum {
	BOOST_ILMIN_75MA,
	BOOST_ILMIN_100MA,
	BOOST_ILMIN_125MA,
	BOOST_ILMIN_150MA,
	BOOST_ILMIN_175MA,
	BOOST_ILMIN_200MA,
	BOOST_ILMIN_225MA,
	BOOST_ILMIN_250MA,
};

struct rk808 {
	struct i2c_client *i2c;
	struct regmap_irq_chip_data *irq_data;
	struct regmap *regmap;
};
#endif /* __LINUX_REGULATOR_rk808_H */
