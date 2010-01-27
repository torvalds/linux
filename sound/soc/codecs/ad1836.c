/*
 * File:         sound/soc/codecs/ad1836.c
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      Aug 04 2009
 * Description:  Driver for AD1836 sound chip
 *
 * Modified:
 *               Copyright 2009 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/soc-dapm.h>
#include <linux/spi/spi.h>
#include "ad1836.h"

/* codec private data */
struct ad1836_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[AD1836_NUM_REGS];
};

static struct snd_soc_codec *ad1836_codec;
struct snd_soc_codec_device soc_codec_dev_ad1836;
static int ad1836_register(struct ad1836_priv *ad1836);
static void ad1836_unregister(struct ad1836_priv *ad1836);

/*
 * AD1836 volume/mute/de-emphasis etc. controls
 */
static const char *ad1836_deemp[] = {"None", "44.1kHz", "32kHz", "48kHz"};

static const struct soc_enum ad1836_deemp_enum =
	SOC_ENUM_SINGLE(AD1836_DAC_CTRL1, 8, 4, ad1836_deemp);

static const struct snd_kcontrol_new ad1836_snd_controls[] = {
	/* DAC volume control */
	SOC_DOUBLE_R("DAC1 Volume", AD1836_DAC_L1_VOL,
			AD1836_DAC_R1_VOL, 0, 0x3FF, 0),
	SOC_DOUBLE_R("DAC2 Volume", AD1836_DAC_L2_VOL,
			AD1836_DAC_R2_VOL, 0, 0x3FF, 0),
	SOC_DOUBLE_R("DAC3 Volume", AD1836_DAC_L3_VOL,
			AD1836_DAC_R3_VOL, 0, 0x3FF, 0),

	/* ADC switch control */
	SOC_DOUBLE("ADC1 Switch", AD1836_ADC_CTRL2, AD1836_ADCL1_MUTE,
		AD1836_ADCR1_MUTE, 1, 1),
	SOC_DOUBLE("ADC2 Switch", AD1836_ADC_CTRL2, AD1836_ADCL2_MUTE,
		AD1836_ADCR2_MUTE, 1, 1),

	/* DAC switch control */
	SOC_DOUBLE("DAC1 Switch", AD1836_DAC_CTRL2, AD1836_DACL1_MUTE,
		AD1836_DACR1_MUTE, 1, 1),
	SOC_DOUBLE("DAC2 Switch", AD1836_DAC_CTRL2, AD1836_DACL2_MUTE,
		AD1836_DACR2_MUTE, 1, 1),
	SOC_DOUBLE("DAC3 Switch", AD1836_DAC_CTRL2, AD1836_DACL3_MUTE,
		AD1836_DACR3_MUTE, 1, 1),

	/* ADC high-pass filter */
	SOC_SINGLE("ADC High Pass Filter Switch", AD1836_ADC_CTRL1,
			AD1836_ADC_HIGHPASS_FILTER, 1, 0),

	/* DAC de-emphasis */
	SOC_ENUM("Playback Deemphasis", ad1836_deemp_enum),
};

static const struct snd_soc_dapm_widget ad1836_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", AD1836_DAC_CTRL1,
				AD1836_DAC_POWERDOWN, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC_PWR", AD1836_ADC_CTRL1,
				AD1836_ADC_POWERDOWN, 1, NULL, 0),
	SND_SOC_DAPM_OUTPUT("DAC1OUT"),
	SND_SOC_DAPM_OUTPUT("DAC2OUT"),
	SND_SOC_DAPM_OUTPUT("DAC3OUT"),
	SND_SOC_DAPM_INPUT("ADC1IN"),
	SND_SOC_DAPM_INPUT("ADC2IN"),
};

static const struct snd_soc_dapm_route audio_paths[] = {
	{ "DAC", NULL, "ADC_PWR" },
	{ "ADC", NULL, "ADC_PWR" },
	{ "DAC1OUT", "DAC1 Switch", "DAC" },
	{ "DAC2OUT", "DAC2 Switch", "DAC" },
	{ "DAC3OUT", "DAC3 Switch", "DAC" },
	{ "ADC", "ADC1 Switch", "ADC1IN" },
	{ "ADC", "ADC2 Switch", "ADC2IN" },
};

/*
 * DAI ops entries
 */

static int ad1836_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	/* at present, we support adc aux mode to interface with
	 * blackfin sport tdm mode
	 */
	case SND_SOC_DAIFMT_DSP_A:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	/* ALCLK,ABCLK are both output, AD1836 can only be master */
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad1836_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int word_len = 0;

	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		word_len = 3;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		word_len = 1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		word_len = 0;
		break;
	}

	snd_soc_update_bits(codec, AD1836_DAC_CTRL1,
		AD1836_DAC_WORD_LEN_MASK, word_len);

	snd_soc_update_bits(codec, AD1836_ADC_CTRL2,
		AD1836_ADC_WORD_LEN_MASK, word_len);

	return 0;
}

#ifdef CONFIG_PM
static int ad1836_soc_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	/* reset clock control mode */
	u16 adc_ctrl2 = snd_soc_read(codec, AD1836_ADC_CTRL2);
	adc_ctrl2 &= ~AD1836_ADC_SERFMT_MASK;

	return snd_soc_write(codec, AD1836_ADC_CTRL2, adc_ctrl2);
}

