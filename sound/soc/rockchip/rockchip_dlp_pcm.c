// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip DLP (Digital Loopback) Driver
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

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

#include "rockchip_dlp.h"

#define SND_DMAENGINE_DLP_DRV_NAME	"snd_dmaengine_dlp"

static unsigned int prealloc_buffer_size_kbytes = 512;
module_param(prealloc_buffer_size_kbytes, uint, 0444);
MODULE_PARM_DESC(prealloc_buffer_size_kbytes, "Preallocate DMA buffer size (KB).");

struct dmaengine_dlp {
	struct dlp dlp;
	struct dma_chan *chan[SNDRV_PCM_STREAM_LAST + 1];
};

struct dmaengine_dlp_runtime_data {
	struct dlp_runtime_data drd;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
};

static inline struct dmaengine_dlp *soc_component_to_ddlp(struct snd_soc_component *p)
{
	return container_of(soc_component_to_dlp(p), struct dmaengine_dlp, dlp);
}

static inline struct dmaengine_dlp_runtime_data *substream_to_ddrd(
	const struct snd_pcm_substream *substream)
{
	struct dlp_runtime_data *drd = substream_to_drd(substream);

	if (!drd)
		return NULL;

	return container_of(drd, struct dmaengine_dlp_runtime_data, drd);
}

static struct dma_chan *snd_dmaengine_dlp_get_chan(struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *ddrd = substream_to_ddrd(substream);

	return ddrd ? ddrd->dma_chan : NULL;
}

static struct device *dmaengine_dma_dev(struct dmaengine_dlp *ddlp,
					struct snd_pcm_substream *substream)
{
	if (!ddlp->chan[substream->stream])
		return NULL;

	return ddlp->chan[substream->stream]->device->dev;
}

static int dmaengine_dlp_hw_params(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct dma_chan *chan = snd_dmaengine_dlp_get_chan(substream);
	struct dma_slave_config slave_config;
	int ret;

	memset(&slave_config, 0, sizeof(slave_config));

	ret = snd_dmaengine_pcm_prepare_slave_config(substream, params, &slave_config);
	if (ret)
		return ret;

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret)
		return ret;

	ret = dlp_hw_params(component, substream, params);

	return ret;
}

static int
dmaengine_pcm_set_runtime_hwparams(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct dmaengine_dlp *ddlp = soc_component_to_ddlp(component);
	struct device *dma_dev = dmaengine_dma_dev(ddlp, substream);
	struct dma_chan *chan = ddlp->chan[substream->stream];
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct snd_pcm_hardware hw;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

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

	snd_dmaengine_pcm_refine_runtime_hwparams(substream, dma_data,
						  &hw, chan);

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int dmaengine_dlp_open(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *ddlp = soc_component_to_ddlp(component);
	struct dma_chan *chan = ddlp->chan[substream->stream];
	struct dmaengine_dlp_runtime_data *ddrd;
	int ret;

	if (!chan)
		return -ENXIO;

	ret = dmaengine_pcm_set_runtime_hwparams(component, substream);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	ddrd = kzalloc(sizeof(*ddrd), GFP_KERNEL);
	if (!ddrd)
		return -ENOMEM;

	ddrd->dma_chan = chan;

	dlp_open(&ddlp->dlp, &ddrd->drd, substream);

	return 0;
}

static int dmaengine_dlp_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp *ddlp = soc_component_to_ddlp(component);
	struct dmaengine_dlp_runtime_data *ddrd = substream_to_ddrd(substream);

	if (unlikely(!ddlp || !ddrd))
		return -EINVAL;

	dmaengine_synchronize(ddrd->dma_chan);

	dlp_close(&ddlp->dlp, &ddrd->drd, substream);

	kfree(ddrd);

	return 0;
}

static snd_pcm_uframes_t dmaengine_dlp_pointer(struct snd_soc_component *component,
					       struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *ddrd = substream_to_ddrd(substream);
	struct dma_tx_state state;
	unsigned int buf_size;
	unsigned int pos = 0;

	if (unlikely(!ddrd))
		return 0;

	dmaengine_tx_status(ddrd->dma_chan, ddrd->cookie, &state);
	buf_size = snd_pcm_lib_buffer_bytes(substream);
	if (state.residue > 0 && state.residue <= buf_size)
		pos = buf_size - state.residue;

	return dlp_bytes_to_frames(&ddrd->drd, pos);
}

