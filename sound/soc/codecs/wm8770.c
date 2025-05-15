// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8770.c  --  WM8770 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8770.h"

#define WM8770_NUM_SUPPLIES 3
static const char *wm8770_supply_names[WM8770_NUM_SUPPLIES] = {
	"AVDD1",
	"AVDD2",
	"DVDD"
};

static const struct reg_default wm8770_reg_defaults[] = {
	{  0, 0x7f },
	{  1, 0x7f },
	{  2, 0x7f },
	{  3, 0x7f },
	{  4, 0x7f },
	{  5, 0x7f },
	{  6, 0x7f },
	{  7, 0x7f },
	{  8, 0x7f },
	{  9, 0xff },
	{ 10, 0xff },
	{ 11, 0xff },
	{ 12, 0xff },
	{ 13, 0xff },
	{ 14, 0xff },
	{ 15, 0xff },
	{ 16, 0xff },
	{ 17, 0xff },
	{ 18, 0    },
	{ 19, 0x90 },
	{ 20, 0    },
	{ 21, 0    },
	{ 22, 0x22 },
	{ 23, 0x22 },
	{ 24, 0x3e },
	{ 25, 0xc  },
	{ 26, 0xc  },
	{ 27, 0x100 },
	{ 28, 0x189 },
	{ 29, 0x189 },
	{ 30, 0x8770 },
};

static bool wm8770_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8770_RESET:
		return true;
	default:
		return false;
	}
}

struct wm8770_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8770_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8770_NUM_SUPPLIES];
	struct snd_soc_component *component;
	int sysclk;
};

static int vout12supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event);
static int vout34supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event);

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8770_REGULATOR_EVENT(n) \
static int wm8770_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct wm8770_priv *wm8770 = container_of(nb, struct wm8770_priv, \
				     disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(wm8770->regmap);	\
	} \
	return 0; \
}

WM8770_REGULATOR_EVENT(0)
WM8770_REGULATOR_EVENT(1)
WM8770_REGULATOR_EVENT(2)

