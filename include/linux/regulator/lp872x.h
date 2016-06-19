/*
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LP872X_REGULATOR_H__
#define __LP872X_REGULATOR_H__

#include <linux/regulator/machine.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#define LP872X_MAX_REGULATORS		9

#define LP8720_ENABLE_DELAY		200
#define LP8725_ENABLE_DELAY		30000

enum lp872x_regulator_id {
	LP8720_ID_BASE,
	LP8720_ID_LDO1 = LP8720_ID_BASE,
	LP8720_ID_LDO2,
	LP8720_ID_LDO3,
	LP8720_ID_LDO4,
	LP8720_ID_LDO5,
	LP8720_ID_BUCK,

	LP8725_ID_BASE,
	LP8725_ID_LDO1 = LP8725_ID_BASE,
	LP8725_ID_LDO2,
	LP8725_ID_LDO3,
	LP8725_ID_LDO4,
	LP8725_ID_LDO5,
	LP8725_ID_LILO1,
	LP8725_ID_LILO2,
	LP8725_ID_BUCK1,
	LP8725_ID_BUCK2,

	LP872X_ID_MAX,
};

enum lp872x_dvs_state {
	DVS_LOW  = GPIOF_OUT_INIT_LOW,
	DVS_HIGH = GPIOF_OUT_INIT_HIGH,
};

enum lp872x_dvs_sel {
	SEL_V1,
	SEL_V2,
};

/**
 * lp872x_dvs
 * @gpio       : gpio pin number for dvs control
 * @vsel       : dvs selector for buck v1 or buck v2 register
 * @init_state : initial dvs pin state
 */
struct lp872x_dvs {
	int gpio;
	enum lp872x_dvs_sel vsel;
	enum lp872x_dvs_state init_state;
};

/**
 * lp872x_regdata
 * @id        : regulator id
 * @init_data : init data for each regulator
 */
struct lp872x_regulator_data {
	enum lp872x_regulator_id id;
	struct regulator_init_data *init_data;
};

/**
 * lp872x_platform_data
 * @general_config    : the value of LP872X_GENERAL_CFG register
 * @update_config     : if LP872X_GENERAL_CFG register is updated, set true
 * @regulator_data    : platform regulator id and init data
 * @dvs               : dvs data for buck voltage control
 * @enable_gpio       : gpio pin number for enable control
 */
struct lp872x_platform_data {
	u8 general_config;
	bool update_config;
	struct lp872x_regulator_data regulator_data[LP872X_MAX_REGULATORS];
	struct lp872x_dvs *dvs;
	int enable_gpio;
};

#endif
