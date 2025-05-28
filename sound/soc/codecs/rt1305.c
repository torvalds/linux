// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt1305.c  --  RT1305 ALSA SoC amplifier component driver
 *
 * Copyright 2018 Realtek Semiconductor Corp.
 * Author: Shuming Fan <shumingf@realtek.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt1305.h"


#define RT1305_PR_RANGE_BASE (0xff + 1)
#define RT1305_PR_SPACING 0x100

#define RT1305_PR_BASE (RT1305_PR_RANGE_BASE + (0 * RT1305_PR_SPACING))


static const struct regmap_range_cfg rt1305_ranges[] = {
	{
		.name = "PR",
		.range_min = RT1305_PR_BASE,
		.range_max = RT1305_PR_BASE + 0xff,
		.selector_reg = RT1305_PRIV_INDEX,
		.selector_mask = 0xff,
		.selector_shift = 0x0,
		.window_start = RT1305_PRIV_DATA,
		.window_len = 0x1,
	},
};


static const struct reg_sequence init_list[] = {

	{ RT1305_PR_BASE + 0xcf, 0x5548 },
	{ RT1305_PR_BASE + 0x5d, 0x0442 },
	{ RT1305_PR_BASE + 0xc1, 0x0320 },

	{ RT1305_POWER_STATUS, 0x0000 },

	{ RT1305_SPK_TEMP_PROTECTION_1, 0xd6de },
	{ RT1305_SPK_TEMP_PROTECTION_2, 0x0707 },
	{ RT1305_SPK_TEMP_PROTECTION_3, 0x4090 },

	{ RT1305_DAC_SET_1, 0xdfdf },	/* 4 ohm 2W  */
	{ RT1305_ADC_SET_3, 0x0219 },
	{ RT1305_ADC_SET_1, 0x170f },	/* 0.2 ohm RSense*/

};
#define RT1305_INIT_REG_LEN ARRAY_SIZE(init_list)

struct rt1305_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;

	int sysclk;
	int sysclk_src;
	int lrck;
	int bclk;
	int master;

	int pll_src;
	int pll_in;
	int pll_out;
};

