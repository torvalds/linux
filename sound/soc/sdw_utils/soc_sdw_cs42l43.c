// SPDX-License-Identifier: GPL-2.0-only
// Based on sof_sdw_rt5682.c
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2023 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

/*
 *  soc_sdw_cs42l43 - Helpers to handle CS42L43 from generic machine driver
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
#include <sound/soc_sdw_utils.h>

#define CS42L43_SPK_VOLUME_0DB	128 /* 0dB Max */

static const struct snd_soc_dapm_route cs42l43_hs_map[] = {
	{ "Headphone", NULL, "cs42l43 AMP3_OUT" },
	{ "Headphone", NULL, "cs42l43 AMP4_OUT" },
	{ "cs42l43 ADC1_IN1_P", NULL, "Headset Mic" },
	{ "cs42l43 ADC1_IN1_N", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_route cs42l43_spk_map[] = {
	{ "Speaker", NULL, "cs42l43 AMP1_OUT_P", },
	{ "Speaker", NULL, "cs42l43 AMP1_OUT_N", },
	{ "Speaker", NULL, "cs42l43 AMP2_OUT_P", },
	{ "Speaker", NULL, "cs42l43 AMP2_OUT_N", },
};

static const struct snd_soc_dapm_route cs42l43_dmic_map[] = {
	{ "cs42l43 PDM1_DIN", NULL, "DMIC" },
	{ "cs42l43 PDM2_DIN", NULL, "DMIC" },
};

static struct snd_soc_jack_pin soc_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

int asoc_sdw_cs42l43_hs_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &ctx->sdw_headset;
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s hs:cs42l43",
					  card->components);
	if (!card->components)
		return -ENOMEM;

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
					 jack, soc_jack_pins,
					 ARRAY_SIZE(soc_jack_pins));
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
EXPORT_SYMBOL_NS(asoc_sdw_cs42l43_hs_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs42l43_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret;

	if (!(ctx->mc_quirk & SOC_SDW_SIDECAR_AMPS)) {
		/* Will be set by the bridge code in this case */
		card->components = devm_kasprintf(card->dev, GFP_KERNEL,
						  "%s spk:cs42l43-spk",
						  card->components);
		if (!card->components)
			return -ENOMEM;
	}

	ret = snd_soc_limit_volume(card, "cs42l43 Speaker Digital Volume",
				   CS42L43_SPK_VOLUME_0DB);
	if (ret)
		dev_err(card->dev, "cs42l43 speaker volume limit failed: %d\n", ret);
	else
		dev_info(card->dev, "Setting CS42L43 Speaker volume limit to %d\n",
			 CS42L43_SPK_VOLUME_0DB);

	ret = snd_soc_dapm_add_routes(&card->dapm, cs42l43_spk_map,
				      ARRAY_SIZE(cs42l43_spk_map));
	if (ret)
		dev_err(card->dev, "cs42l43 speaker map addition failed: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs42l43_spk_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs42l43_spk_init(struct snd_soc_card *card,
			      struct snd_soc_dai_link *dai_links,
			      struct asoc_sdw_codec_info *info,
			      bool playback)
{
	/* Do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;

	return asoc_sdw_bridge_cs35l56_spk_init(card, dai_links, info, playback);
}
EXPORT_SYMBOL_NS(asoc_sdw_cs42l43_spk_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_cs42l43_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL, "%s mic:cs42l43-dmic",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_dapm_add_routes(&card->dapm, cs42l43_dmic_map,
				      ARRAY_SIZE(cs42l43_dmic_map));
	if (ret)
		dev_err(card->dev, "cs42l43 dmic map addition failed: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_cs42l43_dmic_rtd_init, "SND_SOC_SDW_UTILS");
