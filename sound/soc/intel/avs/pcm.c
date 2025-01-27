// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/device.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>
#include <sound/pcm_params.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include <sound/soc-component.h>
#include "avs.h"
#include "path.h"
#include "pcm.h"
#include "topology.h"
#include "../../codecs/hda.h"

struct avs_dma_data {
	struct avs_tplg_path_template *template;
	struct avs_path *path;
	struct avs_dev *adev;

	/* LINK-stream utilized in BE operations while HOST in FE ones. */
	union {
		struct hdac_ext_stream *link_stream;
		struct hdac_ext_stream *host_stream;
	};

	struct work_struct period_elapsed_work;
	struct snd_pcm_substream *substream;
};

static struct avs_tplg_path_template *
avs_dai_find_path_template(struct snd_soc_dai *dai, bool is_fe, int direction)
{
	struct snd_soc_dapm_widget *dw = snd_soc_dai_get_widget(dai, direction);
	struct snd_soc_dapm_path *dp;
	enum snd_soc_dapm_direction dir;

	if (direction == SNDRV_PCM_STREAM_CAPTURE) {
		dir = is_fe ? SND_SOC_DAPM_DIR_OUT : SND_SOC_DAPM_DIR_IN;
	} else {
		dir = is_fe ? SND_SOC_DAPM_DIR_IN : SND_SOC_DAPM_DIR_OUT;
	}

	dp = list_first_entry_or_null(&dw->edges[dir], typeof(*dp), list_node[dir]);
	if (!dp)
		return NULL;

	/* Get the other widget, with actual path template data */
	dw = (dp->source == dw) ? dp->sink : dp->source;

	return dw->priv;
}

static void avs_period_elapsed_work(struct work_struct *work)
{
	struct avs_dma_data *data = container_of(work, struct avs_dma_data, period_elapsed_work);

	snd_pcm_period_elapsed(data->substream);
}

void avs_period_elapsed(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct avs_dma_data *data = snd_soc_dai_get_dma_data(dai, substream);

	schedule_work(&data->period_elapsed_work);
}

static int avs_dai_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dev *adev = to_avs_dev(dai->component->dev);
	struct avs_tplg_path_template *template;
	struct avs_dma_data *data;

	template = avs_dai_find_path_template(dai, !rtd->dai_link->no_pcm, substream->stream);
	if (!template) {
		dev_err(dai->dev, "no %s path for dai %s, invalid tplg?\n",
			snd_pcm_stream_str(substream), dai->name);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->substream = substream;
	data->template = template;
	data->adev = adev;
	INIT_WORK(&data->period_elapsed_work, avs_period_elapsed_work);
	snd_soc_dai_set_dma_data(dai, substream, data);

	if (rtd->dai_link->ignore_suspend)
		adev->num_lp_paths++;

	return 0;
}

static void avs_dai_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);

	if (rtd->dai_link->ignore_suspend)
		data->adev->num_lp_paths--;

	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(data);
}

static int avs_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *fe_hw_params,
			     struct snd_pcm_hw_params *be_hw_params, struct snd_soc_dai *dai,
			     int dma_id)
{
	struct avs_dma_data *data;
	struct avs_path *path;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);

	dev_dbg(dai->dev, "%s FE hw_params str %p rtd %p",
		__func__, substream, substream->runtime);
	dev_dbg(dai->dev, "rate %d chn %d vbd %d bd %d\n",
		params_rate(fe_hw_params), params_channels(fe_hw_params),
		params_width(fe_hw_params), params_physical_width(fe_hw_params));

	dev_dbg(dai->dev, "%s BE hw_params str %p rtd %p",
		__func__, substream, substream->runtime);
	dev_dbg(dai->dev, "rate %d chn %d vbd %d bd %d\n",
		params_rate(be_hw_params), params_channels(be_hw_params),
		params_width(be_hw_params), params_physical_width(be_hw_params));

	path = avs_path_create(data->adev, dma_id, data->template, fe_hw_params, be_hw_params);
	if (IS_ERR(path)) {
		ret = PTR_ERR(path);
		dev_err(dai->dev, "create path failed: %d\n", ret);
		return ret;
	}

	data->path = path;
	return 0;
}

static int avs_dai_be_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *be_hw_params, struct snd_soc_dai *dai,
				int dma_id)
{
	struct snd_pcm_hw_params *fe_hw_params = NULL;
	struct snd_soc_pcm_runtime *fe, *be;
	struct snd_soc_dpcm *dpcm;

	be = snd_soc_substream_to_rtd(substream);
	for_each_dpcm_fe(be, substream->stream, dpcm) {
		fe = dpcm->fe;
		fe_hw_params = &fe->dpcm[substream->stream].hw_params;
	}

	return avs_dai_hw_params(substream, fe_hw_params, be_hw_params, dai, dma_id);
}

static int avs_dai_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (!data->path)
		return 0;

	ret = avs_path_reset(data->path);
	if (ret < 0) {
		dev_err(dai->dev, "reset path failed: %d\n", ret);
		return ret;
	}

	ret = avs_path_pause(data->path);
	if (ret < 0)
		dev_err(dai->dev, "pause path failed: %d\n", ret);
	return ret;
}

static int avs_dai_nonhda_be_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (data->path)
		return 0;

	/* Actual port-id comes from topology. */
	return avs_dai_be_hw_params(substream, hw_params, dai, 0);
}

static int avs_dai_nonhda_be_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (data->path) {
		avs_path_free(data->path);
		data->path = NULL;
	}

	return 0;
}

