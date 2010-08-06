/*
 * wm9081.c  --  WM9081 ALSA SoC Audio driver
 *
 * Author: Mark Brown
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <sound/wm9081.h>
#include "wm9081.h"

static u16 wm9081_reg_defaults[] = {
	0x0000,     /* R0  - Software Reset */
	0x0000,     /* R1 */
	0x00B9,     /* R2  - Analogue Lineout */
	0x00B9,     /* R3  - Analogue Speaker PGA */
	0x0001,     /* R4  - VMID Control */
	0x0068,     /* R5  - Bias Control 1 */
	0x0000,     /* R6 */
	0x0000,     /* R7  - Analogue Mixer */
	0x0000,     /* R8  - Anti Pop Control */
	0x01DB,     /* R9  - Analogue Speaker 1 */
	0x0018,     /* R10 - Analogue Speaker 2 */
	0x0180,     /* R11 - Power Management */
	0x0000,     /* R12 - Clock Control 1 */
	0x0038,     /* R13 - Clock Control 2 */
	0x4000,     /* R14 - Clock Control 3 */
	0x0000,     /* R15 */
	0x0000,     /* R16 - FLL Control 1 */
	0x0200,     /* R17 - FLL Control 2 */
	0x0000,     /* R18 - FLL Control 3 */
	0x0204,     /* R19 - FLL Control 4 */
	0x0000,     /* R20 - FLL Control 5 */
	0x0000,     /* R21 */
	0x0000,     /* R22 - Audio Interface 1 */
	0x0002,     /* R23 - Audio Interface 2 */
	0x0008,     /* R24 - Audio Interface 3 */
	0x0022,     /* R25 - Audio Interface 4 */
	0x0000,     /* R26 - Interrupt Status */
	0x0006,     /* R27 - Interrupt Status Mask */
	0x0000,     /* R28 - Interrupt Polarity */
	0x0000,     /* R29 - Interrupt Control */
	0x00C0,     /* R30 - DAC Digital 1 */
	0x0008,     /* R31 - DAC Digital 2 */
	0x09AF,     /* R32 - DRC 1 */
	0x4201,     /* R33 - DRC 2 */
	0x0000,     /* R34 - DRC 3 */
	0x0000,     /* R35 - DRC 4 */
	0x0000,     /* R36 */
	0x0000,     /* R37 */
	0x0000,     /* R38 - Write Sequencer 1 */
	0x0000,     /* R39 - Write Sequencer 2 */
	0x0002,     /* R40 - MW Slave 1 */
	0x0000,     /* R41 */
	0x0000,     /* R42 - EQ 1 */
	0x0000,     /* R43 - EQ 2 */
	0x0FCA,     /* R44 - EQ 3 */
	0x0400,     /* R45 - EQ 4 */
	0x00B8,     /* R46 - EQ 5 */
	0x1EB5,     /* R47 - EQ 6 */
	0xF145,     /* R48 - EQ 7 */
	0x0B75,     /* R49 - EQ 8 */
	0x01C5,     /* R50 - EQ 9 */
	0x169E,     /* R51 - EQ 10 */
	0xF829,     /* R52 - EQ 11 */
	0x07AD,     /* R53 - EQ 12 */
	0x1103,     /* R54 - EQ 13 */
	0x1C58,     /* R55 - EQ 14 */
	0xF373,     /* R56 - EQ 15 */
	0x0A54,     /* R57 - EQ 16 */
	0x0558,     /* R58 - EQ 17 */
	0x0564,     /* R59 - EQ 18 */
	0x0559,     /* R60 - EQ 19 */
	0x4000,     /* R61 - EQ 20 */
};

static struct {
	int ratio;
	int clk_sys_rate;
} clk_sys_rates[] = {
	{ 64,   0 },
	{ 128,  1 },
	{ 192,  2 },
	{ 256,  3 },
	{ 384,  4 },
	{ 512,  5 },
	{ 768,  6 },
	{ 1024, 7 },
	{ 1408, 8 },
	{ 1536, 9 },
};

static struct {
	int rate;
	int sample_rate;
} sample_rates[] = {
	{ 8000,  0  },
	{ 11025, 1  },
	{ 12000, 2  },
	{ 16000, 3  },
	{ 22050, 4  },
	{ 24000, 5  },
	{ 32000, 6  },
	{ 44100, 7  },
	{ 48000, 8  },
	{ 88200, 9  },
	{ 96000, 10 },
};

static struct {
	int div; /* *10 due to .5s */
	int bclk_div;
} bclk_divs[] = {
	{ 10,  0  },
	{ 15,  1  },
	{ 20,  2  },
	{ 30,  3  },
	{ 40,  4  },
	{ 50,  5  },
	{ 55,  6  },
	{ 60,  7  },
	{ 80,  8  },
	{ 100, 9  },
	{ 110, 10 },
	{ 120, 11 },
	{ 160, 12 },
	{ 200, 13 },
	{ 220, 14 },
	{ 240, 15 },
	{ 250, 16 },
	{ 300, 17 },
	{ 320, 18 },
	{ 440, 19 },
	{ 480, 20 },
};

