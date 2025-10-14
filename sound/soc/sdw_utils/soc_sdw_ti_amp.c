// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Texas Instruments Inc.

/*
 *  soc_sdw_ti_amp - Helpers to handle TI's soundwire based codecs
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include <sound/soc_sdw_utils.h>

#define TIAMP_SPK_VOLUME_0DB		200

int asoc_sdw_ti_amp_initial_settings(struct snd_soc_card *card,
				     const char *name_prefix)
{
	char *volume_ctl_name;
	int ret;

	volume_ctl_name = kasprintf(GFP_KERNEL, "%s Speaker Volume",
				    name_prefix);
	if (!volume_ctl_name)
		return -ENOMEM;

	ret = snd_soc_limit_volume(card, volume_ctl_name,
				   TIAMP_SPK_VOLUME_0DB);
	if (ret)
		dev_err(card->dev,
			"%s update failed %d\n",
			volume_ctl_name, ret);

	kfree(volume_ctl_name);
	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_ti_amp_initial_settings, "SND_SOC_SDW_UTILS");

int asoc_sdw_ti_spk_rtd_init(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	char widget_name[16];
	char speaker[16];
	struct snd_soc_dapm_route route = {speaker, NULL, widget_name};
	struct snd_soc_dai *codec_dai;
	const char *prefix;
	int i, ret = 0;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (!strstr(codec_dai->name, "tas2783"))
			continue;

		prefix = codec_dai->component->name_prefix;
		if (!strncmp(prefix, "tas2783-1", strlen("tas2783-1"))) {
			strscpy(speaker, "Left Spk", sizeof(speaker));
		} else if (!strncmp(prefix, "tas2783-2", strlen("tas2783-2"))) {
			strscpy(speaker, "Right Spk", sizeof(speaker));
		} else {
			ret = -EINVAL;
			dev_err(card->dev, "unhandled prefix %s", prefix);
			break;
		}

		snprintf(widget_name, sizeof(widget_name), "%s SPK", prefix);
		ret = asoc_sdw_ti_amp_initial_settings(card, prefix);
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(&card->dapm, &route, 1);
		if (ret)
			return ret;
	}

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_ti_spk_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_ti_amp_init(struct snd_soc_card *card,
			 struct snd_soc_dai_link *dai_links,
			 struct asoc_sdw_codec_info *info,
			 bool playback)
{
	if (!playback)
		return 0;

	info->amp_num++;

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_ti_amp_init, "SND_SOC_SDW_UTILS");
