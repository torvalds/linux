/*
 * ADAU7002 Stereo PDM-to-I2S/TDM converter driver
 *
 * Copyright 2014-2016 Analog Devices
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static const struct snd_soc_dapm_widget adau7002_widgets[] = {
	SND_SOC_DAPM_INPUT("PDM_DAT"),
	SND_SOC_DAPM_REGULATOR_SUPPLY("IOVDD", 0, 0),
};

static const struct snd_soc_dapm_route adau7002_routes[] = {
	{ "Capture", NULL, "PDM_DAT" },
	{ "Capture", NULL, "IOVDD" },
};

static struct snd_soc_dai_driver adau7002_dai = {
	.name = "adau7002-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 20,
	},
};

static const struct snd_soc_codec_driver adau7002_codec_driver = {
	.component_driver = {
		.dapm_widgets		= adau7002_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(adau7002_widgets),
		.dapm_routes		= adau7002_routes,
		.num_dapm_routes	= ARRAY_SIZE(adau7002_routes),
	},
};

static int adau7002_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &adau7002_codec_driver,
			&adau7002_dai, 1);
}

static int adau7002_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id adau7002_dt_ids[] = {
	{ .compatible = "adi,adau7002", },
	{ }
};
MODULE_DEVICE_TABLE(of, adau7002_dt_ids);
#endif

static struct platform_driver adau7002_driver = {
	.driver = {
		.name = "adau7002",
		.of_match_table	= of_match_ptr(adau7002_dt_ids),
	},
	.probe = adau7002_probe,
	.remove = adau7002_remove,
};
module_platform_driver(adau7002_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADAU7002 Stereo PDM-to-I2S/TDM Converter driver");
MODULE_LICENSE("GPL v2");
