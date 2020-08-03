// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_hdmi - Helpers to handle HDMI from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/jack.h>
#include "sof_sdw_common.h"
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"

static struct snd_soc_jack hdmi[MAX_HDMI_NUM];

struct hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

int sof_sdw_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = asoc_rtd_to_codec(rtd, 0);
	struct hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	/* dai_link id is 1:1 mapped to the PCM device */
	pcm->device = rtd->dai_link->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

#define NAME_SIZE	32
int sof_sdw_hdmi_card_late_probe(struct snd_soc_card *card)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	if (!ctx->idisp_codec)
		return 0;

	if (list_empty(&ctx->hdmi_pcm_list))
		return -EINVAL;

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;

	if (ctx->common_hdmi_codec_drv)
		return hda_dsp_hdmi_build_controls(card, component);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &hdmi[i],
					    NULL, 0);

		if (err)
			return err;

		err = snd_jack_add_new_kctl(hdmi[i].jack,
					    jack_name, SND_JACK_AVOUT);
		if (err)
			dev_warn(component->dev, "failed creating Jack kctl\n");

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
