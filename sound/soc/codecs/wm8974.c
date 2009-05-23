/*
 * wm8974.c  --  WM8974 ALSA Soc Audio driver
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
#include <linux/version.h>
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

#include "wm8974.h"

#define AUDIO_NAME "wm8974"
#define WM8974_VERSION "0.6"

struct snd_soc_codec_device soc_codec_dev_wm8974;

/*
 * wm8974 register cache
 * We can't read the WM8974 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8974_reg[WM8974_CACHEREGNUM] = {
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
    0x0000, 0x0002, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0039, 0x0000,
    0x0000,
};

/*
 * read wm8974 register cache
 */
static inline unsigned int wm8974_read_reg_cache(struct snd_soc_codec * codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8974_RESET)
		return 0;
	if (reg >= WM8974_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8974 register cache
 */
static inline void wm8974_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8974_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8974 register space
 */
static int wm8974_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8974 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8974_write_reg_cache (codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8974_reset(c)	wm8974_write(c, WM8974_RESET, 0)

static const char *wm8974_companding[] = {"Off", "NC", "u-law", "A-law" };
static const char *wm8974_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8974_eqmode[] = {"Capture", "Playback" };
static const char *wm8974_bw[] = {"Narrow", "Wide" };
static const char *wm8974_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz" };
static const char *wm8974_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz" };
static const char *wm8974_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz" };
static const char *wm8974_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz" };
static const char *wm8974_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz" };
static const char *wm8974_alc[] = {"ALC", "Limiter" };

static const struct soc_enum wm8974_enum[] = {
	SOC_ENUM_SINGLE(WM8974_COMP, 1, 4, wm8974_companding), /* adc */
	SOC_ENUM_SINGLE(WM8974_COMP, 3, 4, wm8974_companding), /* dac */
	SOC_ENUM_SINGLE(WM8974_DAC,  4, 4, wm8974_deemp),
	SOC_ENUM_SINGLE(WM8974_EQ1,  8, 2, wm8974_eqmode),

	SOC_ENUM_SINGLE(WM8974_EQ1,  5, 4, wm8974_eq1),
	SOC_ENUM_SINGLE(WM8974_EQ2,  8, 2, wm8974_bw),
	SOC_ENUM_SINGLE(WM8974_EQ2,  5, 4, wm8974_eq2),
	SOC_ENUM_SINGLE(WM8974_EQ3,  8, 2, wm8974_bw),

	SOC_ENUM_SINGLE(WM8974_EQ3,  5, 4, wm8974_eq3),
	SOC_ENUM_SINGLE(WM8974_EQ4,  8, 2, wm8974_bw),
	SOC_ENUM_SINGLE(WM8974_EQ4,  5, 4, wm8974_eq4),
	SOC_ENUM_SINGLE(WM8974_EQ5,  8, 2, wm8974_bw),

	SOC_ENUM_SINGLE(WM8974_EQ5,  5, 4, wm8974_eq5),
	SOC_ENUM_SINGLE(WM8974_ALC3,  8, 2, wm8974_alc),
};

static const struct snd_kcontrol_new wm8974_snd_controls[] = {

SOC_SINGLE("Digital Loopback Switch", WM8974_COMP, 0, 1, 0),

SOC_ENUM("DAC Companding", wm8974_enum[1]),
SOC_ENUM("ADC Companding", wm8974_enum[0]),

SOC_ENUM("Playback De-emphasis", wm8974_enum[2]),
SOC_SINGLE("DAC Inversion Switch", WM8974_DAC, 0, 1, 0),

SOC_SINGLE("PCM Volume", WM8974_DACVOL, 0, 127, 0),

SOC_SINGLE("High Pass Filter Switch", WM8974_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8974_ADC, 4, 7, 0),
SOC_SINGLE("ADC Inversion Switch", WM8974_COMP, 0, 1, 0),

SOC_SINGLE("Capture Volume", WM8974_ADCVOL,  0, 127, 0),

SOC_ENUM("Equaliser Function", wm8974_enum[3]),
SOC_ENUM("EQ1 Cut Off", wm8974_enum[4]),
SOC_SINGLE("EQ1 Volume", WM8974_EQ1,  0, 31, 1),

SOC_ENUM("Equaliser EQ2 Bandwith", wm8974_enum[5]),
SOC_ENUM("EQ2 Cut Off", wm8974_enum[6]),
SOC_SINGLE("EQ2 Volume", WM8974_EQ2,  0, 31, 1),

SOC_ENUM("Equaliser EQ3 Bandwith", wm8974_enum[7]),
SOC_ENUM("EQ3 Cut Off", wm8974_enum[8]),
SOC_SINGLE("EQ3 Volume", WM8974_EQ3,  0, 31, 1),

SOC_ENUM("Equaliser EQ4 Bandwith", wm8974_enum[9]),
SOC_ENUM("EQ4 Cut Off", wm8974_enum[10]),
SOC_SINGLE("EQ4 Volume", WM8974_EQ4,  0, 31, 1),

SOC_ENUM("Equaliser EQ5 Bandwith", wm8974_enum[11]),
SOC_ENUM("EQ5 Cut Off", wm8974_enum[12]),
SOC_SINGLE("EQ5 Volume", WM8974_EQ5,  0, 31, 1),

SOC_SINGLE("DAC Playback Limiter Switch", WM8974_DACLIM1,  8, 1, 0),
SOC_SINGLE("DAC Playback Limiter Decay", WM8974_DACLIM1,  4, 15, 0),
SOC_SINGLE("DAC Playback Limiter Attack", WM8974_DACLIM1,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Threshold", WM8974_DACLIM2,  4, 7, 0),
SOC_SINGLE("DAC Playback Limiter Boost", WM8974_DACLIM2,  0, 15, 0),

SOC_SINGLE("ALC Enable Switch", WM8974_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8974_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8974_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8974_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8974_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8974_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8974_enum[13]),
SOC_SINGLE("ALC Capture Decay", WM8974_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8974_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8974_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8974_NGATE,  0, 7, 0),

SOC_SINGLE("Capture PGA ZC Switch", WM8974_INPPGA,  7, 1, 0),
SOC_SINGLE("Capture PGA Volume", WM8974_INPPGA,  0, 63, 0),

SOC_SINGLE("Speaker Playback ZC Switch", WM8974_SPKVOL,  7, 1, 0),
SOC_SINGLE("Speaker Playback Switch", WM8974_SPKVOL,  6, 1, 1),
SOC_SINGLE("Speaker Playback Volume", WM8974_SPKVOL,  0, 63, 0),

SOC_SINGLE("Capture Boost(+20dB)", WM8974_ADCBOOST,  8, 1, 0),
SOC_SINGLE("Mono Playback Switch", WM8974_MONOMIX, 6, 1, 0),
};

/* add non dapm controls */
static int wm8974_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(wm8974_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				snd_soc_cnew(&wm8974_snd_controls[i],codec, NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

/* Speaker Output Mixer */
static const struct snd_kcontrol_new wm8974_speaker_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8974_SPKMIX, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8974_SPKMIX, 5, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", WM8974_SPKMIX, 0, 1, 1),
};

/* Mono Output Mixer */
static const struct snd_kcontrol_new wm8974_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8974_MONOMIX, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8974_MONOMIX, 2, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", WM8974_MONOMIX, 0, 1, 1),
};

/* AUX Input boost vol */
static const struct snd_kcontrol_new wm8974_aux_boost_controls =
SOC_DAPM_SINGLE("Aux Volume", WM8974_ADCBOOST, 0, 7, 0);

/* Mic Input boost vol */
static const struct snd_kcontrol_new wm8974_mic_boost_controls =
SOC_DAPM_SINGLE("Mic Volume", WM8974_ADCBOOST, 4, 7, 0);

/* Capture boost switch */
static const struct snd_kcontrol_new wm8974_capture_boost_controls =
SOC_DAPM_SINGLE("Capture Boost Switch", WM8974_INPPGA,  6, 1, 0);

/* Aux In to PGA */
static const struct snd_kcontrol_new wm8974_aux_capture_boost_controls =
SOC_DAPM_SINGLE("Aux Capture Boost Switch", WM8974_INPPGA,  2, 1, 0);

/* Mic P In to PGA */
static const struct snd_kcontrol_new wm8974_micp_capture_boost_controls =
SOC_DAPM_SINGLE("Mic P Capture Boost Switch", WM8974_INPPGA,  0, 1, 0);

/* Mic N In to PGA */
static const struct snd_kcontrol_new wm8974_micn_capture_boost_controls =
SOC_DAPM_SINGLE("Mic N Capture Boost Switch", WM8974_INPPGA,  1, 1, 0);

static const struct snd_soc_dapm_widget wm8974_dapm_widgets[] = {
SND_SOC_DAPM_MIXER("Speaker Mixer", WM8974_POWER3, 2, 0,
	&wm8974_speaker_mixer_controls[0],
	ARRAY_SIZE(wm8974_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("Mono Mixer", WM8974_POWER3, 3, 0,
	&wm8974_mono_mixer_controls[0],
	ARRAY_SIZE(wm8974_mono_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8974_POWER3, 0, 0),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8974_POWER3, 0, 0),
SND_SOC_DAPM_PGA("Aux Input", WM8974_POWER1, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkN Out", WM8974_POWER3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkP Out", WM8974_POWER3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mono Out", WM8974_POWER3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic PGA", WM8974_POWER2, 2, 0, NULL, 0),

SND_SOC_DAPM_PGA("Aux Boost", SND_SOC_NOPM, 0, 0,
	&wm8974_aux_boost_controls, 1),
SND_SOC_DAPM_PGA("Mic Boost", SND_SOC_NOPM, 0, 0,
	&wm8974_mic_boost_controls, 1),
SND_SOC_DAPM_SWITCH("Capture Boost", SND_SOC_NOPM, 0, 0,
	&wm8974_capture_boost_controls),

SND_SOC_DAPM_MIXER("Boost Mixer", WM8974_POWER2, 4, 0, NULL, 0),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8974_POWER1, 4, 0),

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

	/* Boost Mixer */
	{"Boost Mixer", NULL, "ADC"},
	{"Capture Boost Switch", "Aux Capture Boost Switch", "AUX"},
	{"Aux Boost", "Aux Volume", "Boost Mixer"},
	{"Capture Boost", "Capture Switch", "Boost Mixer"},
	{"Mic Boost", "Mic Volume", "Boost Mixer"},

	/* Inputs */
	{"MICP", NULL, "Mic Boost"},
	{"MICN", NULL, "Mic PGA"},
	{"Mic PGA", NULL, "Capture Boost"},
	{"AUX", NULL, "Aux Input"},
};

static int wm8974_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8974_dapm_widgets,
				  ARRAY_SIZE(wm8974_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

struct pll_ {
	unsigned int in_hz, out_hz;
	unsigned int pre:4; /* prescale - 1 */
	unsigned int n:4;
	unsigned int k;
};

static struct pll_ pll[] = {
	{12000000, 11289600, 0, 7, 0x86c220},
	{12000000, 12288000, 0, 8, 0x3126e8},
	{13000000, 11289600, 0, 6, 0xf28bd4},
	{13000000, 12288000, 0, 7, 0x8fd525},
	{12288000, 11289600, 0, 7, 0x59999a},
	{11289600, 12288000, 0, 8, 0x80dee9},
	/* liam - add more entries */
};

static int wm8974_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int i;
	u16 reg;

	if(freq_in == 0 || freq_out == 0) {
		reg = wm8974_read_reg_cache(codec, WM8974_POWER1);
		wm8974_write(codec, WM8974_POWER1, reg & 0x1df);
		return 0;
	}

	for(i = 0; i < ARRAY_SIZE(pll); i++) {
		if (freq_in == pll[i].in_hz && freq_out == pll[i].out_hz) {
			wm8974_write(codec, WM8974_PLLN, (pll[i].pre << 4) | pll[i].n);
			wm8974_write(codec, WM8974_PLLK1, pll[i].k >> 18);
			wm8974_write(codec, WM8974_PLLK2, (pll[i].k >> 9) & 0x1ff);
			wm8974_write(codec, WM8974_PLLK3, pll[i].k & 0x1ff);
			reg = wm8974_read_reg_cache(codec, WM8974_POWER1);
			wm8974_write(codec, WM8974_POWER1, reg | 0x020);
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Configure WM8974 clock dividers.
 */
static int wm8974_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8974_OPCLKDIV:
		reg = wm8974_read_reg_cache(codec, WM8974_GPIO) & 0x1cf; 
		wm8974_write(codec, WM8974_GPIO, reg | div);
		break;
	case WM8974_MCLKDIV:
		reg = wm8974_read_reg_cache(codec, WM8974_CLOCK) & 0x11f;
		wm8974_write(codec, WM8974_CLOCK, reg | div);
		break;
	case WM8974_ADCCLK:
		reg = wm8974_read_reg_cache(codec, WM8974_ADC) & 0x1f7;
		wm8974_write(codec, WM8974_ADC, reg | div);
		break;
	case WM8974_DACCLK:
		reg = wm8974_read_reg_cache(codec, WM8974_DAC) & 0x1f7;
		wm8974_write(codec, WM8974_DAC, reg | div);
		break;
	case WM8974_BCLKDIV:
		reg = wm8974_read_reg_cache(codec, WM8974_CLOCK) & 0x1e3;
		wm8974_write(codec, WM8974_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8974_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;
	u16 clk = wm8974_read_reg_cache(codec, WM8974_CLOCK) & 0x1fe;

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

	wm8974_write(codec, WM8974_IFACE, iface);
	wm8974_write(codec, WM8974_CLOCK, clk);
	return 0;
}

static int wm8974_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 iface = wm8974_read_reg_cache(codec, WM8974_IFACE) & 0x19f;
	u16 adn = wm8974_read_reg_cache(codec, WM8974_ADD) & 0x1f1;

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
		break;
	}

	wm8974_write(codec, WM8974_IFACE, iface);
	wm8974_write(codec, WM8974_ADD, adn);
	return 0;
}

static int wm8974_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8974_read_reg_cache(codec, WM8974_DAC) & 0xffbf;

	if(mute)
		wm8974_write(codec, WM8974_DAC, mute_reg | 0x40);
	else
		wm8974_write(codec, WM8974_DAC, mute_reg);
	return 0;
}

/* liam need to make this lower power with dapm */
static int wm8974_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		wm8974_write(codec, WM8974_POWER1, 0x1ff);
		wm8974_write(codec, WM8974_POWER2, 0x1ff);
		wm8974_write(codec, WM8974_POWER3, 0x1ff);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		wm8974_write(codec, WM8974_POWER1, 0x0);
		wm8974_write(codec, WM8974_POWER2, 0x0);
		wm8974_write(codec, WM8974_POWER3, 0x0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define WM8974_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000)

#define WM8974_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8974_ops = {
	.hw_params = wm8974_pcm_hw_params,
	.digital_mute = wm8974_mute,
	.set_fmt = wm8974_set_dai_fmt,
	.set_clkdiv = wm8974_set_dai_clkdiv,
	.set_pll = wm8974_set_dai_pll,
};

struct snd_soc_dai wm8974_dai = {
	.name = "WM8974 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM8974_RATES,
		.formats = WM8974_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM8974_RATES,
		.formats = WM8974_FORMATS,},
	.ops = &wm8974_ops,
};
EXPORT_SYMBOL_GPL(wm8974_dai);

static int wm8974_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8974_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8974_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8974_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	wm8974_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8974_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

/*
 * initialise the WM8974 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int wm8974_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret = 0;

	codec->name = "WM8974";
	codec->owner = THIS_MODULE;
	codec->read = wm8974_read_reg_cache;
	codec->write = wm8974_write;
	codec->set_bias_level = wm8974_set_bias_level;
	codec->dai = &wm8974_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(wm8974_reg);
	codec->reg_cache = kmemdup(wm8974_reg, sizeof(wm8974_reg), GFP_KERNEL);

	if (codec->reg_cache == NULL)
		return -ENOMEM;

	wm8974_reset(codec);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if(ret < 0) {
		printk(KERN_ERR "wm8974: failed to create pcms\n");
		goto pcm_err;
	}

	/* power on device */
	wm8974_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8974_add_controls(codec);
	wm8974_add_widgets(codec);
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "wm8974: failed to register card\n");
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

static struct snd_soc_device *wm8974_socdev;

#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)

/*
 * WM8974 2 wire address is 0x1a
 */
#define I2C_DRIVERID_WM8974 0xfefe /* liam -  need a proper id */

static unsigned short normal_i2c[] = { 0, I2C_CLIENT_END };

/* Magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver wm8974_i2c_driver;
static struct i2c_client client_template;

/* If the i2c layer weren't so broken, we could pass this kind of data
   around */

static int wm8974_codec_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct snd_soc_device *socdev = wm8974_socdev;
	struct wm8974_setup_data *setup = socdev->codec_data;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct i2c_client *i2c;
	int ret;

	if (addr != setup->i2c_address)
		return -ENODEV;

	client_template.adapter = adap;
	client_template.addr = addr;

	i2c = kmemdup(&client_template, sizeof(client_template), GFP_KERNEL);
	if (i2c == NULL) {
		kfree(codec);
		return -ENOMEM;
	}
	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = i2c_attach_client(i2c);
	if (ret < 0) {
		pr_err("failed to attach codec at addr %x\n", addr);
		goto err;
	}

	ret = wm8974_init(socdev);
	if (ret < 0) {
		pr_err("failed to initialise WM8974\n");
		goto err;
	}
	return ret;

err:
	kfree(codec);
	kfree(i2c);
	return ret;
}

