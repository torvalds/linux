/* Atmel PDMIC driver
 *
 * Copyright (C) 2015 Atmel
 *
 * Author: Songjun Wu <songjun.wu@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or later
 * as published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "atmel-pdmic.h"

struct atmel_pdmic_pdata {
	u32 mic_min_freq;
	u32 mic_max_freq;
	s32 mic_offset;
	const char *card_name;
};

struct atmel_pdmic {
	dma_addr_t phy_base;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *gclk;
	int irq;
	struct snd_pcm_substream *substream;
	const struct atmel_pdmic_pdata *pdata;
};

static const struct of_device_id atmel_pdmic_of_match[] = {
	{
		.compatible = "atmel,sama5d2-pdmic",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_pdmic_of_match);

#define PDMIC_OFFSET_MAX_VAL	S16_MAX
#define PDMIC_OFFSET_MIN_VAL	S16_MIN

static struct atmel_pdmic_pdata *atmel_pdmic_dt_init(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct atmel_pdmic_pdata *pdata;

	if (!np) {
		dev_err(dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	if (of_property_read_string(np, "atmel,model", &pdata->card_name))
		pdata->card_name = "PDMIC";

	if (of_property_read_u32(np, "atmel,mic-min-freq",
				 &pdata->mic_min_freq)) {
		dev_err(dev, "failed to get mic-min-freq\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "atmel,mic-max-freq",
				 &pdata->mic_max_freq)) {
		dev_err(dev, "failed to get mic-max-freq\n");
		return ERR_PTR(-EINVAL);
	}

	if (pdata->mic_max_freq < pdata->mic_min_freq) {
		dev_err(dev,
			"mic-max-freq should not less than mic-min-freq\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_s32(np, "atmel,mic-offset", &pdata->mic_offset))
		pdata->mic_offset = 0;

	if (pdata->mic_offset > PDMIC_OFFSET_MAX_VAL) {
		dev_warn(dev,
			 "mic-offset value %d is larger than the max value %d, the max value is specified\n",
			 pdata->mic_offset, PDMIC_OFFSET_MAX_VAL);
		pdata->mic_offset = PDMIC_OFFSET_MAX_VAL;
	} else if (pdata->mic_offset < PDMIC_OFFSET_MIN_VAL) {
		dev_warn(dev,
			 "mic-offset value %d is less than the min value %d, the min value is specified\n",
			 pdata->mic_offset, PDMIC_OFFSET_MIN_VAL);
		pdata->mic_offset = PDMIC_OFFSET_MIN_VAL;
	}

	return pdata;
}

/* cpu dai component */
static int atmel_pdmic_cpu_dai_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	ret = clk_prepare_enable(dd->gclk);
	if (ret)
		return ret;

	ret =  clk_prepare_enable(dd->pclk);
	if (ret)
		return ret;

	/* Clear all bits in the Control Register(PDMIC_CR) */
	regmap_write(dd->regmap, PDMIC_CR, 0);

	dd->substream = substream;

	/* Enable the overrun error interrupt */
	regmap_write(dd->regmap, PDMIC_IER, PDMIC_IER_OVRE);

	return 0;
}

static void atmel_pdmic_cpu_dai_shutdown(struct snd_pcm_substream *substream,
					struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(rtd->card);

	/* Disable the overrun error interrupt */
	regmap_write(dd->regmap, PDMIC_IDR, PDMIC_IDR_OVRE);

	clk_disable_unprepare(dd->gclk);
	clk_disable_unprepare(dd->pclk);
}

static int atmel_pdmic_cpu_dai_prepare(struct snd_pcm_substream *substream,
					struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(rtd->card);
	u32 val;

	/* Clean the PDMIC Converted Data Register */
	return regmap_read(dd->regmap, PDMIC_CDR, &val);
}

static const struct snd_soc_dai_ops atmel_pdmic_cpu_dai_ops = {
	.startup	= atmel_pdmic_cpu_dai_startup,
	.shutdown	= atmel_pdmic_cpu_dai_shutdown,
	.prepare	= atmel_pdmic_cpu_dai_prepare,
};

#define ATMEL_PDMIC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver atmel_pdmic_cpu_dai = {
	.capture = {
		.channels_min	= 1,
		.channels_max	= 1,
		.rates		= SNDRV_PCM_RATE_KNOT,
		.formats	= ATMEL_PDMIC_FORMATS,},
	.ops = &atmel_pdmic_cpu_dai_ops,
};

