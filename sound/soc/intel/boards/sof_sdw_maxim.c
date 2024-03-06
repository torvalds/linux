// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation
//
// sof_sdw_maxim - Helpers to handle maxim codecs
// codec devices from generic machine driver

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "sof_sdw_common.h"
#include "sof_maxim_common.h"

static int maxim_part_id;
#define SOF_SDW_PART_ID_MAX98363 0x8363
#define SOF_SDW_PART_ID_MAX98373 0x8373

static const struct snd_soc_dapm_widget maxim_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new maxim_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static int spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:mx%04x",
					  card->components, maxim_part_id);
	if (!card->components)
		return -ENOMEM;

	dev_dbg(card->dev, "soundwire maxim card components assigned : %s\n",
		card->components);

	ret = snd_soc_add_card_controls(card, maxim_controls,
					ARRAY_SIZE(maxim_controls));
	if (ret) {
		dev_err(card->dev, "mx%04x ctrls addition failed: %d\n", maxim_part_id, ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, maxim_widgets,
					ARRAY_SIZE(maxim_widgets));
	if (ret) {
		dev_err(card->dev, "mx%04x widgets addition failed: %d\n", maxim_part_id, ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, max_98373_dapm_routes, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add first SPK map: %d\n", ret);

	return ret;
}

static int mx8373_enable_spk_pin(struct snd_pcm_substream *substream, bool enable)
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

static int mx8373_sdw_prepare(struct snd_pcm_substream *substream)
{
	int ret;

	/* according to soc_pcm_prepare dai link prepare is called first */
	ret = sdw_prepare(substream);
	if (ret < 0)
		return ret;

	return mx8373_enable_spk_pin(substream, true);
}

static int mx8373_sdw_hw_free(struct snd_pcm_substream *substream)
{
	int ret;

	/* according to soc_pcm_hw_free dai link free is called first */
	ret = sdw_hw_free(substream);
	if (ret < 0)
		return ret;

	return mx8373_enable_spk_pin(substream, false);
}

static const struct snd_soc_ops max_98373_sdw_ops = {
	.startup = sdw_startup,
	.prepare = mx8373_sdw_prepare,
	.trigger = sdw_trigger,
	.hw_params = sdw_hw_params,
	.hw_free = mx8373_sdw_hw_free,
	.shutdown = sdw_shutdown,
};

static int mx8373_sdw_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm = &card->dapm;

	/* Disable Left and Right Spk pin after boot */
	snd_soc_dapm_disable_pin(dapm, "Left Spk");
	snd_soc_dapm_disable_pin(dapm, "Right Spk");
	return snd_soc_dapm_sync(dapm);
}

int sof_sdw_maxim_init(struct snd_soc_card *card,
		       const struct snd_soc_acpi_link_adr *link,
		       struct snd_soc_dai_link *dai_links,
		       struct sof_sdw_codec_info *info,
		       bool playback)
{
	info->amp_num++;
	if (info->amp_num == 2)
		dai_links->init = spk_init;

	maxim_part_id = info->part_id;
	switch (maxim_part_id) {
	case SOF_SDW_PART_ID_MAX98363:
		/* Default ops are set in function init_dai_link.
		 * called as part of function create_sdw_dailink
		 */
		break;
	case SOF_SDW_PART_ID_MAX98373:
		info->codec_card_late_probe = mx8373_sdw_late_probe;
		dai_links->ops = &max_98373_sdw_ops;
		break;
	default:
		dev_err(card->dev, "Invalid maxim_part_id %#x\n", maxim_part_id);
		return -EINVAL;
	}
	return 0;
}
