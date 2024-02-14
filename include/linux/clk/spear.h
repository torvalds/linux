/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 STMicroelectronics - All Rights Reserved
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 */

#ifndef __LINUX_CLK_SPEAR_H
#define __LINUX_CLK_SPEAR_H

#ifdef CONFIG_ARCH_SPEAR3XX
void __init spear3xx_clk_init(void __iomem *misc_base,
			      void __iomem *soc_config_base);
#else
static inline void __init spear3xx_clk_init(void __iomem *misc_base,
					    void __iomem *soc_config_base) {}
#endif

#ifdef CONFIG_ARCH_SPEAR6XX
void __init spear6xx_clk_init(void __iomem *misc_base);
#else
static inline void __init spear6xx_clk_init(void __iomem *misc_base) {}
#endif

#ifdef CONFIG_MACH_SPEAR1310
void __init spear1310_clk_init(void __iomem *misc_base, void __iomem *ras_base);
#else
static inline void spear1310_clk_init(void __iomem *misc_base, void __iomem *ras_base) {}
#endif

#ifdef CONFIG_MACH_SPEAR1340
void __init spear1340_clk_init(void __iomem *misc_base);
#else
static inline void spear1340_clk_init(void __iomem *misc_base) {}
#endif

#endif