static const struct snd_soc_component_driver atmel_pdmic_cpu_dai_component = {
	.name = "atmel-pdmic",
};

/* platform */
#define ATMEL_PDMIC_MAX_BUF_SIZE  (64 * 1024)
#define ATMEL_PDMIC_PREALLOC_BUF_SIZE  ATMEL_PDMIC_MAX_BUF_SIZE

static const struct snd_pcm_hardware atmel_pdmic_hw = {
	.info			= SNDRV_PCM_INFO_MMAP
				| SNDRV_PCM_INFO_MMAP_VALID
				| SNDRV_PCM_INFO_INTERLEAVED
				| SNDRV_PCM_INFO_RESUME
				| SNDRV_PCM_INFO_PAUSE,
	.formats		= ATMEL_PDMIC_FORMATS,
	.buffer_bytes_max	= ATMEL_PDMIC_MAX_BUF_SIZE,
	.period_bytes_min	= 256,
	.period_bytes_max	= 32 * 1024,
	.periods_min		= 2,
	.periods_max		= 256,
};

static int
atmel_pdmic_platform_configure_dma(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	ret = snd_hwparams_to_dma_slave_config(substream, params,
					       slave_config);
	if (ret) {
		dev_err(rtd->platform->dev,
			"hw params to dma slave configure failed\n");
		return ret;
	}

	slave_config->src_addr		= dd->phy_base + PDMIC_CDR;
	slave_config->src_maxburst	= 1;
	slave_config->dst_maxburst	= 1;

	return 0;
}

static const struct snd_dmaengine_pcm_config
atmel_pdmic_dmaengine_pcm_config = {
	.prepare_slave_config	= atmel_pdmic_platform_configure_dma,
	.pcm_hardware		= &atmel_pdmic_hw,
	.prealloc_buffer_size	= ATMEL_PDMIC_PREALLOC_BUF_SIZE,
};

/* codec */
/* Mic Gain = dgain * 2^(-scale) */
struct mic_gain {
	unsigned int dgain;
	unsigned int scale;
};

