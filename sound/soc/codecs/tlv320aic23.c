/*
 * ALSA SoC TLV320AIC23 codec driver
 *
 * Author:      Arun KS, <arunks@mistralsolutions.com>
 * Copyright:   (C) 2008 Mistral Solutions Pvt Ltd.,
 *
 * Based on sound/soc/codecs/wm8731.c by Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Notes:
 *  The AIC23 is a driver for a low power stereo audio
 *  codec tlv320aic23
 *
 *  The machine layer should disable unsupported inputs/outputs by
 *  snd_soc_dapm_disable_pin(codec, "LHPOUT"), etc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/initval.h>

#include "tlv320aic23.h"

/*
 * AIC23 register cache
 */
static const struct reg_default tlv320aic23_reg[] = {
	{  0, 0x0097 },
	{  1, 0x0097 },
	{  2, 0x00F9 },
	{  3, 0x00F9 },
	{  4, 0x001A },
	{  5, 0x0004 },
	{  6, 0x0007 },
	{  7, 0x0001 },
	{  8, 0x0020 },
	{  9, 0x0000 },
};

const struct regmap_config tlv320aic23_regmap = {
	.reg_bits = 7,
	.val_bits = 9,

	.max_register = TLV320AIC23_RESET,
	.reg_defaults = tlv320aic23_reg,
	.num_reg_defaults = ARRAY_SIZE(tlv320aic23_reg),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL(tlv320aic23_regmap);

static const char *rec_src_text[] = { "Line", "Mic" };
static const char *deemph_text[] = {"None", "32Khz", "44.1Khz", "48Khz"};

static SOC_ENUM_SINGLE_DECL(rec_src_enum,
			    TLV320AIC23_ANLG, 2, rec_src_text);

static const struct snd_kcontrol_new tlv320aic23_rec_src_mux_controls =
SOC_DAPM_ENUM("Input Select", rec_src_enum);

static SOC_ENUM_SINGLE_DECL(tlv320aic23_rec_src,
			    TLV320AIC23_ANLG, 2, rec_src_text);
static SOC_ENUM_SINGLE_DECL(tlv320aic23_deemph,
			    TLV320AIC23_DIGT, 1, deemph_text);

static const DECLARE_TLV_DB_SCALE(out_gain_tlv, -12100, 100, 0);
static const DECLARE_TLV_DB_SCALE(input_gain_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_vol_tlv, -1800, 300, 0);

static int snd_soc_tlv320aic23_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	u16 val, reg;

	val = (ucontrol->value.integer.value[0] & 0x07);

	/* linear conversion to userspace
	* 000	=	-6db
	* 001	=	-9db
	* 010	=	-12db
	* 011	=	-18db (Min)
	* 100	=	0db (Max)
	*/
	val = (val >= 4) ? 4  : (3 - val);

	reg = snd_soc_read(codec, TLV320AIC23_ANLG) & (~0x1C0);
	snd_soc_write(codec, TLV320AIC23_ANLG, reg | (val << 6));

	return 0;
}

static int snd_soc_tlv320aic23_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	u16 val;

	val = snd_soc_read(codec, TLV320AIC23_ANLG) & (0x1C0);
	val = val >> 6;
	val = (val >= 4) ? 4  : (3 -  val);
	ucontrol->value.integer.value[0] = val;
	return 0;

}

static const struct snd_kcontrol_new tlv320aic23_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Digital Playback Volume", TLV320AIC23_LCHNVOL,
			 TLV320AIC23_RCHNVOL, 0, 127, 0, out_gain_tlv),
	SOC_SINGLE("Digital Playback Switch", TLV320AIC23_DIGT, 3, 1, 1),
	SOC_DOUBLE_R("Line Input Switch", TLV320AIC23_LINVOL,
		     TLV320AIC23_RINVOL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("Line Input Volume", TLV320AIC23_LINVOL,
			 TLV320AIC23_RINVOL, 0, 31, 0, input_gain_tlv),
	SOC_SINGLE("Mic Input Switch", TLV320AIC23_ANLG, 1, 1, 1),
	SOC_SINGLE("Mic Booster Switch", TLV320AIC23_ANLG, 0, 1, 0),
	SOC_SINGLE_EXT_TLV("Sidetone Volume", TLV320AIC23_ANLG, 6, 4, 0,
			   snd_soc_tlv320aic23_get_volsw,
			   snd_soc_tlv320aic23_put_volsw, sidetone_vol_tlv),
	SOC_ENUM("Playback De-emphasis", tlv320aic23_deemph),
};

