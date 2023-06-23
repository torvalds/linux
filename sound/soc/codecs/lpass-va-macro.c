// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "lpass-macro-common.h"

/* VA macro registers */
#define CDC_VA_CLK_RST_CTRL_MCLK_CONTROL	(0x0000)
#define CDC_VA_MCLK_CONTROL_EN			BIT(0)
#define CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL	(0x0004)
#define CDC_VA_FS_CONTROL_EN			BIT(0)
#define CDC_VA_FS_COUNTER_CLR			BIT(1)
#define CDC_VA_CLK_RST_CTRL_SWR_CONTROL		(0x0008)
#define CDC_VA_SWR_RESET_MASK		BIT(1)
#define CDC_VA_SWR_RESET_ENABLE		BIT(1)
#define CDC_VA_SWR_CLK_EN_MASK		BIT(0)
#define CDC_VA_SWR_CLK_ENABLE		BIT(0)
#define CDC_VA_TOP_CSR_TOP_CFG0			(0x0080)
#define CDC_VA_FS_BROADCAST_EN			BIT(1)
#define CDC_VA_TOP_CSR_DMIC0_CTL		(0x0084)
#define CDC_VA_TOP_CSR_DMIC1_CTL		(0x0088)
#define CDC_VA_TOP_CSR_DMIC2_CTL		(0x008C)
#define CDC_VA_TOP_CSR_DMIC3_CTL		(0x0090)
#define CDC_VA_DMIC_EN_MASK			BIT(0)
#define CDC_VA_DMIC_ENABLE			BIT(0)
#define CDC_VA_DMIC_CLK_SEL_MASK		GENMASK(3, 1)
#define CDC_VA_DMIC_CLK_SEL_SHFT		1
#define CDC_VA_DMIC_CLK_SEL_DIV0		0x0
#define CDC_VA_DMIC_CLK_SEL_DIV1		0x2
#define CDC_VA_DMIC_CLK_SEL_DIV2		0x4
#define CDC_VA_DMIC_CLK_SEL_DIV3		0x6
#define CDC_VA_DMIC_CLK_SEL_DIV4		0x8
#define CDC_VA_DMIC_CLK_SEL_DIV5		0xa
#define CDC_VA_TOP_CSR_DMIC_CFG			(0x0094)
#define CDC_VA_RESET_ALL_DMICS_MASK		BIT(7)
#define CDC_VA_RESET_ALL_DMICS_RESET		BIT(7)
#define CDC_VA_RESET_ALL_DMICS_DISABLE		0
#define CDC_VA_DMIC3_FREQ_CHANGE_MASK		BIT(3)
#define CDC_VA_DMIC3_FREQ_CHANGE_EN		BIT(3)
#define CDC_VA_DMIC2_FREQ_CHANGE_MASK		BIT(2)
#define CDC_VA_DMIC2_FREQ_CHANGE_EN		BIT(2)
#define CDC_VA_DMIC1_FREQ_CHANGE_MASK		BIT(1)
#define CDC_VA_DMIC1_FREQ_CHANGE_EN		BIT(1)
#define CDC_VA_DMIC0_FREQ_CHANGE_MASK		BIT(0)
#define CDC_VA_DMIC0_FREQ_CHANGE_EN		BIT(0)
#define CDC_VA_DMIC_FREQ_CHANGE_DISABLE		0
#define CDC_VA_TOP_CSR_DEBUG_BUS		(0x009C)
#define CDC_VA_TOP_CSR_DEBUG_EN			(0x00A0)
#define CDC_VA_TOP_CSR_TX_I2S_CTL		(0x00A4)
#define CDC_VA_TOP_CSR_I2S_CLK			(0x00A8)
#define CDC_VA_TOP_CSR_I2S_RESET		(0x00AC)
#define CDC_VA_TOP_CSR_CORE_ID_0		(0x00C0)
#define CDC_VA_TOP_CSR_CORE_ID_1		(0x00C4)
#define CDC_VA_TOP_CSR_CORE_ID_2		(0x00C8)
#define CDC_VA_TOP_CSR_CORE_ID_3		(0x00CC)
#define CDC_VA_TOP_CSR_SWR_MIC_CTL0		(0x00D0)
#define CDC_VA_TOP_CSR_SWR_MIC_CTL1		(0x00D4)
#define CDC_VA_TOP_CSR_SWR_MIC_CTL2		(0x00D8)
#define CDC_VA_SWR_MIC_CLK_SEL_0_1_MASK		(0xEE)
#define CDC_VA_SWR_MIC_CLK_SEL_0_1_DIV1		(0xCC)
#define CDC_VA_TOP_CSR_SWR_CTRL			(0x00DC)
#define CDC_VA_INP_MUX_ADC_MUX0_CFG0		(0x0100)
#define CDC_VA_INP_MUX_ADC_MUX0_CFG1		(0x0104)
#define CDC_VA_INP_MUX_ADC_MUX1_CFG0		(0x0108)
#define CDC_VA_INP_MUX_ADC_MUX1_CFG1		(0x010C)
#define CDC_VA_INP_MUX_ADC_MUX2_CFG0		(0x0110)
#define CDC_VA_INP_MUX_ADC_MUX2_CFG1		(0x0114)
#define CDC_VA_INP_MUX_ADC_MUX3_CFG0		(0x0118)
#define CDC_VA_INP_MUX_ADC_MUX3_CFG1		(0x011C)
#define CDC_VA_TX0_TX_PATH_CTL			(0x0400)
#define CDC_VA_TX_PATH_CLK_EN_MASK		BIT(5)
#define CDC_VA_TX_PATH_CLK_EN			BIT(5)
#define CDC_VA_TX_PATH_CLK_DISABLE		0
#define CDC_VA_TX_PATH_PGA_MUTE_EN_MASK		BIT(4)
#define CDC_VA_TX_PATH_PGA_MUTE_EN		BIT(4)
#define CDC_VA_TX_PATH_PGA_MUTE_DISABLE		0
#define CDC_VA_TX0_TX_PATH_CFG0			(0x0404)
#define CDC_VA_ADC_MODE_MASK			GENMASK(2, 1)
#define CDC_VA_ADC_MODE_SHIFT			1
#define  TX_HPF_CUT_OFF_FREQ_MASK		GENMASK(6, 5)
#define  CF_MIN_3DB_4HZ			0x0
#define  CF_MIN_3DB_75HZ		0x1
#define  CF_MIN_3DB_150HZ		0x2
#define CDC_VA_TX0_TX_PATH_CFG1			(0x0408)
#define CDC_VA_TX0_TX_VOL_CTL			(0x040C)
#define CDC_VA_TX0_TX_PATH_SEC0			(0x0410)
#define CDC_VA_TX0_TX_PATH_SEC1			(0x0414)
#define CDC_VA_TX0_TX_PATH_SEC2			(0x0418)
#define CDC_VA_TX_HPF_CUTOFF_FREQ_CHANGE_MASK	BIT(1)
#define CDC_VA_TX_HPF_CUTOFF_FREQ_CHANGE_REQ	BIT(1)
#define CDC_VA_TX_HPF_ZERO_GATE_MASK		BIT(0)
#define CDC_VA_TX_HPF_ZERO_NO_GATE		BIT(0)
#define CDC_VA_TX_HPF_ZERO_GATE			0
#define CDC_VA_TX0_TX_PATH_SEC3			(0x041C)
#define CDC_VA_TX0_TX_PATH_SEC4			(0x0420)
#define CDC_VA_TX0_TX_PATH_SEC5			(0x0424)
#define CDC_VA_TX0_TX_PATH_SEC6			(0x0428)
#define CDC_VA_TX0_TX_PATH_SEC7			(0x042C)
#define CDC_VA_TX1_TX_PATH_CTL			(0x0480)
#define CDC_VA_TX1_TX_PATH_CFG0			(0x0484)
#define CDC_VA_TX1_TX_PATH_CFG1			(0x0488)
#define CDC_VA_TX1_TX_VOL_CTL			(0x048C)
#define CDC_VA_TX1_TX_PATH_SEC0			(0x0490)
#define CDC_VA_TX1_TX_PATH_SEC1			(0x0494)
#define CDC_VA_TX1_TX_PATH_SEC2			(0x0498)
#define CDC_VA_TX1_TX_PATH_SEC3			(0x049C)
#define CDC_VA_TX1_TX_PATH_SEC4			(0x04A0)
#define CDC_VA_TX1_TX_PATH_SEC5			(0x04A4)
#define CDC_VA_TX1_TX_PATH_SEC6			(0x04A8)
#define CDC_VA_TX2_TX_PATH_CTL			(0x0500)
#define CDC_VA_TX2_TX_PATH_CFG0			(0x0504)
#define CDC_VA_TX2_TX_PATH_CFG1			(0x0508)
#define CDC_VA_TX2_TX_VOL_CTL			(0x050C)
#define CDC_VA_TX2_TX_PATH_SEC0			(0x0510)
#define CDC_VA_TX2_TX_PATH_SEC1			(0x0514)
#define CDC_VA_TX2_TX_PATH_SEC2			(0x0518)
#define CDC_VA_TX2_TX_PATH_SEC3			(0x051C)
#define CDC_VA_TX2_TX_PATH_SEC4			(0x0520)
#define CDC_VA_TX2_TX_PATH_SEC5			(0x0524)
#define CDC_VA_TX2_TX_PATH_SEC6			(0x0528)
#define CDC_VA_TX3_TX_PATH_CTL			(0x0580)
#define CDC_VA_TX3_TX_PATH_CFG0			(0x0584)
#define CDC_VA_TX_PATH_ADC_DMIC_SEL_MASK	BIT(7)
#define CDC_VA_TX_PATH_ADC_DMIC_SEL_DMIC	BIT(7)
#define CDC_VA_TX_PATH_ADC_DMIC_SEL_ADC		0
#define CDC_VA_TX3_TX_PATH_CFG1			(0x0588)
#define CDC_VA_TX3_TX_VOL_CTL			(0x058C)
#define CDC_VA_TX3_TX_PATH_SEC0			(0x0590)
#define CDC_VA_TX3_TX_PATH_SEC1			(0x0594)
#define CDC_VA_TX3_TX_PATH_SEC2			(0x0598)
#define CDC_VA_TX3_TX_PATH_SEC3			(0x059C)
#define CDC_VA_TX3_TX_PATH_SEC4			(0x05A0)
#define CDC_VA_TX3_TX_PATH_SEC5			(0x05A4)
#define CDC_VA_TX3_TX_PATH_SEC6			(0x05A8)

