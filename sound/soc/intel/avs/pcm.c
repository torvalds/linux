// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
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
#include "topology.h"

struct avs_dma_data {
	struct avs_tplg_path_template *template;
	struct avs_path *path;
	/*
	 * link stream is stored within substream's runtime
	 * private_data to fulfill the needs of codec BE path
	 *
	 * host stream assigned
	 */
	struct hdac_ext_stream *host_stream;
};

static struct avs_tplg_path_template *
avs_dai_find_path_template(struct snd_soc_dai *dai, bool is_fe, int direction)
{
	struct snd_soc_dapm_widget *dw;
	struct snd_soc_dapm_path *dp;
	enum snd_soc_dapm_direction dir;

	if (direction == SNDRV_PCM_STREAM_CAPTURE) {
		dw = dai->capture_widget;
		dir = is_fe ? SND_SOC_DAPM_DIR_OUT : SND_SOC_DAPM_DIR_IN;
	} else {
		dw = dai->playback_widget;
		dir = is_fe ? SND_SOC_DAPM_DIR_IN : SND_SOC_DAPM_DIR_OUT;
	}

	dp = list_first_entry_or_null(&dw->edges[dir], typeof(*dp), list_node[dir]);
	if (!dp)
		return NULL;

	/* Get the other widget, with actual path template data */
	dw = (dp->source == dw) ? dp->sink : dp->source;

	return dw->priv;
}

static int avs_dai_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai, bool is_fe)
{
	struct avs_tplg_path_template *template;
	struct avs_dma_data *data;

	template = avs_dai_find_path_template(dai, is_fe, substream->stream);
	if (!template) {
		dev_err(dai->dev, "no %s path for dai %s, invalid tplg?\n",
			snd_pcm_stream_str(substream), dai->name);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->template = template;
	snd_soc_dai_set_dma_data(dai, substream, data);

	return 0;
}

static int avs_dai_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *fe_hw_params,
			     struct snd_pcm_hw_params *be_hw_params, struct snd_soc_dai *dai,
			     int dma_id)
{
	struct avs_dma_data *data;
	struct avs_path *path;
	struct avs_dev *adev = to_avs_dev(dai->dev);
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

	path = avs_path_create(adev, dma_id, data->template, fe_hw_params, be_hw_params);
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

	be = asoc_substream_to_rtd(substream);
	for_each_dpcm_fe(be, substream->stream, dpcm) {
		fe = dpcm->fe;
		fe_hw_params = &fe->dpcm[substream->stream].hw_params;
	}

	return avs_dai_hw_params(substream, fe_hw_params, be_hw_params, dai, dma_id);
}

static int avs_dai_prepare(struct avs_dev *adev, struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
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

static int avs_dai_nonhda_be_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	return avs_dai_startup(substream, dai, false);
}

static void avs_dai_nonhda_be_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);

	snd_soc_dai_set_dma_data(dai, substream, NULL);
	kfree(data);
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

static int avs_dai_nonhda_be_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	return avs_dai_prepare(to_avs_dev(dai->dev), substream, dai);
}

static int avs_dai_nonhda_be_trigger(struct snd_pcm_substream *substream, int cmd,
				     struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;
	int ret = 0;

	data = snd_soc_dai_get_dma_data(dai, substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = avs_path_run(data->path, AVS_TPLG_TRIGGER_AUTO);
		if (ret < 0)
			dev_err(dai->dev, "run BE path failed: %d\n", ret);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = avs_path_pause(data->path);
		if (ret < 0)
			dev_err(dai->dev, "pause BE path failed: %d\n", ret);

		if (cmd == SNDRV_PCM_TRIGGER_STOP) {
			ret = avs_path_reset(data->path);
			if (ret < 0)
				dev_err(dai->dev, "reset BE path failed: %d\n", ret);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_soc_dai_ops avs_dai_nonhda_be_ops = {
	.startup = avs_dai_nonhda_be_startup,
	.shutdown = avs_dai_nonhda_be_shutdown,
	.hw_params = avs_dai_nonhda_be_hw_params,
	.hw_free = avs_dai_nonhda_be_hw_free,
	.prepare = avs_dai_nonhda_be_prepare,
	.trigger = avs_dai_nonhda_be_trigger,
};

static const unsigned int rates[] = {
	8000, 11025, 12000, 16000,
	22050, 24000, 32000, 44100,
	48000, 64000, 88200, 96000,
	128000, 176400, 192000,
};

static const struct snd_pcm_hw_constraint_list hw_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static int avs_dai_fe_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct avs_dma_data *data;
	struct avs_dev *adev = to_avs_dev(dai->dev);
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_stream *host_stream;
	int ret;

	ret = avs_dai_startup(substream, dai, true);
	if (ret)
		return ret;

	data = snd_soc_dai_get_dma_data(dai, substream);

	host_stream = snd_hdac_ext_stream_assign(bus, substream, HDAC_EXT_STREAM_TYPE_HOST);
	if (!host_stream) {
		kfree(data);
		return -EBUSY;
	}

	data->host_stream = host_stream;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	/* avoid wrap-around with wall-clock */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_TIME, 20, 178000000);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_rates);
	snd_pcm_set_sync(substream);

	dev_dbg(dai->dev, "%s fe STARTUP tag %d str %p",
		__func__, hdac_stream(host_stream)->stream_tag, substream);

	return 0;
}

static void avs_dai_fe_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;

	data = snd_soc_dai_get_dma_data(dai, substream);

	snd_soc_dai_set_dma_data(dai, substream, NULL);
	snd_hdac_ext_stream_release(data->host_stream, HDAC_EXT_STREAM_TYPE_HOST);
	kfree(data);
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

	fe = asoc_substream_to_rtd(substream);
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

