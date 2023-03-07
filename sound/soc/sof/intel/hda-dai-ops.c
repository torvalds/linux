// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include <sound/pcm_params.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof/ipc4/header.h>
#include <uapi/sound/sof/header.h>
#include "../ipc4-priv.h"
#include "../ipc4-topology.h"
#include "../sof-priv.h"
#include "../sof-audio.h"
#include "hda.h"

/* These ops are only applicable for the HDA DAI's in their current form */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
/*
 * This function checks if the host dma channel corresponding
 * to the link DMA stream_tag argument is assigned to one
 * of the FEs connected to the BE DAI.
 */
static bool hda_check_fes(struct snd_soc_pcm_runtime *rtd,
			  int dir, int stream_tag)
{
	struct snd_pcm_substream *fe_substream;
	struct hdac_stream *fe_hstream;
	struct snd_soc_dpcm *dpcm;

	for_each_dpcm_fe(rtd, dir, dpcm) {
		fe_substream = snd_soc_dpcm_get_substream(dpcm->fe, dir);
		fe_hstream = fe_substream->runtime->private_data;
		if (fe_hstream->stream_tag == stream_tag)
			return true;
	}

	return false;
}

static struct hdac_ext_stream *
hda_link_stream_assign(struct hdac_bus *bus, struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_intel_hda_stream *hda_stream;
	const struct sof_intel_dsp_desc *chip;
	struct snd_sof_dev *sdev;
	struct hdac_ext_stream *res = NULL;
	struct hdac_stream *hstream = NULL;

	int stream_dir = substream->stream;

	if (!bus->ppcap) {
		dev_err(bus->dev, "stream type not supported\n");
		return NULL;
	}

	spin_lock_irq(&bus->reg_lock);
	list_for_each_entry(hstream, &bus->stream_list, list) {
		struct hdac_ext_stream *hext_stream =
			stream_to_hdac_ext_stream(hstream);
		if (hstream->direction != substream->stream)
			continue;

		hda_stream = hstream_to_sof_hda_stream(hext_stream);
		sdev = hda_stream->sdev;
		chip = get_chip_info(sdev->pdata);

		/* check if link is available */
		if (!hext_stream->link_locked) {
			/*
			 * choose the first available link for platforms that do not have the
			 * PROCEN_FMT_QUIRK set.
			 */
			if (!(chip->quirks & SOF_INTEL_PROCEN_FMT_QUIRK)) {
				res = hext_stream;
				break;
			}

			if (hstream->opened) {
				/*
				 * check if the stream tag matches the stream
				 * tag of one of the connected FEs
				 */
				if (hda_check_fes(rtd, stream_dir,
						  hstream->stream_tag)) {
					res = hext_stream;
					break;
				}
			} else {
				res = hext_stream;

				/*
				 * This must be a hostless stream.
				 * So reserve the host DMA channel.
				 */
				hda_stream->host_reserved = 1;
				break;
			}
		}
	}

	if (res) {
		/* Make sure that host and link DMA is decoupled. */
		snd_hdac_ext_stream_decouple_locked(bus, res, true);

		res->link_locked = 1;
		res->link_substream = substream;
	}
	spin_unlock_irq(&bus->reg_lock);

	return res;
}

static struct hdac_ext_stream *hda_get_hext_stream(struct snd_sof_dev *sdev,
						   struct snd_soc_dai *cpu_dai,
						   struct snd_pcm_substream *substream)
{
	return snd_soc_dai_get_dma_data(cpu_dai, substream);
}

static struct hdac_ext_stream *hda_assign_hext_stream(struct snd_sof_dev *sdev,
						      struct snd_soc_dai *cpu_dai,
						      struct snd_pcm_substream *substream)
{
	struct hdac_ext_stream *hext_stream;

	hext_stream = hda_link_stream_assign(sof_to_bus(sdev), substream);
	if (!hext_stream)
		return NULL;

	snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)hext_stream);

	return hext_stream;
}

static void hda_release_hext_stream(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
				    struct snd_pcm_substream *substream)
{
	struct hdac_ext_stream *hext_stream = hda_get_hext_stream(sdev, cpu_dai, substream);

	snd_soc_dai_set_dma_data(cpu_dai, substream, NULL);
	snd_hdac_ext_stream_release(hext_stream, HDAC_EXT_STREAM_TYPE_LINK);
}

static const struct hda_dai_widget_dma_ops hda_dma_ops = {
	.get_hext_stream = hda_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
};

#endif

const struct hda_dai_widget_dma_ops *
hda_select_dai_widget_ops(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	struct snd_sof_dai *sdai = swidget->private;

	switch (sdev->pdata->ipc_type) {
	case SOF_IPC:
	{
		struct sof_dai_private_data *private = sdai->private;

		if (private->dai_config->type == SOF_DAI_INTEL_HDA)
			return &hda_dma_ops;
		break;
	}
	case SOF_INTEL_IPC4:
	{
		struct sof_ipc4_copier *ipc4_copier = sdai->private;

		if (ipc4_copier->dai_type == SOF_DAI_INTEL_HDA)
			return &hda_dma_ops;
		break;
	}
	default:
		break;
	}
#endif
	return NULL;
}