static const DECLARE_TLV_DB_SCALE(adc_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(dac_dig_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_alg_tlv, -12700, 100, 1);

static const char *dac_phase_text[][2] = {
	{ "DAC1 Normal", "DAC1 Inverted" },
	{ "DAC2 Normal", "DAC2 Inverted" },
	{ "DAC3 Normal", "DAC3 Inverted" },
	{ "DAC4 Normal", "DAC4 Inverted" },
};

static const struct soc_enum dac_phase[] = {
	SOC_ENUM_DOUBLE(WM8770_DACPHASE, 0, 1, 2, dac_phase_text[0]),
	SOC_ENUM_DOUBLE(WM8770_DACPHASE, 2, 3, 2, dac_phase_text[1]),
	SOC_ENUM_DOUBLE(WM8770_DACPHASE, 4, 5, 2, dac_phase_text[2]),
	SOC_ENUM_DOUBLE(WM8770_DACPHASE, 6, 7, 2, dac_phase_text[3]),
};

static const struct snd_kcontrol_new wm8770_snd_controls[] = {
	/* global DAC playback controls */
	SOC_SINGLE_TLV("DAC Playback Volume", WM8770_MSDIGVOL, 0, 255, 0,
		dac_dig_tlv),
	SOC_SINGLE("DAC Playback Switch", WM8770_DACMUTE, 4, 1, 1),
	SOC_SINGLE("DAC Playback ZC Switch", WM8770_DACCTRL1, 0, 1, 0),

	/* global VOUT playback controls */
	SOC_SINGLE_TLV("VOUT Playback Volume", WM8770_MSALGVOL, 0, 127, 0,
		dac_alg_tlv),
	SOC_SINGLE("VOUT Playback ZC Switch", WM8770_MSALGVOL, 7, 1, 0),

	/* VOUT1/2/3/4 specific controls */
	SOC_DOUBLE_R_TLV("VOUT1 Playback Volume", WM8770_VOUT1LVOL,
		WM8770_VOUT1RVOL, 0, 127, 0, dac_alg_tlv),
	SOC_DOUBLE_R("VOUT1 Playback ZC Switch", WM8770_VOUT1LVOL,
		WM8770_VOUT1RVOL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("VOUT2 Playback Volume", WM8770_VOUT2LVOL,
		WM8770_VOUT2RVOL, 0, 127, 0, dac_alg_tlv),
	SOC_DOUBLE_R("VOUT2 Playback ZC Switch", WM8770_VOUT2LVOL,
		WM8770_VOUT2RVOL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("VOUT3 Playback Volume", WM8770_VOUT3LVOL,
		WM8770_VOUT3RVOL, 0, 127, 0, dac_alg_tlv),
	SOC_DOUBLE_R("VOUT3 Playback ZC Switch", WM8770_VOUT3LVOL,
		WM8770_VOUT3RVOL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("VOUT4 Playback Volume", WM8770_VOUT4LVOL,
		WM8770_VOUT4RVOL, 0, 127, 0, dac_alg_tlv),
	SOC_DOUBLE_R("VOUT4 Playback ZC Switch", WM8770_VOUT4LVOL,
		WM8770_VOUT4RVOL, 7, 1, 0),

	/* DAC1/2/3/4 specific controls */
	SOC_DOUBLE_R_TLV("DAC1 Playback Volume", WM8770_DAC1LVOL,
		WM8770_DAC1RVOL, 0, 255, 0, dac_dig_tlv),
	SOC_SINGLE("DAC1 Deemphasis Switch", WM8770_DACCTRL2, 0, 1, 0),
	SOC_ENUM("DAC1 Phase", dac_phase[0]),
	SOC_DOUBLE_R_TLV("DAC2 Playback Volume", WM8770_DAC2LVOL,
		WM8770_DAC2RVOL, 0, 255, 0, dac_dig_tlv),
	SOC_SINGLE("DAC2 Deemphasis Switch", WM8770_DACCTRL2, 1, 1, 0),
	SOC_ENUM("DAC2 Phase", dac_phase[1]),
	SOC_DOUBLE_R_TLV("DAC3 Playback Volume", WM8770_DAC3LVOL,
		WM8770_DAC3RVOL, 0, 255, 0, dac_dig_tlv),
	SOC_SINGLE("DAC3 Deemphasis Switch", WM8770_DACCTRL2, 2, 1, 0),
	SOC_ENUM("DAC3 Phase", dac_phase[2]),
	SOC_DOUBLE_R_TLV("DAC4 Playback Volume", WM8770_DAC4LVOL,
		WM8770_DAC4RVOL, 0, 255, 0, dac_dig_tlv),
	SOC_SINGLE("DAC4 Deemphasis Switch", WM8770_DACCTRL2, 3, 1, 0),
	SOC_ENUM("DAC4 Phase", dac_phase[3]),

	/* ADC specific controls */
	SOC_DOUBLE_R_TLV("Capture Volume", WM8770_ADCLCTRL, WM8770_ADCRCTRL,
		0, 31, 0, adc_tlv),
	SOC_DOUBLE_R("Capture Switch", WM8770_ADCLCTRL, WM8770_ADCRCTRL,
		5, 1, 1),

	/* other controls */
	SOC_SINGLE("ADC 128x Oversampling Switch", WM8770_MSTRCTRL, 3, 1, 0),
	SOC_SINGLE("ADC Highpass Filter Switch", WM8770_IFACECTRL, 8, 1, 1)
};

static const char *ain_text[] = {
	"AIN1", "AIN2", "AIN3", "AIN4",
	"AIN5", "AIN6", "AIN7", "AIN8"
};

static SOC_ENUM_DOUBLE_DECL(ain_enum,
			    WM8770_ADCMUX, 0, 4, ain_text);

static const struct snd_kcontrol_new ain_mux =
	SOC_DAPM_ENUM("Capture Mux", ain_enum);

static const struct snd_kcontrol_new vout1_mix_controls[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", WM8770_OUTMUX1, 0, 1, 0),
	SOC_DAPM_SINGLE("AUX1 Switch", WM8770_OUTMUX1, 1, 1, 0),
	SOC_DAPM_SINGLE("Bypass Switch", WM8770_OUTMUX1, 2, 1, 0)
};

static const struct snd_kcontrol_new vout2_mix_controls[] = {
	SOC_DAPM_SINGLE("DAC2 Switch", WM8770_OUTMUX1, 3, 1, 0),
	SOC_DAPM_SINGLE("AUX2 Switch", WM8770_OUTMUX1, 4, 1, 0),
	SOC_DAPM_SINGLE("Bypass Switch", WM8770_OUTMUX1, 5, 1, 0)
};

static const struct snd_kcontrol_new vout3_mix_controls[] = {
	SOC_DAPM_SINGLE("DAC3 Switch", WM8770_OUTMUX2, 0, 1, 0),
	SOC_DAPM_SINGLE("AUX3 Switch", WM8770_OUTMUX2, 1, 1, 0),
	SOC_DAPM_SINGLE("Bypass Switch", WM8770_OUTMUX2, 2, 1, 0)
};

static const struct snd_kcontrol_new vout4_mix_controls[] = {
	SOC_DAPM_SINGLE("DAC4 Switch", WM8770_OUTMUX2, 3, 1, 0),
	SOC_DAPM_SINGLE("Bypass Switch", WM8770_OUTMUX2, 4, 1, 0)
};

static const struct snd_soc_dapm_widget wm8770_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("AUX1"),
	SND_SOC_DAPM_INPUT("AUX2"),
	SND_SOC_DAPM_INPUT("AUX3"),

	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),
	SND_SOC_DAPM_INPUT("AIN4"),
	SND_SOC_DAPM_INPUT("AIN5"),
	SND_SOC_DAPM_INPUT("AIN6"),
	SND_SOC_DAPM_INPUT("AIN7"),
	SND_SOC_DAPM_INPUT("AIN8"),

	SND_SOC_DAPM_MUX("Capture Mux", WM8770_ADCMUX, 8, 1, &ain_mux),

	SND_SOC_DAPM_ADC("ADC", "Capture", WM8770_PWDNCTRL, 1, 1),

	SND_SOC_DAPM_DAC("DAC1", "Playback", WM8770_PWDNCTRL, 2, 1),
	SND_SOC_DAPM_DAC("DAC2", "Playback", WM8770_PWDNCTRL, 3, 1),
	SND_SOC_DAPM_DAC("DAC3", "Playback", WM8770_PWDNCTRL, 4, 1),
	SND_SOC_DAPM_DAC("DAC4", "Playback", WM8770_PWDNCTRL, 5, 1),

	SND_SOC_DAPM_SUPPLY("VOUT12 Supply", SND_SOC_NOPM, 0, 0,
		vout12supply_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("VOUT34 Supply", SND_SOC_NOPM, 0, 0,
		vout34supply_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("VOUT1 Mixer", SND_SOC_NOPM, 0, 0,
		vout1_mix_controls, ARRAY_SIZE(vout1_mix_controls)),
	SND_SOC_DAPM_MIXER("VOUT2 Mixer", SND_SOC_NOPM, 0, 0,
		vout2_mix_controls, ARRAY_SIZE(vout2_mix_controls)),
	SND_SOC_DAPM_MIXER("VOUT3 Mixer", SND_SOC_NOPM, 0, 0,
		vout3_mix_controls, ARRAY_SIZE(vout3_mix_controls)),
	SND_SOC_DAPM_MIXER("VOUT4 Mixer", SND_SOC_NOPM, 0, 0,
		vout4_mix_controls, ARRAY_SIZE(vout4_mix_controls)),

	SND_SOC_DAPM_OUTPUT("VOUT1"),
	SND_SOC_DAPM_OUTPUT("VOUT2"),
	SND_SOC_DAPM_OUTPUT("VOUT3"),
	SND_SOC_DAPM_OUTPUT("VOUT4")
};

static const struct snd_soc_dapm_route wm8770_intercon[] = {
	{ "Capture Mux", "AIN1", "AIN1" },
	{ "Capture Mux", "AIN2", "AIN2" },
	{ "Capture Mux", "AIN3", "AIN3" },
	{ "Capture Mux", "AIN4", "AIN4" },
	{ "Capture Mux", "AIN5", "AIN5" },
	{ "Capture Mux", "AIN6", "AIN6" },
	{ "Capture Mux", "AIN7", "AIN7" },
	{ "Capture Mux", "AIN8", "AIN8" },

	{ "ADC", NULL, "Capture Mux" },

	{ "VOUT1 Mixer", NULL, "VOUT12 Supply" },
	{ "VOUT1 Mixer", "DAC1 Switch", "DAC1" },
	{ "VOUT1 Mixer", "AUX1 Switch", "AUX1" },
	{ "VOUT1 Mixer", "Bypass Switch", "Capture Mux" },

	{ "VOUT2 Mixer", NULL, "VOUT12 Supply" },
	{ "VOUT2 Mixer", "DAC2 Switch", "DAC2" },
	{ "VOUT2 Mixer", "AUX2 Switch", "AUX2" },
	{ "VOUT2 Mixer", "Bypass Switch", "Capture Mux" },

	{ "VOUT3 Mixer", NULL, "VOUT34 Supply" },
	{ "VOUT3 Mixer", "DAC3 Switch", "DAC3" },
	{ "VOUT3 Mixer", "AUX3 Switch", "AUX3" },
	{ "VOUT3 Mixer", "Bypass Switch", "Capture Mux" },

	{ "VOUT4 Mixer", NULL, "VOUT34 Supply" },
	{ "VOUT4 Mixer", "DAC4 Switch", "DAC4" },
	{ "VOUT4 Mixer", "Bypass Switch", "Capture Mux" },

	{ "VOUT1", NULL, "VOUT1 Mixer" },
	{ "VOUT2", NULL, "VOUT2 Mixer" },
	{ "VOUT3", NULL, "VOUT3 Mixer" },
	{ "VOUT4", NULL, "VOUT4 Mixer" }
};

static int vout12supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, WM8770_OUTMUX1, 0x180, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, WM8770_OUTMUX1, 0x180, 0x180);
		break;
	}

	return 0;
}

static int vout34supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, WM8770_OUTMUX2, 0x180, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, WM8770_OUTMUX2, 0x180, 0x180);
		break;
	}

	return 0;
}