static int avs_dai_nonhda_be_trigger(struct snd_pcm_substream *substream, int cmd,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dma_data *data;
	int ret = 0;

	data = snd_soc_dai_get_dma_data(dai, substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = avs_path_pause(data->path);
		if (ret < 0) {
			dev_err(dai->dev, "pause BE path failed: %d\n", ret);
			break;
		}

		ret = avs_path_run(data->path, AVS_TPLG_TRIGGER_AUTO);
		if (ret < 0)
			dev_err(dai->dev, "run BE path failed: %d\n", ret);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = avs_path_pause(data->path);
		if (ret < 0)
			dev_err(dai->dev, "pause BE path failed: %d\n", ret);

		ret = avs_path_reset(data->path);
		if (ret < 0)
			dev_err(dai->dev, "reset BE path failed: %d\n", ret);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_soc_dai_ops avs_dai_nonhda_be_ops = {
	.startup = avs_dai_startup,
	.shutdown = avs_dai_shutdown,
	.hw_params = avs_dai_nonhda_be_hw_params,
	.hw_free = avs_dai_nonhda_be_hw_free,
	.prepare = avs_dai_prepare,
	.trigger = avs_dai_nonhda_be_trigger,
};

static int avs_dai_hda_be_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct hdac_ext_stream *link_stream;
	struct avs_dma_data *data;
	struct hda_codec *codec;
	int ret;

	ret = avs_dai_startup(substream, dai);
	if (ret)
		return ret;

	codec = dev_to_hda_codec(snd_soc_rtd_to_codec(rtd, 0)->dev);
	link_stream = snd_hdac_ext_stream_assign(&codec->bus->core, substream,
						 HDAC_EXT_STREAM_TYPE_LINK);
	if (!link_stream) {
		avs_dai_shutdown(substream, dai);
		return -EBUSY;
	}

	data = snd_soc_dai_get_dma_data(dai, substream);
	data->link_stream = link_stream;
	substream->runtime->private_data = link_stream;
	return 0;
}

static void avs_dai_hda_be_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data = snd_soc_dai_get_dma_data(dai, substream);

	snd_hdac_ext_stream_release(data->link_stream, HDAC_EXT_STREAM_TYPE_LINK);
	substream->runtime->private_data = NULL;
	avs_dai_shutdown(substream, dai);
}

static int avs_dai_hda_be_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (data->path)
		return 0;

	return avs_dai_be_hw_params(substream, hw_params, dai,
				    hdac_stream(data->link_stream)->stream_tag - 1);
}

static int avs_dai_hda_be_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct hdac_ext_stream *link_stream;
	struct hdac_ext_link *link;
	struct hda_codec *codec;

	dev_dbg(dai->dev, "%s: %s\n", __func__, dai->name);

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (!data->path)
		return 0;

	link_stream = data->link_stream;
	link_stream->link_prepared = false;
	avs_path_free(data->path);
	data->path = NULL;

	/* clear link <-> stream mapping */
	codec = dev_to_hda_codec(snd_soc_rtd_to_codec(rtd, 0)->dev);
	link = snd_hdac_ext_bus_get_hlink_by_addr(&codec->bus->core, codec->core.addr);
	if (!link)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_hdac_ext_bus_link_clear_stream_id(link, hdac_stream(link_stream)->stream_tag);

	return 0;
}

static int avs_dai_hda_be_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	const struct snd_soc_pcm_stream *stream_info;
	struct hdac_ext_stream *link_stream;
	struct hdac_ext_link *link;
	struct avs_dma_data *data;
	struct hda_codec *codec;
	struct hdac_bus *bus;
	unsigned int format_val;
	unsigned int bits;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	link_stream = data->link_stream;

	if (link_stream->link_prepared)
		return 0;

	codec = dev_to_hda_codec(snd_soc_rtd_to_codec(rtd, 0)->dev);
	bus = &codec->bus->core;
	stream_info = snd_soc_dai_get_pcm_stream(dai, substream->stream);
	bits = snd_hdac_stream_format_bits(runtime->format, runtime->subformat,
					   stream_info->sig_bits);
	format_val = snd_hdac_stream_format(runtime->channels, bits, runtime->rate);

	snd_hdac_ext_stream_decouple(bus, link_stream, true);
	snd_hdac_ext_stream_reset(link_stream);
	snd_hdac_ext_stream_setup(link_stream, format_val);

	link = snd_hdac_ext_bus_get_hlink_by_addr(bus, codec->core.addr);
	if (!link)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_hdac_ext_bus_link_set_stream_id(link, hdac_stream(link_stream)->stream_tag);

	ret = avs_dai_prepare(substream, dai);
	if (ret)
		return ret;

	link_stream->link_prepared = true;
	return 0;
}

static int avs_dai_hda_be_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dma_data *data;
	int ret = 0;

	dev_dbg(dai->dev, "entry %s cmd=%d\n", __func__, cmd);

	data = snd_soc_dai_get_dma_data(dai, substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_hdac_ext_stream_start(data->link_stream);

		ret = avs_path_pause(data->path);
		if (ret < 0) {
			dev_err(dai->dev, "pause BE path failed: %d\n", ret);
			break;
		}

		ret = avs_path_run(data->path, AVS_TPLG_TRIGGER_AUTO);
		if (ret < 0)
			dev_err(dai->dev, "run BE path failed: %d\n", ret);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = avs_path_pause(data->path);
		if (ret < 0)
			dev_err(dai->dev, "pause BE path failed: %d\n", ret);

		snd_hdac_ext_stream_clear(data->link_stream);

		ret = avs_path_reset(data->path);
		if (ret < 0)
			dev_err(dai->dev, "reset BE path failed: %d\n", ret);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_soc_dai_ops avs_dai_hda_be_ops = {
	.startup = avs_dai_hda_be_startup,
	.shutdown = avs_dai_hda_be_shutdown,
	.hw_params = avs_dai_hda_be_hw_params,
	.hw_free = avs_dai_hda_be_hw_free,
	.prepare = avs_dai_hda_be_prepare,
	.trigger = avs_dai_hda_be_trigger,
};

static int hw_rule_param_size(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *interval = hw_param_interval(params, rule->var);
	struct snd_interval to;

	snd_interval_any(&to);
	to.integer = interval->integer;
	to.max = interval->max;
	/*
	 * Commonly 2ms buffer size is used in HDA scenarios whereas 4ms is used
	 * when streaming through GPDMA. Align to the latter to account for both.
	 */
	to.min = params_rate(params) / 1000 * 4;

	if (rule->var == SNDRV_PCM_HW_PARAM_PERIOD_SIZE)
		to.min /= params_periods(params);

	return snd_interval_refine(interval, &to);
}

static int avs_pcm_hw_constraints_init(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	/* Avoid wrap-around with wall-clock. */
	ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_TIME, 20, 178000000);
	if (ret < 0)
		return ret;

	/* Adjust buffer and period size based on the audio format. */
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, hw_rule_param_size, NULL,
			    SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_HW_PARAM_CHANNELS,
			    SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, hw_rule_param_size, NULL,
			    SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_HW_PARAM_CHANNELS,
			    SNDRV_PCM_HW_PARAM_RATE, -1);

	return 0;
}

