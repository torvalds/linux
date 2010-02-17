/*
 * wm8960.c  --  WM8960 ALSA SoC Audio driver
 *
 * Author: Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
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
#include <sound/tlv.h>

#include "wm8960.h"

#define AUDIO_NAME "wm8960"

struct snd_soc_codec_device soc_codec_dev_wm8960;

/* R25 - Power 1 */
#define WM8960_VREF      0x40

/* R28 - Anti-pop 1 */
#define WM8960_POBCTRL   0x80
#define WM8960_BUFDCOPEN 0x10
#define WM8960_BUFIOEN   0x08
#define WM8960_SOFT_ST   0x04
#define WM8960_HPSTBY    0x01

/* R29 - Anti-pop 2 */
#define WM8960_DISOP     0x40

/*
 * wm8960 register cache
 * We can't read the WM8960 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8960_reg[WM8960_CACHEREGNUM] = {
	0x0097, 0x0097, 0x0000, 0x0000,
	0x0000, 0x0008, 0x0000, 0x000a,
	0x01c0, 0x0000, 0x00ff, 0x00ff,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x007b, 0x0100, 0x0032,
	0x0000, 0x00c3, 0x00c3, 0x01c0,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0100, 0x0100, 0x0050, 0x0050,
	0x0050, 0x0050, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0040, 0x0000,
	0x0000, 0x0050, 0x0050, 0x0000,
	0x0002, 0x0037, 0x004d, 0x0080,
	0x0008, 0x0031, 0x0026, 0x00e9,
};

struct wm8960_priv {
	u16 reg_cache[WM8960_CACHEREGNUM];
	struct snd_soc_codec codec;
};

#define wm8960_reset(c)	snd_soc_write(c, WM8960_RESET, 0)

/* enumerated controls */
static const char *wm8960_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static const char *wm8960_polarity[] = {"No Inversion", "Left Inverted",
	"Right Inverted", "Stereo Inversion"};
static const char *wm8960_3d_upper_cutoff[] = {"High", "Low"};
static const char *wm8960_3d_lower_cutoff[] = {"Low", "High"};
static const char *wm8960_alcfunc[] = {"Off", "Right", "Left", "Stereo"};
static const char *wm8960_alcmode[] = {"ALC", "Limiter"};

static const struct soc_enum wm8960_enum[] = {
	SOC_ENUM_SINGLE(WM8960_DACCTL1, 1, 4, wm8960_deemph),
	SOC_ENUM_SINGLE(WM8960_DACCTL1, 5, 4, wm8960_polarity),
	SOC_ENUM_SINGLE(WM8960_DACCTL2, 5, 4, wm8960_polarity),
	SOC_ENUM_SINGLE(WM8960_3D, 6, 2, wm8960_3d_upper_cutoff),
	SOC_ENUM_SINGLE(WM8960_3D, 5, 2, wm8960_3d_lower_cutoff),
	SOC_ENUM_SINGLE(WM8960_ALC1, 7, 4, wm8960_alcfunc),
	SOC_ENUM_SINGLE(WM8960_ALC3, 8, 2, wm8960_alcmode),
};

static const DECLARE_TLV_DB_SCALE(adc_tlv, -9700, 50, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12700, 50, 1);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -2100, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);

static const struct snd_kcontrol_new wm8960_snd_controls[] = {
SOC_DOUBLE_R_TLV("Capture Volume", WM8960_LINVOL, WM8960_RINVOL,
		 0, 63, 0, adc_tlv),
SOC_DOUBLE_R("Capture Volume ZC Switch", WM8960_LINVOL, WM8960_RINVOL,
	6, 1, 0),
SOC_DOUBLE_R("Capture Switch", WM8960_LINVOL, WM8960_RINVOL,
	7, 1, 0),

SOC_DOUBLE_R_TLV("Playback Volume", WM8960_LDAC, WM8960_RDAC,
		 0, 255, 0, dac_tlv),

SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8960_LOUT1, WM8960_ROUT1,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8960_LOUT1, WM8960_ROUT1,
	7, 1, 0),

