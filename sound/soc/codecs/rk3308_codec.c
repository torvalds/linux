// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RK3308 internal audio codec driver
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 * Copyright (c) 2024, Vivax-Metrotech Ltd
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/util_macros.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rk3308_codec.h"

#define ADC_LR_GROUP_MAX		4

#define GRF_CHIP_ID			0x800

enum {
	ACODEC_VERSION_A = 'A',
	ACODEC_VERSION_B,
	ACODEC_VERSION_C,
};

struct rk3308_codec_priv {
	const struct device *dev;
	struct regmap *regmap;
	struct regmap *grf;
	struct reset_control *reset;
	struct clk *hclk;
	struct clk *mclk_rx;
	struct clk *mclk_tx;
	struct snd_soc_component *component;
	unsigned char codec_ver;
};

static struct clk_bulk_data rk3308_codec_clocks[] = {
	{ .id = "hclk" },
	{ .id = "mclk_rx" },
	{ .id = "mclk_tx" },
};

static const DECLARE_TLV_DB_SCALE(rk3308_codec_adc_alc_gain_tlv,   -1800, 150, 0);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpout_gain_tlv, -3900, 150, 0);
static const DECLARE_TLV_DB_SCALE(rk3308_codec_dac_hpmix_gain_tlv,  -600, 600, 0);

static const DECLARE_TLV_DB_RANGE(rk3308_codec_dac_lineout_gain_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-600, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-300, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-150, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(0,    0, 0),
);

static const char * const rk3308_codec_hpf_cutoff_text[] = {
	"20 Hz", "245 Hz", "612 Hz"
};

static SOC_ENUM_SINGLE_DECL(rk3308_codec_hpf_cutoff_enum12, RK3308_ADC_DIG_CON04(0), 0,
			    rk3308_codec_hpf_cutoff_text);
static SOC_ENUM_SINGLE_DECL(rk3308_codec_hpf_cutoff_enum34, RK3308_ADC_DIG_CON04(1), 0,
			    rk3308_codec_hpf_cutoff_text);
static SOC_ENUM_SINGLE_DECL(rk3308_codec_hpf_cutoff_enum56, RK3308_ADC_DIG_CON04(2), 0,
			    rk3308_codec_hpf_cutoff_text);
static SOC_ENUM_SINGLE_DECL(rk3308_codec_hpf_cutoff_enum78, RK3308_ADC_DIG_CON04(3), 0,
			    rk3308_codec_hpf_cutoff_text);

static const struct snd_kcontrol_new rk3308_codec_controls[] = {
	/* Despite the register names, these set the gain when AGC is OFF */
	SOC_SINGLE_RANGE_TLV("MIC1 Capture Volume",
			     RK3308_ADC_ANA_CON03(0),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC2 Capture Volume",
			     RK3308_ADC_ANA_CON04(0),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC3 Capture Volume",
			     RK3308_ADC_ANA_CON03(1),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC4 Capture Volume",
			     RK3308_ADC_ANA_CON04(1),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC5 Capture Volume",
			     RK3308_ADC_ANA_CON03(2),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC6 Capture Volume",
			     RK3308_ADC_ANA_CON04(2),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC7 Capture Volume",
			     RK3308_ADC_ANA_CON03(3),
			     RK3308_ADC_CH1_ALC_GAIN_SFT,
			     RK3308_ADC_CH1_ALC_GAIN_MIN,
			     RK3308_ADC_CH1_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),
	SOC_SINGLE_RANGE_TLV("MIC8 Capture Volume",
			     RK3308_ADC_ANA_CON04(3),
			     RK3308_ADC_CH2_ALC_GAIN_SFT,
			     RK3308_ADC_CH2_ALC_GAIN_MIN,
			     RK3308_ADC_CH2_ALC_GAIN_MAX,
			     0, rk3308_codec_adc_alc_gain_tlv),

	SOC_SINGLE("MIC1 Capture Switch", RK3308_ADC_ANA_CON00(0), 3, 1, 0),
	SOC_SINGLE("MIC2 Capture Switch", RK3308_ADC_ANA_CON00(0), 7, 1, 0),
	SOC_SINGLE("MIC3 Capture Switch", RK3308_ADC_ANA_CON00(1), 3, 1, 0),
	SOC_SINGLE("MIC4 Capture Switch", RK3308_ADC_ANA_CON00(1), 7, 1, 0),
	SOC_SINGLE("MIC5 Capture Switch", RK3308_ADC_ANA_CON00(2), 3, 1, 0),
	SOC_SINGLE("MIC6 Capture Switch", RK3308_ADC_ANA_CON00(2), 7, 1, 0),
	SOC_SINGLE("MIC7 Capture Switch", RK3308_ADC_ANA_CON00(3), 3, 1, 0),
	SOC_SINGLE("MIC8 Capture Switch", RK3308_ADC_ANA_CON00(3), 7, 1, 0),

	SOC_SINGLE("MIC12 HPF Capture Switch", RK3308_ADC_DIG_CON04(0), 2, 1, 1),
	SOC_SINGLE("MIC34 HPF Capture Switch", RK3308_ADC_DIG_CON04(1), 2, 1, 1),
	SOC_SINGLE("MIC56 HPF Capture Switch", RK3308_ADC_DIG_CON04(2), 2, 1, 1),
	SOC_SINGLE("MIC78 HPF Capture Switch", RK3308_ADC_DIG_CON04(3), 2, 1, 1),

	SOC_ENUM("MIC12 HPF Cutoff", rk3308_codec_hpf_cutoff_enum12),
	SOC_ENUM("MIC34 HPF Cutoff", rk3308_codec_hpf_cutoff_enum34),
	SOC_ENUM("MIC56 HPF Cutoff", rk3308_codec_hpf_cutoff_enum56),
	SOC_ENUM("MIC78 HPF Cutoff", rk3308_codec_hpf_cutoff_enum78),

	SOC_DOUBLE_TLV("Line Out Playback Volume",
		       RK3308_DAC_ANA_CON04,
		       RK3308_DAC_L_LINEOUT_GAIN_SFT,
		       RK3308_DAC_R_LINEOUT_GAIN_SFT,
		       RK3308_DAC_x_LINEOUT_GAIN_MAX,
		       0, rk3308_codec_dac_lineout_gain_tlv),
	SOC_DOUBLE("Line Out Playback Switch",
		   RK3308_DAC_ANA_CON04,
		   RK3308_DAC_L_LINEOUT_MUTE_SFT,
		   RK3308_DAC_R_LINEOUT_MUTE_SFT, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Playback Volume",
			 RK3308_DAC_ANA_CON05,
			 RK3308_DAC_ANA_CON06,
			 RK3308_DAC_x_HPOUT_GAIN_SFT,
			 RK3308_DAC_x_HPOUT_GAIN_MAX,
			 0, rk3308_codec_dac_hpout_gain_tlv),
	SOC_DOUBLE("Headphone Playback Switch",
		   RK3308_DAC_ANA_CON03,
		   RK3308_DAC_L_HPOUT_MUTE_SFT,
		   RK3308_DAC_R_HPOUT_MUTE_SFT, 1, 0),
	SOC_DOUBLE_RANGE_TLV("DAC HPMIX Playback Volume",
			     RK3308_DAC_ANA_CON12,
			     RK3308_DAC_L_HPMIX_GAIN_SFT,
			     RK3308_DAC_R_HPMIX_GAIN_SFT,
			     1, 2, 0, rk3308_codec_dac_hpmix_gain_tlv),
};

