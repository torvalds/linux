/*
 * wm8580.c  --  WM8580 ALSA Soc Audio driver
 *
 * Copyright 2008, 2009 Wolfson Microelectronics PLC.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Notes:
 *  The WM8580 is a multichannel codec with S/PDIF support, featuring six
 *  DAC channels and two ADC channels.
 *
 *  Currently only the primary audio interface is supported - S/PDIF and
 *  the secondary audio interfaces are not.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <asm/div64.h>

#include "wm8580.h"

/* WM8580 register space */
#define WM8580_PLLA1                         0x00
#define WM8580_PLLA2                         0x01
#define WM8580_PLLA3                         0x02
#define WM8580_PLLA4                         0x03
#define WM8580_PLLB1                         0x04
#define WM8580_PLLB2                         0x05
#define WM8580_PLLB3                         0x06
#define WM8580_PLLB4                         0x07
#define WM8580_CLKSEL                        0x08
#define WM8580_PAIF1                         0x09
#define WM8580_PAIF2                         0x0A
#define WM8580_SAIF1                         0x0B
#define WM8580_PAIF3                         0x0C
#define WM8580_PAIF4                         0x0D
#define WM8580_SAIF2                         0x0E
#define WM8580_DAC_CONTROL1                  0x0F
#define WM8580_DAC_CONTROL2                  0x10
#define WM8580_DAC_CONTROL3                  0x11
#define WM8580_DAC_CONTROL4                  0x12
#define WM8580_DAC_CONTROL5                  0x13
#define WM8580_DIGITAL_ATTENUATION_DACL1     0x14
#define WM8580_DIGITAL_ATTENUATION_DACR1     0x15
#define WM8580_DIGITAL_ATTENUATION_DACL2     0x16
#define WM8580_DIGITAL_ATTENUATION_DACR2     0x17
#define WM8580_DIGITAL_ATTENUATION_DACL3     0x18
#define WM8580_DIGITAL_ATTENUATION_DACR3     0x19
#define WM8580_MASTER_DIGITAL_ATTENUATION    0x1C
#define WM8580_ADC_CONTROL1                  0x1D
#define WM8580_SPDTXCHAN0                    0x1E
#define WM8580_SPDTXCHAN1                    0x1F
#define WM8580_SPDTXCHAN2                    0x20
#define WM8580_SPDTXCHAN3                    0x21
#define WM8580_SPDTXCHAN4                    0x22
#define WM8580_SPDTXCHAN5                    0x23
#define WM8580_SPDMODE                       0x24
#define WM8580_INTMASK                       0x25
#define WM8580_GPO1                          0x26
#define WM8580_GPO2                          0x27
#define WM8580_GPO3                          0x28
#define WM8580_GPO4                          0x29
#define WM8580_GPO5                          0x2A
#define WM8580_INTSTAT                       0x2B
#define WM8580_SPDRXCHAN1                    0x2C
#define WM8580_SPDRXCHAN2                    0x2D
#define WM8580_SPDRXCHAN3                    0x2E
#define WM8580_SPDRXCHAN4                    0x2F
#define WM8580_SPDRXCHAN5                    0x30
#define WM8580_SPDSTAT                       0x31
#define WM8580_PWRDN1                        0x32
#define WM8580_PWRDN2                        0x33
#define WM8580_READBACK                      0x34
#define WM8580_RESET                         0x35

#define WM8580_MAX_REGISTER                  0x35

#define WM8580_DACOSR 0x40

/* PLLB4 (register 7h) */
#define WM8580_PLLB4_MCLKOUTSRC_MASK   0x60
#define WM8580_PLLB4_MCLKOUTSRC_PLLA   0x20
#define WM8580_PLLB4_MCLKOUTSRC_PLLB   0x40
#define WM8580_PLLB4_MCLKOUTSRC_OSC    0x60

#define WM8580_PLLB4_CLKOUTSRC_MASK    0x180
#define WM8580_PLLB4_CLKOUTSRC_PLLACLK 0x080
#define WM8580_PLLB4_CLKOUTSRC_PLLBCLK 0x100
#define WM8580_PLLB4_CLKOUTSRC_OSCCLK  0x180

