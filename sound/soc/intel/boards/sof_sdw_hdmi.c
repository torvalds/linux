// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_hdmi - Helpers to handle HDMI from generic machine driver
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/jack.h>
#include "sof_sdw_common.h"
#include "hda_dsp_common.h"

int sof_sdw_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	struct snd_soc_dai *dai = snd_soc_rtd_to_codec(rtd, 0);

	intel_ctx->hdmi.hdmi_comp = dai->component;

	return 0;
}

int sof_sdw_hdmi_card_late_probe(struct snd_soc_card *card)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;

	if (!intel_ctx->hdmi.idisp_codec)
		return 0;

	if (!intel_ctx->hdmi.hdmi_comp)
		return -EINVAL;

	return hda_dsp_hdmi_build_controls(card, intel_ctx->hdmi.hdmi_comp);
}