static int rk3308_codec_pop_sound_set(struct snd_soc_dapm_widget *w,
				      struct snd_kcontrol *kcontrol,
				      int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);
	unsigned int val = (event == SND_SOC_DAPM_POST_PMU) ?
		RK3308_DAC_HPOUT_POP_SOUND_x_WORK :
		RK3308_DAC_HPOUT_POP_SOUND_x_INIT;
	unsigned int mask = RK3308_DAC_HPOUT_POP_SOUND_x_MSK;

	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON01,
			   mask << w->shift, val << w->shift);

	return 0;
}

static const struct snd_soc_dapm_widget rk3308_codec_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),
	SND_SOC_DAPM_INPUT("MIC4"),
	SND_SOC_DAPM_INPUT("MIC5"),
	SND_SOC_DAPM_INPUT("MIC6"),
	SND_SOC_DAPM_INPUT("MIC7"),
	SND_SOC_DAPM_INPUT("MIC8"),

	SND_SOC_DAPM_SUPPLY("ADC_CURRENT_EN12", RK3308_ADC_ANA_CON06(0), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_CURRENT_EN34", RK3308_ADC_ANA_CON06(1), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_CURRENT_EN56", RK3308_ADC_ANA_CON06(2), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_CURRENT_EN78", RK3308_ADC_ANA_CON06(3), 0, 0, NULL, 0),

	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC1_EN", RK3308_ADC_ANA_CON00(0), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC2_EN", RK3308_ADC_ANA_CON00(0), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC3_EN", RK3308_ADC_ANA_CON00(1), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC4_EN", RK3308_ADC_ANA_CON00(1), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC5_EN", RK3308_ADC_ANA_CON00(2), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC6_EN", RK3308_ADC_ANA_CON00(2), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC7_EN", RK3308_ADC_ANA_CON00(3), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC8_EN", RK3308_ADC_ANA_CON00(3), 5, 1, 1, 0),

	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC1_WORK", RK3308_ADC_ANA_CON00(0), 2, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC2_WORK", RK3308_ADC_ANA_CON00(0), 6, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC3_WORK", RK3308_ADC_ANA_CON00(1), 2, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC4_WORK", RK3308_ADC_ANA_CON00(1), 6, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC5_WORK", RK3308_ADC_ANA_CON00(2), 2, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC6_WORK", RK3308_ADC_ANA_CON00(2), 6, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC7_WORK", RK3308_ADC_ANA_CON00(3), 2, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_mic, "MIC8_WORK", RK3308_ADC_ANA_CON00(3), 6, 1, 1, 0),

	/*
	 * In theory MIC1 and MIC2 can switch to LINE IN, but this is not
	 * supported so all we can do is enabling the MIC input.
	 */
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "CH1_IN_SEL", RK3308_ADC_ANA_CON07(0), 4, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "CH2_IN_SEL", RK3308_ADC_ANA_CON07(0), 6, 1, 1, 0),

	SND_SOC_DAPM_SUPPLY("ADC1_BUF_REF_EN", RK3308_ADC_ANA_CON00(0), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2_BUF_REF_EN", RK3308_ADC_ANA_CON00(0), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC3_BUF_REF_EN", RK3308_ADC_ANA_CON00(1), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC4_BUF_REF_EN", RK3308_ADC_ANA_CON00(1), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC5_BUF_REF_EN", RK3308_ADC_ANA_CON00(2), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC6_BUF_REF_EN", RK3308_ADC_ANA_CON00(2), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC7_BUF_REF_EN", RK3308_ADC_ANA_CON00(3), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC8_BUF_REF_EN", RK3308_ADC_ANA_CON00(3), 4, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC_MCLK_GATE", RK3308_GLB_CON, 5, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC1_CLK_EN", RK3308_ADC_ANA_CON05(0), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2_CLK_EN", RK3308_ADC_ANA_CON05(0), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC3_CLK_EN", RK3308_ADC_ANA_CON05(1), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC4_CLK_EN", RK3308_ADC_ANA_CON05(1), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC5_CLK_EN", RK3308_ADC_ANA_CON05(2), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC6_CLK_EN", RK3308_ADC_ANA_CON05(2), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC7_CLK_EN", RK3308_ADC_ANA_CON05(3), 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC8_CLK_EN", RK3308_ADC_ANA_CON05(3), 4, 0, NULL, 0),

	/* The "ALC" name from the TRM is misleading, these are needed even without ALC/AGC */
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC1_EN", RK3308_ADC_ANA_CON02(0), 0, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC2_EN", RK3308_ADC_ANA_CON02(0), 4, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC3_EN", RK3308_ADC_ANA_CON02(1), 0, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC4_EN", RK3308_ADC_ANA_CON02(1), 4, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC5_EN", RK3308_ADC_ANA_CON02(2), 0, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC6_EN", RK3308_ADC_ANA_CON02(2), 4, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC7_EN", RK3308_ADC_ANA_CON02(3), 0, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC8_EN", RK3308_ADC_ANA_CON02(3), 4, 1, 1, 0),

	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC1_EN", RK3308_ADC_ANA_CON05(0), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC2_EN", RK3308_ADC_ANA_CON05(0), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC3_EN", RK3308_ADC_ANA_CON05(1), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC4_EN", RK3308_ADC_ANA_CON05(1), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC5_EN", RK3308_ADC_ANA_CON05(2), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC6_EN", RK3308_ADC_ANA_CON05(2), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC7_EN", RK3308_ADC_ANA_CON05(3), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ADC8_EN", RK3308_ADC_ANA_CON05(3), 5, 1, 1, 0),

	SND_SOC_DAPM_ADC("ADC1_WORK", "Capture", RK3308_ADC_ANA_CON05(0), 2, 0),
	SND_SOC_DAPM_ADC("ADC2_WORK", "Capture", RK3308_ADC_ANA_CON05(0), 6, 0),
	SND_SOC_DAPM_ADC("ADC3_WORK", "Capture", RK3308_ADC_ANA_CON05(1), 2, 0),
	SND_SOC_DAPM_ADC("ADC4_WORK", "Capture", RK3308_ADC_ANA_CON05(1), 6, 0),
	SND_SOC_DAPM_ADC("ADC5_WORK", "Capture", RK3308_ADC_ANA_CON05(2), 2, 0),
	SND_SOC_DAPM_ADC("ADC6_WORK", "Capture", RK3308_ADC_ANA_CON05(2), 6, 0),
	SND_SOC_DAPM_ADC("ADC7_WORK", "Capture", RK3308_ADC_ANA_CON05(3), 2, 0),
	SND_SOC_DAPM_ADC("ADC8_WORK", "Capture", RK3308_ADC_ANA_CON05(3), 6, 0),

	/* The "ALC" name from the TRM is misleading, these are needed even without ALC/AGC */
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC1_WORK", RK3308_ADC_ANA_CON02(0), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC2_WORK", RK3308_ADC_ANA_CON02(0), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC3_WORK", RK3308_ADC_ANA_CON02(1), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC4_WORK", RK3308_ADC_ANA_CON02(1), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC5_WORK", RK3308_ADC_ANA_CON02(2), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC6_WORK", RK3308_ADC_ANA_CON02(2), 5, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC7_WORK", RK3308_ADC_ANA_CON02(3), 1, 1, 1, 0),
	SND_SOC_DAPM_REG(snd_soc_dapm_adc, "ALC8_WORK", RK3308_ADC_ANA_CON02(3), 5, 1, 1, 0),

	SND_SOC_DAPM_SUPPLY("MICBIAS Current", RK3308_ADC_ANA_CON08(0), 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RK3308_ADC_ANA_CON07(1), 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RK3308_ADC_ANA_CON07(2), 3, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("DAC_L_HPMIX_EN",   RK3308_DAC_ANA_CON13, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("DAC_R_HPMIX_EN",   RK3308_DAC_ANA_CON13, 4, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("DAC_L_HPMIX_WORK", RK3308_DAC_ANA_CON13, 1, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("DAC_R_HPMIX_WORK", RK3308_DAC_ANA_CON13, 5, 0, NULL, 0),
	/* HPMIX is not actually acting as a mixer as the only supported input is I2S */
	SND_SOC_DAPM_OUT_DRV("DAC_L_HPMIX_SEL",  RK3308_DAC_ANA_CON12, 2, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("DAC_R_HPMIX_SEL",  RK3308_DAC_ANA_CON12, 6, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DAC HPMIX Left",     RK3308_DAC_ANA_CON13, 2, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DAC HPMIX Right",    RK3308_DAC_ANA_CON13, 6, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC_MCLK_GATE", RK3308_GLB_CON, 4, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC_CURRENT_EN", RK3308_DAC_ANA_CON00, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_L_REF_EN",   RK3308_DAC_ANA_CON02, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_R_REF_EN",   RK3308_DAC_ANA_CON02, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_L_CLK_EN",   RK3308_DAC_ANA_CON02, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_R_CLK_EN",   RK3308_DAC_ANA_CON02, 5, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC_L_DAC_WORK", NULL, RK3308_DAC_ANA_CON02, 3, 0),
	SND_SOC_DAPM_DAC("DAC_R_DAC_WORK", NULL, RK3308_DAC_ANA_CON02, 7, 0),

	SND_SOC_DAPM_SUPPLY("DAC_BUF_REF_L", RK3308_DAC_ANA_CON01, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_BUF_REF_R", RK3308_DAC_ANA_CON01, 6, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV_E("HPOUT_POP_SOUND_L", SND_SOC_NOPM, 0, 0, NULL, 0,
			       rk3308_codec_pop_sound_set,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV_E("HPOUT_POP_SOUND_R", SND_SOC_NOPM, 4, 0, NULL, 0,
			       rk3308_codec_pop_sound_set,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUT_DRV("L_HPOUT_EN", RK3308_DAC_ANA_CON03, 1, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("R_HPOUT_EN", RK3308_DAC_ANA_CON03, 5, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("L_HPOUT_WORK", RK3308_DAC_ANA_CON03, 2, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("R_HPOUT_WORK", RK3308_DAC_ANA_CON03, 6, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("HPOUT_L"),
	SND_SOC_DAPM_OUTPUT("HPOUT_R"),

	SND_SOC_DAPM_OUT_DRV("L_LINEOUT_EN", RK3308_DAC_ANA_CON04, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("R_LINEOUT_EN", RK3308_DAC_ANA_CON04, 4, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("LINEOUT_L"),
	SND_SOC_DAPM_OUTPUT("LINEOUT_R"),
};

static const struct snd_soc_dapm_route rk3308_codec_dapm_routes[] = {
	{ "MICBIAS1", NULL, "MICBIAS Current" },
	{ "MICBIAS2", NULL, "MICBIAS Current" },

	{ "MIC1_EN", NULL, "MIC1" },
	{ "MIC2_EN", NULL, "MIC2" },
	{ "MIC3_EN", NULL, "MIC3" },
	{ "MIC4_EN", NULL, "MIC4" },
	{ "MIC5_EN", NULL, "MIC5" },
	{ "MIC6_EN", NULL, "MIC6" },
	{ "MIC7_EN", NULL, "MIC7" },
	{ "MIC8_EN", NULL, "MIC8" },

	{ "MIC1_WORK", NULL, "MIC1_EN" },
	{ "MIC2_WORK", NULL, "MIC2_EN" },
	{ "MIC3_WORK", NULL, "MIC3_EN" },
	{ "MIC4_WORK", NULL, "MIC4_EN" },
	{ "MIC5_WORK", NULL, "MIC5_EN" },
	{ "MIC6_WORK", NULL, "MIC6_EN" },
	{ "MIC7_WORK", NULL, "MIC7_EN" },
	{ "MIC8_WORK", NULL, "MIC8_EN" },

	{ "CH1_IN_SEL", NULL, "MIC1_WORK" },
	{ "CH2_IN_SEL", NULL, "MIC2_WORK" },

	{ "ALC1_EN", NULL, "CH1_IN_SEL" },
	{ "ALC2_EN", NULL, "CH2_IN_SEL" },
	{ "ALC3_EN", NULL, "MIC3_WORK" },
	{ "ALC4_EN", NULL, "MIC4_WORK" },
	{ "ALC5_EN", NULL, "MIC5_WORK" },
	{ "ALC6_EN", NULL, "MIC6_WORK" },
	{ "ALC7_EN", NULL, "MIC7_WORK" },
	{ "ALC8_EN", NULL, "MIC8_WORK" },

	{ "ADC1_EN", NULL, "ALC1_EN" },
	{ "ADC2_EN", NULL, "ALC2_EN" },
	{ "ADC3_EN", NULL, "ALC3_EN" },
	{ "ADC4_EN", NULL, "ALC4_EN" },
	{ "ADC5_EN", NULL, "ALC5_EN" },
	{ "ADC6_EN", NULL, "ALC6_EN" },
	{ "ADC7_EN", NULL, "ALC7_EN" },
	{ "ADC8_EN", NULL, "ALC8_EN" },

	{ "ADC1_WORK", NULL, "ADC1_EN" },
	{ "ADC2_WORK", NULL, "ADC2_EN" },
	{ "ADC3_WORK", NULL, "ADC3_EN" },
	{ "ADC4_WORK", NULL, "ADC4_EN" },
	{ "ADC5_WORK", NULL, "ADC5_EN" },
	{ "ADC6_WORK", NULL, "ADC6_EN" },
	{ "ADC7_WORK", NULL, "ADC7_EN" },
	{ "ADC8_WORK", NULL, "ADC8_EN" },

	{ "ADC1_BUF_REF_EN", NULL, "ADC_CURRENT_EN12" },
	{ "ADC2_BUF_REF_EN", NULL, "ADC_CURRENT_EN12" },
	{ "ADC3_BUF_REF_EN", NULL, "ADC_CURRENT_EN34" },
	{ "ADC4_BUF_REF_EN", NULL, "ADC_CURRENT_EN34" },
	{ "ADC5_BUF_REF_EN", NULL, "ADC_CURRENT_EN56" },
	{ "ADC6_BUF_REF_EN", NULL, "ADC_CURRENT_EN56" },
	{ "ADC7_BUF_REF_EN", NULL, "ADC_CURRENT_EN78" },
	{ "ADC8_BUF_REF_EN", NULL, "ADC_CURRENT_EN78" },

	{ "ADC1_WORK", NULL, "ADC1_BUF_REF_EN" },
	{ "ADC2_WORK", NULL, "ADC2_BUF_REF_EN" },
	{ "ADC3_WORK", NULL, "ADC3_BUF_REF_EN" },
	{ "ADC4_WORK", NULL, "ADC4_BUF_REF_EN" },
	{ "ADC5_WORK", NULL, "ADC5_BUF_REF_EN" },
	{ "ADC6_WORK", NULL, "ADC6_BUF_REF_EN" },
	{ "ADC7_WORK", NULL, "ADC7_BUF_REF_EN" },
	{ "ADC8_WORK", NULL, "ADC8_BUF_REF_EN" },

	{ "ADC1_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC2_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC3_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC4_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC5_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC6_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC7_CLK_EN", NULL, "ADC_MCLK_GATE" },
	{ "ADC8_CLK_EN", NULL, "ADC_MCLK_GATE" },

	{ "ADC1_WORK", NULL, "ADC1_CLK_EN" },
	{ "ADC2_WORK", NULL, "ADC2_CLK_EN" },
	{ "ADC3_WORK", NULL, "ADC3_CLK_EN" },
	{ "ADC4_WORK", NULL, "ADC4_CLK_EN" },
	{ "ADC5_WORK", NULL, "ADC5_CLK_EN" },
	{ "ADC6_WORK", NULL, "ADC6_CLK_EN" },
	{ "ADC7_WORK", NULL, "ADC7_CLK_EN" },
	{ "ADC8_WORK", NULL, "ADC8_CLK_EN" },

	{ "ALC1_WORK", NULL, "ADC1_WORK" },
	{ "ALC2_WORK", NULL, "ADC2_WORK" },
	{ "ALC3_WORK", NULL, "ADC3_WORK" },
	{ "ALC4_WORK", NULL, "ADC4_WORK" },
	{ "ALC5_WORK", NULL, "ADC5_WORK" },
	{ "ALC6_WORK", NULL, "ADC6_WORK" },
	{ "ALC7_WORK", NULL, "ADC7_WORK" },
	{ "ALC8_WORK", NULL, "ADC8_WORK" },

	{ "HiFi Capture", NULL, "ALC1_WORK" },
	{ "HiFi Capture", NULL, "ALC2_WORK" },
	{ "HiFi Capture", NULL, "ALC3_WORK" },
	{ "HiFi Capture", NULL, "ALC4_WORK" },
	{ "HiFi Capture", NULL, "ALC5_WORK" },
	{ "HiFi Capture", NULL, "ALC6_WORK" },
	{ "HiFi Capture", NULL, "ALC7_WORK" },
	{ "HiFi Capture", NULL, "ALC8_WORK" },

	{ "DAC_L_HPMIX_EN", NULL, "HiFi Playback" },
	{ "DAC_R_HPMIX_EN", NULL, "HiFi Playback" },
	{ "DAC_L_HPMIX_WORK", NULL, "DAC_L_HPMIX_EN" },
	{ "DAC_R_HPMIX_WORK", NULL, "DAC_R_HPMIX_EN" },
	{ "DAC HPMIX Left",  NULL, "DAC_L_HPMIX_WORK" },
	{ "DAC HPMIX Right", NULL, "DAC_R_HPMIX_WORK" },

	{ "DAC_L_DAC_WORK", NULL, "DAC HPMIX Left"  },
	{ "DAC_R_DAC_WORK", NULL, "DAC HPMIX Right" },

	{ "DAC_L_REF_EN", NULL, "DAC_CURRENT_EN" },
	{ "DAC_R_REF_EN", NULL, "DAC_CURRENT_EN" },
	{ "DAC_L_CLK_EN", NULL, "DAC_L_REF_EN" },
	{ "DAC_R_CLK_EN", NULL, "DAC_R_REF_EN" },
	{ "DAC_L_CLK_EN", NULL, "DAC_MCLK_GATE" },
	{ "DAC_R_CLK_EN", NULL, "DAC_MCLK_GATE" },
	{ "DAC_L_DAC_WORK", NULL, "DAC_L_CLK_EN" },
	{ "DAC_R_DAC_WORK", NULL, "DAC_R_CLK_EN" },
	{ "DAC_L_HPMIX_SEL", NULL, "DAC_L_DAC_WORK" },
	{ "DAC_R_HPMIX_SEL", NULL, "DAC_R_DAC_WORK" },

	{ "HPOUT_L", NULL, "DAC_BUF_REF_L" },
	{ "HPOUT_R", NULL, "DAC_BUF_REF_R" },
	{ "L_HPOUT_EN", NULL, "DAC_L_HPMIX_SEL" },
	{ "R_HPOUT_EN", NULL, "DAC_R_HPMIX_SEL" },
	{ "L_HPOUT_WORK", NULL, "L_HPOUT_EN" },
	{ "R_HPOUT_WORK", NULL, "R_HPOUT_EN" },
	{ "HPOUT_POP_SOUND_L", NULL, "L_HPOUT_WORK" },
	{ "HPOUT_POP_SOUND_R", NULL, "R_HPOUT_WORK" },
	{ "HPOUT_L", NULL, "HPOUT_POP_SOUND_L" },
	{ "HPOUT_R", NULL, "HPOUT_POP_SOUND_R" },

	{ "L_LINEOUT_EN", NULL, "DAC_L_HPMIX_SEL" },
	{ "R_LINEOUT_EN", NULL, "DAC_R_HPMIX_SEL" },
	{ "LINEOUT_L", NULL, "L_LINEOUT_EN" },
	{ "LINEOUT_R", NULL, "R_LINEOUT_EN" },
};

static int rk3308_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
				    unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);
	const unsigned int inv_bits = fmt & SND_SOC_DAIFMT_INV_MASK;
	const bool inv_bitclk =
		(inv_bits & SND_SOC_DAIFMT_IB_IF) ||
		(inv_bits & SND_SOC_DAIFMT_IB_NF);
	const bool inv_frmclk =
		(inv_bits & SND_SOC_DAIFMT_IB_IF) ||
		(inv_bits & SND_SOC_DAIFMT_NB_IF);
	const unsigned int dac_master_bits = rk3308->codec_ver < ACODEC_VERSION_C ?
		RK3308_DAC_IO_MODE_MASTER   | RK3308_DAC_MODE_MASTER  :
		RK3308BS_DAC_IO_MODE_MASTER | RK3308BS_DAC_MODE_MASTER;
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;
	bool is_master = false;
	int grp;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		adc_aif2 |= RK3308_ADC_IO_MODE_MASTER;
		adc_aif2 |= RK3308_ADC_MODE_MASTER;
		dac_aif2 |= dac_master_bits;
		is_master = true;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK3308_ADC_I2S_MODE_PCM;
		dac_aif1 |= RK3308_DAC_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK3308_ADC_I2S_MODE_I2S;
		dac_aif1 |= RK3308_DAC_I2S_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_RJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK3308_ADC_I2S_MODE_LJ;
		dac_aif1 |= RK3308_DAC_I2S_MODE_LJ;
		break;
	default:
		return -EINVAL;
	}

	if (inv_bitclk) {
		adc_aif2 |= RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL;
		dac_aif2 |= RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL;
	}

	if (inv_frmclk) {
		adc_aif1 |= RK3308_ADC_I2S_LRC_POL_REVERSAL;
		dac_aif1 |= RK3308_DAC_I2S_LRC_POL_REVERSAL;
	}

	/*
	 * Hold ADC Digital registers start at master mode
	 *
	 * There are 8 ADCs which use the same internal SCLK and LRCK for
	 * master mode. We need to make sure that they are in effect at the
	 * same time, otherwise they will cause abnormal clocks.
	 */
	if (is_master)
		regmap_clear_bits(rk3308->regmap, RK3308_GLB_CON, RK3308_ADC_DIG_WORK);

	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_LRC_POL_REVERSAL |
				   RK3308_ADC_I2S_MODE_MSK,
				   adc_aif1);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp),
				   RK3308_ADC_IO_MODE_MASTER |
				   RK3308_ADC_MODE_MASTER |
				   RK3308_ADC_I2S_BIT_CLK_POL_REVERSAL,
				   adc_aif2);
	}

	/* Hold ADC Digital registers end at master mode */
	if (is_master)
		regmap_set_bits(rk3308->regmap, RK3308_GLB_CON, RK3308_ADC_DIG_WORK);

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_LRC_POL_REVERSAL |
			   RK3308_DAC_I2S_MODE_MSK,
			   dac_aif1);
	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON02,
			   dac_master_bits | RK3308_DAC_I2S_BIT_CLK_POL_REVERSAL,
			   dac_aif2);

	return 0;
}

static int rk3308_codec_dac_dig_config(struct rk3308_codec_priv *rk3308,
				       struct snd_pcm_hw_params *params)
{
	unsigned int dac_aif1 = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dac_aif1 |= RK3308_DAC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rk3308->regmap, RK3308_DAC_DIG_CON01,
			   RK3308_DAC_I2S_VALID_LEN_MSK, dac_aif1);
	regmap_set_bits(rk3308->regmap, RK3308_DAC_DIG_CON02, RK3308_DAC_I2S_WORK);

	return 0;
}

static int rk3308_codec_adc_dig_config(struct rk3308_codec_priv *rk3308,
				       struct snd_pcm_hw_params *params)
{
	unsigned int adc_aif1 = 0;
	/*
	 * grp 0 = ADC1 and ADC2
	 * grp 1 = ADC3 and ADC4
	 * grp 2 = ADC5 and ADC6
	 * grp 3 = ADC7 and ADC8
	 */
	u32 used_adc_grps;
	int grp;

	switch (params_channels(params)) {
	case 1:
		adc_aif1 |= RK3308_ADC_I2S_MONO;
		used_adc_grps = 1;
		break;
	case 2:
	case 4:
	case 6:
	case 8:
		used_adc_grps = params_channels(params) / 2;
		break;
	default:
		dev_err(rk3308->dev, "Invalid channel number %d\n", params_channels(params));
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK3308_ADC_I2S_VALID_LEN_32BITS;
		break;
	default:
		return -EINVAL;
	}

	for (grp = 0; grp < used_adc_grps; grp++) {
		regmap_update_bits(rk3308->regmap,
				   RK3308_ADC_DIG_CON03(grp),
				   RK3308_ADC_L_CH_BIST_MSK | RK3308_ADC_R_CH_BIST_MSK,
				   RK3308_ADC_L_CH_NORMAL_LEFT | RK3308_ADC_R_CH_NORMAL_RIGHT);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_DIG_CON01(grp),
				   RK3308_ADC_I2S_VALID_LEN_MSK | RK3308_ADC_I2S_MONO, adc_aif1);
		regmap_set_bits(rk3308->regmap, RK3308_ADC_DIG_CON02(grp), RK3308_ADC_I2S_WORK);
	}

	return 0;
}

static int rk3308_codec_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);

	return (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		rk3308_codec_dac_dig_config(rk3308, params) :
		rk3308_codec_adc_dig_config(rk3308, params);
}

static const struct snd_soc_dai_ops rk3308_codec_dai_ops = {
	.hw_params = rk3308_codec_hw_params,
	.set_fmt = rk3308_codec_set_dai_fmt,
};

static struct snd_soc_dai_driver rk3308_codec_dai_driver = {
	.name = "rk3308-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &rk3308_codec_dai_ops,
};

static void rk3308_codec_reset(struct snd_soc_component *component)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);

	reset_control_assert(rk3308->reset);
	usleep_range(10000, 11000);     /* estimated value */
	reset_control_deassert(rk3308->reset);

	regmap_write(rk3308->regmap, RK3308_GLB_CON, 0x00);
	usleep_range(10000, 11000);     /* estimated value */
	regmap_write(rk3308->regmap, RK3308_GLB_CON,
		     RK3308_SYS_WORK |
		     RK3308_DAC_DIG_WORK |
		     RK3308_ADC_DIG_WORK);
}