struct wm9081_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[WM9081_MAX_REGISTER + 1];
	int sysclk_source;
	int mclk_rate;
	int sysclk_rate;
	int fs;
	int bclk;
	int master;
	int fll_fref;
	int fll_fout;
	int tdm_width;
	struct wm9081_retune_mobile_config *retune;
};

static int wm9081_volatile_register(unsigned int reg)
{
	switch (reg) {
	case WM9081_SOFTWARE_RESET:
		return 1;
	default:
		return 0;
	}
}

static int wm9081_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM9081_SOFTWARE_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(drc_in_tlv, -4500, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_out_tlv, -2250, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_min_tlv, -1800, 600, 0);
static unsigned int drc_max_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(1200, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(1800, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(3600, 0, 0),
};
static const DECLARE_TLV_DB_SCALE(drc_qr_tlv, 1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_startup_tlv, -300, 50, 0);

static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);

static const DECLARE_TLV_DB_SCALE(in_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);

static const char *drc_high_text[] = {
	"1",
	"1/2",
	"1/4",
	"1/8",
	"1/16",
	"0",
};

static const struct soc_enum drc_high =
	SOC_ENUM_SINGLE(WM9081_DRC_3, 3, 6, drc_high_text);

static const char *drc_low_text[] = {
	"1",
	"1/2",
	"1/4",
	"1/8",
	"0",
};

static const struct soc_enum drc_low =
	SOC_ENUM_SINGLE(WM9081_DRC_3, 0, 5, drc_low_text);

static const char *drc_atk_text[] = {
	"181us",
	"181us",
	"363us",
	"726us",
	"1.45ms",
	"2.9ms",
	"5.8ms",
	"11.6ms",
	"23.2ms",
	"46.4ms",
	"92.8ms",
	"185.6ms",
};

static const struct soc_enum drc_atk =
	SOC_ENUM_SINGLE(WM9081_DRC_2, 12, 12, drc_atk_text);

static const char *drc_dcy_text[] = {
	"186ms",
	"372ms",
	"743ms",
	"1.49s",
	"2.97s",
	"5.94s",
	"11.89s",
	"23.78s",
	"47.56s",
};

static const struct soc_enum drc_dcy =
	SOC_ENUM_SINGLE(WM9081_DRC_2, 8, 9, drc_dcy_text);

static const char *drc_qr_dcy_text[] = {
	"0.725ms",
	"1.45ms",
	"5.8ms",
};

static const struct soc_enum drc_qr_dcy =
	SOC_ENUM_SINGLE(WM9081_DRC_2, 4, 3, drc_qr_dcy_text);

static const char *dac_deemph_text[] = {
	"None",
	"32kHz",
	"44.1kHz",
	"48kHz",
};

static const struct soc_enum dac_deemph =
	SOC_ENUM_SINGLE(WM9081_DAC_DIGITAL_2, 1, 4, dac_deemph_text);

static const char *speaker_mode_text[] = {
	"Class D",
	"Class AB",
};

static const struct soc_enum speaker_mode =
	SOC_ENUM_SINGLE(WM9081_ANALOGUE_SPEAKER_2, 6, 2, speaker_mode_text);

static int speaker_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg;

	reg = snd_soc_read(codec, WM9081_ANALOGUE_SPEAKER_2);
	if (reg & WM9081_SPK_MODE)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

/*
 * Stop any attempts to change speaker mode while the speaker is enabled.
 *
 * We also have some special anti-pop controls dependant on speaker
 * mode which must be changed along with the mode.
 */
static int speaker_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg_pwr = snd_soc_read(codec, WM9081_POWER_MANAGEMENT);
	unsigned int reg2 = snd_soc_read(codec, WM9081_ANALOGUE_SPEAKER_2);

	/* Are we changing anything? */
	if (ucontrol->value.integer.value[0] ==
	    ((reg2 & WM9081_SPK_MODE) != 0))
		return 0;

	/* Don't try to change modes while enabled */
	if (reg_pwr & WM9081_SPK_ENA)
		return -EINVAL;

	if (ucontrol->value.integer.value[0]) {
		/* Class AB */
		reg2 &= ~(WM9081_SPK_INV_MUTE | WM9081_OUT_SPK_CTRL);
		reg2 |= WM9081_SPK_MODE;
	} else {
		/* Class D */
		reg2 |= WM9081_SPK_INV_MUTE | WM9081_OUT_SPK_CTRL;
		reg2 &= ~WM9081_SPK_MODE;
	}

	snd_soc_write(codec, WM9081_ANALOGUE_SPEAKER_2, reg2);

	return 0;
}