static int wm8770_reset(struct snd_soc_component *component)
{
	return snd_soc_component_write(component, WM8770_RESET, 0);
}

static int wm8770_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component;
	int iface, master;

	component = dai->component;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		master = 0x100;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		master = 0;
		break;
	default:
		return -EINVAL;
	}

	iface = 0;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0xc;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x8;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x4;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, WM8770_IFACECTRL, 0xf, iface);
	snd_soc_component_update_bits(component, WM8770_MSTRCTRL, 0x100, master);

	return 0;
}

static const int mclk_ratios[] = {
	128,
	192,
	256,
	384,
	512,
	768
};

static int wm8770_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component;
	struct wm8770_priv *wm8770;
	int i;
	int iface;
	int shift;
	int ratio;

	component = dai->component;
	wm8770 = snd_soc_component_get_drvdata(component);

	iface = 0;
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface |= 0x10;
		break;
	case 24:
		iface |= 0x20;
		break;
	case 32:
		iface |= 0x30;
		break;
	}

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		i = 0;
		shift = 4;
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		i = 2;
		shift = 0;
		break;
	default:
		return -EINVAL;
	}

	/* Only need to set MCLK/LRCLK ratio if we're master */
	if (snd_soc_component_read(component, WM8770_MSTRCTRL) & 0x100) {
		for (; i < ARRAY_SIZE(mclk_ratios); ++i) {
			ratio = wm8770->sysclk / params_rate(params);
			if (ratio == mclk_ratios[i])
				break;
		}

		if (i == ARRAY_SIZE(mclk_ratios)) {
			dev_err(component->dev,
				"Unable to configure MCLK ratio %d/%d\n",
				wm8770->sysclk, params_rate(params));
			return -EINVAL;
		}

		dev_dbg(component->dev, "MCLK is %dfs\n", mclk_ratios[i]);

		snd_soc_component_update_bits(component, WM8770_MSTRCTRL, 0x7 << shift,
				    i << shift);
	}

	snd_soc_component_update_bits(component, WM8770_IFACECTRL, 0x30, iface);

	return 0;
}

