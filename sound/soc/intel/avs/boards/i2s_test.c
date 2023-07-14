// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>

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
	if (!dl->name || !dl->cpus)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", ssp_port);
	dl->codecs = &asoc_dummy_dlc;
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_create_dapm_routes(struct device *dev, int ssp_port,
				  struct snd_soc_dapm_route **routes, int *num_routes)
{
	struct snd_soc_dapm_route *dr;
	const int num_dr = 2;

	dr = devm_kcalloc(dev, num_dr, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	dr[0].sink = devm_kasprintf(dev, GFP_KERNEL, "ssp%dpb", ssp_port);
	dr[0].source = devm_kasprintf(dev, GFP_KERNEL, "ssp%d Tx", ssp_port);
	if (!dr[0].sink || !dr[0].source)
		return -ENOMEM;

	dr[1].sink = devm_kasprintf(dev, GFP_KERNEL, "ssp%d Rx", ssp_port);
	dr[1].source = devm_kasprintf(dev, GFP_KERNEL, "ssp%dcp", ssp_port);
	if (!dr[1].sink || !dr[1].source)
		return -ENOMEM;

	*routes = dr;
	*num_routes = num_dr;

	return 0;
}

static int avs_create_dapm_widgets(struct device *dev, int ssp_port,
				   struct snd_soc_dapm_widget **widgets, int *num_widgets)
{
	struct snd_soc_dapm_widget *dw;
	const int num_dw = 2;

	dw = devm_kcalloc(dev, num_dw, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw[0].id = snd_soc_dapm_hp;
	dw[0].reg = SND_SOC_NOPM;
	dw[0].name = devm_kasprintf(dev, GFP_KERNEL, "ssp%dpb", ssp_port);
	if (!dw[0].name)
		return -ENOMEM;

	dw[1].id = snd_soc_dapm_mic;
	dw[1].reg = SND_SOC_NOPM;
	dw[1].name = devm_kasprintf(dev, GFP_KERNEL, "ssp%dcp", ssp_port);
	if (!dw[1].name)
		return -ENOMEM;

	*widgets = dw;
	*num_widgets = num_dw;

	return 0;
}

static int avs_i2s_test_probe(struct platform_device *pdev)
{
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *routes;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	const char *pname;
	int num_routes, num_widgets;
	int ssp_port, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;
	ssp_port = __ffs(mach->mach_params.i2s_link_mask);

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = devm_kasprintf(dev, GFP_KERNEL, "ssp%d-loopback", ssp_port);
	if (!card->name)
		return -ENOMEM;

	ret = avs_create_dai_link(dev, pname, ssp_port, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d\n", ret);
		return ret;
	}

	ret = avs_create_dapm_routes(dev, ssp_port, &routes, &num_routes);
	if (ret) {
		dev_err(dev, "Failed to create dapm routes: %d\n", ret);
		return ret;
	}

	ret = avs_create_dapm_widgets(dev, ssp_port, &widgets, &num_widgets);
	if (ret) {
		dev_err(dev, "Failed to create dapm widgets: %d\n", ret);
		return ret;
	}

	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->dapm_routes = routes;
	card->num_dapm_routes = num_routes;
	card->dapm_widgets = widgets;
	card->num_dapm_widgets = num_widgets;
	card->fully_routed = true;

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static struct platform_driver avs_i2s_test_driver = {
	.probe = avs_i2s_test_probe,
	.driver = {
		.name = "avs_i2s_test",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_i2s_test_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_i2s_test");
