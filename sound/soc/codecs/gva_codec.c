/*
 * Driver for virtual codec
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 * Author: Li Dongqiang <David.li@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static const struct snd_soc_dapm_widget gva_codec_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route gva_codec_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

#define gva_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver gva_codec_dai = {
	.name = "gva_codec",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = gva_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = gva_FORMATS,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_gva_codec = {
	.dapm_widgets = gva_codec_widgets,
	.num_dapm_widgets = ARRAY_SIZE(gva_codec_widgets),
	.dapm_routes = gva_codec_routes,
	.num_dapm_routes = ARRAY_SIZE(gva_codec_routes),
};

static int gva_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_gva_codec,
			&gva_codec_dai, 1);
}

static int gva_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id gva_codec_of_match[] = {
	{ .compatible = "rockchip,gva-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, gva_codec_of_match);

static struct platform_driver gva_codec_driver = {
	.driver = {
		.name = "gva_codec",
		.of_match_table = of_match_ptr(gva_codec_of_match),
	},
	.probe = gva_codec_probe,
	.remove = gva_codec_remove,
};

module_platform_driver(gva_codec_driver);

MODULE_AUTHOR("Li Dongqiang <David.li@rock-chips.com>");
MODULE_DESCRIPTION("ASoC gva virtual driver");
MODULE_LICENSE("GPL");
