// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC Audio Layer - Rockchip Multi-DAIS-PCM driver
 *
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 *
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_multi_dais.h"

struct dmaengine_mpcm {
	struct rk_mdais_dev *mdais;
	struct dma_chan *tx_chans[MAX_DAIS];
	struct dma_chan *rx_chans[MAX_DAIS];
	struct snd_soc_platform platform;
};

struct dmaengine_mpcm_runtime_data {
	struct dma_chan *chans[MAX_DAIS];
	dma_cookie_t cookies[MAX_DAIS];
	unsigned int *channel_maps;
	int num_chans;
};

static inline struct dmaengine_mpcm_runtime_data *substream_to_prtd(
	const struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}

static struct dmaengine_mpcm *soc_platform_to_pcm(struct snd_soc_platform *p)
{
	return container_of(p, struct dmaengine_mpcm, platform);
}

static struct dma_chan *to_chan(struct dmaengine_mpcm *pcm,
				struct snd_pcm_substream *substream)
{
	struct dma_chan *chan = NULL;
	int i;

	for (i = 0; i < pcm->mdais->num_dais; i++) {
		chan = substream->stream ? pcm->rx_chans[i] : pcm->tx_chans[i];
		if (chan)
			break;
	}

	return chan;
}

static struct device *dmaengine_dma_dev(struct dmaengine_mpcm *pcm,
					struct snd_pcm_substream *substream)
{
	struct dma_chan *chan;

	chan = to_chan(pcm, substream);
	if (!chan)
		return NULL;

	return chan->device->dev;
}

static void snd_dmaengine_mpcm_set_config_from_dai_data(
	const struct snd_pcm_substream *substream,
	const struct snd_dmaengine_dai_dma_data *dma_data,
	struct dma_slave_config *slave_config)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config->dst_addr = dma_data->addr;
		if (dma_data->addr_width != DMA_SLAVE_BUSWIDTH_UNDEFINED)
			slave_config->dst_addr_width = dma_data->addr_width;
	} else {
		slave_config->src_addr = dma_data->addr;
		if (dma_data->addr_width != DMA_SLAVE_BUSWIDTH_UNDEFINED)
			slave_config->src_addr_width = dma_data->addr_width;
	}

	slave_config->slave_id = dma_data->slave_id;
}

static void dmaengine_mpcm_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;

	snd_pcm_period_elapsed(substream);
}

static int dmaengine_mpcm_prepare_and_submit(struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	unsigned int *maps = prtd->channel_maps;
	int offset, buffer_bytes, period_bytes;
	int i;
	bool callback = false;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	offset = 0;
	period_bytes = snd_pcm_lib_period_bytes(substream);
	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	for (i = 0; i < prtd->num_chans; i++) {
		if (!prtd->chans[i])
			continue;
		desc = dmaengine_prep_dma_cyclic(prtd->chans[i],
						 runtime->dma_addr + offset,
						 buffer_bytes, period_bytes,
						 direction, flags);

		if (!desc)
			return -ENOMEM;
		if (!callback) {
			desc->callback = dmaengine_mpcm_dma_complete;
			desc->callback_param = substream;
			callback = true;
		}
		prtd->cookies[i] = dmaengine_submit(desc);
		offset += samples_to_bytes(runtime, maps[i]);
	}

	return 0;
}

static void mpcm_dma_async_issue_pending(struct dmaengine_mpcm_runtime_data *prtd)
{
	int i;

	for (i = 0; i < prtd->num_chans; i++) {
		if (prtd->chans[i])
			dma_async_issue_pending(prtd->chans[i]);
	}
}

static void mpcm_dmaengine_resume(struct dmaengine_mpcm_runtime_data *prtd)
{
	int i;

	for (i = 0; i < prtd->num_chans; i++) {
		if (prtd->chans[i])
			dmaengine_resume(prtd->chans[i]);
	}
}

static void mpcm_dmaengine_pause(struct dmaengine_mpcm_runtime_data *prtd)
{
	int i;

	for (i = 0; i < prtd->num_chans; i++) {
		if (prtd->chans[i])
			dmaengine_pause(prtd->chans[i]);
	}
}

static void mpcm_dmaengine_terminate_all(struct dmaengine_mpcm_runtime_data *prtd)
{
	int i;

	for (i = 0; i < prtd->num_chans; i++) {
		if (prtd->chans[i])
			dmaengine_terminate_all(prtd->chans[i]);
	}
}