static int avs_dai_fe_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *host_stream;
	struct avs_dma_data *data;
	struct hdac_bus *bus;
	int ret;

	ret = avs_pcm_hw_constraints_init(substream);
	if (ret)
		return ret;

	ret = avs_dai_startup(substream, dai);
	if (ret)
		return ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	bus = &data->adev->base.core;

	host_stream = snd_hdac_ext_stream_assign(bus, substream, HDAC_EXT_STREAM_TYPE_HOST);
	if (!host_stream) {
		avs_dai_shutdown(substream, dai);
		return -EBUSY;
	}

	data->host_stream = host_stream;
	snd_pcm_set_sync(substream);

	dev_dbg(dai->dev, "%s fe STARTUP tag %d str %p",
		__func__, hdac_stream(host_stream)->stream_tag, substream);

	return 0;
}

static void avs_dai_fe_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);

	snd_hdac_ext_stream_release(data->host_stream, HDAC_EXT_STREAM_TYPE_HOST);
	avs_dai_shutdown(substream, dai);
}

static int avs_dai_fe_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	struct snd_pcm_hw_params *be_hw_params = NULL;
	struct snd_soc_pcm_runtime *fe, *be;
	struct snd_soc_dpcm *dpcm;
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (data->path)
		return 0;

	host_stream = data->host_stream;

	hdac_stream(host_stream)->bufsize = 0;
	hdac_stream(host_stream)->period_bytes = 0;
	hdac_stream(host_stream)->format_val = 0;

	fe = snd_soc_substream_to_rtd(substream);
	for_each_dpcm_be(fe, substream->stream, dpcm) {
		be = dpcm->be;
		be_hw_params = &be->dpcm[substream->stream].hw_params;
	}

	ret = avs_dai_hw_params(substream, hw_params, be_hw_params, dai,
				hdac_stream(host_stream)->stream_tag - 1);
	if (ret)
		goto create_err;

	ret = avs_path_bind(data->path);
	if (ret < 0) {
		dev_err(dai->dev, "bind FE <-> BE failed: %d\n", ret);
		goto bind_err;
	}

	return 0;

bind_err:
	avs_path_free(data->path);
	data->path = NULL;
create_err:
	snd_pcm_lib_free_pages(substream);
	return ret;
}

static int __avs_dai_fe_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	int ret;

	dev_dbg(dai->dev, "%s fe HW_FREE str %p rtd %p",
		__func__, substream, substream->runtime);

	data = snd_soc_dai_get_dma_data(dai, substream);
	if (!data->path)
		return 0;

	host_stream = data->host_stream;

	ret = avs_path_unbind(data->path);
	if (ret < 0)
		dev_err(dai->dev, "unbind FE <-> BE failed: %d\n", ret);

	avs_path_free(data->path);
	data->path = NULL;
	snd_hdac_stream_cleanup(hdac_stream(host_stream));
	hdac_stream(host_stream)->prepared = false;

	return ret;
}

static int avs_dai_fe_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	int ret;

	ret = __avs_dai_fe_hw_free(substream, dai);
	snd_pcm_lib_free_pages(substream);

	return ret;
}

static int avs_dai_fe_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	const struct snd_soc_pcm_stream *stream_info;
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	unsigned int format_val;
	struct hdac_bus *bus;
	unsigned int bits;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	host_stream = data->host_stream;

	if (hdac_stream(host_stream)->prepared)
		return 0;

	bus = hdac_stream(host_stream)->bus;
	snd_hdac_ext_stream_decouple(bus, data->host_stream, true);
	snd_hdac_stream_reset(hdac_stream(host_stream));

	stream_info = snd_soc_dai_get_pcm_stream(dai, substream->stream);
	bits = snd_hdac_stream_format_bits(runtime->format, runtime->subformat,
					   stream_info->sig_bits);
	format_val = snd_hdac_stream_format(runtime->channels, bits, runtime->rate);

	ret = snd_hdac_stream_set_params(hdac_stream(host_stream), format_val);
	if (ret < 0)
		return ret;

	ret = snd_hdac_ext_host_stream_setup(host_stream, false);
	if (ret < 0)
		return ret;

	ret = avs_dai_prepare(substream, dai);
	if (ret)
		return ret;

	hdac_stream(host_stream)->prepared = true;
	return 0;
}