static const struct reg_default rt1305_reg[] = {

	{ 0x04, 0x0400 },
	{ 0x05, 0x0880 },
	{ 0x06, 0x0000 },
	{ 0x07, 0x3100 },
	{ 0x08, 0x8000 },
	{ 0x09, 0x0000 },
	{ 0x0a, 0x087e },
	{ 0x0b, 0x0020 },
	{ 0x0c, 0x0802 },
	{ 0x0d, 0x0020 },
	{ 0x10, 0x1d1d },
	{ 0x11, 0x1d1d },
	{ 0x12, 0xffff },
	{ 0x14, 0x000c },
	{ 0x16, 0x1717 },
	{ 0x17, 0x4000 },
	{ 0x18, 0x0019 },
	{ 0x20, 0x0000 },
	{ 0x22, 0x0000 },
	{ 0x24, 0x0000 },
	{ 0x26, 0x0000 },
	{ 0x28, 0x0000 },
	{ 0x2a, 0x4000 },
	{ 0x2b, 0x3000 },
	{ 0x2d, 0x6000 },
	{ 0x2e, 0x0000 },
	{ 0x2f, 0x8000 },
	{ 0x32, 0x0000 },
	{ 0x39, 0x0001 },
	{ 0x3a, 0x0000 },
	{ 0x3b, 0x1020 },
	{ 0x3c, 0x0000 },
	{ 0x3d, 0x0000 },
	{ 0x3e, 0x4c00 },
	{ 0x3f, 0x3000 },
	{ 0x40, 0x000c },
	{ 0x42, 0x0400 },
	{ 0x46, 0xc22c },
	{ 0x47, 0x0000 },
	{ 0x4b, 0x0000 },
	{ 0x4c, 0x0300 },
	{ 0x4f, 0xf000 },
	{ 0x50, 0xc200 },
	{ 0x51, 0x1f1f },
	{ 0x52, 0x01f0 },
	{ 0x53, 0x407f },
	{ 0x54, 0xffff },
	{ 0x58, 0x4005 },
	{ 0x5e, 0x0000 },
	{ 0x5f, 0x0000 },
	{ 0x60, 0xee13 },
	{ 0x62, 0x0000 },
	{ 0x63, 0x5f5f },
	{ 0x64, 0x0040 },
	{ 0x65, 0x4000 },
	{ 0x66, 0x4004 },
	{ 0x67, 0x0306 },
	{ 0x68, 0x8c04 },
	{ 0x69, 0xe021 },
	{ 0x6a, 0x0000 },
	{ 0x6c, 0xaaaa },
	{ 0x70, 0x0333 },
	{ 0x71, 0x3330 },
	{ 0x72, 0x3333 },
	{ 0x73, 0x3300 },
	{ 0x74, 0x0000 },
	{ 0x75, 0x0000 },
	{ 0x76, 0x0000 },
	{ 0x7a, 0x0003 },
	{ 0x7c, 0x10ec },
	{ 0x7e, 0x6251 },
	{ 0x80, 0x0800 },
	{ 0x81, 0x4000 },
	{ 0x82, 0x0000 },
	{ 0x90, 0x7a01 },
	{ 0x91, 0x8431 },
	{ 0x92, 0x0180 },
	{ 0x93, 0x0000 },
	{ 0x94, 0x0000 },
	{ 0x95, 0x0000 },
	{ 0x96, 0x0000 },
	{ 0x97, 0x0000 },
	{ 0x98, 0x0000 },
	{ 0x99, 0x0000 },
	{ 0x9a, 0x0000 },
	{ 0x9b, 0x0000 },
	{ 0x9c, 0x0000 },
	{ 0x9d, 0x0000 },
	{ 0x9e, 0x0000 },
	{ 0x9f, 0x0000 },
	{ 0xa0, 0x0000 },
	{ 0xb0, 0x8200 },
	{ 0xb1, 0x00ff },
	{ 0xb2, 0x0008 },
	{ 0xc0, 0x0200 },
	{ 0xc1, 0x0000 },
	{ 0xc2, 0x0000 },
	{ 0xc3, 0x0000 },
	{ 0xc4, 0x0000 },
	{ 0xc5, 0x0000 },
	{ 0xc6, 0x0000 },
	{ 0xc7, 0x0000 },
	{ 0xc8, 0x0000 },
	{ 0xc9, 0x0000 },
	{ 0xca, 0x0200 },
	{ 0xcb, 0x0000 },
	{ 0xcc, 0x0000 },
	{ 0xcd, 0x0000 },
	{ 0xce, 0x0000 },
	{ 0xcf, 0x0000 },
	{ 0xd0, 0x0000 },
	{ 0xd1, 0x0000 },
	{ 0xd2, 0x0000 },
	{ 0xd3, 0x0000 },
	{ 0xd4, 0x0200 },
	{ 0xd5, 0x0000 },
	{ 0xd6, 0x0000 },
	{ 0xd7, 0x0000 },
	{ 0xd8, 0x0000 },
	{ 0xd9, 0x0000 },
	{ 0xda, 0x0000 },
	{ 0xdb, 0x0000 },
	{ 0xdc, 0x0000 },
	{ 0xdd, 0x0000 },
	{ 0xde, 0x0200 },
	{ 0xdf, 0x0000 },
	{ 0xe0, 0x0000 },
	{ 0xe1, 0x0000 },
	{ 0xe2, 0x0000 },
	{ 0xe3, 0x0000 },
	{ 0xe4, 0x0000 },
	{ 0xe5, 0x0000 },
	{ 0xe6, 0x0000 },
	{ 0xe7, 0x0000 },
	{ 0xe8, 0x0200 },
	{ 0xe9, 0x0000 },
	{ 0xea, 0x0000 },
	{ 0xeb, 0x0000 },
	{ 0xec, 0x0000 },
	{ 0xed, 0x0000 },
	{ 0xee, 0x0000 },
	{ 0xef, 0x0000 },
	{ 0xf0, 0x0000 },
	{ 0xf1, 0x0000 },
	{ 0xf2, 0x0200 },
	{ 0xf3, 0x0000 },
	{ 0xf4, 0x0000 },
	{ 0xf5, 0x0000 },
	{ 0xf6, 0x0000 },
	{ 0xf7, 0x0000 },
	{ 0xf8, 0x0000 },
	{ 0xf9, 0x0000 },
	{ 0xfa, 0x0000 },
	{ 0xfb, 0x0000 },
};

static int rt1305_reg_init(struct snd_soc_component *component)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	regmap_multi_reg_write(rt1305->regmap, init_list, RT1305_INIT_REG_LEN);
	return 0;
}

