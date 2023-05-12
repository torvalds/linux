// SPDX-License-Identifier: GPL-2.0-only
//
// Components shared between ASoC and HDA CS35L56 drivers
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include "cs35l56.h"

static const struct reg_default cs35l56_reg_defaults[] = {
	{ CS35L56_ASP1_ENABLES1,		0x00000000 },
	{ CS35L56_ASP1_CONTROL1,		0x00000028 },
	{ CS35L56_ASP1_CONTROL2,		0x18180200 },
	{ CS35L56_ASP1_CONTROL3,		0x00000002 },
	{ CS35L56_ASP1_FRAME_CONTROL1,		0x03020100 },
	{ CS35L56_ASP1_FRAME_CONTROL5,		0x00020100 },
	{ CS35L56_ASP1_DATA_CONTROL1,		0x00000018 },
	{ CS35L56_ASP1_DATA_CONTROL5,		0x00000018 },
	{ CS35L56_ASP1TX1_INPUT,		0x00000018 },
	{ CS35L56_ASP1TX2_INPUT,		0x00000019 },
	{ CS35L56_ASP1TX3_INPUT,		0x00000020 },
	{ CS35L56_ASP1TX4_INPUT,		0x00000028 },
	{ CS35L56_SWIRE_DP3_CH1_INPUT,		0x00000018 },
	{ CS35L56_SWIRE_DP3_CH2_INPUT,		0x00000019 },
	{ CS35L56_SWIRE_DP3_CH3_INPUT,		0x00000029 },
	{ CS35L56_SWIRE_DP3_CH4_INPUT,		0x00000028 },
	{ CS35L56_IRQ1_CFG,			0x00000000 },
	{ CS35L56_IRQ1_MASK_1,			0x83ffffff },
	{ CS35L56_IRQ1_MASK_2,			0xffff7fff },
	{ CS35L56_IRQ1_MASK_4,			0xe0ffffff },
	{ CS35L56_IRQ1_MASK_8,			0xfc000fff },
	{ CS35L56_IRQ1_MASK_18,			0x1f7df0ff },
	{ CS35L56_IRQ1_MASK_20,			0x15c00000 },
	/* CS35L56_MAIN_RENDER_USER_MUTE - soft register, no default	*/
	/* CS35L56_MAIN_RENDER_USER_VOLUME - soft register, no default	*/
	/* CS35L56_MAIN_POSTURE_NUMBER - soft register, no default	*/
};

static bool cs35l56_is_dsp_memory(unsigned int reg)
{
	switch (reg) {
	case CS35L56_DSP1_XMEM_PACKED_0 ... CS35L56_DSP1_XMEM_PACKED_6143:
	case CS35L56_DSP1_XMEM_UNPACKED32_0 ... CS35L56_DSP1_XMEM_UNPACKED32_4095:
	case CS35L56_DSP1_XMEM_UNPACKED24_0 ... CS35L56_DSP1_XMEM_UNPACKED24_8191:
	case CS35L56_DSP1_YMEM_PACKED_0 ... CS35L56_DSP1_YMEM_PACKED_4604:
	case CS35L56_DSP1_YMEM_UNPACKED32_0 ... CS35L56_DSP1_YMEM_UNPACKED32_3070:
	case CS35L56_DSP1_YMEM_UNPACKED24_0 ... CS35L56_DSP1_YMEM_UNPACKED24_6141:
	case CS35L56_DSP1_PMEM_0 ... CS35L56_DSP1_PMEM_5114:
		return true;
	default:
		return false;
	}
}

