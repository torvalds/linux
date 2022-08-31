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

#define MAX_FIFO_SIZE	32 /* max fifo size in frames */
#define SND_DMAENGINE_MPCM_DRV_NAME "snd_dmaengine_mpcm"

struct dmaengine_mpcm {
	struct rk_mdais_dev *mdais;
	struct dma_chan *tx_chans[MAX_DAIS];
	struct dma_chan *rx_chans[MAX_DAIS];
	struct snd_soc_component component;
};

struct dmaengine_mpcm_runtime_data {
	struct dma_chan *chans[MAX_DAIS];
	dma_cookie_t cookies[MAX_DAIS];
	unsigned int *channel_maps;
	int num_chans;
	unsigned int pos;
	unsigned int master_chan;
	bool start_flag;
#ifdef CONFIG_SND_SOC_ROCKCHIP_VAD
	unsigned int vpos;
	unsigned int vresidue_bytes;
#endif
};

static inline struct dmaengine_mpcm_runtime_data *substream_to_prtd(
	const struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}

static struct dmaengine_mpcm *soc_component_to_mpcm(struct snd_soc_component *p)
{
	return container_of(p, struct dmaengine_mpcm, component);
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
#ifdef CONFIG_SND_SOC_ROCKCHIP_VAD
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);

	if (snd_pcm_vad_attached(substream) &&
	    substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		void *buf = substream->runtime->dma_area + prtd->pos;

		snd_pcm_vad_preprocess(substream, buf,
				       substream->runtime->period_size);
	}

	prtd->pos += snd_pcm_lib_period_bytes(substream);
	if (prtd->pos >= snd_pcm_lib_buffer_bytes(substream))
		prtd->pos = 0;

#endif
	snd_pcm_period_elapsed(substream);
}

static void dmaengine_mpcm_get_master_chan(struct dmaengine_mpcm_runtime_data *prtd)
{
	int i;

	for (i = prtd->num_chans; i > 0; i--) {
		if (prtd->chans[i - 1]) {
			prtd->master_chan = i - 1;
			break;
		}
	}
}

static int dmaengine_mpcm_prepare_and_submit(struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_async_tx_descriptor *desc = NULL;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	unsigned int *maps = prtd->channel_maps;
	int offset, buffer_bytes, period_bytes;
	int i;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	prtd->pos = 0;
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

		prtd->cookies[i] = dmaengine_submit(desc);
		offset += samples_to_bytes(runtime, maps[i]);
	}

	if (desc) {
		desc->callback = dmaengine_mpcm_dma_complete;
		desc->callback_param = substream;
		dmaengine_mpcm_get_master_chan(prtd);
	} else {
		return -ENOMEM;
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

#ifdef CONFIG_SND_SOC_ROCKCHIP_VAD
static void dmaengine_mpcm_single_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	unsigned int pos, size;
	void *buf;

	if (snd_pcm_vad_attached(substream) &&
	    substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		buf = substream->runtime->dma_area + prtd->vpos;
		pos = prtd->vpos + snd_pcm_lib_period_bytes(substream);

		if (pos <= snd_pcm_lib_buffer_bytes(substream))
			size = substream->runtime->period_size;
		else
			size = bytes_to_frames(substream->runtime,
					       prtd->vresidue_bytes);
		snd_pcm_vad_preprocess(substream, buf, size);
	}

	prtd->vpos += snd_pcm_lib_period_bytes(substream);
	if (prtd->vpos >= snd_pcm_lib_buffer_bytes(substream))
		prtd->vpos = 0;
	snd_pcm_period_elapsed(substream);
}

static int __mpcm_prepare_single_and_submit(struct snd_pcm_substream *substream,
					    dma_addr_t buf_start, int size)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	unsigned int *maps = prtd->channel_maps;
	int offset, i;
	bool callback = false;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	offset = 0;
	for (i = 0; i < prtd->num_chans; i++) {
		if (!prtd->chans[i])
			continue;
		desc = dmaengine_prep_slave_single(prtd->chans[i],
						   buf_start + offset,
						   size,
						   direction, flags);

		if (!desc)
			return -ENOMEM;
		if (!callback) {
			desc->callback = dmaengine_mpcm_single_dma_complete;
			desc->callback_param = substream;
			callback = true;
		}
		dmaengine_submit(desc);
		offset += samples_to_bytes(runtime, maps[i]);
	}

	return 0;
}

