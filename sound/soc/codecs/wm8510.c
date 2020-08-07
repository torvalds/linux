// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8510.c  --  WM8510 ALSA Soc Audio driver
 *
 * Copyright 2006 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "wm8510.h"

/*
 * wm8510 register cache
 * We can't read the WM8510 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const struct reg_default wm8510_reg_defaults[] = {
	{  1, 0x0000 },
	{  2, 0x0000 },
	{  3, 0x0000 },
	{  4, 0x0050 },
	{  5, 0x0000 },
	{  6, 0x0140 },
	{  7, 0x0000 },
	{  8, 0x0000 },
	{  9, 0x0000 },
	{ 10, 0x0000 },
	{ 11, 0x00ff },
	{ 12, 0x0000 },
	{ 13, 0x0000 },
	{ 14, 0x0100 },
	{ 15, 0x00ff },
	{ 16, 0x0000 },
	{ 17, 0x0000 },
	{ 18, 0x012c },
	{ 19, 0x002c },
	{ 20, 0x002c },
	{ 21, 0x002c },
	{ 22, 0x002c },
	{ 23, 0x0000 },
	{ 24, 0x0032 },
	{ 25, 0x0000 },
	{ 26, 0x0000 },
	{ 27, 0x0000 },
	{ 28, 0x0000 },
	{ 29, 0x0000 },
	{ 30, 0x0000 },
	{ 31, 0x0000 },
	{ 32, 0x0038 },
	{ 33, 0x000b },
	{ 34, 0x0032 },
	{ 35, 0x0000 },
	{ 36, 0x0008 },
	{ 37, 0x000c },
	{ 38, 0x0093 },
	{ 39, 0x00e9 },
	{ 40, 0x0000 },
	{ 41, 0x0000 },
	{ 42, 0x0000 },
	{ 43, 0x0000 },
	{ 44, 0x0003 },
	{ 45, 0x0010 },
	{ 46, 0x0000 },
	{ 47, 0x0000 },
	{ 48, 0x0000 },
	{ 49, 0x0002 },
	{ 50, 0x0001 },
	{ 51, 0x0000 },
	{ 52, 0x0000 },
	{ 53, 0x0000 },
	{ 54, 0x0039 },
	{ 55, 0x0000 },
	{ 56, 0x0001 },
};

static bool wm8510_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8510_RESET:
		return true;
	default:
		return false;
	}
}

#define WM8510_POWER1_BIASEN  0x08
#define WM8510_POWER1_BUFIOEN 0x10

#define wm8510_reset(c)	snd_soc_component_write(c, WM8510_RESET, 0)

/* codec private data */
struct wm8510_priv {
	struct regmap *regmap;
};

static const char *wm8510_companding[] = { "Off", "NC", "u-law", "A-law" };
static const char *wm8510_deemp[] = { "None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8510_alc[] = { "ALC", "Limiter" };

static const struct soc_enum wm8510_enum[] = {
	SOC_ENUM_SINGLE(WM8510_COMP, 1, 4, wm8510_companding), /* adc */
	SOC_ENUM_SINGLE(WM8510_COMP, 3, 4, wm8510_companding), /* dac */
	SOC_ENUM_SINGLE(WM8510_DAC,  4, 4, wm8510_deemp),
	SOC_ENUM_SINGLE(WM8510_ALC3,  8, 2, wm8510_alc),
};

static const struct snd_kcontrol_new wm8510_snd_controls[] = {

SOC_SINGLE("Digital Loopback Switch", WM8510_COMP, 0, 1, 0),

SOC_ENUM("DAC Companding", wm8510_enum[1]),
SOC_ENUM("ADC Companding", wm8510_enum[0]),

SOC_ENUM("Playback De-emphasis", wm8510_enum[2]),
SOC_SINGLE("DAC Inversion Switch", WM8510_DAC, 0, 1, 0),

SOC_SINGLE("Master Playback Volume", WM8510_DACVOL, 0, 127, 0),

SOC_SINGLE("High Pass Filter Switch", WM8510_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8510_ADC, 4, 7, 0),
SOC_SINGLE("ADC Inversion Switch", WM8510_COMP, 0, 1, 0),

SOC_SINGLE("Capture Volume", WM8510_ADCVOL,  0, 127, 0),

SOC_SINGLE("DAC Playback Limiter Switch", WM8510_DACLIM1,  8, 1, 0),
SOC_SINGLE("DAC Playback Limiter Decay", WM8510_DACLIM1,  4, 15, 0),
SOC_SINGLE("DAC Playback Limiter Attack", WM8510_DACLIM1,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Threshold", WM8510_DACLIM2,  4, 7, 0),
SOC_SINGLE("DAC Playback Limiter Boost", WM8510_DACLIM2,  0, 15, 0),

SOC_SINGLE("ALC Enable Switch", WM8510_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8510_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8510_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8510_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8510_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8510_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8510_enum[3]),
SOC_SINGLE("ALC Capture Decay", WM8510_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8510_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8510_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8510_NGATE,  0, 7, 0),

SOC_SINGLE("Capture PGA ZC Switch", WM8510_INPPGA,  7, 1, 0),
SOC_SINGLE("Capture PGA Volume", WM8510_INPPGA,  0, 63, 0),

SOC_SINGLE("Speaker Playback ZC Switch", WM8510_SPKVOL,  7, 1, 0),
SOC_SINGLE("Speaker Playback Switch", WM8510_SPKVOL,  6, 1, 1),
SOC_SINGLE("Speaker Playback Volume", WM8510_SPKVOL,  0, 63, 0),
SOC_SINGLE("Speaker Boost", WM8510_OUTPUT, 2, 1, 0),

SOC_SINGLE("Capture Boost(+20dB)", WM8510_ADCBOOST,  8, 1, 0),
SOC_SINGLE("Mono Playback Switch", WM8510_MONOMIX, 6, 1, 1),
};

/* Speaker Output Mixer */
static const struct snd_kcontrol_new wm8510_speaker_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8510_SPKMIX, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8510_SPKMIX, 5, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", WM8510_SPKMIX, 0, 1, 0),
};

/* Mono Output Mixer */
static const struct snd_kcontrol_new wm8510_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8510_MONOMIX, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8510_MONOMIX, 2, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", WM8510_MONOMIX, 0, 1, 0),
};