#define VA_MAX_OFFSET				(0x07A8)

#define VA_MACRO_NUM_DECIMATORS 4
#define VA_MACRO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define VA_MACRO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE)

#define VA_MACRO_MCLK_FREQ 9600000
#define VA_MACRO_TX_PATH_OFFSET 0x80
#define VA_MACRO_SWR_MIC_MUX_SEL_MASK 0xF
#define VA_MACRO_ADC_MUX_CFG_OFFSET 0x8

static const DECLARE_TLV_DB_SCALE(digital_gain, -8400, 100, -8400);

enum {
	VA_MACRO_AIF_INVALID = 0,
	VA_MACRO_AIF1_CAP,
	VA_MACRO_AIF2_CAP,
	VA_MACRO_AIF3_CAP,
	VA_MACRO_MAX_DAIS,
};

enum {
	VA_MACRO_DEC0,
	VA_MACRO_DEC1,
	VA_MACRO_DEC2,
	VA_MACRO_DEC3,
	VA_MACRO_DEC4,
	VA_MACRO_DEC5,
	VA_MACRO_DEC6,
	VA_MACRO_DEC7,
	VA_MACRO_DEC_MAX,
};

enum {
	VA_MACRO_CLK_DIV_2,
	VA_MACRO_CLK_DIV_3,
	VA_MACRO_CLK_DIV_4,
	VA_MACRO_CLK_DIV_6,
	VA_MACRO_CLK_DIV_8,
	VA_MACRO_CLK_DIV_16,
};

#define VA_NUM_CLKS_MAX		3

struct va_macro {
	struct device *dev;
	unsigned long active_ch_mask[VA_MACRO_MAX_DAIS];
	unsigned long active_ch_cnt[VA_MACRO_MAX_DAIS];
	u16 dmic_clk_div;
	bool has_swr_master;

	int dec_mode[VA_MACRO_NUM_DECIMATORS];
	struct regmap *regmap;
	struct clk *mclk;
	struct clk *macro;
	struct clk *dcodec;
	struct clk *fsgen;
	struct clk_hw hw;
	struct lpass_macro *pds;

	s32 dmic_0_1_clk_cnt;
	s32 dmic_2_3_clk_cnt;
	s32 dmic_4_5_clk_cnt;
	s32 dmic_6_7_clk_cnt;
	u8 dmic_0_1_clk_div;
	u8 dmic_2_3_clk_div;
	u8 dmic_4_5_clk_div;
	u8 dmic_6_7_clk_div;
};

#define to_va_macro(_hw) container_of(_hw, struct va_macro, hw)

struct va_macro_data {
	bool has_swr_master;
};

static const struct va_macro_data sm8250_va_data = {
	.has_swr_master = false,
};

static const struct va_macro_data sm8450_va_data = {
	.has_swr_master = true,
};

static bool va_is_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_VA_TOP_CSR_CORE_ID_0:
	case CDC_VA_TOP_CSR_CORE_ID_1:
	case CDC_VA_TOP_CSR_CORE_ID_2:
	case CDC_VA_TOP_CSR_CORE_ID_3:
	case CDC_VA_TOP_CSR_DMIC0_CTL:
	case CDC_VA_TOP_CSR_DMIC1_CTL:
	case CDC_VA_TOP_CSR_DMIC2_CTL:
	case CDC_VA_TOP_CSR_DMIC3_CTL:
		return true;
	}
	return false;
}

