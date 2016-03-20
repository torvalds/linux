/*
 * wm8955.c  --  WM8955 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8955.h>

#include "wm8955.h"

#define WM8955_NUM_SUPPLIES 4
static const char *wm8955_supply_names[WM8955_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"HPVDD",
	"AVDD",
};

/* codec private data */
struct wm8955_priv {
	struct regmap *regmap;

	unsigned int mclk_rate;

	int deemph;
	int fs;

	struct regulator_bulk_data supplies[WM8955_NUM_SUPPLIES];
};

static const struct reg_default wm8955_reg_defaults[] = {
	{ 2,  0x0079 },     /* R2  - LOUT1 volume */
	{ 3,  0x0079 },     /* R3  - ROUT1 volume */
	{ 5,  0x0008 },     /* R5  - DAC Control */
	{ 7,  0x000A },     /* R7  - Audio Interface */
	{ 8,  0x0000 },     /* R8  - Sample Rate */
	{ 10, 0x00FF },     /* R10 - Left DAC volume */
	{ 11, 0x00FF },     /* R11 - Right DAC volume */
	{ 12, 0x000F },     /* R12 - Bass control */
	{ 13, 0x000F },     /* R13 - Treble control */
	{ 23, 0x00C1 },     /* R23 - Additional control (1) */
	{ 24, 0x0000 },     /* R24 - Additional control (2) */
	{ 25, 0x0000 },     /* R25 - Power Management (1) */
	{ 26, 0x0000 },     /* R26 - Power Management (2) */
	{ 27, 0x0000 },     /* R27 - Additional Control (3) */
	{ 34, 0x0050 },     /* R34 - Left out Mix (1) */
	{ 35, 0x0050 },     /* R35 - Left out Mix (2) */
	{ 36, 0x0050 },     /* R36 - Right out Mix (1) */
	{ 37, 0x0050 },     /* R37 - Right Out Mix (2) */
	{ 38, 0x0050 },     /* R38 - Mono out Mix (1) */
	{ 39, 0x0050 },     /* R39 - Mono out Mix (2) */
	{ 40, 0x0079 },     /* R40 - LOUT2 volume */
	{ 41, 0x0079 },     /* R41 - ROUT2 volume */
	{ 42, 0x0079 },     /* R42 - MONOOUT volume */
	{ 43, 0x0000 },     /* R43 - Clocking / PLL */
	{ 44, 0x0103 },     /* R44 - PLL Control 1 */
	{ 45, 0x0024 },     /* R45 - PLL Control 2 */
	{ 46, 0x01BA },     /* R46 - PLL Control 3 */
	{ 59, 0x0000 },     /* R59 - PLL Control 4 */
};

static bool wm8955_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8955_LOUT1_VOLUME:
	case WM8955_ROUT1_VOLUME:
	case WM8955_DAC_CONTROL:
	case WM8955_AUDIO_INTERFACE:
	case WM8955_SAMPLE_RATE:
	case WM8955_LEFT_DAC_VOLUME:
	case WM8955_RIGHT_DAC_VOLUME:
	case WM8955_BASS_CONTROL:
	case WM8955_TREBLE_CONTROL:
	case WM8955_RESET:
	case WM8955_ADDITIONAL_CONTROL_1:
	case WM8955_ADDITIONAL_CONTROL_2:
	case WM8955_POWER_MANAGEMENT_1:
	case WM8955_POWER_MANAGEMENT_2:
	case WM8955_ADDITIONAL_CONTROL_3:
	case WM8955_LEFT_OUT_MIX_1:
	case WM8955_LEFT_OUT_MIX_2:
	case WM8955_RIGHT_OUT_MIX_1:
	case WM8955_RIGHT_OUT_MIX_2:
	case WM8955_MONO_OUT_MIX_1:
	case WM8955_MONO_OUT_MIX_2:
	case WM8955_LOUT2_VOLUME:
	case WM8955_ROUT2_VOLUME:
	case WM8955_MONOOUT_VOLUME:
	case WM8955_CLOCKING_PLL:
	case WM8955_PLL_CONTROL_1:
	case WM8955_PLL_CONTROL_2:
	case WM8955_PLL_CONTROL_3:
	case WM8955_PLL_CONTROL_4:
		return true;
	default:
		return false;
	}
}

static bool wm8955_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8955_RESET:
		return true;
	default:
		return false;
	}
}

static int wm8955_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8955_RESET, 0);
}

