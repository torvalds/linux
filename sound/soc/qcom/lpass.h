/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * lpass.h - Definitions for the QTi LPASS
 */

#ifndef __LPASS_H__
#define __LPASS_H__

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define LPASS_AHBIX_CLOCK_FREQUENCY		131072000

/* Both the CPU DAI and platform drivers will access this data */
struct lpass_data {

	/* AHB-I/X bus clocks inside the low-power audio subsystem (LPASS) */
	struct clk *ahbix_clk;

	/* MI2S system clock */
	struct clk *mi2s_osr_clk;

	/* MI2S bit clock (derived from system clock by a divider */
	struct clk *mi2s_bit_clk;

	/* low-power audio interface (LPAIF) registers */
	void __iomem *lpaif;

	/* regmap backed by the low-power audio interface (LPAIF) registers */
	struct regmap *lpaif_map;

	/* interrupts from the low-power audio interface (LPAIF) */
	int lpaif_irq;
};

/* register the platform driver from the CPU DAI driver */
int asoc_qcom_lpass_platform_register(struct platform_device *);

#endif /* __LPASS_H__ */