SOC_DOUBLE_R_TLV("Speaker Playback Volume", WM8960_LOUT2, WM8960_ROUT2,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8960_LOUT2, WM8960_ROUT2,
	7, 1, 0),
SOC_SINGLE("Speaker DC Volume", WM8960_CLASSD3, 3, 5, 0),
SOC_SINGLE("Speaker AC Volume", WM8960_CLASSD3, 0, 5, 0),

SOC_SINGLE("PCM Playback -6dB Switch", WM8960_DACCTL1, 7, 1, 0),
SOC_ENUM("ADC Polarity", wm8960_enum[1]),
SOC_ENUM("Playback De-emphasis", wm8960_enum[0]),
SOC_SINGLE("ADC High Pass Filter Switch", WM8960_DACCTL1, 0, 1, 0),

SOC_ENUM("DAC Polarity", wm8960_enum[2]),

SOC_ENUM("3D Filter Upper Cut-Off", wm8960_enum[3]),
SOC_ENUM("3D Filter Lower Cut-Off", wm8960_enum[4]),
SOC_SINGLE("3D Volume", WM8960_3D, 1, 15, 0),
SOC_SINGLE("3D Switch", WM8960_3D, 0, 1, 0),

SOC_ENUM("ALC Function", wm8960_enum[5]),
SOC_SINGLE("ALC Max Gain", WM8960_ALC1, 4, 7, 0),
SOC_SINGLE("ALC Target", WM8960_ALC1, 0, 15, 1),
SOC_SINGLE("ALC Min Gain", WM8960_ALC2, 4, 7, 0),
SOC_SINGLE("ALC Hold Time", WM8960_ALC2, 0, 15, 0),
SOC_ENUM("ALC Mode", wm8960_enum[6]),
SOC_SINGLE("ALC Decay", WM8960_ALC3, 4, 15, 0),
SOC_SINGLE("ALC Attack", WM8960_ALC3, 0, 15, 0),

SOC_SINGLE("Noise Gate Threshold", WM8960_NOISEG, 3, 31, 0),
SOC_SINGLE("Noise Gate Switch", WM8960_NOISEG, 0, 1, 0),

SOC_DOUBLE_R("ADC PCM Capture Volume", WM8960_LINPATH, WM8960_RINPATH,
	0, 127, 0),

SOC_SINGLE_TLV("Left Output Mixer Boost Bypass Volume",
	       WM8960_BYPASS1, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Left Output Mixer LINPUT3 Volume",
	       WM8960_LOUTMIX, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Right Output Mixer Boost Bypass Volume",
	       WM8960_BYPASS2, 4, 7, 1, bypass_tlv),
SOC_SINGLE_TLV("Right Output Mixer RINPUT3 Volume",
	       WM8960_ROUTMIX, 4, 7, 1, bypass_tlv),
};

static const struct snd_kcontrol_new wm8960_lin_boost[] = {
SOC_DAPM_SINGLE("LINPUT2 Switch", WM8960_LINPATH, 6, 1, 0),
SOC_DAPM_SINGLE("LINPUT3 Switch", WM8960_LINPATH, 7, 1, 0),
SOC_DAPM_SINGLE("LINPUT1 Switch", WM8960_LINPATH, 8, 1, 0),
};

static const struct snd_kcontrol_new wm8960_lin[] = {
SOC_DAPM_SINGLE("Boost Switch", WM8960_LINPATH, 3, 1, 0),
};

static const struct snd_kcontrol_new wm8960_rin_boost[] = {
SOC_DAPM_SINGLE("RINPUT2 Switch", WM8960_RINPATH, 6, 1, 0),
SOC_DAPM_SINGLE("RINPUT3 Switch", WM8960_RINPATH, 7, 1, 0),
SOC_DAPM_SINGLE("RINPUT1 Switch", WM8960_RINPATH, 8, 1, 0),
};

