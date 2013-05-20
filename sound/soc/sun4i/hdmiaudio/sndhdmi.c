/*
 * sound\soc\sun4i\hdmiaudio\sndhdmi.c
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

#include "sndhdmi.h"

#define HDMI

struct sndhdmi_priv {
	int sysclk;
	int dai_fmt;

	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

#ifdef HDMI
__audio_hdmi_func g_hdmi_func;

void audio_set_hdmi_func(__audio_hdmi_func * hdmi_func)
{
	g_hdmi_func.hdmi_audio_enable = hdmi_func->hdmi_audio_enable;
	g_hdmi_func.hdmi_set_audio_para = hdmi_func->hdmi_set_audio_para;
}

EXPORT_SYMBOL(audio_set_hdmi_func);
#endif

#define SNDHDMI_RATES  (SNDRV_PCM_RATE_8000_192000|SNDRV_PCM_RATE_KNOT)
#define SNDHDMI_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		                     SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

hdmi_audio_t hdmi_para;

static int sndhdmi_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static int sndhdmi_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	return 0;
}

static void sndhdmi_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{

}

static int sndhdmi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	hdmi_para.sample_rate = params_rate(params);

	#ifdef HDMI
		g_hdmi_func.hdmi_audio_enable(1, 1);

	#endif

	return 0;
}

static int sndhdmi_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int sndhdmi_set_dai_clkdiv(struct snd_soc_dai *codec_dai, int div_id, int div)
{

	hdmi_para.fs_between = div;

	return 0;
}


static int sndhdmi_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int fmt)
{
	return 0;
}

//codec dai operation
struct snd_soc_dai_ops sndhdmi_dai_ops = {
	.startup = sndhdmi_startup,
	.shutdown = sndhdmi_shutdown,
	.hw_params = sndhdmi_hw_params,
	.digital_mute = sndhdmi_mute,
	.set_sysclk = sndhdmi_set_dai_sysclk,
	.set_clkdiv = sndhdmi_set_dai_clkdiv,
	.set_fmt = sndhdmi_set_dai_fmt,
};

//codec dai
struct snd_soc_dai_driver sndhdmi_dai = {
	.name = "sndhdmi",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDHDMI_RATES,
		.formats = SNDHDMI_FORMATS,
	},
	/* pcm operations */
	.ops = &sndhdmi_dai_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL(sndhdmi_dai);

static int sndhdmi_soc_probe(struct snd_soc_codec *codec)
{
	struct sndhdmi_priv *sndhdmi;

	sndhdmi = kzalloc(sizeof(struct sndhdmi_priv), GFP_KERNEL);
	if(sndhdmi == NULL){
		printk("error at:%s,%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	snd_soc_codec_set_drvdata(codec, sndhdmi);

	return 0;
}

static int sndhdmi_soc_remove(struct snd_soc_codec *codec)
{
	struct sndhdmi_priv *sndhdmi = snd_soc_codec_get_drvdata(codec);

	kfree(sndhdmi);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_sndhdmi = {
	.probe =        sndhdmi_soc_probe,
	.remove =       sndhdmi_soc_remove,
};

static int __devinit sndhdmi_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_sndhdmi, &sndhdmi_dai, 1);
}

static int __devexit sndhdmi_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver sndhdmi_codec_driver = {
	.driver = {
		.name = "sun4i-hdmiaudio-codec",
		.owner = THIS_MODULE,
	},
	.probe = sndhdmi_codec_probe,
	.remove = __devexit_p(sndhdmi_codec_remove),
};

static int __init sndhdmi_codec_init(void)
{
	int err = 0;

	if ((err = platform_driver_register(&sndhdmi_codec_driver)) < 0)
		return err;

	return 0;
}
module_init(sndhdmi_codec_init);

static void __exit sndhdmi_codec_exit(void)
{
	platform_driver_unregister(&sndhdmi_codec_driver);
}
module_exit(sndhdmi_codec_exit);

MODULE_DESCRIPTION("SNDHDMI ALSA soc codec driver");
MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_LICENSE("GPL");

