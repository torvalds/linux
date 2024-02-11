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

SND_SOC_DAILINK_DEF(dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));
SND_SOC_DAILINK_DEF(probe_cp, DAILINK_COMP_ARRAY(COMP_CPU("Probe Extraction CPU DAI")));
SND_SOC_DAILINK_DEF(platform, DAILINK_COMP_ARRAY(COMP_PLATFORM("probe-platform")));

static struct snd_soc_dai_link probe_mb_dai_links[] = {
	{
		.name = "Compress Probe Capture",
		.nonatomic = 1,
		SND_SOC_DAILINK_REG(probe_cp, dummy, platform),
	},
};

static int avs_probe_mb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	int ret;

	mach = dev_get_platdata(dev);

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = "avs_probe_mb";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = probe_mb_dai_links;
	card->num_links = ARRAY_SIZE(probe_mb_dai_links);
	card->fully_routed = true;

	ret = snd_soc_fixup_dai_links_platform_name(card, mach->mach_params.platform);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static const struct platform_device_id avs_probe_mb_driver_ids[] = {
	{
		.name = "avs_probe_mb",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, avs_probe_mb_driver_ids);

static struct platform_driver avs_probe_mb_driver = {
	.probe = avs_probe_mb_probe,
	.driver = {
		.name = "avs_probe_mb",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = avs_probe_mb_driver_ids,
};

module_platform_driver(avs_probe_mb_driver);

MODULE_LICENSE("GPL");
