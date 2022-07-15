// SPDX-License-Identifier: GPL-2.0-only
/*
 *  skl-pcm.c -ASoC HDA Platform driver file implementing PCM functionality
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author:  Jeeja KP <jeeja.kp@intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "skl.h"
#include "skl-topology.h"
#include "skl-sst-dsp.h"
#include "skl-sst-ipc.h"

#define HDA_MONO 1
#define HDA_STEREO 2
#define HDA_QUAD 4
#define HDA_MAX 8

static const struct snd_pcm_hardware azx_pcm_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_HAS_WALL_CLOCK | /* legacy */
				 SNDRV_PCM_INFO_HAS_LINK_ATIME |
				 SNDRV_PCM_INFO_NO_PERIOD_WAKEUP),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE |
				SNDRV_PCM_FMTBIT_S24_LE,
	.rates =		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_8000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		8,
	.buffer_bytes_max =	AZX_MAX_BUF_SIZE,
	.period_bytes_min =	128,
	.period_bytes_max =	AZX_MAX_BUF_SIZE / 2,
	.periods_min =		2,
	.periods_max =		AZX_MAX_FRAG,
	.fifo_size =		0,
};

static inline
struct hdac_ext_stream *get_hdac_ext_stream(struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}

static struct hdac_bus *get_bus_ctx(struct snd_pcm_substream *substream)
{
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);
	struct hdac_stream *hstream = hdac_stream(stream);
	struct hdac_bus *bus = hstream->bus;
	return bus;
}

static int skl_substream_alloc_pages(struct hdac_bus *bus,
				 struct snd_pcm_substream *substream,
				 size_t size)
{
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);

	hdac_stream(stream)->bufsize = 0;
	hdac_stream(stream)->period_bytes = 0;
	hdac_stream(stream)->format_val = 0;

	return 0;
}

static void skl_set_pcm_constrains(struct hdac_bus *bus,
				 struct snd_pcm_runtime *runtime)
{
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	/* avoid wrap-around with wall-clock */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_TIME,
				     20, 178000000);
}

static enum hdac_ext_stream_type skl_get_host_stream_type(struct hdac_bus *bus)
{
	if (bus->ppcap)
		return HDAC_EXT_STREAM_TYPE_HOST;
	else
		return HDAC_EXT_STREAM_TYPE_COUPLED;
}

/*
 * check if the stream opened is marked as ignore_suspend by machine, if so
 * then enable suspend_active refcount
 *
 * The count supend_active does not need lock as it is used in open/close
 * and suspend context
 */
static void skl_set_suspend_active(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai, bool enable)
{
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct snd_soc_dapm_widget *w;
	struct skl_dev *skl = bus_to_skl(bus);

	w = snd_soc_dai_get_widget(dai, substream->stream);

	if (w->ignore_suspend && enable)
		skl->supend_active++;
	else if (w->ignore_suspend && !enable)
		skl->supend_active--;
}

int skl_pcm_host_dma_prepare(struct device *dev, struct skl_pipe_params *params)
{
	struct hdac_bus *bus = dev_get_drvdata(dev);
	struct skl_dev *skl = bus_to_skl(bus);
	unsigned int format_val;
	struct hdac_stream *hstream;
	struct hdac_ext_stream *stream;
	int err;

	hstream = snd_hdac_get_stream(bus, params->stream,
					params->host_dma_id + 1);
	if (!hstream)
		return -EINVAL;

	stream = stream_to_hdac_ext_stream(hstream);
	snd_hdac_ext_stream_decouple(bus, stream, true);

	format_val = snd_hdac_calc_stream_format(params->s_freq,
			params->ch, params->format, params->host_bps, 0);

	dev_dbg(dev, "format_val=%d, rate=%d, ch=%d, format=%d\n",
		format_val, params->s_freq, params->ch, params->format);

	snd_hdac_stream_reset(hdac_stream(stream));
	err = snd_hdac_stream_set_params(hdac_stream(stream), format_val);
	if (err < 0)
		return err;

	/*
	 * The recommended SDxFMT programming sequence for BXT
	 * platforms is to couple the stream before writing the format
	 */
	if (IS_BXT(skl->pci)) {
		snd_hdac_ext_stream_decouple(bus, stream, false);
		err = snd_hdac_stream_setup(hdac_stream(stream));
		snd_hdac_ext_stream_decouple(bus, stream, true);
	} else {
		err = snd_hdac_stream_setup(hdac_stream(stream));
	}

	if (err < 0)
		return err;

	hdac_stream(stream)->prepared = 1;

	return 0;
}