static const struct snd_kcontrol_new wm8510_boost_controls[] = {
SOC_DAPM_SINGLE("Mic PGA Switch", WM8510_INPPGA,  6, 1, 1),
SOC_DAPM_SINGLE("Aux Volume", WM8510_ADCBOOST, 0, 7, 0),
SOC_DAPM_SINGLE("Mic Volume", WM8510_ADCBOOST, 4, 7, 0),
};

static const struct snd_kcontrol_new wm8510_micpga_controls[] = {
SOC_DAPM_SINGLE("MICP Switch", WM8510_INPUT, 0, 1, 0),
SOC_DAPM_SINGLE("MICN Switch", WM8510_INPUT, 1, 1, 0),
SOC_DAPM_SINGLE("AUX Switch", WM8510_INPUT, 2, 1, 0),
};

static const struct snd_soc_dapm_widget wm8510_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Speaker Mixer", WM8510_POWER3, 2, 0,
	&wm8510_speaker_mixer_controls[0],
	ARRAY_SIZE(wm8510_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("Mono Mixer", WM8510_POWER3, 3, 0,
	&wm8510_mono_mixer_controls[0],
	ARRAY_SIZE(wm8510_mono_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8510_POWER3, 0, 0),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8510_POWER2, 0, 0),
SND_SOC_DAPM_PGA("Aux Input", WM8510_POWER1, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkN Out", WM8510_POWER3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkP Out", WM8510_POWER3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mono Out", WM8510_POWER3, 7, 0, NULL, 0),

SND_SOC_DAPM_MIXER("Mic PGA", WM8510_POWER2, 2, 0,
		   &wm8510_micpga_controls[0],
		   ARRAY_SIZE(wm8510_micpga_controls)),
SND_SOC_DAPM_MIXER("Boost Mixer", WM8510_POWER2, 4, 0,
	&wm8510_boost_controls[0],
	ARRAY_SIZE(wm8510_boost_controls)),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8510_POWER1, 4, 0),

SND_SOC_DAPM_INPUT("MICN"),
SND_SOC_DAPM_INPUT("MICP"),
SND_SOC_DAPM_INPUT("AUX"),
SND_SOC_DAPM_OUTPUT("MONOOUT"),
SND_SOC_DAPM_OUTPUT("SPKOUTP"),
SND_SOC_DAPM_OUTPUT("SPKOUTN"),
};

static const struct snd_soc_dapm_route wm8510_dapm_routes[] = {
	/* Mono output mixer */
	{"Mono Mixer", "PCM Playback Switch", "DAC"},
	{"Mono Mixer", "Aux Playback Switch", "Aux Input"},
	{"Mono Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Speaker output mixer */
	{"Speaker Mixer", "PCM Playback Switch", "DAC"},
	{"Speaker Mixer", "Aux Playback Switch", "Aux Input"},
	{"Speaker Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Outputs */
	{"Mono Out", NULL, "Mono Mixer"},
	{"MONOOUT", NULL, "Mono Out"},
	{"SpkN Out", NULL, "Speaker Mixer"},
	{"SpkP Out", NULL, "Speaker Mixer"},
	{"SPKOUTN", NULL, "SpkN Out"},
	{"SPKOUTP", NULL, "SpkP Out"},

	/* Microphone PGA */
	{"Mic PGA", "MICN Switch", "MICN"},
	{"Mic PGA", "MICP Switch", "MICP"},
	{ "Mic PGA", "AUX Switch", "Aux Input" },

	/* Boost Mixer */
	{"Boost Mixer", "Mic PGA Switch", "Mic PGA"},
	{"Boost Mixer", "Mic Volume", "MICP"},
	{"Boost Mixer", "Aux Volume", "Aux Input"},

	{"ADC", NULL, "Boost Mixer"},
};

struct pll_ {
	unsigned int pre_div:4; /* prescale - 1 */
	unsigned int n:4;
	unsigned int k;
};

static struct pll_ pll_div;

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 24) * 10)

