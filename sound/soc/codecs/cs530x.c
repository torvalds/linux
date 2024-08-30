// SPDX-License-Identifier: GPL-2.0
//
// CS530x CODEC driver
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <sound/core.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <sound/initval.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs530x.h"

#define CS530X_MAX_ADC_CH	8
#define CS530X_MIN_ADC_CH	2

static const char *cs530x_supply_names[CS530X_NUM_SUPPLIES] = {
	"vdd-a",
	"vdd-io",
};

static const struct reg_default cs530x_reg_defaults[] = {
	{ CS530X_CLK_CFG_0, 0x30 },
	{ CS530X_CLK_CFG_1, 0x0001 },
	{ CS530X_CHIP_ENABLE, 0 },
	{ CS530X_ASP_CFG, 0 },
	{ CS530X_SIGNAL_PATH_CFG, 0 },
	{ CS530X_IN_ENABLES, 0 },
	{ CS530X_IN_RAMP_SUM, 0x0022 },
	{ CS530X_IN_FILTER, 0 },
	{ CS530X_IN_HIZ, 0 },
	{ CS530X_IN_INV, 0 },
	{ CS530X_IN_VOL_CTRL1_0, 0x8000 },
	{ CS530X_IN_VOL_CTRL1_1, 0x8000 },
	{ CS530X_IN_VOL_CTRL2_0, 0x8000 },
	{ CS530X_IN_VOL_CTRL2_1, 0x8000 },
	{ CS530X_IN_VOL_CTRL3_0, 0x8000 },
	{ CS530X_IN_VOL_CTRL3_1, 0x8000 },
	{ CS530X_IN_VOL_CTRL4_0, 0x8000 },
	{ CS530X_IN_VOL_CTRL4_1, 0x8000 },
	{ CS530X_PAD_FN, 0 },
	{ CS530X_PAD_LVL, 0 },
};

static bool cs530x_read_and_write_regs(unsigned int reg)
{
	switch (reg) {
	case CS530X_CLK_CFG_0:
	case CS530X_CLK_CFG_1:
	case CS530X_CHIP_ENABLE:
	case CS530X_ASP_CFG:
	case CS530X_SIGNAL_PATH_CFG:
	case CS530X_IN_ENABLES:
	case CS530X_IN_RAMP_SUM:
	case CS530X_IN_FILTER:
	case CS530X_IN_HIZ:
	case CS530X_IN_INV:
	case CS530X_IN_VOL_CTRL1_0:
	case CS530X_IN_VOL_CTRL1_1:
	case CS530X_IN_VOL_CTRL2_0:
	case CS530X_IN_VOL_CTRL2_1:
	case CS530X_IN_VOL_CTRL3_0:
	case CS530X_IN_VOL_CTRL3_1:
	case CS530X_IN_VOL_CTRL4_0:
	case CS530X_IN_VOL_CTRL4_1:
	case CS530X_PAD_FN:
	case CS530X_PAD_LVL:
		return true;
	default:
		return false;
	}
}

static bool cs530x_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS530X_DEVID:
	case CS530X_REVID:
		return true;
	default:
		return cs530x_read_and_write_regs(reg);
	}
}

static bool cs530x_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS530X_SW_RESET:
	case CS530X_IN_VOL_CTRL5:
		return true;
	default:
		return cs530x_read_and_write_regs(reg);
	}
}

static int cs530x_put_volsw_vu(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret)
		goto volsw_err;

	/* Write IN_VU bit for the volume change to take effect */
	regmap_write(regmap, CS530X_IN_VOL_CTRL5, CS530X_IN_VU);

volsw_err:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -1270, 50, 0);

static const char * const cs530x_in_filter_text[] = {
	"Min Phase Slow Roll-off",
	"Min Phase Fast Roll-off",
	"Linear Phase Slow Roll-off",
	"Linear Phase Fast Roll-off",
};

static SOC_ENUM_SINGLE_DECL(cs530x_in_filter_enum, CS530X_IN_FILTER,
			    CS530X_IN_FILTER_SHIFT,
			    cs530x_in_filter_text);

