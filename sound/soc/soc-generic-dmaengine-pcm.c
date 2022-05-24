// SPDX-License-Identifier: GPL-2.0+
//
//  Copyright (C) 2013, Analog Devices Inc.
//	Author: Lars-Peter Clausen <lars@metafoo.de>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/dmaengine.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>

#include <sound/dmaengine_pcm.h>

/*
 * The platforms dmaengine driver does not support reporting the amount of
 * bytes that are still left to transfer.
 */
#define SND_DMAENGINE_PCM_FLAG_NO_RESIDUE BIT(31)

static struct device *dmaengine_dma_dev(struct dmaengine_pcm *pcm,
	struct snd_pcm_substream *substream)
{
	if (!pcm->chan[substream->stream])
		return NULL;

	return pcm->chan[substream->stream]->device->dev;
}

/**
 * snd_dmaengine_pcm_prepare_slave_config() - Generic prepare_slave_config callback
 * @substream: PCM substream
 * @params: hw_params
 * @slave_config: DMA slave config to prepare
 *
 * This function can be used as a generic prepare_slave_config callback for
 * platforms which make use of the snd_dmaengine_dai_dma_data struct for their
 * DAI DMA data. Internally the function will first call
 * snd_hwparams_to_dma_slave_config to fill in the slave config based on the
 * hw_params, followed by snd_dmaengine_set_config_from_dai_data to fill in the
 * remaining fields based on the DAI DMA data.
 */
int snd_dmaengine_pcm_prepare_slave_config(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_dmaengine_dai_dma_data *dma_data;
	int ret;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret)
		return ret;

	snd_dmaengine_pcm_set_config_from_dai_data(substream, dma_data,
		slave_config);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_dmaengine_pcm_prepare_slave_config);

static int dmaengine_pcm_hw_params(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	int (*prepare_slave_config)(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct dma_slave_config *slave_config);
	struct dma_slave_config slave_config;
	int ret;

	memset(&slave_config, 0, sizeof(slave_config));

	if (pcm->config && pcm->config->prepare_slave_config)
		prepare_slave_config = pcm->config->prepare_slave_config;
	else
		prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config;

	if (prepare_slave_config) {
		ret = prepare_slave_config(substream, params, &slave_config);
		if (ret)
			return ret;

		ret = dmaengine_slave_config(chan, &slave_config);
		if (ret)
			return ret;
	}

	return 0;
}

static int
dmaengine_pcm_set_runtime_hwparams(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	struct device *dma_dev = dmaengine_dma_dev(pcm, substream);
	struct dma_chan *chan = pcm->chan[substream->stream];
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct snd_pcm_hardware hw;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	if (pcm->config && pcm->config->pcm_hardware)
		return snd_soc_set_runtime_hwparams(substream,
				pcm->config->pcm_hardware);

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	memset(&hw, 0, sizeof(hw));
	hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED;
	hw.periods_min = 2;
	hw.periods_max = UINT_MAX;
	hw.period_bytes_min = 256;
	hw.period_bytes_max = dma_get_max_seg_size(dma_dev);
	hw.buffer_bytes_max = SIZE_MAX;
	hw.fifo_size = dma_data->fifo_size;

	if (pcm->flags & SND_DMAENGINE_PCM_FLAG_NO_RESIDUE)
		hw.info |= SNDRV_PCM_INFO_BATCH;

	/**
	 * FIXME: Remove the return value check to align with the code
	 * before adding snd_dmaengine_pcm_refine_runtime_hwparams
	 * function.
	 */
	snd_dmaengine_pcm_refine_runtime_hwparams(substream,
						  dma_data,
						  &hw,
						  chan);

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int dmaengine_pcm_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	struct dma_chan *chan = pcm->chan[substream->stream];
	int ret;

	ret = dmaengine_pcm_set_runtime_hwparams(component, substream);
	if (ret)
		return ret;

	return snd_dmaengine_pcm_open(substream, chan);
}

static int dmaengine_pcm_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_close(substream);
}

static int dmaengine_pcm_trigger(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream, int cmd)
{
	return snd_dmaengine_pcm_trigger(substream, cmd);
}

static struct dma_chan *dmaengine_pcm_compat_request_channel(
	struct snd_soc_component *component,
	struct snd_soc_pcm_runtime *rtd,
	struct snd_pcm_substream *substream)
{
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	struct snd_dmaengine_dai_dma_data *dma_data;
	dma_filter_fn fn = NULL;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return NULL;
	}

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	if ((pcm->flags & SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX) && pcm->chan[0])
		return pcm->chan[0];

	if (pcm->config && pcm->config->compat_request_channel)
		return pcm->config->compat_request_channel(rtd, substream);

	if (pcm->config)
		fn = pcm->config->compat_filter_fn;

	return snd_dmaengine_pcm_request_channel(fn, dma_data->filter_data);
}

