// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2023 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

/*
 *  soc_sdw_rt722_sdca - Helpers to handle RT722-SDCA from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <sound/soc_sdw_utils.h>

static const struct snd_soc_dapm_route rt722_spk_map[] = {
	{ "Speaker", NULL, "rt722 SPK" },
};

int asoc_sdw_rt722_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:rt722",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_add_routes(&card->dapm, rt722_spk_map, ARRAY_SIZE(rt722_spk_map));
	if (ret)
		dev_err(rtd->dev, "failed to add rt722 spk map: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_rt722_spk_rtd_init, SND_SOC_SDW_UTILS);