int skl_pcm_link_dma_prepare(struct device *dev, struct skl_pipe_params *params)
{
	struct hdac_bus *bus = dev_get_drvdata(dev);
	unsigned int format_val;
	struct hdac_stream *hstream;
	struct hdac_ext_stream *stream;
	struct hdac_ext_link *link;
	unsigned char stream_tag;

	hstream = snd_hdac_get_stream(bus, params->stream,
					params->link_dma_id + 1);
	if (!hstream)
		return -EINVAL;

	stream = stream_to_hdac_ext_stream(hstream);
	snd_hdac_ext_stream_decouple(bus, stream, true);
	format_val = snd_hdac_calc_stream_format(params->s_freq, params->ch,
					params->format, params->link_bps, 0);

	dev_dbg(dev, "format_val=%d, rate=%d, ch=%d, format=%d\n",
		format_val, params->s_freq, params->ch, params->format);

	snd_hdac_ext_link_stream_reset(stream);

	snd_hdac_ext_link_stream_setup(stream, format_val);

	stream_tag = hstream->stream_tag;
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

static int skl_pcm_open(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct hdac_ext_stream *stream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct skl_dma_params *dma_params;
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	struct skl_module_cfg *mconfig;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	stream = snd_hdac_ext_stream_assign(bus, substream,
					skl_get_host_stream_type(bus));
	if (stream == NULL)
		return -EBUSY;

	skl_set_pcm_constrains(bus, runtime);

	/*
	 * disable WALLCLOCK timestamps for capture streams
	 * until we figure out how to handle digital inputs
	 */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_HAS_WALL_CLOCK; /* legacy */
		runtime->hw.info &= ~SNDRV_PCM_INFO_HAS_LINK_ATIME;
	}

	runtime->private_data = stream;

	dma_params = kzalloc(sizeof(*dma_params), GFP_KERNEL);
	if (!dma_params)
		return -ENOMEM;

	dma_params->stream_tag = hdac_stream(stream)->stream_tag;
	snd_soc_dai_set_dma_data(dai, substream, dma_params);

	dev_dbg(dai->dev, "stream tag set in dma params=%d\n",
				 dma_params->stream_tag);
	skl_set_suspend_active(substream, dai, true);
	snd_pcm_set_sync(substream);

	mconfig = skl_tplg_fe_get_cpr_module(dai, substream->stream);
	if (!mconfig)
		return -EINVAL;

	skl_tplg_d0i3_get(skl, mconfig->d0i3_caps);

	return 0;
}

