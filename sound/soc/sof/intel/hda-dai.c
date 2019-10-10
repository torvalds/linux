// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Keyon Jie <yang.jie@linux.intel.com>
//

#include <sound/pcm_params.h>
#include <sound/hdaudio_ext.h>
#include "../sof-priv.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

struct hda_pipe_params {
	u8 host_dma_id;
	u8 link_dma_id;
	u32 ch;
	u32 s_freq;
	u32 s_fmt;
	u8 linktype;
	snd_pcm_format_t format;
	int link_index;
	int stream;
	unsigned int host_bps;
	unsigned int link_bps;
};

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
	hda_link_stream_assign(struct hdac_bus *bus,
			       struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_ext_stream *res = NULL;
	struct hdac_stream *stream = NULL;

	int stream_dir = substream->stream;

	if (!bus->ppcap) {
		dev_err(bus->dev, "stream type not supported\n");
		return NULL;
	}

	list_for_each_entry(stream, &bus->stream_list, list) {
		struct hdac_ext_stream *hstream =
			stream_to_hdac_ext_stream(stream);
		if (stream->direction != substream->stream)
			continue;

		hda_stream = hstream_to_sof_hda_stream(hstream);

		/* check if link is available */
		if (!hstream->link_locked) {
			if (stream->opened) {
				/*
				 * check if the stream tag matches the stream
				 * tag of one of the connected FEs
				 */
				if (hda_check_fes(rtd, stream_dir,
						  stream->stream_tag)) {
					res = hstream;
					break;
				}
			} else {
				res = hstream;

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
		/*
		 * Decouple host and link DMA. The decoupled flag
		 * is updated in snd_hdac_ext_stream_decouple().
		 */
		if (!res->decoupled)
			snd_hdac_ext_stream_decouple(bus, res, true);
		spin_lock_irq(&bus->reg_lock);
		res->link_locked = 1;
		res->link_substream = substream;
		spin_unlock_irq(&bus->reg_lock);
	}

	return res;
}

static int hda_link_dma_params(struct hdac_ext_stream *stream,
			       struct hda_pipe_params *params)
{
	struct hdac_stream *hstream = &stream->hstream;
	unsigned char stream_tag = hstream->stream_tag;
	struct hdac_bus *bus = hstream->bus;
	struct hdac_ext_link *link;
	unsigned int format_val;

	snd_hdac_ext_stream_decouple(bus, stream, true);
	snd_hdac_ext_link_stream_reset(stream);

	format_val = snd_hdac_calc_stream_format(params->s_freq, params->ch,
						 params->format,
						 params->link_bps, 0);

	dev_dbg(bus->dev, "format_val=%d, rate=%d, ch=%d, format=%d\n",
		format_val, params->s_freq, params->ch, params->format);

	snd_hdac_ext_link_stream_setup(stream, format_val);

	if (stream->hstream.direction == SNDRV_PCM_STREAM_PLAYBACK) {
		list_for_each_entry(link, &bus->hlink_list, list) {
			if (link->index == params->link_index)
				snd_hdac_ext_link_set_stream_id(link,
								stream_tag);
		}
	}

	stream->link_prepared = 1;

	return 0;
}

/* Send DAI_CONFIG IPC to the DAI that matches the dai_name and direction */
static int hda_link_config_ipc(struct sof_intel_hda_stream *hda_stream,
			       const char *dai_name, int channel, int dir)
{
	struct sof_ipc_dai_config *config;
	struct snd_sof_dai *sof_dai;
	struct sof_ipc_reply reply;
	int ret = 0;

	list_for_each_entry(sof_dai, &hda_stream->sdev->dai_list, list) {
		if (!sof_dai->cpu_dai_name)
			continue;

		if (!strcmp(dai_name, sof_dai->cpu_dai_name) &&
		    dir == sof_dai->comp_dai.direction) {
			config = sof_dai->dai_config;

			if (!config) {
				dev_err(hda_stream->sdev->dev,
					"error: no config for DAI %s\n",
					sof_dai->name);
				return -EINVAL;
			}

			/* update config with stream tag */
			config->hda.link_dma_ch = channel;

			/* send IPC */
			ret = sof_ipc_tx_message(hda_stream->sdev->ipc,
						 config->hdr.cmd,
						 config,
						 config->hdr.size,
						 &reply, sizeof(reply));

			if (ret < 0)
				dev_err(hda_stream->sdev->dev,
					"error: failed to set dai config for %s\n",
					sof_dai->name);
			return ret;
		}
	}

	return -EINVAL;
}

static int hda_link_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_bus *bus = hstream->bus;
	struct hdac_ext_stream *link_dev;
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct sof_intel_hda_stream *hda_stream;
	struct hda_pipe_params p_params = {0};
	struct hdac_ext_link *link;
	int stream_tag;
	int ret;

	/* get stored dma data if resuming from system suspend */
	link_dev = snd_soc_dai_get_dma_data(dai, substream);
	if (!link_dev) {
		link_dev = hda_link_stream_assign(bus, substream);
		if (!link_dev)
			return -EBUSY;
	}

	stream_tag = hdac_stream(link_dev)->stream_tag;

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	/* update the DSP with the new tag */
	ret = hda_link_config_ipc(hda_stream, dai->name, stream_tag - 1,
				  substream->stream);
	if (ret < 0)
		return ret;

	snd_soc_dai_set_dma_data(dai, substream, (void *)link_dev);

	link = snd_hdac_ext_bus_get_link(bus, codec_dai->component->name);
	if (!link)
		return -EINVAL;

	/* set the stream tag in the codec dai dma params */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_set_tdm_slot(codec_dai, stream_tag, 0, 0, 0);
	else
		snd_soc_dai_set_tdm_slot(codec_dai, 0, stream_tag, 0, 0);

	p_params.s_fmt = snd_pcm_format_width(params_format(params));
	p_params.ch = params_channels(params);
	p_params.s_freq = params_rate(params);
	p_params.stream = substream->stream;
	p_params.link_dma_id = stream_tag - 1;
	p_params.link_index = link->index;
	p_params.format = params_format(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		p_params.link_bps = codec_dai->driver->playback.sig_bits;
	else
		p_params.link_bps = codec_dai->driver->capture.sig_bits;

	return hda_link_dma_params(link_dev, &p_params);
}

static int hda_link_pcm_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *link_dev =
				snd_soc_dai_get_dma_data(dai, substream);
	struct sof_intel_hda_stream *hda_stream;
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	int stream = substream->stream;

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	if (link_dev->link_prepared)
		return 0;

	dev_dbg(sdev->dev, "hda: prepare stream dir %d\n", substream->stream);

	return hda_link_hw_params(substream, &rtd->dpcm[stream].hw_params,
				  dai);
}

static int hda_link_pcm_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *link_dev =
				snd_soc_dai_get_dma_data(dai, substream);
	struct sof_intel_hda_stream *hda_stream;
	struct snd_soc_pcm_runtime *rtd;
	struct hdac_ext_link *link;
	struct hdac_stream *hstream;
	struct hdac_bus *bus;
	int stream_tag;
	int ret;

	hstream = substream->runtime->private_data;
	bus = hstream->bus;
	rtd = snd_pcm_substream_chip(substream);

	link = snd_hdac_ext_bus_get_link(bus, rtd->codec_dai->component->name);
	if (!link)
		return -EINVAL;

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	dev_dbg(dai->dev, "In %s cmd=%d\n", __func__, cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		/* set up hw_params */
		ret = hda_link_pcm_prepare(substream, dai);
		if (ret < 0) {
			dev_err(dai->dev,
				"error: setting up hw_params during resume\n");
			return ret;
		}

		/* fallthrough */
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_link_stream_start(link_dev);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		/*
		 * clear link DMA channel. It will be assigned when
		 * hw_params is set up again after resume.
		 */
		ret = hda_link_config_ipc(hda_stream, dai->name,
					  DMA_CHAN_INVALID, substream->stream);
		if (ret < 0)
			return ret;

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			stream_tag = hdac_stream(link_dev)->stream_tag;
			snd_hdac_ext_link_clear_stream_id(link, stream_tag);
		}

		link_dev->link_prepared = 0;

		/* fallthrough */
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_hdac_ext_link_stream_clear(link_dev);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int hda_link_hw_free(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	unsigned int stream_tag;
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_bus *bus;
	struct hdac_ext_link *link;
	struct hdac_stream *hstream;
	struct snd_soc_pcm_runtime *rtd;
	struct hdac_ext_stream *link_dev;
	int ret;

	hstream = substream->runtime->private_data;
	bus = hstream->bus;
	rtd = snd_pcm_substream_chip(substream);
	link_dev = snd_soc_dai_get_dma_data(dai, substream);
	hda_stream = hstream_to_sof_hda_stream(link_dev);

	/* free the link DMA channel in the FW */
	ret = hda_link_config_ipc(hda_stream, dai->name, DMA_CHAN_INVALID,
				  substream->stream);
	if (ret < 0)
		return ret;

	link = snd_hdac_ext_bus_get_link(bus, rtd->codec_dai->component->name);
	if (!link)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		stream_tag = hdac_stream(link_dev)->stream_tag;
		snd_hdac_ext_link_clear_stream_id(link, stream_tag);
	}

	snd_soc_dai_set_dma_data(dai, substream, NULL);
	snd_hdac_ext_stream_release(link_dev, HDAC_EXT_STREAM_TYPE_LINK);
	link_dev->link_prepared = 0;

	/* free the host DMA channel reserved by hostless streams */
	hda_stream->host_reserved = 0;

	return 0;
}