static bool cs35l56_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_DEVID:
	case CS35L56_REVID:
	case CS35L56_RELID:
	case CS35L56_OTPID:
	case CS35L56_SFT_RESET:
	case CS35L56_GLOBAL_ENABLES:
	case CS35L56_BLOCK_ENABLES:
	case CS35L56_BLOCK_ENABLES2:
	case CS35L56_REFCLK_INPUT:
	case CS35L56_GLOBAL_SAMPLE_RATE:
	case CS35L56_ASP1_ENABLES1:
	case CS35L56_ASP1_CONTROL1:
	case CS35L56_ASP1_CONTROL2:
	case CS35L56_ASP1_CONTROL3:
	case CS35L56_ASP1_FRAME_CONTROL1:
	case CS35L56_ASP1_FRAME_CONTROL5:
	case CS35L56_ASP1_DATA_CONTROL1:
	case CS35L56_ASP1_DATA_CONTROL5:
	case CS35L56_DACPCM1_INPUT:
	case CS35L56_DACPCM2_INPUT:
	case CS35L56_ASP1TX1_INPUT:
	case CS35L56_ASP1TX2_INPUT:
	case CS35L56_ASP1TX3_INPUT:
	case CS35L56_ASP1TX4_INPUT:
	case CS35L56_DSP1RX1_INPUT:
	case CS35L56_DSP1RX2_INPUT:
	case CS35L56_SWIRE_DP3_CH1_INPUT:
	case CS35L56_SWIRE_DP3_CH2_INPUT:
	case CS35L56_SWIRE_DP3_CH3_INPUT:
	case CS35L56_SWIRE_DP3_CH4_INPUT:
	case CS35L56_IRQ1_CFG:
	case CS35L56_IRQ1_STATUS:
	case CS35L56_IRQ1_EINT_1 ... CS35L56_IRQ1_EINT_8:
	case CS35L56_IRQ1_EINT_18:
	case CS35L56_IRQ1_EINT_20:
	case CS35L56_IRQ1_MASK_1:
	case CS35L56_IRQ1_MASK_2:
	case CS35L56_IRQ1_MASK_4:
	case CS35L56_IRQ1_MASK_8:
	case CS35L56_IRQ1_MASK_18:
	case CS35L56_IRQ1_MASK_20:
	case CS35L56_DSP_VIRTUAL1_MBOX_1:
	case CS35L56_DSP_VIRTUAL1_MBOX_2:
	case CS35L56_DSP_VIRTUAL1_MBOX_3:
	case CS35L56_DSP_VIRTUAL1_MBOX_4:
	case CS35L56_DSP_VIRTUAL1_MBOX_5:
	case CS35L56_DSP_VIRTUAL1_MBOX_6:
	case CS35L56_DSP_VIRTUAL1_MBOX_7:
	case CS35L56_DSP_VIRTUAL1_MBOX_8:
	case CS35L56_DSP_RESTRICT_STS1:
	case CS35L56_DSP1_SYS_INFO_ID ... CS35L56_DSP1_SYS_INFO_END:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_0:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_1:
	case CS35L56_DSP1_SCRATCH1:
	case CS35L56_DSP1_SCRATCH2:
	case CS35L56_DSP1_SCRATCH3:
	case CS35L56_DSP1_SCRATCH4:
		return true;
	default:
		return cs35l56_is_dsp_memory(reg);
	}
}

static bool cs35l56_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_DSP1_XMEM_PACKED_0 ... CS35L56_DSP1_XMEM_PACKED_6143:
	case CS35L56_DSP1_YMEM_PACKED_0 ... CS35L56_DSP1_YMEM_PACKED_4604:
	case CS35L56_DSP1_PMEM_0 ... CS35L56_DSP1_PMEM_5114:
		return true;
	default:
		return false;
	}
}