/* range from -90 dB to 90 dB */
static const struct mic_gain mic_gain_table[] = {
{    1, 15}, {    1, 14},                           /* -90, -84 dB */
{    3, 15}, {    1, 13}, {    3, 14}, {    1, 12}, /* -81, -78, -75, -72 dB */
{    5, 14}, {   13, 15},                           /* -70, -68 dB */
{    9, 14}, {   21, 15}, {   23, 15}, {   13, 14}, /* -65 ~ -62 dB */
{   29, 15}, {   33, 15}, {   37, 15}, {   41, 15}, /* -61 ~ -58 dB */
{   23, 14}, {   13, 13}, {   58, 15}, {   65, 15}, /* -57 ~ -54 dB */
{   73, 15}, {   41, 14}, {   23, 13}, {   13, 12}, /* -53 ~ -50 dB */
{   29, 13}, {   65, 14}, {   73, 14}, {   41, 13}, /* -49 ~ -46 dB */
{   23, 12}, {  207, 15}, {   29, 12}, {   65, 13}, /* -45 ~ -42 dB */
{   73, 13}, {   41, 12}, {   23, 11}, {  413, 15}, /* -41 ~ -38 dB */
{  463, 15}, {  519, 15}, {  583, 15}, {  327, 14}, /* -37 ~ -34 dB */
{  367, 14}, {  823, 15}, {  231, 13}, { 1036, 15}, /* -33 ~ -30 dB */
{ 1163, 15}, { 1305, 15}, {  183, 12}, { 1642, 15}, /* -29 ~ -26 dB */
{ 1843, 15}, { 2068, 15}, {  145, 11}, { 2603, 15}, /* -25 ~ -22 dB */
{  365, 12}, { 3277, 15}, { 3677, 15}, { 4125, 15}, /* -21 ~ -18 dB */
{ 4629, 15}, { 5193, 15}, { 5827, 15}, { 3269, 14}, /* -17 ~ -14 dB */
{  917, 12}, { 8231, 15}, { 9235, 15}, { 5181, 14}, /* -13 ~ -10 dB */
{11627, 15}, {13045, 15}, {14637, 15}, {16423, 15}, /*  -9 ~ -6 dB */
{18427, 15}, {20675, 15}, { 5799, 13}, {26029, 15}, /*  -5 ~ -2 dB */
{ 7301, 13}, {    1,  0}, {18383, 14}, {10313, 13}, /*  -1 ~ 2 dB */
{23143, 14}, {25967, 14}, {29135, 14}, {16345, 13}, /*   3 ~ 6 dB */
{ 4585, 11}, {20577, 13}, { 1443,  9}, {25905, 13}, /*   7 ~ 10 dB */
{14533, 12}, { 8153, 11}, { 2287,  9}, {20529, 12}, /*  11 ~ 14 dB */
{11517, 11}, { 6461, 10}, {28997, 12}, { 4067,  9}, /*  15 ~ 18 dB */
{18253, 11}, {   10,  0}, {22979, 11}, {25783, 11}, /*  19 ~ 22 dB */
{28929, 11}, {32459, 11}, { 9105,  9}, {20431, 10}, /*  23 ~ 26 dB */
{22925, 10}, {12861,  9}, { 7215,  8}, {16191,  9}, /*  27 ~ 30 dB */
{ 9083,  8}, {20383,  9}, {11435,  8}, { 6145,  7}, /*  31 ~ 34 dB */
{ 3599,  6}, {32305,  9}, {18123,  8}, {20335,  8}, /*  35 ~ 38 dB */
{  713,  3}, {  100,  0}, { 7181,  6}, { 8057,  6}, /*  39 ~ 42 dB */
{  565,  2}, {20287,  7}, {11381,  6}, {25539,  7}, /*  43 ~ 46 dB */
{ 1791,  3}, { 4019,  4}, { 9019,  5}, {20239,  6}, /*  47 ~ 50 dB */
{ 5677,  4}, {25479,  6}, { 7147,  4}, { 8019,  4}, /*  51 ~ 54 dB */
{17995,  5}, {20191,  5}, {11327,  4}, {12709,  4}, /*  55 ~ 58 dB */
{ 3565,  2}, { 1000,  0}, { 1122,  0}, { 1259,  0}, /*  59 ~ 62 dB */
{ 2825,  1}, {12679,  3}, { 7113,  2}, { 7981,  2}, /*  63 ~ 66 dB */
{ 8955,  2}, {20095,  3}, {22547,  3}, {12649,  2}, /*  67 ~ 70 dB */
{28385,  3}, { 3981,  0}, {17867,  2}, {20047,  2}, /*  71 ~ 74 dB */
{11247,  1}, {12619,  1}, {14159,  1}, {31773,  2}, /*  75 ~ 78 dB */
{17825,  1}, {10000,  0}, {11220,  0}, {12589,  0}, /*  79 ~ 82 dB */
{28251,  1}, {15849,  0}, {17783,  0}, {19953,  0}, /*  83 ~ 86 dB */
{22387,  0}, {25119,  0}, {28184,  0}, {31623,  0}, /*  87 ~ 90 dB */
};

static const DECLARE_TLV_DB_RANGE(mic_gain_tlv,
	0, 1, TLV_DB_SCALE_ITEM(-9000, 600, 0),
	2, 5, TLV_DB_SCALE_ITEM(-8100, 300, 0),
	6, 7, TLV_DB_SCALE_ITEM(-7000, 200, 0),
	8, ARRAY_SIZE(mic_gain_table)-1, TLV_DB_SCALE_ITEM(-6500, 100, 0),
);

int pdmic_get_mic_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	unsigned int dgain_val, scale_val;
	int i;

	dgain_val = (snd_soc_read(codec, PDMIC_DSPR1) & PDMIC_DSPR1_DGAIN_MASK)
		    >> PDMIC_DSPR1_DGAIN_SHIFT;

	scale_val = (snd_soc_read(codec, PDMIC_DSPR0) & PDMIC_DSPR0_SCALE_MASK)
		    >> PDMIC_DSPR0_SCALE_SHIFT;

	for (i = 0; i < ARRAY_SIZE(mic_gain_table); i++) {
		if ((mic_gain_table[i].dgain == dgain_val) &&
		    (mic_gain_table[i].scale == scale_val))
			ucontrol->value.integer.value[0] = i;
	}

	return 0;
}

