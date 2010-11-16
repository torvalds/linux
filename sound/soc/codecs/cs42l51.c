/*
 * cs42l51.c
 *
 * ASoC Driver for Cirrus Logic CS42L51 codecs
 *
 * Copyright (c) 2010 Arnaud Patard <apatard@mandriva.com>
 *
 * Based on cs4270.c - Copyright (c) Freescale Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * For now:
 *  - Only I2C is support. Not SPI
 *  - master mode *NOT* supported
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <linux/i2c.h>

#include "cs42l51.h"

enum master_slave_mode {
	MODE_SLAVE,
	MODE_SLAVE_AUTO,
	MODE_MASTER,
};

struct cs42l51_private {
	enum snd_soc_control_type control_type;
	void *control_data;
	unsigned int mclk;
	unsigned int audio_mode;	/* The mode (I2S or left-justified) */
	enum master_slave_mode func;
	u8 reg_cache[CS42L51_NUMREGS];
};

#define CS42L51_FORMATS ( \
		SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_S16_BE  | \
		SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S18_3BE | \
		SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
		SNDRV_PCM_FMTBIT_S24_LE  | SNDRV_PCM_FMTBIT_S24_BE)

static int cs42l51_fill_cache(struct snd_soc_codec *codec)
{
	u8 *cache = codec->reg_cache + 1;
	struct i2c_client *i2c_client = codec->control_data;
	s32 length;

	length = i2c_smbus_read_i2c_block_data(i2c_client,
			CS42L51_FIRSTREG | 0x80, CS42L51_NUMREGS, cache);
	if (length != CS42L51_NUMREGS) {
		dev_err(&i2c_client->dev,
				"I2C read failure, addr=0x%x (ret=%d vs %d)\n",
				i2c_client->addr, length, CS42L51_NUMREGS);
		return -EIO;
	}

	return 0;
}

static int cs42l51_get_chan_mix(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long value = snd_soc_read(codec, CS42L51_PCM_MIXER)&3;

	switch (value) {
	default:
	case 0:
		ucontrol->value.integer.value[0] = 0;
		break;
	/* same value : (L+R)/2 and (R+L)/2 */
	case 1:
	case 2:
		ucontrol->value.integer.value[0] = 1;
		break;
	case 3:
		ucontrol->value.integer.value[0] = 2;
		break;
	}

	return 0;
}

#define CHAN_MIX_NORMAL	0x00
#define CHAN_MIX_BOTH	0x55
#define CHAN_MIX_SWAP	0xFF

static int cs42l51_set_chan_mix(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned char val;

	switch (ucontrol->value.integer.value[0]) {
	default:
	case 0:
		val = CHAN_MIX_NORMAL;
		break;
	case 1:
		val = CHAN_MIX_BOTH;
		break;
	case 2:
		val = CHAN_MIX_SWAP;
		break;
	}

	snd_soc_write(codec, CS42L51_PCM_MIXER, val);

	return 1;
}

static const DECLARE_TLV_DB_SCALE(adc_pcm_tlv, -5150, 50, 0);
static const DECLARE_TLV_DB_SCALE(tone_tlv, -1050, 150, 0);
/* This is a lie. after -102 db, it stays at -102 */
/* maybe a range would be better */
static const DECLARE_TLV_DB_SCALE(aout_tlv, -11550, 50, 0);

static const DECLARE_TLV_DB_SCALE(boost_tlv, 1600, 1600, 0);
static const char *chan_mix[] = {
	"L R",
	"L+R",
	"R L",
};

static const struct soc_enum cs42l51_chan_mix =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(chan_mix), chan_mix);

