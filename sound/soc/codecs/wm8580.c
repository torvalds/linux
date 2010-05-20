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

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
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
#define WM8580_AIF_RATE_128        0x0
#define WM8580_AIF_RATE_192        0x1
#define WM8580_AIF_RATE_256        0x2
#define WM8580_AIF_RATE_384        0x3
#define WM8580_AIF_RATE_512        0x4
#define WM8580_AIF_RATE_768        0x5
#define WM8580_AIF_RATE_1152       0x6

#define WM8580_AIF_BCLKSEL_MASK   0x18
#define WM8580_AIF_BCLKSEL_64     0x00
#define WM8580_AIF_BCLKSEL_128    0x08
#define WM8580_AIF_BCLKSEL_256    0x10
#define WM8580_AIF_BCLKSEL_SYSCLK 0x18

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
	0x001c, 0x0002, 0x0002, 0x00c2, /*R11*/
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
	struct snd_soc_codec codec;
	struct regulator_bulk_data supplies[WM8580_NUM_SUPPLIES];
	u16 reg_cache[WM8580_MAX_REGISTER + 1];
	struct pll_state a;
	struct pll_state b;
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

	ret = snd_soc_put_volsw_2r(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/* Now write again with the volume update bit set */
	snd_soc_update_bits(codec, reg, 0x100, 0x100);
	snd_soc_update_bits(codec, reg2, 0x100, 0x100);

	return 0;
}

#define SOC_WM8580_OUT_DOUBLE_R_TLV(xname, reg_left, reg_right, xshift, xmax, \
				    xinvert, tlv_array)			\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		SNDRV_CTL_ELEM_ACCESS_READWRITE,  \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw_2r, \
	.get = snd_soc_get_volsw_2r, .put = wm8580_out_vu, \
	.private_value = (unsigned long)&(struct soc_mixer_control) \
		{.reg = reg_left, .rreg = reg_right, .shift = xshift, \
		.max = xmax, .invert = xinvert} }

