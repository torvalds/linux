/*
 * ALSA SoC codec driver for HDMI audio codecs.
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Ricardo Neri <ricardo.neri@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/module.h>
#include <sound/soc.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define DRV_NAME "hdmi-audio-codec"

static const struct snd_soc_dapm_widget hdmi_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route hdmi_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static struct snd_soc_dai_driver hdmi_codec_dai = {
	.name = "hdmi-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 24,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},

};

#ifdef CONFIG_OF
static const struct of_device_id hdmi_audio_codec_ids[] = {
	{ .compatible = "linux,hdmi-audio", },
	{ }
};
MODULE_DEVICE_TABLE(of, hdmi_audio_codec_ids);
#endif

static struct snd_soc_codec_driver hdmi_codec = {
	.dapm_widgets = hdmi_widgets,
	.num_dapm_widgets = ARRAY_SIZE(hdmi_widgets),
	.dapm_routes = hdmi_routes,
	.num_dapm_routes = ARRAY_SIZE(hdmi_routes),
	.ignore_pmdown_time = true,
};

static int hdmi_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &hdmi_codec,
			&hdmi_codec_dai, 1);
}

static int hdmi_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver hdmi_codec_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(hdmi_audio_codec_ids),
	},

	.probe		= hdmi_codec_probe,
	.remove		= hdmi_codec_remove,
};

module_platform_driver(hdmi_codec_driver);

MODULE_AUTHOR("Ricardo Neri <ricardo.neri@ti.com>");
MODULE_DESCRIPTION("ASoC generic HDMI codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
