// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation

#include <sound/pcm_params.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda-mlink.h>
#include <sound/sof/ipc4/header.h>
#include <uapi/sound/sof/header.h>
#include "../ipc4-priv.h"
#include "../ipc4-topology.h"
#include "../sof-priv.h"
#include "../sof-audio.h"
#include "hda.h"

/* These ops are only applicable for the HDA DAI's in their current form */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_LINK)
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
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
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

static struct hdac_ext_stream *hda_ipc4_get_hext_stream(struct snd_sof_dev *sdev,
							struct snd_soc_dai *cpu_dai,
							struct snd_pcm_substream *substream)
{
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_widget *swidget;
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(cpu_dai, substream->stream);
	swidget = w->dobj.private;
	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;

	/* mark pipeline so that it can be skipped during FE trigger */
	pipeline->skip_during_fe_trigger = true;

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

static void hda_setup_hext_stream(struct snd_sof_dev *sdev, struct hdac_ext_stream *hext_stream,
				  unsigned int format_val)
{
	snd_hdac_ext_stream_setup(hext_stream, format_val);
}

static void hda_reset_hext_stream(struct snd_sof_dev *sdev, struct hdac_ext_stream *hext_stream)
{
	snd_hdac_ext_stream_reset(hext_stream);
}

static void hda_codec_dai_set_stream(struct snd_sof_dev *sdev,
				     struct snd_pcm_substream *substream,
				     struct hdac_stream *hstream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);

	/* set the hdac_stream in the codec dai */
	snd_soc_dai_set_stream(codec_dai, hstream, substream->stream);
}

static unsigned int hda_calc_stream_format(struct snd_sof_dev *sdev,
					   struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int link_bps;
	unsigned int format_val;
	unsigned int bits;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		link_bps = codec_dai->driver->playback.sig_bits;
	else
		link_bps = codec_dai->driver->capture.sig_bits;

	bits = snd_hdac_stream_format_bits(params_format(params), SNDRV_PCM_SUBFORMAT_STD,
					   link_bps);
	format_val = snd_hdac_stream_format(params_channels(params), bits, params_rate(params));

	dev_dbg(sdev->dev, "format_val=%#x, rate=%d, ch=%d, format=%d\n", format_val,
		params_rate(params), params_channels(params), params_format(params));

	return format_val;
}

static struct hdac_ext_link *hda_get_hlink(struct snd_sof_dev *sdev,
					   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct hdac_bus *bus = sof_to_bus(sdev);

	return snd_hdac_ext_bus_get_hlink_by_name(bus, codec_dai->component->name);
}

static unsigned int generic_calc_stream_format(struct snd_sof_dev *sdev,
					       struct snd_pcm_substream *substream,
					       struct snd_pcm_hw_params *params)
{
	unsigned int format_val;
	unsigned int bits;

	bits = snd_hdac_stream_format_bits(params_format(params), SNDRV_PCM_SUBFORMAT_STD,
					   params_physical_width(params));
	format_val = snd_hdac_stream_format(params_channels(params), bits, params_rate(params));

	dev_dbg(sdev->dev, "format_val=%#x, rate=%d, ch=%d, format=%d\n", format_val,
		params_rate(params), params_channels(params), params_format(params));

	return format_val;
}

static unsigned int dmic_calc_stream_format(struct snd_sof_dev *sdev,
					    struct snd_pcm_substream *substream,
					    struct snd_pcm_hw_params *params)
{
	unsigned int format_val;
	snd_pcm_format_t format;
	unsigned int channels;
	unsigned int width;
	unsigned int bits;

	channels = params_channels(params);
	format = params_format(params);
	width = params_physical_width(params);

	if (format == SNDRV_PCM_FORMAT_S16_LE) {
		format = SNDRV_PCM_FORMAT_S32_LE;
		channels /= 2;
		width = 32;
	}