static int skl_pcm_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	struct skl_module_cfg *mconfig;
	int ret;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	mconfig = skl_tplg_fe_get_cpr_module(dai, substream->stream);

	/*
	 * In case of XRUN recovery or in the case when the application
	 * calls prepare another time, reset the FW pipe to clean state
	 */
	if (mconfig &&
		(substream->runtime->status->state == SNDRV_PCM_STATE_XRUN ||
		 mconfig->pipe->state == SKL_PIPE_CREATED ||
		 mconfig->pipe->state == SKL_PIPE_PAUSED)) {

		ret = skl_reset_pipe(skl, mconfig->pipe);

		if (ret < 0)
			return ret;

		ret = skl_pcm_host_dma_prepare(dai->dev,
					mconfig->pipe->p_params);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int skl_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct skl_pipe_params p_params = {0};
	struct skl_module_cfg *m_cfg;
	int ret, dma_id;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);
	ret = skl_substream_alloc_pages(bus, substream,
					  params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	dev_dbg(dai->dev, "format_val, rate=%d, ch=%d, format=%d\n",
			runtime->rate, runtime->channels, runtime->format);

	dma_id = hdac_stream(stream)->stream_tag - 1;
	dev_dbg(dai->dev, "dma_id=%d\n", dma_id);

	p_params.s_fmt = snd_pcm_format_width(params_format(params));
	p_params.ch = params_channels(params);
	p_params.s_freq = params_rate(params);
	p_params.host_dma_id = dma_id;
	p_params.stream = substream->stream;
	p_params.format = params_format(params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		p_params.host_bps = dai->driver->playback.sig_bits;
	else
		p_params.host_bps = dai->driver->capture.sig_bits;


	m_cfg = skl_tplg_fe_get_cpr_module(dai, p_params.stream);
	if (m_cfg)
		skl_tplg_update_pipe_params(dai->dev, m_cfg, &p_params);

	return 0;
}

static void skl_pcm_close(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct skl_dma_params *dma_params = NULL;
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_module_cfg *mconfig;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	snd_hdac_ext_stream_release(stream, skl_get_host_stream_type(bus));

	dma_params = snd_soc_dai_get_dma_data(dai, substream);
	/*
	 * now we should set this to NULL as we are freeing by the
	 * dma_params
	 */
	snd_soc_dai_set_dma_data(dai, substream, NULL);
	skl_set_suspend_active(substream, dai, false);

	/*
	 * check if close is for "Reference Pin" and set back the
	 * CGCTL.MISCBDCGE if disabled by driver
	 */
	if (!strncmp(dai->name, "Reference Pin", 13) &&
			skl->miscbdcg_disabled) {
		skl->enable_miscbdcge(dai->dev, true);
		skl->miscbdcg_disabled = false;
	}

	mconfig = skl_tplg_fe_get_cpr_module(dai, substream->stream);
	if (mconfig)
		skl_tplg_d0i3_put(skl, mconfig->d0i3_caps);

	kfree(dma_params);
}

static int skl_pcm_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	struct skl_module_cfg *mconfig;
	int ret;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	mconfig = skl_tplg_fe_get_cpr_module(dai, substream->stream);

	if (mconfig) {
		ret = skl_reset_pipe(skl, mconfig->pipe);
		if (ret < 0)
			dev_err(dai->dev, "%s:Reset failed ret =%d",
						__func__, ret);
	}

	snd_hdac_stream_cleanup(hdac_stream(stream));
	hdac_stream(stream)->prepared = 0;

	return 0;
}

static int skl_be_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct skl_pipe_params p_params = {0};

	p_params.s_fmt = snd_pcm_format_width(params_format(params));
	p_params.ch = params_channels(params);
	p_params.s_freq = params_rate(params);
	p_params.stream = substream->stream;

	return skl_tplg_be_update_params(dai, &p_params);
}

static int skl_decoupled_trigger(struct snd_pcm_substream *substream,
		int cmd)
{
	struct hdac_bus *bus = get_bus_ctx(substream);
	struct hdac_ext_stream *stream;
	int start;
	unsigned long cookie;
	struct hdac_stream *hstr;

	stream = get_hdac_ext_stream(substream);
	hstr = hdac_stream(stream);

	if (!hstr->prepared)
		return -EPIPE;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		start = 1;
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		start = 0;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bus->reg_lock, cookie);

	if (start) {
		snd_hdac_stream_start(hdac_stream(stream), true);
		snd_hdac_stream_timecounter_init(hstr, 0);
	} else {
		snd_hdac_stream_stop(hdac_stream(stream));
	}

	spin_unlock_irqrestore(&bus->reg_lock, cookie);

	return 0;
}

