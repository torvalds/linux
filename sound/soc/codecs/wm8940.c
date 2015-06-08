/*
 * wm8940.c  --  WM8940 ALSA Soc Audio driver
 *
 * Author: Jonathan Cameron <jic23@cam.ac.uk>
 *
 * Based on wm8510.c
 *    Copyright  2006 Wolfson Microelectronics PLC.
 *    Author:  Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Not currently handled:
 * Notch filter control
 * AUXMode (inverting vs mixer)
 * No means to obtain current gain if alc enabled.
 * No use made of gpio
 * Fast VMID discharge for power down
 * Soft Start
 * DLR and ALR Swaps not enabled
 * Digital Sidetone not supported
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8940.h"

struct wm8940_priv {
	unsigned int sysclk;
	struct regmap *regmap;
};

static bool wm8940_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8940_SOFTRESET:
		return true;
	default:
		return false;
	}
}

static bool wm8940_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8940_SOFTRESET:
	case WM8940_POWER1:
	case WM8940_POWER2:
	case WM8940_POWER3:
	case WM8940_IFACE:
	case WM8940_COMPANDINGCTL:
	case WM8940_CLOCK:
	case WM8940_ADDCNTRL:
	case WM8940_GPIO:
	case WM8940_CTLINT:
	case WM8940_DAC:
	case WM8940_DACVOL:
	case WM8940_ADC:
	case WM8940_ADCVOL:
	case WM8940_NOTCH1:
	case WM8940_NOTCH2:
	case WM8940_NOTCH3:
	case WM8940_NOTCH4:
	case WM8940_NOTCH5:
	case WM8940_NOTCH6:
	case WM8940_NOTCH7:
	case WM8940_NOTCH8:
	case WM8940_DACLIM1:
	case WM8940_DACLIM2:
	case WM8940_ALC1:
	case WM8940_ALC2:
	case WM8940_ALC3:
	case WM8940_NOISEGATE:
	case WM8940_PLLN:
	case WM8940_PLLK1:
	case WM8940_PLLK2:
	case WM8940_PLLK3:
	case WM8940_ALC4:
	case WM8940_INPUTCTL:
	case WM8940_PGAGAIN:
	case WM8940_ADCBOOST:
	case WM8940_OUTPUTCTL:
	case WM8940_SPKMIX:
	case WM8940_SPKVOL:
	case WM8940_MONOMIX:
		return true;
	default:
		return false;
	}
}

static const struct reg_default wm8940_reg_defaults[] = {
	{  0x1, 0x0000 }, /* Power 1 */
	{  0x2, 0x0000 }, /* Power 2 */
	{  0x3, 0x0000 }, /* Power 3 */
	{  0x4, 0x0010 }, /* Interface Control */
	{  0x5, 0x0000 }, /* Companding Control */
	{  0x6, 0x0140 }, /* Clock Control */
	{  0x7, 0x0000 }, /* Additional Controls */
	{  0x8, 0x0000 }, /* GPIO Control */
	{  0x9, 0x0002 }, /* Auto Increment Control */
	{  0xa, 0x0000 }, /* DAC Control */
	{  0xb, 0x00FF }, /* DAC Volume */

	{  0xe, 0x0100 }, /* ADC Control */
	{  0xf, 0x00FF }, /* ADC Volume */
	{ 0x10, 0x0000 }, /* Notch Filter 1 Control 1 */
	{ 0x11, 0x0000 }, /* Notch Filter 1 Control 2 */
	{ 0x12, 0x0000 }, /* Notch Filter 2 Control 1 */
	{ 0x13, 0x0000 }, /* Notch Filter 2 Control 2 */
	{ 0x14, 0x0000 }, /* Notch Filter 3 Control 1 */
	{ 0x15, 0x0000 }, /* Notch Filter 3 Control 2 */
	{ 0x16, 0x0000 }, /* Notch Filter 4 Control 1 */
	{ 0x17, 0x0000 }, /* Notch Filter 4 Control 2 */
	{ 0x18, 0x0032 }, /* DAC Limit Control 1 */
	{ 0x19, 0x0000 }, /* DAC Limit Control 2 */

	{ 0x20, 0x0038 }, /* ALC Control 1 */
	{ 0x21, 0x000B }, /* ALC Control 2 */
	{ 0x22, 0x0032 }, /* ALC Control 3 */
	{ 0x23, 0x0000 }, /* Noise Gate */
	{ 0x24, 0x0041 }, /* PLLN */
	{ 0x25, 0x000C }, /* PLLK1 */
	{ 0x26, 0x0093 }, /* PLLK2 */
	{ 0x27, 0x00E9 }, /* PLLK3 */

	{ 0x2a, 0x0030 }, /* ALC Control 4 */

	{ 0x2c, 0x0002 }, /* Input Control */
	{ 0x2d, 0x0050 }, /* PGA Gain */

	{ 0x2f, 0x0002 }, /* ADC Boost Control */

	{ 0x31, 0x0002 }, /* Output Control */
	{ 0x32, 0x0000 }, /* Speaker Mixer Control */

	{ 0x36, 0x0079 }, /* Speaker Volume */

	{ 0x38, 0x0000 }, /* Mono Mixer Control */
};

