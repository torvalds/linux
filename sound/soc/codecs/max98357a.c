// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * max98357a.c -- MAX98357A ALSA SoC Codec driver
 */

#include <linux/acpi.h>
#include <linux/delay.h>
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

static int max98357a_sdmode_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct max98357a_priv *max98357a =
		snd_soc_component_get_drvdata(component);

	if (!max98357a->sdmode)
		return 0;

	if (event & SND_SOC_DAPM_POST_PMU) {
		msleep(max98357a->sdmode_delay);
		gpiod_set_value(max98357a->sdmode, 1);
		dev_dbg(component->dev, "set sdmode to 1");
	} else if (event & SND_SOC_DAPM_PRE_PMD) {
		gpiod_set_value(max98357a->sdmode, 0);
		dev_dbg(component->dev, "set sdmode to 0");
	}

	return 0;
}

static const struct snd_soc_dapm_widget max98357a_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
	SND_SOC_DAPM_OUT_DRV_E("SD_MODE", SND_SOC_NOPM, 0, 0, NULL, 0,
			max98357a_sdmode_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
};

static const struct snd_soc_dapm_route max98357a_dapm_routes[] = {
	{"SD_MODE", NULL, "HiFi Playback"},
	{"Speaker", NULL, "SD_MODE"},
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
	{ "MX98360A", 0 },
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