static const char * const cs530x_in_4ch_sum_text[] = {
	"None",
	"Groups of 2",
	"Groups of 4",
};

static SOC_ENUM_SINGLE_DECL(cs530x_in_sum_ch4_enum, CS530X_IN_RAMP_SUM,
			    CS530X_IN_SUM_MODE_SHIFT,
			    cs530x_in_4ch_sum_text);

static const struct snd_kcontrol_new cs530x_in_sum_4ch_controls[] = {
SOC_ENUM("IN Sum Select", cs530x_in_sum_ch4_enum),
};

static const char * const cs530x_in_8ch_sum_text[] = {
	"None",
	"Groups of 2",
	"Groups of 4",
	"Groups of 8",
};

static SOC_ENUM_SINGLE_DECL(cs530x_in_sum_ch8_enum, CS530X_IN_RAMP_SUM,
			    CS530X_IN_SUM_MODE_SHIFT,
			    cs530x_in_8ch_sum_text);

static const struct snd_kcontrol_new cs530x_in_sum_8ch_controls[] = {
SOC_ENUM("IN Sum Select", cs530x_in_sum_ch8_enum),
};


static const char * const cs530x_vol_ramp_text[] = {
	"0ms/6dB", "0.5ms/6dB", "1ms/6dB", "2ms/6dB", "4ms/6dB", "8ms/6dB",
	"15ms/6dB", "30ms/6dB",
};

static SOC_ENUM_SINGLE_DECL(cs530x_ramp_inc_enum, CS530X_IN_RAMP_SUM,
			    CS530X_RAMP_RATE_INC_SHIFT,
			    cs530x_vol_ramp_text);

static SOC_ENUM_SINGLE_DECL(cs530x_ramp_dec_enum, CS530X_IN_RAMP_SUM,
			    CS530X_RAMP_RATE_DEC_SHIFT,
			    cs530x_vol_ramp_text);