static const struct snd_kcontrol_new wm8960_rin[] = {
SOC_DAPM_SINGLE("Boost Switch", WM8960_RINPATH, 3, 1, 0),
};

static const struct snd_kcontrol_new wm8960_loutput_mixer[] = {
SOC_DAPM_SINGLE("PCM Playback Switch", WM8960_LOUTMIX, 8, 1, 0),
SOC_DAPM_SINGLE("LINPUT3 Switch", WM8960_LOUTMIX, 7, 1, 0),
SOC_DAPM_SINGLE("Boost Bypass Switch", WM8960_BYPASS1, 7, 1, 0),
};

static const struct snd_kcontrol_new wm8960_routput_mixer[] = {
SOC_DAPM_SINGLE("PCM Playback Switch", WM8960_ROUTMIX, 8, 1, 0),
SOC_DAPM_SINGLE("RINPUT3 Switch", WM8960_ROUTMIX, 7, 1, 0),
SOC_DAPM_SINGLE("Boost Bypass Switch", WM8960_BYPASS2, 7, 1, 0),
};

static const struct snd_kcontrol_new wm8960_mono_out[] = {
SOC_DAPM_SINGLE("Left Switch", WM8960_MONOMIX1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Switch", WM8960_MONOMIX2, 7, 1, 0),
};

static const struct snd_soc_dapm_widget wm8960_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("LINPUT1"),
SND_SOC_DAPM_INPUT("RINPUT1"),
SND_SOC_DAPM_INPUT("LINPUT2"),
SND_SOC_DAPM_INPUT("RINPUT2"),
SND_SOC_DAPM_INPUT("LINPUT3"),
SND_SOC_DAPM_INPUT("RINPUT3"),

SND_SOC_DAPM_MICBIAS("MICB", WM8960_POWER1, 1, 0),

SND_SOC_DAPM_MIXER("Left Boost Mixer", WM8960_POWER1, 5, 0,
		   wm8960_lin_boost, ARRAY_SIZE(wm8960_lin_boost)),
SND_SOC_DAPM_MIXER("Right Boost Mixer", WM8960_POWER1, 4, 0,
		   wm8960_rin_boost, ARRAY_SIZE(wm8960_rin_boost)),

SND_SOC_DAPM_MIXER("Left Input Mixer", WM8960_POWER3, 5, 0,
		   wm8960_lin, ARRAY_SIZE(wm8960_lin)),
SND_SOC_DAPM_MIXER("Right Input Mixer", WM8960_POWER3, 4, 0,
		   wm8960_rin, ARRAY_SIZE(wm8960_rin)),

SND_SOC_DAPM_ADC("Left ADC", "Capture", WM8960_POWER2, 3, 0),
SND_SOC_DAPM_ADC("Right ADC", "Capture", WM8960_POWER2, 2, 0),

SND_SOC_DAPM_DAC("Left DAC", "Playback", WM8960_POWER2, 8, 0),
SND_SOC_DAPM_DAC("Right DAC", "Playback", WM8960_POWER2, 7, 0),

SND_SOC_DAPM_MIXER("Left Output Mixer", WM8960_POWER3, 3, 0,
	&wm8960_loutput_mixer[0],
	ARRAY_SIZE(wm8960_loutput_mixer)),
SND_SOC_DAPM_MIXER("Right Output Mixer", WM8960_POWER3, 2, 0,
	&wm8960_routput_mixer[0],
	ARRAY_SIZE(wm8960_routput_mixer)),

SND_SOC_DAPM_MIXER("Mono Output Mixer", WM8960_POWER2, 1, 0,
	&wm8960_mono_out[0],
	ARRAY_SIZE(wm8960_mono_out)),

SND_SOC_DAPM_PGA("LOUT1 PGA", WM8960_POWER2, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("ROUT1 PGA", WM8960_POWER2, 5, 0, NULL, 0),

SND_SOC_DAPM_PGA("Left Speaker PGA", WM8960_POWER2, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Speaker PGA", WM8960_POWER2, 3, 0, NULL, 0),

SND_SOC_DAPM_PGA("Right Speaker Output", WM8960_CLASSD1, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left Speaker Output", WM8960_CLASSD1, 6, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("SPK_LP"),
SND_SOC_DAPM_OUTPUT("SPK_LN"),
SND_SOC_DAPM_OUTPUT("HP_L"),
SND_SOC_DAPM_OUTPUT("HP_R"),
SND_SOC_DAPM_OUTPUT("SPK_RP"),
SND_SOC_DAPM_OUTPUT("SPK_RN"),
SND_SOC_DAPM_OUTPUT("OUT3"),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "Left Boost Mixer", "LINPUT1 Switch", "LINPUT1" },
	{ "Left Boost Mixer", "LINPUT2 Switch", "LINPUT2" },
	{ "Left Boost Mixer", "LINPUT3 Switch", "LINPUT3" },

	{ "Left Input Mixer", "Boost Switch", "Left Boost Mixer", },
	{ "Left Input Mixer", NULL, "LINPUT1", },  /* Really Boost Switch */
	{ "Left Input Mixer", NULL, "LINPUT2" },
	{ "Left Input Mixer", NULL, "LINPUT3" },

	{ "Right Boost Mixer", "RINPUT1 Switch", "RINPUT1" },
	{ "Right Boost Mixer", "RINPUT2 Switch", "RINPUT2" },
	{ "Right Boost Mixer", "RINPUT3 Switch", "RINPUT3" },

	{ "Right Input Mixer", "Boost Switch", "Right Boost Mixer", },
	{ "Right Input Mixer", NULL, "RINPUT1", },  /* Really Boost Switch */
	{ "Right Input Mixer", NULL, "RINPUT2" },
	{ "Right Input Mixer", NULL, "LINPUT3" },

	{ "Left ADC", NULL, "Left Input Mixer" },
	{ "Right ADC", NULL, "Right Input Mixer" },

	{ "Left Output Mixer", "LINPUT3 Switch", "LINPUT3" },
	{ "Left Output Mixer", "Boost Bypass Switch", "Left Boost Mixer"} ,
	{ "Left Output Mixer", "PCM Playback Switch", "Left DAC" },

	{ "Right Output Mixer", "RINPUT3 Switch", "RINPUT3" },
	{ "Right Output Mixer", "Boost Bypass Switch", "Right Boost Mixer" } ,
	{ "Right Output Mixer", "PCM Playback Switch", "Right DAC" },

	{ "Mono Output Mixer", "Left Switch", "Left Output Mixer" },
	{ "Mono Output Mixer", "Right Switch", "Right Output Mixer" },

	{ "LOUT1 PGA", NULL, "Left Output Mixer" },
	{ "ROUT1 PGA", NULL, "Right Output Mixer" },

	{ "HP_L", NULL, "LOUT1 PGA" },
	{ "HP_R", NULL, "ROUT1 PGA" },

	{ "Left Speaker PGA", NULL, "Left Output Mixer" },
	{ "Right Speaker PGA", NULL, "Right Output Mixer" },

	{ "Left Speaker Output", NULL, "Left Speaker PGA" },
	{ "Right Speaker Output", NULL, "Right Speaker PGA" },

	{ "SPK_LN", NULL, "Left Speaker Output" },
	{ "SPK_LP", NULL, "Left Speaker Output" },
	{ "SPK_RN", NULL, "Right Speaker Output" },
	{ "SPK_RP", NULL, "Right Speaker Output" },

	{ "OUT3", NULL, "Mono Output Mixer", }
};

static int wm8960_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8960_dapm_widgets,
				  ARRAY_SIZE(wm8960_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_paths, ARRAY_SIZE(audio_paths));

	return 0;
}

static int wm8960_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= 0x0040;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	snd_soc_write(codec, WM8960_IFACE1, iface);
	return 0;
}