/* PGA Mixer controls for Line and Mic switch */
static const struct snd_kcontrol_new tlv320aic23_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line Bypass Switch", TLV320AIC23_ANLG, 3, 1, 0),
	SOC_DAPM_SINGLE("Mic Sidetone Switch", TLV320AIC23_ANLG, 5, 1, 0),
	SOC_DAPM_SINGLE("Playback Switch", TLV320AIC23_ANLG, 4, 1, 0),
};

static const struct snd_soc_dapm_widget tlv320aic23_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", TLV320AIC23_PWR, 3, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", TLV320AIC23_PWR, 2, 1),
	SND_SOC_DAPM_MUX("Capture Source", SND_SOC_NOPM, 0, 0,
			 &tlv320aic23_rec_src_mux_controls),
	SND_SOC_DAPM_MIXER("Output Mixer", TLV320AIC23_PWR, 4, 1,
			   &tlv320aic23_output_mixer_controls[0],
			   ARRAY_SIZE(tlv320aic23_output_mixer_controls)),
	SND_SOC_DAPM_PGA("Line Input", TLV320AIC23_PWR, 0, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Mic Input", TLV320AIC23_PWR, 1, 1, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LHPOUT"),
	SND_SOC_DAPM_OUTPUT("RHPOUT"),
	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),

	SND_SOC_DAPM_INPUT("LLINEIN"),
	SND_SOC_DAPM_INPUT("RLINEIN"),

	SND_SOC_DAPM_INPUT("MICIN"),
};

static const struct snd_soc_dapm_route tlv320aic23_intercon[] = {
	/* Output Mixer */
	{"Output Mixer", "Line Bypass Switch", "Line Input"},
	{"Output Mixer", "Playback Switch", "DAC"},
	{"Output Mixer", "Mic Sidetone Switch", "Mic Input"},

	/* Outputs */
	{"RHPOUT", NULL, "Output Mixer"},
	{"LHPOUT", NULL, "Output Mixer"},
	{"LOUT", NULL, "Output Mixer"},
	{"ROUT", NULL, "Output Mixer"},

	/* Inputs */
	{"Line Input", "NULL", "LLINEIN"},
	{"Line Input", "NULL", "RLINEIN"},

	{"Mic Input", "NULL", "MICIN"},

	/* input mux */
	{"Capture Source", "Line", "Line Input"},
	{"Capture Source", "Mic", "Mic Input"},
	{"ADC", NULL, "Capture Source"},

};

/* AIC23 driver data */
struct aic23 {
	struct regmap *regmap;
	int mclk;
	int requested_adc;
	int requested_dac;
};

/*
 * Common Crystals used
 * 11.2896 Mhz /128 = *88.2k  /192 = 58.8k
 * 12.0000 Mhz /125 = *96k    /136 = 88.235K
 * 12.2880 Mhz /128 = *96k    /192 = 64k
 * 16.9344 Mhz /128 = 132.3k /192 = *88.2k
 * 18.4320 Mhz /128 = 144k   /192 = *96k
 */

/*
 * Normal BOSR 0-256/2 = 128, 1-384/2 = 192
 * USB BOSR 0-250/2 = 125, 1-272/2 = 136
 */
static const int bosr_usb_divisor_table[] = {
	128, 125, 192, 136
};
#define LOWER_GROUP ((1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<6) | (1<<7))
#define UPPER_GROUP ((1<<8) | (1<<9) | (1<<10) | (1<<11)        | (1<<15))
static const unsigned short sr_valid_mask[] = {
	LOWER_GROUP|UPPER_GROUP,	/* Normal, bosr - 0*/
	LOWER_GROUP,			/* Usb, bosr - 0*/
	LOWER_GROUP|UPPER_GROUP,	/* Normal, bosr - 1*/
	UPPER_GROUP,			/* Usb, bosr - 1*/
};
/*
 * Every divisor is a factor of 11*12
 */