static void avs_hda_stream_start(struct hdac_bus *bus, struct hdac_ext_stream *host_stream)
{
	struct hdac_stream *first_running = NULL;
	struct hdac_stream *pos;
	struct avs_dev *adev = hdac_to_avs(bus);

	list_for_each_entry(pos, &bus->stream_list, list) {
		if (pos->running) {
			if (first_running)
				break; /* more than one running */
			first_running = pos;
		}
	}

	/*
	 * If host_stream is a CAPTURE stream and will be the only one running,
	 * disable L1SEN to avoid sound clipping.
	 */
	if (!first_running) {
		if (hdac_stream(host_stream)->direction == SNDRV_PCM_STREAM_CAPTURE)
			avs_hda_l1sen_enable(adev, false);
		snd_hdac_stream_start(hdac_stream(host_stream));
		return;
	}

	snd_hdac_stream_start(hdac_stream(host_stream));
	/*
	 * If host_stream is the first stream to break the rule above,
	 * re-enable L1SEN.
	 */
	if (list_entry_is_head(pos, &bus->stream_list, list) &&
	    first_running->direction == SNDRV_PCM_STREAM_CAPTURE)
		avs_hda_l1sen_enable(adev, true);
}

static void avs_hda_stream_stop(struct hdac_bus *bus, struct hdac_ext_stream *host_stream)
{
	struct hdac_stream *first_running = NULL;
	struct hdac_stream *pos;
	struct avs_dev *adev = hdac_to_avs(bus);

	list_for_each_entry(pos, &bus->stream_list, list) {
		if (pos == hdac_stream(host_stream))
			continue; /* ignore stream that is about to be stopped */
		if (pos->running) {
			if (first_running)
				break; /* more than one running */
			first_running = pos;
		}
	}

	/*
	 * If host_stream is a CAPTURE stream and is the only one running,
	 * re-enable L1SEN.
	 */
	if (!first_running) {
		snd_hdac_stream_stop(hdac_stream(host_stream));
		if (hdac_stream(host_stream)->direction == SNDRV_PCM_STREAM_CAPTURE)
			avs_hda_l1sen_enable(adev, true);
		return;
	}

	/*
	 * If by stopping host_stream there is only a single, CAPTURE stream running
	 * left, disable L1SEN to avoid sound clipping.
	 */
	if (list_entry_is_head(pos, &bus->stream_list, list) &&
	    first_running->direction == SNDRV_PCM_STREAM_CAPTURE)
		avs_hda_l1sen_enable(adev, false);

	snd_hdac_stream_stop(hdac_stream(host_stream));
}

static int avs_dai_fe_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	struct hdac_bus *bus;
	unsigned long flags;
	int ret = 0;

	data = snd_soc_dai_get_dma_data(dai, substream);
	host_stream = data->host_stream;
	bus = hdac_stream(host_stream)->bus;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&bus->reg_lock, flags);
		avs_hda_stream_start(bus, host_stream);
		spin_unlock_irqrestore(&bus->reg_lock, flags);

		/* Timeout on DRSM poll shall not stop the resume so ignore the result. */
		if (cmd == SNDRV_PCM_TRIGGER_RESUME)
			snd_hdac_stream_wait_drsm(hdac_stream(host_stream));

		ret = avs_path_pause(data->path);
		if (ret < 0) {
			dev_err(dai->dev, "pause FE path failed: %d\n", ret);
			break;
		}

		ret = avs_path_run(data->path, AVS_TPLG_TRIGGER_AUTO);
		if (ret < 0)
			dev_err(dai->dev, "run FE path failed: %d\n", ret);

		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (rtd->dai_link->ignore_suspend)
			break;
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = avs_path_pause(data->path);
		if (ret < 0)
			dev_err(dai->dev, "pause FE path failed: %d\n", ret);

		spin_lock_irqsave(&bus->reg_lock, flags);
		avs_hda_stream_stop(bus, host_stream);
		spin_unlock_irqrestore(&bus->reg_lock, flags);

		ret = avs_path_reset(data->path);
		if (ret < 0)
			dev_err(dai->dev, "reset FE path failed: %d\n", ret);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct snd_soc_dai_ops avs_dai_fe_ops = {
	.startup = avs_dai_fe_startup,
	.shutdown = avs_dai_fe_shutdown,
	.hw_params = avs_dai_fe_hw_params,
	.hw_free = avs_dai_fe_hw_free,
	.prepare = avs_dai_fe_prepare,
	.trigger = avs_dai_fe_trigger,
};

static ssize_t topology_name_read(struct file *file, char __user *user_buf, size_t count,
				  loff_t *ppos)
{
	struct snd_soc_component *component = file->private_data;
	struct snd_soc_card *card = component->card;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	char buf[64];
	size_t len;

	len = scnprintf(buf, sizeof(buf), "%s/%s\n", component->driver->topology_name_prefix,
			mach->tplg_filename);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations topology_name_fops = {
	.open = simple_open,
	.read = topology_name_read,
	.llseek = default_llseek,
};

static int avs_component_load_libraries(struct avs_soc_component *acomp)
{
	struct avs_tplg *tplg = acomp->tplg;
	struct avs_dev *adev = to_avs_dev(acomp->base.dev);
	int ret;

	if (!tplg->num_libs)
		return 0;

	/* Parent device may be asleep and library loading involves IPCs. */
	ret = pm_runtime_resume_and_get(adev->dev);
	if (ret < 0)
		return ret;

	avs_hda_power_gating_enable(adev, false);
	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	ret = avs_dsp_load_libraries(adev, tplg->libs, tplg->num_libs);

	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);
	avs_hda_power_gating_enable(adev, true);

	if (!ret)
		ret = avs_module_info_init(adev, false);

	pm_runtime_mark_last_busy(adev->dev);
	pm_runtime_put_autosuspend(adev->dev);

	return ret;
}

