/*
 * wm8727.c
 *
 *  Created on: 15-Oct-2009
 *      Author: neil.jones@imgtec.com
 *
 * Copyright (C) 2009 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "wm8727.h"
/*
 * Note this is a simple chip with no configuration interface, sample rate is
 * determined automatically by examining the Master clock and Bit clock ratios
 */
#define WM8727_RATES  (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |\
			SNDRV_PCM_RATE_192000)


struct snd_soc_dai wm8727_dai = {
	.name = "WM8727",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8727_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		},
};
EXPORT_SYMBOL_GPL(wm8727_dai);

static int wm8727_soc_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;
	mutex_init(&codec->mutex);
	codec->name = "WM8727";
	codec->owner = THIS_MODULE;
	codec->dai = &wm8727_dai;
	codec->num_dai = 1;
	socdev->card->codec = codec;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "wm8727: failed to create pcms\n");
		goto pcm_err;
	}
	/* register card */
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "wm8727: failed to register card\n");
		goto register_err;
	}

	return ret;

register_err:
	snd_soc_free_pcms(socdev);
pcm_err:
	kfree(socdev->card->codec);
	socdev->card->codec = NULL;
	return ret;
}

static int wm8727_soc_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (codec == NULL)
		return 0;
	snd_soc_free_pcms(socdev);
	kfree(codec);
	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8727 = {
	.probe = 	wm8727_soc_probe,
	.remove = 	wm8727_soc_remove,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8727);


static __devinit int wm8727_platform_probe(struct platform_device *pdev)
{
	wm8727_dai.dev = &pdev->dev;
	return snd_soc_register_dai(&wm8727_dai);
}

static int __devexit wm8727_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&wm8727_dai);
	return 0;
}

static struct platform_driver wm8727_codec_driver = {
	.driver = {
			.name = "wm8727-codec",
			.owner = THIS_MODULE,
	},

	.probe = wm8727_platform_probe,
	.remove = __devexit_p(wm8727_platform_remove),
};

static int __init wm8727_init(void)
{
	return platform_driver_register(&wm8727_codec_driver);
}
module_init(wm8727_init);

static void __exit wm8727_exit(void)
{
	platform_driver_unregister(&wm8727_codec_driver);
}
module_exit(wm8727_exit);

MODULE_DESCRIPTION("ASoC wm8727 driver");
MODULE_AUTHOR("Neil Jones");
MODULE_LICENSE("GPL");