static int dmaengine_mpcm_prepare_single_and_submit(struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	snd_pcm_uframes_t avail;
	dma_addr_t buf_start, buf_end;
	int offset, i, count, ret;
	int buffer_bytes, period_bytes, residue_bytes;

	direction = snd_pcm_substream_to_dma_direction(substream);

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	period_bytes = snd_pcm_lib_period_bytes(substream);
	buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	avail = snd_pcm_vad_avail(substream);
	offset = frames_to_bytes(substream->runtime, avail);
	prtd->vpos = offset;
	buf_start = substream->runtime->dma_addr + offset;
	buf_end = substream->runtime->dma_addr + snd_pcm_lib_buffer_bytes(substream);
	count = (buf_end - buf_start) / period_bytes;
	residue_bytes = (buf_end - buf_start) % period_bytes;
	prtd->vresidue_bytes = residue_bytes;
	pr_debug("%s: offset: %d, buffer_bytes: %d\n", __func__, offset, buffer_bytes);
	pr_debug("%s: count: %d, residue_bytes: %d\n", __func__, count, residue_bytes);
	for (i = 0; i < count; i++) {
		ret = __mpcm_prepare_single_and_submit(substream, buf_start,
						       period_bytes);
		if (ret)
			return ret;
		buf_start += period_bytes;
	}

	if (residue_bytes) {
		ret = __mpcm_prepare_single_and_submit(substream, buf_start,
						       residue_bytes);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static int snd_dmaengine_mpcm_trigger(struct snd_soc_component *component,
				      struct snd_pcm_substream *substream, int cmd)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
#ifdef CONFIG_SND_SOC_ROCKCHIP_VAD
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
		    snd_pcm_vad_attached(substream) &&
		    snd_pcm_vad_avail(substream)) {
			dmaengine_mpcm_prepare_single_and_submit(substream);
			mpcm_dma_async_issue_pending(prtd);
		}
#endif
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
		prtd->start_flag = false;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		mpcm_dmaengine_pause(prtd);
		prtd->start_flag = false;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		mpcm_dmaengine_terminate_all(prtd);
		prtd->start_flag = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmaengine_mpcm_hw_params(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct dmaengine_mpcm *pcm = soc_component_to_mpcm(component);
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
#ifdef CONFIG_NO_GKI
			if (sz) {
				slave_config.src_interlace_size = frame_bytes - sz;
				if (slave_config.src_interlace_size)
					slave_config.dst_maxburst = sz / slave_config.dst_addr_width;
			}
#endif
		} else {
			chan = pcm->rx_chans[i];
#ifdef CONFIG_NO_GKI
			if (sz) {
				slave_config.dst_interlace_size = frame_bytes - sz;
				if (slave_config.dst_interlace_size)
					slave_config.src_maxburst = sz / slave_config.src_addr_width;
			}
#endif
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
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, SND_DMAENGINE_MPCM_DRV_NAME);
	struct dmaengine_mpcm *pcm = soc_component_to_mpcm(component);
	struct device *dma_dev = dmaengine_dma_dev(pcm, substream);
	struct dma_chan *chan;
	struct dma_slave_caps dma_caps;
	struct snd_pcm_hardware hw;
	u32 addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			  BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			  BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	snd_pcm_format_t i;
	int ret;

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
		if (dma_caps.cmd_pause && dma_caps.cmd_resume)
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
				hw.formats |= pcm_format_to_bits(i);
			break;
		default:
			/* Unsupported types */
			break;
		}
	}

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int dmaengine_mpcm_open(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm *pcm = soc_component_to_mpcm(component);
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
	prtd->start_flag = false;
	substream->runtime->private_data = prtd;

	return 0;
}