struct pll_factors {
	int n;
	int k;
	int outdiv;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 22) * 10)

static int wm8995_pll_factors(struct device *dev,
			      int Fref, int Fout, struct pll_factors *pll)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;

	dev_dbg(dev, "Fref=%u Fout=%u\n", Fref, Fout);

	/* The oscilator should run at should be 90-100MHz, and
	 * there's a divide by 4 plus an optional divide by 2 in the
	 * output path to generate the system clock.  The clock table
	 * is sortd so we should always generate a suitable target. */
	target = Fout * 4;
	if (target < 90000000) {
		pll->outdiv = 1;
		target *= 2;
	} else {
		pll->outdiv = 0;
	}

	WARN_ON(target < 90000000 || target > 100000000);

	dev_dbg(dev, "Fvco=%dHz\n", target);

	/* Now, calculate N.K */
	Ndiv = target / Fref;

	pll->n = Ndiv;
	Nmod = target % Fref;
	dev_dbg(dev, "Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	pll->k = K / 10;

	dev_dbg(dev, "N=%x K=%x OUTDIV=%x\n", pll->n, pll->k, pll->outdiv);

	return 0;
}

/* Lookup table specifying SRATE (table 25 in datasheet); some of the
 * output frequencies have been rounded to the standard frequencies
 * they are intended to match where the error is slight. */
static struct {
	int mclk;
	int fs;
	int usb;
	int sr;
} clock_cfgs[] = {
	{ 18432000,  8000, 0,  3, },
	{ 18432000, 12000, 0,  9, },
	{ 18432000, 16000, 0, 11, },
	{ 18432000, 24000, 0, 29, },
	{ 18432000, 32000, 0, 13, },
	{ 18432000, 48000, 0,  1, },
	{ 18432000, 96000, 0, 15, },

	{ 16934400,  8018, 0, 19, },
	{ 16934400, 11025, 0, 25, },
	{ 16934400, 22050, 0, 27, },
	{ 16934400, 44100, 0, 17, },
	{ 16934400, 88200, 0, 31, },

	{ 12000000,  8000, 1,  2, },
	{ 12000000, 11025, 1, 25, },
	{ 12000000, 12000, 1,  8, },
	{ 12000000, 16000, 1, 10, },
	{ 12000000, 22050, 1, 27, },
	{ 12000000, 24000, 1, 28, },
	{ 12000000, 32000, 1, 12, },
	{ 12000000, 44100, 1, 17, },
	{ 12000000, 48000, 1,  0, },
	{ 12000000, 88200, 1, 31, },
	{ 12000000, 96000, 1, 14, },

	{ 12288000,  8000, 0,  2, },
	{ 12288000, 12000, 0,  8, },
	{ 12288000, 16000, 0, 10, },
	{ 12288000, 24000, 0, 28, },
	{ 12288000, 32000, 0, 12, },
	{ 12288000, 48000, 0,  0, },
	{ 12288000, 96000, 0, 14, },

	{ 12289600,  8018, 0, 18, },
	{ 12289600, 11025, 0, 24, },
	{ 12289600, 22050, 0, 26, },
	{ 11289600, 44100, 0, 16, },
	{ 11289600, 88200, 0, 31, },
};

