/*
 * dw-hdmi-audio.c
 *
 * DesignerWare ALSA SoC Codec driver for DW HDMI audio.
 * Copyright (c) 2016 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include "../../../drivers/video/rockchip/hdmi/rockchip-hdmi.h"

static int snd_dw_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *codec_dai)
{
	return snd_config_hdmi_audio(params);
}

static const struct snd_soc_dapm_widget snd_dw_hdmi_audio_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route snd_dw_hdmi_audio_routes[] = {
	{ "TX", NULL, "Playback" },
};

static const struct snd_soc_dai_ops dw_hdmi_dai_ops = {
	.hw_params = snd_dw_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver dw_hdmi_audio_dai = {
	.name = "dw-hdmi-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &dw_hdmi_dai_ops,
};

static const struct snd_soc_codec_driver dw_hdmi_audio = {
	.dapm_widgets = snd_dw_hdmi_audio_widgets,
	.num_dapm_widgets = ARRAY_SIZE(snd_dw_hdmi_audio_widgets),
	.dapm_routes = snd_dw_hdmi_audio_routes,
	.num_dapm_routes = ARRAY_SIZE(snd_dw_hdmi_audio_routes),
};

static int dw_hdmi_audio_probe(struct platform_device *pdev)
{
	int ret;

	ret = snd_soc_register_codec(&pdev->dev, &dw_hdmi_audio,
				     &dw_hdmi_audio_dai, 1);
	if (ret)
		dev_err(&pdev->dev, "register codec failed (%d)\n", ret);

	return ret;
}

static int dw_hdmi_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id dw_hdmi_audio_ids[] = {
	{ .compatible = "dw-hdmi-audio", },
	{ }
};

static struct platform_driver dw_hdmi_audio_driver = {
	.driver = {
		.name = "dw-hdmi-audio",
		.of_match_table = of_match_ptr(dw_hdmi_audio_ids),
	},
	.probe = dw_hdmi_audio_probe,
	.remove = dw_hdmi_audio_remove,
};
module_platform_driver(dw_hdmi_audio_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("DW HDMI Audio ASoC Interface");
MODULE_LICENSE("GPL");
