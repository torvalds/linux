/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SuperH Video Output Unit (VOU) driver header
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */
#ifndef SH_VOU_H
#define SH_VOU_H

#include <linux/i2c.h>

/* Bus flags */
#define SH_VOU_PCLK_FALLING	(1 << 0)
#define SH_VOU_HSYNC_LOW	(1 << 1)
#define SH_VOU_VSYNC_LOW	(1 << 2)

enum sh_vou_bus_fmt {
	SH_VOU_BUS_8BIT,
	SH_VOU_BUS_16BIT,
	SH_VOU_BUS_BT656,
};

struct sh_vou_pdata {
	enum sh_vou_bus_fmt bus_fmt;
	int i2c_adap;
	struct i2c_board_info *board_info;
	unsigned long flags;
};

#endif
