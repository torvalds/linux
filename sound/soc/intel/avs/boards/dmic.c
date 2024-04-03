// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/device.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

SND_SOC_DAILINK_DEF(dmic_pin, DAILINK_COMP_ARRAY(COMP_CPU("DMIC Pin")));
SND_SOC_DAILINK_DEF(dmic_wov_pin, DAILINK_COMP_ARRAY(COMP_CPU("DMIC WoV Pin")));
SND_SOC_DAILINK_DEF(dmic_codec, DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));
/* Name overridden on probe */
SND_SOC_DAILINK_DEF(platform, DAILINK_COMP_ARRAY(COMP_PLATFORM("")));

static struct snd_soc_dai_link card_dai_links[] = {
	/* Back ends */
	{
		.name = "DMIC",
		.id = 0,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "DMIC WoV",
		.id = 1,
		.dpcm_capture = 1,
		.nonatomic = 1,
		.no_pcm = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(dmic_wov_pin, dmic_codec, platform),
	},
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{"DMic", NULL, "SoC DMIC"},
};

static int avs_dmic_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	int ret;

	mach = dev_get_platdata(dev);

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = "avs_dmic";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = card_dai_links;
	card->num_links = ARRAY_SIZE(card_dai_links);
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_routes);
	card->fully_routed = true;

	ret = snd_soc_fixup_dai_links_platform_name(card, mach->mach_params.platform);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static const struct platform_device_id avs_dmic_driver_ids[] = {
	{
		.name = "avs_dmic",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_dmic_driver_ids);

static struct platform_driver avs_dmic_driver = {
	.probe = avs_dmic_probe,
	.driver = {
		.name = "avs_dmic",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_dmic_driver_ids,
};

module_platform_driver(avs_dmic_driver);

MODULE_LICENSE("GPL");
