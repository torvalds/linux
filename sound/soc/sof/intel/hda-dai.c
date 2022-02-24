// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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
#include "../sof-audio.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
#include "../sof-probes.h"
#endif

struct hda_pipe_params {
	u32 ch;
	u32 s_freq;
	u32 s_fmt;
	u8 linktype;
	snd_pcm_format_t format;
	int link_index;
	int stream;
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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_ext_stream *res = NULL;
	struct hdac_stream *stream = NULL;

	int stream_dir = substream->stream;

	if (!bus->ppcap) {
		dev_err(bus->dev, "stream type not supported\n");
		return NULL;
	}

	spin_lock_irq(&bus->reg_lock);
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
			snd_hdac_ext_stream_decouple_locked(bus, res, true);

		res->link_locked = 1;
		res->link_substream = substream;
	}
	spin_unlock_irq(&bus->reg_lock);

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

/* Update config for the DAI widget */
static struct sof_ipc_dai_config *hda_dai_update_config(struct snd_soc_dapm_widget *w,
							int channel)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct sof_ipc_dai_config *config;
	struct snd_sof_dai *sof_dai;

	if (!swidget)
		return NULL;

	sof_dai = swidget->private;

	if (!sof_dai || !sof_dai->dai_config) {
		dev_err(swidget->scomp->dev, "error: No config for DAI %s\n", w->name);
		return NULL;
	}

	config = &sof_dai->dai_config[sof_dai->current_config];

	/* update config with stream tag */
	config->hda.link_dma_ch = channel;

	return config;
}

static int hda_link_dai_widget_update(struct sof_intel_hda_stream *hda_stream,
				      struct snd_soc_dapm_widget *w,
				      int channel, bool widget_setup)
{
	struct snd_sof_dev *sdev = hda_stream->sdev;
	struct sof_ipc_dai_config *config;

	config = hda_dai_update_config(w, channel);
	if (!config) {
		dev_err(sdev->dev, "error: no config for DAI %s\n", w->name);
		return -ENOENT;
	}

	/* set up/free DAI widget and send DAI_CONFIG IPC */
	if (widget_setup)
		return hda_ctrl_dai_widget_setup(w, SOF_DAI_CONFIG_FLAGS_2_STEP_STOP);

	return hda_ctrl_dai_widget_free(w, SOF_DAI_CONFIG_FLAGS_NONE);
}

static int hda_link_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct hdac_bus *bus = hstream->bus;
	struct hdac_ext_stream *link_dev;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct sof_intel_hda_stream *hda_stream;
	struct hda_pipe_params p_params = {0};
	struct snd_soc_dapm_widget *w;
	struct hdac_ext_link *link;
	int stream_tag;
	int ret;

	/* get stored dma data if resuming from system suspend */
	link_dev = snd_soc_dai_get_dma_data(dai, substream);
	if (!link_dev) {
		link_dev = hda_link_stream_assign(bus, substream);
		if (!link_dev)
			return -EBUSY;

		snd_soc_dai_set_dma_data(dai, substream, (void *)link_dev);
	}

	stream_tag = hdac_stream(link_dev)->stream_tag;

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		w = dai->playback_widget;
	else
		w = dai->capture_widget;

	/* set up the DAI widget and send the DAI_CONFIG with the new tag */
	ret = hda_link_dai_widget_update(hda_stream, w, stream_tag - 1, true);
	if (ret < 0)
		return ret;

	link = snd_hdac_ext_bus_get_link(bus, codec_dai->component->name);
	if (!link)
		return -EINVAL;

	/* set the hdac_stream in the codec dai */
	snd_soc_dai_set_stream(codec_dai, hdac_stream(link_dev), substream->stream);

	p_params.s_fmt = snd_pcm_format_width(params_format(params));
	p_params.ch = params_channels(params);
	p_params.s_freq = params_rate(params);
	p_params.stream = substream->stream;
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
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int stream = substream->stream;

	if (link_dev->link_prepared)
		return 0;

	dev_dbg(sdev->dev, "hda: prepare stream dir %d\n", substream->stream);

	return hda_link_hw_params(substream, &rtd->dpcm[stream].hw_params,
				  dai);
}