static int wm8770_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component;

	component = dai->component;
	return snd_soc_component_update_bits(component, WM8770_DACMUTE, 0x10,
				   !!mute << 4);
}

static int wm8770_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component;
	struct wm8770_priv *wm8770;

	component = dai->component;
	wm8770 = snd_soc_component_get_drvdata(component);
	wm8770->sysclk = freq;
	return 0;
}

static int wm8770_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	int ret;
	struct wm8770_priv *wm8770;

	wm8770 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8770->supplies),
						    wm8770->supplies);
			if (ret) {
				dev_err(component->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			regcache_sync(wm8770->regmap);

			/* global powerup */
			snd_soc_component_write(component, WM8770_PWDNCTRL, 0);
		}
		break;
	case SND_SOC_BIAS_OFF:
		/* global powerdown */
		snd_soc_component_write(component, WM8770_PWDNCTRL, 1);
		regulator_bulk_disable(ARRAY_SIZE(wm8770->supplies),
				       wm8770->supplies);
		break;
	}

	return 0;
}

#define WM8770_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8770_dai_ops = {
	.mute_stream = wm8770_mute,
	.hw_params = wm8770_hw_params,
	.set_fmt = wm8770_set_fmt,
	.set_sysclk = wm8770_set_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8770_dai = {
	.name = "wm8770-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = WM8770_FORMATS
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = WM8770_FORMATS
	},
	.ops = &wm8770_dai_ops,
	.symmetric_rate = 1
};