static int skl_pcm_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	struct skl_module_cfg *mconfig;
	struct hdac_bus *bus = get_bus_ctx(substream);
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);
	struct snd_soc_dapm_widget *w;
	int ret;

	mconfig = skl_tplg_fe_get_cpr_module(dai, substream->stream);
	if (!mconfig)
		return -EIO;

	w = snd_soc_dai_get_widget(dai, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		if (!w->ignore_suspend) {
			/*
			 * enable DMA Resume enable bit for the stream, set the
			 * dpib & lpib position to resume before starting the
			 * DMA
			 */
			snd_hdac_ext_stream_drsm_enable(bus, true,
						hdac_stream(stream)->index);
			snd_hdac_ext_stream_set_dpibr(bus, stream,
							stream->lpib);
			snd_hdac_ext_stream_set_lpib(stream, stream->lpib);
		}
		fallthrough;

	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/*
		 * Start HOST DMA and Start FE Pipe.This is to make sure that
		 * there are no underrun/overrun in the case when the FE
		 * pipeline is started but there is a delay in starting the
		 * DMA channel on the host.
		 */
		ret = skl_decoupled_trigger(substream, cmd);
		if (ret < 0)
			return ret;
		return skl_run_pipe(skl, mconfig->pipe);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		/*
		 * Stop FE Pipe first and stop DMA. This is to make sure that
		 * there are no underrun/overrun in the case if there is a delay
		 * between the two operations.
		 */
		ret = skl_stop_pipe(skl, mconfig->pipe);
		if (ret < 0)
			return ret;

		ret = skl_decoupled_trigger(substream, cmd);
		if ((cmd == SNDRV_PCM_TRIGGER_SUSPEND) && !w->ignore_suspend) {
			/* save the dpib and lpib positions */
			stream->dpib = readl(bus->remap_addr +
					AZX_REG_VS_SDXDPIB_XBASE +
					(AZX_REG_VS_SDXDPIB_XINTERVAL *
					hdac_stream(stream)->index));

			stream->lpib = snd_hdac_stream_get_pos_lpib(
							hdac_stream(stream));
			snd_hdac_ext_stream_decouple(bus, stream, false);
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}


static int skl_link_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct hdac_ext_stream *link_dev;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct skl_pipe_params p_params = {0};
	struct hdac_ext_link *link;
	int stream_tag;

	link_dev = snd_hdac_ext_stream_assign(bus, substream,
					HDAC_EXT_STREAM_TYPE_LINK);
	if (!link_dev)
		return -EBUSY;

	snd_soc_dai_set_dma_data(dai, substream, (void *)link_dev);

	link = snd_hdac_ext_bus_get_link(bus, codec_dai->component->name);
	if (!link)
		return -EINVAL;

	stream_tag = hdac_stream(link_dev)->stream_tag;

	/* set the stream tag in the codec dai dma params  */
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

	return skl_tplg_be_update_params(dai, &p_params);
}

static int skl_link_pcm_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct skl_dev *skl = get_skl_ctx(dai->dev);
	struct skl_module_cfg *mconfig = NULL;

	/* In case of XRUN recovery, reset the FW pipe to clean state */
	mconfig = skl_tplg_be_get_cpr_module(dai, substream->stream);
	if (mconfig && !mconfig->pipe->passthru &&
		(substream->runtime->status->state == SNDRV_PCM_STATE_XRUN))
		skl_reset_pipe(skl, mconfig->pipe);

	return 0;
}

static int skl_link_pcm_trigger(struct snd_pcm_substream *substream,
	int cmd, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *link_dev =
				snd_soc_dai_get_dma_data(dai, substream);
	struct hdac_bus *bus = get_bus_ctx(substream);
	struct hdac_ext_stream *stream = get_hdac_ext_stream(substream);

	dev_dbg(dai->dev, "In %s cmd=%d\n", __func__, cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_link_stream_start(link_dev);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		snd_hdac_ext_link_stream_clear(link_dev);
		if (cmd == SNDRV_PCM_TRIGGER_SUSPEND)
			snd_hdac_ext_stream_decouple(bus, stream, false);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int skl_link_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct hdac_ext_stream *link_dev =
				snd_soc_dai_get_dma_data(dai, substream);
	struct hdac_ext_link *link;
	unsigned char stream_tag;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	link_dev->link_prepared = 0;

	link = snd_hdac_ext_bus_get_link(bus, asoc_rtd_to_codec(rtd, 0)->component->name);
	if (!link)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		stream_tag = hdac_stream(link_dev)->stream_tag;
		snd_hdac_ext_link_clear_stream_id(link, stream_tag);
	}

	snd_hdac_ext_stream_release(link_dev, HDAC_EXT_STREAM_TYPE_LINK);
	return 0;
}

static const struct snd_soc_dai_ops skl_pcm_dai_ops = {
	.startup = skl_pcm_open,
	.shutdown = skl_pcm_close,
	.prepare = skl_pcm_prepare,
	.hw_params = skl_pcm_hw_params,
	.hw_free = skl_pcm_hw_free,
	.trigger = skl_pcm_trigger,
};

static const struct snd_soc_dai_ops skl_dmic_dai_ops = {
	.hw_params = skl_be_hw_params,
};

static const struct snd_soc_dai_ops skl_be_ssp_dai_ops = {
	.hw_params = skl_be_hw_params,
};

static const struct snd_soc_dai_ops skl_link_dai_ops = {
	.prepare = skl_link_pcm_prepare,
	.hw_params = skl_link_hw_params,
	.hw_free = skl_link_hw_free,
	.trigger = skl_link_pcm_trigger,
};