static int hda_link_dai_config_pause_push_ipc(struct snd_soc_dapm_widget *w)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_soc_component *component = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	struct sof_ipc_dai_config *config;
	struct snd_sof_dai *sof_dai;
	struct sof_ipc_reply reply;
	int ret;

	sof_dai = swidget->private;

	if (!sof_dai || !sof_dai->dai_config) {
		dev_err(sdev->dev, "No config for DAI %s\n", w->name);
		return -EINVAL;
	}

	config = &sof_dai->dai_config[sof_dai->current_config];

	/* set PAUSE command flag */
	config->flags = FIELD_PREP(SOF_DAI_CONFIG_FLAGS_CMD_MASK, SOF_DAI_CONFIG_FLAGS_PAUSE);

	ret = sof_ipc_tx_message(sdev->ipc, config->hdr.cmd, config, config->hdr.size,
				 &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "DAI config for %s failed during pause push\n", w->name);

	return ret;
}

static int hda_link_pcm_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *link_dev =
				snd_soc_dai_get_dma_data(dai, substream);
	struct sof_intel_hda_stream *hda_stream;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dapm_widget *w;
	struct hdac_ext_link *link;
	struct hdac_stream *hstream;
	struct hdac_bus *bus;
	int stream_tag;
	int ret;

	hstream = substream->runtime->private_data;
	bus = hstream->bus;
	rtd = asoc_substream_to_rtd(substream);

	link = snd_hdac_ext_bus_get_link(bus, asoc_rtd_to_codec(rtd, 0)->component->name);
	if (!link)
		return -EINVAL;

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	dev_dbg(dai->dev, "In %s cmd=%d\n", __func__, cmd);

	w = snd_soc_dai_get_widget(dai, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_link_stream_start(link_dev);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_hdac_ext_link_stream_clear(link_dev);

		/*
		 * free DAI widget during stop/suspend to keep widget use_count's balanced.
		 */
		ret = hda_link_dai_widget_update(hda_stream, w, DMA_CHAN_INVALID, false);
		if (ret < 0)
			return ret;

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			stream_tag = hdac_stream(link_dev)->stream_tag;
			snd_hdac_ext_link_clear_stream_id(link, stream_tag);
		}

		link_dev->link_prepared = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_hdac_ext_link_stream_clear(link_dev);

		ret = hda_link_dai_config_pause_push_ipc(w);
		if (ret < 0)
			return ret;
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
	struct snd_soc_dapm_widget *w;
	int ret;

	hstream = substream->runtime->private_data;
	bus = hstream->bus;
	rtd = asoc_substream_to_rtd(substream);
	link_dev = snd_soc_dai_get_dma_data(dai, substream);

	if (!link_dev) {
		dev_dbg(dai->dev,
			"%s: link_dev is not assigned\n", __func__);
		return -EINVAL;
	}

	hda_stream = hstream_to_sof_hda_stream(link_dev);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		w = dai->playback_widget;
	else
		w = dai->capture_widget;

	/* free the link DMA channel in the FW and the DAI widget */
	ret = hda_link_dai_widget_update(hda_stream, w, DMA_CHAN_INVALID, false);
	if (ret < 0)
		return ret;

	link = snd_hdac_ext_bus_get_link(bus, asoc_rtd_to_codec(rtd, 0)->component->name);
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

/* only one flag used so far to harden hw_params/hw_free/trigger/prepare */
struct ssp_dai_dma_data {
	bool setup;
};

