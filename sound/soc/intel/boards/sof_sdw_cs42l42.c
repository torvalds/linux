// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Intel Corporation

/*
 *  sof_sdw_cs42l42 - Helpers to handle CS42L42 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include "sof_sdw_common.h"

static const struct snd_soc_dapm_widget cs42l42_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route cs42l42_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone", NULL, "cs42l42 HP"},

	/* other jacks */
	{"cs42l42 HS", NULL, "Headset Mic"},
};

static const struct snd_kcontrol_new cs42l42_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static struct snd_soc_jack_pin cs42l42_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static int cs42l42_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_jack *jack;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s hs:cs42l42",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_add_card_controls(card, cs42l42_controls,
					ARRAY_SIZE(cs42l42_controls));
	if (ret) {
		dev_err(card->dev, "cs42l42 control addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, cs42l42_widgets,
					ARRAY_SIZE(cs42l42_widgets));
	if (ret) {
		dev_err(card->dev, "cs42l42 widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, cs42l42_map,
				      ARRAY_SIZE(cs42l42_map));

	if (ret) {
		dev_err(card->dev, "cs42l42 map addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 &ctx->sdw_headset,
					 cs42l42_jack_pins,
					 ARRAY_SIZE(cs42l42_jack_pins));
	if (ret) {
		dev_err(rtd->card->dev, "Headset Jack creation failed: %d\n",
			ret);
		return ret;
	}

	jack = &ctx->sdw_headset;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret)
		dev_err(rtd->card->dev, "Headset Jack call-back failed: %d\n",
			ret);

	return ret;
}

int sof_sdw_cs42l42_init(struct snd_soc_card *card,
			 const struct snd_soc_acpi_link_adr *link,
			 struct snd_soc_dai_link *dai_links,
			 struct sof_sdw_codec_info *info,
			 bool playback)
{
	/*
	 * headset should be initialized once.
	 * Do it with dai link for playback.
	 */
	if (!playback)
		return 0;

	dai_links->init = cs42l42_rtd_init;

	return 0;
}