static bool rt1305_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt1305_ranges); i++) {
		if (reg >= rt1305_ranges[i].range_min &&
			reg <= rt1305_ranges[i].range_max) {
			return true;
		}
	}

	switch (reg) {
	case RT1305_RESET:
	case RT1305_SPDIF_IN_SET_1:
	case RT1305_SPDIF_IN_SET_2:
	case RT1305_SPDIF_IN_SET_3:
	case RT1305_POWER_CTRL_2:
	case RT1305_CLOCK_DETECT:
	case RT1305_BIQUAD_SET_1:
	case RT1305_BIQUAD_SET_2:
	case RT1305_EQ_SET_2:
	case RT1305_SPK_TEMP_PROTECTION_0:
	case RT1305_SPK_TEMP_PROTECTION_2:
	case RT1305_SPK_DC_DETECT_1:
	case RT1305_SILENCE_DETECT:
	case RT1305_VERSION_ID:
	case RT1305_VENDOR_ID:
	case RT1305_DEVICE_ID:
	case RT1305_EFUSE_1:
	case RT1305_EFUSE_3:
	case RT1305_DC_CALIB_1:
	case RT1305_DC_CALIB_3:
	case RT1305_DAC_OFFSET_1:
	case RT1305_DAC_OFFSET_2:
	case RT1305_DAC_OFFSET_3:
	case RT1305_DAC_OFFSET_4:
	case RT1305_DAC_OFFSET_5:
	case RT1305_DAC_OFFSET_6:
	case RT1305_DAC_OFFSET_7:
	case RT1305_DAC_OFFSET_8:
	case RT1305_DAC_OFFSET_9:
	case RT1305_DAC_OFFSET_10:
	case RT1305_DAC_OFFSET_11:
	case RT1305_TRIM_1:
	case RT1305_TRIM_2:
		return true;

	default:
		return false;
	}
}

static bool rt1305_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt1305_ranges); i++) {
		if (reg >= rt1305_ranges[i].range_min &&
			reg <= rt1305_ranges[i].range_max) {
			return true;
		}
	}

	switch (reg) {
	case RT1305_RESET:
	case RT1305_CLK_1 ... RT1305_CAL_EFUSE_CLOCK:
	case RT1305_PLL0_1 ... RT1305_PLL1_2:
	case RT1305_MIXER_CTRL_1:
	case RT1305_MIXER_CTRL_2:
	case RT1305_DAC_SET_1:
	case RT1305_DAC_SET_2:
	case RT1305_ADC_SET_1:
	case RT1305_ADC_SET_2:
	case RT1305_ADC_SET_3:
	case RT1305_PATH_SET:
	case RT1305_SPDIF_IN_SET_1:
	case RT1305_SPDIF_IN_SET_2:
	case RT1305_SPDIF_IN_SET_3:
	case RT1305_SPDIF_OUT_SET_1:
	case RT1305_SPDIF_OUT_SET_2:
	case RT1305_SPDIF_OUT_SET_3:
	case RT1305_I2S_SET_1:
	case RT1305_I2S_SET_2:
	case RT1305_PBTL_MONO_MODE_SRC:
	case RT1305_MANUALLY_I2C_DEVICE:
	case RT1305_POWER_STATUS:
	case RT1305_POWER_CTRL_1:
	case RT1305_POWER_CTRL_2:
	case RT1305_POWER_CTRL_3:
	case RT1305_POWER_CTRL_4:
	case RT1305_POWER_CTRL_5:
	case RT1305_CLOCK_DETECT:
	case RT1305_BIQUAD_SET_1:
	case RT1305_BIQUAD_SET_2:
	case RT1305_ADJUSTED_HPF_1:
	case RT1305_ADJUSTED_HPF_2:
	case RT1305_EQ_SET_1:
	case RT1305_EQ_SET_2:
	case RT1305_SPK_TEMP_PROTECTION_0:
	case RT1305_SPK_TEMP_PROTECTION_1:
	case RT1305_SPK_TEMP_PROTECTION_2:
	case RT1305_SPK_TEMP_PROTECTION_3:
	case RT1305_SPK_DC_DETECT_1:
	case RT1305_SPK_DC_DETECT_2:
	case RT1305_LOUDNESS:
	case RT1305_THERMAL_FOLD_BACK_1:
	case RT1305_THERMAL_FOLD_BACK_2:
	case RT1305_SILENCE_DETECT ... RT1305_SPK_EXCURSION_LIMITER_7:
	case RT1305_VERSION_ID:
	case RT1305_VENDOR_ID:
	case RT1305_DEVICE_ID:
	case RT1305_EFUSE_1:
	case RT1305_EFUSE_2:
	case RT1305_EFUSE_3:
	case RT1305_DC_CALIB_1:
	case RT1305_DC_CALIB_2:
	case RT1305_DC_CALIB_3:
	case RT1305_DAC_OFFSET_1 ... RT1305_DAC_OFFSET_14:
	case RT1305_TRIM_1:
	case RT1305_TRIM_2:
	case RT1305_TUNE_INTERNAL_OSC:
	case RT1305_BIQUAD1_H0_L_28_16 ... RT1305_BIQUAD3_A2_R_15_0:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9435, 37, 0);

static const char * const rt1305_rx_data_ch_select[] = {
	"LR",
	"RL",
	"Copy L",
	"Copy R",
};

static SOC_ENUM_SINGLE_DECL(rt1305_rx_data_ch_enum, RT1305_I2S_SET_2, 2,
	rt1305_rx_data_ch_select);

