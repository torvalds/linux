/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2012 ARM Limited
 */

#ifndef _LINUX_VEXPRESS_H
#define _LINUX_VEXPRESS_H

#include <linux/device.h>
#include <linux/regmap.h>

#define VEXPRESS_SITE_MB		0
#define VEXPRESS_SITE_DB1		1
#define VEXPRESS_SITE_DB2		2
#define VEXPRESS_SITE_MASTER		0xf

/* Config infrastructure */

void vexpress_config_set_master(u32 site);

/* Config regmap API */

struct regmap *devm_regmap_init_vexpress_config(struct device *dev);

#endif
