/*
 * ac97.c  --  ALSA Soc AC97 codec support
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Generic AC97 support.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include "ac97.h"

#define AC97_VERSION "0.6"

static int ac97_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;

	int reg = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		  AC97_PCM_FRONT_DAC_RATE : AC97_PCM_LR_ADC_RATE;
	return snd_ac97_set_rate(codec->ac97, reg, runtime->rate);
}

#define STD_AC97_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 |\
		SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_ops ac97_dai_ops = {
	.prepare	= ac97_prepare,
};

struct snd_soc_dai ac97_dai = {
	.name = "AC97 HiFi",
	.ac97_control = 1,
	.playback = {
		.stream_name = "AC97 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = STD_AC97_RATES,
		.formats = SND_SOC_STD_AC97_FMTS,},
	.capture = {
		.stream_name = "AC97 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = STD_AC97_RATES,
		.formats = SND_SOC_STD_AC97_FMTS,},
	.ops = &ac97_dai_ops,
};
EXPORT_SYMBOL_GPL(ac97_dai);

static unsigned int ac97_read(struct snd_soc_codec *codec,
	unsigned int reg)
{
	return soc_ac97_ops.read(codec->ac97, reg);
}

static int ac97_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int val)
{
	soc_ac97_ops.write(codec->ac97, reg, val);
	return 0;
}

static int ac97_soc_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97_template ac97_template;
	int ret = 0;

	printk(KERN_INFO "AC97 SoC Audio Codec %s\n", AC97_VERSION);

	socdev->card->codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (!socdev->card->codec)
		return -ENOMEM;
	codec = socdev->card->codec;
	mutex_init(&codec->mutex);

	codec->name = "AC97";
	codec->owner = THIS_MODULE;
	codec->dai = &ac97_dai;
	codec->num_dai = 1;
	codec->write = ac97_write;
	codec->read = ac97_read;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0)
		goto err;

	/* add codec as bus device for standard ac97 */
	ret = snd_ac97_bus(codec->card, 0, &soc_ac97_ops, NULL, &ac97_bus);
	if (ret < 0)
		goto bus_err;

	memset(&ac97_template, 0, sizeof(struct snd_ac97_template));
	ret = snd_ac97_mixer(ac97_bus, &ac97_template, &codec->ac97);
	if (ret < 0)
		goto bus_err;

	return 0;

bus_err:
	snd_soc_free_pcms(socdev);

err:
	kfree(socdev->card->codec);
	socdev->card->codec = NULL;
	return ret;
}

static int ac97_soc_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (!codec)
		return 0;

	snd_soc_free_pcms(socdev);
	kfree(socdev->card->codec);

	return 0;
}

#ifdef CONFIG_PM
static int ac97_soc_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_ac97_suspend(socdev->card->codec->ac97);

	return 0;
}

static int ac97_soc_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_ac97_resume(socdev->card->codec->ac97);

	return 0;
}
#else
#define ac97_soc_suspend NULL
#define ac97_soc_resume NULL
#endif

struct snd_soc_codec_device soc_codec_dev_ac97 = {
	.probe = 	ac97_soc_probe,
	.remove = 	ac97_soc_remove,
	.suspend =	ac97_soc_suspend,
	.resume =	ac97_soc_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_ac97);

MODULE_DESCRIPTION("Soc Generic AC97 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