static int wm8955_configure_clocking(struct snd_soc_codec *codec)
{
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	int i, ret, val;
	int clocking = 0;
	int srate = 0;
	int sr = -1;
	struct pll_factors pll;

	/* If we're not running a sample rate currently just pick one */
	if (wm8955->fs == 0)
		wm8955->fs = 8000;

	/* Can we generate an exact output? */
	for (i = 0; i < ARRAY_SIZE(clock_cfgs); i++) {
		if (wm8955->fs != clock_cfgs[i].fs)
			continue;
		sr = i;

		if (wm8955->mclk_rate == clock_cfgs[i].mclk)
			break;
	}

	/* We should never get here with an unsupported sample rate */
	if (sr == -1) {
		dev_err(codec->dev, "Sample rate %dHz unsupported\n",
			wm8955->fs);
		WARN_ON(sr == -1);
		return -EINVAL;
	}

	if (i == ARRAY_SIZE(clock_cfgs)) {
		/* If we can't generate the right clock from MCLK then
		 * we should configure the PLL to supply us with an
		 * appropriate clock.
		 */
		clocking |= WM8955_MCLKSEL;

		/* Use the last divider configuration we saw for the
		 * sample rate. */
		ret = wm8995_pll_factors(codec->dev, wm8955->mclk_rate,
					 clock_cfgs[sr].mclk, &pll);
		if (ret != 0) {
			dev_err(codec->dev,
				"Unable to generate %dHz from %dHz MCLK\n",
				wm8955->fs, wm8955->mclk_rate);
			return -EINVAL;
		}

		snd_soc_update_bits(codec, WM8955_PLL_CONTROL_1,
				    WM8955_N_MASK | WM8955_K_21_18_MASK,
				    (pll.n << WM8955_N_SHIFT) |
				    pll.k >> 18);
		snd_soc_update_bits(codec, WM8955_PLL_CONTROL_2,
				    WM8955_K_17_9_MASK,
				    (pll.k >> 9) & WM8955_K_17_9_MASK);
		snd_soc_update_bits(codec, WM8955_PLL_CONTROL_3,
				    WM8955_K_8_0_MASK,
				    pll.k & WM8955_K_8_0_MASK);
		if (pll.k)
			snd_soc_update_bits(codec, WM8955_PLL_CONTROL_4,
					    WM8955_KEN, WM8955_KEN);
		else
			snd_soc_update_bits(codec, WM8955_PLL_CONTROL_4,
					    WM8955_KEN, 0);

		if (pll.outdiv)
			val = WM8955_PLL_RB | WM8955_PLLOUTDIV2;
		else
			val = WM8955_PLL_RB;

		/* Now start the PLL running */
		snd_soc_update_bits(codec, WM8955_CLOCKING_PLL,
				    WM8955_PLL_RB | WM8955_PLLOUTDIV2, val);
		snd_soc_update_bits(codec, WM8955_CLOCKING_PLL,
				    WM8955_PLLEN, WM8955_PLLEN);
	}

	srate = clock_cfgs[sr].usb | (clock_cfgs[sr].sr << WM8955_SR_SHIFT);

	snd_soc_update_bits(codec, WM8955_SAMPLE_RATE,
			    WM8955_USB | WM8955_SR_MASK, srate);
	snd_soc_update_bits(codec, WM8955_CLOCKING_PLL,
			    WM8955_MCLKSEL, clocking);

	return 0;
}

static int wm8955_sysclk(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	int ret = 0;

	/* Always disable the clocks - if we're doing reconfiguration this
	 * avoids misclocking.
	 */
	snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
			    WM8955_DIGENB, 0);
	snd_soc_update_bits(codec, WM8955_CLOCKING_PLL,
			    WM8955_PLL_RB | WM8955_PLLEN, 0);

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		break;
	case SND_SOC_DAPM_PRE_PMU:
		ret = wm8955_configure_clocking(codec);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int deemph_settings[] = { 0, 32000, 44100, 48000 };

static int wm8955_set_deemph(struct snd_soc_codec *codec)
{
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (wm8955->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(deemph_settings); i++) {
			if (abs(deemph_settings[i] - wm8955->fs) <
			    abs(deemph_settings[best] - wm8955->fs))
				best = i;
		}

		val = best << WM8955_DEEMPH_SHIFT;
	} else {
		val = 0;
	}

	dev_dbg(codec->dev, "Set deemphasis %d\n", val);

	return snd_soc_update_bits(codec, WM8955_DAC_CONTROL,
				   WM8955_DEEMPH_MASK, val);
}

static int wm8955_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wm8955->deemph;
	return 0;
}

static int wm8955_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	unsigned int deemph = ucontrol->value.integer.value[0];

	if (deemph > 1)
		return -EINVAL;

	wm8955->deemph = deemph;

	return wm8955_set_deemph(codec);
}

static const char *bass_mode_text[] = {
	"Linear", "Adaptive",
};

static SOC_ENUM_SINGLE_DECL(bass_mode, WM8955_BASS_CONTROL, 7, bass_mode_text);

static const char *bass_cutoff_text[] = {
	"Low", "High"
};

static SOC_ENUM_SINGLE_DECL(bass_cutoff, WM8955_BASS_CONTROL, 6,
			    bass_cutoff_text);

static const char *treble_cutoff_text[] = {
	"High", "Low"
};

static SOC_ENUM_SINGLE_DECL(treble_cutoff, WM8955_TREBLE_CONTROL, 2,
			    treble_cutoff_text);