static int wm8960_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 iface = snd_soc_read(codec, WM8960_IFACE1) & 0xfff3;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	}

	/* set iface */
	snd_soc_write(codec, WM8960_IFACE1, iface);
	return 0;
}

static int wm8960_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8960_DACCTL1) & 0xfff7;

	if (mute)
		snd_soc_write(codec, WM8960_DACCTL1, mute_reg | 0x8);
	else
		snd_soc_write(codec, WM8960_DACCTL1, mute_reg);
	return 0;
}

static int wm8960_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8960_data *pdata = codec->dev->platform_data;
	u16 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Set VMID to 2x50k */
		reg = snd_soc_read(codec, WM8960_POWER1);
		reg &= ~0x180;
		reg |= 0x80;
		snd_soc_write(codec, WM8960_POWER1, reg);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* Enable anti-pop features */
			snd_soc_write(codec, WM8960_APOP1,
				     WM8960_POBCTRL | WM8960_SOFT_ST |
				     WM8960_BUFDCOPEN | WM8960_BUFIOEN);

			/* Discharge HP output */
			reg = WM8960_DISOP;
			if (pdata)
				reg |= pdata->dres << 4;
			snd_soc_write(codec, WM8960_APOP2, reg);

			msleep(400);

			snd_soc_write(codec, WM8960_APOP2, 0);

			/* Enable & ramp VMID at 2x50k */
			reg = snd_soc_read(codec, WM8960_POWER1);
			reg |= 0x80;
			snd_soc_write(codec, WM8960_POWER1, reg);
			msleep(100);

			/* Enable VREF */
			snd_soc_write(codec, WM8960_POWER1, reg | WM8960_VREF);

			/* Disable anti-pop features */
			snd_soc_write(codec, WM8960_APOP1, WM8960_BUFIOEN);
		}

		/* Set VMID to 2x250k */
		reg = snd_soc_read(codec, WM8960_POWER1);
		reg &= ~0x180;
		reg |= 0x100;
		snd_soc_write(codec, WM8960_POWER1, reg);
		break;

	case SND_SOC_BIAS_OFF:
		/* Enable anti-pop features */
		snd_soc_write(codec, WM8960_APOP1,
			     WM8960_POBCTRL | WM8960_SOFT_ST |
			     WM8960_BUFDCOPEN | WM8960_BUFIOEN);

		/* Disable VMID and VREF, let them discharge */
		snd_soc_write(codec, WM8960_POWER1, 0);
		msleep(600);

		snd_soc_write(codec, WM8960_APOP1, 0);
		break;
	}

	codec->bias_level = level;

	return 0;
}