static const char *wm8940_companding[] = { "Off", "NC", "u-law", "A-law" };
static SOC_ENUM_SINGLE_DECL(wm8940_adc_companding_enum,
			    WM8940_COMPANDINGCTL, 1, wm8940_companding);
static SOC_ENUM_SINGLE_DECL(wm8940_dac_companding_enum,
			    WM8940_COMPANDINGCTL, 3, wm8940_companding);

static const char *wm8940_alc_mode_text[] = {"ALC", "Limiter"};
static SOC_ENUM_SINGLE_DECL(wm8940_alc_mode_enum,
			    WM8940_ALC3, 8, wm8940_alc_mode_text);

static const char *wm8940_mic_bias_level_text[] = {"0.9", "0.65"};
static SOC_ENUM_SINGLE_DECL(wm8940_mic_bias_level_enum,
			    WM8940_INPUTCTL, 8, wm8940_mic_bias_level_text);

static const char *wm8940_filter_mode_text[] = {"Audio", "Application"};
static SOC_ENUM_SINGLE_DECL(wm8940_filter_mode_enum,
			    WM8940_ADC, 7, wm8940_filter_mode_text);

static DECLARE_TLV_DB_SCALE(wm8940_spk_vol_tlv, -5700, 100, 1);
static DECLARE_TLV_DB_SCALE(wm8940_att_tlv, -1000, 1000, 0);
static DECLARE_TLV_DB_SCALE(wm8940_pga_vol_tlv, -1200, 75, 0);
static DECLARE_TLV_DB_SCALE(wm8940_alc_min_tlv, -1200, 600, 0);
static DECLARE_TLV_DB_SCALE(wm8940_alc_max_tlv, 675, 600, 0);
static DECLARE_TLV_DB_SCALE(wm8940_alc_tar_tlv, -2250, 50, 0);
static DECLARE_TLV_DB_SCALE(wm8940_lim_boost_tlv, 0, 100, 0);
static DECLARE_TLV_DB_SCALE(wm8940_lim_thresh_tlv, -600, 100, 0);
static DECLARE_TLV_DB_SCALE(wm8940_adc_tlv, -12750, 50, 1);
static DECLARE_TLV_DB_SCALE(wm8940_capture_boost_vol_tlv, 0, 2000, 0);