static const struct snd_kcontrol_new cs42l51_snd_controls[] = {
	SOC_DOUBLE_R_SX_TLV("PCM Playback Volume",
			CS42L51_PCMA_VOL, CS42L51_PCMB_VOL,
			7, 0xffffff99, 0x18, adc_pcm_tlv),
	SOC_DOUBLE_R("PCM Playback Switch",
			CS42L51_PCMA_VOL, CS42L51_PCMB_VOL, 7, 1, 1),
	SOC_DOUBLE_R_SX_TLV("Analog Playback Volume",
			CS42L51_AOUTA_VOL, CS42L51_AOUTB_VOL,
			8, 0xffffff19, 0x18, aout_tlv),
	SOC_DOUBLE_R_SX_TLV("ADC Mixer Volume",
			CS42L51_ADCA_VOL, CS42L51_ADCB_VOL,
			7, 0xffffff99, 0x18, adc_pcm_tlv),
	SOC_DOUBLE_R("ADC Mixer Switch",
			CS42L51_ADCA_VOL, CS42L51_ADCB_VOL, 7, 1, 1),
	SOC_SINGLE("Playback Deemphasis Switch", CS42L51_DAC_CTL, 3, 1, 0),
	SOC_SINGLE("Auto-Mute Switch", CS42L51_DAC_CTL, 2, 1, 0),
	SOC_SINGLE("Soft Ramp Switch", CS42L51_DAC_CTL, 1, 1, 0),
	SOC_SINGLE("Zero Cross Switch", CS42L51_DAC_CTL, 0, 0, 0),
	SOC_DOUBLE_TLV("Mic Boost Volume",
			CS42L51_MIC_CTL, 0, 1, 1, 0, boost_tlv),
	SOC_SINGLE_TLV("Bass Volume", CS42L51_TONE_CTL, 0, 0xf, 1, tone_tlv),
	SOC_SINGLE_TLV("Treble Volume", CS42L51_TONE_CTL, 4, 0xf, 1, tone_tlv),
	SOC_ENUM_EXT("PCM channel mixer",
			cs42l51_chan_mix,
			cs42l51_get_chan_mix, cs42l51_set_chan_mix),
};

/*
 * to power down, one must:
 * 1.) Enable the PDN bit
 * 2.) enable power-down for the select channels
 * 3.) disable the PDN bit.
 */
static int cs42l51_pdn_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	unsigned long value;

	value = snd_soc_read(w->codec, CS42L51_POWER_CTL1);
	value &= ~CS42L51_POWER_CTL1_PDN;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		value |= CS42L51_POWER_CTL1_PDN;
		break;
	default:
	case SND_SOC_DAPM_POST_PMD:
		break;
	}
	snd_soc_update_bits(w->codec, CS42L51_POWER_CTL1,
		CS42L51_POWER_CTL1_PDN, value);

	return 0;
}

static const char *cs42l51_dac_names[] = {"Direct PCM",
	"DSP PCM", "ADC"};
static const struct soc_enum cs42l51_dac_mux_enum =
	SOC_ENUM_SINGLE(CS42L51_DAC_CTL, 6, 3, cs42l51_dac_names);
static const struct snd_kcontrol_new cs42l51_dac_mux_controls =
	SOC_DAPM_ENUM("Route", cs42l51_dac_mux_enum);

static const char *cs42l51_adcl_names[] = {"AIN1 Left", "AIN2 Left",
	"MIC Left", "MIC+preamp Left"};
static const struct soc_enum cs42l51_adcl_mux_enum =
	SOC_ENUM_SINGLE(CS42L51_ADC_INPUT, 4, 4, cs42l51_adcl_names);
static const struct snd_kcontrol_new cs42l51_adcl_mux_controls =
	SOC_DAPM_ENUM("Route", cs42l51_adcl_mux_enum);

static const char *cs42l51_adcr_names[] = {"AIN1 Right", "AIN2 Right",
	"MIC Right", "MIC+preamp Right"};
static const struct soc_enum cs42l51_adcr_mux_enum =
	SOC_ENUM_SINGLE(CS42L51_ADC_INPUT, 6, 4, cs42l51_adcr_names);
