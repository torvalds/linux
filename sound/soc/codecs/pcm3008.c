/*
 * ALSA Soc PCM3008 codec support
 *
 * Author:	Hugo Villeneuve
 * Copyright (C) 2008 Lyrtech inc
 *
 * Based on AC97 Soc codec, original copyright follow:
 * Copyright 2005 Wolfson Microelectronics PLC.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Generic PCM3008 support.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "pcm3008.h"

#define PCM3008_VERSION "0.2"

#define PCM3008_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |	\
		       SNDRV_PCM_RATE_48000)

struct snd_soc_dai pcm3008_dai = {
	.name = "PCM3008 HiFi",
	.playback = {
		.stream_name = "PCM3008 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = PCM3008_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "PCM3008 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = PCM3008_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};
EXPORT_SYMBOL_GPL(pcm3008_dai);

static void pcm3008_gpio_free(struct pcm3008_setup_data *setup)
{
	gpio_free(setup->dem0_pin);
	gpio_free(setup->dem1_pin);
	gpio_free(setup->pdad_pin);
	gpio_free(setup->pdda_pin);
}

static int pcm3008_soc_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct pcm3008_setup_data *setup = socdev->codec_data;
	int ret = 0;

	printk(KERN_INFO "PCM3008 SoC Audio Codec %s\n", PCM3008_VERSION);

	socdev->card->codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (!socdev->card->codec)
		return -ENOMEM;

	codec = socdev->card->codec;
	mutex_init(&codec->mutex);

	codec->name = "PCM3008";
	codec->owner = THIS_MODULE;
	codec->dai = &pcm3008_dai;
	codec->num_dai = 1;
	codec->write = NULL;
	codec->read = NULL;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	/* Register PCMs. */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "pcm3008: failed to create pcms\n");
		goto pcm_err;
	}

	/* DEM1  DEM0  DE-EMPHASIS_MODE
	 * Low   Low   De-emphasis 44.1 kHz ON
	 * Low   High  De-emphasis OFF
	 * High  Low   De-emphasis 48 kHz ON
	 * High  High  De-emphasis 32 kHz ON
	 */

	/* Configure DEM0 GPIO (turning OFF DAC De-emphasis). */
	ret = gpio_request(setup->dem0_pin, "codec_dem0");
	if (ret == 0)
		ret = gpio_direction_output(setup->dem0_pin, 1);
	if (ret != 0)
		goto gpio_err;

	/* Configure DEM1 GPIO (turning OFF DAC De-emphasis). */
	ret = gpio_request(setup->dem1_pin, "codec_dem1");
	if (ret == 0)
		ret = gpio_direction_output(setup->dem1_pin, 0);
	if (ret != 0)
		goto gpio_err;

	/* Configure PDAD GPIO. */
	ret = gpio_request(setup->pdad_pin, "codec_pdad");
	if (ret == 0)
		ret = gpio_direction_output(setup->pdad_pin, 1);
	if (ret != 0)
		goto gpio_err;

	/* Configure PDDA GPIO. */
	ret = gpio_request(setup->pdda_pin, "codec_pdda");
	if (ret == 0)
		ret = gpio_direction_output(setup->pdda_pin, 1);
	if (ret != 0)
		goto gpio_err;

	return ret;

gpio_err:
	pcm3008_gpio_free(setup);
pcm_err:
	kfree(socdev->card->codec);

	return ret;
}

static int pcm3008_soc_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	struct pcm3008_setup_data *setup = socdev->codec_data;

	if (!codec)
		return 0;

	pcm3008_gpio_free(setup);
	snd_soc_free_pcms(socdev);
	kfree(socdev->card->codec);

	return 0;
}

#ifdef CONFIG_PM
static int pcm3008_soc_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct pcm3008_setup_data *setup = socdev->codec_data;

	gpio_set_value(setup->pdad_pin, 0);
	gpio_set_value(setup->pdda_pin, 0);

	return 0;
}

static int pcm3008_soc_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct pcm3008_setup_data *setup = socdev->codec_data;

	gpio_set_value(setup->pdad_pin, 1);
	gpio_set_value(setup->pdda_pin, 1);

	return 0;
}
#else
#define pcm3008_soc_suspend NULL
#define pcm3008_soc_resume NULL
#endif

struct snd_soc_codec_device soc_codec_dev_pcm3008 = {
	.probe = 	pcm3008_soc_probe,
	.remove = 	pcm3008_soc_remove,
	.suspend =	pcm3008_soc_suspend,
	.resume =	pcm3008_soc_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_pcm3008);

static int __init pcm3008_init(void)
{
	return snd_soc_register_dai(&pcm3008_dai);
}
module_init(pcm3008_init);

static void __exit pcm3008_exit(void)
{
	snd_soc_unregister_dai(&pcm3008_dai);
}
module_exit(pcm3008_exit);

MODULE_DESCRIPTION("Soc PCM3008 driver");
MODULE_AUTHOR("Hugo Villeneuve");
MODULE_LICENSE("GPL");