/* CLKSEL (register 8h) */
#define WM8580_CLKSEL_DAC_CLKSEL_MASK 0x03
#define WM8580_CLKSEL_DAC_CLKSEL_PLLA 0x01
#define WM8580_CLKSEL_DAC_CLKSEL_PLLB 0x02

/* AIF control 1 (registers 9h-bh) */
#define WM8580_AIF_RATE_MASK       0x7
#define WM8580_AIF_BCLKSEL_MASK   0x18

#define WM8580_AIF_MS             0x20

#define WM8580_AIF_CLKSRC_MASK    0xc0
#define WM8580_AIF_CLKSRC_PLLA    0x40
#define WM8580_AIF_CLKSRC_PLLB    0x40
#define WM8580_AIF_CLKSRC_MCLK    0xc0

/* AIF control 2 (registers ch-eh) */
#define WM8580_AIF_FMT_MASK    0x03
#define WM8580_AIF_FMT_RIGHTJ  0x00
#define WM8580_AIF_FMT_LEFTJ   0x01
#define WM8580_AIF_FMT_I2S     0x02
#define WM8580_AIF_FMT_DSP     0x03

#define WM8580_AIF_LENGTH_MASK   0x0c
#define WM8580_AIF_LENGTH_16     0x00
#define WM8580_AIF_LENGTH_20     0x04
#define WM8580_AIF_LENGTH_24     0x08
#define WM8580_AIF_LENGTH_32     0x0c

#define WM8580_AIF_LRP         0x10
#define WM8580_AIF_BCP         0x20

/* Powerdown Register 1 (register 32h) */
#define WM8580_PWRDN1_PWDN     0x001
#define WM8580_PWRDN1_ALLDACPD 0x040

/* Powerdown Register 2 (register 33h) */
#define WM8580_PWRDN2_OSSCPD   0x001
#define WM8580_PWRDN2_PLLAPD   0x002
#define WM8580_PWRDN2_PLLBPD   0x004
#define WM8580_PWRDN2_SPDIFPD  0x008
#define WM8580_PWRDN2_SPDIFTXD 0x010
#define WM8580_PWRDN2_SPDIFRXD 0x020

#define WM8580_DAC_CONTROL5_MUTEALL 0x10

/*
 * wm8580 register cache
 * We can't read the WM8580 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8580_reg[] = {
	0x0121, 0x017e, 0x007d, 0x0014, /*R3*/
	0x0121, 0x017e, 0x007d, 0x0194, /*R7*/
	0x0010, 0x0002, 0x0002, 0x00c2, /*R11*/
	0x0182, 0x0082, 0x000a, 0x0024, /*R15*/
	0x0009, 0x0000, 0x00ff, 0x0000, /*R19*/
	0x00ff, 0x00ff, 0x00ff, 0x00ff, /*R23*/
	0x00ff, 0x00ff, 0x00ff, 0x00ff, /*R27*/
	0x01f0, 0x0040, 0x0000, 0x0000, /*R31(0x1F)*/
	0x0000, 0x0000, 0x0031, 0x000b, /*R35*/
	0x0039, 0x0000, 0x0010, 0x0032, /*R39*/
	0x0054, 0x0076, 0x0098, 0x0000, /*R43(0x2B)*/
	0x0000, 0x0000, 0x0000, 0x0000, /*R47*/
	0x0000, 0x0000, 0x005e, 0x003e, /*R51(0x33)*/
	0x0000, 0x0000 /*R53*/
};

struct pll_state {
	unsigned int in;
	unsigned int out;
};

#define WM8580_NUM_SUPPLIES 3
static const char *wm8580_supply_names[WM8580_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
	"PVDD",
};

/* codec private data */
struct wm8580_priv {
	enum snd_soc_control_type control_type;
	struct regulator_bulk_data supplies[WM8580_NUM_SUPPLIES];
	struct pll_state a;
	struct pll_state b;
	int sysclk[2];
};

static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);