static bool dmaengine_pcm_can_report_residue(struct device *dev,
	struct dma_chan *chan)
{
	struct dma_slave_caps dma_caps;
	int ret;

	ret = dma_get_slave_caps(chan, &dma_caps);
	if (ret != 0) {
		dev_warn(dev, "Failed to get DMA channel capabilities, falling back to period counting: %d\n",
			 ret);
		return false;
	}

	if (dma_caps.residue_granularity == DMA_RESIDUE_GRANULARITY_DESCRIPTOR)
		return false;

	return true;
}

static int dmaengine_pcm_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	const struct snd_dmaengine_pcm_config *config = pcm->config;
	struct device *dev = component->dev;
	struct snd_pcm_substream *substream;
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;

	if (config && config->prealloc_buffer_size) {
		prealloc_buffer_size = config->prealloc_buffer_size;
		max_buffer_size = config->pcm_hardware->buffer_bytes_max;
	} else {
		prealloc_buffer_size = 512 * 1024;
		max_buffer_size = SIZE_MAX;
	}

	for_each_pcm_streams(i) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		if (!pcm->chan[i] && config && config->chan_names[i])
			pcm->chan[i] = dma_request_slave_channel(dev,
				config->chan_names[i]);

		if (!pcm->chan[i] && (pcm->flags & SND_DMAENGINE_PCM_FLAG_COMPAT)) {
			pcm->chan[i] = dmaengine_pcm_compat_request_channel(
				component, rtd, substream);
		}

		if (!pcm->chan[i]) {
			dev_err(component->dev,
				"Missing dma channel for stream: %d\n", i);
			return -EINVAL;
		}

		snd_pcm_set_managed_buffer(substream,
				SNDRV_DMA_TYPE_DEV_IRAM,
				dmaengine_dma_dev(pcm, substream),
				prealloc_buffer_size,
				max_buffer_size);

		if (!dmaengine_pcm_can_report_residue(dev, pcm->chan[i]))
			pcm->flags |= SND_DMAENGINE_PCM_FLAG_NO_RESIDUE;

		if (rtd->pcm->streams[i].pcm->name[0] == '\0') {
			strscpy_pad(rtd->pcm->streams[i].pcm->name,
				    rtd->pcm->streams[i].pcm->id,
				    sizeof(rtd->pcm->streams[i].pcm->name));
		}
	}

	return 0;
}

static snd_pcm_uframes_t dmaengine_pcm_pointer(
	struct snd_soc_component *component,
	struct snd_pcm_substream *substream)
{
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);

	if (pcm->flags & SND_DMAENGINE_PCM_FLAG_NO_RESIDUE)
		return snd_dmaengine_pcm_pointer_no_residue(substream);
	else
		return snd_dmaengine_pcm_pointer(substream);
}

static int dmaengine_copy_user(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       int channel, unsigned long hwoff,
			       void __user *buf, unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dmaengine_pcm *pcm = soc_component_to_pcm(component);
	int (*process)(struct snd_pcm_substream *substream,
		       int channel, unsigned long hwoff,
		       void *buf, unsigned long bytes) = pcm->config->process;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	void *dma_ptr = runtime->dma_area + hwoff +
			channel * (runtime->dma_bytes / runtime->channels);
	int ret;

	if (is_playback)
		if (copy_from_user(dma_ptr, buf, bytes))
			return -EFAULT;

	if (process) {
		ret = process(substream, channel, hwoff, (__force void *)buf, bytes);
		if (ret < 0)
			return ret;
	}

	if (!is_playback)
		if (copy_to_user(buf, dma_ptr, bytes))
			return -EFAULT;

	return 0;
}

static const struct snd_soc_component_driver dmaengine_pcm_component = {
	.name		= SND_DMAENGINE_PCM_DRV_NAME,
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.open		= dmaengine_pcm_open,
	.close		= dmaengine_pcm_close,
	.hw_params	= dmaengine_pcm_hw_params,
	.trigger	= dmaengine_pcm_trigger,
	.pointer	= dmaengine_pcm_pointer,
	.pcm_construct	= dmaengine_pcm_new,
};

