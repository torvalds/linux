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
#include <sound/intel-nhlt.h>
#include <sound/sof/ipc4/header.h>
#include <uapi/sound/sof/header.h>
#include "../ipc4-priv.h"
#include "../ipc4-topology.h"
#include "../sof-priv.h"
#include "../sof-audio.h"
#include "hda.h"

/*
 * The default method is to fetch NHLT from BIOS. With this parameter set
 * it is possible to override that with NHLT in the SOF topology manifest.
 */
static bool hda_use_tplg_nhlt;
module_param_named(sof_use_tplg_nhlt, hda_use_tplg_nhlt, bool, 0444);
MODULE_PARM_DESC(sof_use_tplg_nhlt, "SOF topology nhlt override");

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)

struct hda_pipe_params {
	u32 ch;
	u32 s_freq;
	snd_pcm_format_t format;
	int link_index;
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

static int hda_link_dma_cleanup(struct snd_pcm_substream *substream,
				struct hdac_ext_stream *hext_stream,
				struct snd_soc_dai *cpu_dai,
				struct snd_soc_dai *codec_dai,
				bool trigger_suspend_stop)
{
	struct hdac_stream *hstream = &hext_stream->hstream;
	struct hdac_bus *bus = hstream->bus;
	struct sof_intel_hda_stream *hda_stream;
	struct hdac_ext_link *hlink;
	int stream_tag;

	hlink = snd_hdac_ext_bus_get_hlink_by_name(bus, codec_dai->component->name);
	if (!hlink)
		return -EINVAL;

	if (trigger_suspend_stop)
		snd_hdac_ext_stream_clear(hext_stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		stream_tag = hdac_stream(hext_stream)->stream_tag;
		snd_hdac_ext_bus_link_clear_stream_id(hlink, stream_tag);
	}
	snd_soc_dai_set_dma_data(cpu_dai, substream, NULL);
	snd_hdac_ext_stream_release(hext_stream, HDAC_EXT_STREAM_TYPE_LINK);
	hext_stream->link_prepared = 0;

	/* free the host DMA channel reserved by hostless streams */
	hda_stream = hstream_to_sof_hda_stream(hext_stream);
	hda_stream->host_reserved = 0;

	return 0;
}

static int hda_link_dma_params(struct hdac_ext_stream *hext_stream,
			       struct hda_pipe_params *params)
{
	struct hdac_stream *hstream = &hext_stream->hstream;
	unsigned char stream_tag = hstream->stream_tag;
	struct hdac_bus *bus = hstream->bus;
	struct hdac_ext_link *hlink;
	unsigned int format_val;

	snd_hdac_ext_stream_reset(hext_stream);

	format_val = snd_hdac_calc_stream_format(params->s_freq, params->ch,
						 params->format,
						 params->link_bps, 0);

	dev_dbg(bus->dev, "format_val=%d, rate=%d, ch=%d, format=%d\n",
		format_val, params->s_freq, params->ch, params->format);

	snd_hdac_ext_stream_setup(hext_stream, format_val);

	if (hext_stream->hstream.direction == SNDRV_PCM_STREAM_PLAYBACK) {
		list_for_each_entry(hlink, &bus->hlink_list, list) {
			if (hlink->index == params->link_index)
				snd_hdac_ext_bus_link_set_stream_id(hlink,
								    stream_tag);
		}
	}

	hext_stream->link_prepared = 1;

	return 0;
}

static int hda_link_dma_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct hda_pipe_params p_params = {0};
	struct hdac_ext_stream *hext_stream;
	struct hdac_ext_link *hlink;
	struct snd_sof_dev *sdev;
	struct hdac_bus *bus;

	sdev = snd_soc_component_get_drvdata(cpu_dai->component);
	bus = sof_to_bus(sdev);

	hlink = snd_hdac_ext_bus_get_hlink_by_name(bus, codec_dai->component->name);
	if (!hlink)
		return -EINVAL;

