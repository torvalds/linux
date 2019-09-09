// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ads117x.c  --  Driver for ads1174/8 ADC chips
 *
 * Copyright 2009 ShotSpotter Inc.
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <linux/of.h>

#define ADS117X_RATES (SNDRV_PCM_RATE_8000_48000)
#define ADS117X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE)

static const struct snd_soc_dapm_widget ads117x_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("Input1"),
SND_SOC_DAPM_INPUT("Input2"),
SND_SOC_DAPM_INPUT("Input3"),
SND_SOC_DAPM_INPUT("Input4"),
SND_SOC_DAPM_INPUT("Input5"),
SND_SOC_DAPM_INPUT("Input6"),
SND_SOC_DAPM_INPUT("Input7"),
SND_SOC_DAPM_INPUT("Input8"),
};

static const struct snd_soc_dapm_route ads117x_dapm_routes[] = {
	{ "Capture", NULL, "Input1" },
	{ "Capture", NULL, "Input2" },
	{ "Capture", NULL, "Input3" },
	{ "Capture", NULL, "Input4" },
	{ "Capture", NULL, "Input5" },
	{ "Capture", NULL, "Input6" },
	{ "Capture", NULL, "Input7" },
	{ "Capture", NULL, "Input8" },
};

static struct snd_soc_dai_driver ads117x_dai = {
/* ADC */
	.name = "ads117x-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 32,
		.rates = ADS117X_RATES,
		.formats = ADS117X_FORMATS,},
};

static const struct snd_soc_component_driver soc_component_dev_ads117x = {
	.dapm_widgets		= ads117x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ads117x_dapm_widgets),
	.dapm_routes		= ads117x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(ads117x_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int ads117x_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_ads117x, &ads117x_dai, 1);
}

#if defined(CONFIG_OF)
static const struct of_device_id ads117x_dt_ids[] = {
	{ .compatible = "ti,ads1174" },
	{ .compatible = "ti,ads1178" },
	{ },
};
MODULE_DEVICE_TABLE(of, ads117x_dt_ids);
#endif

static struct platform_driver ads117x_codec_driver = {
	.driver = {
			.name = "ads117x-codec",
			.of_match_table = of_match_ptr(ads117x_dt_ids),
	},

	.probe = ads117x_probe,
};

module_platform_driver(ads117x_codec_driver);

MODULE_DESCRIPTION("ASoC ads117x driver");
MODULE_AUTHOR("Graeme Gregory");
MODULE_LICENSE("GPL");
