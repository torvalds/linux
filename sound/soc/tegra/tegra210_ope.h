/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra210_ope.h - Definitions for Tegra210 OPE driver
 *
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 */

#ifndef __TEGRA210_OPE_H__
#define __TEGRA210_OPE_H__

#include <linux/regmap.h>
#include <sound/soc.h>

#include "tegra210_peq.h"

/*
 * OPE_RX registers are with respect to XBAR.
 * The data comes from XBAR to OPE
 */
#define TEGRA210_OPE_RX_STATUS			0xc
#define TEGRA210_OPE_RX_INT_STATUS		0x10
#define TEGRA210_OPE_RX_INT_MASK		0x14
#define TEGRA210_OPE_RX_INT_SET			0x18
#define TEGRA210_OPE_RX_INT_CLEAR		0x1c
#define TEGRA210_OPE_RX_CIF_CTRL		0x20

/*
 * OPE_TX registers are with respect to XBAR.
 * The data goes out from OPE to XBAR
 */
#define TEGRA210_OPE_TX_STATUS			0x4c
#define TEGRA210_OPE_TX_INT_STATUS		0x50
#define TEGRA210_OPE_TX_INT_MASK		0x54
#define TEGRA210_OPE_TX_INT_SET			0x58
#define TEGRA210_OPE_TX_INT_CLEAR		0x5c
#define TEGRA210_OPE_TX_CIF_CTRL		0x60

/* OPE Gloabal registers */
#define TEGRA210_OPE_ENABLE			0x80
#define TEGRA210_OPE_SOFT_RESET			0x84
#define TEGRA210_OPE_CG				0x88
#define TEGRA210_OPE_STATUS			0x8c
#define TEGRA210_OPE_INT_STATUS			0x90
#define TEGRA210_OPE_DIR			0x94

/* Fields for TEGRA210_OPE_ENABLE */
#define TEGRA210_OPE_EN_SHIFT			0
#define TEGRA210_OPE_EN				(1 << TEGRA210_OPE_EN_SHIFT)

/* Fields for TEGRA210_OPE_SOFT_RESET */
#define TEGRA210_OPE_SOFT_RESET_SHIFT		0
#define TEGRA210_OPE_SOFT_RESET_EN		(1 << TEGRA210_OPE_SOFT_RESET_SHIFT)

#define TEGRA210_OPE_DIR_SHIFT			0

struct tegra210_ope {
	struct regmap *regmap;
	struct regmap *peq_regmap;
	struct regmap *mbdrc_regmap;
	u32 peq_biquad_gains[TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH];
	u32 peq_biquad_shifts[TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH];
	unsigned int data_dir;
};

/* Extension of soc_bytes structure defined in sound/soc.h */
struct tegra_soc_bytes {
	struct soc_bytes soc;
	u32 shift; /* Used as offset for AHUB RAM related programing */
};

/* Utility structures for using mixer control of type snd_soc_bytes */
#define TEGRA_SOC_BYTES_EXT(xname, xbase, xregs, xshift, xmask,		\
			    xhandler_get, xhandler_put, xinfo)		\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= xname,						\
	.info	= xinfo,						\
	.get	= xhandler_get,						\
	.put	= xhandler_put,						\
	.private_value = ((unsigned long)&(struct tegra_soc_bytes)	\
	{								\
		.soc.base	= xbase,				\
		.soc.num_regs	= xregs,				\
		.soc.mask	= xmask,				\
		.shift		= xshift				\
	})								\
}

#endif