static int avs_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;
	struct snd_soc_acpi_mach *mach;
	struct avs_soc_component *acomp;
	struct avs_dev *adev;
	char *filename;
	int ret;

	dev_dbg(card->dev, "probing %s card %s\n", component->name, card->name);
	mach = dev_get_platdata(card->dev);
	acomp = to_avs_soc_component(component);
	adev = to_avs_dev(component->dev);

	acomp->tplg = avs_tplg_new(component);
	if (!acomp->tplg)
		return -ENOMEM;

	if (!mach->tplg_filename)
		goto finalize;

	/* Load specified topology and create debugfs for it. */
	filename = kasprintf(GFP_KERNEL, "%s/%s", component->driver->topology_name_prefix,
			     mach->tplg_filename);
	if (!filename)
		return -ENOMEM;

	ret = avs_load_topology(component, filename);
	kfree(filename);
	if (ret == -ENOENT && !strncmp(mach->tplg_filename, "hda-", 4)) {
		unsigned int vendor_id;

		if (sscanf(mach->tplg_filename, "hda-%08x-tplg.bin", &vendor_id) != 1)
			return ret;

		if (((vendor_id >> 16) & 0xFFFF) == 0x8086)
			mach->tplg_filename = devm_kasprintf(adev->dev, GFP_KERNEL,
							     "hda-8086-generic-tplg.bin");
		else
			mach->tplg_filename = devm_kasprintf(adev->dev, GFP_KERNEL,
							     "hda-generic-tplg.bin");

		filename = kasprintf(GFP_KERNEL, "%s/%s", component->driver->topology_name_prefix,
				     mach->tplg_filename);
		if (!filename)
			return -ENOMEM;

		dev_info(card->dev, "trying to load fallback topology %s\n", mach->tplg_filename);
		ret = avs_load_topology(component, filename);
		kfree(filename);
	}
	if (ret < 0)
		return ret;

	ret = avs_component_load_libraries(acomp);
	if (ret < 0) {
		dev_err(card->dev, "libraries loading failed: %d\n", ret);
		goto err_load_libs;
	}

finalize:
	debugfs_create_file("topology_name", 0444, component->debugfs_root, component,
			    &topology_name_fops);

	mutex_lock(&adev->comp_list_mutex);
	list_add_tail(&acomp->node, &adev->comp_list);
	mutex_unlock(&adev->comp_list_mutex);

	return 0;

err_load_libs:
	avs_remove_topology(component);
	return ret;
}

static void avs_component_remove(struct snd_soc_component *component)
{
	struct avs_soc_component *acomp = to_avs_soc_component(component);
	struct snd_soc_acpi_mach *mach;
	struct avs_dev *adev = to_avs_dev(component->dev);
	int ret;

	mach = dev_get_platdata(component->card->dev);

	mutex_lock(&adev->comp_list_mutex);
	list_del(&acomp->node);
	mutex_unlock(&adev->comp_list_mutex);

	if (mach->tplg_filename) {
		ret = avs_remove_topology(component);
		if (ret < 0)
			dev_err(component->dev, "unload topology failed: %d\n", ret);
	}
}

static int avs_dai_resume_hw_params(struct snd_soc_dai *dai, struct avs_dma_data *data)
{
	struct snd_pcm_substream *substream;
	struct snd_soc_pcm_runtime *rtd;
	int ret;

	substream = data->substream;
	rtd = snd_soc_substream_to_rtd(substream);

	ret = dai->driver->ops->hw_params(substream, &rtd->dpcm[substream->stream].hw_params, dai);
	if (ret)
		dev_err(dai->dev, "hw_params on resume failed: %d\n", ret);

	return ret;
}

static int avs_dai_resume_fe_prepare(struct snd_soc_dai *dai, struct avs_dma_data *data)
{
	struct hdac_ext_stream *host_stream;
	struct hdac_stream *hstream;
	struct hdac_bus *bus;
	int ret;

	host_stream = data->host_stream;
	hstream = hdac_stream(host_stream);
	bus = hdac_stream(host_stream)->bus;

	/* Set DRSM before programming stream and position registers. */
	snd_hdac_stream_drsm_enable(bus, true, hstream->index);

	ret = dai->driver->ops->prepare(data->substream, dai);
	if (ret) {
		dev_err(dai->dev, "prepare FE on resume failed: %d\n", ret);
		return ret;
	}

	writel(host_stream->pphcllpl, host_stream->pphc_addr + AZX_REG_PPHCLLPL);
	writel(host_stream->pphcllpu, host_stream->pphc_addr + AZX_REG_PPHCLLPU);
	writel(host_stream->pphcldpl, host_stream->pphc_addr + AZX_REG_PPHCLDPL);
	writel(host_stream->pphcldpu, host_stream->pphc_addr + AZX_REG_PPHCLDPU);

	/* As per HW spec recommendation, program LPIB and DPIB to the same value. */
	snd_hdac_stream_set_lpib(hstream, hstream->lpib);
	snd_hdac_stream_set_dpibr(bus, hstream, hstream->lpib);

	return 0;
}

static int avs_dai_resume_be_prepare(struct snd_soc_dai *dai, struct avs_dma_data *data)
{
	int ret;

	ret = dai->driver->ops->prepare(data->substream, dai);
	if (ret)
		dev_err(dai->dev, "prepare BE on resume failed: %d\n", ret);

	return ret;
}

static int avs_dai_suspend_fe_hw_free(struct snd_soc_dai *dai, struct avs_dma_data *data)
{
	struct hdac_ext_stream *host_stream;
	int ret;

	host_stream = data->host_stream;

	/* Store position addresses so we can resume from them later on. */
	hdac_stream(host_stream)->lpib = snd_hdac_stream_get_pos_lpib(hdac_stream(host_stream));
	host_stream->pphcllpl = readl(host_stream->pphc_addr + AZX_REG_PPHCLLPL);
	host_stream->pphcllpu = readl(host_stream->pphc_addr + AZX_REG_PPHCLLPU);
	host_stream->pphcldpl = readl(host_stream->pphc_addr + AZX_REG_PPHCLDPL);
	host_stream->pphcldpu = readl(host_stream->pphc_addr + AZX_REG_PPHCLDPU);

	ret = __avs_dai_fe_hw_free(data->substream, dai);
	if (ret < 0)
		dev_err(dai->dev, "hw_free FE on suspend failed: %d\n", ret);

	return ret;
}