static bool cs35l56_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_DEVID:
	case CS35L56_REVID:
	case CS35L56_RELID:
	case CS35L56_OTPID:
	case CS35L56_SFT_RESET:
	case CS35L56_GLOBAL_ENABLES:		   /* owned by firmware */
	case CS35L56_BLOCK_ENABLES:		   /* owned by firmware */
	case CS35L56_BLOCK_ENABLES2:		   /* owned by firmware */
	case CS35L56_REFCLK_INPUT:		   /* owned by firmware */
	case CS35L56_GLOBAL_SAMPLE_RATE:	   /* owned by firmware */
	case CS35L56_DACPCM1_INPUT:		   /* owned by firmware */
	case CS35L56_DACPCM2_INPUT:		   /* owned by firmware */
	case CS35L56_DSP1RX1_INPUT:		   /* owned by firmware */
	case CS35L56_DSP1RX2_INPUT:		   /* owned by firmware */
	case CS35L56_IRQ1_STATUS:
	case CS35L56_IRQ1_EINT_1 ... CS35L56_IRQ1_EINT_8:
	case CS35L56_IRQ1_EINT_18:
	case CS35L56_IRQ1_EINT_20:
	case CS35L56_DSP_VIRTUAL1_MBOX_1:
	case CS35L56_DSP_VIRTUAL1_MBOX_2:
	case CS35L56_DSP_VIRTUAL1_MBOX_3:
	case CS35L56_DSP_VIRTUAL1_MBOX_4:
	case CS35L56_DSP_VIRTUAL1_MBOX_5:
	case CS35L56_DSP_VIRTUAL1_MBOX_6:
	case CS35L56_DSP_VIRTUAL1_MBOX_7:
	case CS35L56_DSP_VIRTUAL1_MBOX_8:
	case CS35L56_DSP_RESTRICT_STS1:
	case CS35L56_DSP1_SYS_INFO_ID ... CS35L56_DSP1_SYS_INFO_END:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_0:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_1:
	case CS35L56_DSP1_SCRATCH1:
	case CS35L56_DSP1_SCRATCH2:
	case CS35L56_DSP1_SCRATCH3:
	case CS35L56_DSP1_SCRATCH4:
		return true;
	case CS35L56_MAIN_RENDER_USER_MUTE:
	case CS35L56_MAIN_RENDER_USER_VOLUME:
	case CS35L56_MAIN_POSTURE_NUMBER:
		return false;
	default:
		return cs35l56_is_dsp_memory(reg);
	}
}

static const u32 cs35l56_firmware_registers[] = {
	CS35L56_MAIN_RENDER_USER_MUTE,
	CS35L56_MAIN_RENDER_USER_VOLUME,
	CS35L56_MAIN_POSTURE_NUMBER,
};

void cs35l56_reread_firmware_registers(struct device *dev, struct regmap *regmap)
{
	int i;
	unsigned int val;

	for (i = 0; i < ARRAY_SIZE(cs35l56_firmware_registers); i++) {
		regmap_read(regmap, cs35l56_firmware_registers[i], &val);
		dev_dbg(dev, "%s: %d: %#x: %#x\n", __func__,
			i, cs35l56_firmware_registers[i], val);
	}
}
EXPORT_SYMBOL_NS_GPL(cs35l56_reread_firmware_registers, SND_SOC_CS35L56_SHARED);

const struct cs_dsp_region cs35l56_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L56_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L56_DSP1_XMEM_PACKED_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L56_DSP1_YMEM_PACKED_0 },
	{ .type = WMFW_ADSP2_XM,	.base = CS35L56_DSP1_XMEM_UNPACKED24_0 },
	{ .type = WMFW_ADSP2_YM,	.base = CS35L56_DSP1_YMEM_UNPACKED24_0 },
};
EXPORT_SYMBOL_NS_GPL(cs35l56_dsp1_regions, SND_SOC_CS35L56_SHARED);

static const u32 cs35l56_bclk_valid_for_pll_freq_table[] = {
	[0x0C] = 128000,
	[0x0F] = 256000,
	[0x11] = 384000,
	[0x12] = 512000,
	[0x15] = 768000,
	[0x17] = 1024000,
	[0x1A] = 1500000,
	[0x1B] = 1536000,
	[0x1C] = 2000000,
	[0x1D] = 2048000,
	[0x1E] = 2400000,
	[0x20] = 3000000,
	[0x21] = 3072000,
	[0x23] = 4000000,
	[0x24] = 4096000,
	[0x25] = 4800000,
	[0x27] = 6000000,
	[0x28] = 6144000,
	[0x29] = 6250000,
	[0x2A] = 6400000,
	[0x2E] = 8000000,
	[0x2F] = 8192000,
	[0x30] = 9600000,
	[0x32] = 12000000,
	[0x33] = 12288000,
	[0x37] = 13500000,
	[0x38] = 19200000,
	[0x39] = 22579200,
	[0x3B] = 24576000,
};