static const struct snd_kcontrol_new wm9081_snd_controls[] = {
SOC_SINGLE_TLV("IN1 Volume", WM9081_ANALOGUE_MIXER, 1, 1, 1, in_tlv),
SOC_SINGLE_TLV("IN2 Volume", WM9081_ANALOGUE_MIXER, 3, 1, 1, in_tlv),

SOC_SINGLE_TLV("Playback Volume", WM9081_DAC_DIGITAL_1, 1, 96, 0, dac_tlv),

SOC_SINGLE("LINEOUT Switch", WM9081_ANALOGUE_LINEOUT, 7, 1, 1),
SOC_SINGLE("LINEOUT ZC Switch", WM9081_ANALOGUE_LINEOUT, 6, 1, 0),
SOC_SINGLE_TLV("LINEOUT Volume", WM9081_ANALOGUE_LINEOUT, 0, 63, 0, out_tlv),

SOC_SINGLE("DRC Switch", WM9081_DRC_1, 15, 1, 0),
SOC_ENUM("DRC High Slope", drc_high),
SOC_ENUM("DRC Low Slope", drc_low),
SOC_SINGLE_TLV("DRC Input Volume", WM9081_DRC_4, 5, 60, 1, drc_in_tlv),
SOC_SINGLE_TLV("DRC Output Volume", WM9081_DRC_4, 0, 30, 1, drc_out_tlv),
SOC_SINGLE_TLV("DRC Minimum Volume", WM9081_DRC_2, 2, 3, 1, drc_min_tlv),
SOC_SINGLE_TLV("DRC Maximum Volume", WM9081_DRC_2, 0, 3, 0, drc_max_tlv),
SOC_ENUM("DRC Attack", drc_atk),
SOC_ENUM("DRC Decay", drc_dcy),
SOC_SINGLE("DRC Quick Release Switch", WM9081_DRC_1, 2, 1, 0),
SOC_SINGLE_TLV("DRC Quick Release Volume", WM9081_DRC_2, 6, 3, 0, drc_qr_tlv),
SOC_ENUM("DRC Quick Release Decay", drc_qr_dcy),
SOC_SINGLE_TLV("DRC Startup Volume", WM9081_DRC_1, 6, 18, 0, drc_startup_tlv),

SOC_SINGLE("EQ Switch", WM9081_EQ_1, 0, 1, 0),

SOC_SINGLE("Speaker DC Volume", WM9081_ANALOGUE_SPEAKER_1, 3, 5, 0),
SOC_SINGLE("Speaker AC Volume", WM9081_ANALOGUE_SPEAKER_1, 0, 5, 0),
SOC_SINGLE("Speaker Switch", WM9081_ANALOGUE_SPEAKER_PGA, 7, 1, 1),
SOC_SINGLE("Speaker ZC Switch", WM9081_ANALOGUE_SPEAKER_PGA, 6, 1, 0),
SOC_SINGLE_TLV("Speaker Volume", WM9081_ANALOGUE_SPEAKER_PGA, 0, 63, 0,
	       out_tlv),
SOC_ENUM("DAC Deemphasis", dac_deemph),
SOC_ENUM_EXT("Speaker Mode", speaker_mode, speaker_mode_get, speaker_mode_put),
};

static const struct snd_kcontrol_new wm9081_eq_controls[] = {
SOC_SINGLE_TLV("EQ1 Volume", WM9081_EQ_1, 11, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Volume", WM9081_EQ_1, 6, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Volume", WM9081_EQ_1, 1, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Volume", WM9081_EQ_2, 11, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ5 Volume", WM9081_EQ_2, 6, 24, 0, eq_tlv),
};

