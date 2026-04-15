// SPDX-License-Identifier: GPL-2.0-only
// Based on sof_sdw_cs42l45.c
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2023 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

/*
 *  soc_sdw_cs47l47 - Helpers to handle CS47L47 from generic machine driver
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-card.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>
#include <sound/soc_sdw_utils.h>

static struct snd_soc_jack_pin soc_jack_pins[] = {
	{
		.pin    = "cs47l47 OT 43 Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "cs47l47 OT 45 Headset",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "cs47l47 IT 31 Microphone",
		.mask   = SND_JACK_MICROPHONE,
	},
	{
		.pin    = "cs47l47 IT 33 Headset",
		.mask   = SND_JACK_MICROPHONE,
	},
};

int asoc_sdw_cs47l47_hs_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_jack *jack = &ctx->sdw_headset;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s hs:cs47l47",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_card_jack_new_pins(card, "Jack", SND_JACK_MECHANICAL |
					 SND_JACK_HEADSET | SND_JACK_LINEOUT, jack,
					 soc_jack_pins, ARRAY_SIZE(soc_jack_pins));
	if (ret) {
		dev_err(card->dev, "Failed to create jack: %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(card->dev, "Failed to register jack: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs47l47_hs_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs47l47_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s mic:cs47l47-dmic",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs47l47_dmic_rtd_init, "SND_SOC_SDW_UTILS");