/*
 * Initialize register whose default after HW reset is problematic or which
 * are never modified.
 */
static int rk3308_codec_initialize(struct rk3308_codec_priv *rk3308)
{
	int grp;

	/*
	 * Init ADC digital vol to 0 dB (reset value is 0xff, undocumented).
	 * Range: -97dB ~ +32dB.
	 */
	if (rk3308->codec_ver == ACODEC_VERSION_C) {
		for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++) {
			regmap_write(rk3308->regmap, RK3308_ADC_DIG_CON05(grp),
					   RK3308_ADC_DIG_VOL_CON_x_0DB);
			regmap_write(rk3308->regmap, RK3308_ADC_DIG_CON06(grp),
					   RK3308_ADC_DIG_VOL_CON_x_0DB);
		}
	}

	/* set HPMIX default gains (reset value is 0, which is illegal) */
	regmap_update_bits(rk3308->regmap, RK3308_DAC_ANA_CON12,
			   RK3308_DAC_L_HPMIX_GAIN_MSK |
			   RK3308_DAC_R_HPMIX_GAIN_MSK,
			   RK3308_DAC_L_HPMIX_GAIN_NDB_6 |
			   RK3308_DAC_R_HPMIX_GAIN_NDB_6);

	/* recover DAC digital gain to 0 dB (reset value is 0xff, undocumented) */
	if (rk3308->codec_ver == ACODEC_VERSION_C)
		regmap_write(rk3308->regmap, RK3308_DAC_DIG_CON04,
			     RK3308BS_DAC_DIG_GAIN_0DB);

	/*
	 * Unconditionally enable zero-cross detection (needed for AGC,
	 * harmless without AGC)
	 */
	for (grp = 0; grp < ADC_LR_GROUP_MAX; grp++)
		regmap_set_bits(rk3308->regmap, RK3308_ADC_ANA_CON02(grp),
				RK3308_ADC_CH1_ZEROCROSS_DET_EN |
				RK3308_ADC_CH2_ZEROCROSS_DET_EN);

	return 0;
}

