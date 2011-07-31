/*
 * wm8741.c  --  WM8741 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Ian Lartey <ian@opensource.wolfsonmicro.com>
 *
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
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8741.h"

static struct snd_soc_codec *wm8741_codec;
struct snd_soc_codec_device soc_codec_dev_wm8741;

#define WM8741_NUM_SUPPLIES 2
static const char *wm8741_supply_names[WM8741_NUM_SUPPLIES] = {
	"AVDD",
	"DVDD",
};

#define WM8741_NUM_RATES 4

/* codec private data */
struct wm8741_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[WM8741_REGISTER_COUNT];
	struct regulator_bulk_data supplies[WM8741_NUM_SUPPLIES];
	unsigned int sysclk;
	unsigned int rate_constraint_list[WM8741_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
};

static const u16 wm8741_reg_defaults[WM8741_REGISTER_COUNT] = {
	0x0000,     /* R0  - DACLLSB Attenuation */
	0x0000,     /* R1  - DACLMSB Attenuation */
	0x0000,     /* R2  - DACRLSB Attenuation */
	0x0000,     /* R3  - DACRMSB Attenuation */
	0x0000,     /* R4  - Volume Control */
	0x000A,     /* R5  - Format Control */
	0x0000,     /* R6  - Filter Control */
	0x0000,     /* R7  - Mode Control 1 */
	0x0002,     /* R8  - Mode Control 2 */
	0x0000,	    /* R9  - Reset */
	0x0002,     /* R32 - ADDITONAL_CONTROL_1 */
};


static int wm8741_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8741_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(dac_tlv_fine, -12700, 13, 0);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12700, 400, 0);

static const struct snd_kcontrol_new wm8741_snd_controls[] = {
SOC_DOUBLE_R_TLV("Fine Playback Volume", WM8741_DACLLSB_ATTENUATION,
		 WM8741_DACRLSB_ATTENUATION, 1, 255, 1, dac_tlv_fine),
SOC_DOUBLE_R_TLV("Playback Volume", WM8741_DACLMSB_ATTENUATION,
		 WM8741_DACRMSB_ATTENUATION, 0, 511, 1, dac_tlv),
};

static const struct snd_soc_dapm_widget wm8741_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DACL", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DACR", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("VOUTLP"),
SND_SOC_DAPM_OUTPUT("VOUTLN"),
SND_SOC_DAPM_OUTPUT("VOUTRP"),
SND_SOC_DAPM_OUTPUT("VOUTRN"),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "VOUTLP", NULL, "DACL" },
	{ "VOUTLN", NULL, "DACL" },
	{ "VOUTRP", NULL, "DACR" },
	{ "VOUTRN", NULL, "DACR" },
};

static int wm8741_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8741_dapm_widgets,
				  ARRAY_SIZE(wm8741_dapm_widgets));

	snd_soc_dapm_add_routes(codec, intercon, ARRAY_SIZE(intercon));

	return 0;
}

static struct {
	int value;
	int ratio;
} lrclk_ratios[WM8741_NUM_RATES] = {
	{ 1, 256 },
	{ 2, 384 },
	{ 3, 512 },
	{ 4, 768 },
};


static int wm8741_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8741->sysclk) {
		dev_err(codec->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &wm8741->rate_constraint);

	return 0;
}

static int wm8741_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8741_FORMAT_CONTROL) & 0x1FC;
	int i;

	/* Find a supported LRCLK ratio */
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		if (wm8741->sysclk / params_rate(params) ==
		    lrclk_ratios[i].ratio)
			break;
	}

	/* Should never happen, should be handled by constraints */
	if (i == ARRAY_SIZE(lrclk_ratios)) {
		dev_err(codec->dev, "MCLK/fs ratio %d unsupported\n",
			wm8741->sysclk / params_rate(params));
		return -EINVAL;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0001;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0002;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x0003;
		break;
	default:
		dev_dbg(codec->dev, "wm8741_hw_params:    Unsupported bit size param = %d",
			params_format(params));
		return -EINVAL;
	}

	dev_dbg(codec->dev, "wm8741_hw_params:    bit size param = %d",
		params_format(params));

	snd_soc_write(codec, WM8741_FORMAT_CONTROL, iface);
	return 0;
}

static int wm8741_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8741_priv *wm8741 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;
	int i;

	dev_dbg(codec->dev, "wm8741_set_dai_sysclk info: freq=%dHz\n", freq);

	wm8741->sysclk = freq;

	wm8741->rate_constraint.count = 0;

	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		dev_dbg(codec->dev, "index = %d, ratio = %d, freq = %d",
				i, lrclk_ratios[i].ratio, freq);

		val = freq / lrclk_ratios[i].ratio;
		/* Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */
		switch (val) {
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
			dev_dbg(codec->dev, "Supported sample rate: %dHz\n",
				val);
			wm8741->rate_constraint_list[i] = val;
			wm8741->rate_constraint.count++;
			break;
		default:
			dev_dbg(codec->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (wm8741->rate_constraint.count == 0)
		return -EINVAL;

	return 0;
}

static int wm8741_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = snd_soc_read(codec, WM8741_FORMAT_CONTROL) & 0x1C3;

	/* check master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0004;
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
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0020;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0030;
		break;
	default:
		return -EINVAL;
	}


	dev_dbg(codec->dev, "wm8741_set_dai_fmt:    Format=%x, Clock Inv=%x\n",
				fmt & SND_SOC_DAIFMT_FORMAT_MASK,
				((fmt & SND_SOC_DAIFMT_INV_MASK)));

	snd_soc_write(codec, WM8741_FORMAT_CONTROL, iface);
	return 0;
}

#define WM8741_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | \
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 | \
			SNDRV_PCM_RATE_192000)

#define WM8741_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8741_dai_ops = {
	.startup	= wm8741_startup,
	.hw_params	= wm8741_hw_params,
	.set_sysclk	= wm8741_set_dai_sysclk,
	.set_fmt	= wm8741_set_dai_fmt,
};

struct snd_soc_dai wm8741_dai = {
	.name = "WM8741",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,  /* Mono modes not yet supported */
		.channels_max = 2,
		.rates = WM8741_RATES,
		.formats = WM8741_FORMATS,
	},
	.ops = &wm8741_dai_ops,
};
EXPORT_SYMBOL_GPL(wm8741_dai);

