/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2018 ROHM Semiconductors */

#ifndef __LINUX_MFD_ROHM_H__
#define __LINUX_MFD_ROHM_H__

enum {
	ROHM_CHIP_TYPE_BD71837 = 0,
	ROHM_CHIP_TYPE_BD71847,
	ROHM_CHIP_TYPE_BD70528,
	ROHM_CHIP_TYPE_AMOUNT
};

struct rohm_regmap_dev {
	unsigned int chip_type;
	struct device *dev;
	struct regmap *regmap;
};

#endif