	hext_stream = snd_soc_dai_get_dma_data(cpu_dai, substream);
	if (!hext_stream) {
		hext_stream = hda_link_stream_assign(bus, substream);
		if (!hext_stream)
			return -EBUSY;

		snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)hext_stream);
	}

	/* set the hdac_stream in the codec dai */
	snd_soc_dai_set_stream(codec_dai, hdac_stream(hext_stream), substream->stream);

	p_params.ch = params_channels(params);
	p_params.s_freq = params_rate(params);
	p_params.link_index = hlink->index;
	p_params.format = params_format(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		p_params.link_bps = codec_dai->driver->playback.sig_bits;
	else
		p_params.link_bps = codec_dai->driver->capture.sig_bits;

	return hda_link_dma_params(hext_stream, &p_params);
}

static int hda_link_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int stream = substream->stream;

	return hda_link_dma_hw_params(substream, &rtd->dpcm[stream].hw_params);
}

static int hda_link_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct hdac_ext_stream *hext_stream = snd_soc_dai_get_dma_data(cpu_dai, substream);
	int ret;

	if (!hext_stream)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_stream_start(hext_stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = hda_link_dma_cleanup(substream, hext_stream, cpu_dai, codec_dai, true);
		if (ret < 0)
			return ret;

		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_hdac_ext_stream_clear(hext_stream);

		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int hda_link_dma_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct hdac_ext_stream *hext_stream;

	hext_stream = snd_soc_dai_get_dma_data(cpu_dai, substream);
	if (!hext_stream)
		return 0;

	return hda_link_dma_cleanup(substream, hext_stream, cpu_dai, codec_dai, false);
}

static int hda_dai_widget_update(struct snd_soc_dapm_widget *w,
				 int channel, bool widget_setup)
{
	struct snd_sof_dai_config_data data;

	data.dai_data = channel;

	/* set up/free DAI widget and send DAI_CONFIG IPC */
	if (widget_setup)
		return hda_ctrl_dai_widget_setup(w, SOF_DAI_CONFIG_FLAGS_2_STEP_STOP, &data);

	return hda_ctrl_dai_widget_free(w, SOF_DAI_CONFIG_FLAGS_NONE, &data);
}

static int hda_dai_hw_params_update(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream;
	struct snd_soc_dapm_widget *w;
	int stream_tag;

	hext_stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!hext_stream)
		return -EINVAL;

	stream_tag = hdac_stream(hext_stream)->stream_tag;

	w = snd_soc_dai_get_widget(dai, substream->stream);

	/* set up the DAI widget and send the DAI_CONFIG with the new tag */
	return hda_dai_widget_update(w, stream_tag - 1, true);
}

static int hda_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream =
				snd_soc_dai_get_dma_data(dai, substream);
	int ret;

	if (hext_stream && hext_stream->link_prepared)
		return 0;

	ret = hda_link_dma_hw_params(substream, params);
	if (ret < 0)
		return ret;

	return hda_dai_hw_params_update(substream, params, dai);
}


static int hda_dai_config_pause_push_ipc(struct snd_soc_dapm_widget *w)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_soc_component *component = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(component);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;
	int ret = 0;

	if (tplg_ops->dai_config) {
		ret = tplg_ops->dai_config(sdev, swidget, SOF_DAI_CONFIG_FLAGS_PAUSE, NULL);
		if (ret < 0)
			dev_err(sdev->dev, "%s: DAI config failed for widget %s\n", __func__,
				w->name);
	}

	return ret;
}

static int hda_dai_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream =
				snd_soc_dai_get_dma_data(dai, substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int stream = substream->stream;
	int ret;

	if (hext_stream && hext_stream->link_prepared)
		return 0;

	dev_dbg(sdev->dev, "prepare stream dir %d\n", substream->stream);

	ret = hda_link_dma_prepare(substream);
	if (ret < 0)
		return ret;

	return hda_dai_hw_params_update(substream, &rtd->dpcm[stream].hw_params, dai);
}

static int hda_dai_hw_free_ipc(int stream, /* direction */
			       struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(dai, stream);

	/* free the link DMA channel in the FW and the DAI widget */
	return hda_dai_widget_update(w, DMA_CHAN_INVALID, false);
}

