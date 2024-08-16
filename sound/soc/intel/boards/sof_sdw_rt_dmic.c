// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 Intel Corporation

/*
 * sof_sdw_rt_dmic - Helpers to handle Realtek SDW DMIC from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof_board_helpers.h"
#include "sof_sdw_common.h"

int rt_dmic_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_component *component;
	char *mic_name;

	component = dai->component;

	/*
	 * rt715-sdca (aka rt714) is a special case that uses different name in card->components
	 * and component->name_prefix.
	 */
	if (!strcmp(component->name_prefix, "rt714"))
		mic_name = devm_kasprintf(card->dev, GFP_KERNEL, "rt715-sdca");
	else
		mic_name = devm_kasprintf(card->dev, GFP_KERNEL, "%s", component->name_prefix);

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s mic:%s", card->components,
					  mic_name);
	if (!card->components)
		return -ENOMEM;

	dev_dbg(card->dev, "card->components: %s\n", card->components);

	return 0;
}
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
