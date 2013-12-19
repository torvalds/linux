/*
 * LP8755 High Performance Power Management Unit Driver:System Interface Driver
 *
 *			Copyright (C) 2012 Texas Instruments
 *
 * Author: Daniel(Geon Si) Jeong <daniel.jeong@ti.com>
 *             G.Shark Jeong <gshark.jeong@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _LP8755_H
#define _LP8755_H

#include <linux/regulator/consumer.h>

#define LP8755_NAME "lp8755-regulator"
/*
 *PWR FAULT : power fault detected
 *OCP : over current protect activated
 *OVP : over voltage protect activated
 *TEMP_WARN : thermal warning
 *TEMP_SHDN : thermal shutdonw detected
 *I_LOAD : current measured
 */
#define LP8755_EVENT_PWR_FAULT REGULATOR_EVENT_FAIL
#define LP8755_EVENT_OCP REGULATOR_EVENT_OVER_CURRENT
#define LP8755_EVENT_OVP 0x10000
#define LP8755_EVENT_TEMP_WARN 0x2000
#define LP8755_EVENT_TEMP_SHDN REGULATOR_EVENT_OVER_TEMP
#define LP8755_EVENT_I_LOAD	0x40000

enum lp8755_bucks {
	LP8755_BUCK0 = 0,
	LP8755_BUCK1,
	LP8755_BUCK2,
	LP8755_BUCK3,
	LP8755_BUCK4,
	LP8755_BUCK5,
	LP8755_BUCK_MAX,
};

/**
 * multiphase configuration options
 */
enum lp8755_mphase_config {
	MPHASE_CONF0,
	MPHASE_CONF1,
	MPHASE_CONF2,
	MPHASE_CONF3,
	MPHASE_CONF4,
	MPHASE_CONF5,
	MPHASE_CONF6,
	MPHASE_CONF7,
	MPHASE_CONF8,
	MPHASE_CONF_MAX
};

/**
 * struct lp8755_platform_data
 * @mphase_type : Multiphase Switcher Configurations.
 * @buck_data   : buck0~6 init voltage in uV
 */
struct lp8755_platform_data {
	int mphase;
	struct regulator_init_data *buck_data[LP8755_BUCK_MAX];
};
#endif