/* PLL divisors */
struct _pll_div {
	u32 pre_div:1;
	u32 n:4;
	u32 k:24;
};

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 24) * 10)

static int pll_factors(unsigned int source, unsigned int target,
		       struct _pll_div *pll_div)
{
	unsigned long long Kpart;
	unsigned int K, Ndiv, Nmod;

	pr_debug("WM8960 PLL: setting %dHz->%dHz\n", source, target);

	/* Scale up target to PLL operating frequency */
	target *= 4;

	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div->pre_div = 1;
		Ndiv = target / source;
	} else
		pll_div->pre_div = 0;

	if ((Ndiv < 6) || (Ndiv > 12)) {
		pr_err("WM8960 PLL: Unsupported N=%d\n", Ndiv);
		return -EINVAL;
	}

	pll_div->n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div->k = K;

	pr_debug("WM8960 PLL: N=%x K=%x pre_div=%d\n",
		 pll_div->n, pll_div->k, pll_div->pre_div);

	return 0;
}

static int wm8960_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;
	static struct _pll_div pll_div;
	int ret;

	if (freq_in && freq_out) {
		ret = pll_factors(freq_in, freq_out, &pll_div);
		if (ret != 0)
			return ret;
	}

	/* Disable the PLL: even if we are changing the frequency the
	 * PLL needs to be disabled while we do so. */
	snd_soc_write(codec, WM8960_CLOCK1,
		     snd_soc_read(codec, WM8960_CLOCK1) & ~1);
	snd_soc_write(codec, WM8960_POWER2,
		     snd_soc_read(codec, WM8960_POWER2) & ~1);

	if (!freq_in || !freq_out)
		return 0;

	reg = snd_soc_read(codec, WM8960_PLL1) & ~0x3f;
	reg |= pll_div.pre_div << 4;
	reg |= pll_div.n;

	if (pll_div.k) {
		reg |= 0x20;

		snd_soc_write(codec, WM8960_PLL2, (pll_div.k >> 18) & 0x3f);
		snd_soc_write(codec, WM8960_PLL3, (pll_div.k >> 9) & 0x1ff);
		snd_soc_write(codec, WM8960_PLL4, pll_div.k & 0x1ff);
	}
	snd_soc_write(codec, WM8960_PLL1, reg);

	/* Turn it on */
	snd_soc_write(codec, WM8960_POWER2,
		     snd_soc_read(codec, WM8960_POWER2) | 1);
	msleep(250);
	snd_soc_write(codec, WM8960_CLOCK1,
		     snd_soc_read(codec, WM8960_CLOCK1) | 1);

	return 0;
}

