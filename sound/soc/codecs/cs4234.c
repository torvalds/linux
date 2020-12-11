// SPDX-License-Identifier: GPL-2.0-only
// cs4234.c -- ALSA SoC CS4234 driver
//
// Copyright (C) 2020 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.
//

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/workqueue.h>

#include "cs4234.h"

struct cs4234 {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data core_supplies[2];
	int num_core_supplies;
	struct completion vq_ramp_complete;
	struct delayed_work vq_ramp_delay;
	struct clk *mclk;
	unsigned long mclk_rate;
	unsigned long lrclk_rate;
	unsigned int format;
	struct snd_ratnum rate_dividers[2];
	struct snd_pcm_hw_constraint_ratnums rate_constraint;
};

/* -89.92dB to +6.02dB with step of 0.38dB */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -8992, 38, 0);

static const char * const cs4234_dac14_delay_text[] = {
	  "0us", "100us", "150us", "200us", "225us", "250us", "275us", "300us",
	"325us", "350us", "375us", "400us", "425us", "450us", "475us", "500us",
};
static SOC_ENUM_SINGLE_DECL(cs4234_dac14_group_delay, CS4234_TPS_CTRL,
			    CS4234_GRP_DELAY_SHIFT, cs4234_dac14_delay_text);

static const char * const cs4234_noise_gate_text[] = {
	"72dB",  "78dB",  "84dB", "90dB", "96dB", "102dB", "138dB", "Disabled",
};
static SOC_ENUM_SINGLE_DECL(cs4234_ll_noise_gate, CS4234_LOW_LAT_CTRL1,
			    CS4234_LL_NG_SHIFT, cs4234_noise_gate_text);
static SOC_ENUM_SINGLE_DECL(cs4234_dac14_noise_gate, CS4234_DAC_CTRL1,
			    CS4234_DAC14_NG_SHIFT, cs4234_noise_gate_text);
static SOC_ENUM_SINGLE_DECL(cs4234_dac5_noise_gate, CS4234_DAC_CTRL2,
			    CS4234_DAC5_NG_SHIFT, cs4234_noise_gate_text);

static const char * const cs4234_dac5_config_fltr_sel_text[] = {
	"Interpolation Filter", "Sample and Hold"
};
static SOC_ENUM_SINGLE_DECL(cs4234_dac5_config_fltr_sel, CS4234_DAC_CTRL1,
			    CS4234_DAC5_CFG_FLTR_SHIFT,
			    cs4234_dac5_config_fltr_sel_text);

static const char * const cs4234_mute_delay_text[] = {
	"1x",  "4x",  "16x", "64x",
};
static SOC_ENUM_SINGLE_DECL(cs4234_mute_delay, CS4234_VOLUME_MODE,
			    CS4234_MUTE_DELAY_SHIFT, cs4234_mute_delay_text);

static const char * const cs4234_minmax_delay_text[] = {
	"1x",  "2x",  "4x", "8x", "16x",  "32x", "64x", "128x",
};
static SOC_ENUM_SINGLE_DECL(cs4234_min_delay, CS4234_VOLUME_MODE,
			    CS4234_MIN_DELAY_SHIFT, cs4234_minmax_delay_text);
static SOC_ENUM_SINGLE_DECL(cs4234_max_delay, CS4234_VOLUME_MODE,
			    CS4234_MAX_DELAY_SHIFT, cs4234_minmax_delay_text);

static int cs4234_dac14_grp_delay_put(struct snd_kcontrol *kctrl,
				      struct snd_ctl_elem_value *uctrl)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kctrl);
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	unsigned int val = 0;
	int ret = 0;

	snd_soc_dapm_mutex_lock(dapm);

	regmap_read(cs4234->regmap, CS4234_ADC_CTRL2, &val);
	if ((val & 0x0F) != 0x0F) { // are all the ADCs powerdown
		ret = -EBUSY;
		dev_err(component->dev, "Can't change group delay while ADC are ON\n");
		goto exit;
	}

	regmap_read(cs4234->regmap, CS4234_DAC_CTRL4, &val);
	if ((val & 0x1F) != 0x1F) { // are all the DACs powerdown
		ret = -EBUSY;
		dev_err(component->dev, "Can't change group delay while DAC are ON\n");
		goto exit;
	}

	ret = snd_soc_put_enum_double(kctrl, uctrl);
exit:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static void cs4234_vq_ramp_done(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct cs4234 *cs4234 = container_of(dw, struct cs4234, vq_ramp_delay);

	complete_all(&cs4234->vq_ramp_complete);
}