static const struct snd_kcontrol_new mixer[] = {
SOC_DAPM_SINGLE("IN1 Switch", WM9081_ANALOGUE_MIXER, 0, 1, 0),
SOC_DAPM_SINGLE("IN2 Switch", WM9081_ANALOGUE_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("Playback Switch", WM9081_ANALOGUE_MIXER, 4, 1, 0),
};

static int speaker_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int reg = snd_soc_read(codec, WM9081_POWER_MANAGEMENT);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		reg |= WM9081_SPK_ENA;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		reg &= ~WM9081_SPK_ENA;
		break;
	}

	snd_soc_write(codec, WM9081_POWER_MANAGEMENT, reg);

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_clk_ref_div;
	u16 n;
	u16 k;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;
	unsigned int div;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	while ((Fref / div) > 13500000) {
		div *= 2;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}
	fll_div->fll_clk_ref_div = div / 2;

	pr_debug("Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 0;
	target = Fout * 2;
	while (target < 90000000) {
		div++;
		target *= 2;
		if (div > 7) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	fll_div->fll_outdiv = div;

	pr_debug("Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			target /= fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	/* Now, calculate N.K */
	Ndiv = target / Fref;

	fll_div->n = Ndiv;
	Nmod = target % Fref;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll_div->k = K / 10;

	pr_debug("N=%x K=%x FLL_FRATIO=%x FLL_OUTDIV=%x FLL_CLK_REF_DIV=%x\n",
		 fll_div->n, fll_div->k,
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_clk_ref_div);

	return 0;
}

static int wm9081_set_fll(struct snd_soc_codec *codec, int fll_id,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);
	u16 reg1, reg4, reg5;
	struct _fll_div fll_div;
	int ret;
	int clk_sys_reg;

	/* Any change? */
	if (Fref == wm9081->fll_fref && Fout == wm9081->fll_fout)
		return 0;

	/* Disable the FLL */
	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");
		wm9081->fll_fref = 0;
		wm9081->fll_fout = 0;

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	reg5 = snd_soc_read(codec, WM9081_FLL_CONTROL_5);
	reg5 &= ~WM9081_FLL_CLK_SRC_MASK;

	switch (fll_id) {
	case WM9081_SYSCLK_FLL_MCLK:
		reg5 |= 0x1;
		break;

	default:
		dev_err(codec->dev, "Unknown FLL ID %d\n", fll_id);
		return -EINVAL;
	}

	/* Disable CLK_SYS while we reconfigure */
	clk_sys_reg = snd_soc_read(codec, WM9081_CLOCK_CONTROL_3);
	if (clk_sys_reg & WM9081_CLK_SYS_ENA)
		snd_soc_write(codec, WM9081_CLOCK_CONTROL_3,
			     clk_sys_reg & ~WM9081_CLK_SYS_ENA);

	/* Any FLL configuration change requires that the FLL be
	 * disabled first. */
	reg1 = snd_soc_read(codec, WM9081_FLL_CONTROL_1);
	reg1 &= ~WM9081_FLL_ENA;
	snd_soc_write(codec, WM9081_FLL_CONTROL_1, reg1);

	/* Apply the configuration */
	if (fll_div.k)
		reg1 |= WM9081_FLL_FRAC_MASK;
	else
		reg1 &= ~WM9081_FLL_FRAC_MASK;
	snd_soc_write(codec, WM9081_FLL_CONTROL_1, reg1);

	snd_soc_write(codec, WM9081_FLL_CONTROL_2,
		     (fll_div.fll_outdiv << WM9081_FLL_OUTDIV_SHIFT) |
		     (fll_div.fll_fratio << WM9081_FLL_FRATIO_SHIFT));
	snd_soc_write(codec, WM9081_FLL_CONTROL_3, fll_div.k);

	reg4 = snd_soc_read(codec, WM9081_FLL_CONTROL_4);
	reg4 &= ~WM9081_FLL_N_MASK;
	reg4 |= fll_div.n << WM9081_FLL_N_SHIFT;
	snd_soc_write(codec, WM9081_FLL_CONTROL_4, reg4);

	reg5 &= ~WM9081_FLL_CLK_REF_DIV_MASK;
	reg5 |= fll_div.fll_clk_ref_div << WM9081_FLL_CLK_REF_DIV_SHIFT;
	snd_soc_write(codec, WM9081_FLL_CONTROL_5, reg5);

	/* Enable the FLL */
	snd_soc_write(codec, WM9081_FLL_CONTROL_1, reg1 | WM9081_FLL_ENA);

	/* Then bring CLK_SYS up again if it was disabled */
	if (clk_sys_reg & WM9081_CLK_SYS_ENA)
		snd_soc_write(codec, WM9081_CLOCK_CONTROL_3, clk_sys_reg);

	dev_dbg(codec->dev, "FLL enabled at %dHz->%dHz\n", Fref, Fout);

	wm9081->fll_fref = Fref;
	wm9081->fll_fout = Fout;

	return 0;
}

static int configure_clock(struct snd_soc_codec *codec)
{
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);
	int new_sysclk, i, target;
	unsigned int reg;
	int ret = 0;
	int mclkdiv = 0;
	int fll = 0;

	switch (wm9081->sysclk_source) {
	case WM9081_SYSCLK_MCLK:
		if (wm9081->mclk_rate > 12225000) {
			mclkdiv = 1;
			wm9081->sysclk_rate = wm9081->mclk_rate / 2;
		} else {
			wm9081->sysclk_rate = wm9081->mclk_rate;
		}
		wm9081_set_fll(codec, WM9081_SYSCLK_FLL_MCLK, 0, 0);
		break;

	case WM9081_SYSCLK_FLL_MCLK:
		/* If we have a sample rate calculate a CLK_SYS that
		 * gives us a suitable DAC configuration, plus BCLK.
		 * Ideally we would check to see if we can clock
		 * directly from MCLK and only use the FLL if this is
		 * not the case, though care must be taken with free
		 * running mode.
		 */
		if (wm9081->master && wm9081->bclk) {
			/* Make sure we can generate CLK_SYS and BCLK
			 * and that we've got 3MHz for optimal
			 * performance. */
			for (i = 0; i < ARRAY_SIZE(clk_sys_rates); i++) {
				target = wm9081->fs * clk_sys_rates[i].ratio;
				new_sysclk = target;
				if (target >= wm9081->bclk &&
				    target > 3000000)
					break;
			}

			if (i == ARRAY_SIZE(clk_sys_rates))
				return -EINVAL;

		} else if (wm9081->fs) {
			for (i = 0; i < ARRAY_SIZE(clk_sys_rates); i++) {
				new_sysclk = clk_sys_rates[i].ratio
					* wm9081->fs;
				if (new_sysclk > 3000000)
					break;
			}

			if (i == ARRAY_SIZE(clk_sys_rates))
				return -EINVAL;

		} else {
			new_sysclk = 12288000;
		}

		ret = wm9081_set_fll(codec, WM9081_SYSCLK_FLL_MCLK,
				     wm9081->mclk_rate, new_sysclk);
		if (ret == 0) {
			wm9081->sysclk_rate = new_sysclk;

			/* Switch SYSCLK over to FLL */
			fll = 1;
		} else {
			wm9081->sysclk_rate = wm9081->mclk_rate;
		}
		break;

	default:
		return -EINVAL;
	}

	reg = snd_soc_read(codec, WM9081_CLOCK_CONTROL_1);
	if (mclkdiv)
		reg |= WM9081_MCLKDIV2;
	else
		reg &= ~WM9081_MCLKDIV2;
	snd_soc_write(codec, WM9081_CLOCK_CONTROL_1, reg);

	reg = snd_soc_read(codec, WM9081_CLOCK_CONTROL_3);
	if (fll)
		reg |= WM9081_CLK_SRC_SEL;
	else
		reg &= ~WM9081_CLK_SRC_SEL;
	snd_soc_write(codec, WM9081_CLOCK_CONTROL_3, reg);

	dev_dbg(codec->dev, "CLK_SYS is %dHz\n", wm9081->sysclk_rate);

	return ret;
}

