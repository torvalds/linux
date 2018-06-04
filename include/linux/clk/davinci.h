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

#ifdef CONFIG_ARCH_DAVINCI_DA830
int da830_pll_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DA850
int da850_pll0_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM355
int dm355_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
int dm355_psc_init(struct device *dev, void __iomem *base);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM365
int dm365_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
int dm365_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
int dm365_psc_init(struct device *dev, void __iomem *base);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM644x
int dm644x_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
int dm644x_psc_init(struct device *dev, void __iomem *base);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM646x
int dm646x_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
int dm646x_psc_init(struct device *dev, void __iomem *base);
#endif

#endif /* __LINUX_CLK_DAVINCI_PLL_H___ */