	bits = snd_hdac_stream_format_bits(format, SNDRV_PCM_SUBFORMAT_STD, width);
	format_val = snd_hdac_stream_format(channels, bits, params_rate(params));

	dev_dbg(sdev->dev, "format_val=%#x, rate=%d, ch=%d, format=%d\n", format_val,
		params_rate(params), channels, format);

	return format_val;
}

static struct hdac_ext_link *ssp_get_hlink(struct snd_sof_dev *sdev,
					   struct snd_pcm_substream *substream)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	return hdac_bus_eml_ssp_get_hlink(bus);
}

static struct hdac_ext_link *dmic_get_hlink(struct snd_sof_dev *sdev,
					    struct snd_pcm_substream *substream)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	return hdac_bus_eml_dmic_get_hlink(bus);
}

static struct hdac_ext_link *sdw_get_hlink(struct snd_sof_dev *sdev,
					   struct snd_pcm_substream *substream)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	return hdac_bus_eml_sdw_get_hlink(bus);
}

static int hda_ipc4_pre_trigger(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
				struct snd_pcm_substream *substream, int cmd)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_widget *swidget;
	struct snd_soc_dapm_widget *w;
	int ret = 0;

	w = snd_soc_dai_get_widget(cpu_dai, substream->stream);
	swidget = w->dobj.private;
	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;

	if (pipe_widget->instance_id < 0)
		return 0;

	mutex_lock(&ipc4_data->pipeline_state_mutex);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_PAUSED);
		if (ret < 0)
			goto out;

		pipeline->state = SOF_IPC4_PIPE_PAUSED;
		break;
	default:
		dev_err(sdev->dev, "unknown trigger command %d\n", cmd);
		ret = -EINVAL;
	}
out:
	mutex_unlock(&ipc4_data->pipeline_state_mutex);
	return ret;
}

static int hda_trigger(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
		       struct snd_pcm_substream *substream, int cmd)
{
	struct hdac_ext_stream *hext_stream = snd_soc_dai_get_dma_data(cpu_dai, substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_stream_start(hext_stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_hdac_ext_stream_clear(hext_stream);

		/*
		 * Save the LLP registers in case the stream is
		 * restarting due PAUSE_RELEASE, or START without a pcm
		 * close/open since in this case the LLP register is not reset
		 * to 0 and the delay calculation will return with invalid
		 * results.
		 */
		hext_stream->pplcllpl = readl(hext_stream->pplc_addr + AZX_REG_PPLCLLPL);
		hext_stream->pplcllpu = readl(hext_stream->pplc_addr + AZX_REG_PPLCLLPU);
		break;
	default:
		dev_err(sdev->dev, "unknown trigger command %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static int hda_ipc4_post_trigger(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
				 struct snd_pcm_substream *substream, int cmd)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct snd_sof_widget *swidget;
	struct snd_soc_dapm_widget *w;
	int ret = 0;

	w = snd_soc_dai_get_widget(cpu_dai, substream->stream);
	swidget = w->dobj.private;
	pipe_widget = swidget->spipe->pipe_widget;
	pipeline = pipe_widget->private;

	if (pipe_widget->instance_id < 0)
		return 0;

	mutex_lock(&ipc4_data->pipeline_state_mutex);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (pipeline->state != SOF_IPC4_PIPE_PAUSED) {
			ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
							  SOF_IPC4_PIPE_PAUSED);
			if (ret < 0)
				goto out;
			pipeline->state = SOF_IPC4_PIPE_PAUSED;
		}

		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_RUNNING);
		if (ret < 0)
			goto out;
		pipeline->state = SOF_IPC4_PIPE_RUNNING;
		swidget->spipe->started_count++;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = sof_ipc4_set_pipeline_state(sdev, pipe_widget->instance_id,
						  SOF_IPC4_PIPE_RUNNING);
		if (ret < 0)
			goto out;
		pipeline->state = SOF_IPC4_PIPE_RUNNING;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		/*
		 * STOP/SUSPEND trigger is invoked only once when all users of this pipeline have
		 * been stopped. So, clear the started_count so that the pipeline can be reset
		 */
		swidget->spipe->started_count = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		dev_err(sdev->dev, "unknown trigger command %d\n", cmd);
		ret = -EINVAL;
		break;
	}