static int clk_sys_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);

	/* This should be done on init() for bypass paths */
	switch (wm9081->sysclk_source) {
	case WM9081_SYSCLK_MCLK:
		dev_dbg(codec->dev, "Using %dHz MCLK\n", wm9081->mclk_rate);
		break;
	case WM9081_SYSCLK_FLL_MCLK:
		dev_dbg(codec->dev, "Using %dHz MCLK with FLL\n",
			wm9081->mclk_rate);
		break;
	default:
		dev_err(codec->dev, "System clock not configured\n");
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		configure_clock(codec);
		break;

	case SND_SOC_DAPM_POST_PMD:
		/* Disable the FLL if it's running */
		wm9081_set_fll(codec, 0, 0, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget wm9081_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1"),
SND_SOC_DAPM_INPUT("IN2"),

SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM9081_POWER_MANAGEMENT, 0, 0),

SND_SOC_DAPM_MIXER_NAMED_CTL("Mixer", SND_SOC_NOPM, 0, 0,
			     mixer, ARRAY_SIZE(mixer)),

SND_SOC_DAPM_PGA("LINEOUT PGA", WM9081_POWER_MANAGEMENT, 4, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("Speaker PGA", WM9081_POWER_MANAGEMENT, 2, 0, NULL, 0,
		   speaker_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_OUTPUT("LINEOUT"),
SND_SOC_DAPM_OUTPUT("SPKN"),
SND_SOC_DAPM_OUTPUT("SPKP"),

SND_SOC_DAPM_SUPPLY("CLK_SYS", WM9081_CLOCK_CONTROL_3, 0, 0, clk_sys_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM9081_CLOCK_CONTROL_3, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("TOCLK", WM9081_CLOCK_CONTROL_3, 2, 0, NULL, 0),
};


static const struct snd_soc_dapm_route audio_paths[] = {
	{ "DAC", NULL, "CLK_SYS" },
	{ "DAC", NULL, "CLK_DSP" },

	{ "Mixer", "IN1 Switch", "IN1" },
	{ "Mixer", "IN2 Switch", "IN2" },
	{ "Mixer", "Playback Switch", "DAC" },

	{ "LINEOUT PGA", NULL, "Mixer" },
	{ "LINEOUT PGA", NULL, "TOCLK" },
	{ "LINEOUT PGA", NULL, "CLK_SYS" },

	{ "LINEOUT", NULL, "LINEOUT PGA" },

	{ "Speaker PGA", NULL, "Mixer" },
	{ "Speaker PGA", NULL, "TOCLK" },
	{ "Speaker PGA", NULL, "CLK_SYS" },

	{ "SPKN", NULL, "Speaker PGA" },
	{ "SPKP", NULL, "Speaker PGA" },
};

static int wm9081_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID=2*40k */
		reg = snd_soc_read(codec, WM9081_VMID_CONTROL);
		reg &= ~WM9081_VMID_SEL_MASK;
		reg |= 0x2;
		snd_soc_write(codec, WM9081_VMID_CONTROL, reg);

		/* Normal bias current */
		reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
		reg &= ~WM9081_STBY_BIAS_ENA;
		snd_soc_write(codec, WM9081_BIAS_CONTROL_1, reg);
		break;

	case SND_SOC_BIAS_STANDBY:
		/* Initial cold start */
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* Disable LINEOUT discharge */
			reg = snd_soc_read(codec, WM9081_ANTI_POP_CONTROL);
			reg &= ~WM9081_LINEOUT_DISCH;
			snd_soc_write(codec, WM9081_ANTI_POP_CONTROL, reg);

			/* Select startup bias source */
			reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
			reg |= WM9081_BIAS_SRC | WM9081_BIAS_ENA;
			snd_soc_write(codec, WM9081_BIAS_CONTROL_1, reg);

			/* VMID 2*4k; Soft VMID ramp enable */
			reg = snd_soc_read(codec, WM9081_VMID_CONTROL);
			reg |= WM9081_VMID_RAMP | 0x6;
			snd_soc_write(codec, WM9081_VMID_CONTROL, reg);

			mdelay(100);

			/* Normal bias enable & soft start off */
			reg |= WM9081_BIAS_ENA;
			reg &= ~WM9081_VMID_RAMP;
			snd_soc_write(codec, WM9081_VMID_CONTROL, reg);

			/* Standard bias source */
			reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
			reg &= ~WM9081_BIAS_SRC;
			snd_soc_write(codec, WM9081_BIAS_CONTROL_1, reg);
		}

		/* VMID 2*240k */
		reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
		reg &= ~WM9081_VMID_SEL_MASK;
		reg |= 0x40;
		snd_soc_write(codec, WM9081_VMID_CONTROL, reg);

		/* Standby bias current on */
		reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
		reg |= WM9081_STBY_BIAS_ENA;
		snd_soc_write(codec, WM9081_BIAS_CONTROL_1, reg);
		break;

	case SND_SOC_BIAS_OFF:
		/* Startup bias source */
		reg = snd_soc_read(codec, WM9081_BIAS_CONTROL_1);
		reg |= WM9081_BIAS_SRC;
		snd_soc_write(codec, WM9081_BIAS_CONTROL_1, reg);

		/* Disable VMID and biases with soft ramping */
		reg = snd_soc_read(codec, WM9081_VMID_CONTROL);
		reg &= ~(WM9081_VMID_SEL_MASK | WM9081_BIAS_ENA);
		reg |= WM9081_VMID_RAMP;
		snd_soc_write(codec, WM9081_VMID_CONTROL, reg);

		/* Actively discharge LINEOUT */
		reg = snd_soc_read(codec, WM9081_ANTI_POP_CONTROL);
		reg |= WM9081_LINEOUT_DISCH;
		snd_soc_write(codec, WM9081_ANTI_POP_CONTROL, reg);
		break;
	}

	codec->bias_level = level;

	return 0;
}