static void rt1305_reset(struct regmap *regmap)
{
	regmap_write(regmap, RT1305_RESET, 0);
}

static const struct snd_kcontrol_new rt1305_snd_controls[] = {
	SOC_DOUBLE_TLV("DAC Playback Volume", RT1305_DAC_SET_1,
			8, 0, 0xff, 0, dac_vol_tlv),

	/* I2S Data Channel Selection */
	SOC_ENUM("RX Channel Select", rt1305_rx_data_ch_enum),
};

static int rt1305_is_rc_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(source->dapm);
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);
	unsigned int val;

	val = snd_soc_component_read(component, RT1305_CLK_1);

	if (rt1305->sysclk_src == RT1305_FS_SYS_PRE_S_PLL1 &&
		(val & RT1305_SEL_PLL_SRC_2_RCCLK))
		return 1;
	else
		return 0;
}

static int rt1305_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(source->dapm);
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	if (rt1305->sysclk_src == RT1305_FS_SYS_PRE_S_PLL1)
		return 1;
	else
		return 0;
}

static int rt1305_classd_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT1305_POWER_CTRL_1,
			RT1305_POW_PDB_JD_MASK, RT1305_POW_PDB_JD);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT1305_POWER_CTRL_1,
			RT1305_POW_PDB_JD_MASK, 0);
		usleep_range(150000, 200000);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_kcontrol_new rt1305_sto_dac_l =
	SOC_DAPM_SINGLE("Switch", RT1305_DAC_SET_2,
		RT1305_DVOL_MUTE_L_EN_SFT, 1, 1);

static const struct snd_kcontrol_new rt1305_sto_dac_r =
	SOC_DAPM_SINGLE("Switch", RT1305_DAC_SET_2,
		RT1305_DVOL_MUTE_R_EN_SFT, 1, 1);

