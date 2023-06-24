// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
//
// cs35l45-tables.c -- CS35L45 ALSA SoC audio driver
//
// Copyright 2019-2022 Cirrus Logic, Inc.
//
// Author: James Schulman <james.schulman@cirrus.com>

#include <linux/module.h>
#include <linux/regmap.h>

#include "cs35l45.h"

static const struct reg_sequence cs35l45_patch[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00000044,			0x00000055 },
	{ 0x00000044,			0x000000AA },
	{ 0x00006480,			0x0830500A },
	{ 0x00007C60,			0x1000850B },
	{ CS35L45_BOOST_OV_CFG,		0x007000D0 },
	{ CS35L45_LDPM_CONFIG,		0x0001B636 },
	{ 0x00002C08,			0x00000009 },
	{ 0x00006850,			0x0A30FFC4 },
	{ 0x00003820,			0x00040100 },
	{ 0x00003824,			0x00000000 },
	{ 0x00007CFC,			0x62870004 },
	{ 0x00007C60,			0x1001850B },
	{ 0x00000040,			0x00000000 },
	{ 0x00000044,			0x00000000 },
	{ CS35L45_BOOST_CCM_CFG,	0xF0000003 },
	{ CS35L45_BOOST_DCM_CFG,	0x08710220 },
	{ CS35L45_ERROR_RELEASE,	0x00200000 },
};

int cs35l45_apply_patch(struct cs35l45_private *cs35l45)
{
	return regmap_register_patch(cs35l45->regmap, cs35l45_patch,
				     ARRAY_SIZE(cs35l45_patch));
}
EXPORT_SYMBOL_NS_GPL(cs35l45_apply_patch, SND_SOC_CS35L45_TABLES);

static const struct reg_default cs35l45_defaults[] = {
	{ CS35L45_BLOCK_ENABLES,		0x00003323 },
	{ CS35L45_BLOCK_ENABLES2,		0x00000010 },
	{ CS35L45_REFCLK_INPUT,			0x00000510 },
	{ CS35L45_GLOBAL_SAMPLE_RATE,		0x00000003 },
	{ CS35L45_ASP_ENABLES1,			0x00000000 },
	{ CS35L45_ASP_CONTROL1,			0x00000028 },
	{ CS35L45_ASP_CONTROL2,			0x18180200 },
	{ CS35L45_ASP_CONTROL3,			0x00000002 },
	{ CS35L45_ASP_FRAME_CONTROL1,		0x03020100 },
	{ CS35L45_ASP_FRAME_CONTROL2,		0x00000004 },
	{ CS35L45_ASP_FRAME_CONTROL5,		0x00000100 },
	{ CS35L45_ASP_DATA_CONTROL1,		0x00000018 },
	{ CS35L45_ASP_DATA_CONTROL5,		0x00000018 },
	{ CS35L45_DACPCM1_INPUT,		0x00000008 },
	{ CS35L45_ASPTX1_INPUT,			0x00000018 },
	{ CS35L45_ASPTX2_INPUT,			0x00000019 },
	{ CS35L45_ASPTX3_INPUT,			0x00000020 },
	{ CS35L45_ASPTX4_INPUT,			0x00000028 },
	{ CS35L45_ASPTX5_INPUT,			0x00000048 },
	{ CS35L45_AMP_PCM_CONTROL,		0x00100000 },
};

