// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADAU7002 Stereo PDM-to-I2S/TDM converter driver
 *
 * Copyright 2014-2016 Analog Devices
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

struct adau7002_priv {
	int wakeup_delay;
};

static int adau7002_aif_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	struct adau7002_priv *adau7002 =
			snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (adau7002->wakeup_delay)
			msleep(adau7002->wakeup_delay);
		break;
	}

	return 0;
}

static int adau7002_component_probe(struct snd_soc_component *component)
{
	struct adau7002_priv *adau7002;

	adau7002 = devm_kzalloc(component->dev, sizeof(*adau7002),
				GFP_KERNEL);
	if (!adau7002)
		return -ENOMEM;

	device_property_read_u32(component->dev, "wakeup-delay-ms",
				 &adau7002->wakeup_delay);

	snd_soc_component_set_drvdata(component, adau7002);

	return 0;
}

static const struct snd_soc_dapm_widget adau7002_widgets[] = {
	SND_SOC_DAPM_AIF_OUT_E("ADAU AIF", "Capture", 0,
			       SND_SOC_NOPM, 0, 0, adau7002_aif_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_INPUT("PDM_DAT"),
	SND_SOC_DAPM_REGULATOR_SUPPLY("IOVDD", 0, 0),
};

static const struct snd_soc_dapm_route adau7002_routes[] = {
	{ "ADAU AIF", NULL, "PDM_DAT"},
	{ "Capture", NULL, "PDM_DAT" },
	{ "Capture", NULL, "IOVDD" },
};

static struct snd_soc_dai_driver adau7002_dai = {
	.name = "adau7002-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE |
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 20,
	},
};

static const struct snd_soc_component_driver adau7002_component_driver = {
	.probe			= adau7002_component_probe,
	.dapm_widgets		= adau7002_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(adau7002_widgets),
	.dapm_routes		= adau7002_routes,
	.num_dapm_routes	= ARRAY_SIZE(adau7002_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int adau7002_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&adau7002_component_driver,
			&adau7002_dai, 1);
}

static int adau7002_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id adau7002_dt_ids[] = {
	{ .compatible = "adi,adau7002", },
	{ }
};
MODULE_DEVICE_TABLE(of, adau7002_dt_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id adau7002_acpi_match[] = {
	{ "ADAU7002", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, adau7002_acpi_match);
#endif

static struct platform_driver adau7002_driver = {
	.driver = {
		.name = "adau7002",
		.of_match_table	= of_match_ptr(adau7002_dt_ids),
		.acpi_match_table = ACPI_PTR(adau7002_acpi_match),
	},
	.probe = adau7002_probe,
	.remove = adau7002_remove,
};
module_platform_driver(adau7002_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADAU7002 Stereo PDM-to-I2S/TDM Converter driver");
MODULE_LICENSE("GPL v2");
