// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * max98357a.c -- MAX98357A ALSA SoC Codec driver
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

struct max98357a_priv {
	struct gpio_desc *sdmode;
	unsigned int sdmode_delay;
};

static int max98357a_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct max98357a_priv *max98357a = snd_soc_dai_get_drvdata(dai);

	if (!max98357a->sdmode)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mdelay(max98357a->sdmode_delay);
		gpiod_set_value(max98357a->sdmode, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		gpiod_set_value(max98357a->sdmode, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget max98357a_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route max98357a_dapm_routes[] = {
	{"Speaker", NULL, "HiFi Playback"},
};

static const struct snd_soc_component_driver max98357a_component_driver = {
	.dapm_widgets		= max98357a_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98357a_dapm_widgets),
	.dapm_routes		= max98357a_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(max98357a_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops max98357a_dai_ops = {
	.trigger	= max98357a_daiops_trigger,
};

static struct snd_soc_dai_driver max98357a_dai_driver = {
	.name = "HiFi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
		.rates		= SNDRV_PCM_RATE_8000 |
					SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_32000 |
					SNDRV_PCM_RATE_44100 |
					SNDRV_PCM_RATE_48000 |
					SNDRV_PCM_RATE_88200 |
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
	struct max98357a_priv *max98357a;
	int ret;

	max98357a = devm_kzalloc(&pdev->dev, sizeof(*max98357a), GFP_KERNEL);
	if (!max98357a)
		return -ENOMEM;

	max98357a->sdmode = devm_gpiod_get_optional(&pdev->dev,
				"sdmode", GPIOD_OUT_LOW);
	if (IS_ERR(max98357a->sdmode))
		return PTR_ERR(max98357a->sdmode);

	ret = device_property_read_u32(&pdev->dev, "sdmode-delay",
					&max98357a->sdmode_delay);
	if (ret) {
		max98357a->sdmode_delay = 0;
		dev_dbg(&pdev->dev,
			"no optional property 'sdmode-delay' found, "
			"default: no delay\n");
	}

	dev_set_drvdata(&pdev->dev, max98357a);

	return devm_snd_soc_register_component(&pdev->dev,
			&max98357a_component_driver,
			&max98357a_dai_driver, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id max98357a_device_id[] = {
	{ .compatible = "maxim,max98357a" },
	{}
};
MODULE_DEVICE_TABLE(of, max98357a_device_id);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id max98357a_acpi_match[] = {
	{ "MX98357A", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98357a_acpi_match);
#endif

static struct platform_driver max98357a_platform_driver = {
	.driver = {
		.name = "max98357a",
		.of_match_table = of_match_ptr(max98357a_device_id),
		.acpi_match_table = ACPI_PTR(max98357a_acpi_match),
	},
	.probe	= max98357a_platform_probe,
};
module_platform_driver(max98357a_platform_driver);

MODULE_DESCRIPTION("Maxim MAX98357A Codec Driver");
MODULE_LICENSE("GPL v2");