static int wm8974_i2c_detach(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	i2c_detach_client(client);
	kfree(codec->reg_cache);
	kfree(client);
	return 0;
}

static int wm8974_i2c_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, wm8974_codec_probe);
}

/* corgi i2c codec control layer */
static struct i2c_driver wm8974_i2c_driver = {
	.driver = {
		.name = "WM8974 I2C Codec",
		.owner = THIS_MODULE,
	},
	.id =             I2C_DRIVERID_WM8974,
	.attach_adapter = wm8974_i2c_attach,
	.detach_client =  wm8974_i2c_detach,
	.command =        NULL,
};

static struct i2c_client client_template = {
	.name =   "WM8974",
	.driver = &wm8974_i2c_driver,
};
#endif

static int wm8974_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct wm8974_setup_data *setup;
	struct snd_soc_codec *codec;
	int ret = 0;

	pr_info("WM8974 Audio Codec %s", WM8974_VERSION);

	setup = socdev->codec_data;
	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	socdev->card->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	wm8974_socdev = socdev;
#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
	if (setup->i2c_address) {
		normal_i2c[0] = setup->i2c_address;
		codec->hw_write = (hw_write_t)i2c_master_send;
		ret = i2c_add_driver(&wm8974_i2c_driver);
		if (ret != 0)
			printk(KERN_ERR "can't add i2c driver");
	}
#else
	/* Add other interfaces here */
#endif
	return ret;
}

/* power down chip */
static int wm8974_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (codec->control_data)
		wm8974_set_bias_level(codec, SND_SOC_BIAS_OFF);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8974_i2c_driver);
#endif
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8974 = {
	.probe = 	wm8974_probe,
	.remove = 	wm8974_remove,
	.suspend = 	wm8974_suspend,
	.resume =	wm8974_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8974);

static int __init wm8974_modinit(void)
{
	return snd_soc_register_dai(&wm8974_dai);
}
module_init(wm8974_modinit);

static void __exit wm8974_exit(void)
{
	snd_soc_unregister_dai(&wm8974_dai);
}
module_exit(wm8974_exit);

MODULE_DESCRIPTION("ASoC WM8974 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