static int ad1836_soc_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	/* restore clock control mode */
	u16 adc_ctrl2 = snd_soc_read(codec, AD1836_ADC_CTRL2);
	adc_ctrl2 |= AD1836_ADC_AUX;

	return snd_soc_write(codec, AD1836_ADC_CTRL2, adc_ctrl2);
}
#else
#define ad1836_soc_suspend NULL
#define ad1836_soc_resume  NULL
#endif

static int __devinit ad1836_spi_probe(struct spi_device *spi)
{
	struct snd_soc_codec *codec;
	struct ad1836_priv *ad1836;

	ad1836 = kzalloc(sizeof(struct ad1836_priv), GFP_KERNEL);
	if (ad1836 == NULL)
		return -ENOMEM;

	codec = &ad1836->codec;
	codec->control_data = spi;
	codec->dev = &spi->dev;

	dev_set_drvdata(&spi->dev, ad1836);

	return ad1836_register(ad1836);
}

static int __devexit ad1836_spi_remove(struct spi_device *spi)
{
	struct ad1836_priv *ad1836 = dev_get_drvdata(&spi->dev);

	ad1836_unregister(ad1836);
	return 0;
}

static struct spi_driver ad1836_spi_driver = {
	.driver = {
		.name	= "ad1836",
		.owner	= THIS_MODULE,
	},
	.probe		= ad1836_spi_probe,
	.remove		= __devexit_p(ad1836_spi_remove),
};

static struct snd_soc_dai_ops ad1836_dai_ops = {
	.hw_params = ad1836_hw_params,
	.set_fmt = ad1836_set_dai_fmt,
};

/* codec DAI instance */
struct snd_soc_dai ad1836_dai = {
	.name = "AD1836",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &ad1836_dai_ops,
};
EXPORT_SYMBOL_GPL(ad1836_dai);

static int ad1836_register(struct ad1836_priv *ad1836)
{
	int ret;
	struct snd_soc_codec *codec = &ad1836->codec;

	if (ad1836_codec) {
		dev_err(codec->dev, "Another ad1836 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	codec->private_data = ad1836;
	codec->reg_cache = ad1836->reg_cache;
	codec->reg_cache_size = AD1836_NUM_REGS;
	codec->name = "AD1836";
	codec->owner = THIS_MODULE;
	codec->dai = &ad1836_dai;
	codec->num_dai = 1;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	ad1836_dai.dev = codec->dev;
	ad1836_codec = codec;

	ret = snd_soc_codec_set_cache_io(codec, 4, 12, SND_SOC_SPI);
	if (ret < 0) {
		dev_err(codec->dev, "failed to set cache I/O: %d\n",
				ret);
		kfree(ad1836);
		return ret;
	}

	/* default setting for ad1836 */
	/* de-emphasis: 48kHz, power-on dac */
	snd_soc_write(codec, AD1836_DAC_CTRL1, 0x300);
	/* unmute dac channels */
	snd_soc_write(codec, AD1836_DAC_CTRL2, 0x0);
	/* high-pass filter enable, power-on adc */
	snd_soc_write(codec, AD1836_ADC_CTRL1, 0x100);
	/* unmute adc channles, adc aux mode */
	snd_soc_write(codec, AD1836_ADC_CTRL2, 0x180);
	/* left/right diff:PGA/MUX */
	snd_soc_write(codec, AD1836_ADC_CTRL3, 0x3A);
	/* volume */
	snd_soc_write(codec, AD1836_DAC_L1_VOL, 0x3FF);
	snd_soc_write(codec, AD1836_DAC_R1_VOL, 0x3FF);
	snd_soc_write(codec, AD1836_DAC_L2_VOL, 0x3FF);
	snd_soc_write(codec, AD1836_DAC_R2_VOL, 0x3FF);
	snd_soc_write(codec, AD1836_DAC_L3_VOL, 0x3FF);
	snd_soc_write(codec, AD1836_DAC_R3_VOL, 0x3FF);

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		kfree(ad1836);
		return ret;
	}

	ret = snd_soc_register_dai(&ad1836_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		kfree(ad1836);
		return ret;
	}

	return 0;
}

static void ad1836_unregister(struct ad1836_priv *ad1836)
{
	snd_soc_unregister_dai(&ad1836_dai);
	snd_soc_unregister_codec(&ad1836->codec);
	kfree(ad1836);
	ad1836_codec = NULL;
}

static int ad1836_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (ad1836_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = ad1836_codec;
	codec = ad1836_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, ad1836_snd_controls,
			     ARRAY_SIZE(ad1836_snd_controls));
	snd_soc_dapm_new_controls(codec, ad1836_dapm_widgets,
				  ARRAY_SIZE(ad1836_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_paths, ARRAY_SIZE(audio_paths));

pcm_err:
	return ret;
}

/* power down chip */
static int ad1836_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_ad1836 = {
	.probe = 	ad1836_probe,
	.remove = 	ad1836_remove,
	.suspend =      ad1836_soc_suspend,
	.resume =       ad1836_soc_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ad1836);

static int __init ad1836_init(void)
{
	int ret;

	ret = spi_register_driver(&ad1836_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register ad1836 SPI driver: %d\n",
				ret);
	}

	return ret;
}
module_init(ad1836_init);

static void __exit ad1836_exit(void)
{
	spi_unregister_driver(&ad1836_spi_driver);
}
module_exit(ad1836_exit);

MODULE_DESCRIPTION("ASoC ad1836 driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