static const struct snd_soc_dapm_widget rt1305_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL0", RT1305_POWER_CTRL_1,
		RT1305_POW_PLL0_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT1305_POWER_CTRL_1,
		RT1305_POW_PLL1_EN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MBIAS", RT1305_POWER_CTRL_1,
		RT1305_POW_MBIAS_LV_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG MBIAS", RT1305_POWER_CTRL_1,
		RT1305_POW_BG_MBIAS_LV_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO2", RT1305_POWER_CTRL_1,
		RT1305_POW_LDO2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG2", RT1305_POWER_CTRL_1,
		RT1305_POW_BG2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO2 IB2", RT1305_POWER_CTRL_1,
		RT1305_POW_LDO2_IB2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF", RT1305_POWER_CTRL_1,
		RT1305_POW_VREF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF1", RT1305_POWER_CTRL_1,
		RT1305_POW_VREF1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF2", RT1305_POWER_CTRL_1,
		RT1305_POW_VREF2_BIT, 0, NULL, 0),


	SND_SOC_DAPM_SUPPLY("DISC VREF", RT1305_POWER_CTRL_2,
		RT1305_POW_DISC_VREF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("FASTB VREF", RT1305_POWER_CTRL_2,
		RT1305_POW_FASTB_VREF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ULTRA FAST VREF", RT1305_POWER_CTRL_2,
		RT1305_POW_ULTRA_FAST_VREF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CHOP DAC", RT1305_POWER_CTRL_2,
		RT1305_POW_CKXEN_DAC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CKGEN DAC", RT1305_POWER_CTRL_2,
		RT1305_POW_EN_CKGEN_DAC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLAMP", RT1305_POWER_CTRL_2,
		RT1305_POW_CLAMP_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BUFL", RT1305_POWER_CTRL_2,
		RT1305_POW_BUFL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BUFR", RT1305_POWER_CTRL_2,
		RT1305_POW_BUFR_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CKGEN ADC", RT1305_POWER_CTRL_2,
		RT1305_POW_EN_CKGEN_ADC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC3 L", RT1305_POWER_CTRL_2,
		RT1305_POW_ADC3_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC3 R", RT1305_POWER_CTRL_2,
		RT1305_POW_ADC3_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("TRIOSC", RT1305_POWER_CTRL_2,
		RT1305_POW_TRIOSC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AVDD1", RT1305_POWER_CTRL_2,
		RT1305_POR_AVDD1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("AVDD2", RT1305_POWER_CTRL_2,
		RT1305_POR_AVDD2_BIT, 0, NULL, 0),


	SND_SOC_DAPM_SUPPLY("VSENSE R", RT1305_POWER_CTRL_3,
		RT1305_POW_VSENSE_RCH_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VSENSE L", RT1305_POWER_CTRL_3,
		RT1305_POW_VSENSE_LCH_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ISENSE R", RT1305_POWER_CTRL_3,
		RT1305_POW_ISENSE_RCH_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ISENSE L", RT1305_POWER_CTRL_3,
		RT1305_POW_ISENSE_LCH_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("POR AVDD1", RT1305_POWER_CTRL_3,
		RT1305_POW_POR_AVDD1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("POR AVDD2", RT1305_POWER_CTRL_3,
		RT1305_POW_POR_AVDD2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VCM 6172", RT1305_POWER_CTRL_3,
		RT1305_EN_VCM_6172_BIT, 0, NULL, 0),


	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("DAC L Power", RT1305_POWER_CTRL_2,
		RT1305_POW_DAC1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R Power", RT1305_POWER_CTRL_2,
		RT1305_POW_DAC1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SWITCH("DAC L", SND_SOC_NOPM, 0, 0, &rt1305_sto_dac_l),
	SND_SOC_DAPM_SWITCH("DAC R", SND_SOC_NOPM, 0, 0, &rt1305_sto_dac_r),

	/* Output Lines */
	SND_SOC_DAPM_PGA_E("CLASS D", SND_SOC_NOPM, 0, 0, NULL, 0,
		rt1305_classd_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
};

static const struct snd_soc_dapm_route rt1305_dapm_routes[] = {

	{ "DAC", NULL, "AIF1RX" },

	{ "DAC", NULL, "PLL0", rt1305_is_rc_clk_from_pll },
	{ "DAC", NULL, "PLL1", rt1305_is_sys_clk_from_pll },

	{ "DAC", NULL, "MBIAS" },
	{ "DAC", NULL, "BG MBIAS" },
	{ "DAC", NULL, "LDO2" },
	{ "DAC", NULL, "BG2" },
	{ "DAC", NULL, "LDO2 IB2" },
	{ "DAC", NULL, "VREF" },
	{ "DAC", NULL, "VREF1" },
	{ "DAC", NULL, "VREF2" },

	{ "DAC", NULL, "DISC VREF" },
	{ "DAC", NULL, "FASTB VREF" },
	{ "DAC", NULL, "ULTRA FAST VREF" },
	{ "DAC", NULL, "CHOP DAC" },
	{ "DAC", NULL, "CKGEN DAC" },
	{ "DAC", NULL, "CLAMP" },
	{ "DAC", NULL, "CKGEN ADC" },
	{ "DAC", NULL, "TRIOSC" },
	{ "DAC", NULL, "AVDD1" },
	{ "DAC", NULL, "AVDD2" },

	{ "DAC", NULL, "POR AVDD1" },
	{ "DAC", NULL, "POR AVDD2" },
	{ "DAC", NULL, "VCM 6172" },

	{ "DAC L", "Switch", "DAC" },
	{ "DAC R", "Switch", "DAC" },

	{ "DAC R", NULL, "VSENSE R" },
	{ "DAC L", NULL, "VSENSE L" },
	{ "DAC R", NULL, "ISENSE R" },
	{ "DAC L", NULL, "ISENSE L" },
	{ "DAC L", NULL, "ADC3 L" },
	{ "DAC R", NULL, "ADC3 R" },
	{ "DAC L", NULL, "BUFL" },
	{ "DAC R", NULL, "BUFR" },
	{ "DAC L", NULL, "DAC L Power" },
	{ "DAC R", NULL, "DAC R Power" },

	{ "CLASS D", NULL, "DAC L" },
	{ "CLASS D", NULL, "DAC R" },

	{ "SPOL", NULL, "CLASS D" },
	{ "SPOR", NULL, "CLASS D" },
};

static int rt1305_get_clk_info(int sclk, int rate)
{
	int i;
	static const int pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt1305_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt1305->lrck = params_rate(params);
	pre_div = rt1305_get_clk_info(rt1305->sysclk, rt1305->lrck);
	if (pre_div < 0) {
		dev_warn(component->dev, "Force using PLL ");
		snd_soc_dai_set_pll(dai, 0, RT1305_PLL1_S_BCLK,
			rt1305->lrck * 64, rt1305->lrck * 256);
		snd_soc_dai_set_sysclk(dai, RT1305_FS_SYS_PRE_S_PLL1,
			rt1305->lrck * 256, SND_SOC_CLOCK_IN);
		pre_div = 0;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	bclk_ms = frame_size > 32;
	rt1305->bclk = rt1305->lrck * (32 << bclk_ms);

	dev_dbg(component->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt1305->lrck, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		val_len |= RT1305_I2S_DL_SEL_16B;
		break;
	case 20:
		val_len |= RT1305_I2S_DL_SEL_20B;
		break;
	case 24:
		val_len |= RT1305_I2S_DL_SEL_24B;
		break;
	case 8:
		val_len |= RT1305_I2S_DL_SEL_8B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT1305_AIF1:
		mask_clk = RT1305_DIV_FS_SYS_MASK;
		val_clk = pre_div << RT1305_DIV_FS_SYS_SFT;
		snd_soc_component_update_bits(component, RT1305_I2S_SET_2,
			RT1305_I2S_DL_SEL_MASK,
			val_len);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT1305_CLK_2,
		mask_clk, val_clk);

	return 0;
}

static int rt1305_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0, reg1_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		reg_val |= RT1305_SEL_I2S_OUT_MODE_M;
		rt1305->master = 1;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		reg_val |= RT1305_SEL_I2S_OUT_MODE_S;
		rt1305->master = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg1_val |= RT1305_I2S_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg1_val |= RT1305_I2S_DF_SEL_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg1_val |= RT1305_I2S_DF_SEL_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg1_val |= RT1305_I2S_DF_SEL_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT1305_AIF1:
		snd_soc_component_update_bits(component, RT1305_I2S_SET_1,
			RT1305_SEL_I2S_OUT_MODE_MASK, reg_val);
		snd_soc_component_update_bits(component, RT1305_I2S_SET_2,
			RT1305_I2S_DF_SEL_MASK | RT1305_I2S_BCLK_MASK,
			reg1_val);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt1305_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt1305->sysclk && clk_id == rt1305->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT1305_FS_SYS_PRE_S_MCLK:
		reg_val |= RT1305_SEL_FS_SYS_PRE_MCLK;
		snd_soc_component_update_bits(component,
			RT1305_CLOCK_DETECT, RT1305_SEL_CLK_DET_SRC_MASK,
			RT1305_SEL_CLK_DET_SRC_MCLK);
		break;
	case RT1305_FS_SYS_PRE_S_PLL1:
		reg_val |= RT1305_SEL_FS_SYS_PRE_PLL;
		break;
	case RT1305_FS_SYS_PRE_S_RCCLK:
		reg_val |= RT1305_SEL_FS_SYS_PRE_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT1305_CLK_1,
		RT1305_SEL_FS_SYS_PRE_MASK, reg_val);
	rt1305->sysclk = freq;
	rt1305->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt1305_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt1305->pll_src && freq_in == rt1305->pll_in &&
	    freq_out == rt1305->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt1305->pll_in = 0;
		rt1305->pll_out = 0;
		snd_soc_component_update_bits(component, RT1305_CLK_1,
			RT1305_SEL_FS_SYS_PRE_MASK | RT1305_SEL_PLL_SRC_1_MASK,
			RT1305_SEL_FS_SYS_PRE_PLL | RT1305_SEL_PLL_SRC_1_BCLK);
		return 0;
	}

	switch (source) {
	case RT1305_PLL2_S_MCLK:
		snd_soc_component_update_bits(component, RT1305_CLK_1,
			RT1305_SEL_PLL_SRC_2_MASK | RT1305_SEL_PLL_SRC_1_MASK |
			RT1305_DIV_PLL_SRC_2_MASK,
			RT1305_SEL_PLL_SRC_2_MCLK | RT1305_SEL_PLL_SRC_1_PLL2);
		snd_soc_component_update_bits(component,
			RT1305_CLOCK_DETECT, RT1305_SEL_CLK_DET_SRC_MASK,
			RT1305_SEL_CLK_DET_SRC_MCLK);
		break;
	case RT1305_PLL1_S_BCLK:
		snd_soc_component_update_bits(component,
			RT1305_CLK_1, RT1305_SEL_PLL_SRC_1_MASK,
			RT1305_SEL_PLL_SRC_1_BCLK);
		break;
	case RT1305_PLL2_S_RCCLK:
		snd_soc_component_update_bits(component, RT1305_CLK_1,
			RT1305_SEL_PLL_SRC_2_MASK | RT1305_SEL_PLL_SRC_1_MASK |
			RT1305_DIV_PLL_SRC_2_MASK,
			RT1305_SEL_PLL_SRC_2_RCCLK | RT1305_SEL_PLL_SRC_1_PLL2);
		freq_in = 98304000;
		break;
	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupported input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT1305_PLL1_1,
		((pll_code.m_bp ? 0 : pll_code.m_code) << RT1305_PLL_1_M_SFT) |
		(pll_code.m_bp << RT1305_PLL_1_M_BYPASS_SFT) |
		pll_code.n_code);
	snd_soc_component_write(component, RT1305_PLL1_2,
		pll_code.k_code);

	rt1305->pll_in = freq_in;
	rt1305->pll_out = freq_out;
	rt1305->pll_src = source;

	return 0;
}

static int rt1305_probe(struct snd_soc_component *component)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	rt1305->component = component;

	/* initial settings */
	rt1305_reg_init(component);

	return 0;
}

static void rt1305_remove(struct snd_soc_component *component)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	rt1305_reset(rt1305->regmap);
}

#ifdef CONFIG_PM
static int rt1305_suspend(struct snd_soc_component *component)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1305->regmap, true);
	regcache_mark_dirty(rt1305->regmap);

	return 0;
}

