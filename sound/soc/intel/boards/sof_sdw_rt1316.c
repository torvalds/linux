// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_rt1316 - Helpers to handle RT1316 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "sof_sdw_common.h"

static const struct snd_soc_dapm_widget rt1316_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

/*
 * dapm routes for rt1316 will be registered dynamically according
 * to the number of rt1316 used. The first two entries will be registered
 * for one codec case, and the last two entries are also registered
 * if two 1316s are used.
 */
static const struct snd_soc_dapm_route rt1316_map[] = {
	{ "Speaker", NULL, "rt1316-1 SPOL" },
	{ "Speaker", NULL, "rt1316-1 SPOR" },
	{ "Speaker", NULL, "rt1316-2 SPOL" },
	{ "Speaker", NULL, "rt1316-2 SPOR" },
};

static const struct snd_kcontrol_new rt1316_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static int first_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:rt1316",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_add_card_controls(card, rt1316_controls,
					ARRAY_SIZE(rt1316_controls));
	if (ret) {
		dev_err(card->dev, "rt1316 controls addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, rt1316_widgets,
					ARRAY_SIZE(rt1316_widgets));
	if (ret) {
		dev_err(card->dev, "rt1316 widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, rt1316_map, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add first SPK map: %d\n", ret);

	return ret;
}

static int second_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, rt1316_map + 2, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add second SPK map: %d\n", ret);

	return ret;
}

static int all_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	ret = first_spk_init(rtd);
	if (ret)
		return ret;

	return second_spk_init(rtd);
}

int sof_sdw_rt1316_init(struct snd_soc_card *card,
			const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback)
{
	/* Count amp number and do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;
	if (info->amp_num == 1)
		dai_links->init = first_spk_init;

	if (info->amp_num == 2) {
		/*
		 * if two 1316s are in one dai link, the init function
		 * in this dai link will be first set for the first speaker,
		 * and it should be reset to initialize all speakers when
		 * the second speaker is found.
		 */
		if (dai_links->init)
			dai_links->init = all_spk_init;
		else
			dai_links->init = second_spk_init;
	}

	return 0;
}