static int cs4234_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		switch (snd_soc_component_get_bias_level(component)) {
		case SND_SOC_BIAS_STANDBY:
			wait_for_completion(&cs4234->vq_ramp_complete);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget cs4234_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("SDRX1", NULL,  0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDRX2", NULL,  1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDRX3", NULL,  2, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDRX4", NULL,  3, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDRX5", NULL,  4, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC1", NULL, CS4234_DAC_CTRL4, CS4234_PDN_DAC1_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC2", NULL, CS4234_DAC_CTRL4, CS4234_PDN_DAC2_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC3", NULL, CS4234_DAC_CTRL4, CS4234_PDN_DAC3_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC4", NULL, CS4234_DAC_CTRL4, CS4234_PDN_DAC4_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC5", NULL, CS4234_DAC_CTRL4, CS4234_PDN_DAC5_SHIFT, 1),

	SND_SOC_DAPM_OUTPUT("AOUT1"),
	SND_SOC_DAPM_OUTPUT("AOUT2"),
	SND_SOC_DAPM_OUTPUT("AOUT3"),
	SND_SOC_DAPM_OUTPUT("AOUT4"),
	SND_SOC_DAPM_OUTPUT("AOUT5"),

	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),
	SND_SOC_DAPM_INPUT("AIN4"),

	SND_SOC_DAPM_ADC("ADC1", NULL, CS4234_ADC_CTRL2, CS4234_PDN_ADC1_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC2", NULL, CS4234_ADC_CTRL2, CS4234_PDN_ADC2_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC3", NULL, CS4234_ADC_CTRL2, CS4234_PDN_ADC3_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC4", NULL, CS4234_ADC_CTRL2, CS4234_PDN_ADC4_SHIFT, 1),

	SND_SOC_DAPM_AIF_OUT("SDTX1", NULL, 0, SND_SOC_NOPM, 0, 1),
	SND_SOC_DAPM_AIF_OUT("SDTX2", NULL, 1, SND_SOC_NOPM, 0, 1),
	SND_SOC_DAPM_AIF_OUT("SDTX3", NULL, 2, SND_SOC_NOPM, 0, 1),
	SND_SOC_DAPM_AIF_OUT("SDTX4", NULL, 3, SND_SOC_NOPM, 0, 1),
};

static const struct snd_soc_dapm_route cs4234_dapm_routes[] = {
	/* Playback */
	{ "AOUT1", NULL, "DAC1" },
	{ "AOUT2", NULL, "DAC2" },
	{ "AOUT3", NULL, "DAC3" },
	{ "AOUT4", NULL, "DAC4" },
	{ "AOUT5", NULL, "DAC5" },

	{ "DAC1", NULL, "SDRX1" },
	{ "DAC2", NULL, "SDRX2" },
	{ "DAC3", NULL, "SDRX3" },
	{ "DAC4", NULL, "SDRX4" },
	{ "DAC5", NULL, "SDRX5" },

	{ "SDRX1", NULL, "Playback" },
	{ "SDRX2", NULL, "Playback" },
	{ "SDRX3", NULL, "Playback" },
	{ "SDRX4", NULL, "Playback" },
	{ "SDRX5", NULL, "Playback" },

	/* Capture */
	{ "ADC1", NULL, "AIN1" },
	{ "ADC2", NULL, "AIN2" },
	{ "ADC3", NULL, "AIN3" },
	{ "ADC4", NULL, "AIN4" },

	{ "SDTX1", NULL, "ADC1" },
	{ "SDTX2", NULL, "ADC2" },
	{ "SDTX3", NULL, "ADC3" },
	{ "SDTX4", NULL, "ADC4" },

	{ "Capture", NULL, "SDTX1" },
	{ "Capture", NULL, "SDTX2" },
	{ "Capture", NULL, "SDTX3" },
	{ "Capture", NULL, "SDTX4" },
};

static const struct snd_kcontrol_new cs4234_snd_controls[] = {
	SOC_SINGLE_TLV("Master Volume", CS4234_MASTER_VOL, 0, 0xff, 1, dac_tlv),
	SOC_SINGLE_TLV("DAC1 Volume", CS4234_DAC1_VOL, 0, 0xff, 1, dac_tlv),
	SOC_SINGLE_TLV("DAC2 Volume", CS4234_DAC2_VOL, 0, 0xff, 1, dac_tlv),
	SOC_SINGLE_TLV("DAC3 Volume", CS4234_DAC3_VOL, 0, 0xff, 1, dac_tlv),
	SOC_SINGLE_TLV("DAC4 Volume", CS4234_DAC4_VOL, 0, 0xff, 1, dac_tlv),
	SOC_SINGLE_TLV("DAC5 Volume", CS4234_DAC5_VOL, 0, 0xff, 1, dac_tlv),

	SOC_SINGLE("DAC5 Soft Ramp Switch", CS4234_DAC_CTRL3, CS4234_DAC5_ATT_SHIFT, 1, 1),
	SOC_SINGLE("DAC1-4 Soft Ramp Switch", CS4234_DAC_CTRL3, CS4234_DAC14_ATT_SHIFT, 1, 1),

	SOC_SINGLE("ADC HPF Switch", CS4234_ADC_CTRL1, CS4234_ENA_HPF_SHIFT, 1, 0),

	SOC_ENUM_EXT("DAC1-4 Group Delay", cs4234_dac14_group_delay,
		     snd_soc_get_enum_double, cs4234_dac14_grp_delay_put),

	SOC_SINGLE("ADC1 Invert Switch", CS4234_ADC_CTRL1, CS4234_INV_ADC1_SHIFT, 1, 0),
	SOC_SINGLE("ADC2 Invert Switch", CS4234_ADC_CTRL1, CS4234_INV_ADC2_SHIFT, 1, 0),
	SOC_SINGLE("ADC3 Invert Switch", CS4234_ADC_CTRL1, CS4234_INV_ADC3_SHIFT, 1, 0),
	SOC_SINGLE("ADC4 Invert Switch", CS4234_ADC_CTRL1, CS4234_INV_ADC4_SHIFT, 1, 0),

	SOC_SINGLE("DAC1 Invert Switch", CS4234_DAC_CTRL2, CS4234_INV_DAC1_SHIFT, 1, 0),
	SOC_SINGLE("DAC2 Invert Switch", CS4234_DAC_CTRL2, CS4234_INV_DAC2_SHIFT, 1, 0),
	SOC_SINGLE("DAC3 Invert Switch", CS4234_DAC_CTRL2, CS4234_INV_DAC3_SHIFT, 1, 0),
	SOC_SINGLE("DAC4 Invert Switch", CS4234_DAC_CTRL2, CS4234_INV_DAC4_SHIFT, 1, 0),
	SOC_SINGLE("DAC5 Invert Switch", CS4234_DAC_CTRL2, CS4234_INV_DAC5_SHIFT, 1, 0),

	SOC_SINGLE("ADC1 Switch", CS4234_ADC_CTRL2, CS4234_MUTE_ADC1_SHIFT, 1, 1),
	SOC_SINGLE("ADC2 Switch", CS4234_ADC_CTRL2, CS4234_MUTE_ADC2_SHIFT, 1, 1),
	SOC_SINGLE("ADC3 Switch", CS4234_ADC_CTRL2, CS4234_MUTE_ADC3_SHIFT, 1, 1),
	SOC_SINGLE("ADC4 Switch", CS4234_ADC_CTRL2, CS4234_MUTE_ADC4_SHIFT, 1, 1),

	SOC_SINGLE("DAC1 Switch", CS4234_DAC_CTRL3, CS4234_MUTE_DAC1_SHIFT, 1, 1),
	SOC_SINGLE("DAC2 Switch", CS4234_DAC_CTRL3, CS4234_MUTE_DAC2_SHIFT, 1, 1),
	SOC_SINGLE("DAC3 Switch", CS4234_DAC_CTRL3, CS4234_MUTE_DAC3_SHIFT, 1, 1),
	SOC_SINGLE("DAC4 Switch", CS4234_DAC_CTRL3, CS4234_MUTE_DAC4_SHIFT, 1, 1),
	SOC_SINGLE("DAC5 Switch", CS4234_DAC_CTRL3, CS4234_MUTE_DAC5_SHIFT, 1, 1),
	SOC_SINGLE("Low-latency Switch", CS4234_DAC_CTRL3, CS4234_MUTE_LL_SHIFT, 1, 1),

	SOC_SINGLE("DAC1 Low-latency Invert Switch", CS4234_LOW_LAT_CTRL1,
		   CS4234_INV_LL1_SHIFT, 1, 0),
	SOC_SINGLE("DAC2 Low-latency Invert Switch", CS4234_LOW_LAT_CTRL1,
		   CS4234_INV_LL2_SHIFT, 1, 0),
	SOC_SINGLE("DAC3 Low-latency Invert Switch", CS4234_LOW_LAT_CTRL1,
		   CS4234_INV_LL3_SHIFT, 1, 0),
	SOC_SINGLE("DAC4 Low-latency Invert Switch", CS4234_LOW_LAT_CTRL1,
		   CS4234_INV_LL4_SHIFT, 1, 0),

	SOC_ENUM("Low-latency Noise Gate", cs4234_ll_noise_gate),
	SOC_ENUM("DAC1-4 Noise Gate", cs4234_dac14_noise_gate),
	SOC_ENUM("DAC5 Noise Gate", cs4234_dac5_noise_gate),

	SOC_SINGLE("DAC1-4 De-emphasis Switch", CS4234_DAC_CTRL1,
		   CS4234_DAC14_DE_SHIFT, 1, 0),
	SOC_SINGLE("DAC5 De-emphasis Switch", CS4234_DAC_CTRL1,
		   CS4234_DAC5_DE_SHIFT, 1, 0),

	SOC_SINGLE("DAC5 Master Controlled Switch", CS4234_DAC_CTRL1,
		   CS4234_DAC5_MVC_SHIFT, 1, 0),

	SOC_ENUM("DAC5 Filter", cs4234_dac5_config_fltr_sel),

	SOC_ENUM("Mute Delay", cs4234_mute_delay),
	SOC_ENUM("Ramp Minimum Delay", cs4234_min_delay),
	SOC_ENUM("Ramp Maximum Delay", cs4234_max_delay),

};

static int cs4234_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(component);
	unsigned int sp_ctrl = 0;

	cs4234->format = format & SND_SOC_DAIFMT_FORMAT_MASK;
	switch (cs4234->format) {
	case SND_SOC_DAIFMT_LEFT_J:
		sp_ctrl |= CS4234_LEFT_J << CS4234_SP_FORMAT_SHIFT;
		break;
	case SND_SOC_DAIFMT_I2S:
		sp_ctrl |= CS4234_I2S << CS4234_SP_FORMAT_SHIFT;
		break;
	case SND_SOC_DAIFMT_DSP_A: /* TDM mode in datasheet */
		sp_ctrl |= CS4234_TDM << CS4234_SP_FORMAT_SHIFT;
		break;
	default:
		dev_err(component->dev, "Unsupported dai format\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		if (cs4234->format == SND_SOC_DAIFMT_DSP_A) {
			dev_err(component->dev, "Unsupported DSP A format in master mode\n");
			return -EINVAL;
		}
		sp_ctrl |= CS4234_MST_SLV_MASK;
		break;
	default:
		dev_err(component->dev, "Unsupported master/slave mode\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		sp_ctrl |= CS4234_INVT_SCLK_MASK;
		break;
	default:
		dev_err(component->dev, "Unsupported inverted clock setting\n");
		return -EINVAL;
	}

	regmap_update_bits(cs4234->regmap, CS4234_SP_CTRL,
			   CS4234_SP_FORMAT_MASK | CS4234_MST_SLV_MASK | CS4234_INVT_SCLK_MASK,
			   sp_ctrl);

	return 0;
}

static int cs4234_dai_hw_params(struct snd_pcm_substream *sub,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(component);
	unsigned int mclk_mult, double_speed = 0;
	int ret = 0, rate_ad, sample_width;

	cs4234->lrclk_rate = params_rate(params);
	mclk_mult = cs4234->mclk_rate / cs4234->lrclk_rate;

	if (cs4234->lrclk_rate > 48000) {
		double_speed = 1;
		mclk_mult *= 2;
	}

	switch (mclk_mult) {
	case 256:
	case 384:
	case 512:
		regmap_update_bits(cs4234->regmap, CS4234_CLOCK_SP,
				   CS4234_SPEED_MODE_MASK,
				   double_speed << CS4234_SPEED_MODE_SHIFT);
		regmap_update_bits(cs4234->regmap, CS4234_CLOCK_SP,
				   CS4234_MCLK_RATE_MASK,
				   ((mclk_mult / 128) - 2) << CS4234_MCLK_RATE_SHIFT);
		break;
	default:
		dev_err(component->dev, "Unsupported mclk/lrclk rate\n");
		return -EINVAL;
	}

	switch (cs4234->lrclk_rate) {
	case 48000:
	case 96000:
		rate_ad = CS4234_48K;
		break;
	case 44100:
	case 88200:
		rate_ad = CS4234_44K1;
		break;
	case 32000:
	case 64000:
		rate_ad = CS4234_32K;
		break;
	default:
		dev_err(component->dev, "Unsupported LR clock\n");
		return -EINVAL;
	}
	regmap_update_bits(cs4234->regmap, CS4234_CLOCK_SP, CS4234_BASE_RATE_MASK,
			   rate_ad << CS4234_BASE_RATE_SHIFT);

	sample_width = params_width(params);
	switch (sample_width) {
	case 16:
		sample_width = 0;
		break;
	case 18:
		sample_width = 1;
		break;
	case 20:
		sample_width = 2;
		break;
	case 24:
		sample_width = 3;
		break;
	default:
		dev_err(component->dev, "Unsupported sample width\n");
		return -EINVAL;
	}
	if (sub->stream == SNDRV_PCM_STREAM_CAPTURE)
		regmap_update_bits(cs4234->regmap, CS4234_SAMPLE_WIDTH,
				   CS4234_SDOUTX_SW_MASK,
				   sample_width << CS4234_SDOUTX_SW_SHIFT);
	else
		regmap_update_bits(cs4234->regmap, CS4234_SAMPLE_WIDTH,
				CS4234_INPUT_SW_MASK | CS4234_LOW_LAT_SW_MASK | CS4234_DAC5_SW_MASK,
				sample_width << CS4234_INPUT_SW_SHIFT |
				sample_width << CS4234_LOW_LAT_SW_SHIFT |
				sample_width << CS4234_DAC5_SW_SHIFT);

	return ret;
}

/* Scale MCLK rate by 64 to avoid overflow in the ratnum calculation */
#define CS4234_MCLK_SCALE  64

static const struct snd_ratnum cs4234_dividers[] = {
	{
		.num = 0,
		.den_min = 256 / CS4234_MCLK_SCALE,
		.den_max = 512 / CS4234_MCLK_SCALE,
		.den_step = 128 / CS4234_MCLK_SCALE,
	},
	{
		.num = 0,
		.den_min = 128 / CS4234_MCLK_SCALE,
		.den_max = 192 / CS4234_MCLK_SCALE,
		.den_step = 64 / CS4234_MCLK_SCALE,
	},
};

static int cs4234_dai_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	struct cs4234 *cs4234 = rule->private;
	int mclk = cs4234->mclk_rate;
	struct snd_interval ranges[] = {
		{ /* Single Speed Mode */
			.min = mclk / clamp(mclk / 30000, 256, 512),
			.max = mclk / clamp(mclk / 50000, 256, 512),
		},
		{ /* Double Speed Mode */
			.min = mclk / clamp(mclk / 60000,  128, 256),
			.max = mclk / clamp(mclk / 100000, 128, 256),
		},
	};

	return snd_interval_ranges(hw_param_interval(params, rule->var),
				   ARRAY_SIZE(ranges), ranges, 0);
}

