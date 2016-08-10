/*
 * Driver for generic Bluetooth SCO link
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

static const struct snd_soc_dapm_widget bt_sco_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route bt_sco_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static struct snd_soc_dai_driver bt_sco_dai[] = {
	{
		.name = "bt-sco-pcm",
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
	},
	{
		.name = "bt-sco-pcm-wb",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			 .stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	}
};

static struct snd_soc_codec_driver soc_codec_dev_bt_sco = {
	.dapm_widgets = bt_sco_widgets,
	.num_dapm_widgets = ARRAY_SIZE(bt_sco_widgets),
	.dapm_routes = bt_sco_routes,
	.num_dapm_routes = ARRAY_SIZE(bt_sco_routes),
};

static int bt_sco_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_bt_sco,
				      bt_sco_dai, ARRAY_SIZE(bt_sco_dai));
}

static int bt_sco_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct platform_device_id bt_sco_driver_ids[] = {
	{
		.name		= "dfbmcs320",
	},
	{
		.name		= "bt-sco",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, bt_sco_driver_ids);

#if defined(CONFIG_OF)
static const struct of_device_id bt_sco_codec_of_match[] = {
	{ .compatible = "delta,dfbmcs320", },
	{ .compatible = "linux,bt-sco", },
	{},
};
MODULE_DEVICE_TABLE(of, bt_sco_codec_of_match);
#endif

static struct platform_driver bt_sco_driver = {
	.driver = {
		.name = "bt-sco",
		.of_match_table = of_match_ptr(bt_sco_codec_of_match),
	},
	.probe = bt_sco_probe,
	.remove = bt_sco_remove,
	.id_table = bt_sco_driver_ids,
};

module_platform_driver(bt_sco_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ASoC generic bluetooth sco link driver");
MODULE_LICENSE("GPL");