static const struct snd_kcontrol_new wm8580_snd_controls[] = {
SOC_WM8580_OUT_DOUBLE_R_TLV("DAC1 Playback Volume",
			    WM8580_DIGITAL_ATTENUATION_DACL1,
			    WM8580_DIGITAL_ATTENUATION_DACR1,
			    0, 0xff, 0, dac_tlv),
SOC_WM8580_OUT_DOUBLE_R_TLV("DAC2 Playback Volume",
			    WM8580_DIGITAL_ATTENUATION_DACL2,
			    WM8580_DIGITAL_ATTENUATION_DACR2,
			    0, 0xff, 0, dac_tlv),
SOC_WM8580_OUT_DOUBLE_R_TLV("DAC3 Playback Volume",
			    WM8580_DIGITAL_ATTENUATION_DACL3,
			    WM8580_DIGITAL_ATTENUATION_DACR3,
			    0, 0xff, 0, dac_tlv),

SOC_SINGLE("DAC1 Deemphasis Switch", WM8580_DAC_CONTROL3, 0, 1, 0),
SOC_SINGLE("DAC2 Deemphasis Switch", WM8580_DAC_CONTROL3, 1, 1, 0),
SOC_SINGLE("DAC3 Deemphasis Switch", WM8580_DAC_CONTROL3, 2, 1, 0),

SOC_DOUBLE("DAC1 Invert Switch", WM8580_DAC_CONTROL4,  0, 1, 1, 0),
SOC_DOUBLE("DAC2 Invert Switch", WM8580_DAC_CONTROL4,  2, 3, 1, 0),
SOC_DOUBLE("DAC3 Invert Switch", WM8580_DAC_CONTROL4,  4, 5, 1, 0),

SOC_SINGLE("DAC ZC Switch", WM8580_DAC_CONTROL5, 5, 1, 0),
SOC_SINGLE("DAC1 Switch", WM8580_DAC_CONTROL5, 0, 1, 0),
SOC_SINGLE("DAC2 Switch", WM8580_DAC_CONTROL5, 1, 1, 0),
SOC_SINGLE("DAC3 Switch", WM8580_DAC_CONTROL5, 2, 1, 0),

SOC_DOUBLE("ADC Mute Switch", WM8580_ADC_CONTROL1, 0, 1, 1, 0),
SOC_SINGLE("ADC High-Pass Filter Switch", WM8580_ADC_CONTROL1, 4, 1, 0),
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
	snd_soc_dapm_new_controls(codec, wm8580_dapm_widgets,
				  ARRAY_SIZE(wm8580_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

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
	reg = snd_soc_read(codec, WM8580_PWRDN2);
	snd_soc_write(codec, WM8580_PWRDN2, reg | pwr_mask);

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
	reg = snd_soc_read(codec, WM8580_PWRDN2);
	snd_soc_write(codec, WM8580_PWRDN2, reg & ~pwr_mask);

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8580_paif_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 paifb = snd_soc_read(codec, WM8580_PAIF3 + dai->id);

	paifb &= ~WM8580_AIF_LENGTH_MASK;
	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		paifb |= WM8580_AIF_LENGTH_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		paifb |= WM8580_AIF_LENGTH_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		paifb |= WM8580_AIF_LENGTH_24;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8580_PAIF3 + dai->id, paifb);
	return 0;
}

static int wm8580_set_paif_dai_fmt(struct snd_soc_dai *codec_dai,
				      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int aifa;
	unsigned int aifb;
	int can_invert_lrclk;

	aifa = snd_soc_read(codec, WM8580_PAIF1 + codec_dai->id);
	aifb = snd_soc_read(codec, WM8580_PAIF3 + codec_dai->id);

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

	snd_soc_write(codec, WM8580_PAIF1 + codec_dai->id, aifa);
	snd_soc_write(codec, WM8580_PAIF3 + codec_dai->id, aifb);

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

	case WM8580_DAC_CLKSEL:
		reg = snd_soc_read(codec, WM8580_CLKSEL);
		reg &= ~WM8580_CLKSEL_DAC_CLKSEL_MASK;

		switch (div) {
		case WM8580_CLKSRC_MCLK:
			break;

		case WM8580_CLKSRC_PLLA:
			reg |= WM8580_CLKSEL_DAC_CLKSEL_PLLA;
			break;

		case WM8580_CLKSRC_PLLB:
			reg |= WM8580_CLKSEL_DAC_CLKSEL_PLLB;
			break;

		default:
			return -EINVAL;
		}
		snd_soc_write(codec, WM8580_CLKSEL, reg);
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
	u16 reg;
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* Power up and get individual control of the DACs */
			reg = snd_soc_read(codec, WM8580_PWRDN1);
			reg &= ~(WM8580_PWRDN1_PWDN | WM8580_PWRDN1_ALLDACPD);
			snd_soc_write(codec, WM8580_PWRDN1, reg);

			/* Make VMID high impedence */
			reg = snd_soc_read(codec,  WM8580_ADC_CONTROL1);
			reg &= ~0x100;
			snd_soc_write(codec, WM8580_ADC_CONTROL1, reg);
		}
		break;

	case SND_SOC_BIAS_OFF:
		reg = snd_soc_read(codec, WM8580_PWRDN1);
		snd_soc_write(codec, WM8580_PWRDN1, reg | WM8580_PWRDN1_PWDN);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define WM8580_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8580_dai_ops_playback = {
	.hw_params	= wm8580_paif_hw_params,
	.set_fmt	= wm8580_set_paif_dai_fmt,
	.set_clkdiv	= wm8580_set_dai_clkdiv,
	.set_pll	= wm8580_set_dai_pll,
	.digital_mute	= wm8580_digital_mute,
};

static struct snd_soc_dai_ops wm8580_dai_ops_capture = {
	.hw_params	= wm8580_paif_hw_params,
	.set_fmt	= wm8580_set_paif_dai_fmt,
	.set_clkdiv	= wm8580_set_dai_clkdiv,
	.set_pll	= wm8580_set_dai_pll,
};

struct snd_soc_dai wm8580_dai[] = {
	{
		.name = "WM8580 PAIFRX",
		.id = 0,
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
		.name = "WM8580 PAIFTX",
		.id = 1,
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
EXPORT_SYMBOL_GPL(wm8580_dai);

static struct snd_soc_codec *wm8580_codec;

static int wm8580_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8580_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8580_codec;
	codec = wm8580_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8580_snd_controls,
			     ARRAY_SIZE(wm8580_snd_controls));
	wm8580_add_widgets(codec);

	return ret;

pcm_err:
	return ret;
}

/* power down chip */
static int wm8580_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8580 = {
	.probe = 	wm8580_probe,
	.remove = 	wm8580_remove,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8580);

static int wm8580_register(struct wm8580_priv *wm8580,
			   enum snd_soc_control_type control)
{
	int ret, i;
	struct snd_soc_codec *codec = &wm8580->codec;

	if (wm8580_codec) {
		dev_err(codec->dev, "Another WM8580 is registered\n");
		ret = -EINVAL;
		goto err;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	snd_soc_codec_set_drvdata(codec, wm8580);
	codec->name = "WM8580";
	codec->owner = THIS_MODULE;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8580_set_bias_level;
	codec->dai = wm8580_dai;
	codec->num_dai = ARRAY_SIZE(wm8580_dai);
	codec->reg_cache_size = ARRAY_SIZE(wm8580->reg_cache);
	codec->reg_cache = &wm8580->reg_cache;

	memcpy(codec->reg_cache, wm8580_reg, sizeof(wm8580_reg));

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, control);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm8580->supplies); i++)
		wm8580->supplies[i].supply = wm8580_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8580->supplies),
				 wm8580->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		goto err;
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

	for (i = 0; i < ARRAY_SIZE(wm8580_dai); i++)
		wm8580_dai[i].dev = codec->dev;

	wm8580_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	wm8580_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto err_regulator_enable;
	}

	ret = snd_soc_register_dais(wm8580_dai, ARRAY_SIZE(wm8580_dai));
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}

	return 0;