static int rk3308_codec_probe(struct snd_soc_component *component)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);

	rk3308->component = component;

	rk3308_codec_reset(component);
	rk3308_codec_initialize(rk3308);

	return 0;
}

static int rk3308_codec_set_bias_level(struct snd_soc_component *component,
				       enum snd_soc_bias_level level)
{
	struct rk3308_codec_priv *rk3308 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) != SND_SOC_BIAS_OFF)
			break;

		/* Sequence from TRM Section 8.6.3 "Power Up" */
		regmap_set_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
				RK3308_DAC_L_DAC_EN | RK3308_DAC_R_DAC_EN);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				   RK3308_ADC_CURRENT_CHARGE_MSK, 1);
		regmap_set_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				RK3308_ADC_REF_EN);
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				   RK3308_ADC_CURRENT_CHARGE_MSK, 0x7f);
		msleep(20);	/* estimated value */
		break;
	case SND_SOC_BIAS_OFF:
		/* Sequence from TRM Section 8.6.4 "Power Down" */
		regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				   RK3308_ADC_CURRENT_CHARGE_MSK, 1);
		regmap_clear_bits(rk3308->regmap, RK3308_ADC_ANA_CON10(0),
				  RK3308_ADC_REF_EN);
		regmap_clear_bits(rk3308->regmap, RK3308_DAC_ANA_CON02,
				  RK3308_DAC_L_DAC_EN | RK3308_DAC_R_DAC_EN);
		msleep(20);	/* estimated value */
		break;
	}
	return 0;
}

