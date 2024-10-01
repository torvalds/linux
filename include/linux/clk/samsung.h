/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __LINUX_CLK_SAMSUNG_H_
#define __LINUX_CLK_SAMSUNG_H_

#include <linux/compiler_types.h>

struct device_node;

#ifdef CONFIG_S3C64XX_COMMON_CLK
void s3c64xx_clk_init(struct device_node *np, unsigned long xtal_f,
		      unsigned long xusbxti_f, bool s3c6400,
		      void __iomem *base);
#else
static inline void s3c64xx_clk_init(struct device_node *np,
				    unsigned long xtal_f,
				    unsigned long xusbxti_f,
				    bool s3c6400, void __iomem *base) { }
#endif /* CONFIG_S3C64XX_COMMON_CLK */

#ifdef CONFIG_S3C2410_COMMON_CLK
void s3c2410_common_clk_init(struct device_node *np, unsigned long xti_f,
			     int current_soc,
			     void __iomem *reg_base);
#else
static inline void s3c2410_common_clk_init(struct device_node *np,
					   unsigned long xti_f,
					   int current_soc,
					   void __iomem *reg_base) { }
#endif /* CONFIG_S3C2410_COMMON_CLK */

#ifdef CONFIG_S3C2412_COMMON_CLK
void s3c2412_common_clk_init(struct device_node *np, unsigned long xti_f,
			     unsigned long ext_f, void __iomem *reg_base);
#else
static inline void s3c2412_common_clk_init(struct device_node *np,
					   unsigned long xti_f,
					   unsigned long ext_f,
					   void __iomem *reg_base) { }
#endif /* CONFIG_S3C2412_COMMON_CLK */

#ifdef CONFIG_S3C2443_COMMON_CLK
void s3c2443_common_clk_init(struct device_node *np, unsigned long xti_f,
			     int current_soc,
			     void __iomem *reg_base);
#else
static inline void s3c2443_common_clk_init(struct device_node *np,
					   unsigned long xti_f,
					   int current_soc,
					   void __iomem *reg_base) { }
#endif /* CONFIG_S3C2443_COMMON_CLK */

#endif /* __LINUX_CLK_SAMSUNG_H_ */
