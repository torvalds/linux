// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2025 Intel Corporation
#include <linux/module.h>
#include <linux/string.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/sof.h>
#include <uapi/sound/asound.h>
#include "../common/soc-intel-quirks.h"
#include "sof_ti_common.h"

/*
 * Texas Instruments TAS2563 just mount one device to manage multiple devices,
 * so the kcontrols, widgets and routes just keep one item, respectively.
 */
static const struct snd_kcontrol_new tas2563_spk_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget tas2563_spk_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_route tas2563_spk_dapm_routes[] = {
	{ "Spk", NULL, "OUT" },
};

static struct snd_soc_dai_link_component tas2563_dai_link_components[] = {
	{
		.name = TAS2563_DEV0_NAME,
		.dai_name = TAS2563_CODEC_DAI,
	},
};

static int tas2563_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	int ret;

	ret = snd_soc_dapm_new_controls(dapm, tas2563_spk_dapm_widgets,
					ARRAY_SIZE(tas2563_spk_dapm_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add dapm widgets, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, tas2563_spk_kcontrols,
					ARRAY_SIZE(tas2563_spk_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "unable to add controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(dapm, tas2563_spk_dapm_routes,
				      ARRAY_SIZE(tas2563_spk_dapm_routes));
	if (ret)
		dev_err(rtd->dev, "unable to add dapm routes, ret %d\n", ret);

	return ret;
}

void sof_tas2563_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = tas2563_dai_link_components;
	link->num_codecs = ARRAY_SIZE(tas2563_dai_link_components);
	link->init = tas2563_init;
}
EXPORT_SYMBOL_NS(sof_tas2563_dai_link, "SND_SOC_INTEL_SOF_TI_COMMON");

MODULE_DESCRIPTION("ASoC Intel SOF Texas Instruments helpers");
MODULE_LICENSE("GPL");
