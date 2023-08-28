// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Intel Corporation

/*
 *  sof_sdw_cs_amp - Helpers to handle CS35L56 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof_sdw_common.h"

#define CODEC_NAME_SIZE	8

static int cs_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	const char *dai_name = rtd->dai_link->codecs->dai_name;
	struct snd_soc_card *card = rtd->card;
	char codec_name[CODEC_NAME_SIZE];

	snprintf(codec_name, CODEC_NAME_SIZE, "%s", dai_name);
	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:%s",
					  card->components, codec_name);
	if (!card->components)
		return -ENOMEM;

	return 0;
}


int sof_sdw_cs_amp_init(struct snd_soc_card *card,
			const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback)
{
	/* Count amp number and do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;
	dai_links->init = cs_spk_init;

	return 0;
}
