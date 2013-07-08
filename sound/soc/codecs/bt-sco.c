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

static struct snd_soc_dai_driver bt_sco_dai = {
	.name = "bt-sco-pcm",
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

static struct snd_soc_codec_driver soc_codec_dev_bt_sco;

static int bt_sco_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_bt_sco,
			&bt_sco_dai, 1);
}

static int bt_sco_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static struct platform_device_id bt_sco_driver_ids[] = {
	{
		.name		= "dfbmcs320",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, bt_sco_driver_ids);

static struct platform_driver bt_sco_driver = {
	.driver = {
		.name = "bt-sco",
		.owner = THIS_MODULE,
	},
	.probe = bt_sco_probe,
	.remove = bt_sco_remove,
	.id_table = bt_sco_driver_ids,
};

module_platform_driver(bt_sco_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ASoC generic bluethooth sco link driver");
MODULE_LICENSE("GPL");