static struct snd_soc_dai_driver skl_fe_dai[] = {
{
	.name = "System Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "System Playback",
		.channels_min = HDA_MONO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 32,
	},
	.capture = {
		.stream_name = "System Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sig_bits = 32,
	},
},
{
	.name = "System Pin2",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "Headset Playback",
		.channels_min = HDA_MONO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
},
{
	.name = "Echoref Pin",
	.ops = &skl_pcm_dai_ops,
	.capture = {
		.stream_name = "Echoreference Capture",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
},
{
	.name = "Reference Pin",
	.ops = &skl_pcm_dai_ops,
	.capture = {
		.stream_name = "Reference Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_QUAD,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sig_bits = 32,
	},
},
{
	.name = "Deepbuffer Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "Deepbuffer Playback",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sig_bits = 32,
	},
},
{
	.name = "LowLatency Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "Low Latency Playback",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sig_bits = 32,
	},
},
{
	.name = "DMIC Pin",
	.ops = &skl_pcm_dai_ops,
	.capture = {
		.stream_name = "DMIC Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_QUAD,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
		.sig_bits = 32,
	},
},
{
	.name = "HDMI1 Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "HDMI1 Playback",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |	SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 32,
	},
},
{
	.name = "HDMI2 Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "HDMI2 Playback",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |	SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 32,
	},
},
{
	.name = "HDMI3 Pin",
	.ops = &skl_pcm_dai_ops,
	.playback = {
		.stream_name = "HDMI3 Playback",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |	SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 32,
	},
},
};

/* BE CPU  Dais */
static struct snd_soc_dai_driver skl_platform_dai[] = {
{
	.name = "SSP0 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp0 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp0 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "SSP1 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp1 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp1 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "SSP2 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp2 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp2 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "SSP3 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp3 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp3 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "SSP4 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp4 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp4 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "SSP5 Pin",
	.ops = &skl_be_ssp_dai_ops,
	.playback = {
		.stream_name = "ssp5 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp5 Rx",
		.channels_min = HDA_STEREO,
		.channels_max = HDA_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "iDisp1 Pin",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "iDisp1 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "iDisp2 Pin",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "iDisp2 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "iDisp3 Pin",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "iDisp3 Tx",
		.channels_min = HDA_STEREO,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "DMIC01 Pin",
	.ops = &skl_dmic_dai_ops,
	.capture = {
		.stream_name = "DMIC01 Rx",
		.channels_min = HDA_MONO,
		.channels_max = HDA_QUAD,
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "DMIC16k Pin",
	.ops = &skl_dmic_dai_ops,
	.capture = {
		.stream_name = "DMIC16k Rx",
		.channels_min = HDA_MONO,
		.channels_max = HDA_QUAD,
		.rates = SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "Analog CPU DAI",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "Analog CPU Playback",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "Analog CPU Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
},
{
	.name = "Alt Analog CPU DAI",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "Alt Analog CPU Playback",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "Alt Analog CPU Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
},
{
	.name = "Digital CPU DAI",
	.ops = &skl_link_dai_ops,
	.playback = {
		.stream_name = "Digital CPU Playback",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "Digital CPU Capture",
		.channels_min = HDA_MONO,
		.channels_max = HDA_MAX,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32_LE,
	},
},
};

int skl_dai_load(struct snd_soc_component *cmp, int index,
			struct snd_soc_dai_driver *dai_drv,
			struct snd_soc_tplg_pcm *pcm, struct snd_soc_dai *dai)
{
	dai_drv->ops = &skl_pcm_dai_ops;

	return 0;
}

static int skl_platform_soc_open(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	dev_dbg(asoc_rtd_to_cpu(rtd, 0)->dev, "In %s:%s\n", __func__,
					dai_link->cpus->dai_name);

	snd_soc_set_runtime_hwparams(substream, &azx_pcm_hw);

	return 0;
}

static int skl_coupled_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct hdac_bus *bus = get_bus_ctx(substream);
	struct hdac_ext_stream *stream;
	struct snd_pcm_substream *s;
	bool start;
	int sbits = 0;
	unsigned long cookie;
	struct hdac_stream *hstr;

	stream = get_hdac_ext_stream(substream);
	hstr = hdac_stream(stream);

	dev_dbg(bus->dev, "In %s cmd=%d\n", __func__, cmd);

	if (!hstr->prepared)
		return -EPIPE;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		start = true;
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		start = false;
		break;

	default:
		return -EINVAL;
	}

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		stream = get_hdac_ext_stream(s);
		sbits |= 1 << hdac_stream(stream)->index;
		snd_pcm_trigger_done(s, substream);
	}