#ifdef CONFIG_PM
static int wm8741_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 *cache = codec->reg_cache;
	int i;

	/* RESTORE REG Cache */
	for (i = 0; i < WM8741_REGISTER_COUNT; i++) {
		if (cache[i] == wm8741_reg_defaults[i] || WM8741_RESET == i)
			continue;
		snd_soc_write(codec, i, cache[i]);
	}
	return 0;
}
#else
#define wm8741_suspend NULL
#define wm8741_resume NULL
#endif

static int wm8741_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8741_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8741_codec;
	codec = wm8741_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8741_snd_controls,
			     ARRAY_SIZE(wm8741_snd_controls));
	wm8741_add_widgets(codec);

	return ret;

pcm_err:
	return ret;
}

static int wm8741_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8741 = {
	.probe =	wm8741_probe,
	.remove =	wm8741_remove,
	.resume =	wm8741_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8741);

static int wm8741_register(struct wm8741_priv *wm8741,
			   enum snd_soc_control_type control)
{
	int ret;
	struct snd_soc_codec *codec = &wm8741->codec;
	int i;

	if (wm8741_codec) {
		dev_err(codec->dev, "Another WM8741 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	snd_soc_codec_set_drvdata(codec, wm8741);
	codec->name = "WM8741";
	codec->owner = THIS_MODULE;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = NULL;
	codec->dai = &wm8741_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8741_REGISTER_COUNT;
	codec->reg_cache = &wm8741->reg_cache;

	wm8741->rate_constraint.list = &wm8741->rate_constraint_list[0];
	wm8741->rate_constraint.count =
		ARRAY_SIZE(wm8741->rate_constraint_list);

	memcpy(codec->reg_cache, wm8741_reg_defaults,
		sizeof(wm8741->reg_cache));

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, control);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm8741->supplies); i++)
		wm8741->supplies[i].supply = wm8741_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8741->supplies),
				 wm8741->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		goto err;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8741->supplies),
				    wm8741->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = wm8741_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	wm8741_dai.dev = codec->dev;

	/* Change some default settings - latch VU */
	wm8741->reg_cache[WM8741_DACLLSB_ATTENUATION] |= WM8741_UPDATELL;
	wm8741->reg_cache[WM8741_DACLMSB_ATTENUATION] |= WM8741_UPDATELM;
	wm8741->reg_cache[WM8741_DACRLSB_ATTENUATION] |= WM8741_UPDATERL;
	wm8741->reg_cache[WM8741_DACRLSB_ATTENUATION] |= WM8741_UPDATERM;

	wm8741_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm8741_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	dev_dbg(codec->dev, "Successful registration\n");
	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);

err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);

err:
	kfree(wm8741);
	return ret;
}

static void wm8741_unregister(struct wm8741_priv *wm8741)
{
	regulator_bulk_free(ARRAY_SIZE(wm8741->supplies), wm8741->supplies);

	snd_soc_unregister_dai(&wm8741_dai);
	snd_soc_unregister_codec(&wm8741->codec);
	kfree(wm8741);
	wm8741_codec = NULL;
}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8741_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8741_priv *wm8741;
	struct snd_soc_codec *codec;

	wm8741 = kzalloc(sizeof(struct wm8741_priv), GFP_KERNEL);
	if (wm8741 == NULL)
		return -ENOMEM;

	codec = &wm8741->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;

	i2c_set_clientdata(i2c, wm8741);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8741_register(wm8741, SND_SOC_I2C);
}

static __devexit int wm8741_i2c_remove(struct i2c_client *client)
{
	struct wm8741_priv *wm8741 = i2c_get_clientdata(client);
	wm8741_unregister(wm8741);
	return 0;
}

static const struct i2c_device_id wm8741_i2c_id[] = {
	{ "wm8741", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8741_i2c_id);


static struct i2c_driver wm8741_i2c_driver = {
	.driver = {
		.name = "WM8741",
		.owner = THIS_MODULE,
	},
	.probe =    wm8741_i2c_probe,
	.remove =   __devexit_p(wm8741_i2c_remove),
	.id_table = wm8741_i2c_id,
};
#endif

static int __init wm8741_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8741_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8741 I2C driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8741_modinit);

static void __exit wm8741_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8741_i2c_driver);
#endif
}
module_exit(wm8741_exit);

MODULE_DESCRIPTION("ASoC WM8741 driver");
MODULE_AUTHOR("Ian Lartey <ian@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