static const struct reg_default va_defaults[] = {
	/* VA macro */
	{ CDC_VA_CLK_RST_CTRL_MCLK_CONTROL, 0x00},
	{ CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL, 0x00},
	{ CDC_VA_CLK_RST_CTRL_SWR_CONTROL, 0x00},
	{ CDC_VA_TOP_CSR_TOP_CFG0, 0x00},
	{ CDC_VA_TOP_CSR_DMIC0_CTL, 0x00},
	{ CDC_VA_TOP_CSR_DMIC1_CTL, 0x00},
	{ CDC_VA_TOP_CSR_DMIC2_CTL, 0x00},
	{ CDC_VA_TOP_CSR_DMIC3_CTL, 0x00},
	{ CDC_VA_TOP_CSR_DMIC_CFG, 0x80},
	{ CDC_VA_TOP_CSR_DEBUG_BUS, 0x00},
	{ CDC_VA_TOP_CSR_DEBUG_EN, 0x00},
	{ CDC_VA_TOP_CSR_TX_I2S_CTL, 0x0C},
	{ CDC_VA_TOP_CSR_I2S_CLK, 0x00},
	{ CDC_VA_TOP_CSR_I2S_RESET, 0x00},
	{ CDC_VA_TOP_CSR_CORE_ID_0, 0x00},
	{ CDC_VA_TOP_CSR_CORE_ID_1, 0x00},
	{ CDC_VA_TOP_CSR_CORE_ID_2, 0x00},
	{ CDC_VA_TOP_CSR_CORE_ID_3, 0x00},
	{ CDC_VA_TOP_CSR_SWR_MIC_CTL0, 0xEE},
	{ CDC_VA_TOP_CSR_SWR_MIC_CTL1, 0xEE},
	{ CDC_VA_TOP_CSR_SWR_MIC_CTL2, 0xEE},
	{ CDC_VA_TOP_CSR_SWR_CTRL, 0x06},

	/* VA core */
	{ CDC_VA_INP_MUX_ADC_MUX0_CFG0, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX0_CFG1, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX1_CFG0, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX1_CFG1, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX2_CFG0, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX2_CFG1, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX3_CFG0, 0x00},
	{ CDC_VA_INP_MUX_ADC_MUX3_CFG1, 0x00},
	{ CDC_VA_TX0_TX_PATH_CTL, 0x04},
	{ CDC_VA_TX0_TX_PATH_CFG0, 0x10},
	{ CDC_VA_TX0_TX_PATH_CFG1, 0x0B},
	{ CDC_VA_TX0_TX_VOL_CTL, 0x00},
	{ CDC_VA_TX0_TX_PATH_SEC0, 0x00},
	{ CDC_VA_TX0_TX_PATH_SEC1, 0x00},
	{ CDC_VA_TX0_TX_PATH_SEC2, 0x01},
	{ CDC_VA_TX0_TX_PATH_SEC3, 0x3C},
	{ CDC_VA_TX0_TX_PATH_SEC4, 0x20},
	{ CDC_VA_TX0_TX_PATH_SEC5, 0x00},
	{ CDC_VA_TX0_TX_PATH_SEC6, 0x00},
	{ CDC_VA_TX0_TX_PATH_SEC7, 0x25},
	{ CDC_VA_TX1_TX_PATH_CTL, 0x04},
	{ CDC_VA_TX1_TX_PATH_CFG0, 0x10},
	{ CDC_VA_TX1_TX_PATH_CFG1, 0x0B},
	{ CDC_VA_TX1_TX_VOL_CTL, 0x00},
	{ CDC_VA_TX1_TX_PATH_SEC0, 0x00},
	{ CDC_VA_TX1_TX_PATH_SEC1, 0x00},
	{ CDC_VA_TX1_TX_PATH_SEC2, 0x01},
	{ CDC_VA_TX1_TX_PATH_SEC3, 0x3C},
	{ CDC_VA_TX1_TX_PATH_SEC4, 0x20},
	{ CDC_VA_TX1_TX_PATH_SEC5, 0x00},
	{ CDC_VA_TX1_TX_PATH_SEC6, 0x00},
	{ CDC_VA_TX2_TX_PATH_CTL, 0x04},
	{ CDC_VA_TX2_TX_PATH_CFG0, 0x10},
	{ CDC_VA_TX2_TX_PATH_CFG1, 0x0B},
	{ CDC_VA_TX2_TX_VOL_CTL, 0x00},
	{ CDC_VA_TX2_TX_PATH_SEC0, 0x00},
	{ CDC_VA_TX2_TX_PATH_SEC1, 0x00},
	{ CDC_VA_TX2_TX_PATH_SEC2, 0x01},
	{ CDC_VA_TX2_TX_PATH_SEC3, 0x3C},
	{ CDC_VA_TX2_TX_PATH_SEC4, 0x20},
	{ CDC_VA_TX2_TX_PATH_SEC5, 0x00},
	{ CDC_VA_TX2_TX_PATH_SEC6, 0x00},
	{ CDC_VA_TX3_TX_PATH_CTL, 0x04},
	{ CDC_VA_TX3_TX_PATH_CFG0, 0x10},
	{ CDC_VA_TX3_TX_PATH_CFG1, 0x0B},
	{ CDC_VA_TX3_TX_VOL_CTL, 0x00},
	{ CDC_VA_TX3_TX_PATH_SEC0, 0x00},
	{ CDC_VA_TX3_TX_PATH_SEC1, 0x00},
	{ CDC_VA_TX3_TX_PATH_SEC2, 0x01},
	{ CDC_VA_TX3_TX_PATH_SEC3, 0x3C},
	{ CDC_VA_TX3_TX_PATH_SEC4, 0x20},
	{ CDC_VA_TX3_TX_PATH_SEC5, 0x00},
	{ CDC_VA_TX3_TX_PATH_SEC6, 0x00},
};

static bool va_is_rw_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_VA_CLK_RST_CTRL_MCLK_CONTROL:
	case CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL:
	case CDC_VA_CLK_RST_CTRL_SWR_CONTROL:
	case CDC_VA_TOP_CSR_TOP_CFG0:
	case CDC_VA_TOP_CSR_DMIC0_CTL:
	case CDC_VA_TOP_CSR_DMIC1_CTL:
	case CDC_VA_TOP_CSR_DMIC2_CTL:
	case CDC_VA_TOP_CSR_DMIC3_CTL:
	case CDC_VA_TOP_CSR_DMIC_CFG:
	case CDC_VA_TOP_CSR_SWR_MIC_CTL0:
	case CDC_VA_TOP_CSR_SWR_MIC_CTL1:
	case CDC_VA_TOP_CSR_SWR_MIC_CTL2:
	case CDC_VA_TOP_CSR_DEBUG_BUS:
	case CDC_VA_TOP_CSR_DEBUG_EN:
	case CDC_VA_TOP_CSR_TX_I2S_CTL:
	case CDC_VA_TOP_CSR_I2S_CLK:
	case CDC_VA_TOP_CSR_I2S_RESET:
	case CDC_VA_INP_MUX_ADC_MUX0_CFG0:
	case CDC_VA_INP_MUX_ADC_MUX0_CFG1:
	case CDC_VA_INP_MUX_ADC_MUX1_CFG0:
	case CDC_VA_INP_MUX_ADC_MUX1_CFG1:
	case CDC_VA_INP_MUX_ADC_MUX2_CFG0:
	case CDC_VA_INP_MUX_ADC_MUX2_CFG1:
	case CDC_VA_INP_MUX_ADC_MUX3_CFG0:
	case CDC_VA_INP_MUX_ADC_MUX3_CFG1:
	case CDC_VA_TX0_TX_PATH_CTL:
	case CDC_VA_TX0_TX_PATH_CFG0:
	case CDC_VA_TX0_TX_PATH_CFG1:
	case CDC_VA_TX0_TX_VOL_CTL:
	case CDC_VA_TX0_TX_PATH_SEC0:
	case CDC_VA_TX0_TX_PATH_SEC1:
	case CDC_VA_TX0_TX_PATH_SEC2:
	case CDC_VA_TX0_TX_PATH_SEC3:
	case CDC_VA_TX0_TX_PATH_SEC4:
	case CDC_VA_TX0_TX_PATH_SEC5:
	case CDC_VA_TX0_TX_PATH_SEC6:
	case CDC_VA_TX0_TX_PATH_SEC7:
	case CDC_VA_TX1_TX_PATH_CTL:
	case CDC_VA_TX1_TX_PATH_CFG0:
	case CDC_VA_TX1_TX_PATH_CFG1:
	case CDC_VA_TX1_TX_VOL_CTL:
	case CDC_VA_TX1_TX_PATH_SEC0:
	case CDC_VA_TX1_TX_PATH_SEC1:
	case CDC_VA_TX1_TX_PATH_SEC2:
	case CDC_VA_TX1_TX_PATH_SEC3:
	case CDC_VA_TX1_TX_PATH_SEC4:
	case CDC_VA_TX1_TX_PATH_SEC5:
	case CDC_VA_TX1_TX_PATH_SEC6:
	case CDC_VA_TX2_TX_PATH_CTL:
	case CDC_VA_TX2_TX_PATH_CFG0:
	case CDC_VA_TX2_TX_PATH_CFG1:
	case CDC_VA_TX2_TX_VOL_CTL:
	case CDC_VA_TX2_TX_PATH_SEC0:
	case CDC_VA_TX2_TX_PATH_SEC1:
	case CDC_VA_TX2_TX_PATH_SEC2:
	case CDC_VA_TX2_TX_PATH_SEC3:
	case CDC_VA_TX2_TX_PATH_SEC4:
	case CDC_VA_TX2_TX_PATH_SEC5:
	case CDC_VA_TX2_TX_PATH_SEC6:
	case CDC_VA_TX3_TX_PATH_CTL:
	case CDC_VA_TX3_TX_PATH_CFG0:
	case CDC_VA_TX3_TX_PATH_CFG1:
	case CDC_VA_TX3_TX_VOL_CTL:
	case CDC_VA_TX3_TX_PATH_SEC0:
	case CDC_VA_TX3_TX_PATH_SEC1:
	case CDC_VA_TX3_TX_PATH_SEC2:
	case CDC_VA_TX3_TX_PATH_SEC3:
	case CDC_VA_TX3_TX_PATH_SEC4:
	case CDC_VA_TX3_TX_PATH_SEC5:
	case CDC_VA_TX3_TX_PATH_SEC6:
		return true;
	}

	return false;
}

static bool va_is_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CDC_VA_TOP_CSR_CORE_ID_0:
	case CDC_VA_TOP_CSR_CORE_ID_1:
	case CDC_VA_TOP_CSR_CORE_ID_2:
	case CDC_VA_TOP_CSR_CORE_ID_3:
		return true;
	}

	return va_is_rw_register(dev, reg);
}