static const struct snd_soc_dai_ops hda_link_dai_ops = {
	.hw_params = hda_link_hw_params,
	.hw_free = hda_link_hw_free,
	.trigger = hda_link_pcm_trigger,
	.prepare = hda_link_pcm_prepare,
};
#endif

/*
 * common dai driver for skl+ platforms.
 * some products who use this DAI array only physically have a subset of
 * the DAIs, but no harm is done here by adding the whole set.
 */
struct snd_soc_dai_driver skl_dai[] = {
{
	.name = "SSP0 Pin",
},
{
	.name = "SSP1 Pin",
},
{
	.name = "SSP2 Pin",
},
{
	.name = "SSP3 Pin",
},
{
	.name = "SSP4 Pin",
},
{
	.name = "SSP5 Pin",
},
{
	.name = "DMIC01 Pin",
},
{
	.name = "DMIC16k Pin",
},
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
{
	.name = "iDisp1 Pin",
	.ops = &hda_link_dai_ops,
},
{
	.name = "iDisp2 Pin",
	.ops = &hda_link_dai_ops,
},
{
	.name = "iDisp3 Pin",
	.ops = &hda_link_dai_ops,
},
{
	.name = "Analog CPU DAI",
	.ops = &hda_link_dai_ops,
},
{
	.name = "Digital CPU DAI",
	.ops = &hda_link_dai_ops,
},
{
	.name = "Alt Analog CPU DAI",
	.ops = &hda_link_dai_ops,
},
#endif
};
