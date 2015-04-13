/* Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * max98357a.c -- MAX98357A ALSA SoC Codec driver
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <sound/soc.h>

#define DRV_NAME "max98357a"

static int max98357a_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct gpio_desc *sdmode = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		gpiod_set_value(sdmode, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		gpiod_set_value(sdmode, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget max98357a_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("SDMode", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route max98357a_dapm_routes[] = {
	{"Speaker", NULL, "SDMode"},
};

static int max98357a_codec_probe(struct snd_soc_codec *codec)
{
	struct gpio_desc *sdmode;

	sdmode = devm_gpiod_get(codec->dev, "sdmode");
	if (IS_ERR(sdmode)) {
		dev_err(codec->dev, "%s() unable to get sdmode GPIO: %ld\n",
				__func__, PTR_ERR(sdmode));
		return PTR_ERR(sdmode);
	}
	gpiod_direction_output(sdmode, 0);
	snd_soc_codec_set_drvdata(codec, sdmode);

	return 0;
}

static struct snd_soc_codec_driver max98357a_codec_driver = {
	.probe			= max98357a_codec_probe,
	.dapm_widgets		= max98357a_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98357a_dapm_widgets),
	.dapm_routes		= max98357a_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(max98357a_dapm_routes),
};

static struct snd_soc_dai_ops max98357a_dai_ops = {
	.trigger	= max98357a_daiops_trigger,
};

static struct snd_soc_dai_driver max98357a_dai_driver = {
	.name = DRV_NAME,
	.playback = {
		.stream_name	= DRV_NAME "-playback",
		.formats	= SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
		.rates		= SNDRV_PCM_RATE_8000 |
					SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000 |
					SNDRV_PCM_RATE_96000,
		.rate_min	= 8000,
		.rate_max	= 96000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops    = &max98357a_dai_ops,
};

static int max98357a_platform_probe(struct platform_device *pdev)
{
	int ret;

	ret = snd_soc_register_codec(&pdev->dev, &max98357a_codec_driver,
			&max98357a_dai_driver, 1);
	if (ret)
		dev_err(&pdev->dev, "%s() error registering codec driver: %d\n",
				__func__, ret);

	return ret;
}

static int max98357a_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id max98357a_device_id[] = {
	{ .compatible = "maxim," DRV_NAME, },
	{}
};
MODULE_DEVICE_TABLE(of, max98357a_device_id);
#endif

static struct platform_driver max98357a_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(max98357a_device_id),
	},
	.probe	= max98357a_platform_probe,
	.remove = max98357a_platform_remove,
};
module_platform_driver(max98357a_platform_driver);

MODULE_DESCRIPTION("Maxim MAX98357A Codec Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
