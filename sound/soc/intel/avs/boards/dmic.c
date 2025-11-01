// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/device.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../utils.h"

SND_SOC_DAILINK_DEF(dmic_pin, DAILINK_COMP_ARRAY(COMP_CPU("DMIC Pin")));
SND_SOC_DAILINK_DEF(dmic_wov_pin, DAILINK_COMP_ARRAY(COMP_CPU("DMIC WoV Pin")));

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{"DMic", NULL, "SoC DMIC"},
};

static int avs_create_dai_links(struct device *dev, const char *codec_name,
				struct snd_soc_dai_link **links, int *num_links)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;
	const int num_dl = 2;

	dl = devm_kcalloc(dev, num_dl, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->codecs)
		return -ENOMEM;

	dl->codecs->name = devm_kstrdup(dev, codec_name, GFP_KERNEL);
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, "dmic-hifi");
	if (!dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	platform->name = dev_name(dev);
	dl[0].num_cpus = 1;
	dl[0].num_codecs = 1;
	dl[0].platforms = platform;
	dl[0].num_platforms = 1;
	dl[0].nonatomic = 1;
	dl[0].no_pcm = 1;
	dl[0].capture_only = 1;
	memcpy(&dl[1], &dl[0], sizeof(*dl));

	dl[0].name = "DMIC";
	dl[0].cpus = dmic_pin;
	dl[0].id = 0;
	dl[1].name = "DMIC WoV";
	dl[1].cpus = dmic_wov_pin;
	dl[1].id = 1;
	dl[1].ignore_suspend = 1;

	*links = dl;
	*num_links = num_dl;
	return 0;
}

static int avs_dmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach;
	struct avs_mach_pdata *pdata;
	struct snd_soc_card *card;
	int ret;

	mach = dev_get_platdata(dev);
	pdata = mach->pdata;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	ret = avs_create_dai_links(dev, pdata->codec_name, &card->dai_link, &card->num_links);
	if (ret)
		return ret;

	if (pdata->obsolete_card_names) {
		card->name = "avs_dmic";
	} else {
		card->driver_name = "avs_dmic";
		card->long_name = card->name = "AVS DMIC";
	}
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_routes);
	card->fully_routed = true;

	return devm_snd_soc_register_deferrable_card(dev, card);
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

MODULE_DESCRIPTION("Intel DMIC machine driver");
MODULE_LICENSE("GPL");