static int wm8580_out_vu(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 *reg_cache = codec->reg_cache;
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	int ret;

	/* Clear the register cache so we write without VU set */
	reg_cache[reg] = 0;
	reg_cache[reg2] = 0;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/* Now write again with the volume update bit set */
	snd_soc_update_bits(codec, reg, 0x100, 0x100);
	snd_soc_update_bits(codec, reg2, 0x100, 0x100);

	return 0;
}

static const struct snd_kcontrol_new wm8580_snd_controls[] = {
SOC_DOUBLE_R_EXT_TLV("DAC1 Playback Volume",
		     WM8580_DIGITAL_ATTENUATION_DACL1,
		     WM8580_DIGITAL_ATTENUATION_DACR1,
		     0, 0xff, 0, snd_soc_get_volsw, wm8580_out_vu, dac_tlv),
SOC_DOUBLE_R_EXT_TLV("DAC2 Playback Volume",
		     WM8580_DIGITAL_ATTENUATION_DACL2,
		     WM8580_DIGITAL_ATTENUATION_DACR2,
		     0, 0xff, 0, snd_soc_get_volsw, wm8580_out_vu, dac_tlv),
SOC_DOUBLE_R_EXT_TLV("DAC3 Playback Volume",
		     WM8580_DIGITAL_ATTENUATION_DACL3,
		     WM8580_DIGITAL_ATTENUATION_DACR3,
		     0, 0xff, 0, snd_soc_get_volsw, wm8580_out_vu, dac_tlv),

SOC_SINGLE("DAC1 Deemphasis Switch", WM8580_DAC_CONTROL3, 0, 1, 0),
SOC_SINGLE("DAC2 Deemphasis Switch", WM8580_DAC_CONTROL3, 1, 1, 0),
SOC_SINGLE("DAC3 Deemphasis Switch", WM8580_DAC_CONTROL3, 2, 1, 0),

SOC_DOUBLE("DAC1 Invert Switch", WM8580_DAC_CONTROL4,  0, 1, 1, 0),
SOC_DOUBLE("DAC2 Invert Switch", WM8580_DAC_CONTROL4,  2, 3, 1, 0),
SOC_DOUBLE("DAC3 Invert Switch", WM8580_DAC_CONTROL4,  4, 5, 1, 0),

SOC_SINGLE("DAC ZC Switch", WM8580_DAC_CONTROL5, 5, 1, 0),
SOC_SINGLE("DAC1 Switch", WM8580_DAC_CONTROL5, 0, 1, 1),
SOC_SINGLE("DAC2 Switch", WM8580_DAC_CONTROL5, 1, 1, 1),
SOC_SINGLE("DAC3 Switch", WM8580_DAC_CONTROL5, 2, 1, 1),

SOC_DOUBLE("Capture Switch", WM8580_ADC_CONTROL1, 0, 1, 1, 1),
SOC_SINGLE("Capture High-Pass Filter Switch", WM8580_ADC_CONTROL1, 4, 1, 0),
};

static const struct snd_soc_dapm_widget wm8580_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC1", "Playback", WM8580_PWRDN1, 2, 1),
SND_SOC_DAPM_DAC("DAC2", "Playback", WM8580_PWRDN1, 3, 1),
SND_SOC_DAPM_DAC("DAC3", "Playback", WM8580_PWRDN1, 4, 1),

SND_SOC_DAPM_OUTPUT("VOUT1L"),
SND_SOC_DAPM_OUTPUT("VOUT1R"),
SND_SOC_DAPM_OUTPUT("VOUT2L"),
SND_SOC_DAPM_OUTPUT("VOUT2R"),
SND_SOC_DAPM_OUTPUT("VOUT3L"),
SND_SOC_DAPM_OUTPUT("VOUT3R"),

SND_SOC_DAPM_ADC("ADC", "Capture", WM8580_PWRDN1, 1, 1),

SND_SOC_DAPM_INPUT("AINL"),
SND_SOC_DAPM_INPUT("AINR"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{ "VOUT1L", NULL, "DAC1" },
	{ "VOUT1R", NULL, "DAC1" },

	{ "VOUT2L", NULL, "DAC2" },
	{ "VOUT2R", NULL, "DAC2" },

	{ "VOUT3L", NULL, "DAC3" },
	{ "VOUT3R", NULL, "DAC3" },

	{ "ADC", NULL, "AINL" },
	{ "ADC", NULL, "AINR" },
};