static const struct regmap_config va_regmap_config = {
	.name = "va_macro",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = va_defaults,
	.num_reg_defaults = ARRAY_SIZE(va_defaults),
	.max_register = VA_MAX_OFFSET,
	.volatile_reg = va_is_volatile_register,
	.readable_reg = va_is_readable_register,
	.writeable_reg = va_is_rw_register,
};

static int va_clk_rsc_fs_gen_request(struct va_macro *va, bool enable)
{
	struct regmap *regmap = va->regmap;

	if (enable) {
		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_MCLK_CONTROL,
				   CDC_VA_MCLK_CONTROL_EN,
				   CDC_VA_MCLK_CONTROL_EN);
		/* clear the fs counter */
		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL,
				   CDC_VA_FS_CONTROL_EN | CDC_VA_FS_COUNTER_CLR,
				   CDC_VA_FS_CONTROL_EN | CDC_VA_FS_COUNTER_CLR);
		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL,
				   CDC_VA_FS_CONTROL_EN | CDC_VA_FS_COUNTER_CLR,
				   CDC_VA_FS_CONTROL_EN);

		regmap_update_bits(regmap, CDC_VA_TOP_CSR_TOP_CFG0,
				   CDC_VA_FS_BROADCAST_EN,
				   CDC_VA_FS_BROADCAST_EN);
	} else {
		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_MCLK_CONTROL,
				   CDC_VA_MCLK_CONTROL_EN, 0x0);

		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_FS_CNT_CONTROL,
				   CDC_VA_FS_CONTROL_EN, 0x0);

		regmap_update_bits(regmap, CDC_VA_TOP_CSR_TOP_CFG0,
				   CDC_VA_FS_BROADCAST_EN, 0x0);
	}

	return 0;
}

static int va_macro_mclk_enable(struct va_macro *va, bool mclk_enable)
{
	struct regmap *regmap = va->regmap;

	if (mclk_enable) {
		va_clk_rsc_fs_gen_request(va, true);
		regcache_mark_dirty(regmap);
		regcache_sync_region(regmap, 0x0, VA_MAX_OFFSET);
	} else {
		va_clk_rsc_fs_gen_request(va, false);
	}

	return 0;
}

static int va_macro_mclk_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct va_macro *va = snd_soc_component_get_drvdata(comp);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return clk_prepare_enable(va->fsgen);
	case SND_SOC_DAPM_POST_PMD:
		clk_disable_unprepare(va->fsgen);
	}

	return 0;
}

static int va_macro_put_dec_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val;
	u16 mic_sel_reg;

	val = ucontrol->value.enumerated.item[0];

	switch (e->reg) {
	case CDC_VA_INP_MUX_ADC_MUX0_CFG0:
		mic_sel_reg = CDC_VA_TX0_TX_PATH_CFG0;
		break;
	case CDC_VA_INP_MUX_ADC_MUX1_CFG0:
		mic_sel_reg = CDC_VA_TX1_TX_PATH_CFG0;
		break;
	case CDC_VA_INP_MUX_ADC_MUX2_CFG0:
		mic_sel_reg = CDC_VA_TX2_TX_PATH_CFG0;
		break;
	case CDC_VA_INP_MUX_ADC_MUX3_CFG0:
		mic_sel_reg = CDC_VA_TX3_TX_PATH_CFG0;
		break;
	default:
		dev_err(component->dev, "%s: e->reg: 0x%x not expected\n",
			__func__, e->reg);
		return -EINVAL;
	}

	if (val != 0)
		snd_soc_component_update_bits(component, mic_sel_reg,
					      CDC_VA_TX_PATH_ADC_DMIC_SEL_MASK,
					      CDC_VA_TX_PATH_ADC_DMIC_SEL_DMIC);

	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static int va_macro_tx_mixer_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 dai_id = widget->shift;
	u32 dec_id = mc->shift;
	struct va_macro *va = snd_soc_component_get_drvdata(component);

	if (test_bit(dec_id, &va->active_ch_mask[dai_id]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int va_macro_tx_mixer_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
					snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct snd_soc_dapm_update *update = NULL;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 dai_id = widget->shift;
	u32 dec_id = mc->shift;
	u32 enable = ucontrol->value.integer.value[0];
	struct va_macro *va = snd_soc_component_get_drvdata(component);

	if (enable) {
		set_bit(dec_id, &va->active_ch_mask[dai_id]);
		va->active_ch_cnt[dai_id]++;
	} else {
		clear_bit(dec_id, &va->active_ch_mask[dai_id]);
		va->active_ch_cnt[dai_id]--;
	}

	snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, enable, update);

	return 0;
}

static int va_dmic_clk_enable(struct snd_soc_component *component,
			      u32 dmic, bool enable)
{
	struct va_macro *va = snd_soc_component_get_drvdata(component);
	u16 dmic_clk_reg;
	s32 *dmic_clk_cnt;
	u8 *dmic_clk_div;
	u8 freq_change_mask;
	u8 clk_div;

	switch (dmic) {
	case 0:
	case 1:
		dmic_clk_cnt = &(va->dmic_0_1_clk_cnt);
		dmic_clk_div = &(va->dmic_0_1_clk_div);
		dmic_clk_reg = CDC_VA_TOP_CSR_DMIC0_CTL;
		freq_change_mask = CDC_VA_DMIC0_FREQ_CHANGE_MASK;
		break;
	case 2:
	case 3:
		dmic_clk_cnt = &(va->dmic_2_3_clk_cnt);
		dmic_clk_div = &(va->dmic_2_3_clk_div);
		dmic_clk_reg = CDC_VA_TOP_CSR_DMIC1_CTL;
		freq_change_mask = CDC_VA_DMIC1_FREQ_CHANGE_MASK;
		break;
	case 4:
	case 5:
		dmic_clk_cnt = &(va->dmic_4_5_clk_cnt);
		dmic_clk_div = &(va->dmic_4_5_clk_div);
		dmic_clk_reg = CDC_VA_TOP_CSR_DMIC2_CTL;
		freq_change_mask = CDC_VA_DMIC2_FREQ_CHANGE_MASK;
		break;
	case 6:
	case 7:
		dmic_clk_cnt = &(va->dmic_6_7_clk_cnt);
		dmic_clk_div = &(va->dmic_6_7_clk_div);
		dmic_clk_reg = CDC_VA_TOP_CSR_DMIC3_CTL;
		freq_change_mask = CDC_VA_DMIC3_FREQ_CHANGE_MASK;
		break;
	default:
		dev_err(component->dev, "%s: Invalid DMIC Selection\n",
			__func__);
		return -EINVAL;
	}

	if (enable) {
		clk_div = va->dmic_clk_div;
		(*dmic_clk_cnt)++;
		if (*dmic_clk_cnt == 1) {
			snd_soc_component_update_bits(component,
					      CDC_VA_TOP_CSR_DMIC_CFG,
					      CDC_VA_RESET_ALL_DMICS_MASK,
					      CDC_VA_RESET_ALL_DMICS_DISABLE);
			snd_soc_component_update_bits(component, dmic_clk_reg,
					CDC_VA_DMIC_CLK_SEL_MASK,
					clk_div << CDC_VA_DMIC_CLK_SEL_SHFT);
			snd_soc_component_update_bits(component, dmic_clk_reg,
						      CDC_VA_DMIC_EN_MASK,
						      CDC_VA_DMIC_ENABLE);
		} else {
			if (*dmic_clk_div > clk_div) {
				snd_soc_component_update_bits(component,
						CDC_VA_TOP_CSR_DMIC_CFG,
						freq_change_mask,
						freq_change_mask);
				snd_soc_component_update_bits(component, dmic_clk_reg,
						CDC_VA_DMIC_CLK_SEL_MASK,
						clk_div << CDC_VA_DMIC_CLK_SEL_SHFT);
				snd_soc_component_update_bits(component,
					      CDC_VA_TOP_CSR_DMIC_CFG,
					      freq_change_mask,
					      CDC_VA_DMIC_FREQ_CHANGE_DISABLE);
			} else {
				clk_div = *dmic_clk_div;
			}
		}
		*dmic_clk_div = clk_div;
	} else {
		(*dmic_clk_cnt)--;
		if (*dmic_clk_cnt  == 0) {
			snd_soc_component_update_bits(component, dmic_clk_reg,
						      CDC_VA_DMIC_EN_MASK, 0);
			clk_div = 0;
			snd_soc_component_update_bits(component, dmic_clk_reg,
						CDC_VA_DMIC_CLK_SEL_MASK,
						clk_div << CDC_VA_DMIC_CLK_SEL_SHFT);
		} else {
			clk_div = va->dmic_clk_div;
			if (*dmic_clk_div > clk_div) {
				clk_div = va->dmic_clk_div;
				snd_soc_component_update_bits(component,
							CDC_VA_TOP_CSR_DMIC_CFG,
							freq_change_mask,
							freq_change_mask);
				snd_soc_component_update_bits(component, dmic_clk_reg,
						CDC_VA_DMIC_CLK_SEL_MASK,
						clk_div << CDC_VA_DMIC_CLK_SEL_SHFT);
				snd_soc_component_update_bits(component,
						      CDC_VA_TOP_CSR_DMIC_CFG,
						      freq_change_mask,
						      CDC_VA_DMIC_FREQ_CHANGE_DISABLE);
			} else {
				clk_div = *dmic_clk_div;
			}
		}
		*dmic_clk_div = clk_div;
	}

	return 0;
}

