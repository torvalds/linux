/*
 * ad73311.c  --  ALSA Soc AD73311 codec support
 *
 * Copyright:	Analog Device Inc.
 * Author:	Cliff Cai <cliff.cai@analog.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "ad73311.h"

static const struct snd_soc_dapm_widget ad73311_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("VINP"),
SND_SOC_DAPM_INPUT("VINN"),
SND_SOC_DAPM_OUTPUT("VOUTN"),
SND_SOC_DAPM_OUTPUT("VOUTP"),
};

static const struct snd_soc_dapm_route ad73311_dapm_routes[] = {
	{ "Capture", NULL, "VINP" },
	{ "Capture", NULL, "VINN" },

	{ "VOUTN", NULL, "Playback" },
	{ "VOUTP", NULL, "Playback" },
};

static struct snd_soc_dai_driver ad73311_dai = {
	.name = "ad73311-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE, },
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE, },
};

static struct snd_soc_codec_driver soc_codec_dev_ad73311 = {
	.dapm_widgets = ad73311_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ad73311_dapm_widgets),
	.dapm_routes = ad73311_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(ad73311_dapm_routes),
};

static int ad73311_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_ad73311, &ad73311_dai, 1);
}

static int ad73311_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver ad73311_codec_driver = {
	.driver = {
			.name = "ad73311",
			.owner = THIS_MODULE,
	},

	.probe = ad73311_probe,
	.remove = ad73311_remove,
};

module_platform_driver(ad73311_codec_driver);

MODULE_DESCRIPTION("ASoC ad73311 driver");
MODULE_AUTHOR("Cliff Cai ");
MODULE_LICENSE("GPL");