static int ipc3_hda_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_widget *w;
	int ret;

	dev_dbg(dai->dev, "cmd=%d dai %s direction %d\n", cmd,
		dai->name, substream->stream);

	ret = hda_link_dma_trigger(substream, cmd);
	if (ret < 0)
		return ret;

	w = snd_soc_dai_get_widget(dai, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		/*
		 * free DAI widget during stop/suspend to keep widget use_count's balanced.
		 */
		ret = hda_dai_hw_free_ipc(substream->stream, dai);
		if (ret < 0)
			return ret;

		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = hda_dai_config_pause_push_ipc(w);
		if (ret < 0)
			return ret;
		break;

	default:
		break;
	}
	return 0;
}

/*
 * In contrast to IPC3, the dai trigger in IPC4 mixes pipeline state changes
 * (over IPC channel) and DMA state change (direct host register changes).
 */
static int ipc4_hda_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *hext_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(dai->component);
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_sof_widget *swidget;
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	int ret;

	dev_dbg(dai->dev, "cmd=%d dai %s direction %d\n", cmd,
		dai->name, substream->stream);

	rtd = asoc_substream_to_rtd(substream);
	cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	codec_dai = asoc_rtd_to_codec(rtd, 0);

	w = snd_soc_dai_get_widget(dai, substream->stream);
	swidget = w->dobj.private;
	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_stream_start(hext_stream);
		if (pipeline->state != SOF_IPC4_PIPE_PAUSED) {
			ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
							  SOF_IPC4_PIPE_PAUSED);
			if (ret < 0)
				return ret;
			pipeline->state = SOF_IPC4_PIPE_PAUSED;
		}

		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_RUNNING);
		if (ret < 0)
			return ret;
		pipeline->state = SOF_IPC4_PIPE_RUNNING;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_PAUSED);
		if (ret < 0)
			return ret;

		pipeline->state = SOF_IPC4_PIPE_PAUSED;

		snd_hdac_ext_stream_clear(hext_stream);

		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_RESET);
		if (ret < 0)
			return ret;

		pipeline->state = SOF_IPC4_PIPE_RESET;

		ret = hda_link_dma_cleanup(substream, hext_stream, cpu_dai, codec_dai, false);
		if (ret < 0) {
			dev_err(sdev->dev, "%s: failed to clean up link DMA\n", __func__);
			return ret;
		}
		break;
	}
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	{
		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_PAUSED);
		if (ret < 0)
			return ret;

		pipeline->state = SOF_IPC4_PIPE_PAUSED;

		snd_hdac_ext_stream_clear(hext_stream);
		break;
	}
	default:
		dev_err(sdev->dev, "%s: unknown trigger command %d\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static int hda_dai_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret;

	ret = hda_link_dma_hw_free(substream);
	if (ret < 0)
		return ret;

	return hda_dai_hw_free_ipc(substream->stream, dai);
}

static const struct snd_soc_dai_ops ipc3_hda_dai_ops = {
	.hw_params = hda_dai_hw_params,
	.hw_free = hda_dai_hw_free,
	.trigger = ipc3_hda_dai_trigger,
	.prepare = hda_dai_prepare,
};

