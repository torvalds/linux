// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Intel Corporation

/*
 *  sof_sdw_rt722_sdca - Helpers to handle RT722-SDCA from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "sof_sdw_common.h"

static const struct snd_soc_dapm_widget generic_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route rt722_spk_map[] = {
	{ "Speaker", NULL, "rt722 SPK" },
};

static const struct snd_kcontrol_new generic_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

int rt722_spk_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:rt722",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_add_card_controls(card, generic_spk_controls,
					ARRAY_SIZE(generic_spk_controls));
	if (ret) {
		dev_err(card->dev, "failed to add rt722 spk controls: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, generic_spk_widgets,
					ARRAY_SIZE(generic_spk_widgets));
	if (ret) {
		dev_err(card->dev, "failed to add rt722 spk widgets: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, rt722_spk_map, ARRAY_SIZE(rt722_spk_map));
	if (ret)
		dev_err(rtd->dev, "failed to add rt722 spk map: %d\n", ret);

	return ret;
}