static int pdmic_put_mic_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	int max = mc->max;
	unsigned int val;
	int ret;

	val = ucontrol->value.integer.value[0];

	if (val > max)
		return -EINVAL;

	ret = snd_soc_update_bits(codec, PDMIC_DSPR1, PDMIC_DSPR1_DGAIN_MASK,
			 mic_gain_table[val].dgain << PDMIC_DSPR1_DGAIN_SHIFT);
	if (ret < 0)
		return ret;

	ret = snd_soc_update_bits(codec, PDMIC_DSPR0, PDMIC_DSPR0_SCALE_MASK,
			 mic_gain_table[val].scale << PDMIC_DSPR0_SCALE_SHIFT);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_kcontrol_new atmel_pdmic_snd_controls[] = {
SOC_SINGLE_EXT_TLV("Mic Capture Volume", PDMIC_DSPR1, PDMIC_DSPR1_DGAIN_SHIFT,
		   ARRAY_SIZE(mic_gain_table)-1, 0,
		   pdmic_get_mic_volsw, pdmic_put_mic_volsw, mic_gain_tlv),

SOC_SINGLE("High Pass Filter Switch", PDMIC_DSPR0,
	   PDMIC_DSPR0_HPFBYP_SHIFT, 1, 1),

SOC_SINGLE("SINCC Filter Switch", PDMIC_DSPR0, PDMIC_DSPR0_SINBYP_SHIFT, 1, 1),
};

static int atmel_pdmic_codec_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_card *card = snd_soc_codec_get_drvdata(codec);
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(card);

	snd_soc_update_bits(codec, PDMIC_DSPR1, PDMIC_DSPR1_OFFSET_MASK,
		     (u32)(dd->pdata->mic_offset << PDMIC_DSPR1_OFFSET_SHIFT));

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_pdmic = {
	.probe		= atmel_pdmic_codec_probe,
	.component_driver = {
		.controls		= atmel_pdmic_snd_controls,
		.num_controls		= ARRAY_SIZE(atmel_pdmic_snd_controls),
	},
};

/* codec dai component */
#define PDMIC_MR_PRESCAL_MAX_VAL 127

static int
atmel_pdmic_codec_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *codec_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int rate_min = substream->runtime->hw.rate_min;
	unsigned int rate_max = substream->runtime->hw.rate_max;
	int fs = params_rate(params);
	int bits = params_width(params);
	unsigned long pclk_rate, gclk_rate;
	unsigned int f_pdmic;
	u32 mr_val, dspr0_val, pclk_prescal, gclk_prescal;

	if (params_channels(params) != 1) {
		dev_err(codec->dev,
			"only supports one channel\n");
		return -EINVAL;
	}

	if ((fs < rate_min) || (fs > rate_max)) {
		dev_err(codec->dev,
			"sample rate is %dHz, min rate is %dHz, max rate is %dHz\n",
			fs, rate_min, rate_max);

		return -EINVAL;
	}

	switch (bits) {
	case 16:
		dspr0_val = (PDMIC_DSPR0_SIZE_16_BITS
			     << PDMIC_DSPR0_SIZE_SHIFT);
		break;
	case 32:
		dspr0_val = (PDMIC_DSPR0_SIZE_32_BITS
			     << PDMIC_DSPR0_SIZE_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	if ((fs << 7) > (rate_max << 6)) {
		f_pdmic = fs << 6;
		dspr0_val |= PDMIC_DSPR0_OSR_64 << PDMIC_DSPR0_OSR_SHIFT;
	} else {
		f_pdmic = fs << 7;
		dspr0_val |= PDMIC_DSPR0_OSR_128 << PDMIC_DSPR0_OSR_SHIFT;
	}

	pclk_rate = clk_get_rate(dd->pclk);
	gclk_rate = clk_get_rate(dd->gclk);

	/* PRESCAL = SELCK/(2*f_pdmic) - 1*/
	pclk_prescal = (u32)(pclk_rate/(f_pdmic << 1)) - 1;
	gclk_prescal = (u32)(gclk_rate/(f_pdmic << 1)) - 1;

	if ((pclk_prescal > PDMIC_MR_PRESCAL_MAX_VAL) ||
	    (gclk_rate/((gclk_prescal + 1) << 1) <
	     pclk_rate/((pclk_prescal + 1) << 1))) {
		mr_val = gclk_prescal << PDMIC_MR_PRESCAL_SHIFT;
		mr_val |= PDMIC_MR_CLKS_GCK << PDMIC_MR_CLKS_SHIFT;
	} else {
		mr_val = pclk_prescal << PDMIC_MR_PRESCAL_SHIFT;
		mr_val |= PDMIC_MR_CLKS_PCK << PDMIC_MR_CLKS_SHIFT;
	}

	snd_soc_update_bits(codec, PDMIC_MR,
		PDMIC_MR_PRESCAL_MASK | PDMIC_MR_CLKS_MASK, mr_val);

	snd_soc_update_bits(codec, PDMIC_DSPR0,
		PDMIC_DSPR0_OSR_MASK | PDMIC_DSPR0_SIZE_MASK, dspr0_val);

	return 0;
}

static int atmel_pdmic_codec_dai_prepare(struct snd_pcm_substream *substream,
					struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;

	snd_soc_update_bits(codec, PDMIC_CR, PDMIC_CR_ENPDM_MASK,
			    PDMIC_CR_ENPDM_DIS << PDMIC_CR_ENPDM_SHIFT);

	return 0;
}

static int atmel_pdmic_codec_dai_trigger(struct snd_pcm_substream *substream,
					int cmd, struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u32 val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = PDMIC_CR_ENPDM_EN << PDMIC_CR_ENPDM_SHIFT;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = PDMIC_CR_ENPDM_DIS << PDMIC_CR_ENPDM_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, PDMIC_CR, PDMIC_CR_ENPDM_MASK, val);

	return 0;
}