static int dmaengine_mpcm_new(struct snd_soc_component *component, struct snd_soc_pcm_runtime *rtd)
{
	struct dmaengine_mpcm *pcm = soc_component_to_mpcm(component);
	struct snd_pcm_substream *substream;
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;

	prealloc_buffer_size = 512 * 1024;
	max_buffer_size = SIZE_MAX;

	for (i = SNDRV_PCM_STREAM_PLAYBACK; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
		substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		snd_pcm_lib_preallocate_pages(substream,
					      SNDRV_DMA_TYPE_DEV_IRAM,
					      dmaengine_dma_dev(pcm, substream),
					      prealloc_buffer_size,
					      max_buffer_size);
	}

	return 0;
}

static snd_pcm_uframes_t dmaengine_mpcm_pointer(struct snd_soc_component *component,
						struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_tx_state state;
	snd_pcm_uframes_t frames;
	unsigned int buf_size;
	unsigned int pos = 0;
	unsigned int master = prtd->master_chan;

	buf_size = snd_pcm_lib_buffer_bytes(substream);
	dmaengine_tx_status(prtd->chans[master], prtd->cookies[master], &state);
	if (state.residue > 0 && state.residue <= buf_size)
		pos = buf_size - state.residue;

	frames = bytes_to_frames(substream->runtime, pos);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return frames;

#ifdef CONFIG_SND_SOC_ROCKCHIP_VAD
	if (prtd->vpos)
		frames = bytes_to_frames(substream->runtime, prtd->vpos);
#endif
	if (!prtd->start_flag && frames >= MAX_FIFO_SIZE)
		prtd->start_flag = true;

	if (prtd->start_flag) {
		if (frames >= MAX_FIFO_SIZE)
			frames -= MAX_FIFO_SIZE;
		else
			frames = runtime->buffer_size + frames - MAX_FIFO_SIZE;
	} else {
		frames = 0;
	}

	return frames;
}

static int dmaengine_mpcm_ioctl(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int dmaengine_mpcm_hw_free(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int dmaengine_mpcm_close(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	struct dmaengine_mpcm_runtime_data *prtd = substream_to_prtd(substream);

	kfree(prtd);

	return 0;
}

static const struct snd_soc_component_driver dmaengine_mpcm_platform = {
	.name		= SND_DMAENGINE_MPCM_DRV_NAME,
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.pcm_construct	= dmaengine_mpcm_new,
	.open		= dmaengine_mpcm_open,
	.close		= dmaengine_mpcm_close,
	.ioctl		= dmaengine_mpcm_ioctl,
	.hw_params	= dmaengine_mpcm_hw_params,
	.hw_free	= dmaengine_mpcm_hw_free,
	.trigger	= snd_dmaengine_mpcm_trigger,
	.pointer	= dmaengine_mpcm_pointer,
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
	unsigned int *tx_maps, *rx_maps;
	int ret, i, num;

	dev = mdais->dev;
	num = mdais->num_dais;
	tx_maps = mdais->playback_channel_maps;
	rx_maps = mdais->capture_channel_maps;
	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->mdais = mdais;
	for (i = 0; i < num; i++) {
		child = mdais->dais[i].dev;
		if (tx_maps[i]) {
			chan = dma_request_chan(child, "tx");
			if (IS_ERR(chan))
				chan = NULL;
			pcm->tx_chans[i] = chan;
		}

		if (rx_maps[i]) {
			chan = dma_request_chan(child, "rx");
			if (IS_ERR(chan))
				chan = NULL;
			pcm->rx_chans[i] = chan;
		}
	}

	ret = snd_soc_component_initialize(&pcm->component, &dmaengine_mpcm_platform,
					   dev);
	if (ret)
		goto err_free_dma;

	ret = snd_soc_add_component(&pcm->component, NULL, 0);
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
	struct snd_soc_component *component;
	struct dmaengine_mpcm *pcm;

	component = snd_soc_lookup_component(dev, SND_DMAENGINE_MPCM_DRV_NAME);
	if (!component)
		return;

	pcm = soc_component_to_mpcm(component);

	snd_soc_unregister_component(dev);
	dmaengine_mpcm_release_chan(pcm);
	kfree(pcm);
}
EXPORT_SYMBOL_GPL(snd_dmaengine_mpcm_unregister);

MODULE_LICENSE("GPL");