static int wm8960_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8960_SYSCLKSEL:
		reg = snd_soc_read(codec, WM8960_CLOCK1) & 0x1fe;
		snd_soc_write(codec, WM8960_CLOCK1, reg | div);
		break;
	case WM8960_SYSCLKDIV:
		reg = snd_soc_read(codec, WM8960_CLOCK1) & 0x1f9;
		snd_soc_write(codec, WM8960_CLOCK1, reg | div);
		break;
	case WM8960_DACDIV:
		reg = snd_soc_read(codec, WM8960_CLOCK1) & 0x1c7;
		snd_soc_write(codec, WM8960_CLOCK1, reg | div);
		break;
	case WM8960_OPCLKDIV:
		reg = snd_soc_read(codec, WM8960_PLL1) & 0x03f;
		snd_soc_write(codec, WM8960_PLL1, reg | div);
		break;
	case WM8960_DCLKDIV:
		reg = snd_soc_read(codec, WM8960_CLOCK2) & 0x03f;
		snd_soc_write(codec, WM8960_CLOCK2, reg | div);
		break;
	case WM8960_TOCLKSEL:
		reg = snd_soc_read(codec, WM8960_ADDCTL1) & 0x1fd;
		snd_soc_write(codec, WM8960_ADDCTL1, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define WM8960_RATES SNDRV_PCM_RATE_8000_48000

#define WM8960_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8960_dai_ops = {
	.hw_params = wm8960_hw_params,
	.digital_mute = wm8960_mute,
	.set_fmt = wm8960_set_dai_fmt,
	.set_clkdiv = wm8960_set_dai_clkdiv,
	.set_pll = wm8960_set_dai_pll,
};

struct snd_soc_dai wm8960_dai = {
	.name = "WM8960",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8960_RATES,
		.formats = WM8960_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8960_RATES,
		.formats = WM8960_FORMATS,},
	.ops = &wm8960_dai_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(wm8960_dai);

static int wm8960_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8960_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8960_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8960_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}

	wm8960_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8960_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

static struct snd_soc_codec *wm8960_codec;

static int wm8960_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8960_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8960_codec;
	codec = wm8960_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8960_snd_controls,
			     ARRAY_SIZE(wm8960_snd_controls));
	wm8960_add_widgets(codec);

	return ret;

pcm_err:
	return ret;
}

/* power down chip */
static int wm8960_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8960 = {
	.probe = 	wm8960_probe,
	.remove = 	wm8960_remove,
	.suspend = 	wm8960_suspend,
	.resume =	wm8960_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8960);