static const struct snd_soc_dai_ops atmel_pdmic_codec_dai_ops = {
	.hw_params	= atmel_pdmic_codec_dai_hw_params,
	.prepare	= atmel_pdmic_codec_dai_prepare,
	.trigger	= atmel_pdmic_codec_dai_trigger,
};

#define ATMEL_PDMIC_CODEC_DAI_NAME  "atmel-pdmic-hifi"

static struct snd_soc_dai_driver atmel_pdmic_codec_dai = {
	.name = ATMEL_PDMIC_CODEC_DAI_NAME,
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 1,
		.rates		= SNDRV_PCM_RATE_KNOT,
		.formats	= ATMEL_PDMIC_FORMATS,
	},
	.ops = &atmel_pdmic_codec_dai_ops,
};

/* ASoC sound card */
static int atmel_pdmic_asoc_card_init(struct device *dev,
				struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	struct atmel_pdmic *dd = snd_soc_card_get_drvdata(card);

	dai_link = devm_kzalloc(dev, sizeof(*dai_link), GFP_KERNEL);
	if (!dai_link)
		return -ENOMEM;

	dai_link->name			= "PDMIC";
	dai_link->stream_name		= "PDMIC PCM";
	dai_link->codec_dai_name	= ATMEL_PDMIC_CODEC_DAI_NAME;
	dai_link->cpu_dai_name		= dev_name(dev);
	dai_link->codec_name		= dev_name(dev);
	dai_link->platform_name		= dev_name(dev);

	card->dai_link	= dai_link;
	card->num_links	= 1;
	card->name	= dd->pdata->card_name;
	card->dev	= dev;

	return 0;
}

static void atmel_pdmic_get_sample_rate(struct atmel_pdmic *dd,
	unsigned int *rate_min, unsigned int *rate_max)
{
	u32 mic_min_freq = dd->pdata->mic_min_freq;
	u32 mic_max_freq = dd->pdata->mic_max_freq;
	u32 clk_max_rate = (u32)(clk_get_rate(dd->pclk) >> 1);
	u32 clk_min_rate = (u32)(clk_get_rate(dd->gclk) >> 8);

	if (mic_max_freq > clk_max_rate)
		mic_max_freq = clk_max_rate;

	if (mic_min_freq < clk_min_rate)
		mic_min_freq = clk_min_rate;

	*rate_min = DIV_ROUND_CLOSEST(mic_min_freq, 128);
	*rate_max = mic_max_freq >> 6;
}

/* PDMIC interrupt handler */
static irqreturn_t atmel_pdmic_interrupt(int irq, void *dev_id)
{
	struct atmel_pdmic *dd = (struct atmel_pdmic *)dev_id;
	u32 pdmic_isr;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(dd->regmap, PDMIC_ISR, &pdmic_isr);

	if (pdmic_isr & PDMIC_ISR_OVRE) {
		regmap_update_bits(dd->regmap, PDMIC_CR, PDMIC_CR_ENPDM_MASK,
				   PDMIC_CR_ENPDM_DIS << PDMIC_CR_ENPDM_SHIFT);

		snd_pcm_stop_xrun(dd->substream);

		ret = IRQ_HANDLED;
	}

	return ret;
}

/* regmap configuration */
#define ATMEL_PDMIC_REG_MAX    0x124
static const struct regmap_config atmel_pdmic_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= ATMEL_PDMIC_REG_MAX,
};

