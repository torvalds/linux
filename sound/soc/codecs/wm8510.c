/*
 * wm8510.c  --  WM8510 ALSA Soc Audio driver
 *
 * Copyright 2006 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wm8510.h"

#define AUDIO_NAME "wm8510"
#define WM8510_VERSION "0.6"

struct snd_soc_codec_device soc_codec_dev_wm8510;

/*
 * wm8510 register cache
 * We can't read the WM8510 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8510_reg[WM8510_CACHEREGNUM] = {
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0050, 0x0000, 0x0140, 0x0000,
	0x0000, 0x0000, 0x0000, 0x00ff,
	0x0000, 0x0000, 0x0100, 0x00ff,
	0x0000, 0x0000, 0x012c, 0x002c,
	0x002c, 0x002c, 0x002c, 0x0000,
	0x0032, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0038, 0x000b, 0x0032, 0x0000,
	0x0008, 0x000c, 0x0093, 0x00e9,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0003, 0x0010, 0x0000, 0x0000,
	0x0000, 0x0002, 0x0001, 0x0000,
	0x0000, 0x0000, 0x0039, 0x0000,
	0x0001,
};

/*
 * read wm8510 register cache
 */
static inline unsigned int wm8510_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8510_RESET)
		return 0;
	if (reg >= WM8510_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8510 register cache
 */
static inline void wm8510_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8510_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8510 register space
 */
static int wm8510_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8510 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8510_write_reg_cache(codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8510_reset(c)	wm8510_write(c, WM8510_RESET, 0)

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

/* add non dapm controls */
static int wm8510_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(wm8510_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				snd_soc_cnew(&wm8510_snd_controls[i], codec,
					NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

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

SND_SOC_DAPM_PGA("Mic PGA", WM8510_POWER2, 2, 0,
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

static const struct snd_soc_dapm_route audio_map[] = {
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

static int wm8510_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8510_dapm_widgets,
				  ARRAY_SIZE(wm8510_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

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
			"WM8510 N value %d outwith recommended range!d\n",
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

static int wm8510_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	if (freq_in == 0 || freq_out == 0) {
		/* Clock CODEC directly from MCLK */
		reg = wm8510_read_reg_cache(codec, WM8510_CLOCK);
		wm8510_write(codec, WM8510_CLOCK, reg & 0x0ff);

		/* Turn off PLL */
		reg = wm8510_read_reg_cache(codec, WM8510_POWER1);
		wm8510_write(codec, WM8510_POWER1, reg & 0x1df);
		return 0;
	}

	pll_factors(freq_out*8, freq_in);

	wm8510_write(codec, WM8510_PLLN, (pll_div.pre_div << 4) | pll_div.n);
	wm8510_write(codec, WM8510_PLLK1, pll_div.k >> 18);
	wm8510_write(codec, WM8510_PLLK2, (pll_div.k >> 9) & 0x1ff);
	wm8510_write(codec, WM8510_PLLK3, pll_div.k & 0x1ff);
	reg = wm8510_read_reg_cache(codec, WM8510_POWER1);
	wm8510_write(codec, WM8510_POWER1, reg | 0x020);

	/* Run CODEC from PLL instead of MCLK */
	reg = wm8510_read_reg_cache(codec, WM8510_CLOCK);
	wm8510_write(codec, WM8510_CLOCK, reg | 0x100);

	return 0;
}

/*
 * Configure WM8510 clock dividers.
 */
static int wm8510_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8510_OPCLKDIV:
		reg = wm8510_read_reg_cache(codec, WM8510_GPIO) & 0x1cf;
		wm8510_write(codec, WM8510_GPIO, reg | div);
		break;
	case WM8510_MCLKDIV:
		reg = wm8510_read_reg_cache(codec, WM8510_CLOCK) & 0x1f;
		wm8510_write(codec, WM8510_CLOCK, reg | div);
		break;
	case WM8510_ADCCLK:
		reg = wm8510_read_reg_cache(codec, WM8510_ADC) & 0x1f7;
		wm8510_write(codec, WM8510_ADC, reg | div);
		break;
	case WM8510_DACCLK:
		reg = wm8510_read_reg_cache(codec, WM8510_DAC) & 0x1f7;
		wm8510_write(codec, WM8510_DAC, reg | div);
		break;
	case WM8510_BCLKDIV:
		reg = wm8510_read_reg_cache(codec, WM8510_CLOCK) & 0x1e3;
		wm8510_write(codec, WM8510_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8510_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;
	u16 clk = wm8510_read_reg_cache(codec, WM8510_CLOCK) & 0x1fe;

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

	wm8510_write(codec, WM8510_IFACE, iface);
	wm8510_write(codec, WM8510_CLOCK, clk);
	return 0;
}

static int wm8510_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->codec;
	u16 iface = wm8510_read_reg_cache(codec, WM8510_IFACE) & 0x19f;
	u16 adn = wm8510_read_reg_cache(codec, WM8510_ADD) & 0x1f1;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0020;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0040;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x0060;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case SNDRV_PCM_RATE_8000:
		adn |= 0x5 << 1;
		break;
	case SNDRV_PCM_RATE_11025:
		adn |= 0x4 << 1;
		break;
	case SNDRV_PCM_RATE_16000:
		adn |= 0x3 << 1;
		break;
	case SNDRV_PCM_RATE_22050:
		adn |= 0x2 << 1;
		break;
	case SNDRV_PCM_RATE_32000:
		adn |= 0x1 << 1;
		break;
	case SNDRV_PCM_RATE_44100:
	case SNDRV_PCM_RATE_48000:
		break;
	}

	wm8510_write(codec, WM8510_IFACE, iface);
	wm8510_write(codec, WM8510_ADD, adn);
	return 0;
}

static int wm8510_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8510_read_reg_cache(codec, WM8510_DAC) & 0xffbf;

	if (mute)
		wm8510_write(codec, WM8510_DAC, mute_reg | 0x40);
	else
		wm8510_write(codec, WM8510_DAC, mute_reg);
	return 0;
}

/* liam need to make this lower power with dapm */
static int wm8510_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{

	switch (level) {
	case SND_SOC_BIAS_ON:
		wm8510_write(codec, WM8510_POWER1, 0x1ff);
		wm8510_write(codec, WM8510_POWER2, 0x1ff);
		wm8510_write(codec, WM8510_POWER3, 0x1ff);
		break;
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		wm8510_write(codec, WM8510_POWER1, 0x0);
		wm8510_write(codec, WM8510_POWER2, 0x0);
		wm8510_write(codec, WM8510_POWER3, 0x0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define WM8510_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define WM8510_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

struct snd_soc_dai wm8510_dai = {
	.name = "WM8510 HiFi",
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
	.ops = {
		.hw_params = wm8510_pcm_hw_params,
	},
	.dai_ops = {
		.digital_mute = wm8510_mute,
		.set_fmt = wm8510_set_dai_fmt,
		.set_clkdiv = wm8510_set_dai_clkdiv,
		.set_pll = wm8510_set_dai_pll,
	},
};
EXPORT_SYMBOL_GPL(wm8510_dai);

static int wm8510_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	wm8510_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8510_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8510_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	wm8510_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8510_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

/*
 * initialise the WM8510 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int wm8510_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
	int ret = 0;

	codec->name = "WM8510";
	codec->owner = THIS_MODULE;
	codec->read = wm8510_read_reg_cache;
	codec->write = wm8510_write;
	codec->set_bias_level = wm8510_set_bias_level;
	codec->dai = &wm8510_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(wm8510_reg);
	codec->reg_cache = kmemdup(wm8510_reg, sizeof(wm8510_reg), GFP_KERNEL);

	if (codec->reg_cache == NULL)
		return -ENOMEM;

	wm8510_reset(codec);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "wm8510: failed to create pcms\n");
		goto pcm_err;
	}

	/* power on device */
	wm8510_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8510_add_controls(codec);
	wm8510_add_widgets(codec);
	ret = snd_soc_register_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "wm8510: failed to register card\n");
		goto card_err;
	}
	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

static struct snd_soc_device *wm8510_socdev;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)

/*
 * WM8510 2 wire address is 0x1a
 */

static int wm8510_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = wm8510_socdev;
	struct snd_soc_codec *codec = socdev->codec;
	int ret;

	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = wm8510_init(socdev);
	if (ret < 0)
		pr_err("failed to initialise WM8510\n");

	return ret;
}

static int wm8510_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	kfree(codec->reg_cache);
	return 0;
}

static const struct i2c_device_id wm8510_i2c_id[] = {
	{ "wm8510", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8510_i2c_id);

static struct i2c_driver wm8510_i2c_driver = {
	.driver = {
		.name = "WM8510 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8510_i2c_probe,
	.remove =   wm8510_i2c_remove,
	.id_table = wm8510_i2c_id,
};

static int wm8510_add_i2c_device(struct platform_device *pdev,
				 const struct wm8510_setup_data *setup)
{
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	int ret;

	ret = i2c_add_driver(&wm8510_i2c_driver);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c driver\n");
		return ret;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = setup->i2c_address;
	strlcpy(info.type, "wm8510", I2C_NAME_SIZE);

	adapter = i2c_get_adapter(setup->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "can't get i2c adapter %d\n",
			setup->i2c_bus);
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	i2c_put_adapter(adapter);
	if (!client) {
		dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
			(unsigned int)info.addr);
		goto err_driver;
	}

	return 0;

err_driver:
	i2c_del_driver(&wm8510_i2c_driver);
	return -ENODEV;
}
#endif

static int wm8510_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct wm8510_setup_data *setup;
	struct snd_soc_codec *codec;
	int ret = 0;

	pr_info("WM8510 Audio Codec %s", WM8510_VERSION);

	setup = socdev->codec_data;
	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	socdev->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	wm8510_socdev = socdev;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	if (setup->i2c_address) {
		codec->hw_write = (hw_write_t)i2c_master_send;
		ret = wm8510_add_i2c_device(pdev, setup);
	}
#else
	/* Add other interfaces here */
#endif

	if (ret != 0)
		kfree(codec);
	return ret;
}

/* power down chip */
static int wm8510_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	if (codec->control_data)
		wm8510_set_bias_level(codec, SND_SOC_BIAS_OFF);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_unregister_device(codec->control_data);
	i2c_del_driver(&wm8510_i2c_driver);
#endif
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8510 = {
	.probe = 	wm8510_probe,
	.remove = 	wm8510_remove,
	.suspend = 	wm8510_suspend,
	.resume =	wm8510_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8510);

MODULE_DESCRIPTION("ASoC WM8510 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
