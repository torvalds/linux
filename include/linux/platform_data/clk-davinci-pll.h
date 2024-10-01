// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock driver for TI Davinci SoCs
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#ifndef __LINUX_PLATFORM_DATA_CLK_DAVINCI_PLL_H__
#define __LINUX_PLATFORM_DATA_CLK_DAVINCI_PLL_H__

#include <linux/regmap.h>

/**
 * davinci_pll_platform_data
 * @cfgchip: CFGCHIP syscon regmap
 */
struct davinci_pll_platform_data {
	struct regmap *cfgchip;
};

#endif /* __LINUX_PLATFORM_DATA_CLK_DAVINCI_PLL_H__ */