static int atmel_pdmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atmel_pdmic *dd;
	struct resource *res;
	void __iomem *io_base;
	const struct atmel_pdmic_pdata *pdata;
	struct snd_soc_card *card;
	unsigned int rate_min, rate_max;
	int ret;

	pdata = atmel_pdmic_dt_init(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	dd = devm_kzalloc(dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	dd->pdata = pdata;

	dd->irq = platform_get_irq(pdev, 0);
	if (dd->irq < 0) {
		ret = dd->irq;
		dev_err(dev, "failed to could not get irq: %d\n", ret);
		return ret;
	}

	dd->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dd->pclk)) {
		ret = PTR_ERR(dd->pclk);
		dev_err(dev, "failed to get peripheral clock: %d\n", ret);
		return ret;
	}

	dd->gclk = devm_clk_get(dev, "gclk");
	if (IS_ERR(dd->gclk)) {
		ret = PTR_ERR(dd->gclk);
		dev_err(dev, "failed to get GCK: %d\n", ret);
		return ret;
	}

	/* The gclk clock frequency must always be tree times
	 * lower than the pclk clock frequency
	 */
	ret = clk_set_rate(dd->gclk, clk_get_rate(dd->pclk)/3);
	if (ret) {
		dev_err(dev, "failed to set GCK clock rate: %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(io_base)) {
		ret = PTR_ERR(io_base);
		dev_err(dev, "failed to remap register memory: %d\n", ret);
		return ret;
	}

	dd->phy_base = res->start;

	dd->regmap = devm_regmap_init_mmio(dev, io_base,
					   &atmel_pdmic_regmap_config);
	if (IS_ERR(dd->regmap)) {
		ret = PTR_ERR(dd->regmap);
		dev_err(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	ret =  devm_request_irq(dev, dd->irq, atmel_pdmic_interrupt, 0,
				"PDMIC", (void *)dd);
	if (ret < 0) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			dd->irq, ret);
		return ret;
	}

	/* Get the minimal and maximal sample rate that micphone supports */
	atmel_pdmic_get_sample_rate(dd, &rate_min, &rate_max);

	/* register cpu dai */
	atmel_pdmic_cpu_dai.capture.rate_min = rate_min;
	atmel_pdmic_cpu_dai.capture.rate_max = rate_max;
	ret = devm_snd_soc_register_component(dev,
					      &atmel_pdmic_cpu_dai_component,
					      &atmel_pdmic_cpu_dai, 1);
	if (ret) {
		dev_err(dev, "could not register CPU DAI: %d\n", ret);
		return ret;
	}

	/* register platform */
	ret = devm_snd_dmaengine_pcm_register(dev,
					     &atmel_pdmic_dmaengine_pcm_config,
					     0);
	if (ret) {
		dev_err(dev, "could not register platform: %d\n", ret);
		return ret;
	}

	/* register codec and codec dai */
	atmel_pdmic_codec_dai.capture.rate_min = rate_min;
	atmel_pdmic_codec_dai.capture.rate_max = rate_max;
	ret = snd_soc_register_codec(dev, &soc_codec_dev_pdmic,
				     &atmel_pdmic_codec_dai, 1);
	if (ret) {
		dev_err(dev, "could not register codec: %d\n", ret);
		return ret;
	}

	/* register sound card */
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		ret = -ENOMEM;
		goto unregister_codec;
	}

	snd_soc_card_set_drvdata(card, dd);
	platform_set_drvdata(pdev, card);

	ret = atmel_pdmic_asoc_card_init(dev, card);
	if (ret) {
		dev_err(dev, "failed to init sound card: %d\n", ret);
		goto unregister_codec;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret) {
		dev_err(dev, "failed to register sound card: %d\n", ret);
		goto unregister_codec;
	}

	return 0;

unregister_codec:
	snd_soc_unregister_codec(dev);
	return ret;
}

static int atmel_pdmic_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver atmel_pdmic_driver = {
	.driver	= {
		.name		= "atmel-pdmic",
		.of_match_table	= of_match_ptr(atmel_pdmic_of_match),
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= atmel_pdmic_probe,
	.remove	= atmel_pdmic_remove,
};
module_platform_driver(atmel_pdmic_driver);

MODULE_DESCRIPTION("Atmel PDMIC driver under ALSA SoC architecture");
MODULE_AUTHOR("Songjun Wu <songjun.wu@atmel.com>");
MODULE_LICENSE("GPL v2");