static int wm9081_set_dai_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);
	unsigned int aif2 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_2);

	aif2 &= ~(WM9081_AIF_BCLK_INV | WM9081_AIF_LRCLK_INV |
		  WM9081_BCLK_DIR | WM9081_LRCLK_DIR | WM9081_AIF_FMT_MASK);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		wm9081->master = 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif2 |= WM9081_LRCLK_DIR;
		wm9081->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif2 |= WM9081_BCLK_DIR;
		wm9081->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif2 |= WM9081_LRCLK_DIR | WM9081_BCLK_DIR;
		wm9081->master = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif2 |= WM9081_AIF_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_A:
		aif2 |= 0x3;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif2 |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif2 |= 0x1;
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
			aif2 |= WM9081_AIF_BCLK_INV;
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
			aif2 |= WM9081_AIF_BCLK_INV | WM9081_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif2 |= WM9081_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif2 |= WM9081_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM9081_AUDIO_INTERFACE_2, aif2);

	return 0;
}

static int wm9081_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);
	int ret, i, best, best_val, cur_val;
	unsigned int clk_ctrl2, aif1, aif2, aif3, aif4;

	clk_ctrl2 = snd_soc_read(codec, WM9081_CLOCK_CONTROL_2);
	clk_ctrl2 &= ~(WM9081_CLK_SYS_RATE_MASK | WM9081_SAMPLE_RATE_MASK);

	aif1 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_1);

	aif2 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_2);
	aif2 &= ~WM9081_AIF_WL_MASK;

	aif3 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_3);
	aif3 &= ~WM9081_BCLK_DIV_MASK;

	aif4 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_4);
	aif4 &= ~WM9081_LRCLK_RATE_MASK;

	wm9081->fs = params_rate(params);

	if (wm9081->tdm_width) {
		/* If TDM is set up then that fixes our BCLK. */
		int slots = ((aif1 & WM9081_AIFDAC_TDM_MODE_MASK) >>
			     WM9081_AIFDAC_TDM_MODE_SHIFT) + 1;

		wm9081->bclk = wm9081->fs * wm9081->tdm_width * slots;
	} else {
		/* Otherwise work out a BCLK from the sample size */
		wm9081->bclk = 2 * wm9081->fs;

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			wm9081->bclk *= 16;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			wm9081->bclk *= 20;
			aif2 |= 0x4;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			wm9081->bclk *= 24;
			aif2 |= 0x8;
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			wm9081->bclk *= 32;
			aif2 |= 0xc;
			break;
		default:
			return -EINVAL;
		}
	}

	dev_dbg(codec->dev, "Target BCLK is %dHz\n", wm9081->bclk);

	ret = configure_clock(codec);
	if (ret != 0)
		return ret;

	/* Select nearest CLK_SYS_RATE */
	best = 0;
	best_val = abs((wm9081->sysclk_rate / clk_sys_rates[0].ratio)
		       - wm9081->fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_rates); i++) {
		cur_val = abs((wm9081->sysclk_rate /
			       clk_sys_rates[i].ratio) - wm9081->fs);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected CLK_SYS_RATIO of %d\n",
		clk_sys_rates[best].ratio);
	clk_ctrl2 |= (clk_sys_rates[best].clk_sys_rate
		      << WM9081_CLK_SYS_RATE_SHIFT);

	/* SAMPLE_RATE */
	best = 0;
	best_val = abs(wm9081->fs - sample_rates[0].rate);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		/* Closest match */
		cur_val = abs(wm9081->fs - sample_rates[i].rate);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected SAMPLE_RATE of %dHz\n",
		sample_rates[best].rate);
	clk_ctrl2 |= (sample_rates[best].sample_rate
			<< WM9081_SAMPLE_RATE_SHIFT);

	/* BCLK_DIV */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = ((wm9081->sysclk_rate * 10) / bclk_divs[i].div)
			- wm9081->bclk;
		if (cur_val < 0) /* Table is sorted */
			break;
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	wm9081->bclk = (wm9081->sysclk_rate * 10) / bclk_divs[best].div;
	dev_dbg(codec->dev, "Selected BCLK_DIV of %d for %dHz BCLK\n",
		bclk_divs[best].div, wm9081->bclk);
	aif3 |= bclk_divs[best].bclk_div;

	/* LRCLK is a simple fraction of BCLK */
	dev_dbg(codec->dev, "LRCLK_RATE is %d\n", wm9081->bclk / wm9081->fs);
	aif4 |= wm9081->bclk / wm9081->fs;

	/* Apply a ReTune Mobile configuration if it's in use */
	if (wm9081->retune) {
		struct wm9081_retune_mobile_config *retune = wm9081->retune;
		struct wm9081_retune_mobile_setting *s;
		int eq1;

		best = 0;
		best_val = abs(retune->configs[0].rate - wm9081->fs);
		for (i = 0; i < retune->num_configs; i++) {
			cur_val = abs(retune->configs[i].rate - wm9081->fs);
			if (cur_val < best_val) {
				best_val = cur_val;
				best = i;
			}
		}
		s = &retune->configs[best];

		dev_dbg(codec->dev, "ReTune Mobile %s tuned for %dHz\n",
			s->name, s->rate);

		/* If the EQ is enabled then disable it while we write out */
		eq1 = snd_soc_read(codec, WM9081_EQ_1) & WM9081_EQ_ENA;
		if (eq1 & WM9081_EQ_ENA)
			snd_soc_write(codec, WM9081_EQ_1, 0);

		/* Write out the other values */
		for (i = 1; i < ARRAY_SIZE(s->config); i++)
			snd_soc_write(codec, WM9081_EQ_1 + i, s->config[i]);

		eq1 |= (s->config[0] & ~WM9081_EQ_ENA);
		snd_soc_write(codec, WM9081_EQ_1, eq1);
	}

	snd_soc_write(codec, WM9081_CLOCK_CONTROL_2, clk_ctrl2);
	snd_soc_write(codec, WM9081_AUDIO_INTERFACE_2, aif2);
	snd_soc_write(codec, WM9081_AUDIO_INTERFACE_3, aif3);
	snd_soc_write(codec, WM9081_AUDIO_INTERFACE_4, aif4);

	return 0;
}