err_codec:
	snd_soc_unregister_codec(codec);
err_regulator_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
err_regulator_get:
	regulator_bulk_free(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
err:
	kfree(wm8580);
	return ret;
}

static void wm8580_unregister(struct wm8580_priv *wm8580)
{
	wm8580_set_bias_level(&wm8580->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dais(wm8580_dai, ARRAY_SIZE(wm8580_dai));
	snd_soc_unregister_codec(&wm8580->codec);
	regulator_bulk_disable(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
	regulator_bulk_free(ARRAY_SIZE(wm8580->supplies), wm8580->supplies);
	kfree(wm8580);
	wm8580_codec = NULL;
}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int wm8580_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8580_priv *wm8580;
	struct snd_soc_codec *codec;

	wm8580 = kzalloc(sizeof(struct wm8580_priv), GFP_KERNEL);
	if (wm8580 == NULL)
		return -ENOMEM;

	codec = &wm8580->codec;

	i2c_set_clientdata(i2c, wm8580);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8580_register(wm8580, SND_SOC_I2C);
}

static int wm8580_i2c_remove(struct i2c_client *client)
{
	struct wm8580_priv *wm8580 = i2c_get_clientdata(client);
	wm8580_unregister(wm8580);
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
	},
	.probe =    wm8580_i2c_probe,
	.remove =   wm8580_i2c_remove,
	.id_table = wm8580_i2c_id,
};
#endif

static int __init wm8580_modinit(void)
{
	int ret;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8580_i2c_driver);
	if (ret != 0) {
		pr_err("Failed to register WM8580 I2C driver: %d\n", ret);
	}
#endif

	return 0;
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