static int wm8580_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8580_dapm_widgets,
				  ARRAY_SIZE(wm8580_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

/* PLL divisors */
struct _pll_div {
	u32 prescale:1;
	u32 postscale:1;
	u32 freqmode:2;
	u32 n:4;
	u32 k:24;
};

/* The size in bits of the pll divide */
#define FIXED_PLL_SIZE (1 << 22)

/* PLL rate to output rate divisions */
static struct {
	unsigned int div;
	unsigned int freqmode;
	unsigned int postscale;
} post_table[] = {
	{  2,  0, 0 },
	{  4,  0, 1 },
	{  4,  1, 0 },
	{  8,  1, 1 },
	{  8,  2, 0 },
	{ 16,  2, 1 },
	{ 12,  3, 0 },
	{ 24,  3, 1 }
};

static int pll_factors(struct _pll_div *pll_div, unsigned int target,
		       unsigned int source)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;
	int i;

	pr_debug("wm8580: PLL %uHz->%uHz\n", source, target);

	/* Scale the output frequency up; the PLL should run in the
	 * region of 90-100MHz.
	 */
	for (i = 0; i < ARRAY_SIZE(post_table); i++) {
		if (target * post_table[i].div >=  90000000 &&
		    target * post_table[i].div <= 100000000) {
			pll_div->freqmode = post_table[i].freqmode;
			pll_div->postscale = post_table[i].postscale;
			target *= post_table[i].div;
			break;
		}
	}

	if (i == ARRAY_SIZE(post_table)) {
		printk(KERN_ERR "wm8580: Unable to scale output frequency "
		       "%u\n", target);
		return -EINVAL;
	}

	Ndiv = target / source;

	if (Ndiv < 5) {
		source /= 2;
		pll_div->prescale = 1;
		Ndiv = target / source;
	} else
		pll_div->prescale = 0;

	if ((Ndiv < 5) || (Ndiv > 13)) {
		printk(KERN_ERR
			"WM8580 N=%u outside supported range\n", Ndiv);
		return -EINVAL;
	}

	pll_div->n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	pll_div->k = K;

	pr_debug("PLL %x.%x prescale %d freqmode %d postscale %d\n",
		 pll_div->n, pll_div->k, pll_div->prescale, pll_div->freqmode,
		 pll_div->postscale);

	return 0;
}

static int wm8580_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	int offset;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8580_priv *wm8580 = snd_soc_codec_get_drvdata(codec);
	struct pll_state *state;
	struct _pll_div pll_div;
	unsigned int reg;
	unsigned int pwr_mask;
	int ret;

	/* GCC isn't able to work out the ifs below for initialising/using
	 * pll_div so suppress warnings.
	 */
	memset(&pll_div, 0, sizeof(pll_div));

	switch (pll_id) {
	case WM8580_PLLA:
		state = &wm8580->a;
		offset = 0;
		pwr_mask = WM8580_PWRDN2_PLLAPD;
		break;
	case WM8580_PLLB:
		state = &wm8580->b;
		offset = 4;
		pwr_mask = WM8580_PWRDN2_PLLBPD;
		break;
	default:
		return -ENODEV;
	}

	if (freq_in && freq_out) {
		ret = pll_factors(&pll_div, freq_out, freq_in);
		if (ret != 0)
			return ret;
	}

	state->in = freq_in;
	state->out = freq_out;

	/* Always disable the PLL - it is not safe to leave it running
	 * while reprogramming it.
	 */
	snd_soc_update_bits(codec, WM8580_PWRDN2, pwr_mask, pwr_mask);

	if (!freq_in || !freq_out)
		return 0;

	snd_soc_write(codec, WM8580_PLLA1 + offset, pll_div.k & 0x1ff);
	snd_soc_write(codec, WM8580_PLLA2 + offset, (pll_div.k >> 9) & 0x1ff);
	snd_soc_write(codec, WM8580_PLLA3 + offset,
		     (pll_div.k >> 18 & 0xf) | (pll_div.n << 4));

	reg = snd_soc_read(codec, WM8580_PLLA4 + offset);
	reg &= ~0x1b;
	reg |= pll_div.prescale | pll_div.postscale << 1 |
		pll_div.freqmode << 3;

	snd_soc_write(codec, WM8580_PLLA4 + offset, reg);

	/* All done, turn it on */
	snd_soc_update_bits(codec, WM8580_PWRDN2, pwr_mask, 0);

	return 0;
}