static void pll_factors(unsigned int target, unsigned int source)
{
	unsigned long long Kpart;
	unsigned int K, Ndiv, Nmod;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div.pre_div = 1;
		Ndiv = target / source;
	} else
		pll_div.pre_div = 0;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
			"WM8510 N value %u outwith recommended range!d\n",
			Ndiv);

	pll_div.n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div.k = K;
}

static int wm8510_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 reg;

	if (freq_in == 0 || freq_out == 0) {
		/* Clock CODEC directly from MCLK */
		reg = snd_soc_component_read(component, WM8510_CLOCK);
		snd_soc_component_write(component, WM8510_CLOCK, reg & 0x0ff);

		/* Turn off PLL */
		reg = snd_soc_component_read(component, WM8510_POWER1);
		snd_soc_component_write(component, WM8510_POWER1, reg & 0x1df);
		return 0;
	}

	pll_factors(freq_out*4, freq_in);

	snd_soc_component_write(component, WM8510_PLLN, (pll_div.pre_div << 4) | pll_div.n);
	snd_soc_component_write(component, WM8510_PLLK1, pll_div.k >> 18);
	snd_soc_component_write(component, WM8510_PLLK2, (pll_div.k >> 9) & 0x1ff);
	snd_soc_component_write(component, WM8510_PLLK3, pll_div.k & 0x1ff);
	reg = snd_soc_component_read(component, WM8510_POWER1);
	snd_soc_component_write(component, WM8510_POWER1, reg | 0x020);

	/* Run CODEC from PLL instead of MCLK */
	reg = snd_soc_component_read(component, WM8510_CLOCK);
	snd_soc_component_write(component, WM8510_CLOCK, reg | 0x100);

	return 0;
}

/*
 * Configure WM8510 clock dividers.
 */
static int wm8510_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 reg;

	switch (div_id) {
	case WM8510_OPCLKDIV:
		reg = snd_soc_component_read(component, WM8510_GPIO) & 0x1cf;
		snd_soc_component_write(component, WM8510_GPIO, reg | div);
		break;
	case WM8510_MCLKDIV:
		reg = snd_soc_component_read(component, WM8510_CLOCK) & 0x11f;
		snd_soc_component_write(component, WM8510_CLOCK, reg | div);
		break;
	case WM8510_ADCCLK:
		reg = snd_soc_component_read(component, WM8510_ADC) & 0x1f7;
		snd_soc_component_write(component, WM8510_ADC, reg | div);
		break;
	case WM8510_DACCLK:
		reg = snd_soc_component_read(component, WM8510_DAC) & 0x1f7;
		snd_soc_component_write(component, WM8510_DAC, reg | div);
		break;
	case WM8510_BCLKDIV:
		reg = snd_soc_component_read(component, WM8510_CLOCK) & 0x1e3;
		snd_soc_component_write(component, WM8510_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8510_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 iface = 0;
	u16 clk = snd_soc_component_read(component, WM8510_CLOCK) & 0x1fe;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 0x0001;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x00018;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0080;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8510_IFACE, iface);
	snd_soc_component_write(component, WM8510_CLOCK, clk);
	return 0;
}

static int wm8510_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u16 iface = snd_soc_component_read(component, WM8510_IFACE) & 0x19f;
	u16 adn = snd_soc_component_read(component, WM8510_ADD) & 0x1f1;

	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface |= 0x0020;
		break;
	case 24:
		iface |= 0x0040;
		break;
	case 32:
		iface |= 0x0060;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case 8000:
		adn |= 0x5 << 1;
		break;
	case 11025:
		adn |= 0x4 << 1;
		break;
	case 16000:
		adn |= 0x3 << 1;
		break;
	case 22050:
		adn |= 0x2 << 1;
		break;
	case 32000:
		adn |= 0x1 << 1;
		break;
	case 44100:
	case 48000:
		break;
	}

	snd_soc_component_write(component, WM8510_IFACE, iface);
	snd_soc_component_write(component, WM8510_ADD, adn);
	return 0;
}