static int avs_dai_fe_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
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

	ret = snd_pcm_lib_free_pages(substream);
	if (ret < 0)
		dev_dbg(dai->dev, "Failed to free pages!\n");

	return ret;
}

static int avs_dai_fe_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct avs_dma_data *data;
	struct avs_dev *adev = to_avs_dev(dai->dev);
	struct hdac_ext_stream *host_stream;
	struct hdac_bus *bus;
	unsigned int format_val;
	int ret;

	data = snd_soc_dai_get_dma_data(dai, substream);
	host_stream = data->host_stream;

	if (hdac_stream(host_stream)->prepared)
		return 0;

	bus = hdac_stream(host_stream)->bus;
	snd_hdac_ext_stream_decouple(bus, data->host_stream, true);
	snd_hdac_stream_reset(hdac_stream(host_stream));

	format_val = snd_hdac_calc_stream_format(runtime->rate, runtime->channels, runtime->format,
						 runtime->sample_bits, 0);

	ret = snd_hdac_stream_set_params(hdac_stream(host_stream), format_val);
	if (ret < 0)
		return ret;

	ret = snd_hdac_stream_setup(hdac_stream(host_stream));
	if (ret < 0)
		return ret;

	ret = avs_dai_prepare(adev, substream, dai);
	if (ret)
		return ret;

	hdac_stream(host_stream)->prepared = true;
	return 0;
}

static int avs_dai_fe_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	struct hdac_bus *bus;
	unsigned long flags;
	int ret = 0;

	data = snd_soc_dai_get_dma_data(dai, substream);
	host_stream = data->host_stream;
	bus = hdac_stream(host_stream)->bus;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&bus->reg_lock, flags);
		snd_hdac_stream_start(hdac_stream(host_stream), true);
		spin_unlock_irqrestore(&bus->reg_lock, flags);

		ret = avs_path_run(data->path, AVS_TPLG_TRIGGER_AUTO);
		if (ret < 0)
			dev_err(dai->dev, "run FE path failed: %d\n", ret);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = avs_path_pause(data->path);
		if (ret < 0)
			dev_err(dai->dev, "pause FE path failed: %d\n", ret);

		spin_lock_irqsave(&bus->reg_lock, flags);
		snd_hdac_stream_stop(hdac_stream(host_stream));
		spin_unlock_irqrestore(&bus->reg_lock, flags);

		if (cmd == SNDRV_PCM_TRIGGER_STOP) {
			ret = avs_path_reset(data->path);
			if (ret < 0)
				dev_err(dai->dev, "reset FE path failed: %d\n", ret);
		}
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

	len = snprintf(buf, sizeof(buf), "%s/%s\n", component->driver->topology_name_prefix,
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

	avs_hda_clock_gating_enable(adev, false);
	avs_hda_l1sen_enable(adev, false);

	ret = avs_dsp_load_libraries(adev, tplg->libs, tplg->num_libs);

	avs_hda_l1sen_enable(adev, true);
	avs_hda_clock_gating_enable(adev, true);

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

static int avs_component_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_pcm_hardware hwparams;

	/* only FE DAI links are handled here */
	if (rtd->dai_link->no_pcm)
		return 0;

	hwparams.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP;

	hwparams.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE;
	hwparams.period_bytes_min = 128;
	hwparams.period_bytes_max = AZX_MAX_BUF_SIZE / 2;
	hwparams.periods_min = 2;
	hwparams.periods_max = AZX_MAX_FRAG;
	hwparams.buffer_bytes_max = AZX_MAX_BUF_SIZE;
	hwparams.fifo_size = 0;

	return snd_soc_set_runtime_hwparams(substream, &hwparams);
}

static unsigned int avs_hda_stream_dpib_read(struct hdac_ext_stream *stream)
{
	return readl(hdac_stream(stream)->bus->remap_addr + AZX_REG_VS_SDXDPIB_XBASE +
		     (AZX_REG_VS_SDXDPIB_XINTERVAL * hdac_stream(stream)->index));
}

static snd_pcm_uframes_t
avs_component_pointer(struct snd_soc_component *component, struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct avs_dma_data *data;
	struct hdac_ext_stream *host_stream;
	unsigned int pos;

	data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);
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
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
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
	.open			= avs_component_open,
	.pointer		= avs_component_pointer,
	.mmap			= avs_component_mmap,
	.pcm_construct		= avs_component_construct,
	.module_get_upon_open	= 1, /* increment refcount when a pcm is opened */
	.topology_name_prefix	= "intel/avs",
	.non_legacy_dai_naming	= true,
};

static int avs_soc_component_register(struct device *dev, const char *name,
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
				  SNDRV_PCM_RATE_KNOT,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min	= 1,
		.channels_max	= 8,
		.rates		= SNDRV_PCM_RATE_8000_192000 |
				  SNDRV_PCM_RATE_KNOT,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	},
};

int avs_i2s_platform_register(struct avs_dev *adev, const char *name, unsigned long port_mask,
			      unsigned long *tdms)
{
	struct snd_soc_dai_driver *cpus, *dai;
	size_t ssp_count, cpu_count;
	int i, j;

	ssp_count = adev->hw_cfg.i2s_caps.ctrl_count;
	cpu_count = hweight_long(port_mask);
	if (tdms)
		for_each_set_bit(i, &port_mask, ssp_count)
			cpu_count += hweight_long(tdms[i]);

	cpus = devm_kzalloc(adev->dev, sizeof(*cpus) * cpu_count, GFP_KERNEL);
	if (!cpus)
		return -ENOMEM;

	dai = cpus;
	for_each_set_bit(i, &port_mask, ssp_count) {
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