static const int wm8580_sysclk_ratios[] = {
	128, 192, 256, 384, 512, 768, 1152,
};

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8580_paif_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8580_priv *wm8580 = snd_soc_codec_get_drvdata(codec);
	u16 paifa = 0;
	u16 paifb = 0;
	int i, ratio, osr;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		paifa |= 0x8;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		paifa |= 0x0;
		paifb |= WM8580_AIF_LENGTH_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		paifa |= 0x0;
		paifb |= WM8580_AIF_LENGTH_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		paifa |= 0x0;
		paifb |= WM8580_AIF_LENGTH_32;
		break;
	default:
		return -EINVAL;
	}

	/* Look up the SYSCLK ratio; accept only exact matches */
	ratio = wm8580->sysclk[dai->driver->id] / params_rate(params);
	for (i = 0; i < ARRAY_SIZE(wm8580_sysclk_ratios); i++)
		if (ratio == wm8580_sysclk_ratios[i])
			break;
	if (i == ARRAY_SIZE(wm8580_sysclk_ratios)) {
		dev_err(codec->dev, "Invalid clock ratio %d/%d\n",
			wm8580->sysclk[dai->driver->id], params_rate(params));
		return -EINVAL;
	}
	paifa |= i;
	dev_dbg(codec->dev, "Running at %dfs with %dHz clock\n",
		wm8580_sysclk_ratios[i], wm8580->sysclk[dai->driver->id]);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (ratio) {
		case 128:
		case 192:
			osr = WM8580_DACOSR;
			dev_dbg(codec->dev, "Selecting 64x OSR\n");
			break;
		default:
			osr = 0;
			dev_dbg(codec->dev, "Selecting 128x OSR\n");
			break;
		}

		snd_soc_update_bits(codec, WM8580_PAIF3, WM8580_DACOSR, osr);
	}

	snd_soc_update_bits(codec, WM8580_PAIF1 + dai->driver->id,
			    WM8580_AIF_RATE_MASK | WM8580_AIF_BCLKSEL_MASK,
			    paifa);
	snd_soc_update_bits(codec, WM8580_PAIF3 + dai->driver->id,
			    WM8580_AIF_LENGTH_MASK, paifb);
	return 0;
}

static int wm8580_set_paif_dai_fmt(struct snd_soc_dai *codec_dai,
				      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int aifa;
	unsigned int aifb;
	int can_invert_lrclk;

	aifa = snd_soc_read(codec, WM8580_PAIF1 + codec_dai->driver->id);
	aifb = snd_soc_read(codec, WM8580_PAIF3 + codec_dai->driver->id);

	aifb &= ~(WM8580_AIF_FMT_MASK | WM8580_AIF_LRP | WM8580_AIF_BCP);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		aifa &= ~WM8580_AIF_MS;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aifa |= WM8580_AIF_MS;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		can_invert_lrclk = 1;
		aifb |= WM8580_AIF_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		can_invert_lrclk = 1;
		aifb |= WM8580_AIF_FMT_RIGHTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		can_invert_lrclk = 1;
		aifb |= WM8580_AIF_FMT_LEFTJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		can_invert_lrclk = 0;
		aifb |= WM8580_AIF_FMT_DSP;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		can_invert_lrclk = 0;
		aifb |= WM8580_AIF_FMT_DSP;
		aifb |= WM8580_AIF_LRP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_IB_IF:
		if (!can_invert_lrclk)
			return -EINVAL;
		aifb |= WM8580_AIF_BCP;
		aifb |= WM8580_AIF_LRP;
		break;

	case SND_SOC_DAIFMT_IB_NF:
		aifb |= WM8580_AIF_BCP;
		break;

	case SND_SOC_DAIFMT_NB_IF:
		if (!can_invert_lrclk)
			return -EINVAL;
		aifb |= WM8580_AIF_LRP;
		break;

	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8580_PAIF1 + codec_dai->driver->id, aifa);
	snd_soc_write(codec, WM8580_PAIF3 + codec_dai->driver->id, aifb);

	return 0;
}