static bool cs35l45_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L45_DEVID ... CS35L45_OTPID:
	case CS35L45_SFT_RESET:
	case CS35L45_GLOBAL_ENABLES:
	case CS35L45_BLOCK_ENABLES:
	case CS35L45_BLOCK_ENABLES2:
	case CS35L45_ERROR_RELEASE:
	case CS35L45_REFCLK_INPUT:
	case CS35L45_GLOBAL_SAMPLE_RATE:
	case CS35L45_ASP_ENABLES1:
	case CS35L45_ASP_CONTROL1:
	case CS35L45_ASP_CONTROL2:
	case CS35L45_ASP_CONTROL3:
	case CS35L45_ASP_FRAME_CONTROL1:
	case CS35L45_ASP_FRAME_CONTROL2:
	case CS35L45_ASP_FRAME_CONTROL5:
	case CS35L45_ASP_DATA_CONTROL1:
	case CS35L45_ASP_DATA_CONTROL5:
	case CS35L45_DACPCM1_INPUT:
	case CS35L45_ASPTX1_INPUT:
	case CS35L45_ASPTX2_INPUT:
	case CS35L45_ASPTX3_INPUT:
	case CS35L45_ASPTX4_INPUT:
	case CS35L45_ASPTX5_INPUT:
	case CS35L45_AMP_PCM_CONTROL:
	case CS35L45_AMP_PCM_HPF_TST:
	case CS35L45_IRQ1_EINT_4:
		return true;
	default:
		return false;
	}
}

static bool cs35l45_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L45_DEVID ... CS35L45_OTPID:
	case CS35L45_SFT_RESET:
	case CS35L45_GLOBAL_ENABLES:
	case CS35L45_ERROR_RELEASE:
	case CS35L45_AMP_PCM_HPF_TST:	/* not cachable */
	case CS35L45_IRQ1_EINT_4:
		return true;
	default:
		return false;
	}
}

const struct regmap_config cs35l45_i2c_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L45_LASTREG,
	.reg_defaults = cs35l45_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l45_defaults),
	.volatile_reg = cs35l45_volatile_reg,
	.readable_reg = cs35l45_readable_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(cs35l45_i2c_regmap, SND_SOC_CS35L45_TABLES);

const struct regmap_config cs35l45_spi_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.pad_bits = 16,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L45_LASTREG,
	.reg_defaults = cs35l45_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l45_defaults),
	.volatile_reg = cs35l45_volatile_reg,
	.readable_reg = cs35l45_readable_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(cs35l45_spi_regmap, SND_SOC_CS35L45_TABLES);

static const struct {
	u8 cfg_id;
	u32 freq;
} cs35l45_pll_refclk_freq[] = {
	{ 0x0C,   128000 },
	{ 0x0F,   256000 },
	{ 0x11,   384000 },
	{ 0x12,   512000 },
	{ 0x15,   768000 },
	{ 0x17,  1024000 },
	{ 0x19,  1411200 },
	{ 0x1B,  1536000 },
	{ 0x1C,  2116800 },
	{ 0x1D,  2048000 },
	{ 0x1E,  2304000 },
	{ 0x1F,  2822400 },
	{ 0x21,  3072000 },
	{ 0x23,  4233600 },
	{ 0x24,  4096000 },
	{ 0x25,  4608000 },
	{ 0x26,  5644800 },
	{ 0x27,  6000000 },
	{ 0x28,  6144000 },
	{ 0x29,  6350400 },
	{ 0x2A,  6912000 },
	{ 0x2D,  7526400 },
	{ 0x2E,  8467200 },
	{ 0x2F,  8192000 },
	{ 0x30,  9216000 },
	{ 0x31, 11289600 },
	{ 0x33, 12288000 },
	{ 0x37, 16934400 },
	{ 0x38, 18432000 },
	{ 0x39, 22579200 },
	{ 0x3B, 24576000 },
};

unsigned int cs35l45_get_clk_freq_id(unsigned int freq)
{
	int i;

	if (freq == 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cs35l45_pll_refclk_freq); ++i) {
		if (cs35l45_pll_refclk_freq[i].freq == freq)
			return cs35l45_pll_refclk_freq[i].cfg_id;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(cs35l45_get_clk_freq_id, SND_SOC_CS35L45_TABLES);

MODULE_DESCRIPTION("ASoC CS35L45 driver tables");
MODULE_AUTHOR("James Schulman, Cirrus Logic Inc, <james.schulman@cirrus.com>");
MODULE_LICENSE("Dual BSD/GPL");