static const struct snd_kcontrol_new wm8940_snd_controls[] = {
	SOC_SINGLE("Digital Loopback Switch", WM8940_COMPANDINGCTL,
		   6, 1, 0),
	SOC_ENUM("DAC Companding", wm8940_dac_companding_enum),
	SOC_ENUM("ADC Companding", wm8940_adc_companding_enum),

	SOC_ENUM("ALC Mode", wm8940_alc_mode_enum),
	SOC_SINGLE("ALC Switch", WM8940_ALC1, 8, 1, 0),
	SOC_SINGLE_TLV("ALC Capture Max Gain", WM8940_ALC1,
		       3, 7, 1, wm8940_alc_max_tlv),
	SOC_SINGLE_TLV("ALC Capture Min Gain", WM8940_ALC1,
		       0, 7, 0, wm8940_alc_min_tlv),
	SOC_SINGLE_TLV("ALC Capture Target", WM8940_ALC2,
		       0, 14, 0, wm8940_alc_tar_tlv),
	SOC_SINGLE("ALC Capture Hold", WM8940_ALC2, 4, 10, 0),
	SOC_SINGLE("ALC Capture Decay", WM8940_ALC3, 4, 10, 0),
	SOC_SINGLE("ALC Capture Attach", WM8940_ALC3, 0, 10, 0),
	SOC_SINGLE("ALC ZC Switch", WM8940_ALC4, 1, 1, 0),
	SOC_SINGLE("ALC Capture Noise Gate Switch", WM8940_NOISEGATE,
		   3, 1, 0),
	SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8940_NOISEGATE,
		   0, 7, 0),

	SOC_SINGLE("DAC Playback Limiter Switch", WM8940_DACLIM1, 8, 1, 0),
	SOC_SINGLE("DAC Playback Limiter Attack", WM8940_DACLIM1, 0, 9, 0),
	SOC_SINGLE("DAC Playback Limiter Decay", WM8940_DACLIM1, 4, 11, 0),
	SOC_SINGLE_TLV("DAC Playback Limiter Threshold", WM8940_DACLIM2,
		       4, 9, 1, wm8940_lim_thresh_tlv),
	SOC_SINGLE_TLV("DAC Playback Limiter Boost", WM8940_DACLIM2,
		       0, 12, 0, wm8940_lim_boost_tlv),

	SOC_SINGLE("Capture PGA ZC Switch", WM8940_PGAGAIN, 7, 1, 0),
	SOC_SINGLE_TLV("Capture PGA Volume", WM8940_PGAGAIN,
		       0, 63, 0, wm8940_pga_vol_tlv),
	SOC_SINGLE_TLV("Digital Playback Volume", WM8940_DACVOL,
		       0, 255, 0, wm8940_adc_tlv),
	SOC_SINGLE_TLV("Digital Capture Volume", WM8940_ADCVOL,
		       0, 255, 0, wm8940_adc_tlv),
	SOC_ENUM("Mic Bias Level", wm8940_mic_bias_level_enum),
	SOC_SINGLE_TLV("Capture Boost Volue", WM8940_ADCBOOST,
		       8, 1, 0, wm8940_capture_boost_vol_tlv),
	SOC_SINGLE_TLV("Speaker Playback Volume", WM8940_SPKVOL,
		       0, 63, 0, wm8940_spk_vol_tlv),
	SOC_SINGLE("Speaker Playback Switch", WM8940_SPKVOL,  6, 1, 1),

	SOC_SINGLE_TLV("Speaker Mixer Line Bypass Volume", WM8940_SPKVOL,
		       8, 1, 1, wm8940_att_tlv),
	SOC_SINGLE("Speaker Playback ZC Switch", WM8940_SPKVOL, 7, 1, 0),

	SOC_SINGLE("Mono Out Switch", WM8940_MONOMIX, 6, 1, 1),
	SOC_SINGLE_TLV("Mono Mixer Line Bypass Volume", WM8940_MONOMIX,
		       7, 1, 1, wm8940_att_tlv),

	SOC_SINGLE("High Pass Filter Switch", WM8940_ADC, 8, 1, 0),
	SOC_ENUM("High Pass Filter Mode", wm8940_filter_mode_enum),
	SOC_SINGLE("High Pass Filter Cut Off", WM8940_ADC, 4, 7, 0),
	SOC_SINGLE("ADC Inversion Switch", WM8940_ADC, 0, 1, 0),
	SOC_SINGLE("DAC Inversion Switch", WM8940_DAC, 0, 1, 0),
	SOC_SINGLE("DAC Auto Mute Switch", WM8940_DAC, 2, 1, 0),
	SOC_SINGLE("ZC Timeout Clock Switch", WM8940_ADDCNTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new wm8940_speaker_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Bypass Switch", WM8940_SPKMIX, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Playback Switch", WM8940_SPKMIX, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Playback Switch", WM8940_SPKMIX, 0, 1, 0),
};

static const struct snd_kcontrol_new wm8940_mono_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Bypass Switch", WM8940_MONOMIX, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Playback Switch", WM8940_MONOMIX, 2, 1, 0),
	SOC_DAPM_SINGLE("PCM Playback Switch", WM8940_MONOMIX, 0, 1, 0),
};

static DECLARE_TLV_DB_SCALE(wm8940_boost_vol_tlv, -1500, 300, 1);
static const struct snd_kcontrol_new wm8940_input_boost_controls[] = {
	SOC_DAPM_SINGLE("Mic PGA Switch", WM8940_PGAGAIN, 6, 1, 1),
	SOC_DAPM_SINGLE_TLV("Aux Volume", WM8940_ADCBOOST,
			    0, 7, 0, wm8940_boost_vol_tlv),
	SOC_DAPM_SINGLE_TLV("Mic Volume", WM8940_ADCBOOST,
			    4, 7, 0, wm8940_boost_vol_tlv),
};

