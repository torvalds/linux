// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/module.h>
#include <sound/sof.h>
#include "sof-audio.h"
#include "sof-priv.h"

static struct snd_soc_card sof_nocodec_card = {
	.name = "nocodec", /* the sof- prefix is added by the core */
	.topology_shortname = "sof-nocodec",
	.owner = THIS_MODULE
};

static int sof_nocodec_bes_setup(struct device *dev,
				 struct snd_soc_dai_driver *drv,
				 struct snd_soc_dai_link *links,
				 int link_num, struct snd_soc_card *card)
{
	struct snd_soc_dai_link_component *dlc;
	int i;

	if (!drv || !links || !card)
		return -EINVAL;

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		dlc = devm_kcalloc(dev, 3, sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		links[i].name = devm_kasprintf(dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		links[i].stream_name = links[i].name;

		links[i].cpus = &dlc[0];
		links[i].codecs = &dlc[1];
		links[i].platforms = &dlc[2];

		links[i].num_cpus = 1;
		links[i].num_codecs = 1;
		links[i].num_platforms = 1;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpus->dai_name = drv[i].name;
		links[i].platforms->name = dev_name(dev->parent);
		links[i].codecs->dai_name = "snd-soc-dummy-dai";
		links[i].codecs->name = "snd-soc-dummy";
		if (drv[i].playback.channels_min)
			links[i].dpcm_playback = 1;
		if (drv[i].capture.channels_min)
			links[i].dpcm_capture = 1;

		links[i].be_hw_params_fixup = sof_pcm_dai_link_fixup;
	}

	card->dai_link = links;
	card->num_links = link_num;

	return 0;
}

static int sof_nocodec_setup(struct device *dev,
			     u32 num_dai_drivers,
			     struct snd_soc_dai_driver *dai_drivers)
{
	struct snd_soc_dai_link *links;

	/* create dummy BE dai_links */
	links = devm_kcalloc(dev, num_dai_drivers, sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	return sof_nocodec_bes_setup(dev, dai_drivers, links, num_dai_drivers, &sof_nocodec_card);
}

static int sof_nocodec_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &sof_nocodec_card;
	struct snd_soc_acpi_mach *mach;
	int ret;

	card->dev = &pdev->dev;
	card->topology_shortname_created = true;
	mach = pdev->dev.platform_data;

	ret = sof_nocodec_setup(card->dev, mach->mach_params.num_dai_drivers,
				mach->mach_params.dai_drivers);
	if (ret < 0)
		return ret;

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver sof_nocodec_audio = {
	.probe = sof_nocodec_probe,
	.driver = {
		.name = "sof-nocodec",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(sof_nocodec_audio)

MODULE_DESCRIPTION("ASoC sof nocodec");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-nocodec");