static int cs4234_dai_startup(struct snd_pcm_substream *sub, struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(comp);
	int i, ret;

	switch (cs4234->format) {
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_I2S:
		cs4234->rate_constraint.nrats = 2;

		/*
		 * Playback only supports 24-bit samples in these modes.
		 * Note: SNDRV_PCM_HW_PARAM_SAMPLE_BITS constrains the physical
		 * width, which we don't care about, so constrain the format.
		 */
		if (sub->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = snd_pcm_hw_constraint_mask64(
						sub->runtime,
						SNDRV_PCM_HW_PARAM_FORMAT,
						SNDRV_PCM_FMTBIT_S24_LE |
						SNDRV_PCM_FMTBIT_S24_3LE);
			if (ret < 0)
				return ret;

			ret = snd_pcm_hw_constraint_minmax(sub->runtime,
							   SNDRV_PCM_HW_PARAM_CHANNELS,
							   1, 4);
			if (ret < 0)
				return ret;
		}

		break;
	case SND_SOC_DAIFMT_DSP_A:
		cs4234->rate_constraint.nrats = 1;
		break;
	default:
		dev_err(comp->dev, "Startup unsupported DAI format\n");
		return -EINVAL;
	}

	for (i = 0; i < cs4234->rate_constraint.nrats; i++)
		cs4234->rate_dividers[i].num = cs4234->mclk_rate / CS4234_MCLK_SCALE;

	ret = snd_pcm_hw_constraint_ratnums(sub->runtime, 0,
					    SNDRV_PCM_HW_PARAM_RATE,
					    &cs4234->rate_constraint);
	if (ret < 0)
		return ret;

	/*
	 * MCLK/rate may be a valid ratio but out-of-spec (e.g. 24576000/64000)
	 * so this rule limits the range of sample rate for given MCLK.
	 */
	return snd_pcm_hw_rule_add(sub->runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   cs4234_dai_rule_rate, cs4234, -1);
}

