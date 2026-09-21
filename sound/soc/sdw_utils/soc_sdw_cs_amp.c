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

#define CS35L56_SPK_VOLUME_0DB 400 /* 0dB Max */

int asoc_sdw_cs35l56_volume_limit(struct snd_soc_card *card, const char *name_prefix)
{
	char *volume_ctl_name;
	int ret;

	volume_ctl_name = kasprintf(GFP_KERNEL, "%s Speaker Volume", name_prefix);
	if (!volume_ctl_name)
		return -ENOMEM;

	ret = snd_soc_limit_volume(card, volume_ctl_name, CS35L56_SPK_VOLUME_0DB);
	if (ret)
		dev_err(card->dev, "%s limit set failed: %d\n", volume_ctl_name, ret);

	kfree(volume_ctl_name);
	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs35l56_volume_limit, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	char widget_name[16];
	struct snd_soc_dapm_route route = { "Speaker", NULL, widget_name };
	struct snd_soc_dai *codec_dai;
	int i, ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (!strstr(codec_dai->name, "cs35l56"))
			continue;

		snprintf(widget_name, sizeof(widget_name), "%s SPK",
			 codec_dai->component->name_prefix);

		ret = asoc_sdw_cs35l56_volume_limit(card, codec_dai->component->name_prefix);
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm, &route, 1);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs_spk_rtd_init, "SND_SOC_SDW_UTILS");

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