static const DECLARE_TLV_DB_SCALE(digital_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(atten_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(mono_tlv, -2100, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(treble_tlv, -1200, 150, 1);

static const struct snd_kcontrol_new wm8955_snd_controls[] = {
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8955_LEFT_DAC_VOLUME,
		 WM8955_RIGHT_DAC_VOLUME, 0, 255, 0, digital_tlv),
SOC_SINGLE_TLV("Playback Attenuation Volume", WM8955_DAC_CONTROL, 7, 1, 1,
	       atten_tlv),
SOC_SINGLE_BOOL_EXT("DAC Deemphasis Switch", 0,
		    wm8955_get_deemph, wm8955_put_deemph),

SOC_ENUM("Bass Mode", bass_mode),
SOC_ENUM("Bass Cutoff", bass_cutoff),
SOC_SINGLE("Bass Volume", WM8955_BASS_CONTROL, 0, 15, 1),

SOC_ENUM("Treble Cutoff", treble_cutoff),
SOC_SINGLE_TLV("Treble Volume", WM8955_TREBLE_CONTROL, 0, 14, 1, treble_tlv),

SOC_SINGLE_TLV("Left Bypass Volume", WM8955_LEFT_OUT_MIX_1, 4, 7, 1,
	       bypass_tlv),
SOC_SINGLE_TLV("Left Mono Volume", WM8955_LEFT_OUT_MIX_2, 4, 7, 1,
	       bypass_tlv),

SOC_SINGLE_TLV("Right Mono Volume", WM8955_RIGHT_OUT_MIX_1, 4, 7, 1,
	       bypass_tlv),
SOC_SINGLE_TLV("Right Bypass Volume", WM8955_RIGHT_OUT_MIX_2, 4, 7, 1,
	       bypass_tlv),

/* Not a stereo pair so they line up with the DAPM switches */
SOC_SINGLE_TLV("Mono Left Bypass Volume", WM8955_MONO_OUT_MIX_1, 4, 7, 1,
	       mono_tlv),
SOC_SINGLE_TLV("Mono Right Bypass Volume", WM8955_MONO_OUT_MIX_2, 4, 7, 1,
	       mono_tlv),

SOC_DOUBLE_R_TLV("Headphone Volume", WM8955_LOUT1_VOLUME,
		 WM8955_ROUT1_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Headphone ZC Switch", WM8955_LOUT1_VOLUME,
	     WM8955_ROUT1_VOLUME, 7, 1, 0),

SOC_DOUBLE_R_TLV("Speaker Volume", WM8955_LOUT2_VOLUME,
		 WM8955_ROUT2_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Speaker ZC Switch", WM8955_LOUT2_VOLUME,
	     WM8955_ROUT2_VOLUME, 7, 1, 0),

SOC_SINGLE_TLV("Mono Volume", WM8955_MONOOUT_VOLUME, 0, 127, 0, out_tlv),
SOC_SINGLE("Mono ZC Switch", WM8955_MONOOUT_VOLUME, 7, 1, 0),
};

static const struct snd_kcontrol_new lmixer[] = {
SOC_DAPM_SINGLE("Playback Switch", WM8955_LEFT_OUT_MIX_1, 8, 1, 0),
SOC_DAPM_SINGLE("Bypass Switch", WM8955_LEFT_OUT_MIX_1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8955_LEFT_OUT_MIX_2, 8, 1, 0),
SOC_DAPM_SINGLE("Mono Switch", WM8955_LEFT_OUT_MIX_2, 7, 1, 0),
};

static const struct snd_kcontrol_new rmixer[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8955_RIGHT_OUT_MIX_1, 8, 1, 0),
SOC_DAPM_SINGLE("Mono Switch", WM8955_RIGHT_OUT_MIX_1, 7, 1, 0),
SOC_DAPM_SINGLE("Playback Switch", WM8955_RIGHT_OUT_MIX_2, 8, 1, 0),
SOC_DAPM_SINGLE("Bypass Switch", WM8955_RIGHT_OUT_MIX_2, 7, 1, 0),
};

static const struct snd_kcontrol_new mmixer[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8955_MONO_OUT_MIX_1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8955_MONO_OUT_MIX_1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8955_MONO_OUT_MIX_2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8955_MONO_OUT_MIX_2, 7, 1, 0),
};

static const struct snd_soc_dapm_widget wm8955_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("MONOIN-"),
SND_SOC_DAPM_INPUT("MONOIN+"),
SND_SOC_DAPM_INPUT("LINEINR"),
SND_SOC_DAPM_INPUT("LINEINL"),

