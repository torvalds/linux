// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2020 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.
//
// soc_sdw_maxim - Helpers to handle maxim codecs
// codec devices from generic machine driver

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <sound/soc_sdw_utils.h>

static int maxim_part_id;
#define SOC_SDW_PART_ID_MAX98363 0x8363
#define SOC_SDW_PART_ID_MAX98373 0x8373

static const struct snd_soc_dapm_route max_98373_dapm_routes[] = {
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

int asoc_sdw_maxim_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, max_98373_dapm_routes, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add first SPK map: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_maxim_spk_rtd_init, "SND_SOC_SDW_UTILS");

static int asoc_sdw_mx8373_enable_spk_pin(struct snd_pcm_substream *substream, bool enable)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	int ret;
	int j;

	/* set spk pin by playback only */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		struct snd_soc_dapm_context *dapm =
				snd_soc_component_get_dapm(cpu_dai->component);
		char pin_name[16];

		snprintf(pin_name, ARRAY_SIZE(pin_name), "%s Spk",
			 codec_dai->component->name_prefix);

		if (enable)
			ret = snd_soc_dapm_enable_pin(dapm, pin_name);
		else
			ret = snd_soc_dapm_disable_pin(dapm, pin_name);

		if (!ret)
			snd_soc_dapm_sync(dapm);
	}

	return 0;
}

static int asoc_sdw_mx8373_prepare(struct snd_pcm_substream *substream)
{
	int ret;

	/* according to soc_pcm_prepare dai link prepare is called first */
	ret = asoc_sdw_prepare(substream);
	if (ret < 0)
		return ret;

	return asoc_sdw_mx8373_enable_spk_pin(substream, true);
}

static int asoc_sdw_mx8373_hw_free(struct snd_pcm_substream *substream)
{
	int ret;

	/* according to soc_pcm_hw_free dai link free is called first */
	ret = asoc_sdw_hw_free(substream);
	if (ret < 0)
		return ret;

	return asoc_sdw_mx8373_enable_spk_pin(substream, false);
}

static const struct snd_soc_ops max_98373_sdw_ops = {
	.startup = asoc_sdw_startup,
	.prepare = asoc_sdw_mx8373_prepare,
	.trigger = asoc_sdw_trigger,
	.hw_params = asoc_sdw_hw_params,
	.hw_free = asoc_sdw_mx8373_hw_free,
	.shutdown = asoc_sdw_shutdown,
};

static int asoc_sdw_mx8373_sdw_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm = &card->dapm;

	/* Disable Left and Right Spk pin after boot */
	snd_soc_dapm_disable_pin(dapm, "Left Spk");
	snd_soc_dapm_disable_pin(dapm, "Right Spk");
	return snd_soc_dapm_sync(dapm);
}

int asoc_sdw_maxim_init(struct snd_soc_card *card,
			struct snd_soc_dai_link *dai_links,
			struct asoc_sdw_codec_info *info,
			bool playback)
{
	info->amp_num++;

	maxim_part_id = info->part_id;
	switch (maxim_part_id) {
	case SOC_SDW_PART_ID_MAX98363:
		/* Default ops are set in function init_dai_link.
		 * called as part of function create_sdw_dailink
		 */
		break;
	case SOC_SDW_PART_ID_MAX98373:
		info->codec_card_late_probe = asoc_sdw_mx8373_sdw_late_probe;
		dai_links->ops = &max_98373_sdw_ops;
		break;
	default:
		dev_err(card->dev, "Invalid maxim_part_id %#x\n", maxim_part_id);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_maxim_init, "SND_SOC_SDW_UTILS");