static int cs4234_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				   unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct cs4234 *cs4234 = snd_soc_component_get_drvdata(component);
	unsigned int slot_offset, dac5_slot, dac5_mask_group;
	uint8_t dac5_masks[4];

	if (slot_width != 32) {
		dev_err(component->dev, "Unsupported slot width\n");
		return -EINVAL;
	}

	/* Either 4 or 5 consecutive bits, DAC5 is optional */
	slot_offset = ffs(tx_mask) - 1;
	tx_mask >>= slot_offset;
	if ((slot_offset % 4) || ((tx_mask != 0x0F) && (tx_mask != 0x1F))) {
		dev_err(component->dev, "Unsupported tx slots allocation\n");
		return -EINVAL;
	}

	regmap_update_bits(cs4234->regmap, CS4234_SP_DATA_SEL, CS4234_DAC14_SRC_MASK,
			   (slot_offset / 4) << CS4234_DAC14_SRC_SHIFT);
	regmap_update_bits(cs4234->regmap, CS4234_SP_DATA_SEL, CS4234_LL_SRC_MASK,
			   (slot_offset / 4) << CS4234_LL_SRC_SHIFT);

	if (tx_mask == 0x1F) {
		dac5_slot = slot_offset + 4;
		memset(dac5_masks, 0xFF, sizeof(dac5_masks));
		dac5_mask_group = dac5_slot / 8;
		dac5_slot %= 8;
		dac5_masks[dac5_mask_group] ^= BIT(7 - dac5_slot);
		regmap_bulk_write(cs4234->regmap,
				  CS4234_SDIN1_MASK1,
				  dac5_masks,
				  ARRAY_SIZE(dac5_masks));
	}

	return 0;
}