static int wm8580_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
				 int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int reg;

	switch (div_id) {
	case WM8580_MCLK:
		reg = snd_soc_read(codec, WM8580_PLLB4);
		reg &= ~WM8580_PLLB4_MCLKOUTSRC_MASK;

		switch (div) {
		case WM8580_CLKSRC_MCLK:
			/* Input */
			break;

		case WM8580_CLKSRC_PLLA:
			reg |= WM8580_PLLB4_MCLKOUTSRC_PLLA;
			break;
		case WM8580_CLKSRC_PLLB:
			reg |= WM8580_PLLB4_MCLKOUTSRC_PLLB;
			break;

		case WM8580_CLKSRC_OSC:
			reg |= WM8580_PLLB4_MCLKOUTSRC_OSC;
			break;

		default:
			return -EINVAL;
		}
		snd_soc_write(codec, WM8580_PLLB4, reg);
		break;

	case WM8580_CLKOUTSRC:
		reg = snd_soc_read(codec, WM8580_PLLB4);
		reg &= ~WM8580_PLLB4_CLKOUTSRC_MASK;

		switch (div) {
		case WM8580_CLKSRC_NONE:
			break;

		case WM8580_CLKSRC_PLLA:
			reg |= WM8580_PLLB4_CLKOUTSRC_PLLACLK;
			break;

		case WM8580_CLKSRC_PLLB:
			reg |= WM8580_PLLB4_CLKOUTSRC_PLLBCLK;
			break;

		case WM8580_CLKSRC_OSC:
			reg |= WM8580_PLLB4_CLKOUTSRC_OSCCLK;
			break;

		default:
			return -EINVAL;
		}
		snd_soc_write(codec, WM8580_PLLB4, reg);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8580_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8580_priv *wm8580 = snd_soc_codec_get_drvdata(codec);
	int sel, sel_mask, sel_shift;

	switch (dai->driver->id) {
	case WM8580_DAI_PAIFRX:
		sel_mask = 0x3;
		sel_shift = 0;
		break;

	case WM8580_DAI_PAIFTX:
		sel_mask = 0xc;
		sel_shift = 2;
		break;

	default:
		BUG_ON("Unknown DAI driver ID\n");
		return -EINVAL;
	}

	switch (clk_id) {
	case WM8580_CLKSRC_ADCMCLK:
		if (dai->driver->id != WM8580_DAI_PAIFTX)
			return -EINVAL;
		sel = 0 << sel_shift;
		break;
	case WM8580_CLKSRC_PLLA:
		sel = 1 << sel_shift;
		break;
	case WM8580_CLKSRC_PLLB:
		sel = 2 << sel_shift;
		break;
	case WM8580_CLKSRC_MCLK:
		sel = 3 << sel_shift;
		break;
	default:
		dev_err(codec->dev, "Unknown clock %d\n", clk_id);
		return -EINVAL;
	}

	/* We really should validate PLL settings but not yet */
	wm8580->sysclk[dai->driver->id] = freq;

	return snd_soc_update_bits(codec, WM8580_CLKSEL, sel_mask, sel);
}

static int wm8580_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int reg;

	reg = snd_soc_read(codec, WM8580_DAC_CONTROL5);

	if (mute)
		reg |= WM8580_DAC_CONTROL5_MUTEALL;
	else
		reg &= ~WM8580_DAC_CONTROL5_MUTEALL;

	snd_soc_write(codec, WM8580_DAC_CONTROL5, reg);

	return 0;
}