static const struct snd_soc_component_driver dmaengine_pcm_component_process = {
	.name		= SND_DMAENGINE_PCM_DRV_NAME,
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.open		= dmaengine_pcm_open,
	.close		= dmaengine_pcm_close,
	.hw_params	= dmaengine_pcm_hw_params,
	.trigger	= dmaengine_pcm_trigger,
	.pointer	= dmaengine_pcm_pointer,
	.copy_user	= dmaengine_copy_user,
	.pcm_construct	= dmaengine_pcm_new,
};

static const char * const dmaengine_pcm_dma_channel_names[] = {
	[SNDRV_PCM_STREAM_PLAYBACK] = "tx",
	[SNDRV_PCM_STREAM_CAPTURE] = "rx",
};

static int dmaengine_pcm_request_chan_of(struct dmaengine_pcm *pcm,
	struct device *dev, const struct snd_dmaengine_pcm_config *config)
{
	unsigned int i;
	const char *name;
	struct dma_chan *chan;

	if ((pcm->flags & SND_DMAENGINE_PCM_FLAG_NO_DT) || (!dev->of_node &&
	    !(config && config->dma_dev && config->dma_dev->of_node)))
		return 0;

	if (config && config->dma_dev) {
		/*
		 * If this warning is seen, it probably means that your Linux
		 * device structure does not match your HW device structure.
		 * It would be best to refactor the Linux device structure to
		 * correctly match the HW structure.
		 */
		dev_warn(dev, "DMA channels sourced from device %s",
			 dev_name(config->dma_dev));
		dev = config->dma_dev;
	}

	for_each_pcm_streams(i) {
		if (pcm->flags & SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX)
			name = "rx-tx";
		else
			name = dmaengine_pcm_dma_channel_names[i];
		if (config && config->chan_names[i])
			name = config->chan_names[i];
		chan = dma_request_chan(dev, name);
		if (IS_ERR(chan)) {
			/*
			 * Only report probe deferral errors, channels
			 * might not be present for devices that
			 * support only TX or only RX.
			 */
			if (PTR_ERR(chan) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			pcm->chan[i] = NULL;
		} else {
			pcm->chan[i] = chan;
		}
		if (pcm->flags & SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX)
			break;
	}

	if (pcm->flags & SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX)
		pcm->chan[1] = pcm->chan[0];

	return 0;
}

static void dmaengine_pcm_release_chan(struct dmaengine_pcm *pcm)
{
	unsigned int i;

	for_each_pcm_streams(i) {
		if (!pcm->chan[i])
			continue;
		dma_release_channel(pcm->chan[i]);
		if (pcm->flags & SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX)
			break;
	}
}

/**
 * snd_dmaengine_pcm_register - Register a dmaengine based PCM device
 * @dev: The parent device for the PCM device
 * @config: Platform specific PCM configuration
 * @flags: Platform specific quirks
 */
int snd_dmaengine_pcm_register(struct device *dev,
	const struct snd_dmaengine_pcm_config *config, unsigned int flags)
{
	const struct snd_soc_component_driver *driver;
	struct dmaengine_pcm *pcm;
	int ret;

	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

#ifdef CONFIG_DEBUG_FS
	pcm->component.debugfs_prefix = "dma";
#endif
	pcm->config = config;
	pcm->flags = flags;

	ret = dmaengine_pcm_request_chan_of(pcm, dev, config);
	if (ret)
		goto err_free_dma;

	if (config && config->process)
		driver = &dmaengine_pcm_component_process;
	else
		driver = &dmaengine_pcm_component;

	ret = snd_soc_component_initialize(&pcm->component, driver, dev);
	if (ret)
		goto err_free_dma;

	ret = snd_soc_add_component(&pcm->component, NULL, 0);
	if (ret)
		goto err_free_dma;

	return 0;

err_free_dma:
	dmaengine_pcm_release_chan(pcm);
	kfree(pcm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_dmaengine_pcm_register);

/**
 * snd_dmaengine_pcm_unregister - Removes a dmaengine based PCM device
 * @dev: Parent device the PCM was register with
 *
 * Removes a dmaengine based PCM device previously registered with
 * snd_dmaengine_pcm_register.
 */
void snd_dmaengine_pcm_unregister(struct device *dev)
{
	struct snd_soc_component *component;
	struct dmaengine_pcm *pcm;

	component = snd_soc_lookup_component(dev, SND_DMAENGINE_PCM_DRV_NAME);
	if (!component)
		return;

	pcm = soc_component_to_pcm(component);

	snd_soc_unregister_component_by_driver(dev, component->driver);
	dmaengine_pcm_release_chan(pcm);
	kfree(pcm);
}
EXPORT_SYMBOL_GPL(snd_dmaengine_pcm_unregister);

MODULE_LICENSE("GPL");