static const struct snd_kcontrol_new wm8940_micpga_controls[] = {
	SOC_DAPM_SINGLE("AUX Switch", WM8940_INPUTCTL, 2, 1, 0),
	SOC_DAPM_SINGLE("MICP Switch", WM8940_INPUTCTL, 0, 1, 0),
	SOC_DAPM_SINGLE("MICN Switch", WM8940_INPUTCTL, 1, 1, 0),
};

static const struct snd_soc_dapm_widget wm8940_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Speaker Mixer", WM8940_POWER3, 2, 0,
			   &wm8940_speaker_mixer_controls[0],
			   ARRAY_SIZE(wm8940_speaker_mixer_controls)),
	SND_SOC_DAPM_MIXER("Mono Mixer", WM8940_POWER3, 3, 0,
			   &wm8940_mono_mixer_controls[0],
			   ARRAY_SIZE(wm8940_mono_mixer_controls)),
	SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8940_POWER3, 0, 0),

	SND_SOC_DAPM_PGA("SpkN Out", WM8940_POWER3, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SpkP Out", WM8940_POWER3, 6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono Out", WM8940_POWER3, 7, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("MONOOUT"),
	SND_SOC_DAPM_OUTPUT("SPKOUTP"),
	SND_SOC_DAPM_OUTPUT("SPKOUTN"),

	SND_SOC_DAPM_PGA("Aux Input", WM8940_POWER1, 6, 0, NULL, 0),
	SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8940_POWER2, 0, 0),
	SND_SOC_DAPM_MIXER("Mic PGA", WM8940_POWER2, 2, 0,
			   &wm8940_micpga_controls[0],
			   ARRAY_SIZE(wm8940_micpga_controls)),
	SND_SOC_DAPM_MIXER("Boost Mixer", WM8940_POWER2, 4, 0,
			   &wm8940_input_boost_controls[0],
			   ARRAY_SIZE(wm8940_input_boost_controls)),
	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8940_POWER1, 4, 0),

	SND_SOC_DAPM_INPUT("MICN"),
	SND_SOC_DAPM_INPUT("MICP"),
	SND_SOC_DAPM_INPUT("AUX"),
};

static const struct snd_soc_dapm_route wm8940_dapm_routes[] = {
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

	/*  Microphone PGA */
	{"Mic PGA", "MICN Switch", "MICN"},
	{"Mic PGA", "MICP Switch", "MICP"},
	{"Mic PGA", "AUX Switch", "AUX"},

	/* Boost Mixer */
	{"Boost Mixer", "Mic PGA Switch", "Mic PGA"},
	{"Boost Mixer", "Mic Volume",  "MICP"},
	{"Boost Mixer", "Aux Volume", "Aux Input"},

	{"ADC", NULL, "Boost Mixer"},
};

#define wm8940_reset(c) snd_soc_write(c, WM8940_SOFTRESET, 0);

static int wm8940_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = snd_soc_read(codec, WM8940_IFACE) & 0xFE67;
	u16 clk = snd_soc_read(codec, WM8940_CLOCK) & 0x1fe;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}
	snd_soc_write(codec, WM8940_CLOCK, clk);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= (2 << 3);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= (1 << 3);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= (3 << 3);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= (3 << 3) | (1 << 7);
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= (1 << 7);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= (1 << 8);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= (1 << 8) | (1 << 7);
		break;
	}

	snd_soc_write(codec, WM8940_IFACE, iface);

	return 0;
}

static int wm8940_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 iface = snd_soc_read(codec, WM8940_IFACE) & 0xFD9F;
	u16 addcntrl = snd_soc_read(codec, WM8940_ADDCNTRL) & 0xFFF1;
	u16 companding =  snd_soc_read(codec,
						WM8940_COMPANDINGCTL) & 0xFFDF;
	int ret;

	/* LoutR control */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE
	    && params_channels(params) == 2)
		iface |= (1 << 9);

	switch (params_rate(params)) {
	case 8000:
		addcntrl |= (0x5 << 1);
		break;
	case 11025:
		addcntrl |= (0x4 << 1);
		break;
	case 16000:
		addcntrl |= (0x3 << 1);
		break;
	case 22050:
		addcntrl |= (0x2 << 1);
		break;
	case 32000:
		addcntrl |= (0x1 << 1);
		break;
	case 44100:
	case 48000:
		break;
	}
	ret = snd_soc_write(codec, WM8940_ADDCNTRL, addcntrl);
	if (ret)
		goto error_ret;

	switch (params_width(params)) {
	case 8:
		companding = companding | (1 << 5);
		break;
	case 16:
		break;
	case 20:
		iface |= (1 << 5);
		break;
	case 24:
		iface |= (2 << 5);
		break;
	case 32:
		iface |= (3 << 5);
		break;
	}
	ret = snd_soc_write(codec, WM8940_COMPANDINGCTL, companding);
	if (ret)
		goto error_ret;
	ret = snd_soc_write(codec, WM8940_IFACE, iface);