static int wm8580_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			/* Power up and get individual control of the DACs */
			snd_soc_update_bits(codec, WM8580_PWRDN1,
					    WM8580_PWRDN1_PWDN |
					    WM8580_PWRDN1_ALLDACPD, 0);

			/* Make VMID high impedance */
			snd_soc_update_bits(codec, WM8580_ADC_CONTROL1,
					    0x100, 0);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, WM8580_PWRDN1,
				    WM8580_PWRDN1_PWDN, WM8580_PWRDN1_PWDN);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8580_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8580_dai_ops_playback = {
	.set_sysclk	= wm8580_set_sysclk,
	.hw_params	= wm8580_paif_hw_params,
	.set_fmt	= wm8580_set_paif_dai_fmt,
	.set_clkdiv	= wm8580_set_dai_clkdiv,
	.set_pll	= wm8580_set_dai_pll,
	.digital_mute	= wm8580_digital_mute,
};

static struct snd_soc_dai_ops wm8580_dai_ops_capture = {
	.set_sysclk	= wm8580_set_sysclk,
	.hw_params	= wm8580_paif_hw_params,
	.set_fmt	= wm8580_set_paif_dai_fmt,
	.set_clkdiv	= wm8580_set_dai_clkdiv,
	.set_pll	= wm8580_set_dai_pll,
};

static struct snd_soc_dai_driver wm8580_dai[] = {
	{
		.name = "wm8580-hifi-playback",
		.id	= WM8580_DAI_PAIFRX,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 6,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = WM8580_FORMATS,
		},
		.ops = &wm8580_dai_ops_playback,
	},
	{
		.name = "wm8580-hifi-capture",
		.id	=	WM8580_DAI_PAIFTX,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = WM8580_FORMATS,
		},
		.ops = &wm8580_dai_ops_capture,
	},
};

static int wm8580_probe(struct snd_soc_codec *codec)
{
	struct wm8580_priv *wm8580 = snd_soc_codec_get_drvdata(codec);
	int ret = 0,i;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, wm8580->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8580->supplies); i++)
		wm8580->supplies[i].supply = wm8580_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8580->supplies),
				 wm8580->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8580->supplies),
				    wm8580->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_regulator_get;
	}

	/* Get the codec into a known state */
	ret = snd_soc_write(codec, WM8580_RESET, 0);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to reset codec: %d\n", ret);
		goto err_regulator_enable;
	}

	wm8580_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	snd_soc_add_controls(codec, wm8580_snd_controls,
			     ARRAY_SIZE(wm8580_snd_controls));
	wm8580_add_widgets(codec);

	return 0;

err_regulator_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
err_regulator_get:
	regulator_bulk_free(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
	return ret;
}

/* power down chip */
static int wm8580_remove(struct snd_soc_codec *codec)
{
	struct wm8580_priv *wm8580 = snd_soc_codec_get_drvdata(codec);

	wm8580_set_bias_level(codec, SND_SOC_BIAS_OFF);

	regulator_bulk_disable(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
	regulator_bulk_free(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8580 = {
	.probe =	wm8580_probe,
	.remove =	wm8580_remove,
	.set_bias_level = wm8580_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(wm8580_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8580_reg,
};

static const struct of_device_id wm8580_of_match[] = {
	{ .compatible = "wlf,wm8580" },
	{ },
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int wm8580_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8580_priv *wm8580;
	int ret;

	wm8580 = kzalloc(sizeof(struct wm8580_priv), GFP_KERNEL);
	if (wm8580 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8580);
	wm8580->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8580, wm8580_dai, ARRAY_SIZE(wm8580_dai));
	if (ret < 0)
		kfree(wm8580);
	return ret;
}

static int wm8580_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8580_i2c_id[] = {
	{ "wm8580", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8580_i2c_id);

static struct i2c_driver wm8580_i2c_driver = {
	.driver = {
		.name = "wm8580",
		.owner = THIS_MODULE,
		.of_match_table = wm8580_of_match,
	},
	.probe =    wm8580_i2c_probe,
	.remove =   wm8580_i2c_remove,
	.id_table = wm8580_i2c_id,
};
#endif

static int __init wm8580_modinit(void)
{
	int ret = 0;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8580_i2c_driver);
	if (ret != 0) {
		pr_err("Failed to register WM8580 I2C driver: %d\n", ret);
	}
#endif

	return ret;
}
module_init(wm8580_modinit);

static void __exit wm8580_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8580_i2c_driver);
#endif
}
module_exit(wm8580_exit);

MODULE_DESCRIPTION("ASoC WM8580 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
