/*
 * sound\soc\sunxi\i2s\sndi2s.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <plat/sys_config.h>
#include <linux/io.h>

#include "sndi2s.h"

struct sndi2s_priv {
	int sysclk;
	int dai_fmt;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

static int i2s_used = 0;
#define sndi2s_RATES  (SNDRV_PCM_RATE_8000_192000|SNDRV_PCM_RATE_KNOT)
#define sndi2s_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		                     SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

hdmi_audio_t hdmi_parameter;

static int sndi2s_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int sndi2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	return 0;
}

static void sndi2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{

}

static int sndi2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	hdmi_parameter.sample_rate = params_rate(params);

	return 0;
}

static int sndi2s_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int sndi2s_set_dai_clkdiv(struct snd_soc_dai *codec_dai, int div_id, int div)
{

	hdmi_parameter.fs_between = div;

	return 0;
}

static int sndi2s_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int fmt)
{
	return 0;
}

struct snd_soc_dai_ops sndi2s_dai_ops = {
	.startup = sndi2s_startup,
	.shutdown = sndi2s_shutdown,
	.hw_params = sndi2s_hw_params,
	.digital_mute = sndi2s_mute,
	.set_sysclk = sndi2s_set_dai_sysclk,
	.set_clkdiv = sndi2s_set_dai_clkdiv,
	.set_fmt = sndi2s_set_dai_fmt,
};

struct snd_soc_dai_driver sndi2s_dai = {
	.name = "sndi2s",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = sndi2s_RATES,
		.formats = sndi2s_FORMATS,
	},
	/* pcm operations */
	.ops = &sndi2s_dai_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL(sndi2s_dai);

static int sndi2s_soc_probe(struct snd_soc_codec *codec)
{
	struct sndi2s_priv *sndi2s;

	sndi2s = kzalloc(sizeof(struct sndi2s_priv), GFP_KERNEL);
	if(sndi2s == NULL){
		return -ENOMEM;
	}
	snd_soc_codec_set_drvdata(codec, sndi2s);

	return 0;
}

/* power down chip */
static int sndi2s_soc_remove(struct snd_soc_codec *codec)
{
	struct sndhdmi_priv *sndi2s = snd_soc_codec_get_drvdata(codec);

	kfree(sndi2s);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_sndi2s = {
	.probe 	=	sndi2s_soc_probe,
	.remove =   sndi2s_soc_remove,
};

static int __devinit sndi2s_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sndi2s, &sndi2s_dai, 1);
}

static int __devexit sndi2s_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}
/*data relating*/
static struct platform_device sndi2s_codec_device = {
	.name = "sunxi-i2s-codec",
};

/*method relating*/
static struct platform_driver sndi2s_codec_driver = {
	.driver = {
		.name = "sunxi-i2s-codec",
		.owner = THIS_MODULE,
	},
	.probe = sndi2s_codec_probe,
	.remove = __devexit_p(sndi2s_codec_remove),
};

static int __init sndi2s_codec_init(void)
{
	int err = 0;
	int ret = 0;

	ret = script_parser_fetch("i2s_para","i2s_used", &i2s_used, sizeof(int));
	if (ret) {
        printk("[I2S]sndi2s_init fetch i2s using configuration failed\n");
    }

	if (i2s_used) {
		if((err = platform_device_register(&sndi2s_codec_device)) < 0)
			return err;

		if ((err = platform_driver_register(&sndi2s_codec_driver)) < 0)
			return err;
	} else {
       printk("[I2S]sndi2s cannot find any using configuration for controllers, return directly!\n");
       return 0;
    }

	return 0;
}
module_init(sndi2s_codec_init);

static void __exit sndi2s_codec_exit(void)
{
	if (i2s_used) {
		i2s_used = 0;
		platform_driver_unregister(&sndi2s_codec_driver);
	}
}
module_exit(sndi2s_codec_exit);

MODULE_DESCRIPTION("SNDI2S ALSA soc codec driver");
MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-i2s-codec");