static void dmaengine_dlp_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct dlp_runtime_data *drd;
	struct dlp *dlp;

	snd_pcm_stream_lock_irq(substream);
	if (!substream->runtime) {
		snd_pcm_stream_unlock_irq(substream);
		return;
	}

	drd = substream_to_drd(substream);
	dlp = drd->parent;

	dlp_dma_complete(dlp, drd);
	snd_pcm_stream_unlock_irq(substream);

	snd_pcm_period_elapsed(substream);
}

static int dmaengine_dlp_prepare_and_submit(struct snd_pcm_substream *substream)
{
	struct dmaengine_dlp_runtime_data *ddrd = substream_to_ddrd(substream);
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;

	if (unlikely(!ddrd))
		return -EINVAL;

	chan = ddrd->dma_chan;
	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	desc = dmaengine_prep_dma_cyclic(chan, substream->runtime->dma_addr,
					 snd_pcm_lib_buffer_bytes(substream),
					 snd_pcm_lib_period_bytes(substream),
					 direction, flags);

	if (!desc)
		return -ENOMEM;

	desc->callback = dmaengine_dlp_dma_complete;
	desc->callback_param = substream;
	ddrd->cookie = dmaengine_submit(desc);

	return 0;
}

static int dmaengine_dlp_start(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct dlp *dlp = soc_component_to_dlp(component);

	return dlp_start(component, substream, dlp->dev, dmaengine_dlp_pointer);
}

static void dmaengine_dlp_stop(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	dlp_stop(component, substream, dmaengine_dlp_pointer);
}

static int dmaengine_dlp_trigger(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream, int cmd)
{
	struct dmaengine_dlp_runtime_data *ddrd = substream_to_ddrd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	if (unlikely(!ddrd))
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = dmaengine_dlp_prepare_and_submit(substream);
		if (ret)
			return ret;
		dma_async_issue_pending(ddrd->dma_chan);
		dmaengine_dlp_start(component, substream);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dmaengine_resume(ddrd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE) {
			dmaengine_pause(ddrd->dma_chan);
		} else {
			dmaengine_dlp_stop(component, substream);
			dmaengine_terminate_async(ddrd->dma_chan);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(ddrd->dma_chan);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dmaengine_dlp_stop(component, substream);
		dmaengine_terminate_async(ddrd->dma_chan);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmaengine_dlp_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct dmaengine_dlp *ddlp = soc_component_to_ddlp(component);
	struct snd_pcm_substream *substream;
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;

	prealloc_buffer_size = prealloc_buffer_size_kbytes * 1024;
	max_buffer_size = SIZE_MAX;

	for_each_pcm_streams(i) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		if (!ddlp->chan[i]) {
			dev_err(component->dev,
				"Missing dma channel for stream: %d\n", i);
			return -EINVAL;
		}

		snd_pcm_set_managed_buffer(substream, SNDRV_DMA_TYPE_DEV_IRAM,
					   dmaengine_dma_dev(ddlp, substream),
					   prealloc_buffer_size,
					   max_buffer_size);

		if (rtd->pcm->streams[i].pcm->name[0] == '\0') {
			strscpy_pad(rtd->pcm->streams[i].pcm->name,
				    rtd->pcm->streams[i].pcm->id,
				    sizeof(rtd->pcm->streams[i].pcm->name));
		}
	}

	return 0;
}

static int dmaengine_dlp_copy_user(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream,
				   int channel, unsigned long hwoff,
				   void __user *buf, unsigned long bytes)
{
	return dlp_copy_user(component, substream, channel, hwoff, buf, bytes);
}

static int dmaengine_dlp_prepare(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream)
{
	return dlp_prepare(component, substream);
}

static int dmaengine_dlp_probe(struct snd_soc_component *component)
{
	return dlp_probe(component);
}

