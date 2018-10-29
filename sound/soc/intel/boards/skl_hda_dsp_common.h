/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2015-18 Intel Corporation.
 */

/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with HDA Codecs.
 */

#ifndef __SOUND_SOC_HDA_DSP_COMMON_H
#define __SOUND_SOC_HDA_DSP_COMMON_H
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>

#define HDA_DSP_MAX_BE_DAI_LINKS 5

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
};

extern struct snd_soc_dai_link skl_hda_be_dai_links[HDA_DSP_MAX_BE_DAI_LINKS];
int skl_hda_hdmi_jack_init(struct snd_soc_card *card);
int skl_hda_hdmi_add_pcm(struct snd_soc_card *card, int device);

#endif /* __SOUND_SOC_HDA_DSP_COMMON_H */
