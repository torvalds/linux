/*
 * Driver for the DFBM-CS320 bluetooth module
 * Copyright 2011 Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static struct snd_soc_dai_driver dfbmcs320_dai = {
	.name = "dfbmcs320-pcm",
	.playback = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_dfbmcs320;

static int dfbmcs320_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_dfbmcs320,
			&dfbmcs320_dai, 1);
}

static int dfbmcs320_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static struct platform_driver dfmcs320_driver = {
	.driver = {
		.name = "dfbmcs320",
		.owner = THIS_MODULE,
	},
	.probe = dfbmcs320_probe,
	.remove = dfbmcs320_remove,
};

module_platform_driver(dfmcs320_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ASoC DFBM-CS320 bluethooth module driver");
MODULE_LICENSE("GPL");
