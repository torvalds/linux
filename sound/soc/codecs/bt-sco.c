// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for generic Bluetooth SCO link
 * Copyright 2011 Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static const struct snd_soc_dapm_widget bt_sco_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
	SND_SOC_DAPM_AIF_IN("BT_SCO_RX", "Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("BT_SCO_TX", "Capture", 0,
			     SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route bt_sco_routes[] = {
	{ "BT_SCO_TX", NULL, "RX" },
	{ "TX", NULL, "BT_SCO_RX" },
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

static const struct snd_soc_component_driver soc_component_dev_bt_sco = {
	.dapm_widgets		= bt_sco_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(bt_sco_widgets),
	.dapm_routes		= bt_sco_routes,
	.num_dapm_routes	= ARRAY_SIZE(bt_sco_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int bt_sco_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
				      &soc_component_dev_bt_sco,
				      bt_sco_dai, ARRAY_SIZE(bt_sco_dai));
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
	.id_table = bt_sco_driver_ids,
};

module_platform_driver(bt_sco_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ASoC generic bluetooth sco link driver");
MODULE_LICENSE("GPL");