static int rt1305_resume(struct snd_soc_component *component)
{
	struct rt1305_priv *rt1305 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1305->regmap, false);
	regcache_sync(rt1305->regmap);

	return 0;
}
#else
#define rt1305_suspend NULL
#define rt1305_resume NULL
#endif

#define RT1305_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT1305_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops rt1305_aif_dai_ops = {
	.hw_params = rt1305_hw_params,
	.set_fmt = rt1305_set_dai_fmt,
};

static struct snd_soc_dai_driver rt1305_dai[] = {
	{
		.name = "rt1305-aif",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1305_STEREO_RATES,
			.formats = RT1305_FORMATS,
		},
		.ops = &rt1305_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt1305 = {
	.probe = rt1305_probe,
	.remove = rt1305_remove,
	.suspend = rt1305_suspend,
	.resume = rt1305_resume,
	.controls = rt1305_snd_controls,
	.num_controls = ARRAY_SIZE(rt1305_snd_controls),
	.dapm_widgets = rt1305_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1305_dapm_widgets),
	.dapm_routes = rt1305_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1305_dapm_routes),
	.set_sysclk = rt1305_set_component_sysclk,
	.set_pll = rt1305_set_component_pll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config rt1305_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = RT1305_MAX_REG + 1 + (ARRAY_SIZE(rt1305_ranges) *
					       RT1305_PR_SPACING),
	.volatile_reg = rt1305_volatile_register,
	.readable_reg = rt1305_readable_register,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = rt1305_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1305_reg),
	.ranges = rt1305_ranges,
	.num_ranges = ARRAY_SIZE(rt1305_ranges),
	.use_single_read = true,
	.use_single_write = true,
};