static const struct snd_soc_component_driver rk3308_codec_component_driver = {
	.probe = rk3308_codec_probe,
	.set_bias_level = rk3308_codec_set_bias_level,
	.controls = rk3308_codec_controls,
	.num_controls = ARRAY_SIZE(rk3308_codec_controls),
	.dapm_widgets = rk3308_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk3308_codec_dapm_widgets),
	.dapm_routes = rk3308_codec_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rk3308_codec_dapm_routes),
};

static const struct regmap_config rk3308_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = RK3308_DAC_ANA_CON15,
};

static int rk3308_codec_get_version(struct rk3308_codec_priv *rk3308)
{
	unsigned int chip_id;
	int err;

	err = regmap_read(rk3308->grf, GRF_CHIP_ID, &chip_id);
	if (err)
		return err;

	switch (chip_id) {
	case 3306:
		rk3308->codec_ver = ACODEC_VERSION_A;
		break;
	case 0x3308:
		rk3308->codec_ver = ACODEC_VERSION_B;
		return dev_err_probe(rk3308->dev, -EINVAL, "Chip version B not supported\n");
	case 0x3308c:
		rk3308->codec_ver = ACODEC_VERSION_C;
		break;
	default:
		return dev_err_probe(rk3308->dev, -EINVAL, "Unknown chip_id: 0x%x\n", chip_id);
	}

	dev_info(rk3308->dev, "Found codec version %c\n", rk3308->codec_ver);
	return 0;
}