static int va_macro_enable_dmic(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	unsigned int dmic = w->shift;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		va_dmic_clk_enable(comp, dmic, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		va_dmic_clk_enable(comp, dmic, false);
		break;
	}

	return 0;
}

static int va_macro_enable_dec(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	unsigned int decimator;
	u16 tx_vol_ctl_reg, dec_cfg_reg, hpf_gate_reg;
	u16 tx_gain_ctl_reg;
	u8 hpf_cut_off_freq;

	struct va_macro *va = snd_soc_component_get_drvdata(comp);

	decimator = w->shift;

	tx_vol_ctl_reg = CDC_VA_TX0_TX_PATH_CTL +
				VA_MACRO_TX_PATH_OFFSET * decimator;
	hpf_gate_reg = CDC_VA_TX0_TX_PATH_SEC2 +
				VA_MACRO_TX_PATH_OFFSET * decimator;
	dec_cfg_reg = CDC_VA_TX0_TX_PATH_CFG0 +
				VA_MACRO_TX_PATH_OFFSET * decimator;
	tx_gain_ctl_reg = CDC_VA_TX0_TX_VOL_CTL +
				VA_MACRO_TX_PATH_OFFSET * decimator;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(comp,
			dec_cfg_reg, CDC_VA_ADC_MODE_MASK,
			va->dec_mode[decimator] << CDC_VA_ADC_MODE_SHIFT);
		/* Enable TX PGA Mute */
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* Enable TX CLK */
		snd_soc_component_update_bits(comp, tx_vol_ctl_reg,
					      CDC_VA_TX_PATH_CLK_EN_MASK,
					      CDC_VA_TX_PATH_CLK_EN);
		snd_soc_component_update_bits(comp, hpf_gate_reg,
					      CDC_VA_TX_HPF_ZERO_GATE_MASK,
					      CDC_VA_TX_HPF_ZERO_GATE);

		usleep_range(1000, 1010);
		hpf_cut_off_freq = (snd_soc_component_read(comp, dec_cfg_reg) &
				    TX_HPF_CUT_OFF_FREQ_MASK) >> 5;

		if (hpf_cut_off_freq != CF_MIN_3DB_150HZ) {
			snd_soc_component_update_bits(comp, dec_cfg_reg,
						      TX_HPF_CUT_OFF_FREQ_MASK,
						      CF_MIN_3DB_150HZ << 5);

			snd_soc_component_update_bits(comp, hpf_gate_reg,
				      CDC_VA_TX_HPF_CUTOFF_FREQ_CHANGE_MASK,
				      CDC_VA_TX_HPF_CUTOFF_FREQ_CHANGE_REQ);

			/*
			 * Minimum 1 clk cycle delay is required as per HW spec
			 */
			usleep_range(1000, 1010);

			snd_soc_component_update_bits(comp,
				hpf_gate_reg,
				CDC_VA_TX_HPF_CUTOFF_FREQ_CHANGE_MASK,
				0x0);
		}


		usleep_range(1000, 1010);
		snd_soc_component_update_bits(comp, hpf_gate_reg,
					      CDC_VA_TX_HPF_ZERO_GATE_MASK,
					      CDC_VA_TX_HPF_ZERO_NO_GATE);
		/*
		 * 6ms delay is required as per HW spec
		 */
		usleep_range(6000, 6010);
		/* apply gain after decimator is enabled */
		snd_soc_component_write(comp, tx_gain_ctl_reg,
			snd_soc_component_read(comp, tx_gain_ctl_reg));
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable TX CLK */
		snd_soc_component_update_bits(comp, tx_vol_ctl_reg,
						CDC_VA_TX_PATH_CLK_EN_MASK,
						CDC_VA_TX_PATH_CLK_DISABLE);
		break;
	}
	return 0;
}

static int va_macro_dec_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct va_macro *va = snd_soc_component_get_drvdata(comp);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;

	ucontrol->value.enumerated.item[0] = va->dec_mode[path];

	return 0;
}

static int va_macro_dec_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.enumerated.item[0];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int path = e->shift_l;
	struct va_macro *va = snd_soc_component_get_drvdata(comp);

	va->dec_mode[path] = value;

	return 0;
}

static int va_macro_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	int tx_fs_rate;
	struct snd_soc_component *component = dai->component;
	u32 decimator, sample_rate;
	u16 tx_fs_reg;
	struct device *va_dev = component->dev;
	struct va_macro *va = snd_soc_component_get_drvdata(component);

	sample_rate = params_rate(params);
	switch (sample_rate) {
	case 8000:
		tx_fs_rate = 0;
		break;
	case 16000:
		tx_fs_rate = 1;
		break;
	case 32000:
		tx_fs_rate = 3;
		break;
	case 48000:
		tx_fs_rate = 4;
		break;
	case 96000:
		tx_fs_rate = 5;
		break;
	case 192000:
		tx_fs_rate = 6;
		break;
	case 384000:
		tx_fs_rate = 7;
		break;
	default:
		dev_err(va_dev, "%s: Invalid TX sample rate: %d\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	for_each_set_bit(decimator, &va->active_ch_mask[dai->id],
			 VA_MACRO_DEC_MAX) {
		tx_fs_reg = CDC_VA_TX0_TX_PATH_CTL +
			    VA_MACRO_TX_PATH_OFFSET * decimator;
		snd_soc_component_update_bits(component, tx_fs_reg, 0x0F,
					      tx_fs_rate);
	}
	return 0;
}

static int va_macro_get_channel_map(struct snd_soc_dai *dai,
				    unsigned int *tx_num, unsigned int *tx_slot,
				    unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_component *component = dai->component;
	struct device *va_dev = component->dev;
	struct va_macro *va = snd_soc_component_get_drvdata(component);

	switch (dai->id) {
	case VA_MACRO_AIF1_CAP:
	case VA_MACRO_AIF2_CAP:
	case VA_MACRO_AIF3_CAP:
		*tx_slot = va->active_ch_mask[dai->id];
		*tx_num = va->active_ch_cnt[dai->id];
		break;
	default:
		dev_err(va_dev, "%s: Invalid AIF\n", __func__);
		break;
	}
	return 0;
}

static int va_macro_digital_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct va_macro *va = snd_soc_component_get_drvdata(component);
	u16 tx_vol_ctl_reg, decimator;

	for_each_set_bit(decimator, &va->active_ch_mask[dai->id],
			 VA_MACRO_DEC_MAX) {
		tx_vol_ctl_reg = CDC_VA_TX0_TX_PATH_CTL +
					VA_MACRO_TX_PATH_OFFSET * decimator;
		if (mute)
			snd_soc_component_update_bits(component, tx_vol_ctl_reg,
					CDC_VA_TX_PATH_PGA_MUTE_EN_MASK,
					CDC_VA_TX_PATH_PGA_MUTE_EN);
		else
			snd_soc_component_update_bits(component, tx_vol_ctl_reg,
					CDC_VA_TX_PATH_PGA_MUTE_EN_MASK,
					CDC_VA_TX_PATH_PGA_MUTE_DISABLE);
	}

	return 0;
}

static const struct snd_soc_dai_ops va_macro_dai_ops = {
	.hw_params = va_macro_hw_params,
	.get_channel_map = va_macro_get_channel_map,
	.mute_stream = va_macro_digital_mute,
};