static const struct snd_kcontrol_new cs42l51_adcr_mux_controls =
	SOC_DAPM_ENUM("Route", cs42l51_adcr_mux_enum);

static const struct snd_soc_dapm_widget cs42l51_dapm_widgets[] = {
	SND_SOC_DAPM_MICBIAS("Mic Bias", CS42L51_MIC_POWER_CTL, 1, 1),
	SND_SOC_DAPM_PGA_E("Left PGA", CS42L51_POWER_CTL1, 3, 1, NULL, 0,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_PGA_E("Right PGA", CS42L51_POWER_CTL1, 4, 1, NULL, 0,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_ADC_E("Left ADC", "Left HiFi Capture",
		CS42L51_POWER_CTL1, 1, 1,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_ADC_E("Right ADC", "Right HiFi Capture",
		CS42L51_POWER_CTL1, 2, 1,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_DAC_E("Left DAC", "Left HiFi Playback",
		CS42L51_POWER_CTL1, 5, 1,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),
	SND_SOC_DAPM_DAC_E("Right DAC", "Right HiFi Playback",
		CS42L51_POWER_CTL1, 6, 1,
		cs42l51_pdn_event, SND_SOC_DAPM_PRE_POST_PMD),

	/* analog/mic */
	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("MICL"),
	SND_SOC_DAPM_INPUT("MICR"),

	SND_SOC_DAPM_MIXER("Mic Preamp Left",
		CS42L51_MIC_POWER_CTL, 2, 1, NULL, 0),
	SND_SOC_DAPM_MIXER("Mic Preamp Right",
		CS42L51_MIC_POWER_CTL, 3, 1, NULL, 0),

	/* HP */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),

	/* mux */
	SND_SOC_DAPM_MUX("DAC Mux", SND_SOC_NOPM, 0, 0,
		&cs42l51_dac_mux_controls),
	SND_SOC_DAPM_MUX("PGA-ADC Mux Left", SND_SOC_NOPM, 0, 0,
		&cs42l51_adcl_mux_controls),
	SND_SOC_DAPM_MUX("PGA-ADC Mux Right", SND_SOC_NOPM, 0, 0,
		&cs42l51_adcr_mux_controls),
};

static const struct snd_soc_dapm_route cs42l51_routes[] = {
	{"HPL", NULL, "Left DAC"},
	{"HPR", NULL, "Right DAC"},

	{"Left ADC", NULL, "Left PGA"},
	{"Right ADC", NULL, "Right PGA"},

	{"Mic Preamp Left",  NULL,  "MICL"},
	{"Mic Preamp Right", NULL,  "MICR"},

	{"PGA-ADC Mux Left",  "AIN1 Left",        "AIN1L" },
	{"PGA-ADC Mux Left",  "AIN2 Left",        "AIN2L" },
	{"PGA-ADC Mux Left",  "MIC Left",         "MICL"  },
	{"PGA-ADC Mux Left",  "MIC+preamp Left",  "Mic Preamp Left" },
	{"PGA-ADC Mux Right", "AIN1 Right",       "AIN1R" },
	{"PGA-ADC Mux Right", "AIN2 Right",       "AIN2R" },
	{"PGA-ADC Mux Right", "MIC Right",        "MICR" },
	{"PGA-ADC Mux Right", "MIC+preamp Right", "Mic Preamp Right" },

	{"Left PGA", NULL, "PGA-ADC Mux Left"},
	{"Right PGA", NULL, "PGA-ADC Mux Right"},
};

static int cs42l51_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42l51_private *cs42l51 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		cs42l51->audio_mode = format & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(codec->dev, "invalid DAI format\n");
		ret = -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		cs42l51->func = MODE_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		cs42l51->func = MODE_SLAVE_AUTO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct cs42l51_ratios {
	unsigned int ratio;
	unsigned char speed_mode;
	unsigned char mclk;
};

static struct cs42l51_ratios slave_ratios[] = {
	{  512, CS42L51_QSM_MODE, 0 }, {  768, CS42L51_QSM_MODE, 0 },
	{ 1024, CS42L51_QSM_MODE, 0 }, { 1536, CS42L51_QSM_MODE, 0 },
	{ 2048, CS42L51_QSM_MODE, 0 }, { 3072, CS42L51_QSM_MODE, 0 },
	{  256, CS42L51_HSM_MODE, 0 }, {  384, CS42L51_HSM_MODE, 0 },
	{  512, CS42L51_HSM_MODE, 0 }, {  768, CS42L51_HSM_MODE, 0 },
	{ 1024, CS42L51_HSM_MODE, 0 }, { 1536, CS42L51_HSM_MODE, 0 },
	{  128, CS42L51_SSM_MODE, 0 }, {  192, CS42L51_SSM_MODE, 0 },
	{  256, CS42L51_SSM_MODE, 0 }, {  384, CS42L51_SSM_MODE, 0 },
	{  512, CS42L51_SSM_MODE, 0 }, {  768, CS42L51_SSM_MODE, 0 },
	{  128, CS42L51_DSM_MODE, 0 }, {  192, CS42L51_DSM_MODE, 0 },
	{  256, CS42L51_DSM_MODE, 0 }, {  384, CS42L51_DSM_MODE, 0 },
};

static struct cs42l51_ratios slave_auto_ratios[] = {
	{ 1024, CS42L51_QSM_MODE, 0 }, { 1536, CS42L51_QSM_MODE, 0 },
	{ 2048, CS42L51_QSM_MODE, 1 }, { 3072, CS42L51_QSM_MODE, 1 },
	{  512, CS42L51_HSM_MODE, 0 }, {  768, CS42L51_HSM_MODE, 0 },
	{ 1024, CS42L51_HSM_MODE, 1 }, { 1536, CS42L51_HSM_MODE, 1 },
	{  256, CS42L51_SSM_MODE, 0 }, {  384, CS42L51_SSM_MODE, 0 },
	{  512, CS42L51_SSM_MODE, 1 }, {  768, CS42L51_SSM_MODE, 1 },
	{  128, CS42L51_DSM_MODE, 0 }, {  192, CS42L51_DSM_MODE, 0 },
	{  256, CS42L51_DSM_MODE, 1 }, {  384, CS42L51_DSM_MODE, 1 },
};

static int cs42l51_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42l51_private *cs42l51 = snd_soc_codec_get_drvdata(codec);

	cs42l51->mclk = freq;
	return 0;
}

static int cs42l51_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct cs42l51_private *cs42l51 = snd_soc_codec_get_drvdata(codec);
	int ret;
	unsigned int i;
	unsigned int rate;
	unsigned int ratio;
	struct cs42l51_ratios *ratios = NULL;
	int nr_ratios = 0;
	int intf_ctl, power_ctl, fmt;

	switch (cs42l51->func) {
	case MODE_MASTER:
		return -EINVAL;
	case MODE_SLAVE:
		ratios = slave_ratios;
		nr_ratios = ARRAY_SIZE(slave_ratios);
		break;
	case MODE_SLAVE_AUTO:
		ratios = slave_auto_ratios;
		nr_ratios = ARRAY_SIZE(slave_auto_ratios);
		break;
	}

	/* Figure out which MCLK/LRCK ratio to use */
	rate = params_rate(params);     /* Sampling rate, in Hz */
	ratio = cs42l51->mclk / rate;    /* MCLK/LRCK ratio */
	for (i = 0; i < nr_ratios; i++) {
		if (ratios[i].ratio == ratio)
			break;
	}

	if (i == nr_ratios) {
		/* We did not find a matching ratio */
		dev_err(codec->dev, "could not find matching ratio\n");
		return -EINVAL;
	}

	intf_ctl = snd_soc_read(codec, CS42L51_INTF_CTL);
	power_ctl = snd_soc_read(codec, CS42L51_MIC_POWER_CTL);

	intf_ctl &= ~(CS42L51_INTF_CTL_MASTER | CS42L51_INTF_CTL_ADC_I2S
			| CS42L51_INTF_CTL_DAC_FORMAT(7));
	power_ctl &= ~(CS42L51_MIC_POWER_CTL_SPEED(3)
			| CS42L51_MIC_POWER_CTL_MCLK_DIV2);

	switch (cs42l51->func) {
	case MODE_MASTER:
		intf_ctl |= CS42L51_INTF_CTL_MASTER;
		power_ctl |= CS42L51_MIC_POWER_CTL_SPEED(ratios[i].speed_mode);
		break;
	case MODE_SLAVE:
		power_ctl |= CS42L51_MIC_POWER_CTL_SPEED(ratios[i].speed_mode);
		break;
	case MODE_SLAVE_AUTO:
		power_ctl |= CS42L51_MIC_POWER_CTL_AUTO;
		break;
	}

	switch (cs42l51->audio_mode) {
	case SND_SOC_DAIFMT_I2S:
		intf_ctl |= CS42L51_INTF_CTL_ADC_I2S;
		intf_ctl |= CS42L51_INTF_CTL_DAC_FORMAT(CS42L51_DAC_DIF_I2S);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		intf_ctl |= CS42L51_INTF_CTL_DAC_FORMAT(CS42L51_DAC_DIF_LJ24);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
		case SNDRV_PCM_FORMAT_S16_BE:
			fmt = CS42L51_DAC_DIF_RJ16;
			break;
		case SNDRV_PCM_FORMAT_S18_3LE:
		case SNDRV_PCM_FORMAT_S18_3BE:
			fmt = CS42L51_DAC_DIF_RJ18;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
		case SNDRV_PCM_FORMAT_S20_3BE:
			fmt = CS42L51_DAC_DIF_RJ20;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
		case SNDRV_PCM_FORMAT_S24_BE:
			fmt = CS42L51_DAC_DIF_RJ24;
			break;
		default:
			dev_err(codec->dev, "unknown format\n");
			return -EINVAL;
		}
		intf_ctl |= CS42L51_INTF_CTL_DAC_FORMAT(fmt);
		break;
	default:
		dev_err(codec->dev, "unknown format\n");
		return -EINVAL;
	}

	if (ratios[i].mclk)
		power_ctl |= CS42L51_MIC_POWER_CTL_MCLK_DIV2;

	ret = snd_soc_write(codec, CS42L51_INTF_CTL, intf_ctl);
	if (ret < 0)
		return ret;

	ret = snd_soc_write(codec, CS42L51_MIC_POWER_CTL, power_ctl);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs42l51_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int reg;
	int mask = CS42L51_DAC_OUT_CTL_DACA_MUTE|CS42L51_DAC_OUT_CTL_DACB_MUTE;

	reg = snd_soc_read(codec, CS42L51_DAC_OUT_CTL);

	if (mute)
		reg |= mask;
	else
		reg &= ~mask;

	return snd_soc_write(codec, CS42L51_DAC_OUT_CTL, reg);
}

