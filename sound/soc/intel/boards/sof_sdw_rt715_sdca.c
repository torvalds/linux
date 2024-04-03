// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_rt715_sdca - Helpers to handle RT715-SDCA from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof_sdw_common.h"

int rt715_sdca_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s mic:rt715-sdca",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	return 0;
}