static int wm8510_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	u16 mute_reg = snd_soc_component_read(component, WM8510_DAC) & 0xffbf;

	if (mute)
		snd_soc_component_write(component, WM8510_DAC, mute_reg | 0x40);
	else
		snd_soc_component_write(component, WM8510_DAC, mute_reg);
	return 0;
}

/* liam need to make this lower power with dapm */
static int wm8510_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct wm8510_priv *wm8510 = snd_soc_component_get_drvdata(component);
	u16 power1 = snd_soc_component_read(component, WM8510_POWER1) & ~0x3;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		power1 |= 0x1;  /* VMID 50k */
		snd_soc_component_write(component, WM8510_POWER1, power1);
		break;

	case SND_SOC_BIAS_STANDBY:
		power1 |= WM8510_POWER1_BIASEN | WM8510_POWER1_BUFIOEN;

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_sync(wm8510->regmap);

			/* Initial cap charge at VMID 5k */
			snd_soc_component_write(component, WM8510_POWER1, power1 | 0x3);
			mdelay(100);
		}

		power1 |= 0x2;  /* VMID 500k */
		snd_soc_component_write(component, WM8510_POWER1, power1);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component, WM8510_POWER1, 0);
		snd_soc_component_write(component, WM8510_POWER2, 0);
		snd_soc_component_write(component, WM8510_POWER3, 0);
		break;
	}

	return 0;
}

#define WM8510_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define WM8510_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8510_dai_ops = {
	.hw_params	= wm8510_pcm_hw_params,
	.mute_stream	= wm8510_mute,
	.set_fmt	= wm8510_set_dai_fmt,
	.set_clkdiv	= wm8510_set_dai_clkdiv,
	.set_pll	= wm8510_set_dai_pll,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8510_dai = {
	.name = "wm8510-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8510_RATES,
		.formats = WM8510_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8510_RATES,
		.formats = WM8510_FORMATS,},
	.ops = &wm8510_dai_ops,
	.symmetric_rates = 1,
};

static int wm8510_probe(struct snd_soc_component *component)
{
	wm8510_reset(component);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8510 = {
	.probe			= wm8510_probe,
	.set_bias_level		= wm8510_set_bias_level,
	.controls		= wm8510_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8510_snd_controls),
	.dapm_widgets		= wm8510_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8510_dapm_widgets),
	.dapm_routes		= wm8510_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8510_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct of_device_id wm8510_of_match[] = {
	{ .compatible = "wlf,wm8510" },
	{ },
};
MODULE_DEVICE_TABLE(of, wm8510_of_match);

static const struct regmap_config wm8510_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8510_MONOMIX,

	.reg_defaults = wm8510_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8510_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = wm8510_volatile,
};

#if defined(CONFIG_SPI_MASTER)
static int wm8510_spi_probe(struct spi_device *spi)
{
	struct wm8510_priv *wm8510;
	int ret;

	wm8510 = devm_kzalloc(&spi->dev, sizeof(struct wm8510_priv),
			      GFP_KERNEL);
	if (wm8510 == NULL)
		return -ENOMEM;

	wm8510->regmap = devm_regmap_init_spi(spi, &wm8510_regmap);
	if (IS_ERR(wm8510->regmap))
		return PTR_ERR(wm8510->regmap);

	spi_set_drvdata(spi, wm8510);

	ret = devm_snd_soc_register_component(&spi->dev,
			&soc_component_dev_wm8510, &wm8510_dai, 1);

	return ret;
}

static struct spi_driver wm8510_spi_driver = {
	.driver = {
		.name	= "wm8510",
		.of_match_table = wm8510_of_match,
	},
	.probe		= wm8510_spi_probe,
};
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
static int wm8510_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8510_priv *wm8510;
	int ret;

	wm8510 = devm_kzalloc(&i2c->dev, sizeof(struct wm8510_priv),
			      GFP_KERNEL);
	if (wm8510 == NULL)
		return -ENOMEM;

	wm8510->regmap = devm_regmap_init_i2c(i2c, &wm8510_regmap);
	if (IS_ERR(wm8510->regmap))
		return PTR_ERR(wm8510->regmap);

	i2c_set_clientdata(i2c, wm8510);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8510, &wm8510_dai, 1);

	return ret;
}

static const struct i2c_device_id wm8510_i2c_id[] = {
	{ "wm8510", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8510_i2c_id);

static struct i2c_driver wm8510_i2c_driver = {
	.driver = {
		.name = "wm8510",
		.of_match_table = wm8510_of_match,
	},
	.probe =    wm8510_i2c_probe,
	.id_table = wm8510_i2c_id,
};
#endif

static int __init wm8510_modinit(void)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8510_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8510 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8510_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8510 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8510_modinit);

static void __exit wm8510_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8510_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8510_spi_driver);
#endif
}
module_exit(wm8510_exit);

MODULE_DESCRIPTION("ASoC WM8510 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
