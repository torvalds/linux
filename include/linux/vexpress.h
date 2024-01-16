/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2012 ARM Limited
 */

#ifndef _LINUX_VEXPRESS_H
#define _LINUX_VEXPRESS_H

#include <linux/device.h>
#include <linux/regmap.h>

/* Config regmap API */

struct regmap *devm_regmap_init_vexpress_config(struct device *dev);

#endif