static int wm9081_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int reg;

	reg = snd_soc_read(codec, WM9081_DAC_DIGITAL_2);

	if (mute)
		reg |= WM9081_DAC_MUTE;
	else
		reg &= ~WM9081_DAC_MUTE;

	snd_soc_write(codec, WM9081_DAC_DIGITAL_2, reg);

	return 0;
}

static int wm9081_set_sysclk(struct snd_soc_dai *codec_dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case WM9081_SYSCLK_MCLK:
	case WM9081_SYSCLK_FLL_MCLK:
		wm9081->sysclk_source = clk_id;
		wm9081->mclk_rate = freq;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm9081_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm9081_priv *wm9081 = snd_soc_codec_get_drvdata(codec);
	unsigned int aif1 = snd_soc_read(codec, WM9081_AUDIO_INTERFACE_1);

	aif1 &= ~(WM9081_AIFDAC_TDM_SLOT_MASK | WM9081_AIFDAC_TDM_MODE_MASK);

	if (slots < 0 || slots > 4)
		return -EINVAL;

	wm9081->tdm_width = slot_width;

	if (slots == 0)
		slots = 1;

	aif1 |= (slots - 1) << WM9081_AIFDAC_TDM_MODE_SHIFT;

	switch (rx_mask) {
	case 1:
		break;
	case 2:
		aif1 |= 0x10;
		break;
	case 4:
		aif1 |= 0x20;
		break;
	case 8:
		aif1 |= 0x30;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM9081_AUDIO_INTERFACE_1, aif1);

	return 0;
}

#define WM9081_RATES SNDRV_PCM_RATE_8000_96000

#define WM9081_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm9081_dai_ops = {
	.hw_params = wm9081_hw_params,
	.set_sysclk = wm9081_set_sysclk,
	.set_fmt = wm9081_set_dai_fmt,
	.digital_mute = wm9081_digital_mute,
	.set_tdm_slot = wm9081_set_tdm_slot,
};

/* We report two channels because the CODEC processes a stereo signal, even
 * though it is only capable of handling a mono output.
 */
struct snd_soc_dai wm9081_dai = {
	.name = "WM9081",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM9081_RATES,
		.formats = WM9081_FORMATS,
	},
	.ops = &wm9081_dai_ops,
};
EXPORT_SYMBOL_GPL(wm9081_dai);


static struct snd_soc_codec *wm9081_codec;