out:
	mutex_unlock(&ipc4_data->pipeline_state_mutex);
	return ret;
}

static const struct hda_dai_widget_dma_ops hda_ipc4_dma_ops = {
	.get_hext_stream = hda_ipc4_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.pre_trigger = hda_ipc4_pre_trigger,
	.trigger = hda_trigger,
	.post_trigger = hda_ipc4_post_trigger,
	.codec_dai_set_stream = hda_codec_dai_set_stream,
	.calc_stream_format = hda_calc_stream_format,
	.get_hlink = hda_get_hlink,
};

static const struct hda_dai_widget_dma_ops ssp_ipc4_dma_ops = {
	.get_hext_stream = hda_ipc4_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.pre_trigger = hda_ipc4_pre_trigger,
	.trigger = hda_trigger,
	.post_trigger = hda_ipc4_post_trigger,
	.calc_stream_format = generic_calc_stream_format,
	.get_hlink = ssp_get_hlink,
};

static const struct hda_dai_widget_dma_ops dmic_ipc4_dma_ops = {
	.get_hext_stream = hda_ipc4_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.pre_trigger = hda_ipc4_pre_trigger,
	.trigger = hda_trigger,
	.post_trigger = hda_ipc4_post_trigger,
	.calc_stream_format = dmic_calc_stream_format,
	.get_hlink = dmic_get_hlink,
};

static const struct hda_dai_widget_dma_ops sdw_ipc4_dma_ops = {
	.get_hext_stream = hda_ipc4_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.pre_trigger = hda_ipc4_pre_trigger,
	.trigger = hda_trigger,
	.post_trigger = hda_ipc4_post_trigger,
	.calc_stream_format = generic_calc_stream_format,
	.get_hlink = sdw_get_hlink,
};

static const struct hda_dai_widget_dma_ops hda_ipc4_chain_dma_ops = {
	.get_hext_stream = hda_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.trigger = hda_trigger,
	.codec_dai_set_stream = hda_codec_dai_set_stream,
	.calc_stream_format = hda_calc_stream_format,
	.get_hlink = hda_get_hlink,
};

static const struct hda_dai_widget_dma_ops sdw_ipc4_chain_dma_ops = {
	.get_hext_stream = hda_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.trigger = hda_trigger,
	.calc_stream_format = generic_calc_stream_format,
	.get_hlink = sdw_get_hlink,
};

static int hda_ipc3_post_trigger(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
				 struct snd_pcm_substream *substream, int cmd)
{
	struct hdac_ext_stream *hext_stream = hda_get_hext_stream(sdev, cpu_dai, substream);
	struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget(cpu_dai, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		struct snd_sof_dai_config_data data = { 0 };
		int ret;

		data.dai_data = DMA_CHAN_INVALID;
		ret = hda_dai_config(w, SOF_DAI_CONFIG_FLAGS_HW_FREE, &data);
		if (ret < 0)
			return ret;

		if (cmd == SNDRV_PCM_TRIGGER_STOP)
			return hda_link_dma_cleanup(substream, hext_stream, cpu_dai);

		break;
	}
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		return hda_dai_config(w, SOF_DAI_CONFIG_FLAGS_PAUSE, NULL);
	default:
		break;
	}

	return 0;
}

static const struct hda_dai_widget_dma_ops hda_ipc3_dma_ops = {
	.get_hext_stream = hda_get_hext_stream,
	.assign_hext_stream = hda_assign_hext_stream,
	.release_hext_stream = hda_release_hext_stream,
	.setup_hext_stream = hda_setup_hext_stream,
	.reset_hext_stream = hda_reset_hext_stream,
	.trigger = hda_trigger,
	.post_trigger = hda_ipc3_post_trigger,
	.codec_dai_set_stream = hda_codec_dai_set_stream,
	.calc_stream_format = hda_calc_stream_format,
	.get_hlink = hda_get_hlink,
};

