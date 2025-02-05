// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2023 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

/*
 *  soc_sdw_cs_amp - Helpers to handle CS35L56 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include <sound/soc_sdw_utils.h>

#define CODEC_NAME_SIZE	8
#define CS_AMP_CHANNELS_PER_AMP	4

int asoc_sdw_cs_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	const char *dai_name = rtd->dai_link->codecs->dai_name;
	struct snd_soc_card *card = rtd->card;
	char codec_name[CODEC_NAME_SIZE];
	char widget_name[16];
	struct snd_soc_dapm_route route = { "Speaker", NULL, widget_name };
	struct snd_soc_dai *codec_dai;
	int i, ret;

	snprintf(codec_name, CODEC_NAME_SIZE, "%s", dai_name);
	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:%s",
					  card->components, codec_name);
	if (!card->components)
		return -ENOMEM;

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
EXPORT_SYMBOL_NS(asoc_sdw_cs_spk_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs_spk_feedback_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	const struct snd_soc_dai_link *dai_link = rtd->dai_link;
	const struct snd_soc_dai_link_ch_map *ch_map;
	const struct snd_soc_dai_link_component *codec_dlc;
	struct snd_soc_dai *codec_dai;
	u8 ch_slot[8] = {};
	unsigned int amps_per_bus, ch_per_amp, mask;
	int i, ret;

	WARN_ON(dai_link->num_cpus > ARRAY_SIZE(ch_slot));

	/*
	 * CS35L56 has 4 TX channels. When the capture is aggregated the
	 * same bus slots will be allocated to all the amps on a bus. Only
	 * one amp on that bus can be transmitting in each slot so divide
	 * the available 4 slots between all the amps on a bus.
	 */
	amps_per_bus = dai_link->num_codecs / dai_link->num_cpus;
	if ((amps_per_bus == 0) || (amps_per_bus > CS_AMP_CHANNELS_PER_AMP)) {
		dev_err(rtd->card->dev, "Illegal num_codecs:%u / num_cpus:%u\n",
			dai_link->num_codecs, dai_link->num_cpus);
		return -EINVAL;
	}

	ch_per_amp = CS_AMP_CHANNELS_PER_AMP / amps_per_bus;

	for_each_rtd_ch_maps(rtd, i, ch_map) {
		codec_dlc = snd_soc_link_to_codec(rtd->dai_link, i);
		codec_dai = snd_soc_find_dai(codec_dlc);
		mask = GENMASK(ch_per_amp - 1, 0) << ch_slot[ch_map->cpu];

		ret = snd_soc_dai_set_tdm_slot(codec_dai, 0, mask, 4, 32);
		if (ret < 0) {
			dev_err(rtd->card->dev, "Failed to set TDM slot:%d\n", ret);
			return ret;
		}

		ch_slot[ch_map->cpu] += ch_per_amp;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs_spk_feedback_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback)
{
	/* Do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs_amp_init, "SND_SOC_SDW_UTILS");
