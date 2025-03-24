// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file defines data structures and functions used in Machine
 * Driver for Intel platforms with Nuvoton Codecs.
 *
 * Copyright 2023 Intel Corporation.
 */
#include <linux/module.h>
#include <sound/sof.h>
#include "sof_nuvoton_common.h"

/*
 * Nuvoton NAU8318
 */
static const struct snd_kcontrol_new nau8318_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget nau8318_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_route nau8318_routes[] = {
	{ "Spk", NULL, "Speaker" },
};

static struct snd_soc_dai_link_component nau8318_components[] = {
	{
		.name = NAU8318_DEV0_NAME,
		.dai_name = NAU8318_CODEC_DAI,
	}
};

static int nau8318_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, nau8318_widgets,
					ARRAY_SIZE(nau8318_widgets));
	if (ret) {
		dev_err(rtd->dev, "fail to add nau8318 widgets, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, nau8318_kcontrols,
					ARRAY_SIZE(nau8318_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "fail to add nau8318 kcontrols, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, nau8318_routes,
				      ARRAY_SIZE(nau8318_routes));

	if (ret) {
		dev_err(rtd->dev, "fail to add nau8318 routes, ret %d\n", ret);
		return ret;
	}

	return ret;
}

void nau8318_set_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = nau8318_components;
	link->num_codecs = ARRAY_SIZE(nau8318_components);
	link->init = nau8318_init;
}
EXPORT_SYMBOL_NS(nau8318_set_dai_link, "SND_SOC_INTEL_SOF_NUVOTON_COMMON");

MODULE_DESCRIPTION("ASoC Intel SOF Nuvoton helpers");
MODULE_LICENSE("GPL");
