// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Intel Corporation

/*
 *  sof_sdw_cs_amp - Helpers to handle CS35L56 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include "sof_sdw_common.h"

#define CODEC_NAME_SIZE	8

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_SPK("Speakers", NULL),
};

int cs_spk_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	const char *dai_name = rtd->dai_link->codecs->dai_name;
	struct snd_soc_card *card = rtd->card;
	char codec_name[CODEC_NAME_SIZE];
	char widget_name[16];
	struct snd_soc_dapm_route route = { "Speakers", NULL, widget_name };
	struct snd_soc_dai *codec_dai;
	int i, ret;

	snprintf(codec_name, CODEC_NAME_SIZE, "%s", dai_name);
	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:%s",
					  card->components, codec_name);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_new_controls(&card->dapm, sof_widgets,
					ARRAY_SIZE(sof_widgets));
	if (ret) {
		dev_err(card->dev, "widgets addition failed: %d\n", ret);
		return ret;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (!strstr(codec_dai->name, "cs35l56"))
			continue;

		snprintf(widget_name, sizeof(widget_name), "%s SPK",
			 codec_dai->component->name_prefix);
		ret = snd_soc_dapm_add_routes(&card->dapm, &route, 1);
		if (ret)
			return ret;
	}

	return 0;
}

int sof_sdw_cs_amp_init(struct snd_soc_card *card,
			const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback)
{
	/* Do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;

	return 0;
}
