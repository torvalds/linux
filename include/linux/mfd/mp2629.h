/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Monolithic Power Systems, Inc
 */

#ifndef __MP2629_H__
#define __MP2629_H__

#include <linux/device.h>
#include <linux/regmap.h>

struct mp2629_data {
	struct device *dev;
	struct regmap *regmap;
};

#endif