static int snd_dmaengine_mpcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = dmaengine_mpcm_prepare_and_submit(substream);
		if (ret)
			return ret;
		mpcm_dma_async_issue_pending(prtd);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mpcm_dmaengine_resume(prtd);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE)
			mpcm_dmaengine_pause(prtd);
		else
			mpcm_dmaengine_terminate_all(prtd);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		mpcm_dmaengine_pause(prtd);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		mpcm_dmaengine_terminate_all(prtd);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmaengine_mpcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dmaengine_mpcm *pcm = soc_platform_to_pcm(rtd->platform);
	struct dma_chan *chan;
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct dma_slave_config slave_config;
	snd_pcm_format_t format;
	unsigned int *maps;
	int frame_bytes;
	int ret, num, i, sz;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		maps = pcm->mdais->playback_channel_maps;
	else
		maps = pcm->mdais->capture_channel_maps;
	format = params_format(params);
	frame_bytes = snd_pcm_format_size(format, params_channels(params));
	num = pcm->mdais->num_dais;

	for (i = 0; i < num; i++) {
		memset(&slave_config, 0, sizeof(slave_config));
		ret = snd_hwparams_to_dma_slave_config(substream, params,
						       &slave_config);
		if (ret)
			return ret;

		dma_data = snd_soc_dai_get_dma_data(pcm->mdais->dais[i].dai,
						    substream);
		if (!dma_data)
			continue;

		snd_dmaengine_mpcm_set_config_from_dai_data(substream,
							    dma_data,
							    &slave_config);

		/* refine params for interlace access */
		sz = snd_pcm_format_size(format, maps[i]);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			chan = pcm->tx_chans[i];
			if (sz) {
				slave_config.dst_maxburst = sz / slave_config.dst_addr_width;
				slave_config.src_interlace_size = frame_bytes - sz;
			}
		} else {
			chan = pcm->rx_chans[i];
			if (sz) {
				slave_config.src_maxburst = sz / slave_config.src_addr_width;
				slave_config.dst_interlace_size = frame_bytes - sz;
			}
		}
		if (!chan)
			continue;

		ret = dmaengine_slave_config(chan, &slave_config);
		if (ret)
			return ret;
	}
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static int dmaengine_mpcm_set_runtime_hwparams(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dmaengine_mpcm *pcm = soc_platform_to_pcm(rtd->platform);
	struct device *dma_dev = dmaengine_dma_dev(pcm, substream);
	struct dma_chan *chan;
	struct dma_slave_caps dma_caps;
	struct snd_pcm_hardware hw;
	u32 addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			  BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			  BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	int i, ret;

	chan = to_chan(pcm, substream);
	if (!chan)
		return -EINVAL;

	memset(&hw, 0, sizeof(hw));
	hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED;
	hw.periods_min = 2;
	hw.periods_max = UINT_MAX;
	hw.period_bytes_min = 256;
	hw.period_bytes_max = dma_get_max_seg_size(dma_dev);
	hw.buffer_bytes_max = SIZE_MAX;

	ret = dma_get_slave_caps(chan, &dma_caps);
	if (ret == 0) {
		if (dma_caps.cmd_pause)
			hw.info |= SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME;
		if (dma_caps.residue_granularity <= DMA_RESIDUE_GRANULARITY_SEGMENT)
			hw.info |= SNDRV_PCM_INFO_BATCH;

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			addr_widths = dma_caps.dst_addr_widths;
		else
			addr_widths = dma_caps.src_addr_widths;
	}

	/*
	 * Prepare formats mask for valid/allowed sample types. If the dma does
	 * not have support for the given physical word size, it needs to be
	 * masked out so user space can not use the format which produces
	 * corrupted audio.
	 * In case the dma driver does not implement the slave_caps the default
	 * assumption is that it supports 1, 2 and 4 bytes widths.
	 */
	for (i = 0; i <= SNDRV_PCM_FORMAT_LAST; i++) {
		int bits = snd_pcm_format_physical_width(i);

		/* Enable only samples with DMA supported physical widths */
		switch (bits) {
		case 8:
		case 16:
		case 24:
		case 32:
		case 64:
			if (addr_widths & (1 << (bits / 8)))
				hw.formats |= (1LL << i);
			break;
		default:
			/* Unsupported types */
			break;
		}
	}

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int dmaengine_mpcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dmaengine_mpcm *pcm = soc_platform_to_pcm(rtd->platform);
	struct dmaengine_mpcm_runtime_data *prtd;
	int ret, i;

	ret = dmaengine_mpcm_set_runtime_hwparams(substream);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (!prtd)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		prtd->channel_maps = pcm->mdais->playback_channel_maps;
		for (i = 0; i < pcm->mdais->num_dais; i++)
			prtd->chans[i] = pcm->tx_chans[i];
	} else {
		prtd->channel_maps = pcm->mdais->capture_channel_maps;
		for (i = 0; i < pcm->mdais->num_dais; i++)
			prtd->chans[i] = pcm->rx_chans[i];
	}

	prtd->num_chans = pcm->mdais->num_dais;
	substream->runtime->private_data = prtd;

	return 0;
}

