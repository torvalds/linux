/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2015-18 Intel Corporation.
 */

/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with HDA Codecs.
 */

#ifndef __SKL_HDA_DSP_COMMON_H
#define __SKL_HDA_DSP_COMMON_H
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/hda_codec.h>
#include "../../codecs/hdac_hda.h"
#include "hda_dsp_common.h"
#include "sof_hdmi_common.h"

#define HDA_DSP_MAX_BE_DAI_LINKS 8

struct skl_hda_private {
	struct snd_soc_card card;
	struct sof_hdmi_private hdmi;
	int pcm_count;
	int dai_index;
	const char *platform_name;
	bool bt_offload_present;
	int ssp_bt;
};

extern struct snd_soc_dai_link skl_hda_be_dai_links[HDA_DSP_MAX_BE_DAI_LINKS];
int skl_hda_hdmi_jack_init(struct snd_soc_card *card);
int skl_hda_hdmi_add_pcm(struct snd_soc_card *card, int device);

#endif /* __SOUND_SOC_HDA_DSP_COMMON_H */