#if defined(CONFIG_OF)
static const struct of_device_id rt1305_of_match[] = {
	{ .compatible = "realtek,rt1305", },
	{ .compatible = "realtek,rt1306", },
	{ }
};
MODULE_DEVICE_TABLE(of, rt1305_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt1305_acpi_match[] = {
	{ "10EC1305" },
	{ "10EC1306" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, rt1305_acpi_match);
#endif

static const struct i2c_device_id rt1305_i2c_id[] = {
	{ "rt1305" },
	{ "rt1306" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1305_i2c_id);

static void rt1305_calibrate(struct rt1305_priv *rt1305)
{
	unsigned int valmsb, vallsb, offsetl, offsetr;
	unsigned int rh, rl, rhl, r0ohm;
	u64 r0l, r0r;

	regcache_cache_bypass(rt1305->regmap, true);

	rt1305_reset(rt1305->regmap);
	regmap_write(rt1305->regmap, RT1305_ADC_SET_3, 0x0219);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xcf, 0x5548);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xc1, 0x0320);
	regmap_write(rt1305->regmap, RT1305_CLOCK_DETECT, 0x1000);
	regmap_write(rt1305->regmap, RT1305_CLK_1, 0x0600);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xffd0);
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0080);
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0880);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_1, 0x0dfe);

	/* Sin Gen */
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x5d, 0x0442);

	regmap_write(rt1305->regmap, RT1305_CAL_EFUSE_CLOCK, 0xb000);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xc3, 0xd4a0);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xcc, 0x00cc);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xc1, 0x0320);
	regmap_write(rt1305->regmap, RT1305_POWER_STATUS, 0x0000);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_2, 0xffff);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfc20);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x06, 0x00c0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfca0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfce0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfcf0);

	/* EFUSE read */
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0080);
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0880);
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0880);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfce0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfca0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfc20);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x06, 0x0000);
	regmap_write(rt1305->regmap, RT1305_EFUSE_1, 0x0000);

	regmap_read(rt1305->regmap, RT1305_DAC_OFFSET_5, &valmsb);
	regmap_read(rt1305->regmap, RT1305_DAC_OFFSET_6, &vallsb);
	offsetl = valmsb << 16 | vallsb;
	regmap_read(rt1305->regmap, RT1305_DAC_OFFSET_7, &valmsb);
	regmap_read(rt1305->regmap, RT1305_DAC_OFFSET_8, &vallsb);
	offsetr = valmsb << 16 | vallsb;
	pr_info("DC offsetl=0x%x, offsetr=0x%x\n", offsetl, offsetr);

	/* R0 calibration */
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x5d, 0x9542);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0xfcf0);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_2, 0xffff);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_1, 0x1dfe);
	regmap_write(rt1305->regmap, RT1305_SILENCE_DETECT, 0x0e13);
	regmap_write(rt1305->regmap, RT1305_CLK_1, 0x0650);

	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x50, 0x0064);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x51, 0x0770);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x52, 0xc30c);
	regmap_write(rt1305->regmap, RT1305_SPK_TEMP_PROTECTION_1, 0x8200);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xd4, 0xfb00);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xd4, 0xff80);
	msleep(2000);
	regmap_read(rt1305->regmap, RT1305_PR_BASE + 0x55, &rh);
	regmap_read(rt1305->regmap, RT1305_PR_BASE + 0x56, &rl);
	rhl = (rh << 16) | rl;
	r0ohm = (rhl*10) / 33554432;

	pr_debug("Left_rhl = 0x%x rh=0x%x rl=0x%x\n", rhl, rh, rl);
	pr_info("Left channel %d.%dohm\n", (r0ohm/10), (r0ohm%10));

	r0l = 562949953421312ULL;
	if (rhl != 0)
		do_div(r0l, rhl);
	pr_debug("Left_r0 = 0x%llx\n", r0l);

	regmap_write(rt1305->regmap, RT1305_SPK_TEMP_PROTECTION_1, 0x9200);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xd4, 0xfb00);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xd4, 0xff80);
	msleep(2000);
	regmap_read(rt1305->regmap, RT1305_PR_BASE + 0x55, &rh);
	regmap_read(rt1305->regmap, RT1305_PR_BASE + 0x56, &rl);
	rhl = (rh << 16) | rl;
	r0ohm = (rhl*10) / 33554432;

	pr_debug("Right_rhl = 0x%x rh=0x%x rl=0x%x\n", rhl, rh, rl);
	pr_info("Right channel %d.%dohm\n", (r0ohm/10), (r0ohm%10));

	r0r = 562949953421312ULL;
	if (rhl != 0)
		do_div(r0r, rhl);
	pr_debug("Right_r0 = 0x%llx\n", r0r);

	regmap_write(rt1305->regmap, RT1305_SPK_TEMP_PROTECTION_1, 0xc2ec);

	if ((r0l > R0_UPPER) && (r0l < R0_LOWER) &&
		(r0r > R0_UPPER) && (r0r < R0_LOWER)) {
		regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x4e,
			(r0l >> 16) & 0xffff);
		regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x4f,
			r0l & 0xffff);
		regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xfe,
			((r0r >> 16) & 0xffff) | 0xf800);
		regmap_write(rt1305->regmap, RT1305_PR_BASE + 0xfd,
			r0r & 0xffff);
	} else {
		pr_err("R0 calibration failed\n");
	}

	/* restore some registers */
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_1, 0x0dfe);
	usleep_range(200000, 400000);
	regmap_write(rt1305->regmap, RT1305_PR_BASE + 0x5d, 0x0442);
	regmap_write(rt1305->regmap, RT1305_CLOCK_DETECT, 0x3000);
	regmap_write(rt1305->regmap, RT1305_CLK_1, 0x0400);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_1, 0x0000);
	regmap_write(rt1305->regmap, RT1305_CAL_EFUSE_CLOCK, 0x8000);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_2, 0x1020);
	regmap_write(rt1305->regmap, RT1305_POWER_CTRL_3, 0x0000);

	regcache_cache_bypass(rt1305->regmap, false);
}