static int ssp_dai_setup_or_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai,
				 bool setup)
{
	struct snd_soc_component *component;
	struct snd_sof_widget *swidget;
	struct snd_soc_dapm_widget *w;
	struct sof_ipc_fw_version *v;
	struct snd_sof_dev *sdev;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		w = dai->playback_widget;
	else
		w = dai->capture_widget;

	swidget = w->dobj.private;
	component = swidget->scomp;
	sdev = snd_soc_component_get_drvdata(component);
	v = &sdev->fw_ready.version;

	/* DAI_CONFIG IPC during hw_params is not supported in older firmware */
	if (v->abi_version < SOF_ABI_VER(3, 18, 0))
		return 0;

	if (setup)
		return hda_ctrl_dai_widget_setup(w, SOF_DAI_CONFIG_FLAGS_NONE);

	return hda_ctrl_dai_widget_free(w, SOF_DAI_CONFIG_FLAGS_NONE);
}

static int ssp_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct ssp_dai_dma_data *dma_data;

	dma_data = kzalloc(sizeof(*dma_data), GFP_KERNEL);
	if (!dma_data)
		return -ENOMEM;

	snd_soc_dai_set_dma_data(dai, substream, dma_data);

	return 0;
}

static int ssp_dai_setup(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai,
			 bool setup)
{
	struct ssp_dai_dma_data *dma_data;
	int ret = 0;

	dma_data = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma_data) {
		dev_err(dai->dev, "%s: failed to get dma_data\n", __func__);
		return -EIO;
	}

	if (dma_data->setup != setup) {
		ret = ssp_dai_setup_or_free(substream, dai, setup);
		if (!ret)
			dma_data->setup = setup;
	}
	return ret;
}

static int ssp_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	/* params are ignored for now */
	return ssp_dai_setup(substream, dai, true);
}

static int ssp_dai_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	/*
	 * the SSP will only be reconfigured during resume operations and
	 * not in case of xruns
	 */
	return ssp_dai_setup(substream, dai, true);
}

static int ssp_dai_trigger(struct snd_pcm_substream *substream,
			   int cmd, struct snd_soc_dai *dai)
{
	if (cmd != SNDRV_PCM_TRIGGER_SUSPEND)
		return 0;

	return ssp_dai_setup(substream, dai, false);
}

static int ssp_dai_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return ssp_dai_setup(substream, dai, false);
}

static void ssp_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct ssp_dai_dma_data *dma_data;

	dma_data = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma_data) {
		dev_err(dai->dev, "%s: failed to get dma_data\n", __func__);
		return;
	}
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(dma_data);
}

static const struct snd_soc_dai_ops ssp_dai_ops = {
	.startup = ssp_dai_startup,
	.hw_params = ssp_dai_hw_params,
	.prepare = ssp_dai_prepare,
	.trigger = ssp_dai_trigger,
	.hw_free = ssp_dai_hw_free,
	.shutdown = ssp_dai_shutdown,
};

/*
 * common dai driver for skl+ platforms.
 * some products who use this DAI array only physically have a subset of
 * the DAIs, but no harm is done here by adding the whole set.
 */
struct snd_soc_dai_driver skl_dai[] = {
{
	.name = "SSP0 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "SSP1 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "SSP2 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "SSP3 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "SSP4 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "SSP5 Pin",
	.ops = &ssp_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "DMIC01 Pin",
	.capture = {
		.channels_min = 1,
		.channels_max = 4,
	},
},
{
	.name = "DMIC16k Pin",
	.capture = {
		.channels_min = 1,
		.channels_max = 4,
	},
},
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
{
	.name = "iDisp1 Pin",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp2 Pin",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp3 Pin",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp4 Pin",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "Analog CPU DAI",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
	},
},
{
	.name = "Digital CPU DAI",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
	},
},
{
	.name = "Alt Analog CPU DAI",
	.ops = &hda_link_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
	},
},
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_PROBES)
{
	.name = "Probe Extraction CPU DAI",
	.compress_new = snd_soc_new_compress,
	.cops = &sof_probe_compr_ops,
	.capture = {
		.stream_name = "Probe Extraction",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_48000,
		.rate_min = 48000,
		.rate_max = 48000,
	},
},
#endif
#endif
};
