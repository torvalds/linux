/*
 * hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
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

struct snd_soc_dai_driver hdmi_i2s_dai = {
	.name = "rk-hdmi-i2s-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = (SNDRV_PCM_RATE_32000 |
			  SNDRV_PCM_RATE_44100 |
			  SNDRV_PCM_RATE_48000 |
			  SNDRV_PCM_RATE_96000 |
			  SNDRV_PCM_RATE_192000),
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE),
	},
};

static struct snd_soc_codec_driver soc_codec_dev_hdmi_i2s;

static int rockchip_hdmi_i2s_audio_probe(struct platform_device *pdev)
{
	int ret;

	ret = snd_soc_register_codec(&pdev->dev,
				     &soc_codec_dev_hdmi_i2s,
				     &hdmi_i2s_dai, 1);

	if (ret)
		dev_err(&pdev->dev, "register card failed: %d\n", ret);

	return ret;
}

static int rockchip_hdmi_i2s_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_hdmi_i2s_of_match[] = {
	{ .compatible = "hdmi-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_hdmi_i2s_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_hdmi_i2s_audio_driver = {
	.driver = {
		.name = "hdmi-i2s",
		.of_match_table = of_match_ptr(rockchip_hdmi_i2s_of_match),
	},
	.probe = rockchip_hdmi_i2s_audio_probe,
	.remove = rockchip_hdmi_i2s_audio_remove,
};

module_platform_driver(rockchip_hdmi_i2s_audio_driver);

MODULE_DESCRIPTION("Rockchip HDMI I2S Dummy Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi-i2s");