static const struct snd_soc_component_driver dmaengine_dlp_component = {
	.name		= SND_DMAENGINE_DLP_DRV_NAME,
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.probe		= dmaengine_dlp_probe,
	.open		= dmaengine_dlp_open,
	.close		= dmaengine_dlp_close,
	.hw_params	= dmaengine_dlp_hw_params,
	.prepare	= dmaengine_dlp_prepare,
	.trigger	= dmaengine_dlp_trigger,
	.pointer	= dmaengine_dlp_pointer,
	.copy_user	= dmaengine_dlp_copy_user,
	.pcm_construct	= dmaengine_dlp_new,
};

static const char * const dmaengine_pcm_dma_channel_names[] = {
	[SNDRV_PCM_STREAM_PLAYBACK] = "tx",
	[SNDRV_PCM_STREAM_CAPTURE] = "rx",
};

static int dmaengine_pcm_request_chan_of(struct dmaengine_dlp *ddlp,
	struct device *dev, const struct snd_dmaengine_pcm_config *config)
{
	unsigned int i;
	const char *name;
	struct dma_chan *chan;

	for_each_pcm_streams(i) {
		name = dmaengine_pcm_dma_channel_names[i];
		chan = dma_request_chan(dev, name);
		if (IS_ERR(chan)) {
			/*
			 * Only report probe deferral errors, channels
			 * might not be present for devices that
			 * support only TX or only RX.
			 */
			if (PTR_ERR(chan) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			ddlp->chan[i] = NULL;
		} else {
			ddlp->chan[i] = chan;
		}
	}

	return 0;
}

static void dmaengine_pcm_release_chan(struct dmaengine_dlp *ddlp)
{
	unsigned int i;

	for_each_pcm_streams(i) {
		if (!ddlp->chan[i])
			continue;
		dma_release_channel(ddlp->chan[i]);
	}
}

/**
 * snd_dmaengine_dlp_register - Register a dmaengine based DLP device
 * @dev: The parent device for the DLP device
 * @config: Platform specific DLP configuration
 */
static int snd_dmaengine_dlp_register(struct device *dev,
				      const struct snd_dlp_config *config)
{
	struct dmaengine_dlp *ddlp;
	int ret;

	ddlp = kzalloc(sizeof(*ddlp), GFP_KERNEL);
	if (!ddlp)
		return -ENOMEM;

	ret = dmaengine_pcm_request_chan_of(ddlp, dev, NULL);
	if (ret)
		goto err_free_dma;

	ret = dlp_register(&ddlp->dlp, dev, &dmaengine_dlp_component, config);
	if (ret)
		goto err_free_dma;

	return 0;

err_free_dma:
	dmaengine_pcm_release_chan(ddlp);
	kfree(ddlp);
	return ret;
}

/**
 * snd_dmaengine_dlp_unregister - Removes a dmaengine based DLP device
 * @dev: Parent device the DLP was register with
 *
 * Removes a dmaengine based DLP device previously registered with
 * snd_dmaengine_dlp_register.
 */
static void snd_dmaengine_dlp_unregister(struct device *dev)
{
	struct snd_soc_component *component;
	struct dmaengine_dlp *ddlp;

	component = snd_soc_lookup_component(dev, SND_DMAENGINE_DLP_DRV_NAME);
	if (!component)
		return;

	ddlp = soc_component_to_ddlp(component);

	snd_soc_unregister_component_by_driver(dev, component->driver);
	dmaengine_pcm_release_chan(ddlp);

	kfree(ddlp);
}

static void devm_dmaengine_dlp_stop(struct device *dev, void *res)
{
	snd_dmaengine_dlp_unregister(*(struct device **)res);
}

/**
 * devm_snd_dmaengine_dlp_register - resource managed dmaengine DLP registration
 * @dev: The parent device for the DLP device
 * @config: Platform specific DLP configuration
 *
 * Register a dmaengine based DLP device with automatic unregistration when the
 * device is unregistered.
 */
int devm_snd_dmaengine_dlp_register(struct device *dev,
				    const struct snd_dlp_config *config)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_dmaengine_dlp_stop, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = snd_dmaengine_dlp_register(dev, config);
	if (ret == 0) {
		*ptr = dev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_snd_dmaengine_dlp_register);

MODULE_DESCRIPTION("Rockchip Digital Loopback PCM Driver");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL");
