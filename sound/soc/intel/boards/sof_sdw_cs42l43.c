// SPDX-License-Identifier: GPL-2.0-only
// Based on sof_sdw_rt5682.c
// Copyright (c) 2023 Intel Corporation

/*
 *  sof_sdw_cs42l43 - Helpers to handle CS42L43 from generic machine driver
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <sound/jack.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/cs42l43.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "sof_sdw_common.h"

static const struct snd_soc_dapm_widget cs42l43_hs_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route cs42l43_hs_map[] = {
	{ "Headphone", NULL, "cs42l43 AMP3_OUT" },
	{ "Headphone", NULL, "cs42l43 AMP4_OUT" },
	{ "cs42l43 ADC1_IN1_P", NULL, "Headset Mic" },
	{ "cs42l43 ADC1_IN1_N", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_widget cs42l43_dmic_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const struct snd_soc_dapm_route cs42l43_dmic_map[] = {
	{ "cs42l43 PDM1_DIN", NULL, "DMIC" },
	{ "cs42l43 PDM2_DIN", NULL, "DMIC" },
};

static struct snd_soc_jack_pin sof_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

int cs42l43_hs_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &ctx->sdw_headset;
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s hs:cs42l43",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_new_controls(&card->dapm, cs42l43_hs_widgets,
					ARRAY_SIZE(cs42l43_hs_widgets));
	if (ret) {
		dev_err(card->dev, "cs42l43 hs widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, cs42l43_hs_map,
				      ARRAY_SIZE(cs42l43_hs_map));
	if (ret) {
		dev_err(card->dev, "cs42l43 hs map addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(card, "Jack",
					 SND_JACK_MECHANICAL | SND_JACK_AVOUT |
					 SND_JACK_HEADSET | SND_JACK_LINEOUT |
					 SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					 SND_JACK_BTN_2 | SND_JACK_BTN_3,
					 jack, sof_jack_pins,
					 ARRAY_SIZE(sof_jack_pins));
	if (ret) {
		dev_err(card->dev, "Failed to create jack: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(card->dev, "Failed to register jack: %d\n", ret);
		return ret;
	}

	ret = snd_soc_component_set_sysclk(component, CS42L43_SYSCLK, CS42L43_SYSCLK_SDW,
					   0, SND_SOC_CLOCK_IN);
	if (ret)
		dev_err(card->dev, "Failed to set sysclk: %d\n", ret);

	return ret;
}

int cs42l43_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s mic:cs42l43-dmic",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_new_controls(&card->dapm, cs42l43_dmic_widgets,
					ARRAY_SIZE(cs42l43_dmic_widgets));
	if (ret) {
		dev_err(card->dev, "cs42l43 dmic widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, cs42l43_dmic_map,
				      ARRAY_SIZE(cs42l43_dmic_map));
	if (ret)
		dev_err(card->dev, "cs42l43 dmic map addition failed: %d\n", ret);

	return ret;
}