static int rk3308_codec_set_micbias_level(struct rk3308_codec_priv *rk3308)
{
	struct device_node *np = rk3308->dev->of_node;
	u32 percent;
	u32 mult;
	int err;

	err = of_property_read_u32(np, "rockchip,micbias-avdd-percent", &percent);
	if (err == -EINVAL)
		return 0;
	if (err)
		return dev_err_probe(rk3308->dev, err,
				     "Error reading 'rockchip,micbias-avdd-percent'\n");

	/* Convert percent to register value, linerarly (50% -> 0, 5% step = +1) */
	mult = (percent - 50) / 5;

	/* Check range and that the percent was an exact value allowed */
	if (mult > RK3308_ADC_LEVEL_RANGE_MICBIAS_MAX || mult * 5 + 50 != percent)
		return dev_err_probe(rk3308->dev, -EINVAL,
				     "Invalid value %u for 'rockchip,micbias-avdd-percent'\n",
				     percent);

	regmap_update_bits(rk3308->regmap, RK3308_ADC_ANA_CON07(0),
			   RK3308_ADC_LEVEL_RANGE_MICBIAS_MSK,
			   mult << RK3308_ADC_LEVEL_RANGE_MICBIAS_SFT);

	return 0;
}

static int rk3308_codec_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct rk3308_codec_priv *rk3308;
	void __iomem *base;
	int err;

	rk3308 = devm_kzalloc(&pdev->dev, sizeof(*rk3308), GFP_KERNEL);
	if (!rk3308)
		return -ENOMEM;

	rk3308->dev = dev;

	rk3308->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rk3308->grf))
		return dev_err_probe(dev, PTR_ERR(rk3308->grf), "Error getting GRF\n");

	rk3308->reset = devm_reset_control_get_optional_exclusive(dev, "codec");
	if (IS_ERR(rk3308->reset))
		return dev_err_probe(dev, PTR_ERR(rk3308->reset), "Failed to get reset control\n");

	err = devm_clk_bulk_get(dev, ARRAY_SIZE(rk3308_codec_clocks), rk3308_codec_clocks);
	if (err)
		return dev_err_probe(dev, err, "Failed to get clocks\n");

	err = clk_bulk_prepare_enable(ARRAY_SIZE(rk3308_codec_clocks), rk3308_codec_clocks);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable clocks\n");

	err = rk3308_codec_get_version(rk3308);
	if (err)
		return err;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rk3308->regmap = devm_regmap_init_mmio(dev, base, &rk3308_codec_regmap_config);
	if (IS_ERR(rk3308->regmap))
		return dev_err_probe(dev, PTR_ERR(rk3308->regmap),
				     "Failed to init regmap\n");

	platform_set_drvdata(pdev, rk3308);

	err = rk3308_codec_set_micbias_level(rk3308);
	if (err)
		return err;

	err = devm_snd_soc_register_component(dev, &rk3308_codec_component_driver,
					      &rk3308_codec_dai_driver, 1);
	if (err)
		return dev_err_probe(dev, err, "Failed to register codec\n");

	return 0;
}

static const struct of_device_id __maybe_unused rk3308_codec_of_match[] = {
	{ .compatible = "rockchip,rk3308-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, rk3308_codec_of_match);

static struct platform_driver rk3308_codec_driver = {
	.driver = {
		.name = "rk3308-acodec",
		.of_match_table = rk3308_codec_of_match,
	},
	.probe = rk3308_codec_platform_probe,
};
module_platform_driver(rk3308_codec_driver);

MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_AUTHOR("Luca Ceresoli <luca.ceresoli@bootlin.com>");
MODULE_DESCRIPTION("ASoC RK3308 Codec Driver");
MODULE_LICENSE("GPL");