int cs35l56_get_bclk_freq_id(unsigned int freq)
{
	int i;

	if (freq == 0)
		return -EINVAL;

	/* The BCLK frequency must be a valid PLL REFCLK */
	for (i = 0; i < ARRAY_SIZE(cs35l56_bclk_valid_for_pll_freq_table); ++i) {
		if (cs35l56_bclk_valid_for_pll_freq_table[i] == freq)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_get_bclk_freq_id, SND_SOC_CS35L56_SHARED);

static const char * const cs35l56_supplies[/* auto-sized */] = {
	"VDD_P",
	"VDD_IO",
	"VDD_A",
};

void cs35l56_fill_supply_names(struct regulator_bulk_data *data)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cs35l56_supplies) != CS35L56_NUM_BULK_SUPPLIES);
	for (i = 0; i < ARRAY_SIZE(cs35l56_supplies); i++)
		data[i].supply = cs35l56_supplies[i];
}
EXPORT_SYMBOL_NS_GPL(cs35l56_fill_supply_names, SND_SOC_CS35L56_SHARED);

const char * const cs35l56_tx_input_texts[] = {
	"None", "ASP1RX1", "ASP1RX2", "VMON", "IMON", "ERRVOL", "CLASSH",
	"VDDBMON", "VBSTMON", "DSP1TX1", "DSP1TX2", "DSP1TX3", "DSP1TX4",
	"DSP1TX5", "DSP1TX6", "DSP1TX7", "DSP1TX8", "TEMPMON",
	"INTERPOLATOR", "SDW1RX1", "SDW1RX2",
};
EXPORT_SYMBOL_NS_GPL(cs35l56_tx_input_texts, SND_SOC_CS35L56_SHARED);

const unsigned int cs35l56_tx_input_values[] = {
	CS35L56_INPUT_SRC_NONE,
	CS35L56_INPUT_SRC_ASP1RX1,
	CS35L56_INPUT_SRC_ASP1RX2,
	CS35L56_INPUT_SRC_VMON,
	CS35L56_INPUT_SRC_IMON,
	CS35L56_INPUT_SRC_ERR_VOL,
	CS35L56_INPUT_SRC_CLASSH,
	CS35L56_INPUT_SRC_VDDBMON,
	CS35L56_INPUT_SRC_VBSTMON,
	CS35L56_INPUT_SRC_DSP1TX1,
	CS35L56_INPUT_SRC_DSP1TX2,
	CS35L56_INPUT_SRC_DSP1TX3,
	CS35L56_INPUT_SRC_DSP1TX4,
	CS35L56_INPUT_SRC_DSP1TX5,
	CS35L56_INPUT_SRC_DSP1TX6,
	CS35L56_INPUT_SRC_DSP1TX7,
	CS35L56_INPUT_SRC_DSP1TX8,
	CS35L56_INPUT_SRC_TEMPMON,
	CS35L56_INPUT_SRC_INTERPOLATOR,
	CS35L56_INPUT_SRC_SWIRE_DP1_CHANNEL1,
	CS35L56_INPUT_SRC_SWIRE_DP1_CHANNEL2,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_tx_input_values, SND_SOC_CS35L56_SHARED);

struct regmap_config cs35l56_regmap_i2c = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_i2c, SND_SOC_CS35L56_SHARED);

struct regmap_config cs35l56_regmap_spi = {
	.reg_bits = 32,
	.val_bits = 32,
	.pad_bits = 16,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_spi, SND_SOC_CS35L56_SHARED);

struct regmap_config cs35l56_regmap_sdw = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_sdw, SND_SOC_CS35L56_SHARED);

MODULE_DESCRIPTION("ASoC CS35L56 Shared");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