static int avs_dai_suspend_be_hw_free(struct snd_soc_dai *dai, struct avs_dma_data *data)
{
	int ret;

	ret = dai->driver->ops->hw_free(data->substream, dai);
	if (ret < 0)
		dev_err(dai->dev, "hw_free BE on suspend failed: %d\n", ret);

	return ret;
}

static int avs_component_pm_op(struct snd_soc_component *component, bool be,
			       int (*op)(struct snd_soc_dai *, struct avs_dma_data *))
{
	struct snd_soc_pcm_runtime *rtd;
	struct avs_dma_data *data;
	struct snd_soc_dai *dai;
	int ret;

	for_each_component_dais(component, dai) {
		data = snd_soc_dai_dma_data_get_playback(dai);
		if (data) {
			rtd = snd_soc_substream_to_rtd(data->substream);
			if (rtd->dai_link->no_pcm == be && !rtd->dai_link->ignore_suspend) {
				ret = op(dai, data);
				if (ret < 0) {
					__snd_pcm_set_state(data->substream->runtime,
							    SNDRV_PCM_STATE_DISCONNECTED);
					return ret;
				}
			}
		}

		data = snd_soc_dai_dma_data_get_capture(dai);
		if (data) {
			rtd = snd_soc_substream_to_rtd(data->substream);
			if (rtd->dai_link->no_pcm == be && !rtd->dai_link->ignore_suspend) {
				ret = op(dai, data);
				if (ret < 0) {
					__snd_pcm_set_state(data->substream->runtime,
							    SNDRV_PCM_STATE_DISCONNECTED);
					return ret;
				}
			}
		}
	}

	return 0;
}

static int avs_component_resume_hw_params(struct snd_soc_component *component, bool be)
{
	return avs_component_pm_op(component, be, &avs_dai_resume_hw_params);
}

static int avs_component_resume_prepare(struct snd_soc_component *component, bool be)
{
	int (*prepare_cb)(struct snd_soc_dai *dai, struct avs_dma_data *data);

	if (be)
		prepare_cb = &avs_dai_resume_be_prepare;
	else
		prepare_cb = &avs_dai_resume_fe_prepare;

	return avs_component_pm_op(component, be, prepare_cb);
}

static int avs_component_suspend_hw_free(struct snd_soc_component *component, bool be)
{
	int (*hw_free_cb)(struct snd_soc_dai *dai, struct avs_dma_data *data);

	if (be)
		hw_free_cb = &avs_dai_suspend_be_hw_free;
	else
		hw_free_cb = &avs_dai_suspend_fe_hw_free;

	return avs_component_pm_op(component, be, hw_free_cb);
}

static int avs_component_suspend(struct snd_soc_component *component)
{
	int ret;

	/*
	 * When freeing paths, FEs need to be first as they perform
	 * path unbinding.
	 */
	ret = avs_component_suspend_hw_free(component, false);
	if (ret)
		return ret;

	return avs_component_suspend_hw_free(component, true);
}

static int avs_component_resume(struct snd_soc_component *component)
{
	int ret;

	/*
	 * When creating paths, FEs need to be last as they perform
	 * path binding.
	 */
	ret = avs_component_resume_hw_params(component, true);
	if (ret)
		return ret;

	ret = avs_component_resume_hw_params(component, false);
	if (ret)
		return ret;

	/* It is expected that the LINK stream is prepared first. */
	ret = avs_component_resume_prepare(component, true);
	if (ret)
		return ret;

	return avs_component_resume_prepare(component, false);
}

static const struct snd_pcm_hardware avs_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.subformats		= SNDRV_PCM_SUBFMTBIT_MSBITS_20 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_24 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_MAX,
	.buffer_bytes_max	= AZX_MAX_BUF_SIZE,
	.period_bytes_min	= 128,
	.period_bytes_max	= AZX_MAX_BUF_SIZE / 2,
	.periods_min		= 2,
	.periods_max		= AZX_MAX_FRAG,
	.fifo_size		= 0,
};

static int avs_component_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	/* only FE DAI links are handled here */
	if (rtd->dai_link->no_pcm)
		return 0;

	return snd_soc_set_runtime_hwparams(substream, &avs_pcm_hardware);
}

static unsigned int avs_hda_stream_dpib_read(struct hdac_ext_stream *stream)
{
	return readl(hdac_stream(stream)->bus->remap_addr + AZX_REG_VS_SDXDPIB_XBASE +
		     (AZX_REG_VS_SDXDPIB_XINTERVAL * hdac_stream(stream)->index));
}

static snd_pcm_uframes_t
avs_component_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	unsigned int pos;

	data = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream);
	if (!data->host_stream)
		return 0;

	host_stream = data->host_stream;
	pos = avs_hda_stream_dpib_read(host_stream);

	if (pos >= hdac_stream(host_stream)->bufsize)
		pos = 0;

	return bytes_to_frames(substream->runtime, pos);
}

static int avs_component_mmap(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream,
			      struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

#define MAX_PREALLOC_SIZE	(32 * 1024 * 1024)

static int avs_component_construct(struct snd_soc_component *component,
				   struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_pcm *pcm = rtd->pcm;

	if (dai->driver->playback.channels_min)
		snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
					   SNDRV_DMA_TYPE_DEV_SG, component->dev, 0,
					   MAX_PREALLOC_SIZE);

	if (dai->driver->capture.channels_min)
		snd_pcm_set_managed_buffer(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
					   SNDRV_DMA_TYPE_DEV_SG, component->dev, 0,
					   MAX_PREALLOC_SIZE);

	return 0;
}