	spin_lock_irqsave(&bus->reg_lock, cookie);

	/* first, set SYNC bits of corresponding streams */
	snd_hdac_stream_sync_trigger(hstr, true, sbits, AZX_REG_SSYNC);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		stream = get_hdac_ext_stream(s);
		if (start)
			snd_hdac_stream_start(hdac_stream(stream), true);
		else
			snd_hdac_stream_stop(hdac_stream(stream));
	}
	spin_unlock_irqrestore(&bus->reg_lock, cookie);

	snd_hdac_stream_sync(hstr, start, sbits);

	spin_lock_irqsave(&bus->reg_lock, cookie);

	/* reset SYNC bits */
	snd_hdac_stream_sync_trigger(hstr, false, sbits, AZX_REG_SSYNC);
	if (start)
		snd_hdac_stream_timecounter_init(hstr, sbits);
	spin_unlock_irqrestore(&bus->reg_lock, cookie);

	return 0;
}

static int skl_platform_soc_trigger(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream,
				    int cmd)
{
	struct hdac_bus *bus = get_bus_ctx(substream);

	if (!bus->ppcap)
		return skl_coupled_trigger(substream, cmd);

	return 0;
}

static snd_pcm_uframes_t skl_platform_soc_pointer(
	struct snd_soc_component *component,
	struct snd_pcm_substream *substream)
{
	struct hdac_ext_stream *hstream = get_hdac_ext_stream(substream);
	struct hdac_bus *bus = get_bus_ctx(substream);
	unsigned int pos;

	/*
	 * Use DPIB for Playback stream as the periodic DMA Position-in-
	 * Buffer Writes may be scheduled at the same time or later than
	 * the MSI and does not guarantee to reflect the Position of the
	 * last buffer that was transferred. Whereas DPIB register in
	 * HAD space reflects the actual data that is transferred.
	 * Use the position buffer for capture, as DPIB write gets
	 * completed earlier than the actual data written to the DDR.
	 *
	 * For capture stream following workaround is required to fix the
	 * incorrect position reporting.
	 *
	 * 1. Wait for 20us before reading the DMA position in buffer once
	 * the interrupt is generated for stream completion as update happens
	 * on the HDA frame boundary i.e. 20.833uSec.
	 * 2. Read DPIB register to flush the DMA position value. This dummy
	 * read is required to flush DMA position value.
	 * 3. Read the DMA Position-in-Buffer. This value now will be equal to
	 * or greater than period boundary.
	 */

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pos = readl(bus->remap_addr + AZX_REG_VS_SDXDPIB_XBASE +
				(AZX_REG_VS_SDXDPIB_XINTERVAL *
				hdac_stream(hstream)->index));
	} else {
		udelay(20);
		readl(bus->remap_addr +
				AZX_REG_VS_SDXDPIB_XBASE +
				(AZX_REG_VS_SDXDPIB_XINTERVAL *
				 hdac_stream(hstream)->index));
		pos = snd_hdac_stream_get_pos_posbuf(hdac_stream(hstream));
	}

	if (pos >= hdac_stream(hstream)->bufsize)
		pos = 0;

	return bytes_to_frames(substream->runtime, pos);
}

static int skl_platform_soc_mmap(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream,
				 struct vm_area_struct *area)
{
	return snd_pcm_lib_default_mmap(substream, area);
}

static u64 skl_adjust_codec_delay(struct snd_pcm_substream *substream,
				u64 nsec)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	u64 codec_frames, codec_nsecs;

	if (!codec_dai->driver->ops->delay)
		return nsec;

	codec_frames = codec_dai->driver->ops->delay(substream, codec_dai);
	codec_nsecs = div_u64(codec_frames * 1000000000LL,
			      substream->runtime->rate);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return nsec + codec_nsecs;

	return (nsec > codec_nsecs) ? nsec - codec_nsecs : 0;
}

