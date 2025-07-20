// SPDX-License-Identifier: GPL-2.0
/*
 * Clock drivers for TI DaVinci PLL and PSC controllers
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#ifndef __LINUX_CLK_DAVINCI_PLL_H___
#define __LINUX_CLK_DAVINCI_PLL_H___

#include <linux/device.h>
#include <linux/regmap.h>

/* function for registering clocks in early boot */
int da850_pll0_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);

#endif /* __LINUX_CLK_DAVINCI_PLL_H___ */