static const struct snd_soc_component_driver avs_component_driver = {
	.name			= "avs-pcm",
	.probe			= avs_component_probe,
	.remove			= avs_component_remove,
	.suspend		= avs_component_suspend,
	.resume			= avs_component_resume,
	.open			= avs_component_open,
	.pointer		= avs_component_pointer,
	.mmap			= avs_component_mmap,
	.pcm_construct		= avs_component_construct,
	.module_get_upon_open	= 1, /* increment refcount when a pcm is opened */
	.topology_name_prefix	= "intel/avs",
};

int avs_soc_component_register(struct device *dev, const char *name,
			       const struct snd_soc_component_driver *drv,
			       struct snd_soc_dai_driver *cpu_dais, int num_cpu_dais)
{
	struct avs_soc_component *acomp;
	int ret;

	acomp = devm_kzalloc(dev, sizeof(*acomp), GFP_KERNEL);
	if (!acomp)
		return -ENOMEM;

	ret = snd_soc_component_initialize(&acomp->base, drv, dev);
	if (ret < 0)
		return ret;

	/* force name change after ASoC is done with its init */
	acomp->base.name = name;
	INIT_LIST_HEAD(&acomp->node);

	return snd_soc_add_component(&acomp->base, cpu_dais, num_cpu_dais);
}

static struct snd_soc_dai_driver dmic_cpu_dais[] = {
{
	.name = "DMIC Pin",
	.ops = &avs_dai_nonhda_be_ops,
	.capture = {
		.stream_name	= "DMIC Rx",
		.channels_min	= 1,
		.channels_max	= 4,
		.rates		= SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "DMIC WoV Pin",
	.ops = &avs_dai_nonhda_be_ops,
	.capture = {
		.stream_name	= "DMIC WoV Rx",
		.channels_min	= 1,
		.channels_max	= 4,
		.rates		= SNDRV_PCM_RATE_16000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
},
};

int avs_dmic_platform_register(struct avs_dev *adev, const char *name)
{
	return avs_soc_component_register(adev->dev, name, &avs_component_driver, dmic_cpu_dais,
					  ARRAY_SIZE(dmic_cpu_dais));
}

static const struct snd_soc_dai_driver i2s_dai_template = {
	.ops = &avs_dai_nonhda_be_ops,
	.playback = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000 |
				  SNDRV_PCM_RATE_12000 |
				  SNDRV_PCM_RATE_24000 |
				  SNDRV_PCM_RATE_128000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.subformats	= SNDRV_PCM_SUBFMTBIT_MSBITS_20 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_24 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_MAX,
	},
	.capture = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000 |
				  SNDRV_PCM_RATE_12000 |
				  SNDRV_PCM_RATE_24000 |
				  SNDRV_PCM_RATE_128000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.subformats	= SNDRV_PCM_SUBFMTBIT_MSBITS_20 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_24 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_MAX,
	},
};

int avs_i2s_platform_register(struct avs_dev *adev, const char *name, unsigned long port_mask,
			      unsigned long *tdms)
{
	struct snd_soc_dai_driver *cpus, *dai;
	size_t ssp_count, cpu_count;
	int i, j;

	ssp_count = adev->hw_cfg.i2s_caps.ctrl_count;

	cpu_count = 0;
	for_each_set_bit(i, &port_mask, ssp_count)
		if (!tdms || test_bit(0, &tdms[i]))
			cpu_count++;
	if (tdms)
		for_each_set_bit(i, &port_mask, ssp_count)
			cpu_count += hweight_long(tdms[i]);

	cpus = devm_kzalloc(adev->dev, sizeof(*cpus) * cpu_count, GFP_KERNEL);
	if (!cpus)
		return -ENOMEM;

	dai = cpus;
	for_each_set_bit(i, &port_mask, ssp_count) {
		if (!tdms || test_bit(0, &tdms[i])) {
			memcpy(dai, &i2s_dai_template, sizeof(*dai));

			dai->name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "SSP%d Pin", i);
			dai->playback.stream_name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "ssp%d Tx", i);
			dai->capture.stream_name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "ssp%d Rx", i);

			if (!dai->name || !dai->playback.stream_name || !dai->capture.stream_name)
				return -ENOMEM;
			dai++;
		}
	}

	if (!tdms)
		goto plat_register;

	for_each_set_bit(i, &port_mask, ssp_count) {
		for_each_set_bit(j, &tdms[i], ssp_count) {
			memcpy(dai, &i2s_dai_template, sizeof(*dai));

			dai->name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "SSP%d:%d Pin", i, j);
			dai->playback.stream_name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "ssp%d:%d Tx", i, j);
			dai->capture.stream_name =
				devm_kasprintf(adev->dev, GFP_KERNEL, "ssp%d:%d Rx", i, j);

			if (!dai->name || !dai->playback.stream_name || !dai->capture.stream_name)
				return -ENOMEM;
			dai++;
		}
	}

plat_register:
	return avs_soc_component_register(adev->dev, name, &avs_component_driver, cpus, cpu_count);
}

/* HD-Audio CPU DAI template */
static const struct snd_soc_dai_driver hda_cpu_dai = {
	.ops = &avs_dai_hda_be_ops,
	.playback = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.subformats	= SNDRV_PCM_SUBFMTBIT_MSBITS_20 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_24 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_MAX,
	},
	.capture = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.subformats	= SNDRV_PCM_SUBFMTBIT_MSBITS_20 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_24 |
				  SNDRV_PCM_SUBFMTBIT_MSBITS_MAX,
	},
};

static void avs_component_hda_unregister_dais(struct snd_soc_component *component)
{
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_dai *dai, *save;
	struct hda_codec *codec;
	char name[32];

	mach = dev_get_platdata(component->card->dev);
	codec = mach->pdata;
	snprintf(name, sizeof(name), "%s-cpu", dev_name(&codec->core.dev));

	for_each_component_dais_safe(component, dai, save) {
		int stream;

		if (!strstr(dai->driver->name, name))
			continue;

		for_each_pcm_streams(stream)
			snd_soc_dapm_free_widget(snd_soc_dai_get_widget(dai, stream));

		snd_soc_unregister_dai(dai);
	}
}

