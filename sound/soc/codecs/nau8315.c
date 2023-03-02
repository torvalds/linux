// SPDX-License-Identifier: GPL-2.0-only
//
// nau8315.c  --  NAU8315 ALSA SoC Audio Amplifier Driver
//
// Copyright 2020 Nuvoton Technology Crop.
//
// Author: David Lin <ctlin0@nuvoton.com>
//
// Based on MAX98357A.c

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

struct nau8315_priv {
	struct gpio_desc *enable;
	int enpin_switch;
};

static int nau8315_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct nau8315_priv *nau8315 =
		snd_soc_component_get_drvdata(component);

	if (!nau8315->enable)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (nau8315->enpin_switch) {
			gpiod_set_value(nau8315->enable, 1);
			dev_dbg(component->dev, "set enable to 1");
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		gpiod_set_value(nau8315->enable, 0);
		dev_dbg(component->dev, "set enable to 0");
		break;
	}

	return 0;
}

static int nau8315_enpin_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct nau8315_priv *nau8315 =
		snd_soc_component_get_drvdata(component);

	if (event & SND_SOC_DAPM_PRE_PMU)
		nau8315->enpin_switch = 1;
	else if (event & SND_SOC_DAPM_POST_PMD)
		nau8315->enpin_switch = 0;

	return 0;
}

static const struct snd_soc_dapm_widget nau8315_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
	SND_SOC_DAPM_OUT_DRV_E("EN_Pin", SND_SOC_NOPM, 0, 0, NULL, 0,
			nau8315_enpin_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route nau8315_dapm_routes[] = {
	{"EN_Pin", NULL, "HiFi Playback"},
	{"Speaker", NULL, "EN_Pin"},
};

static const struct snd_soc_component_driver nau8315_component_driver = {
	.dapm_widgets		= nau8315_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(nau8315_dapm_widgets),
	.dapm_routes		= nau8315_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(nau8315_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct snd_soc_dai_ops nau8315_dai_ops = {
	.trigger	= nau8315_daiops_trigger,
};

#define NAU8315_RATES SNDRV_PCM_RATE_8000_96000
#define NAU8315_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_3LE)

static struct snd_soc_dai_driver nau8315_dai_driver = {
	.name = "nau8315-hifi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= NAU8315_FORMATS,
		.rates		= NAU8315_RATES,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops    = &nau8315_dai_ops,
};

static int nau8315_platform_probe(struct platform_device *pdev)
{
	struct nau8315_priv *nau8315;

	nau8315 = devm_kzalloc(&pdev->dev, sizeof(*nau8315), GFP_KERNEL);
	if (!nau8315)
		return -ENOMEM;

	nau8315->enable = devm_gpiod_get_optional(&pdev->dev,
				"enable", GPIOD_OUT_LOW);
	if (IS_ERR(nau8315->enable))
		return PTR_ERR(nau8315->enable);

	dev_set_drvdata(&pdev->dev, nau8315);

	return devm_snd_soc_register_component(&pdev->dev,
			&nau8315_component_driver,
			&nau8315_dai_driver, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id nau8315_device_id[] = {
	{ .compatible = "nuvoton,nau8315" },
	{ .compatible = "nuvoton,nau8318" },
	{}
};
MODULE_DEVICE_TABLE(of, nau8315_device_id);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id nau8315_acpi_match[] = {
	{ "NVTN2010", 0 },
	{ "NVTN2012", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, nau8315_acpi_match);
#endif

static struct platform_driver nau8315_platform_driver = {
	.driver = {
		.name = "nau8315",
		.of_match_table = of_match_ptr(nau8315_device_id),
		.acpi_match_table = ACPI_PTR(nau8315_acpi_match),
	},
	.probe	= nau8315_platform_probe,
};
module_platform_driver(nau8315_platform_driver);

MODULE_DESCRIPTION("ASoC NAU8315 Mono Class-D Amplifier Driver");
MODULE_AUTHOR("David Lin <ctlin0@nuvoton.com>");
MODULE_LICENSE("GPL v2");