error_ret:
	return ret;
}

static int wm8940_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8940_DAC) & 0xffbf;

	if (mute)
		mute_reg |= 0x40;

	return snd_soc_write(codec, WM8940_DAC, mute_reg);
}

static int wm8940_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8940_priv *wm8940 = snd_soc_codec_get_drvdata(codec);
	u16 val;
	u16 pwr_reg = snd_soc_read(codec, WM8940_POWER1) & 0x1F0;
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* ensure bufioen and biasen */
		pwr_reg |= (1 << 2) | (1 << 3);
		/* Enable thermal shutdown */
		val = snd_soc_read(codec, WM8940_OUTPUTCTL);
		ret = snd_soc_write(codec, WM8940_OUTPUTCTL, val | 0x2);
		if (ret)
			break;
		/* set vmid to 75k */
		ret = snd_soc_write(codec, WM8940_POWER1, pwr_reg | 0x1);
		break;
	case SND_SOC_BIAS_PREPARE:
		/* ensure bufioen and biasen */
		pwr_reg |= (1 << 2) | (1 << 3);
		ret = snd_soc_write(codec, WM8940_POWER1, pwr_reg | 0x1);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			ret = regcache_sync(wm8940->regmap);
			if (ret < 0) {
				dev_err(codec->dev, "Failed to sync cache: %d\n", ret);
				return ret;
			}
		}

		/* ensure bufioen and biasen */
		pwr_reg |= (1 << 2) | (1 << 3);
		/* set vmid to 300k for standby */
		ret = snd_soc_write(codec, WM8940_POWER1, pwr_reg | 0x2);
		break;
	case SND_SOC_BIAS_OFF:
		ret = snd_soc_write(codec, WM8940_POWER1, pwr_reg);
		break;
	}

	return ret;
}

struct pll_ {
	unsigned int pre_scale:2;
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
	/* The left shift ist to avoid accuracy loss when right shifting */
	Ndiv = target / source;

	if (Ndiv > 12) {
		source <<= 1;
		/* Multiply by 2 */
		pll_div.pre_scale = 0;
		Ndiv = target / source;
	} else if (Ndiv < 3) {
		source >>= 2;
		/* Divide by 4 */
		pll_div.pre_scale = 3;
		Ndiv = target / source;
	} else if (Ndiv < 6) {
		source >>= 1;
		/* divide by 2 */
		pll_div.pre_scale = 2;
		Ndiv = target / source;
	} else
		pll_div.pre_scale = 1;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
			"WM8940 N value %d outwith recommended range!d\n",
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

/* Untested at the moment */
static int wm8940_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	/* Turn off PLL */
	reg = snd_soc_read(codec, WM8940_POWER1);
	snd_soc_write(codec, WM8940_POWER1, reg & 0x1df);

	if (freq_in == 0 || freq_out == 0) {
		/* Clock CODEC directly from MCLK */
		reg = snd_soc_read(codec, WM8940_CLOCK);
		snd_soc_write(codec, WM8940_CLOCK, reg & 0x0ff);
		/* Pll power down */
		snd_soc_write(codec, WM8940_PLLN, (1 << 7));
		return 0;
	}

	/* Pll is followed by a frequency divide by 4 */
	pll_factors(freq_out*4, freq_in);
	if (pll_div.k)
		snd_soc_write(codec, WM8940_PLLN,
			     (pll_div.pre_scale << 4) | pll_div.n | (1 << 6));
	else /* No factional component */
		snd_soc_write(codec, WM8940_PLLN,
			     (pll_div.pre_scale << 4) | pll_div.n);
	snd_soc_write(codec, WM8940_PLLK1, pll_div.k >> 18);
	snd_soc_write(codec, WM8940_PLLK2, (pll_div.k >> 9) & 0x1ff);
	snd_soc_write(codec, WM8940_PLLK3, pll_div.k & 0x1ff);
	/* Enable the PLL */
	reg = snd_soc_read(codec, WM8940_POWER1);
	snd_soc_write(codec, WM8940_POWER1, reg | 0x020);

	/* Run CODEC from PLL instead of MCLK */
	reg = snd_soc_read(codec, WM8940_CLOCK);
	snd_soc_write(codec, WM8940_CLOCK, reg | 0x100);