static int avs_component_hda_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_dai_driver *dais;
	struct snd_soc_acpi_mach *mach;
	struct hda_codec *codec;
	struct hda_pcm *pcm;
	const char *cname;
	int pcm_count = 0, ret, i;

	mach = dev_get_platdata(component->card->dev);
	if (!mach)
		return -EINVAL;

	codec = mach->pdata;
	if (list_empty(&codec->pcm_list_head))
		return -EINVAL;
	list_for_each_entry(pcm, &codec->pcm_list_head, list)
		pcm_count++;

	dais = devm_kcalloc(component->dev, pcm_count, sizeof(*dais),
			    GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	cname = dev_name(&codec->core.dev);
	dapm = snd_soc_component_get_dapm(component);
	pcm = list_first_entry(&codec->pcm_list_head, struct hda_pcm, list);

	for (i = 0; i < pcm_count; i++, pcm = list_next_entry(pcm, list)) {
		struct snd_soc_dai *dai;

		memcpy(&dais[i], &hda_cpu_dai, sizeof(*dais));
		dais[i].id = i;
		dais[i].name = devm_kasprintf(component->dev, GFP_KERNEL,
					      "%s-cpu%d", cname, i);
		if (!dais[i].name) {
			ret = -ENOMEM;
			goto exit;
		}

		if (pcm->stream[0].substreams) {
			dais[i].playback.stream_name =
				devm_kasprintf(component->dev, GFP_KERNEL,
					       "%s-cpu%d Tx", cname, i);
			if (!dais[i].playback.stream_name) {
				ret = -ENOMEM;
				goto exit;
			}

			if (!hda_codec_is_display(codec)) {
				dais[i].playback.formats = pcm->stream[0].formats;
				dais[i].playback.subformats = pcm->stream[0].subformats;
				dais[i].playback.rates = pcm->stream[0].rates;
				dais[i].playback.channels_min = pcm->stream[0].channels_min;
				dais[i].playback.channels_max = pcm->stream[0].channels_max;
				dais[i].playback.sig_bits = pcm->stream[0].maxbps;
			}
		}

		if (pcm->stream[1].substreams) {
			dais[i].capture.stream_name =
				devm_kasprintf(component->dev, GFP_KERNEL,
					       "%s-cpu%d Rx", cname, i);
			if (!dais[i].capture.stream_name) {
				ret = -ENOMEM;
				goto exit;
			}

			if (!hda_codec_is_display(codec)) {
				dais[i].capture.formats = pcm->stream[1].formats;
				dais[i].capture.subformats = pcm->stream[1].subformats;
				dais[i].capture.rates = pcm->stream[1].rates;
				dais[i].capture.channels_min = pcm->stream[1].channels_min;
				dais[i].capture.channels_max = pcm->stream[1].channels_max;
				dais[i].capture.sig_bits = pcm->stream[1].maxbps;
			}
		}

		dai = snd_soc_register_dai(component, &dais[i], false);
		if (!dai) {
			dev_err(component->dev, "register dai for %s failed\n",
				pcm->name);
			ret = -EINVAL;
			goto exit;
		}

		ret = snd_soc_dapm_new_dai_widgets(dapm, dai);
		if (ret < 0) {
			dev_err(component->dev, "create widgets failed: %d\n",
				ret);
			goto exit;
		}
	}

	ret = avs_component_probe(component);
exit:
	if (ret)
		avs_component_hda_unregister_dais(component);

	return ret;
}

static void avs_component_hda_remove(struct snd_soc_component *component)
{
	avs_component_hda_unregister_dais(component);
	avs_component_remove(component);
}

static int avs_component_hda_open(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if (!rtd->dai_link->no_pcm) {
		struct snd_pcm_hardware hwparams = avs_pcm_hardware;
		struct snd_soc_pcm_runtime *be;
		struct snd_soc_dpcm *dpcm;
		int dir = substream->stream;

		/*
		 * Support the DPCM reparenting while still fulfilling expectations of HDAudio
		 * common code - a valid stream pointer at substream->runtime->private_data -
		 * by having all FEs point to the same private data.
		 */
		for_each_dpcm_be(rtd, dir, dpcm) {
			struct snd_pcm_substream *be_substream;

			be = dpcm->be;
			if (be->dpcm[dir].users == 1)
				break;

			be_substream = snd_soc_dpcm_get_substream(be, dir);
			substream->runtime->private_data = be_substream->runtime->private_data;
			break;
		}

		/* RESUME unsupported for de-coupled HD-Audio capture. */
		if (dir == SNDRV_PCM_STREAM_CAPTURE)
			hwparams.info &= ~SNDRV_PCM_INFO_RESUME;

		return snd_soc_set_runtime_hwparams(substream, &hwparams);
	}

	return 0;
}

static const struct snd_soc_component_driver avs_hda_component_driver = {
	.name			= "avs-hda-pcm",
	.probe			= avs_component_hda_probe,
	.remove			= avs_component_hda_remove,
	.suspend		= avs_component_suspend,
	.resume			= avs_component_resume,
	.open			= avs_component_hda_open,
	.pointer		= avs_component_pointer,
	.mmap			= avs_component_mmap,
	.pcm_construct		= avs_component_construct,
	/*
	 * hda platform component's probe() is dependent on
	 * codec->pcm_list_head, it needs to be initialized after codec
	 * component. remove_order is here for completeness sake
	 */
	.probe_order		= SND_SOC_COMP_ORDER_LATE,
	.remove_order		= SND_SOC_COMP_ORDER_EARLY,
	.module_get_upon_open	= 1,
	.topology_name_prefix	= "intel/avs",
};

int avs_hda_platform_register(struct avs_dev *adev, const char *name)
{
	return avs_soc_component_register(adev->dev, name,
					  &avs_hda_component_driver, NULL, 0);
}
