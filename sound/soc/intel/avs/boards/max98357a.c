// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_route card_base_routes[] = {
	{ "Spk", NULL, "Speaker" },
};

static int avs_create_dai_link(struct device *dev, const char *platform_name, int ssp_port,
			       struct snd_soc_dai_link **dai_link)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;

	dl = devm_kzalloc(dev, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = platform_name;

	dl->name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", ssp_port);
	dl->cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", ssp_port);
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "MX98357A:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, "HiFi");
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_create_dapm_routes(struct device *dev, int ssp_port,
				  struct snd_soc_dapm_route **routes, int *num_routes)
{
	struct snd_soc_dapm_route *dr;
	const int num_base = ARRAY_SIZE(card_base_routes);
	const int num_dr = num_base + 1;
	int idx;

	dr = devm_kcalloc(dev, num_dr, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	memcpy(dr, card_base_routes, num_base * sizeof(*dr));

	idx = num_base;
	dr[idx].sink = devm_kasprintf(dev, GFP_KERNEL, "HiFi Playback");
	dr[idx].source = devm_kasprintf(dev, GFP_KERNEL, "ssp%d Tx", ssp_port);
	if (!dr[idx].sink || !dr[idx].source)
		return -ENOMEM;

	*routes = dr;
	*num_routes = num_dr;

	return 0;
}

static int avs_max98357a_probe(struct platform_device *pdev)
{
	struct snd_soc_dapm_route *routes;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	const char *pname;
	int num_routes, ssp_port, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;
	ssp_port = __ffs(mach->mach_params.i2s_link_mask);

	ret = avs_create_dai_link(dev, pname, ssp_port, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	ret = avs_create_dapm_routes(dev, ssp_port, &routes, &num_routes);
	if (ret) {
		dev_err(dev, "Failed to create dapm routes: %d", ret);
		return ret;
	}

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = "avs_max98357a";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->controls = card_controls;
	card->num_controls = ARRAY_SIZE(card_controls);
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = routes;
	card->num_dapm_routes = num_routes;
	card->fully_routed = true;

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static struct platform_driver avs_max98357a_driver = {
	.probe = avs_max98357a_probe,
	.driver = {
		.name = "avs_max98357a",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_max98357a_driver)

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_max98357a");