	return 0;
}

static int wm8940_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8940_priv *wm8940 = snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		wm8940->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8940_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
				 int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;
	int ret = 0;

	switch (div_id) {
	case WM8940_BCLKDIV:
		reg = snd_soc_read(codec, WM8940_CLOCK) & 0xFFE3;
		ret = snd_soc_write(codec, WM8940_CLOCK, reg | (div << 2));
		break;
	case WM8940_MCLKDIV:
		reg = snd_soc_read(codec, WM8940_CLOCK) & 0xFF1F;
		ret = snd_soc_write(codec, WM8940_CLOCK, reg | (div << 5));
		break;
	case WM8940_OPCLKDIV:
		reg = snd_soc_read(codec, WM8940_GPIO) & 0xFFCF;
		ret = snd_soc_write(codec, WM8940_GPIO, reg | (div << 4));
		break;
	}
	return ret;
}

#define WM8940_RATES SNDRV_PCM_RATE_8000_48000

#define WM8940_FORMATS (SNDRV_PCM_FMTBIT_S8 |				\
			SNDRV_PCM_FMTBIT_S16_LE |			\
			SNDRV_PCM_FMTBIT_S20_3LE |			\
			SNDRV_PCM_FMTBIT_S24_LE |			\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8940_dai_ops = {
	.hw_params = wm8940_i2s_hw_params,
	.set_sysclk = wm8940_set_dai_sysclk,
	.digital_mute = wm8940_mute,
	.set_fmt = wm8940_set_dai_fmt,
	.set_clkdiv = wm8940_set_dai_clkdiv,
	.set_pll = wm8940_set_dai_pll,
};

static struct snd_soc_dai_driver wm8940_dai = {
	.name = "wm8940-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8940_RATES,
		.formats = WM8940_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8940_RATES,
		.formats = WM8940_FORMATS,
	},
	.ops = &wm8940_dai_ops,
	.symmetric_rates = 1,
};

static int wm8940_probe(struct snd_soc_codec *codec)
{
	struct wm8940_setup_data *pdata = codec->dev->platform_data;
	int ret;
	u16 reg;

	ret = wm8940_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	snd_soc_codec_force_bias_level(codec, SND_SOC_BIAS_STANDBY);

	ret = snd_soc_write(codec, WM8940_POWER1, 0x180);
	if (ret < 0)
		return ret;

	if (!pdata)
		dev_warn(codec->dev, "No platform data supplied\n");
	else {
		reg = snd_soc_read(codec, WM8940_OUTPUTCTL);
		ret = snd_soc_write(codec, WM8940_OUTPUTCTL, reg | pdata->vroi);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8940 = {
	.probe =	wm8940_probe,
	.set_bias_level = wm8940_set_bias_level,
	.suspend_bias_off = true,

	.controls =     wm8940_snd_controls,
	.num_controls = ARRAY_SIZE(wm8940_snd_controls),
	.dapm_widgets = wm8940_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8940_dapm_widgets),
	.dapm_routes =  wm8940_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8940_dapm_routes),
};

static const struct regmap_config wm8940_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = WM8940_MONOMIX,
	.reg_defaults = wm8940_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8940_reg_defaults),

	.readable_reg = wm8940_readable_register,
	.volatile_reg = wm8940_volatile_register,
};

static int wm8940_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8940_priv *wm8940;
	int ret;

	wm8940 = devm_kzalloc(&i2c->dev, sizeof(struct wm8940_priv),
			      GFP_KERNEL);
	if (wm8940 == NULL)
		return -ENOMEM;

	wm8940->regmap = devm_regmap_init_i2c(i2c, &wm8940_regmap);
	if (IS_ERR(wm8940->regmap))
		return PTR_ERR(wm8940->regmap);

	i2c_set_clientdata(i2c, wm8940);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8940, &wm8940_dai, 1);

	return ret;
}

static int wm8940_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static const struct i2c_device_id wm8940_i2c_id[] = {
	{ "wm8940", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8940_i2c_id);

static struct i2c_driver wm8940_i2c_driver = {
	.driver = {
		.name = "wm8940",
		.owner = THIS_MODULE,
	},
	.probe =    wm8940_i2c_probe,
	.remove =   wm8940_i2c_remove,
	.id_table = wm8940_i2c_id,
};

module_i2c_driver(wm8940_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8940 driver");
MODULE_AUTHOR("Jonathan Cameron");
MODULE_LICENSE("GPL");