static int wm9081_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct wm9081_priv *wm9081;
	int ret = 0;

	if (wm9081_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm9081_codec;
	codec = wm9081_codec;
	wm9081 = snd_soc_codec_get_drvdata(codec);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm9081_snd_controls,
			     ARRAY_SIZE(wm9081_snd_controls));
	if (!wm9081->retune) {
		dev_dbg(codec->dev,
			"No ReTune Mobile data, using normal EQ\n");
		snd_soc_add_controls(codec, wm9081_eq_controls,
				     ARRAY_SIZE(wm9081_eq_controls));
	}

	snd_soc_dapm_new_controls(codec, wm9081_dapm_widgets,
				  ARRAY_SIZE(wm9081_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_paths, ARRAY_SIZE(audio_paths));

	return ret;

pcm_err:
	return ret;
}

static int wm9081_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

#ifdef CONFIG_PM
static int wm9081_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm9081_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm9081_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 *reg_cache = codec->reg_cache;
	int i;

	for (i = 0; i < codec->reg_cache_size; i++) {
		if (i == WM9081_SOFTWARE_RESET)
			continue;

		snd_soc_write(codec, i, reg_cache[i]);
	}

	wm9081_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define wm9081_suspend NULL
#define wm9081_resume NULL
#endif

struct snd_soc_codec_device soc_codec_dev_wm9081 = {
	.probe = 	wm9081_probe,
	.remove = 	wm9081_remove,
	.suspend =	wm9081_suspend,
	.resume =	wm9081_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm9081);

static int wm9081_register(struct wm9081_priv *wm9081,
			   enum snd_soc_control_type control)
{
	struct snd_soc_codec *codec = &wm9081->codec;
	int ret;
	u16 reg;

	if (wm9081_codec) {
		dev_err(codec->dev, "Another WM9081 is registered\n");
		ret = -EINVAL;
		goto err;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	snd_soc_codec_set_drvdata(codec, wm9081);
	codec->name = "WM9081";
	codec->owner = THIS_MODULE;
	codec->dai = &wm9081_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = ARRAY_SIZE(wm9081->reg_cache);
	codec->reg_cache = &wm9081->reg_cache;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm9081_set_bias_level;
	codec->volatile_register = wm9081_volatile_register;

	memcpy(codec->reg_cache, wm9081_reg_defaults,
	       sizeof(wm9081_reg_defaults));

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, control);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	reg = snd_soc_read(codec, WM9081_SOFTWARE_RESET);
	if (reg != 0x9081) {
		dev_err(codec->dev, "Device is not a WM9081: ID=0x%x\n", reg);
		ret = -EINVAL;
		goto err;
	}

	ret = wm9081_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	wm9081_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Enable zero cross by default */
	reg = snd_soc_read(codec, WM9081_ANALOGUE_LINEOUT);
	snd_soc_write(codec, WM9081_ANALOGUE_LINEOUT, reg | WM9081_LINEOUTZC);
	reg = snd_soc_read(codec, WM9081_ANALOGUE_SPEAKER_PGA);
	snd_soc_write(codec, WM9081_ANALOGUE_SPEAKER_PGA,
		     reg | WM9081_SPKPGAZC);

	wm9081_dai.dev = codec->dev;

	wm9081_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm9081_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;

err:
	kfree(wm9081);
	return ret;
}

static void wm9081_unregister(struct wm9081_priv *wm9081)
{
	wm9081_set_bias_level(&wm9081->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm9081_dai);
	snd_soc_unregister_codec(&wm9081->codec);
	kfree(wm9081);
	wm9081_codec = NULL;
}

static __devinit int wm9081_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm9081_priv *wm9081;
	struct snd_soc_codec *codec;

	wm9081 = kzalloc(sizeof(struct wm9081_priv), GFP_KERNEL);
	if (wm9081 == NULL)
		return -ENOMEM;

	codec = &wm9081->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;
	wm9081->retune = i2c->dev.platform_data;

	i2c_set_clientdata(i2c, wm9081);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm9081_register(wm9081, SND_SOC_I2C);
}

static __devexit int wm9081_i2c_remove(struct i2c_client *client)
{
	struct wm9081_priv *wm9081 = i2c_get_clientdata(client);
	wm9081_unregister(wm9081);
	return 0;
}

static const struct i2c_device_id wm9081_i2c_id[] = {
	{ "wm9081", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm9081_i2c_id);

static struct i2c_driver wm9081_i2c_driver = {
	.driver = {
		.name = "wm9081",
		.owner = THIS_MODULE,
	},
	.probe =    wm9081_i2c_probe,
	.remove =   __devexit_p(wm9081_i2c_remove),
	.id_table = wm9081_i2c_id,
};

static int __init wm9081_modinit(void)
{
	int ret;

	ret = i2c_add_driver(&wm9081_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM9081 I2C driver: %d\n",
		       ret);
	}

	return ret;
}
module_init(wm9081_modinit);

static void __exit wm9081_exit(void)
{
	i2c_del_driver(&wm9081_i2c_driver);
}
module_exit(wm9081_exit);


MODULE_DESCRIPTION("ASoC WM9081 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
