/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface of Maxim max8649
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *      Haojian Zhuang <haojian.zhuang@marvell.com>
 */

#ifndef __LINUX_REGULATOR_MAX8649_H
#define	__LINUX_REGULATOR_MAX8649_H

#include <linux/regulator/machine.h>

enum {
	MAX8649_EXTCLK_26MHZ = 0,
	MAX8649_EXTCLK_13MHZ,
	MAX8649_EXTCLK_19MHZ,	/* 19.2MHz */
};

enum {
	MAX8649_RAMP_32MV = 0,
	MAX8649_RAMP_16MV,
	MAX8649_RAMP_8MV,
	MAX8649_RAMP_4MV,
	MAX8649_RAMP_2MV,
	MAX8649_RAMP_1MV,
	MAX8649_RAMP_0_5MV,
	MAX8649_RAMP_0_25MV,
};

struct max8649_platform_data {
	struct regulator_init_data *regulator;

	unsigned	mode:2;		/* bit[1:0] = VID1,VID0 */
	unsigned	extclk_freq:2;
	unsigned	extclk:1;
	unsigned	ramp_timing:3;
	unsigned	ramp_down:1;
};

#endif	/* __LINUX_REGULATOR_MAX8649_H */