static int skl_platform_soc_get_time_info(
			struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct timespec64 *system_ts, struct timespec64 *audio_ts,
			struct snd_pcm_audio_tstamp_config *audio_tstamp_config,
			struct snd_pcm_audio_tstamp_report *audio_tstamp_report)
{
	struct hdac_ext_stream *sstream = get_hdac_ext_stream(substream);
	struct hdac_stream *hstr = hdac_stream(sstream);
	u64 nsec;

	if ((substream->runtime->hw.info & SNDRV_PCM_INFO_HAS_LINK_ATIME) &&
		(audio_tstamp_config->type_requested == SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK)) {

		snd_pcm_gettime(substream->runtime, system_ts);

		nsec = timecounter_read(&hstr->tc);
		nsec = div_u64(nsec, 3); /* can be optimized */
		if (audio_tstamp_config->report_delay)
			nsec = skl_adjust_codec_delay(substream, nsec);

		*audio_ts = ns_to_timespec64(nsec);

		audio_tstamp_report->actual_type = SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK;
		audio_tstamp_report->accuracy_report = 1; /* rest of struct is valid */
		audio_tstamp_report->accuracy = 42; /* 24MHzWallClk == 42ns resolution */

	} else {
		audio_tstamp_report->actual_type = SNDRV_PCM_AUDIO_TSTAMP_TYPE_DEFAULT;
	}

	return 0;
}

#define MAX_PREALLOC_SIZE	(32 * 1024 * 1024)

static int skl_platform_soc_new(struct snd_soc_component *component,
				struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct hdac_bus *bus = dev_get_drvdata(dai->dev);
	struct snd_pcm *pcm = rtd->pcm;
	unsigned int size;
	struct skl_dev *skl = bus_to_skl(bus);

	if (dai->driver->playback.channels_min ||
		dai->driver->capture.channels_min) {
		/* buffer pre-allocation */
		size = CONFIG_SND_HDA_PREALLOC_SIZE * 1024;
		if (size > MAX_PREALLOC_SIZE)
			size = MAX_PREALLOC_SIZE;
		snd_pcm_set_managed_buffer_all(pcm,
					       SNDRV_DMA_TYPE_DEV_SG,
					       &skl->pci->dev,
					       size, MAX_PREALLOC_SIZE);
	}

	return 0;
}

static int skl_get_module_info(struct skl_dev *skl,
		struct skl_module_cfg *mconfig)
{
	struct skl_module_inst_id *pin_id;
	guid_t *uuid_mod, *uuid_tplg;
	struct skl_module *skl_module;
	struct uuid_module *module;
	int i, ret = -EIO;

	uuid_mod = (guid_t *)mconfig->guid;

	if (list_empty(&skl->uuid_list)) {
		dev_err(skl->dev, "Module list is empty\n");
		return -EIO;
	}

	for (i = 0; i < skl->nr_modules; i++) {
		skl_module = skl->modules[i];
		uuid_tplg = &skl_module->uuid;
		if (guid_equal(uuid_mod, uuid_tplg)) {
			mconfig->module = skl_module;
			ret = 0;
			break;
		}
	}

	if (skl->nr_modules && ret)
		return ret;

	ret = -EIO;
	list_for_each_entry(module, &skl->uuid_list, list) {
		if (guid_equal(uuid_mod, &module->uuid)) {
			mconfig->id.module_id = module->id;
			mconfig->module->loadable = module->is_loadable;
			ret = 0;
		}

		for (i = 0; i < MAX_IN_QUEUE; i++) {
			pin_id = &mconfig->m_in_pin[i].id;
			if (guid_equal(&pin_id->mod_uuid, &module->uuid))
				pin_id->module_id = module->id;
		}

		for (i = 0; i < MAX_OUT_QUEUE; i++) {
			pin_id = &mconfig->m_out_pin[i].id;
			if (guid_equal(&pin_id->mod_uuid, &module->uuid))
				pin_id->module_id = module->id;
		}
	}

	return ret;
}

static int skl_populate_modules(struct skl_dev *skl)
{
	struct skl_pipeline *p;
	struct skl_pipe_module *m;
	struct snd_soc_dapm_widget *w;
	struct skl_module_cfg *mconfig;
	int ret = 0;

	list_for_each_entry(p, &skl->ppl_list, node) {
		list_for_each_entry(m, &p->pipe->w_list, node) {
			w = m->w;
			mconfig = w->priv;

			ret = skl_get_module_info(skl, mconfig);
			if (ret < 0) {
				dev_err(skl->dev,
					"query module info failed\n");
				return ret;
			}

			skl_tplg_add_moduleid_in_bind_params(skl, w);
		}
	}

	return ret;
}