static const struct snd_soc_dai_ops cs4234_dai_ops = {
	.set_fmt	= cs4234_dai_set_fmt,
	.hw_params	= cs4234_dai_hw_params,
	.startup	= cs4234_dai_startup,
	.set_tdm_slot	= cs4234_dai_set_tdm_slot,
};

static struct snd_soc_dai_driver cs4234_dai[] = {
	{
		.name = "cs4234-dai",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 5,
			.rates = CS4234_PCM_RATES,
			.formats = CS4234_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = CS4234_PCM_RATES,
			.formats = CS4234_FORMATS,
		},
		.ops = &cs4234_dai_ops,
		.symmetric_rates = 1,
	},
};

static const struct reg_default cs4234_default_reg[] = {
	{ CS4234_CLOCK_SP,	 0x04},
	{ CS4234_SAMPLE_WIDTH,	 0xFF},
	{ CS4234_SP_CTRL,	 0x48},
	{ CS4234_SP_DATA_SEL,	 0x01},
	{ CS4234_SDIN1_MASK1,	 0xFF},
	{ CS4234_SDIN1_MASK2,	 0xFF},
	{ CS4234_SDIN2_MASK1,	 0xFF},
	{ CS4234_SDIN2_MASK2,	 0xFF},
	{ CS4234_TPS_CTRL,	 0x00},
	{ CS4234_ADC_CTRL1,	 0xC0},
	{ CS4234_ADC_CTRL2,	 0xFF},
	{ CS4234_LOW_LAT_CTRL1,	 0xE0},
	{ CS4234_DAC_CTRL1,	 0xE0},
	{ CS4234_DAC_CTRL2,	 0xE0},
	{ CS4234_DAC_CTRL3,	 0xBF},
	{ CS4234_DAC_CTRL4,	 0x1F},
	{ CS4234_VOLUME_MODE,	 0x87},
	{ CS4234_MASTER_VOL,	 0x10},
	{ CS4234_DAC1_VOL,	 0x10},
	{ CS4234_DAC2_VOL,	 0x10},
	{ CS4234_DAC3_VOL,	 0x10},
	{ CS4234_DAC4_VOL,	 0x10},
	{ CS4234_DAC5_VOL,	 0x10},
	{ CS4234_INT_CTRL,	 0x40},
	{ CS4234_INT_MASK1,	 0x10},
	{ CS4234_INT_MASK2,	 0x20},
};