static const struct snd_kcontrol_new cs530x_in_1_to_2_controls[] = {
SOC_SINGLE_EXT_TLV("IN1 Volume", CS530X_IN_VOL_CTRL1_0, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),
SOC_SINGLE_EXT_TLV("IN2 Volume", CS530X_IN_VOL_CTRL1_1, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),

SOC_ENUM("IN DEC Filter Select", cs530x_in_filter_enum),
SOC_ENUM("Input Ramp Up", cs530x_ramp_inc_enum),
SOC_ENUM("Input Ramp Down", cs530x_ramp_dec_enum),

SOC_SINGLE("ADC1 Invert Switch", CS530X_IN_INV, CS530X_IN1_INV_SHIFT, 1, 0),
SOC_SINGLE("ADC2 Invert Switch", CS530X_IN_INV, CS530X_IN2_INV_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new cs530x_in_3_to_4_controls[] = {
SOC_SINGLE_EXT_TLV("IN3 Volume", CS530X_IN_VOL_CTRL2_0, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),
SOC_SINGLE_EXT_TLV("IN4 Volume", CS530X_IN_VOL_CTRL2_1, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),

SOC_SINGLE("ADC3 Invert Switch", CS530X_IN_INV, CS530X_IN3_INV_SHIFT, 1, 0),
SOC_SINGLE("ADC4 Invert Switch", CS530X_IN_INV, CS530X_IN4_INV_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new cs530x_in_5_to_8_controls[] = {
SOC_SINGLE_EXT_TLV("IN5 Volume", CS530X_IN_VOL_CTRL3_0, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),
SOC_SINGLE_EXT_TLV("IN6 Volume", CS530X_IN_VOL_CTRL3_1, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),
SOC_SINGLE_EXT_TLV("IN7 Volume", CS530X_IN_VOL_CTRL4_0, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),
SOC_SINGLE_EXT_TLV("IN8 Volume", CS530X_IN_VOL_CTRL4_1, 0, 255, 1,
		    snd_soc_get_volsw, cs530x_put_volsw_vu, in_vol_tlv),

SOC_SINGLE("ADC5 Invert Switch", CS530X_IN_INV, CS530X_IN5_INV_SHIFT, 1, 0),
SOC_SINGLE("ADC6 Invert Switch", CS530X_IN_INV, CS530X_IN6_INV_SHIFT, 1, 0),
SOC_SINGLE("ADC7 Invert Switch", CS530X_IN_INV, CS530X_IN7_INV_SHIFT, 1, 0),
SOC_SINGLE("ADC8 Invert Switch", CS530X_IN_INV, CS530X_IN8_INV_SHIFT, 1, 0),
};

static int cs530x_adc_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		cs530x->adc_pairs_count++;
		break;
	case SND_SOC_DAPM_POST_PMU:
		regmap_clear_bits(regmap, CS530X_IN_VOL_CTRL1_0 +
				 (w->shift * 2), CS530X_IN_MUTE);
		regmap_clear_bits(regmap, CS530X_IN_VOL_CTRL1_0 +
				 ((w->shift+1) * 2), CS530X_IN_MUTE);

		cs530x->adc_pairs_count--;
		if (!cs530x->adc_pairs_count) {
			usleep_range(1000, 1100);
			return regmap_write(regmap, CS530X_IN_VOL_CTRL5,
					    CS530X_IN_VU);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_set_bits(regmap, CS530X_IN_VOL_CTRL1_0 +
			       (w->shift * 2), CS530X_IN_MUTE);
		regmap_set_bits(regmap, CS530X_IN_VOL_CTRL1_0 +
			       ((w->shift+1) * 2), CS530X_IN_MUTE);
		return regmap_write(regmap, CS530X_IN_VOL_CTRL5,
				    CS530X_IN_VU);
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new adc12_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new adc34_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new adc56_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new adc78_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

static const struct snd_kcontrol_new in_hpf_ctrl =
	SOC_DAPM_SINGLE_VIRT("Switch", 1);

/* General DAPM widgets for all devices */
static const struct snd_soc_dapm_widget cs530x_gen_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("Global Enable", CS530X_CHIP_ENABLE, 0, 0, NULL, 0),
};

/* ADC's Channels 1 and 2 plus generic ADC DAPM events */
static const struct snd_soc_dapm_widget cs530x_adc_ch12_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1"),
SND_SOC_DAPM_INPUT("IN2"),
SND_SOC_DAPM_ADC_E("ADC1", NULL, CS530X_IN_ENABLES, 0, 0,
		   cs530x_adc_event,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_ADC("ADC2", NULL, CS530X_IN_ENABLES, 1, 0),
SND_SOC_DAPM_SWITCH("ADC12 Enable", SND_SOC_NOPM, 0, 0, &adc12_ctrl),
SND_SOC_DAPM_SWITCH("IN HPF", CS530X_IN_FILTER, CS530X_IN_HPF_EN_SHIFT,
		    0, &in_hpf_ctrl),
};

/* ADC's Channels 3 and 4 */
static const struct snd_soc_dapm_widget cs530x_adc_ch34_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN3"),
SND_SOC_DAPM_INPUT("IN4"),
SND_SOC_DAPM_ADC_E("ADC3", NULL, CS530X_IN_ENABLES, 2, 0,
		   cs530x_adc_event,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_ADC("ADC4", NULL, CS530X_IN_ENABLES, 3, 0),
SND_SOC_DAPM_SWITCH("ADC34 Enable", SND_SOC_NOPM, 0, 0, &adc34_ctrl),
};

/* ADC's Channels 5 to 8 */
static const struct snd_soc_dapm_widget cs530x_adc_ch58_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN5"),
SND_SOC_DAPM_INPUT("IN6"),
SND_SOC_DAPM_INPUT("IN7"),
SND_SOC_DAPM_INPUT("IN8"),
SND_SOC_DAPM_ADC_E("ADC5", NULL, CS530X_IN_ENABLES, 4, 0,
		   cs530x_adc_event,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_ADC("ADC6", NULL, CS530X_IN_ENABLES, 5, 0),
SND_SOC_DAPM_SWITCH("ADC56 Enable", SND_SOC_NOPM, 0, 0, &adc56_ctrl),
SND_SOC_DAPM_ADC_E("ADC7", NULL, CS530X_IN_ENABLES, 6, 0,
		   cs530x_adc_event,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_ADC("ADC8", NULL, CS530X_IN_ENABLES, 7, 0),
SND_SOC_DAPM_SWITCH("ADC78 Enable", SND_SOC_NOPM, 0, 0, &adc78_ctrl),
};

static const struct snd_soc_dapm_route adc_ch1_2_routes[] = {
	{ "ADC1", NULL, "Global Enable" },
	{ "ADC2", NULL, "Global Enable" },

	{ "ADC12 Enable", "Switch", "IN1" },
	{ "ADC12 Enable", "Switch", "IN2" },
	{ "ADC1", NULL, "ADC12 Enable" },
	{ "ADC2", NULL, "ADC12 Enable" },
	{ "IN HPF", "Switch", "ADC1" },
	{ "IN HPF", "Switch", "ADC2" },

	{ "AIF Capture", NULL, "IN HPF" },
	{ "AIF Capture", NULL, "ADC1" },
	{ "AIF Capture", NULL, "ADC2" },
};

static const struct snd_soc_dapm_route adc_ch3_4_routes[] = {
	{ "ADC3", NULL, "Global Enable" },
	{ "ADC4", NULL, "Global Enable" },

	{ "ADC34 Enable", "Switch", "IN3" },
	{ "ADC34 Enable", "Switch", "IN4" },
	{ "ADC3", NULL, "ADC34 Enable" },
	{ "ADC4", NULL, "ADC34 Enable" },
	{ "IN HPF", "Switch", "ADC3" },
	{ "IN HPF", "Switch", "ADC4" },

	{ "AIF Capture", NULL, "ADC3" },
	{ "AIF Capture", NULL, "ADC4" },
};

static const struct snd_soc_dapm_route adc_ch5_8_routes[] = {
	{ "ADC5", NULL, "Global Enable" },
	{ "ADC6", NULL, "Global Enable" },
	{ "ADC7", NULL, "Global Enable" },
	{ "ADC8", NULL, "Global Enable" },

	{ "ADC56 Enable", "Switch", "IN5" },
	{ "ADC56 Enable", "Switch", "IN6" },
	{ "ADC5", NULL, "ADC56 Enable" },
	{ "ADC6", NULL, "ADC56 Enable" },
	{ "IN HPF", "Switch", "ADC5" },
	{ "IN HPF", "Switch", "ADC6" },

	{ "AIF Capture", NULL, "ADC5" },
	{ "AIF Capture", NULL, "ADC6" },

	{ "ADC78 Enable", "Switch", "IN7" },
	{ "ADC78 Enable", "Switch", "IN8" },
	{ "ADC7", NULL, "ADC78 Enable" },
	{ "ADC8", NULL, "ADC78 Enable" },
	{ "IN HPF", "Switch", "ADC7" },
	{ "IN HPF", "Switch", "ADC8" },

	{ "AIF Capture", NULL, "ADC7" },
	{ "AIF Capture", NULL, "ADC8" },
};

static void cs530x_add_12_adc_widgets(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	snd_soc_add_component_controls(component,
				       cs530x_in_1_to_2_controls,
				       ARRAY_SIZE(cs530x_in_1_to_2_controls));

	snd_soc_dapm_new_controls(dapm, cs530x_adc_ch12_dapm_widgets,
				  ARRAY_SIZE(cs530x_adc_ch12_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, adc_ch1_2_routes,
				ARRAY_SIZE(adc_ch1_2_routes));
}

static void cs530x_add_34_adc_widgets(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	snd_soc_add_component_controls(component,
				       cs530x_in_3_to_4_controls,
				       ARRAY_SIZE(cs530x_in_3_to_4_controls));

	snd_soc_dapm_new_controls(dapm, cs530x_adc_ch34_dapm_widgets,
				  ARRAY_SIZE(cs530x_adc_ch34_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, adc_ch3_4_routes,
				ARRAY_SIZE(adc_ch3_4_routes));
}

static int cs530x_set_bclk(struct snd_soc_component *component, const int freq)
{
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;
	unsigned int bclk_val;

	switch (freq) {
	case 2822400:
	case 3072000:
		bclk_val = CS530X_BCLK_2P822_3P072;
		break;
	case 5644800:
	case 6144000:
		bclk_val = CS530X_BCLK_5P6448_6P144;
		break;
	case 11289600:
	case 12288000:
		bclk_val = CS530X_BCLK_11P2896_12P288;
		break;
	case 22579200:
	case 24576000:
		bclk_val = CS530X_BCLK_24P5792_24P576;
		break;
	default:
		dev_err(component->dev, "Invalid BCLK frequency %d\n", freq);
		return -EINVAL;
	}

	dev_dbg(component->dev, "BCLK frequency is %d\n", freq);

	return regmap_update_bits(regmap, CS530X_ASP_CFG,
				  CS530X_ASP_BCLK_FREQ_MASK, bclk_val);
}

static int cs530x_set_pll_refclk(struct snd_soc_component *component,
				  const unsigned int freq)
{
	struct cs530x_priv *priv = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = priv->regmap;
	unsigned int refclk;

	switch (freq) {
	case 2822400:
	case 3072000:
		refclk = CS530X_REFCLK_2P822_3P072;
		break;
	case 5644800:
	case 6144000:
		refclk = CS530X_REFCLK_5P6448_6P144;
		break;
	case 11289600:
	case 12288000:
		refclk = CS530X_REFCLK_11P2896_12P288;
		break;
	case 22579200:
	case 24576000:
		refclk = CS530X_REFCLK_24P5792_24P576;
		break;
	default:
		dev_err(component->dev, "Invalid PLL refclk %d\n", freq);
		return -EINVAL;
	}

	return regmap_update_bits(regmap, CS530X_CLK_CFG_0,
				  CS530X_PLL_REFCLK_FREQ_MASK, refclk);
}

static int cs530x_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;
	int ret = 0, fs = params_rate(params), bclk;
	unsigned int fs_val;


	switch (fs) {
	case 32000:
		fs_val = CS530X_FS_32K;
		break;
	case 44100:
	case 48000:
		fs_val = CS530X_FS_48K_44P1K;
		break;
	case 88200:
	case 96000:
		fs_val = CS530X_FS_96K_88P2K;
		break;
	case 176400:
	case 192000:
		fs_val = CS530X_FS_192K_176P4K;
		break;
	case 356800:
	case 384000:
		fs_val = CS530X_FS_384K_356P8K;
		break;
	case 705600:
	case 768000:
		fs_val = CS530X_FS_768K_705P6K;
		break;
	default:
		dev_err(component->dev, "Invalid sample rate %d\n", fs);
		return -EINVAL;
	}

	cs530x->fs = fs;
	regmap_update_bits(regmap, CS530X_CLK_CFG_1,
			   CS530X_SAMPLE_RATE_MASK, fs_val);


	if (regmap_test_bits(regmap, CS530X_SIGNAL_PATH_CFG,
			     CS530X_TDM_EN_MASK)) {
		dev_dbg(component->dev, "Configuring for %d %d bit TDM slots\n",
			cs530x->tdm_slots, cs530x->tdm_width);
		bclk = snd_soc_tdm_params_to_bclk(params,
						  cs530x->tdm_width,
						  cs530x->tdm_slots,
						  1);
	} else {
		bclk = snd_soc_params_to_bclk(params);
	}

	if (!regmap_test_bits(regmap, CS530X_CLK_CFG_0,
			     CS530X_PLL_REFCLK_SRC_MASK)) {
		ret = cs530x_set_pll_refclk(component, bclk);
		if (ret)
			return ret;
	}

	return cs530x_set_bclk(component, bclk);
}

static int cs530x_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct cs530x_priv *priv = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = priv->regmap;
	unsigned int asp_fmt, asp_cfg = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		asp_cfg = CS530X_ASP_PRIMARY;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = CS530X_ASP_FMT_DSP_A;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = CS530X_ASP_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		asp_fmt = CS530X_ASP_FMT_LJ;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		asp_cfg |= CS530X_ASP_BCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(regmap, CS530X_ASP_CFG,
			   CS530X_ASP_PRIMARY | CS530X_ASP_BCLK_INV,
			   asp_cfg);

	return regmap_update_bits(regmap, CS530X_SIGNAL_PATH_CFG,
				  CS530X_ASP_FMT_MASK, asp_fmt);
}

static bool cs530x_check_mclk_freq(struct snd_soc_component *component,
				   const unsigned int freq)
{
	switch (freq) {
	case 24576000:
	case 22579200:
	case 12288000:
	case 11289600:
		return true;
	default:
		dev_err(component->dev, "Invalid MCLK %d\n", freq);
		return false;
	}
}

static int cs530x_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				 unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;
	unsigned int val;

	switch (tx_mask) {
	case CS530X_0_1_TDM_SLOT_MASK:
	case CS530X_0_3_TDM_SLOT_MASK:
	case CS530X_0_7_TDM_SLOT_MASK:
		val = CS530X_0_7_TDM_SLOT_VAL;
		break;
	case CS530X_2_3_TDM_SLOT_MASK:
		val = CS530X_2_3_TDM_SLOT_VAL;
		break;
	case CS530X_4_5_TDM_SLOT_MASK:
	case CS530X_4_7_TDM_SLOT_MASK:
		val = CS530X_4_7_TDM_SLOT_VAL;
		break;
	case CS530X_6_7_TDM_SLOT_MASK:
		val = CS530X_6_7_TDM_SLOT_VAL;
		break;
	case CS530X_8_9_TDM_SLOT_MASK:
	case CS530X_8_11_TDM_SLOT_MASK:
	case CS530X_8_15_TDM_SLOT_MASK:
		val = CS530X_8_15_TDM_SLOT_VAL;
		break;
	case CS530X_10_11_TDM_SLOT_MASK:
		val = CS530X_10_11_TDM_SLOT_VAL;
		break;
	case CS530X_12_13_TDM_SLOT_MASK:
	case CS530X_12_15_TDM_SLOT_MASK:
		val = CS530X_12_15_TDM_SLOT_VAL;
		break;
	case CS530X_14_15_TDM_SLOT_MASK:
		val = CS530X_14_15_TDM_SLOT_VAL;
		break;
	default:
		dev_err(component->dev, "Invalid TX slot(s) 0x%x\n", tx_mask);
		return -EINVAL;
	}

	cs530x->tdm_width = slot_width;
	cs530x->tdm_slots = slots;

	return regmap_update_bits(regmap, CS530X_SIGNAL_PATH_CFG,
				  CS530X_ASP_TDM_SLOT_MASK,
				  val << CS530X_ASP_TDM_SLOT_SHIFT);
}

static const struct snd_soc_dai_ops cs530x_dai_ops = {
	.set_fmt = cs530x_set_fmt,
	.hw_params = cs530x_hw_params,
	.set_tdm_slot = cs530x_set_tdm_slot,
};

static const struct snd_soc_dai_driver cs530x_dai = {
	.name = "cs530x-dai",
	.capture = {
		.stream_name = "AIF Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &cs530x_dai_ops,
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
};

static int cs530x_set_pll(struct snd_soc_component *component, int pll_id,
			   int source, unsigned int freq_in,
			   unsigned int freq_out)
{
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;
	unsigned int sysclk_src;
	int ret;

	regmap_read(regmap, CS530X_CLK_CFG_0, &sysclk_src);

	/* Check if the source is the PLL  */
	if ((sysclk_src & CS530X_SYSCLK_SRC_MASK) == 0)
		return 0;

	switch (source) {
	case CS530X_PLL_SRC_MCLK:
		if (!cs530x_check_mclk_freq(component, freq_in))
			return -EINVAL;

		ret = cs530x_set_pll_refclk(component, freq_in);
		if (ret)
			return ret;

		break;
	case CS530X_PLL_SRC_BCLK:
		break;
	default:
		dev_err(component->dev, "Invalid PLL source %d\n", source);
		return -EINVAL;
	}

	return regmap_update_bits(regmap, CS530X_CLK_CFG_0,
				  CS530X_PLL_REFCLK_SRC_MASK, source);
}

static int cs530x_component_probe(struct snd_soc_component *component)
{
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int num_widgets;

	snd_soc_dapm_new_controls(dapm, cs530x_gen_dapm_widgets,
				  ARRAY_SIZE(cs530x_gen_dapm_widgets));

	switch (cs530x->devtype) {
	case CS5302:
		cs530x_add_12_adc_widgets(component);
		break;
	case CS5304:
		cs530x_add_12_adc_widgets(component);
		cs530x_add_34_adc_widgets(component);

		num_widgets = ARRAY_SIZE(cs530x_in_sum_4ch_controls);
		snd_soc_add_component_controls(component,
					       cs530x_in_sum_4ch_controls,
					       num_widgets);
		break;

	case CS5308:
		cs530x_add_12_adc_widgets(component);
		cs530x_add_34_adc_widgets(component);

		num_widgets = ARRAY_SIZE(cs530x_in_5_to_8_controls);
		snd_soc_add_component_controls(component,
					       cs530x_in_5_to_8_controls,
					       num_widgets);

		num_widgets = ARRAY_SIZE(cs530x_in_sum_8ch_controls);
		snd_soc_add_component_controls(component,
					       cs530x_in_sum_8ch_controls,
					       num_widgets);

		num_widgets = ARRAY_SIZE(cs530x_adc_ch58_dapm_widgets);
		snd_soc_dapm_new_controls(dapm, cs530x_adc_ch58_dapm_widgets,
					  num_widgets);

		snd_soc_dapm_add_routes(dapm, adc_ch5_8_routes,
					ARRAY_SIZE(adc_ch5_8_routes));
		break;
	default:
		dev_err(component->dev, "Invalid device type %d\n",
			cs530x->devtype);
		return -EINVAL;
	}

	return 0;
}

static int cs530x_set_sysclk(struct snd_soc_component *component, int clk_id,
				int source, unsigned int freq, int dir)
{
	struct cs530x_priv *cs530x = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = cs530x->regmap;

	switch (source) {
	case CS530X_SYSCLK_SRC_MCLK:
		if (freq != 24560000 && freq != 22572000) {
			dev_err(component->dev, "Invalid MCLK source rate %d\n",
				freq);
			return -EINVAL;
		}

		cs530x->mclk_rate = freq;
		break;
	case CS530X_SYSCLK_SRC_PLL:
		break;
	default:
		dev_err(component->dev, "Invalid clock id %d\n", clk_id);
		return -EINVAL;
	}

	return regmap_update_bits(regmap, CS530X_CLK_CFG_0,
				  CS530X_SYSCLK_SRC_MASK,
				  source << CS530X_SYSCLK_SRC_SHIFT);
}

static const struct snd_soc_component_driver soc_component_dev_cs530x = {
	.probe			= cs530x_component_probe,
	.set_sysclk		= cs530x_set_sysclk,
	.set_pll		= cs530x_set_pll,
	.endianness		= 1,
};

const struct regmap_config cs530x_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = CS530X_MAX_REGISTER,
	.readable_reg = cs530x_readable_register,
	.writeable_reg = cs530x_writeable_register,

	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = cs530x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs530x_reg_defaults),
};
EXPORT_SYMBOL_NS_GPL(cs530x_regmap, SND_SOC_CS530X);

static int cs530x_check_device_id(struct cs530x_priv *cs530x)
{
	struct device *dev = cs530x->dev;
	unsigned int dev_id, rev;
	int ret;

	ret = regmap_read(cs530x->regmap, CS530X_DEVID, &dev_id);
	if (ret)
		return dev_err_probe(dev, ret, "Can't read device ID\n");

	ret = regmap_read(cs530x->regmap, CS530X_REVID, &rev);
	if (ret)
		return dev_err_probe(dev, ret, "Can't read REV ID\n");

	dev_dbg(dev, "Device ID 0x%x Rev ID 0x%x\n", dev_id, rev);

	switch (dev_id) {
	case CS530X_2CH_ADC_DEV_ID:
		cs530x->num_adcs = 2;
		break;
	case CS530X_4CH_ADC_DEV_ID:
		cs530x->num_adcs = 4;
		break;
	case CS530X_8CH_ADC_DEV_ID:
		cs530x->num_adcs = 8;
		break;
	default:
		return dev_err_probe(dev, -EINVAL, "Invalid device ID 0x%x\n",
				     dev_id);
	}

	return 0;
}

static int cs530x_parse_device_properties(struct cs530x_priv *cs530x)
{
	struct regmap *regmap = cs530x->regmap;
	struct device *dev = cs530x->dev;
	unsigned int val = 0;

	switch (cs530x->num_adcs) {
	case 8:
		if (device_property_read_bool(dev, "cirrus,in-hiz-pin78"))
			val = CS530X_IN78_HIZ;

		if (device_property_read_bool(dev, "cirrus,in-hiz-pin56"))
			val |= CS530X_IN56_HIZ;

		fallthrough;
	case 4:
		if (device_property_read_bool(dev, "cirrus,in-hiz-pin34"))
			val |= CS530X_IN34_HIZ;

		fallthrough;
	case 2:
		if (device_property_read_bool(dev, "cirrus,in-hiz-pin12"))
			val |= CS530X_IN12_HIZ;

		return regmap_set_bits(regmap, CS530X_IN_HIZ, val);
	default:
		return dev_err_probe(dev, -EINVAL,
				     "Invalid number of adcs %d\n",
				     cs530x->num_adcs);
	}
}

int cs530x_probe(struct cs530x_priv *cs530x)
{
	struct device *dev = cs530x->dev;
	int ret, i;

	cs530x->dev_dai = devm_kmemdup(dev, &cs530x_dai,
					sizeof(*(cs530x->dev_dai)),
					GFP_KERNEL);
	if (!cs530x->dev_dai)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(cs530x->supplies); i++)
		cs530x->supplies[i].supply = cs530x_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(cs530x->supplies),
				      cs530x->supplies);
	if (ret != 0)
		return dev_err_probe(dev, ret, "Failed to request supplies");

	ret = regulator_bulk_enable(ARRAY_SIZE(cs530x->supplies),
				    cs530x->supplies);
	if (ret != 0)
		return dev_err_probe(dev, ret, "Failed to enable supplies");

	cs530x->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(cs530x->reset_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(cs530x->reset_gpio),
			      "Reset gpio not available\n");
		goto err_regulator;
	}

	if (cs530x->reset_gpio) {
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs530x->reset_gpio, 0);
	}

	usleep_range(5000, 5100);
	ret = cs530x_check_device_id(cs530x);
	if (ret)
		goto err_reset;

	if (!cs530x->reset_gpio) {
		ret = regmap_write(cs530x->regmap, CS530X_SW_RESET,
				   CS530X_SW_RST_VAL);
		if (ret) {
			dev_err_probe(dev, ret, "Soft Reset Failed\n");
			goto err_reset;
		}
	}

	ret = cs530x_parse_device_properties(cs530x);
	if (ret)
		goto err_reset;

	cs530x->dev_dai->capture.channels_max = cs530x->num_adcs;

	ret = devm_snd_soc_register_component(dev,
			&soc_component_dev_cs530x, cs530x->dev_dai, 1);
	if (ret) {
		dev_err_probe(dev, ret, "Can't register cs530x component\n");
		goto err_reset;
	}

	return 0;

err_reset:
	gpiod_set_value_cansleep(cs530x->reset_gpio, 1);

err_regulator:
	regulator_bulk_disable(ARRAY_SIZE(cs530x->supplies),
			       cs530x->supplies);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs530x_probe, SND_SOC_CS530X);

MODULE_DESCRIPTION("CS530X CODEC Driver");
MODULE_AUTHOR("Paul Handrigan <paulha@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