static int skl_platform_soc_probe(struct snd_soc_component *component)
{
	struct hdac_bus *bus = dev_get_drvdata(component->dev);
	struct skl_dev *skl = bus_to_skl(bus);
	const struct skl_dsp_ops *ops;
	int ret;

	pm_runtime_get_sync(component->dev);
	if (bus->ppcap) {
		skl->component = component;

		/* init debugfs */
		skl->debugfs = skl_debugfs_init(skl);

		ret = skl_tplg_init(component, bus);
		if (ret < 0) {
			dev_err(component->dev, "Failed to init topology!\n");
			return ret;
		}

		/* load the firmwares, since all is set */
		ops = skl_get_dsp_ops(skl->pci->device);
		if (!ops)
			return -EIO;

		/*
		 * Disable dynamic clock and power gating during firmware
		 * and library download
		 */
		skl->enable_miscbdcge(component->dev, false);
		skl->clock_power_gating(component->dev, false);

		ret = ops->init_fw(component->dev, skl);
		skl->enable_miscbdcge(component->dev, true);
		skl->clock_power_gating(component->dev, true);
		if (ret < 0) {
			dev_err(component->dev, "Failed to boot first fw: %d\n", ret);
			return ret;
		}
		skl_populate_modules(skl);
		skl->update_d0i3c = skl_update_d0i3c;

		if (skl->cfg.astate_cfg != NULL) {
			skl_dsp_set_astate_cfg(skl,
					skl->cfg.astate_cfg->count,
					skl->cfg.astate_cfg);
		}
	}
	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

static void skl_platform_soc_remove(struct snd_soc_component *component)
{
	struct hdac_bus *bus = dev_get_drvdata(component->dev);
	struct skl_dev *skl = bus_to_skl(bus);

	skl_tplg_exit(component, bus);

	skl_debugfs_exit(skl);
}

static const struct snd_soc_component_driver skl_component  = {
	.name		= "pcm",
	.probe		= skl_platform_soc_probe,
	.remove		= skl_platform_soc_remove,
	.open		= skl_platform_soc_open,
	.trigger	= skl_platform_soc_trigger,
	.pointer	= skl_platform_soc_pointer,
	.get_time_info	= skl_platform_soc_get_time_info,
	.mmap		= skl_platform_soc_mmap,
	.pcm_construct	= skl_platform_soc_new,
	.module_get_upon_open = 1, /* increment refcount when a pcm is opened */
};

int skl_platform_register(struct device *dev)
{
	int ret;
	struct snd_soc_dai_driver *dais;
	int num_dais = ARRAY_SIZE(skl_platform_dai);
	struct hdac_bus *bus = dev_get_drvdata(dev);
	struct skl_dev *skl = bus_to_skl(bus);

	skl->dais = kmemdup(skl_platform_dai, sizeof(skl_platform_dai),
			    GFP_KERNEL);
	if (!skl->dais) {
		ret = -ENOMEM;
		goto err;
	}

	if (!skl->use_tplg_pcm) {
		dais = krealloc(skl->dais, sizeof(skl_fe_dai) +
				sizeof(skl_platform_dai), GFP_KERNEL);
		if (!dais) {
			ret = -ENOMEM;
			goto err;
		}

		skl->dais = dais;
		memcpy(&skl->dais[ARRAY_SIZE(skl_platform_dai)], skl_fe_dai,
		       sizeof(skl_fe_dai));
		num_dais += ARRAY_SIZE(skl_fe_dai);
	}

	ret = devm_snd_soc_register_component(dev, &skl_component,
					 skl->dais, num_dais);
	if (ret)
		dev_err(dev, "soc component registration failed %d\n", ret);
err:
	return ret;
}

int skl_platform_unregister(struct device *dev)
{
	struct hdac_bus *bus = dev_get_drvdata(dev);
	struct skl_dev *skl = bus_to_skl(bus);
	struct skl_module_deferred_bind *modules, *tmp;

	list_for_each_entry_safe(modules, tmp, &skl->bind_list, node) {
		list_del(&modules->node);
		kfree(modules);
	}

	kfree(skl->dais);

	return 0;
}
