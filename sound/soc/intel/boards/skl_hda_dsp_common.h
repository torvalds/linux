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

#define HDA_DSP_MAX_BE_DAI_LINKS 7

struct skl_hda_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_jack hdmi_jack;
	int device;
};

struct skl_hda_private {
	struct list_head hdmi_pcm_list;
	int pcm_count;
	int dai_index;
	const char *platform_name;
	bool common_hdmi_codec_drv;
};

extern struct snd_soc_dai_link skl_hda_be_dai_links[HDA_DSP_MAX_BE_DAI_LINKS];
int skl_hda_hdmi_jack_init(struct snd_soc_card *card);
int skl_hda_hdmi_add_pcm(struct snd_soc_card *card, int device);

/*
 * Search card topology and register HDMI PCM related controls
 * to codec driver.
 */
static inline int skl_hda_hdmi_build_controls(struct snd_soc_card *card)
{
	struct skl_hda_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component;
	struct skl_hda_hdmi_pcm *pcm;

	/* HDMI disabled, do not create controls */
	if (list_empty(&ctx->hdmi_pcm_list))
		return 0;

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct skl_hda_hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;
	if (!component)
		return -EINVAL;

	return hda_dsp_hdmi_build_controls(card, component);
}

#endif /* __SOUND_SOC_HDA_DSP_COMMON_H */
