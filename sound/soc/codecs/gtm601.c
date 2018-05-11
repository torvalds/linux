/*
 * This is a simple driver for the GTM601 Voice PCM interface
 *
 * Copyright (C) 2015 Goldelico GmbH
 *
 * Author: Marek Belisko <marek@goldelico.com>
 *
 * Based on wm8727.c driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

static const struct snd_soc_dapm_widget gtm601_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("AOUT"),
	SND_SOC_DAPM_INPUT("AIN"),
};

static const struct snd_soc_dapm_route gtm601_dapm_routes[] = {
	{ "AOUT", NULL, "Playback" },
	{ "Capture", NULL, "AIN" },
};

static struct snd_soc_dai_driver gtm601_dai = {
	.name = "gtm601",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static const struct snd_soc_component_driver soc_component_dev_gtm601 = {
	.dapm_widgets		= gtm601_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(gtm601_dapm_widgets),
	.dapm_routes		= gtm601_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(gtm601_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int gtm601_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_gtm601, &gtm601_dai, 1);
}

#if defined(CONFIG_OF)
static const struct of_device_id gtm601_codec_of_match[] = {
	{ .compatible = "option,gtm601", },
	{},
};
MODULE_DEVICE_TABLE(of, gtm601_codec_of_match);
#endif

static struct platform_driver gtm601_codec_driver = {
	.driver = {
		.name = "gtm601",
		.of_match_table = of_match_ptr(gtm601_codec_of_match),
	},
	.probe = gtm601_platform_probe,
};

module_platform_driver(gtm601_codec_driver);

MODULE_DESCRIPTION("ASoC gtm601 driver");
MODULE_AUTHOR("Marek Belisko <marek@goldelico.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gtm601");