static struct snd_soc_dai_ops cs42l51_dai_ops = {
	.hw_params      = cs42l51_hw_params,
	.set_sysclk     = cs42l51_set_dai_sysclk,
	.set_fmt        = cs42l51_set_dai_fmt,
	.digital_mute   = cs42l51_dai_mute,
};

static struct snd_soc_dai_driver cs42l51_dai = {
	.name = "cs42l51-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = CS42L51_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = CS42L51_FORMATS,
	},
	.ops = &cs42l51_dai_ops,
};

static int cs42l51_probe(struct snd_soc_codec *codec)
{
	struct cs42l51_private *cs42l51 = snd_soc_codec_get_drvdata(codec);
	int ret, reg;

	codec->control_data = cs42l51->control_data;

	ret = cs42l51_fill_cache(codec);
	if (ret < 0) {
		dev_err(codec->dev, "failed to fill register cache\n");
		return ret;
	}

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, cs42l51->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/*
	 * DAC configuration
	 * - Use signal processor
	 * - auto mute
	 * - vol changes immediate
	 * - no de-emphasize
	 */
	reg = CS42L51_DAC_CTL_DATA_SEL(1)
		| CS42L51_DAC_CTL_AMUTE | CS42L51_DAC_CTL_DACSZ(0);
	ret = snd_soc_write(codec, CS42L51_DAC_CTL, reg);
	if (ret < 0)
		return ret;

	snd_soc_add_controls(codec, cs42l51_snd_controls,
		ARRAY_SIZE(cs42l51_snd_controls));
	snd_soc_dapm_new_controls(codec, cs42l51_dapm_widgets,
		ARRAY_SIZE(cs42l51_dapm_widgets));
	snd_soc_dapm_add_routes(codec, cs42l51_routes,
		ARRAY_SIZE(cs42l51_routes));

	return 0;
}