static int dmaengine_mpcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct dmaengine_mpcm *pcm = soc_platform_to_pcm(rtd->platform);
	struct snd_pcm_substream *substream;
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;
	int ret;

	prealloc_buffer_size = 512 * 1024;
	max_buffer_size = SIZE_MAX;

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		ret = snd_pcm_lib_preallocate_pages(substream,
						    SNDRV_DMA_TYPE_DEV_IRAM,
						    dmaengine_dma_dev(pcm, substream),
						    prealloc_buffer_size,
						    max_buffer_size);
		if (ret)
			return ret;
	}

	return 0;
}

static snd_pcm_uframes_t dmaengine_mpcm_pointer(struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct dma_tx_state state;
	unsigned int buf_size;
	unsigned int pos = 0;
	int i = 0;

	buf_size = snd_pcm_lib_buffer_bytes(substream);
	for (i = 0; i < prtd->num_chans; i++) {
		if (!prtd->chans[i])
			continue;
		dmaengine_tx_status(prtd->chans[i], prtd->cookies[i], &state);
		if (state.residue > 0 && state.residue <= buf_size)
			pos = buf_size - state.residue;
		break;
	}

	return bytes_to_frames(substream->runtime, pos);
}

static int dmaengine_mpcm_close(struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);

	kfree(prtd);

	return 0;
}

static const struct snd_pcm_ops dmaengine_mpcm_ops = {
	.open		= dmaengine_mpcm_open,
	.close		= dmaengine_mpcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= dmaengine_mpcm_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.trigger	= snd_dmaengine_mpcm_trigger,
	.pointer	= dmaengine_mpcm_pointer,
};

static const struct snd_soc_platform_driver dmaengine_mpcm_platform = {
	.component_driver = {
		.probe_order = SND_SOC_COMP_ORDER_LATE,
	},
	.ops		= &dmaengine_mpcm_ops,
	.pcm_new	= dmaengine_mpcm_new,
};

static void dmaengine_mpcm_release_chan(struct dmaengine_mpcm *pcm)
{
	int i;

	for (i = 0; i < pcm->mdais->num_dais; i++) {
		if (pcm->tx_chans[i])
			dma_release_channel(pcm->tx_chans[i]);
		if (pcm->rx_chans[i])
			dma_release_channel(pcm->rx_chans[i]);
	}
}

int snd_dmaengine_mpcm_register(struct rk_mdais_dev *mdais)
{
	struct device *dev;
	struct device *child;
	struct dmaengine_mpcm *pcm;
	struct dma_chan *chan;
	int ret, i, num;

	dev = mdais->dev;
	num = mdais->num_dais;
	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->mdais = mdais;
	for (i = 0; i < num; i++) {
		child = mdais->dais[i].dev;
		chan = dma_request_slave_channel_reason(child, "tx");
		if (IS_ERR(chan))
			chan = NULL;
		pcm->tx_chans[i] = chan;

		chan = dma_request_slave_channel_reason(child, "rx");
		if (IS_ERR(chan))
			chan = NULL;
		pcm->rx_chans[i] = chan;
	}

	ret = snd_soc_add_platform(dev, &pcm->platform,
				   &dmaengine_mpcm_platform);
	if (ret)
		goto err_free_dma;

	return 0;

err_free_dma:
	dmaengine_mpcm_release_chan(pcm);
	kfree(pcm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_dmaengine_mpcm_register);

void snd_dmaengine_mpcm_unregister(struct device *dev)
{
	struct snd_soc_platform *platform;
	struct dmaengine_mpcm *pcm;

	platform = snd_soc_lookup_platform(dev);
	if (!platform)
		return;

	pcm = soc_platform_to_pcm(platform);

	snd_soc_remove_platform(platform);
	dmaengine_mpcm_release_chan(pcm);
	kfree(pcm);
}
EXPORT_SYMBOL_GPL(snd_dmaengine_mpcm_unregister);

MODULE_LICENSE("GPL");
