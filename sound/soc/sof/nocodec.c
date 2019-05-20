// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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
#include "sof-priv.h"

static struct snd_soc_card sof_nocodec_card = {
	.name = "nocodec", /* the sof- prefix is added by the core */
};

static int sof_nocodec_bes_setup(struct device *dev,
				 const struct snd_sof_dsp_ops *ops,
				 struct snd_soc_dai_link *links,
				 int link_num, struct snd_soc_card *card)
{
	int i;

	if (!ops || !links || !card)
		return -EINVAL;

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		links[i].name = devm_kasprintf(dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpu_dai_name = ops->drv[i].name;
		links[i].platform_name = dev_name(dev);
		links[i].codec_dai_name = "snd-soc-dummy-dai";
		links[i].codec_name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = link_num;

	return 0;
}

int sof_nocodec_setup(struct device *dev,
		      struct snd_sof_pdata *sof_pdata,
		      struct snd_soc_acpi_mach *mach,
		      const struct sof_dev_desc *desc,
		      const struct snd_sof_dsp_ops *ops)
{
	struct snd_soc_dai_link *links;
	int ret;

	if (!mach)
		return -EINVAL;

	sof_pdata->drv_name = "sof-nocodec";

	mach->drv_name = "sof-nocodec";
	sof_pdata->fw_filename = desc->nocodec_fw_filename;
	sof_pdata->tplg_filename = desc->nocodec_tplg_filename;

	/* create dummy BE dai_links */
	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) *
			     ops->num_drv, GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	ret = sof_nocodec_bes_setup(dev, ops, links, ops->num_drv,
				    &sof_nocodec_card);
	return ret;
}
EXPORT_SYMBOL(sof_nocodec_setup);

static int sof_nocodec_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &sof_nocodec_card;

	card->dev = &pdev->dev;

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static int sof_nocodec_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sof_nocodec_audio = {
	.probe = sof_nocodec_probe,
	.remove = sof_nocodec_remove,
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