static int rt1305_i2c_probe(struct i2c_client *i2c)
{
	struct rt1305_priv *rt1305;
	int ret;
	unsigned int val;

	rt1305 = devm_kzalloc(&i2c->dev, sizeof(struct rt1305_priv),
				GFP_KERNEL);
	if (rt1305 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1305);

	rt1305->regmap = devm_regmap_init_i2c(i2c, &rt1305_regmap);
	if (IS_ERR(rt1305->regmap)) {
		ret = PTR_ERR(rt1305->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt1305->regmap, RT1305_DEVICE_ID, &val);
	if (val != RT1305_DEVICE_ID_NUM) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt1305\n", val);
		return -ENODEV;
	}

	rt1305_reset(rt1305->regmap);
	rt1305_calibrate(rt1305);

	return devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_rt1305,
			rt1305_dai, ARRAY_SIZE(rt1305_dai));
}

static void rt1305_i2c_shutdown(struct i2c_client *client)
{
	struct rt1305_priv *rt1305 = i2c_get_clientdata(client);

	rt1305_reset(rt1305->regmap);
}


static struct i2c_driver rt1305_i2c_driver = {
	.driver = {
		.name = "rt1305",
#if defined(CONFIG_OF)
		.of_match_table = rt1305_of_match,
#endif
#if defined(CONFIG_ACPI)
		.acpi_match_table = ACPI_PTR(rt1305_acpi_match)
#endif
	},
	.probe = rt1305_i2c_probe,
	.shutdown = rt1305_i2c_shutdown,
	.id_table = rt1305_i2c_id,
};
module_i2c_driver(rt1305_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1305 amplifier driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL v2");
