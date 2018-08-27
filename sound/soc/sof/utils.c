// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//

#include <linux/device.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"

int sof_bes_setup(struct device *dev, struct snd_sof_dsp_ops *ops,
		  struct snd_soc_dai_link *links, int link_num,
		  struct snd_soc_card *card)
{
	char name[32];
	int i;

	if (!ops || !links || !card)
		return -EINVAL;

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		snprintf(name, 32, "NoCodec-%d", i);
		links[i].name = devm_kstrdup(dev, name, GFP_KERNEL);
		if (!links[i].name)
			return -ENOMEM;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpu_dai_name = ops->drv[i].name;
		links[i].platform_name = "sof-audio";
		links[i].codec_dai_name = "snd-soc-dummy-dai";
		links[i].codec_name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = link_num;

	return 0;
}
EXPORT_SYMBOL(sof_bes_setup);

/* register sof platform device */
int sof_create_platform_device(struct sof_platform_priv *priv)
{
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;
	struct device *dev = sof_pdata->dev;

	priv->pdev_pcm =
		platform_device_register_data(dev, "sof-audio", -1,
					      sof_pdata, sizeof(*sof_pdata));
	if (IS_ERR(priv->pdev_pcm)) {
		dev_err(dev, "Cannot register device sof-audio. Error %d\n",
			(int)PTR_ERR(priv->pdev_pcm));
		return PTR_ERR(priv->pdev_pcm);
	}

	return 0;
}
EXPORT_SYMBOL(sof_create_platform_device);