#define SR_MULT (11*12)
#define A(x) (SR_MULT/x)
static const unsigned char sr_adc_mult_table[] = {
	A(2), A(2), A(12), A(12),  0, 0, A(3), A(1),
	A(2), A(2), A(11), A(11),  0, 0, 0, A(1)
};
static const unsigned char sr_dac_mult_table[] = {
	A(2), A(12), A(2), A(12),  0, 0, A(3), A(1),
	A(2), A(11), A(2), A(11),  0, 0, 0, A(1)
};

static unsigned get_score(int adc, int adc_l, int adc_h, int need_adc,
		int dac, int dac_l, int dac_h, int need_dac)
{
	if ((adc >= adc_l) && (adc <= adc_h) &&
			(dac >= dac_l) && (dac <= dac_h)) {
		int diff_adc = need_adc - adc;
		int diff_dac = need_dac - dac;
		return abs(diff_adc) + abs(diff_dac);
	}
	return UINT_MAX;
}

static int find_rate(int mclk, u32 need_adc, u32 need_dac)
{
	int i, j;
	int best_i = -1;
	int best_j = -1;
	int best_div = 0;
	unsigned best_score = UINT_MAX;
	int adc_l, adc_h, dac_l, dac_h;

	need_adc *= SR_MULT;
	need_dac *= SR_MULT;
	/*
	 * rates given are +/- 1/32
	 */
	adc_l = need_adc - (need_adc >> 5);
	adc_h = need_adc + (need_adc >> 5);
	dac_l = need_dac - (need_dac >> 5);
	dac_h = need_dac + (need_dac >> 5);
	for (i = 0; i < ARRAY_SIZE(bosr_usb_divisor_table); i++) {
		int base = mclk / bosr_usb_divisor_table[i];
		int mask = sr_valid_mask[i];
		for (j = 0; j < ARRAY_SIZE(sr_adc_mult_table);
				j++, mask >>= 1) {
			int adc;
			int dac;
			int score;
			if ((mask & 1) == 0)
				continue;
			adc = base * sr_adc_mult_table[j];
			dac = base * sr_dac_mult_table[j];
			score = get_score(adc, adc_l, adc_h, need_adc,
					dac, dac_l, dac_h, need_dac);
			if (best_score > score) {
				best_score = score;
				best_i = i;
				best_j = j;
				best_div = 0;
			}
			score = get_score((adc >> 1), adc_l, adc_h, need_adc,
					(dac >> 1), dac_l, dac_h, need_dac);
			/* prefer to have a /2 */
			if ((score != UINT_MAX) && (best_score >= score)) {
				best_score = score;
				best_i = i;
				best_j = j;
				best_div = 1;
			}
		}
	}
	return (best_j << 2) | best_i | (best_div << TLV320AIC23_CLKIN_SHIFT);
}

#ifdef DEBUG
static void get_current_sample_rates(struct snd_soc_codec *codec, int mclk,
		u32 *sample_rate_adc, u32 *sample_rate_dac)
{
	int src = snd_soc_read(codec, TLV320AIC23_SRATE);
	int sr = (src >> 2) & 0x0f;
	int val = (mclk / bosr_usb_divisor_table[src & 3]);
	int adc = (val * sr_adc_mult_table[sr]) / SR_MULT;
	int dac = (val * sr_dac_mult_table[sr]) / SR_MULT;
	if (src & TLV320AIC23_CLKIN_HALF) {
		adc >>= 1;
		dac >>= 1;
	}
	*sample_rate_adc = adc;
	*sample_rate_dac = dac;
}
#endif

static int set_sample_rate_control(struct snd_soc_codec *codec, int mclk,
		u32 sample_rate_adc, u32 sample_rate_dac)
{
	/* Search for the right sample rate */
	int data = find_rate(mclk, sample_rate_adc, sample_rate_dac);
	if (data < 0) {
		printk(KERN_ERR "%s:Invalid rate %u,%u requested\n",
				__func__, sample_rate_adc, sample_rate_dac);
		return -EINVAL;
	}
	snd_soc_write(codec, TLV320AIC23_SRATE, data);
#ifdef DEBUG
	{
		u32 adc, dac;
		get_current_sample_rates(codec, mclk, &adc, &dac);
		printk(KERN_DEBUG "actual samplerate = %u,%u reg=%x\n",
			adc, dac, data);
	}
#endif
	return 0;
}