static int wm8770_probe(struct snd_soc_component *component)
{
	struct wm8770_priv *wm8770;
	int ret;

	wm8770 = snd_soc_component_get_drvdata(component);
	wm8770->component = component;

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8770->supplies),
				    wm8770->supplies);
	if (ret) {
		dev_err(component->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = wm8770_reset(component);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset: %d\n", ret);
		goto err_reg_enable;
	}

	/* latch the volume update bits */
	snd_soc_component_update_bits(component, WM8770_MSDIGVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_MSALGVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_VOUT1RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_VOUT2RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_VOUT3RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_VOUT4RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_DAC1RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_DAC2RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_DAC3RVOL, 0x100, 0x100);
	snd_soc_component_update_bits(component, WM8770_DAC4RVOL, 0x100, 0x100);

	/* mute all DACs */
	snd_soc_component_update_bits(component, WM8770_DACMUTE, 0x10, 0x10);

err_reg_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8770->supplies), wm8770->supplies);
	return ret;
}

static const struct snd_soc_component_driver soc_component_dev_wm8770 = {
	.probe			= wm8770_probe,
	.set_bias_level		= wm8770_set_bias_level,
	.controls		= wm8770_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8770_snd_controls),
	.dapm_widgets		= wm8770_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8770_dapm_widgets),
	.dapm_routes		= wm8770_intercon,
	.num_dapm_routes	= ARRAY_SIZE(wm8770_intercon),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct of_device_id wm8770_of_match[] = {
	{ .compatible = "wlf,wm8770", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8770_of_match);

static const struct regmap_config wm8770_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8770_RESET,

	.reg_defaults = wm8770_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8770_reg_defaults),
	.cache_type = REGCACHE_MAPLE,

	.volatile_reg = wm8770_volatile_reg,
};

static int wm8770_spi_probe(struct spi_device *spi)
{
	struct wm8770_priv *wm8770;
	int ret, i;

	wm8770 = devm_kzalloc(&spi->dev, sizeof(struct wm8770_priv),
			      GFP_KERNEL);
	if (!wm8770)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(wm8770->supplies); i++)
		wm8770->supplies[i].supply = wm8770_supply_names[i];

	ret = devm_regulator_bulk_get(&spi->dev, ARRAY_SIZE(wm8770->supplies),
				      wm8770->supplies);
	if (ret) {
		dev_err(&spi->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	wm8770->disable_nb[0].notifier_call = wm8770_regulator_event_0;
	wm8770->disable_nb[1].notifier_call = wm8770_regulator_event_1;
	wm8770->disable_nb[2].notifier_call = wm8770_regulator_event_2;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8770->supplies); i++) {
		ret = devm_regulator_register_notifier(
						wm8770->supplies[i].consumer,
						&wm8770->disable_nb[i]);
		if (ret) {
			dev_err(&spi->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	wm8770->regmap = devm_regmap_init_spi(spi, &wm8770_regmap);
	if (IS_ERR(wm8770->regmap))
		return PTR_ERR(wm8770->regmap);

	spi_set_drvdata(spi, wm8770);

	ret = devm_snd_soc_register_component(&spi->dev,
				     &soc_component_dev_wm8770, &wm8770_dai, 1);

	return ret;
}

static struct spi_driver wm8770_spi_driver = {
	.driver = {
		.name = "wm8770",
		.of_match_table = wm8770_of_match,
	},
	.probe = wm8770_spi_probe,
};

module_spi_driver(wm8770_spi_driver);

MODULE_DESCRIPTION("ASoC WM8770 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