static struct snd_soc_dai_driver va_macro_dais[] = {
	{
		.name = "va_macro_tx1",
		.id = VA_MACRO_AIF1_CAP,
		.capture = {
			.stream_name = "VA_AIF1 Capture",
			.rates = VA_MACRO_RATES,
			.formats = VA_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &va_macro_dai_ops,
	},
	{
		.name = "va_macro_tx2",
		.id = VA_MACRO_AIF2_CAP,
		.capture = {
			.stream_name = "VA_AIF2 Capture",
			.rates = VA_MACRO_RATES,
			.formats = VA_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &va_macro_dai_ops,
	},
	{
		.name = "va_macro_tx3",
		.id = VA_MACRO_AIF3_CAP,
		.capture = {
			.stream_name = "VA_AIF3 Capture",
			.rates = VA_MACRO_RATES,
			.formats = VA_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &va_macro_dai_ops,
	},
};

static const char * const adc_mux_text[] = {
	"VA_DMIC", "SWR_MIC"
};

static SOC_ENUM_SINGLE_DECL(va_dec0_enum, CDC_VA_INP_MUX_ADC_MUX0_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(va_dec1_enum, CDC_VA_INP_MUX_ADC_MUX1_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(va_dec2_enum, CDC_VA_INP_MUX_ADC_MUX2_CFG1,
		   0, adc_mux_text);
static SOC_ENUM_SINGLE_DECL(va_dec3_enum, CDC_VA_INP_MUX_ADC_MUX3_CFG1,
		   0, adc_mux_text);

static const struct snd_kcontrol_new va_dec0_mux = SOC_DAPM_ENUM("va_dec0",
								 va_dec0_enum);
static const struct snd_kcontrol_new va_dec1_mux = SOC_DAPM_ENUM("va_dec1",
								 va_dec1_enum);
static const struct snd_kcontrol_new va_dec2_mux = SOC_DAPM_ENUM("va_dec2",
								 va_dec2_enum);
static const struct snd_kcontrol_new va_dec3_mux = SOC_DAPM_ENUM("va_dec3",
								 va_dec3_enum);

static const char * const dmic_mux_text[] = {
	"ZERO", "DMIC0", "DMIC1", "DMIC2", "DMIC3",
	"DMIC4", "DMIC5", "DMIC6", "DMIC7"
};

static SOC_ENUM_SINGLE_DECL(va_dmic0_enum, CDC_VA_INP_MUX_ADC_MUX0_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(va_dmic1_enum, CDC_VA_INP_MUX_ADC_MUX1_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(va_dmic2_enum, CDC_VA_INP_MUX_ADC_MUX2_CFG0,
			4, dmic_mux_text);

static SOC_ENUM_SINGLE_DECL(va_dmic3_enum, CDC_VA_INP_MUX_ADC_MUX3_CFG0,
			4, dmic_mux_text);

static const struct snd_kcontrol_new va_dmic0_mux = SOC_DAPM_ENUM_EXT("va_dmic0",
			 va_dmic0_enum, snd_soc_dapm_get_enum_double,
			 va_macro_put_dec_enum);

static const struct snd_kcontrol_new va_dmic1_mux = SOC_DAPM_ENUM_EXT("va_dmic1",
			 va_dmic1_enum, snd_soc_dapm_get_enum_double,
			 va_macro_put_dec_enum);

static const struct snd_kcontrol_new va_dmic2_mux = SOC_DAPM_ENUM_EXT("va_dmic2",
			 va_dmic2_enum, snd_soc_dapm_get_enum_double,
			 va_macro_put_dec_enum);

static const struct snd_kcontrol_new va_dmic3_mux = SOC_DAPM_ENUM_EXT("va_dmic3",
			 va_dmic3_enum, snd_soc_dapm_get_enum_double,
			 va_macro_put_dec_enum);

static const struct snd_kcontrol_new va_aif1_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, VA_MACRO_DEC0, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, VA_MACRO_DEC1, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, VA_MACRO_DEC2, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, VA_MACRO_DEC3, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, VA_MACRO_DEC4, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, VA_MACRO_DEC5, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, VA_MACRO_DEC6, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, VA_MACRO_DEC7, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new va_aif2_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, VA_MACRO_DEC0, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, VA_MACRO_DEC1, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, VA_MACRO_DEC2, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, VA_MACRO_DEC3, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, VA_MACRO_DEC4, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, VA_MACRO_DEC5, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, VA_MACRO_DEC6, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, VA_MACRO_DEC7, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new va_aif3_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, VA_MACRO_DEC0, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, VA_MACRO_DEC1, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, VA_MACRO_DEC2, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, VA_MACRO_DEC3, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, VA_MACRO_DEC4, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, VA_MACRO_DEC5, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, VA_MACRO_DEC6, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, VA_MACRO_DEC7, 1, 0,
			va_macro_tx_mixer_get, va_macro_tx_mixer_put),
};

static const struct snd_soc_dapm_widget va_macro_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("VA_AIF1 CAP", "VA_AIF1 Capture", 0,
		SND_SOC_NOPM, VA_MACRO_AIF1_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("VA_AIF2 CAP", "VA_AIF2 Capture", 0,
		SND_SOC_NOPM, VA_MACRO_AIF2_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("VA_AIF3 CAP", "VA_AIF3 Capture", 0,
		SND_SOC_NOPM, VA_MACRO_AIF3_CAP, 0),

	SND_SOC_DAPM_MIXER("VA_AIF1_CAP Mixer", SND_SOC_NOPM,
		VA_MACRO_AIF1_CAP, 0,
		va_aif1_cap_mixer, ARRAY_SIZE(va_aif1_cap_mixer)),

	SND_SOC_DAPM_MIXER("VA_AIF2_CAP Mixer", SND_SOC_NOPM,
		VA_MACRO_AIF2_CAP, 0,
		va_aif2_cap_mixer, ARRAY_SIZE(va_aif2_cap_mixer)),

	SND_SOC_DAPM_MIXER("VA_AIF3_CAP Mixer", SND_SOC_NOPM,
		VA_MACRO_AIF3_CAP, 0,
		va_aif3_cap_mixer, ARRAY_SIZE(va_aif3_cap_mixer)),

	SND_SOC_DAPM_MUX("VA DMIC MUX0", SND_SOC_NOPM, 0, 0, &va_dmic0_mux),
	SND_SOC_DAPM_MUX("VA DMIC MUX1", SND_SOC_NOPM, 0, 0, &va_dmic1_mux),
	SND_SOC_DAPM_MUX("VA DMIC MUX2", SND_SOC_NOPM, 0, 0, &va_dmic2_mux),
	SND_SOC_DAPM_MUX("VA DMIC MUX3", SND_SOC_NOPM, 0, 0, &va_dmic3_mux),

	SND_SOC_DAPM_REGULATOR_SUPPLY("vdd-micb", 0, 0),
	SND_SOC_DAPM_INPUT("DMIC0 Pin"),
	SND_SOC_DAPM_INPUT("DMIC1 Pin"),
	SND_SOC_DAPM_INPUT("DMIC2 Pin"),
	SND_SOC_DAPM_INPUT("DMIC3 Pin"),
	SND_SOC_DAPM_INPUT("DMIC4 Pin"),
	SND_SOC_DAPM_INPUT("DMIC5 Pin"),
	SND_SOC_DAPM_INPUT("DMIC6 Pin"),
	SND_SOC_DAPM_INPUT("DMIC7 Pin"),

	SND_SOC_DAPM_ADC_E("VA DMIC0", NULL, SND_SOC_NOPM, 0, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC1", NULL, SND_SOC_NOPM, 1, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC2", NULL, SND_SOC_NOPM, 2, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC3", NULL, SND_SOC_NOPM, 3, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC4", NULL, SND_SOC_NOPM, 4, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC5", NULL, SND_SOC_NOPM, 5, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC6", NULL, SND_SOC_NOPM, 6, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("VA DMIC7", NULL, SND_SOC_NOPM, 7, 0,
		va_macro_enable_dmic, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("VA SWR_ADC0"),
	SND_SOC_DAPM_INPUT("VA SWR_ADC1"),
	SND_SOC_DAPM_INPUT("VA SWR_ADC2"),
	SND_SOC_DAPM_INPUT("VA SWR_ADC3"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC0"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC1"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC2"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC3"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC4"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC5"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC6"),
	SND_SOC_DAPM_INPUT("VA SWR_MIC7"),

	SND_SOC_DAPM_MUX_E("VA DEC0 MUX", SND_SOC_NOPM, VA_MACRO_DEC0, 0,
			   &va_dec0_mux, va_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("VA DEC1 MUX", SND_SOC_NOPM, VA_MACRO_DEC1, 0,
			   &va_dec1_mux, va_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("VA DEC2 MUX", SND_SOC_NOPM, VA_MACRO_DEC2, 0,
			   &va_dec2_mux, va_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("VA DEC3 MUX", SND_SOC_NOPM, VA_MACRO_DEC3, 0,
			   &va_dec3_mux, va_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("VA_MCLK", -1, SND_SOC_NOPM, 0, 0,
			      va_macro_mclk_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route va_audio_map[] = {
	{"VA_AIF1 CAP", NULL, "VA_MCLK"},
	{"VA_AIF2 CAP", NULL, "VA_MCLK"},
	{"VA_AIF3 CAP", NULL, "VA_MCLK"},

	{"VA_AIF1 CAP", NULL, "VA_AIF1_CAP Mixer"},
	{"VA_AIF2 CAP", NULL, "VA_AIF2_CAP Mixer"},
	{"VA_AIF3 CAP", NULL, "VA_AIF3_CAP Mixer"},

	{"VA_AIF1_CAP Mixer", "DEC0", "VA DEC0 MUX"},
	{"VA_AIF1_CAP Mixer", "DEC1", "VA DEC1 MUX"},
	{"VA_AIF1_CAP Mixer", "DEC2", "VA DEC2 MUX"},
	{"VA_AIF1_CAP Mixer", "DEC3", "VA DEC3 MUX"},

	{"VA_AIF2_CAP Mixer", "DEC0", "VA DEC0 MUX"},
	{"VA_AIF2_CAP Mixer", "DEC1", "VA DEC1 MUX"},
	{"VA_AIF2_CAP Mixer", "DEC2", "VA DEC2 MUX"},
	{"VA_AIF2_CAP Mixer", "DEC3", "VA DEC3 MUX"},

	{"VA_AIF3_CAP Mixer", "DEC0", "VA DEC0 MUX"},
	{"VA_AIF3_CAP Mixer", "DEC1", "VA DEC1 MUX"},
	{"VA_AIF3_CAP Mixer", "DEC2", "VA DEC2 MUX"},
	{"VA_AIF3_CAP Mixer", "DEC3", "VA DEC3 MUX"},

	{"VA DEC0 MUX", "VA_DMIC", "VA DMIC MUX0"},
	{"VA DMIC MUX0", "DMIC0", "VA DMIC0"},
	{"VA DMIC MUX0", "DMIC1", "VA DMIC1"},
	{"VA DMIC MUX0", "DMIC2", "VA DMIC2"},
	{"VA DMIC MUX0", "DMIC3", "VA DMIC3"},
	{"VA DMIC MUX0", "DMIC4", "VA DMIC4"},
	{"VA DMIC MUX0", "DMIC5", "VA DMIC5"},
	{"VA DMIC MUX0", "DMIC6", "VA DMIC6"},
	{"VA DMIC MUX0", "DMIC7", "VA DMIC7"},

	{"VA DEC1 MUX", "VA_DMIC", "VA DMIC MUX1"},
	{"VA DMIC MUX1", "DMIC0", "VA DMIC0"},
	{"VA DMIC MUX1", "DMIC1", "VA DMIC1"},
	{"VA DMIC MUX1", "DMIC2", "VA DMIC2"},
	{"VA DMIC MUX1", "DMIC3", "VA DMIC3"},
	{"VA DMIC MUX1", "DMIC4", "VA DMIC4"},
	{"VA DMIC MUX1", "DMIC5", "VA DMIC5"},
	{"VA DMIC MUX1", "DMIC6", "VA DMIC6"},
	{"VA DMIC MUX1", "DMIC7", "VA DMIC7"},

	{"VA DEC2 MUX", "VA_DMIC", "VA DMIC MUX2"},
	{"VA DMIC MUX2", "DMIC0", "VA DMIC0"},
	{"VA DMIC MUX2", "DMIC1", "VA DMIC1"},
	{"VA DMIC MUX2", "DMIC2", "VA DMIC2"},
	{"VA DMIC MUX2", "DMIC3", "VA DMIC3"},
	{"VA DMIC MUX2", "DMIC4", "VA DMIC4"},
	{"VA DMIC MUX2", "DMIC5", "VA DMIC5"},
	{"VA DMIC MUX2", "DMIC6", "VA DMIC6"},
	{"VA DMIC MUX2", "DMIC7", "VA DMIC7"},

	{"VA DEC3 MUX", "VA_DMIC", "VA DMIC MUX3"},
	{"VA DMIC MUX3", "DMIC0", "VA DMIC0"},
	{"VA DMIC MUX3", "DMIC1", "VA DMIC1"},
	{"VA DMIC MUX3", "DMIC2", "VA DMIC2"},
	{"VA DMIC MUX3", "DMIC3", "VA DMIC3"},
	{"VA DMIC MUX3", "DMIC4", "VA DMIC4"},
	{"VA DMIC MUX3", "DMIC5", "VA DMIC5"},
	{"VA DMIC MUX3", "DMIC6", "VA DMIC6"},
	{"VA DMIC MUX3", "DMIC7", "VA DMIC7"},

	{ "VA DMIC0", NULL, "DMIC0 Pin" },
	{ "VA DMIC1", NULL, "DMIC1 Pin" },
	{ "VA DMIC2", NULL, "DMIC2 Pin" },
	{ "VA DMIC3", NULL, "DMIC3 Pin" },
	{ "VA DMIC4", NULL, "DMIC4 Pin" },
	{ "VA DMIC5", NULL, "DMIC5 Pin" },
	{ "VA DMIC6", NULL, "DMIC6 Pin" },
	{ "VA DMIC7", NULL, "DMIC7 Pin" },
};

static const char * const dec_mode_mux_text[] = {
	"ADC_DEFAULT", "ADC_LOW_PWR", "ADC_HIGH_PERF",
};

static const struct soc_enum dec_mode_mux_enum[] = {
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 1, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 2,  ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 3, ARRAY_SIZE(dec_mode_mux_text),
			dec_mode_mux_text),
};

static const struct snd_kcontrol_new va_macro_snd_controls[] = {
	SOC_SINGLE_S8_TLV("VA_DEC0 Volume", CDC_VA_TX0_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("VA_DEC1 Volume", CDC_VA_TX1_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("VA_DEC2 Volume", CDC_VA_TX2_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("VA_DEC3 Volume", CDC_VA_TX3_TX_VOL_CTL,
			  -84, 40, digital_gain),

	SOC_ENUM_EXT("VA_DEC0 MODE", dec_mode_mux_enum[0],
		     va_macro_dec_mode_get, va_macro_dec_mode_put),
	SOC_ENUM_EXT("VA_DEC1 MODE", dec_mode_mux_enum[1],
		     va_macro_dec_mode_get, va_macro_dec_mode_put),
	SOC_ENUM_EXT("VA_DEC2 MODE", dec_mode_mux_enum[2],
		     va_macro_dec_mode_get, va_macro_dec_mode_put),
	SOC_ENUM_EXT("VA_DEC3 MODE", dec_mode_mux_enum[3],
		     va_macro_dec_mode_get, va_macro_dec_mode_put),
};

static int va_macro_component_probe(struct snd_soc_component *component)
{
	struct va_macro *va = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, va->regmap);

	return 0;
}

static const struct snd_soc_component_driver va_macro_component_drv = {
	.name = "VA MACRO",
	.probe = va_macro_component_probe,
	.controls = va_macro_snd_controls,
	.num_controls = ARRAY_SIZE(va_macro_snd_controls),
	.dapm_widgets = va_macro_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(va_macro_dapm_widgets),
	.dapm_routes = va_audio_map,
	.num_dapm_routes = ARRAY_SIZE(va_audio_map),
};

static int fsgen_gate_enable(struct clk_hw *hw)
{
	struct va_macro *va = to_va_macro(hw);
	struct regmap *regmap = va->regmap;
	int ret;

	ret = va_macro_mclk_enable(va, true);
	if (!va->has_swr_master)
		return ret;

	regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_VA_SWR_RESET_MASK,  CDC_VA_SWR_RESET_ENABLE);

	regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_VA_SWR_CLK_EN_MASK,
			   CDC_VA_SWR_CLK_ENABLE);
	regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_VA_SWR_RESET_MASK, 0x0);

	return ret;
}

static void fsgen_gate_disable(struct clk_hw *hw)
{
	struct va_macro *va = to_va_macro(hw);
	struct regmap *regmap = va->regmap;

	if (va->has_swr_master)
		regmap_update_bits(regmap, CDC_VA_CLK_RST_CTRL_SWR_CONTROL,
			   CDC_VA_SWR_CLK_EN_MASK, 0x0);

	va_macro_mclk_enable(va, false);
}

static int fsgen_gate_is_enabled(struct clk_hw *hw)
{
	struct va_macro *va = to_va_macro(hw);
	int val;

	regmap_read(va->regmap, CDC_VA_TOP_CSR_TOP_CFG0, &val);

	return  !!(val & CDC_VA_FS_BROADCAST_EN);
}

static const struct clk_ops fsgen_gate_ops = {
	.prepare = fsgen_gate_enable,
	.unprepare = fsgen_gate_disable,
	.is_enabled = fsgen_gate_is_enabled,
};

static int va_macro_register_fsgen_output(struct va_macro *va)
{
	struct clk *parent = va->mclk;
	struct device *dev = va->dev;
	struct device_node *np = dev->of_node;
	const char *parent_clk_name;
	const char *clk_name = "fsgen";
	struct clk_init_data init;
	int ret;

	parent_clk_name = __clk_get_name(parent);

	of_property_read_string(np, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &fsgen_gate_ops;
	init.flags = 0;
	init.parent_names = &parent_clk_name;
	init.num_parents = 1;
	va->hw.init = &init;
	ret = devm_clk_hw_register(va->dev, &va->hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &va->hw);
}

static int va_macro_validate_dmic_sample_rate(u32 dmic_sample_rate,
					      struct va_macro *va)
{
	u32 div_factor;
	u32 mclk_rate = VA_MACRO_MCLK_FREQ;

	if (!dmic_sample_rate || mclk_rate % dmic_sample_rate != 0)
		goto undefined_rate;

	div_factor = mclk_rate / dmic_sample_rate;

	switch (div_factor) {
	case 2:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_2;
		break;
	case 3:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_3;
		break;
	case 4:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_4;
		break;
	case 6:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_6;
		break;
	case 8:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_8;
		break;
	case 16:
		va->dmic_clk_div = VA_MACRO_CLK_DIV_16;
		break;
	default:
		/* Any other DIV factor is invalid */
		goto undefined_rate;
	}

	return dmic_sample_rate;

undefined_rate:
	dev_err(va->dev, "%s: Invalid rate %d, for mclk %d\n",
		__func__, dmic_sample_rate, mclk_rate);
	dmic_sample_rate = 0;

	return dmic_sample_rate;
}

static int va_macro_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct va_macro_data *data;
	struct va_macro *va;
	void __iomem *base;
	u32 sample_rate = 0;
	int ret;

	va = devm_kzalloc(dev, sizeof(*va), GFP_KERNEL);
	if (!va)
		return -ENOMEM;

	va->dev = dev;

	va->macro = devm_clk_get_optional(dev, "macro");
	if (IS_ERR(va->macro))
		return PTR_ERR(va->macro);

	va->dcodec = devm_clk_get_optional(dev, "dcodec");
	if (IS_ERR(va->dcodec))
		return PTR_ERR(va->dcodec);

	va->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(va->mclk))
		return PTR_ERR(va->mclk);

	va->pds = lpass_macro_pds_init(dev);
	if (IS_ERR(va->pds))
		return PTR_ERR(va->pds);

	ret = of_property_read_u32(dev->of_node, "qcom,dmic-sample-rate",
				   &sample_rate);
	if (ret) {
		dev_err(dev, "qcom,dmic-sample-rate dt entry missing\n");
		va->dmic_clk_div = VA_MACRO_CLK_DIV_2;
	} else {
		ret = va_macro_validate_dmic_sample_rate(sample_rate, va);
		if (!ret) {
			ret = -EINVAL;
			goto err;
		}
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err;
	}

	va->regmap = devm_regmap_init_mmio(dev, base,  &va_regmap_config);
	if (IS_ERR(va->regmap)) {
		ret = -EINVAL;
		goto err;
	}

	dev_set_drvdata(dev, va);

	data = of_device_get_match_data(dev);
	va->has_swr_master = data->has_swr_master;

	/* mclk rate */
	clk_set_rate(va->mclk, 2 * VA_MACRO_MCLK_FREQ);

	ret = clk_prepare_enable(va->macro);
	if (ret)
		goto err;

	ret = clk_prepare_enable(va->dcodec);
	if (ret)
		goto err_dcodec;

	ret = clk_prepare_enable(va->mclk);
	if (ret)
		goto err_mclk;

	ret = va_macro_register_fsgen_output(va);
	if (ret)
		goto err_clkout;

	va->fsgen = clk_hw_get_clk(&va->hw, "fsgen");
	if (IS_ERR(va->fsgen)) {
		ret = PTR_ERR(va->fsgen);
		goto err_clkout;
	}

	if (va->has_swr_master) {
		/* Set default CLK div to 1 */
		regmap_update_bits(va->regmap, CDC_VA_TOP_CSR_SWR_MIC_CTL0,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_MASK,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_DIV1);
		regmap_update_bits(va->regmap, CDC_VA_TOP_CSR_SWR_MIC_CTL1,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_MASK,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_DIV1);
		regmap_update_bits(va->regmap, CDC_VA_TOP_CSR_SWR_MIC_CTL2,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_MASK,
				  CDC_VA_SWR_MIC_CLK_SEL_0_1_DIV1);

	}

	ret = devm_snd_soc_register_component(dev, &va_macro_component_drv,
					      va_macro_dais,
					      ARRAY_SIZE(va_macro_dais));
	if (ret)
		goto err_clkout;

	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

err_clkout:
	clk_disable_unprepare(va->mclk);
err_mclk:
	clk_disable_unprepare(va->dcodec);
err_dcodec:
	clk_disable_unprepare(va->macro);
err:
	lpass_macro_pds_exit(va->pds);

	return ret;
}

static int va_macro_remove(struct platform_device *pdev)
{
	struct va_macro *va = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(va->mclk);
	clk_disable_unprepare(va->dcodec);
	clk_disable_unprepare(va->macro);

	lpass_macro_pds_exit(va->pds);

	return 0;
}

static int __maybe_unused va_macro_runtime_suspend(struct device *dev)
{
	struct va_macro *va = dev_get_drvdata(dev);

	regcache_cache_only(va->regmap, true);
	regcache_mark_dirty(va->regmap);

	clk_disable_unprepare(va->mclk);

	return 0;
}

static int __maybe_unused va_macro_runtime_resume(struct device *dev)
{
	struct va_macro *va = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(va->mclk);
	if (ret) {
		dev_err(va->dev, "unable to prepare mclk\n");
		return ret;
	}

	regcache_cache_only(va->regmap, false);
	regcache_sync(va->regmap);

	return 0;
}


static const struct dev_pm_ops va_macro_pm_ops = {
	SET_RUNTIME_PM_OPS(va_macro_runtime_suspend, va_macro_runtime_resume, NULL)
};

static const struct of_device_id va_macro_dt_match[] = {
	{ .compatible = "qcom,sc7280-lpass-va-macro", .data = &sm8250_va_data },
	{ .compatible = "qcom,sm8250-lpass-va-macro", .data = &sm8250_va_data },
	{ .compatible = "qcom,sm8450-lpass-va-macro", .data = &sm8450_va_data },
	{ .compatible = "qcom,sc8280xp-lpass-va-macro", .data = &sm8450_va_data },
	{}
};
MODULE_DEVICE_TABLE(of, va_macro_dt_match);

static struct platform_driver va_macro_driver = {
	.driver = {
		.name = "va_macro",
		.of_match_table = va_macro_dt_match,
		.suppress_bind_attrs = true,
		.pm = &va_macro_pm_ops,
	},
	.probe = va_macro_probe,
	.remove = va_macro_remove,
};

module_platform_driver(va_macro_driver);
MODULE_DESCRIPTION("VA macro driver");
MODULE_LICENSE("GPL");