static int tlv320aic23_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 iface_reg;
	int ret;
	struct aic23 *aic23 = snd_soc_codec_get_drvdata(codec);
	u32 sample_rate_adc = aic23->requested_adc;
	u32 sample_rate_dac = aic23->requested_dac;
	u32 sample_rate = params_rate(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		aic23->requested_dac = sample_rate_dac = sample_rate;
		if (!sample_rate_adc)
			sample_rate_adc = sample_rate;
	} else {
		aic23->requested_adc = sample_rate_adc = sample_rate;
		if (!sample_rate_dac)
			sample_rate_dac = sample_rate;
	}
	ret = set_sample_rate_control(codec, aic23->mclk, sample_rate_adc,
			sample_rate_dac);
	if (ret < 0)
		return ret;

	iface_reg = snd_soc_read(codec, TLV320AIC23_DIGT_FMT) & ~(0x03 << 2);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface_reg |= (0x01 << 2);
		break;
	case 24:
		iface_reg |= (0x02 << 2);
		break;
	case 32:
		iface_reg |= (0x03 << 2);
		break;
	}
	snd_soc_write(codec, TLV320AIC23_DIGT_FMT, iface_reg);

	return 0;
}

static int tlv320aic23_pcm_prepare(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	/* set active */
	snd_soc_write(codec, TLV320AIC23_ACTIVE, 0x0001);

	return 0;
}

static void tlv320aic23_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct aic23 *aic23 = snd_soc_codec_get_drvdata(codec);

	/* deactivate */
	if (!snd_soc_codec_is_active(codec)) {
		udelay(50);
		snd_soc_write(codec, TLV320AIC23_ACTIVE, 0x0);
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aic23->requested_dac = 0;
	else
		aic23->requested_adc = 0;
}

static int tlv320aic23_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg;

	reg = snd_soc_read(codec, TLV320AIC23_DIGT);
	if (mute)
		reg |= TLV320AIC23_DACM_MUTE;

	else
		reg &= ~TLV320AIC23_DACM_MUTE;

	snd_soc_write(codec, TLV320AIC23_DIGT, reg);

	return 0;
}

static int tlv320aic23_set_dai_fmt(struct snd_soc_dai *codec_dai,
				   unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface_reg;

	iface_reg = snd_soc_read(codec, TLV320AIC23_DIGT_FMT) & (~0x03);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface_reg |= TLV320AIC23_MS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface_reg &= ~TLV320AIC23_MS_MASTER;
		break;
	default:
		return -EINVAL;

	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface_reg |= TLV320AIC23_FOR_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface_reg |= TLV320AIC23_LRP_ON;
	case SND_SOC_DAIFMT_DSP_B:
		iface_reg |= TLV320AIC23_FOR_DSP;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface_reg |= TLV320AIC23_FOR_LJUST;
		break;
	default:
		return -EINVAL;

	}

	snd_soc_write(codec, TLV320AIC23_DIGT_FMT, iface_reg);

	return 0;
}

static int tlv320aic23_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				      int clk_id, unsigned int freq, int dir)
{
	struct aic23 *aic23 = snd_soc_dai_get_drvdata(codec_dai);
	aic23->mclk = freq;
	return 0;
}

static int tlv320aic23_set_bias_level(struct snd_soc_codec *codec,
				      enum snd_soc_bias_level level)
{
	u16 reg = snd_soc_read(codec, TLV320AIC23_PWR) & 0x17f;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* vref/mid, osc on, dac unmute */
		reg &= ~(TLV320AIC23_DEVICE_PWR_OFF | TLV320AIC23_OSC_OFF | \
			TLV320AIC23_DAC_OFF);
		snd_soc_write(codec, TLV320AIC23_PWR, reg);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		snd_soc_write(codec, TLV320AIC23_PWR,
			      reg | TLV320AIC23_CLK_OFF);
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		snd_soc_write(codec, TLV320AIC23_ACTIVE, 0x0);
		snd_soc_write(codec, TLV320AIC23_PWR, 0x1ff);
		break;
	}
	return 0;
}

