// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-18 Intel Corporation.

/*
 * Common functions used in different Intel machine drivers
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "../../codecs/hdac_hdmi.h"
#include "../skylake/skl.h"
#include "skl_hda_dsp_common.h"

#define NAME_SIZE	32

int skl_hda_hdmi_add_pcm(struct snd_soc_card *card, int device)
{
	struct skl_hda_private *ctx = snd_soc_card_get_drvdata(card);
	struct skl_hda_hdmi_pcm *pcm;
	char dai_name[NAME_SIZE];

	pcm = devm_kzalloc(card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	snprintf(dai_name, sizeof(dai_name), "intel-hdmi-hifi%d",
		 ctx->dai_index);
	pcm->codec_dai = snd_soc_card_get_codec_dai(card, dai_name);
	if (!pcm->codec_dai)
		return -EINVAL;

	pcm->device = device;
	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

/* skl_hda_digital audio interface glue - connects codec <--> CPU */
struct snd_soc_dai_link skl_hda_be_dai_links[HDA_DSP_MAX_BE_DAI_LINKS] = {
	/* Back End DAI links */
	{
		.name = "iDisp1",
		.id = 1,
		.cpu_dai_name = "iDisp1 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi1",
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp2",
		.id = 2,
		.cpu_dai_name = "iDisp2 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi2",
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp3",
		.id = 3,
		.cpu_dai_name = "iDisp3 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi3",
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "Analog Playback and Capture",
		.id = 4,
		.cpu_dai_name = "Analog CPU DAI",
		.codec_name = "ehdaudio0D0",
		.codec_dai_name = "Analog Codec DAI",
		.platform_name = "0000:00:1f.3",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = NULL,
		.no_pcm = 1,
	},
	{
		.name = "Digital Playback and Capture",
		.id = 5,
		.cpu_dai_name = "Digital CPU DAI",
		.codec_name = "ehdaudio0D0",
		.codec_dai_name = "Digital Codec DAI",
		.platform_name = "0000:00:1f.3",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = NULL,
		.no_pcm = 1,
	},
};

int skl_hda_hdmi_jack_init(struct snd_soc_card *card)
{
	struct skl_hda_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	struct skl_hda_hdmi_pcm *pcm;
	char jack_name[NAME_SIZE];
	int err;

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &pcm->hdmi_jack,
					    NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &pcm->hdmi_jack);
		if (err < 0)
			return err;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