static struct snd_soc_codec_driver soc_codec_device_cs42l51 = {
	.probe =	cs42l51_probe,
	.reg_cache_size = CS42L51_NUMREGS,
	.reg_word_size = sizeof(u8),
};

static int cs42l51_i2c_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id)
{
	struct cs42l51_private *cs42l51;
	int ret;

	/* Verify that we have a CS42L51 */
	ret = i2c_smbus_read_byte_data(i2c_client, CS42L51_CHIP_REV_ID);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "failed to read I2C\n");
		goto error;
	}

	if ((ret != CS42L51_MK_CHIP_REV(CS42L51_CHIP_ID, CS42L51_CHIP_REV_A)) &&
	    (ret != CS42L51_MK_CHIP_REV(CS42L51_CHIP_ID, CS42L51_CHIP_REV_B))) {
		dev_err(&i2c_client->dev, "Invalid chip id\n");
		ret = -ENODEV;
		goto error;
	}

	dev_info(&i2c_client->dev, "found device cs42l51 rev %d\n",
				ret & 7);

	cs42l51 = kzalloc(sizeof(struct cs42l51_private), GFP_KERNEL);
	if (!cs42l51) {
		dev_err(&i2c_client->dev, "could not allocate codec\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c_client, cs42l51);
	cs42l51->control_data = i2c_client;
	cs42l51->control_type = SND_SOC_I2C;

	ret =  snd_soc_register_codec(&i2c_client->dev,
			&soc_codec_device_cs42l51, &cs42l51_dai, 1);
	if (ret < 0)
		kfree(cs42l51);
error:
	return ret;
}

static int cs42l51_i2c_remove(struct i2c_client *client)
{
	struct cs42l51_private *cs42l51 = i2c_get_clientdata(client);

	snd_soc_unregister_codec(&client->dev);
	kfree(cs42l51);
	return 0;
}

static const struct i2c_device_id cs42l51_id[] = {
	{"cs42l51", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs42l51_id);

static struct i2c_driver cs42l51_i2c_driver = {
	.driver = {
		.name = "cs42l51-codec",
		.owner = THIS_MODULE,
	},
	.id_table = cs42l51_id,
	.probe = cs42l51_i2c_probe,
	.remove = cs42l51_i2c_remove,
};

static int __init cs42l51_init(void)
{
	int ret;

	ret = i2c_add_driver(&cs42l51_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "%s: can't add i2c driver\n", __func__);
		return ret;
	}
	return 0;
}
module_init(cs42l51_init);

static void __exit cs42l51_exit(void)
{
	i2c_del_driver(&cs42l51_i2c_driver);
}
module_exit(cs42l51_exit);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("Cirrus Logic CS42L51 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