static int wm8960_register(struct wm8960_priv *wm8960,
			   enum snd_soc_control_type control)
{
	struct wm8960_data *pdata = wm8960->codec.dev->platform_data;
	struct snd_soc_codec *codec = &wm8960->codec;
	int ret;
	u16 reg;

	if (wm8960_codec) {
		dev_err(codec->dev, "Another WM8960 is registered\n");
		ret = -EINVAL;
		goto err;
	}

	if (!pdata) {
		dev_warn(codec->dev, "No platform data supplied\n");
	} else {
		if (pdata->dres > WM8960_DRES_MAX) {
			dev_err(codec->dev, "Invalid DRES: %d\n", pdata->dres);
			pdata->dres = 0;
		}
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data = wm8960;
	codec->name = "WM8960";
	codec->owner = THIS_MODULE;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8960_set_bias_level;
	codec->dai = &wm8960_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8960_CACHEREGNUM;
	codec->reg_cache = &wm8960->reg_cache;

	memcpy(codec->reg_cache, wm8960_reg, sizeof(wm8960_reg));

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, control);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	ret = wm8960_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err;
	}

	wm8960_dai.dev = codec->dev;

	wm8960_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Latch the update bits */
	reg = snd_soc_read(codec, WM8960_LINVOL);
	snd_soc_write(codec, WM8960_LINVOL, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_RINVOL);
	snd_soc_write(codec, WM8960_RINVOL, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_LADC);
	snd_soc_write(codec, WM8960_LADC, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_RADC);
	snd_soc_write(codec, WM8960_RADC, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_LDAC);
	snd_soc_write(codec, WM8960_LDAC, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_RDAC);
	snd_soc_write(codec, WM8960_RDAC, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_LOUT1);
	snd_soc_write(codec, WM8960_LOUT1, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_ROUT1);
	snd_soc_write(codec, WM8960_ROUT1, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_LOUT2);
	snd_soc_write(codec, WM8960_LOUT2, reg | 0x100);
	reg = snd_soc_read(codec, WM8960_ROUT2);
	snd_soc_write(codec, WM8960_ROUT2, reg | 0x100);

	wm8960_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

	ret = snd_soc_register_dai(&wm8960_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}

	return 0;

err_codec:
	snd_soc_unregister_codec(codec);
err:
	kfree(wm8960);
	return ret;
}

static void wm8960_unregister(struct wm8960_priv *wm8960)
{
	wm8960_set_bias_level(&wm8960->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8960_dai);
	snd_soc_unregister_codec(&wm8960->codec);
	kfree(wm8960);
	wm8960_codec = NULL;
}

static __devinit int wm8960_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8960_priv *wm8960;
	struct snd_soc_codec *codec;

	wm8960 = kzalloc(sizeof(struct wm8960_priv), GFP_KERNEL);
	if (wm8960 == NULL)
		return -ENOMEM;

	codec = &wm8960->codec;

	i2c_set_clientdata(i2c, wm8960);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8960_register(wm8960, SND_SOC_I2C);
}

static __devexit int wm8960_i2c_remove(struct i2c_client *client)
{
	struct wm8960_priv *wm8960 = i2c_get_clientdata(client);
	wm8960_unregister(wm8960);
	return 0;
}

static const struct i2c_device_id wm8960_i2c_id[] = {
	{ "wm8960", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8960_i2c_id);

static struct i2c_driver wm8960_i2c_driver = {
	.driver = {
		.name = "WM8960 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8960_i2c_probe,
	.remove =   __devexit_p(wm8960_i2c_remove),
	.id_table = wm8960_i2c_id,
};

static int __init wm8960_modinit(void)
{
	int ret;

	ret = i2c_add_driver(&wm8960_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8960 I2C driver: %d\n",
		       ret);
	}

	return ret;
}
module_init(wm8960_modinit);

static void __exit wm8960_exit(void)
{
	i2c_del_driver(&wm8960_i2c_driver);
}
module_exit(wm8960_exit);


MODULE_DESCRIPTION("ASoC WM8960 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