SND_SOC_DAPM_PGA("Mono Input", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("SYSCLK", WM8955_POWER_MANAGEMENT_1, 0, 1, wm8955_sysclk,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("TSDEN", WM8955_ADDITIONAL_CONTROL_1, 8, 0, NULL, 0),

SND_SOC_DAPM_DAC("DACL", "Playback", WM8955_POWER_MANAGEMENT_2, 8, 0),
SND_SOC_DAPM_DAC("DACR", "Playback", WM8955_POWER_MANAGEMENT_2, 7, 0),

SND_SOC_DAPM_PGA("LOUT1 PGA", WM8955_POWER_MANAGEMENT_2, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("ROUT1 PGA", WM8955_POWER_MANAGEMENT_2, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("LOUT2 PGA", WM8955_POWER_MANAGEMENT_2, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("ROUT2 PGA", WM8955_POWER_MANAGEMENT_2, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA("MOUT PGA", WM8955_POWER_MANAGEMENT_2, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("OUT3 PGA", WM8955_POWER_MANAGEMENT_2, 1, 0, NULL, 0),

/* The names are chosen to make the control names nice */
SND_SOC_DAPM_MIXER("Left", SND_SOC_NOPM, 0, 0,
		   lmixer, ARRAY_SIZE(lmixer)),
SND_SOC_DAPM_MIXER("Right", SND_SOC_NOPM, 0, 0,
		   rmixer, ARRAY_SIZE(rmixer)),
SND_SOC_DAPM_MIXER("Mono", SND_SOC_NOPM, 0, 0,
		   mmixer, ARRAY_SIZE(mmixer)),

SND_SOC_DAPM_OUTPUT("LOUT1"),
SND_SOC_DAPM_OUTPUT("ROUT1"),
SND_SOC_DAPM_OUTPUT("LOUT2"),
SND_SOC_DAPM_OUTPUT("ROUT2"),
SND_SOC_DAPM_OUTPUT("MONOOUT"),
SND_SOC_DAPM_OUTPUT("OUT3"),
};

static const struct snd_soc_dapm_route wm8955_dapm_routes[] = {
	{ "DACL", NULL, "SYSCLK" },
	{ "DACR", NULL, "SYSCLK" },

	{ "Mono Input", NULL, "MONOIN-" },
	{ "Mono Input", NULL, "MONOIN+" },

	{ "Left", "Playback Switch", "DACL" },
	{ "Left", "Right Playback Switch", "DACR" },
	{ "Left", "Bypass Switch", "LINEINL" },
	{ "Left", "Mono Switch", "Mono Input" },

	{ "Right", "Playback Switch", "DACR" },
	{ "Right", "Left Playback Switch", "DACL" },
	{ "Right", "Bypass Switch", "LINEINR" },
	{ "Right", "Mono Switch", "Mono Input" },

	{ "Mono", "Left Playback Switch", "DACL" },
	{ "Mono", "Right Playback Switch", "DACR" },
	{ "Mono", "Left Bypass Switch", "LINEINL" },
	{ "Mono", "Right Bypass Switch", "LINEINR" },

	{ "LOUT1 PGA", NULL, "Left" },
	{ "LOUT1", NULL, "TSDEN" },
	{ "LOUT1", NULL, "LOUT1 PGA" },

	{ "ROUT1 PGA", NULL, "Right" },
	{ "ROUT1", NULL, "TSDEN" },
	{ "ROUT1", NULL, "ROUT1 PGA" },

	{ "LOUT2 PGA", NULL, "Left" },
	{ "LOUT2", NULL, "TSDEN" },
	{ "LOUT2", NULL, "LOUT2 PGA" },

	{ "ROUT2 PGA", NULL, "Right" },
	{ "ROUT2", NULL, "TSDEN" },
	{ "ROUT2", NULL, "ROUT2 PGA" },

	{ "MOUT PGA", NULL, "Mono" },
	{ "MONOOUT", NULL, "MOUT PGA" },

	/* OUT3 not currently implemented */
	{ "OUT3", NULL, "OUT3 PGA" },
};

static int wm8955_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	int ret;
	int wl;

	switch (params_width(params)) {
	case 16:
		wl = 0;
		break;
	case 20:
		wl = 0x4;
		break;
	case 24:
		wl = 0x8;
		break;
	case 32:
		wl = 0xc;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, WM8955_AUDIO_INTERFACE,
			    WM8955_WL_MASK, wl);

	wm8955->fs = params_rate(params);
	wm8955_set_deemph(codec);

	/* If the chip is clocked then disable the clocks and force a
	 * reconfiguration, otherwise DAPM will power up the
	 * clocks for us later. */
	ret = snd_soc_read(codec, WM8955_POWER_MANAGEMENT_1);
	if (ret < 0)
		return ret;
	if (ret & WM8955_DIGENB) {
		snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
				    WM8955_DIGENB, 0);
		snd_soc_update_bits(codec, WM8955_CLOCKING_PLL,
				    WM8955_PLL_RB | WM8955_PLLEN, 0);

		wm8955_configure_clocking(codec);
	}

	return 0;
}


static int wm8955_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8955_priv *priv = snd_soc_codec_get_drvdata(codec);
	int div;

	switch (clk_id) {
	case WM8955_CLK_MCLK:
		if (freq > 15000000) {
			priv->mclk_rate = freq /= 2;
			div = WM8955_MCLKDIV2;
		} else {
			priv->mclk_rate = freq;
			div = 0;
		}

		snd_soc_update_bits(codec, WM8955_SAMPLE_RATE,
				    WM8955_MCLKDIV2, div);
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

	return 0;
}

static int wm8955_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 aif = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif |= WM8955_MS;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif |= WM8955_LRP;
	case SND_SOC_DAIFMT_DSP_A:
		aif |= 0x3;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif |= 0x1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif |= WM8955_BCLKINV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif |= WM8955_BCLKINV | WM8955_LRP;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif |= WM8955_BCLKINV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif |= WM8955_LRP;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8955_AUDIO_INTERFACE,
			    WM8955_MS | WM8955_FORMAT_MASK | WM8955_BCLKINV |
			    WM8955_LRP, aif);

	return 0;
}


static int wm8955_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int val;

	if (mute)
		val = WM8955_DACMU;
	else
		val = 0;

	snd_soc_update_bits(codec, WM8955_DAC_CONTROL, WM8955_DACMU, val);

	return 0;
}

static int wm8955_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID resistance 2*50k */
		snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
				    WM8955_VMIDSEL_MASK,
				    0x1 << WM8955_VMIDSEL_SHIFT);

		/* Default bias current */
		snd_soc_update_bits(codec, WM8955_ADDITIONAL_CONTROL_1,
				    WM8955_VSEL_MASK,
				    0x2 << WM8955_VSEL_SHIFT);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8955->supplies),
						    wm8955->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			regcache_sync(wm8955->regmap);

			/* Enable VREF and VMID */
			snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
					    WM8955_VREF |
					    WM8955_VMIDSEL_MASK,
					    WM8955_VREF |
					    0x3 << WM8955_VREF_SHIFT);

			/* Let VMID ramp */
			msleep(500);

			/* High resistance VROI to maintain outputs */
			snd_soc_update_bits(codec,
					    WM8955_ADDITIONAL_CONTROL_3,
					    WM8955_VROI, WM8955_VROI);
		}

		/* Maintain VMID with 2*250k */
		snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
				    WM8955_VMIDSEL_MASK,
				    0x2 << WM8955_VMIDSEL_SHIFT);

		/* Minimum bias current */
		snd_soc_update_bits(codec, WM8955_ADDITIONAL_CONTROL_1,
				    WM8955_VSEL_MASK, 0);
		break;

	case SND_SOC_BIAS_OFF:
		/* Low resistance VROI to help discharge */
		snd_soc_update_bits(codec,
				    WM8955_ADDITIONAL_CONTROL_3,
				    WM8955_VROI, 0);

		/* Turn off VMID and VREF */
		snd_soc_update_bits(codec, WM8955_POWER_MANAGEMENT_1,
				    WM8955_VREF |
				    WM8955_VMIDSEL_MASK, 0);

		regulator_bulk_disable(ARRAY_SIZE(wm8955->supplies),
				       wm8955->supplies);
		break;
	}
	return 0;
}