static bool cs4234_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4234_DEVID_AB ... CS4234_DEVID_EF:
	case CS4234_REVID ... CS4234_DAC5_VOL:
	case CS4234_INT_CTRL ... CS4234_MAX_REGISTER:
		return true;
	default:
		return false;
	}
}

static bool cs4234_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4234_INT_NOTIFY1:
	case CS4234_INT_NOTIFY2:
		return true;
	default:
		return false;
	}
}

static bool cs4234_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4234_DEVID_AB ... CS4234_REVID:
	case CS4234_INT_NOTIFY1 ... CS4234_INT_NOTIFY2:
		return false;
	default:
		return true;
	}
}

static const struct snd_soc_component_driver soc_component_cs4234 = {
	.dapm_widgets		= cs4234_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4234_dapm_widgets),
	.dapm_routes		= cs4234_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4234_dapm_routes),
	.controls		= cs4234_snd_controls,
	.num_controls		= ARRAY_SIZE(cs4234_snd_controls),
	.set_bias_level		= cs4234_set_bias_level,
	.non_legacy_dai_naming	= 1,
	.idle_bias_on		= 1,
	.suspend_bias_off	= 1,
};

static const struct regmap_config cs4234_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS4234_MAX_REGISTER,
	.readable_reg = cs4234_readable_register,
	.volatile_reg = cs4234_volatile_reg,
	.writeable_reg = cs4234_writeable_register,
	.reg_defaults = cs4234_default_reg,
	.num_reg_defaults = ARRAY_SIZE(cs4234_default_reg),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static const char * const cs4234_core_supplies[] = {
	"VA",
	"VL",
};

