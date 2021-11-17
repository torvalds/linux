// SPDX-License-Identifier: GPL-2.0-only
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
#include "skl_hda_dsp_common.h"

#include <sound/hda_codec.h>
#include "../../codecs/hdac_hda.h"

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

SND_SOC_DAILINK_DEF(idisp1_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(idisp3_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp3 Pin")));
SND_SOC_DAILINK_DEF(idisp3_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi3")));

SND_SOC_DAILINK_DEF(analog_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("Analog CPU DAI")));
SND_SOC_DAILINK_DEF(analog_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D0", "Analog Codec DAI")));

SND_SOC_DAILINK_DEF(digital_cpu,
	DAILINK_COMP_ARRAY(COMP_CPU("Digital CPU DAI")));
SND_SOC_DAILINK_DEF(digital_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D0", "Digital Codec DAI")));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));

SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

SND_SOC_DAILINK_DEF(dmic16k,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC16k Pin")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

/* skl_hda_digital audio interface glue - connects codec <--> CPU */
struct snd_soc_dai_link skl_hda_be_dai_links[HDA_DSP_MAX_BE_DAI_LINKS] = {
	/* Back End DAI links */
	{
		.name = "iDisp1",
		.id = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_cpu, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 2,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_cpu, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 3,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_cpu, idisp3_codec, platform),
	},
	{
		.name = "Analog Playback and Capture",
		.id = 4,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(analog_cpu, analog_codec, platform),
	},
	{
		.name = "Digital Playback and Capture",
		.id = 5,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(digital_cpu, digital_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 6,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 7,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k, dmic_codec, platform),
	},
};

int skl_hda_hdmi_jack_init(struct snd_soc_card *card)
{
	struct skl_hda_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	struct skl_hda_hdmi_pcm *pcm;
	char jack_name[NAME_SIZE];
	int err;

	if (ctx->common_hdmi_codec_drv)
		return skl_hda_hdmi_build_controls(card);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &pcm->hdmi_jack,
					    NULL, 0);

		if (err)
			return err;

		err = snd_jack_add_new_kctl(pcm->hdmi_jack.jack,
					    jack_name, SND_JACK_AVOUT);
		if (err)
			dev_warn(component->dev, "failed creating Jack kctl\n");

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &pcm->hdmi_jack);
		if (err < 0)
			return err;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
