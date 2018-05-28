/*
 * dummy_codec.c  --  dummy audio codec for rockchip
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

struct snd_soc_dai_driver dummy_dai = {
	.name = "dummy_codec",
	.playback = {
		.stream_name = "Dummy Playback",
		.channels_min = 2,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.capture = {
		.stream_name = "Dummy Capture",
		.channels_min = 2,
		.channels_max = 384,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
};

static struct snd_soc_codec_driver soc_dummy_codec;

static int rockchip_dummy_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_dummy_codec,
				      &dummy_dai, 1);
}

static int rockchip_dummy_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id rockchip_dummy_codec_of_match[] = {
	{ .compatible = "rockchip,dummy-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_dummy_codec_of_match);

static struct platform_driver rockchip_dummy_codec_driver = {
	.driver = {
		.name = "dummy_codec",
		.of_match_table = of_match_ptr(rockchip_dummy_codec_of_match),
	},
	.probe = rockchip_dummy_codec_probe,
	.remove = rockchip_dummy_codec_remove,
};

module_platform_driver(rockchip_dummy_codec_driver);

MODULE_AUTHOR("Sugar <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Dummy Codec Driver");
MODULE_LICENSE("GPL v2");