#define AIC23_RATES	SNDRV_PCM_RATE_8000_96000
#define AIC23_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops tlv320aic23_dai_ops = {
	.prepare	= tlv320aic23_pcm_prepare,
	.hw_params	= tlv320aic23_hw_params,
	.shutdown	= tlv320aic23_shutdown,
	.digital_mute	= tlv320aic23_mute,
	.set_fmt	= tlv320aic23_set_dai_fmt,
	.set_sysclk	= tlv320aic23_set_dai_sysclk,
};

static struct snd_soc_dai_driver tlv320aic23_dai = {
	.name = "tlv320aic23-hifi",
	.playback = {
		     .stream_name = "Playback",
		     .channels_min = 2,
		     .channels_max = 2,
		     .rates = AIC23_RATES,
		     .formats = AIC23_FORMATS,},
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 2,
		    .channels_max = 2,
		    .rates = AIC23_RATES,
		    .formats = AIC23_FORMATS,},
	.ops = &tlv320aic23_dai_ops,
};

static int tlv320aic23_resume(struct snd_soc_codec *codec)
{
	struct aic23 *aic23 = snd_soc_codec_get_drvdata(codec);
	regcache_mark_dirty(aic23->regmap);
	regcache_sync(aic23->regmap);

	return 0;
}

static int tlv320aic23_codec_probe(struct snd_soc_codec *codec)
{
	/* Reset codec */
	snd_soc_write(codec, TLV320AIC23_RESET, 0);

	snd_soc_write(codec, TLV320AIC23_DIGT, TLV320AIC23_DEEMP_44K);

	/* Unmute input */
	snd_soc_update_bits(codec, TLV320AIC23_LINVOL,
			    TLV320AIC23_LIM_MUTED, TLV320AIC23_LRS_ENABLED);

	snd_soc_update_bits(codec, TLV320AIC23_RINVOL,
			    TLV320AIC23_LIM_MUTED, TLV320AIC23_LRS_ENABLED);

	snd_soc_update_bits(codec, TLV320AIC23_ANLG,
			    TLV320AIC23_BYPASS_ON | TLV320AIC23_MICM_MUTED,
			    0);

	/* Default output volume */
	snd_soc_write(codec, TLV320AIC23_LCHNVOL,
		      TLV320AIC23_DEFAULT_OUT_VOL & TLV320AIC23_OUT_VOL_MASK);
	snd_soc_write(codec, TLV320AIC23_RCHNVOL,
		      TLV320AIC23_DEFAULT_OUT_VOL & TLV320AIC23_OUT_VOL_MASK);

	snd_soc_write(codec, TLV320AIC23_ACTIVE, 0x1);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_tlv320aic23 = {
	.probe = tlv320aic23_codec_probe,
	.resume = tlv320aic23_resume,
	.set_bias_level = tlv320aic23_set_bias_level,
	.suspend_bias_off = true,

	.controls = tlv320aic23_snd_controls,
	.num_controls = ARRAY_SIZE(tlv320aic23_snd_controls),
	.dapm_widgets = tlv320aic23_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tlv320aic23_dapm_widgets),
	.dapm_routes = tlv320aic23_intercon,
	.num_dapm_routes = ARRAY_SIZE(tlv320aic23_intercon),
};

int tlv320aic23_probe(struct device *dev, struct regmap *regmap)
{
	struct aic23 *aic23;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	aic23 = devm_kzalloc(dev, sizeof(struct aic23), GFP_KERNEL);
	if (aic23 == NULL)
		return -ENOMEM;

	aic23->regmap = regmap;

	dev_set_drvdata(dev, aic23);

	return snd_soc_register_codec(dev, &soc_codec_dev_tlv320aic23,
				      &tlv320aic23_dai, 1);
}
EXPORT_SYMBOL(tlv320aic23_probe);

MODULE_DESCRIPTION("ASoC TLV320AIC23 codec driver");
MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_LICENSE("GPL");