#define WM8955_RATES SNDRV_PCM_RATE_8000_96000

#define WM8955_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8955_dai_ops = {
	.set_sysclk = wm8955_set_sysclk,
	.set_fmt = wm8955_set_fmt,
	.hw_params = wm8955_hw_params,
	.digital_mute = wm8955_digital_mute,
};

static struct snd_soc_dai_driver wm8955_dai = {
	.name = "wm8955-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8955_RATES,
		.formats = WM8955_FORMATS,
	},
	.ops = &wm8955_dai_ops,
};

static int wm8955_probe(struct snd_soc_codec *codec)
{
	struct wm8955_priv *wm8955 = snd_soc_codec_get_drvdata(codec);
	struct wm8955_pdata *pdata = dev_get_platdata(codec->dev);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(wm8955->supplies); i++)
		wm8955->supplies[i].supply = wm8955_supply_names[i];

	ret = devm_regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8955->supplies),
				 wm8955->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8955->supplies),
				    wm8955->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = wm8955_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		goto err_enable;
	}

	/* Change some default settings - latch VU and enable ZC */
	snd_soc_update_bits(codec, WM8955_LEFT_DAC_VOLUME,
			    WM8955_LDVU, WM8955_LDVU);
	snd_soc_update_bits(codec, WM8955_RIGHT_DAC_VOLUME,
			    WM8955_RDVU, WM8955_RDVU);
	snd_soc_update_bits(codec, WM8955_LOUT1_VOLUME,
			    WM8955_LO1VU | WM8955_LO1ZC,
			    WM8955_LO1VU | WM8955_LO1ZC);
	snd_soc_update_bits(codec, WM8955_ROUT1_VOLUME,
			    WM8955_RO1VU | WM8955_RO1ZC,
			    WM8955_RO1VU | WM8955_RO1ZC);
	snd_soc_update_bits(codec, WM8955_LOUT2_VOLUME,
			    WM8955_LO2VU | WM8955_LO2ZC,
			    WM8955_LO2VU | WM8955_LO2ZC);
	snd_soc_update_bits(codec, WM8955_ROUT2_VOLUME,
			    WM8955_RO2VU | WM8955_RO2ZC,
			    WM8955_RO2VU | WM8955_RO2ZC);
	snd_soc_update_bits(codec, WM8955_MONOOUT_VOLUME,
			    WM8955_MOZC, WM8955_MOZC);

	/* Also enable adaptive bass boost by default */
	snd_soc_update_bits(codec, WM8955_BASS_CONTROL, WM8955_BB, WM8955_BB);

	/* Set platform data values */
	if (pdata) {
		if (pdata->out2_speaker)
			snd_soc_update_bits(codec, WM8955_ADDITIONAL_CONTROL_2,
					    WM8955_ROUT2INV, WM8955_ROUT2INV);

		if (pdata->monoin_diff)
			snd_soc_update_bits(codec, WM8955_MONO_OUT_MIX_1,
					    WM8955_DMEN, WM8955_DMEN);
	}

	snd_soc_codec_force_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(wm8955->supplies), wm8955->supplies);

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8955->supplies), wm8955->supplies);
	return ret;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8955 = {
	.probe =	wm8955_probe,
	.set_bias_level = wm8955_set_bias_level,
	.suspend_bias_off = true,

	.controls =	wm8955_snd_controls,
	.num_controls = ARRAY_SIZE(wm8955_snd_controls),
	.dapm_widgets = wm8955_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8955_dapm_widgets),
	.dapm_routes =	wm8955_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8955_dapm_routes),
};

static const struct regmap_config wm8955_regmap = {
	.reg_bits = 7,
	.val_bits = 9,

	.max_register = WM8955_MAX_REGISTER,
	.volatile_reg = wm8955_volatile,
	.writeable_reg = wm8955_writeable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8955_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8955_reg_defaults),
};

static int wm8955_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8955_priv *wm8955;
	int ret;

	wm8955 = devm_kzalloc(&i2c->dev, sizeof(struct wm8955_priv),
			      GFP_KERNEL);
	if (wm8955 == NULL)
		return -ENOMEM;

	wm8955->regmap = devm_regmap_init_i2c(i2c, &wm8955_regmap);
	if (IS_ERR(wm8955->regmap)) {
		ret = PTR_ERR(wm8955->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8955);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8955, &wm8955_dai, 1);

	return ret;
}

static int wm8955_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static const struct i2c_device_id wm8955_i2c_id[] = {
	{ "wm8955", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8955_i2c_id);

static struct i2c_driver wm8955_i2c_driver = {
	.driver = {
		.name = "wm8955",
	},
	.probe =    wm8955_i2c_probe,
	.remove =   wm8955_i2c_remove,
	.id_table = wm8955_i2c_id,
};

module_i2c_driver(wm8955_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8955 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