static void cs4234_shutdown(struct cs4234 *cs4234)
{
	cancel_delayed_work_sync(&cs4234->vq_ramp_delay);
	reinit_completion(&cs4234->vq_ramp_complete);

	regmap_update_bits(cs4234->regmap, CS4234_DAC_CTRL4, CS4234_VQ_RAMP_MASK,
			   CS4234_VQ_RAMP_MASK);
	msleep(50);
	regcache_cache_only(cs4234->regmap, true);
	/* Clear VQ Ramp Bit in cache for the next PowerUp */
	regmap_update_bits(cs4234->regmap, CS4234_DAC_CTRL4, CS4234_VQ_RAMP_MASK, 0);
	gpiod_set_value_cansleep(cs4234->reset_gpio, 0);
	regulator_bulk_disable(cs4234->num_core_supplies, cs4234->core_supplies);
	clk_disable_unprepare(cs4234->mclk);
}

static int cs4234_powerup(struct cs4234 *cs4234)
{
	int ret;

	ret = clk_prepare_enable(cs4234->mclk);
	if (ret) {
		dev_err(cs4234->dev, "Failed to enable mclk: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(cs4234->num_core_supplies, cs4234->core_supplies);
	if (ret) {
		dev_err(cs4234->dev, "Failed to enable core supplies: %d\n", ret);
		clk_disable_unprepare(cs4234->mclk);
		return ret;
	}

	usleep_range(CS4234_HOLD_RESET_TIME_US, 2 * CS4234_HOLD_RESET_TIME_US);
	gpiod_set_value_cansleep(cs4234->reset_gpio, 1);

	/* Make sure hardware reset done 2 ms + (3000/MCLK) */
	usleep_range(CS4234_BOOT_TIME_US, CS4234_BOOT_TIME_US * 2);

	queue_delayed_work(system_power_efficient_wq,
			   &cs4234->vq_ramp_delay,
			   msecs_to_jiffies(CS4234_VQ_CHARGE_MS));

	return 0;
}

static int cs4234_i2c_probe(struct i2c_client *i2c_client, const struct i2c_device_id *id)
{
	struct cs4234 *cs4234;
	struct device *dev = &i2c_client->dev;
	unsigned int revid;
	uint32_t devid;
	uint8_t ids[3];
	int ret = 0, i;

	cs4234 = devm_kzalloc(dev, sizeof(*cs4234), GFP_KERNEL);
	if (!cs4234)
		return -ENOMEM;
	i2c_set_clientdata(i2c_client, cs4234);
	cs4234->dev = dev;
	init_completion(&cs4234->vq_ramp_complete);
	INIT_DELAYED_WORK(&cs4234->vq_ramp_delay, cs4234_vq_ramp_done);

	cs4234->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs4234->reset_gpio))
		return PTR_ERR(cs4234->reset_gpio);

	BUILD_BUG_ON(ARRAY_SIZE(cs4234->core_supplies) < ARRAY_SIZE(cs4234_core_supplies));

	cs4234->num_core_supplies = ARRAY_SIZE(cs4234_core_supplies);
	for (i = 0; i < ARRAY_SIZE(cs4234_core_supplies); i++)
		cs4234->core_supplies[i].supply = cs4234_core_supplies[i];

	ret = devm_regulator_bulk_get(dev, cs4234->num_core_supplies, cs4234->core_supplies);
	if (ret) {
		dev_err(dev, "Failed to request core supplies %d\n", ret);
		return ret;
	}

	cs4234->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(cs4234->mclk)) {
		ret = PTR_ERR(cs4234->mclk);
		dev_err(dev, "Failed to get the mclk: %d\n", ret);
		return ret;
	}
	cs4234->mclk_rate = clk_get_rate(cs4234->mclk);

	if (cs4234->mclk_rate < 7680000 || cs4234->mclk_rate > 25600000) {
		dev_err(dev, "Invalid Master Clock rate\n");
		return -EINVAL;
	}

	cs4234->regmap = devm_regmap_init_i2c(i2c_client, &cs4234_regmap);
	if (IS_ERR(cs4234->regmap)) {
		ret = PTR_ERR(cs4234->regmap);
		dev_err(dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ret = cs4234_powerup(cs4234);
	if (ret)
		return ret;

	ret = regmap_bulk_read(cs4234->regmap, CS4234_DEVID_AB, ids, ARRAY_SIZE(ids));
	if (ret < 0) {
		dev_err(dev, "Failed to read DEVID: %d\n", ret);
		goto fail_shutdown;
	}

	devid = (ids[0] << 16) | (ids[1] << 8) | ids[2];
	if (devid != CS4234_SUPPORTED_ID) {
		dev_err(dev, "Unknown device ID: %x\n", devid);
		ret = -EINVAL;
		goto fail_shutdown;
	}

	ret = regmap_read(cs4234->regmap, CS4234_REVID, &revid);
	if (ret < 0) {
		dev_err(dev, "Failed to read CS4234_REVID: %d\n", ret);
		goto fail_shutdown;
	}

	dev_info(dev, "Cirrus Logic CS4234, Alpha Rev: %02X, Numeric Rev: %02X\n",
		 (revid & 0xF0) >> 4, revid & 0x0F);

	ret = regulator_get_voltage(cs4234->core_supplies[CS4234_SUPPLY_VA].consumer);
	switch (ret) {
	case 3135000 ... 3650000:
		regmap_update_bits(cs4234->regmap, CS4234_ADC_CTRL1,
				   CS4234_VA_SEL_MASK,
				   CS4234_3V3 << CS4234_VA_SEL_SHIFT);
		break;
	case 4750000 ... 5250000:
		regmap_update_bits(cs4234->regmap, CS4234_ADC_CTRL1,
				   CS4234_VA_SEL_MASK,
				   CS4234_5V << CS4234_VA_SEL_SHIFT);
		break;
	default:
		dev_err(dev, "Invalid VA voltage\n");
		ret = -EINVAL;
		goto fail_shutdown;
	}

	pm_runtime_set_active(&i2c_client->dev);
	pm_runtime_enable(&i2c_client->dev);

	memcpy(&cs4234->rate_dividers, &cs4234_dividers, sizeof(cs4234_dividers));
	cs4234->rate_constraint.rats = cs4234->rate_dividers;

	ret = snd_soc_register_component(dev, &soc_component_cs4234, cs4234_dai,
					 ARRAY_SIZE(cs4234_dai));
	if (ret < 0) {
		dev_err(dev, "Failed to register component:%d\n", ret);
		pm_runtime_disable(&i2c_client->dev);
		goto fail_shutdown;
	}

	return ret;

fail_shutdown:
	cs4234_shutdown(cs4234);

	return ret;
}

static int cs4234_i2c_remove(struct i2c_client *i2c_client)
{
	struct cs4234 *cs4234 = i2c_get_clientdata(i2c_client);
	struct device *dev = &i2c_client->dev;

	snd_soc_unregister_component(dev);
	pm_runtime_disable(dev);
	cs4234_shutdown(cs4234);

	return 0;
}

static int __maybe_unused cs4234_runtime_resume(struct device *dev)
{
	struct cs4234 *cs4234 = dev_get_drvdata(dev);
	int ret;

	ret = cs4234_powerup(cs4234);
	if (ret)
		return ret;

	regcache_mark_dirty(cs4234->regmap);
	regcache_cache_only(cs4234->regmap, false);
	ret = regcache_sync(cs4234->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap: %d\n", ret);
		cs4234_shutdown(cs4234);
		return ret;
	}

	return 0;
}

static int __maybe_unused cs4234_runtime_suspend(struct device *dev)
{
	struct cs4234 *cs4234 = dev_get_drvdata(dev);

	cs4234_shutdown(cs4234);

	return 0;
}

static const struct dev_pm_ops cs4234_pm = {
	SET_RUNTIME_PM_OPS(cs4234_runtime_suspend, cs4234_runtime_resume, NULL)
};

static const struct of_device_id cs4234_of_match[] = {
	{ .compatible = "cirrus,cs4234", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs4234_of_match);

static struct i2c_driver cs4234_i2c_driver = {
	.driver = {
		.name = "cs4234",
		.pm = &cs4234_pm,
		.of_match_table = cs4234_of_match,
	},
	.probe =	cs4234_i2c_probe,
	.remove =	cs4234_i2c_remove,
};
module_i2c_driver(cs4234_i2c_driver);

MODULE_DESCRIPTION("ASoC Cirrus Logic CS4234 driver");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