static int hda_dai_suspend(struct hdac_bus *bus)
{
	struct snd_soc_pcm_runtime *rtd;
	struct hdac_ext_stream *hext_stream;
	struct hdac_stream *s;
	int ret;

	/* set internal flag for BE */
	list_for_each_entry(s, &bus->stream_list, list) {

		hext_stream = stream_to_hdac_ext_stream(s);

		/*
		 * clear stream. This should already be taken care for running
		 * streams when the SUSPEND trigger is called. But paused
		 * streams do not get suspended, so this needs to be done
		 * explicitly during suspend.
		 */
		if (hext_stream->link_substream) {
			struct snd_soc_dai *cpu_dai;
			struct snd_soc_dai *codec_dai;

			rtd = asoc_substream_to_rtd(hext_stream->link_substream);
			cpu_dai = asoc_rtd_to_cpu(rtd, 0);
			codec_dai = asoc_rtd_to_codec(rtd, 0);

			ret = hda_link_dma_cleanup(hext_stream->link_substream,
						   hext_stream,
						   cpu_dai, codec_dai, false);
			if (ret < 0)
				return ret;

			/* for consistency with TRIGGER_SUSPEND we free DAI resources */
			ret = hda_dai_hw_free_ipc(hdac_stream(hext_stream)->direction, cpu_dai);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops ipc4_hda_dai_ops = {
	.hw_params = hda_dai_hw_params,
	.hw_free = hda_dai_hw_free,
	.trigger = ipc4_hda_dai_trigger,
	.prepare = hda_dai_prepare,
};

#endif

/* only one flag used so far to harden hw_params/hw_free/trigger/prepare */
struct ssp_dai_dma_data {
	bool setup;
};

static int ssp_dai_setup_or_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai,
				 bool setup)
{
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(dai, substream->stream);

	if (setup)
		return hda_ctrl_dai_widget_setup(w, SOF_DAI_CONFIG_FLAGS_NONE, NULL);

	return hda_ctrl_dai_widget_free(w, SOF_DAI_CONFIG_FLAGS_NONE, NULL);
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

static int ipc3_ssp_dai_trigger(struct snd_pcm_substream *substream,
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

static const struct snd_soc_dai_ops ipc3_ssp_dai_ops = {
	.startup = ssp_dai_startup,
	.hw_params = ssp_dai_hw_params,
	.prepare = ssp_dai_prepare,
	.trigger = ipc3_ssp_dai_trigger,
	.hw_free = ssp_dai_hw_free,
	.shutdown = ssp_dai_shutdown,
};

void hda_set_dai_drv_ops(struct snd_sof_dev *sdev, struct snd_sof_dsp_ops *ops)
{
	int i;

	switch (sdev->pdata->ipc_type) {
	case SOF_IPC:
		for (i = 0; i < ops->num_drv; i++) {
			if (strstr(ops->drv[i].name, "SSP")) {
				ops->drv[i].ops = &ipc3_ssp_dai_ops;
				continue;
			}
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
			if (strstr(ops->drv[i].name, "iDisp") ||
			    strstr(ops->drv[i].name, "Analog") ||
			    strstr(ops->drv[i].name, "Digital"))
				ops->drv[i].ops = &ipc3_hda_dai_ops;
#endif
		}
		break;
	case SOF_INTEL_IPC4:
	{
		struct sof_ipc4_fw_data *ipc4_data = sdev->private;

		for (i = 0; i < ops->num_drv; i++) {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
			if (strstr(ops->drv[i].name, "iDisp") ||
			    strstr(ops->drv[i].name, "Analog") ||
			    strstr(ops->drv[i].name, "Digital"))
				ops->drv[i].ops = &ipc4_hda_dai_ops;
#endif
		}

		if (!hda_use_tplg_nhlt)
			ipc4_data->nhlt = intel_nhlt_init(sdev->dev);

		break;
	}
	default:
		break;
	}
}

void hda_ops_free(struct snd_sof_dev *sdev)
{
	if (sdev->pdata->ipc_type == SOF_INTEL_IPC4) {
		struct sof_ipc4_fw_data *ipc4_data = sdev->private;

		if (!hda_use_tplg_nhlt)
			intel_nhlt_free(ipc4_data->nhlt);
	}
}
EXPORT_SYMBOL_NS(hda_ops_free, SND_SOC_SOF_INTEL_HDA_COMMON);

/*
 * common dai driver for skl+ platforms.
 * some products who use this DAI array only physically have a subset of
 * the DAIs, but no harm is done here by adding the whole set.
 */
struct snd_soc_dai_driver skl_dai[] = {
{
	.name = "SSP0 Pin",
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
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
{
	.name = "iDisp1 Pin",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp2 Pin",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp3 Pin",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "iDisp4 Pin",
	.playback = {
		.channels_min = 1,
		.channels_max = 8,
	},
},
{
	.name = "Analog CPU DAI",
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
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
	},
},
#endif
};

int hda_dsp_dais_suspend(struct snd_sof_dev *sdev)
{
	/*
	 * In the corner case where a SUSPEND happens during a PAUSE, the ALSA core
	 * does not throw the TRIGGER_SUSPEND. This leaves the DAIs in an unbalanced state.
	 * Since the component suspend is called last, we can trap this corner case
	 * and force the DAIs to release their resources.
	 */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	int ret;

	ret = hda_dai_suspend(sof_to_bus(sdev));
	if (ret < 0)
		return ret;
#endif

	return 0;
}
