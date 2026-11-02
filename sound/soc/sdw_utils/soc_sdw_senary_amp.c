// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2022 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc_sdw_utils.h>

static int senary_amp_add_device_props(struct device *sdw_dev)
{
	return 0;
}

int asoc_sdw_senary_amp_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->amp_dev1) {
		device_remove_software_node(ctx->amp_dev1);
		put_device(ctx->amp_dev1);
	}

	if (ctx->amp_dev2) {
		device_remove_software_node(ctx->amp_dev2);
		put_device(ctx->amp_dev2);
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_senary_amp_exit, "SND_SOC_SDW_UTILS");

int asoc_sdw_senary_amp_init(struct snd_soc_card *card,
			     struct snd_soc_dai_link *dai_links,
			     struct asoc_sdw_codec_info *info,
			     bool playback)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct device *sdw_dev1, *sdw_dev2;
	int ret;

	/* Count amp number and do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;

	if (info->amp_num == 2) {
		sdw_dev1 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[0].name);
		if (!sdw_dev1)
			return -EPROBE_DEFER;

		ret = senary_amp_add_device_props(sdw_dev1);
		if (ret < 0) {
			put_device(sdw_dev1);
			return ret;
		}
		ctx->amp_dev1 = sdw_dev1;

		sdw_dev2 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[1].name);
		if (!sdw_dev2)
			return -EPROBE_DEFER;

		ret = senary_amp_add_device_props(sdw_dev2);
		if (ret < 0) {
			put_device(sdw_dev2);
			return ret;
		}
		ctx->amp_dev2 = sdw_dev2;
	}

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_senary_amp_init, "SND_SOC_SDW_UTILS");