static struct hdac_ext_stream *
hda_dspless_get_hext_stream(struct snd_sof_dev *sdev, struct snd_soc_dai *cpu_dai,
			    struct snd_pcm_substream *substream)
{
	struct hdac_stream *hstream = substream->runtime->private_data;

	return stream_to_hdac_ext_stream(hstream);
}

static void hda_dspless_setup_hext_stream(struct snd_sof_dev *sdev,
					  struct hdac_ext_stream *hext_stream,
					  unsigned int format_val)
{
	/*
	 * Save the format_val which was adjusted by the maxbps of the codec.
	 * This information is not available on the FE side since there we are
	 * using dummy_codec.
	 */
	hext_stream->hstream.format_val = format_val;
}

static const struct hda_dai_widget_dma_ops hda_dspless_dma_ops = {
	.get_hext_stream = hda_dspless_get_hext_stream,
	.setup_hext_stream = hda_dspless_setup_hext_stream,
	.codec_dai_set_stream = hda_codec_dai_set_stream,
	.calc_stream_format = hda_calc_stream_format,
	.get_hlink = hda_get_hlink,
};

static const struct hda_dai_widget_dma_ops sdw_dspless_dma_ops = {
	.get_hext_stream = hda_dspless_get_hext_stream,
	.setup_hext_stream = hda_dspless_setup_hext_stream,
	.calc_stream_format = generic_calc_stream_format,
	.get_hlink = sdw_get_hlink,
};

#endif

const struct hda_dai_widget_dma_ops *
hda_select_dai_widget_ops(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_LINK)
	struct snd_sof_dai *sdai;
	const struct sof_intel_dsp_desc *chip;

	chip = get_chip_info(sdev->pdata);
	sdai = swidget->private;

	if (sdev->dspless_mode_selected) {
		switch (sdai->type) {
		case SOF_DAI_INTEL_HDA:
			return &hda_dspless_dma_ops;
		case SOF_DAI_INTEL_ALH:
			if (chip->hw_ip_version < SOF_INTEL_ACE_2_0)
				return NULL;
			return &sdw_dspless_dma_ops;
		default:
			return NULL;
		}
	}

	switch (sdev->pdata->ipc_type) {
	case SOF_IPC_TYPE_3:
	{
		struct sof_dai_private_data *private = sdai->private;

		if (private->dai_config->type == SOF_DAI_INTEL_HDA)
			return &hda_ipc3_dma_ops;
		break;
	}
	case SOF_IPC_TYPE_4:
	{
		struct snd_sof_widget *pipe_widget = swidget->spipe->pipe_widget;
		struct sof_ipc4_pipeline *pipeline = pipe_widget->private;

		switch (sdai->type) {
		case SOF_DAI_INTEL_HDA:
			if (pipeline->use_chain_dma)
				return &hda_ipc4_chain_dma_ops;

			return &hda_ipc4_dma_ops;
		case SOF_DAI_INTEL_SSP:
			if (chip->hw_ip_version < SOF_INTEL_ACE_2_0)
				return NULL;
			return &ssp_ipc4_dma_ops;
		case SOF_DAI_INTEL_DMIC:
			if (chip->hw_ip_version < SOF_INTEL_ACE_2_0)
				return NULL;
			return &dmic_ipc4_dma_ops;
		case SOF_DAI_INTEL_ALH:
			if (chip->hw_ip_version < SOF_INTEL_ACE_2_0)
				return NULL;
			if (pipeline->use_chain_dma)
				return &sdw_ipc4_chain_dma_ops;
			return &sdw_ipc4_dma_ops;

		default:
			break;
		}
		break;
	}
	default:
		break;
	}
#endif
	return NULL;
}
