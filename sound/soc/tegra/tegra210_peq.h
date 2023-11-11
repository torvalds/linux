/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra210_peq.h - Definitions for Tegra210 PEQ driver
 *
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 */

#ifndef __TEGRA210_PEQ_H__
#define __TEGRA210_PEQ_H__

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/soc.h>

/* Register offsets from PEQ base */
#define TEGRA210_PEQ_SOFT_RESET				0x0
#define TEGRA210_PEQ_CG					0x4
#define TEGRA210_PEQ_STATUS				0x8
#define TEGRA210_PEQ_CFG				0xc
#define TEGRA210_PEQ_CFG_RAM_CTRL			0x10
#define TEGRA210_PEQ_CFG_RAM_DATA			0x14
#define TEGRA210_PEQ_CFG_RAM_SHIFT_CTRL			0x18
#define TEGRA210_PEQ_CFG_RAM_SHIFT_DATA			0x1c

/* Fields in TEGRA210_PEQ_CFG */
#define TEGRA210_PEQ_CFG_BIQUAD_STAGES_SHIFT		2
#define TEGRA210_PEQ_CFG_BIQUAD_STAGES_MASK		(0xf << TEGRA210_PEQ_CFG_BIQUAD_STAGES_SHIFT)

#define TEGRA210_PEQ_CFG_MODE_SHIFT			0
#define TEGRA210_PEQ_CFG_MODE_MASK			(0x1 << TEGRA210_PEQ_CFG_MODE_SHIFT)

#define TEGRA210_PEQ_RAM_CTRL_RW_READ			0
#define TEGRA210_PEQ_RAM_CTRL_RW_WRITE			(1 << 14)
#define TEGRA210_PEQ_RAM_CTRL_ADDR_INIT_EN		(1 << 13)
#define TEGRA210_PEQ_RAM_CTRL_SEQ_ACCESS_EN		(1 << 12)
#define TEGRA210_PEQ_RAM_CTRL_RAM_ADDR_MASK		0x1ff

/* PEQ register definition ends here */
#define TEGRA210_PEQ_MAX_BIQUAD_STAGES			12

#define TEGRA210_PEQ_MAX_CHANNELS			8

#define TEGRA210_PEQ_BIQUAD_INIT_STAGE			5

#define TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH (2 + TEGRA210_PEQ_MAX_BIQUAD_STAGES * 5)
#define TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH (2 + TEGRA210_PEQ_MAX_BIQUAD_STAGES)

int tegra210_peq_regmap_init(struct platform_device *pdev);
int tegra210_peq_component_init(struct snd_soc_component *cmpnt);
void tegra210_peq_restore(struct regmap *regmap, u32 *biquad_gains,
			  u32 *biquad_shifts);
void tegra210_peq_save(struct regmap *regmap, u32 *biquad_gains,
		       u32 *biquad_shifts);

#endif
