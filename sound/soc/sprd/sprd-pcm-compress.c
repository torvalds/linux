// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Spreadtrum Communications Inc.

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/sprd-dma.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>

#include "sprd-pcm-dma.h"

#define SPRD_COMPR_DMA_CHANS		2

/* Default values if userspace does not set */
#define SPRD_COMPR_MIN_FRAGMENT_SIZE	SZ_8K
#define SPRD_COMPR_MAX_FRAGMENT_SIZE	SZ_128K
#define SPRD_COMPR_MIN_NUM_FRAGMENTS	4
#define SPRD_COMPR_MAX_NUM_FRAGMENTS	64

/* DSP FIFO size */
#define SPRD_COMPR_MCDT_EMPTY_WMK	0
#define SPRD_COMPR_MCDT_FIFO_SIZE	512

/* Stage 0 IRAM buffer size definition */
#define SPRD_COMPR_IRAM_BUF_SIZE	SZ_32K
#define SPRD_COMPR_IRAM_INFO_SIZE	(sizeof(struct sprd_compr_playinfo))
#define SPRD_COMPR_IRAM_LINKLIST_SIZE	(1024 - SPRD_COMPR_IRAM_INFO_SIZE)
#define SPRD_COMPR_IRAM_SIZE		(SPRD_COMPR_IRAM_BUF_SIZE + \
					 SPRD_COMPR_IRAM_INFO_SIZE + \
					 SPRD_COMPR_IRAM_LINKLIST_SIZE)

/* Stage 1 DDR buffer size definition */
#define SPRD_COMPR_AREA_BUF_SIZE	SZ_2M
#define SPRD_COMPR_AREA_LINKLIST_SIZE	1024
#define SPRD_COMPR_AREA_SIZE		(SPRD_COMPR_AREA_BUF_SIZE + \
					 SPRD_COMPR_AREA_LINKLIST_SIZE)

struct sprd_compr_dma {
	struct dma_chan *chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	dma_addr_t phys;
	void *virt;
	int trans_len;
};

/*
 * The Spreadtrum Audio compress offload mode will use 2-stage DMA transfer to
 * save power. That means we can request 2 dma channels, one for source channel,
 * and another one for destination channel. Once the source channel's transaction
 * is done, it will trigger the destination channel's transaction automatically
 * by hardware signal.
 *
 * For 2-stage DMA transfer, we can allocate 2 buffers: IRAM buffer (always
 * power-on) and DDR buffer. The source channel will transfer data from IRAM
 * buffer to the DSP fifo to decoding/encoding, once IRAM buffer is empty by
 * transferring done, the destination channel will start to transfer data from
 * DDR buffer to IRAM buffer.
 *
 * Since the DSP fifo is only 512B, IRAM buffer is allocated by 32K, and DDR
 * buffer is larger to 2M. That means only the IRAM 32k data is transferred
 * done, we can wake up the AP system to transfer data from DDR to IRAM, and
 * other time the AP system can be suspended to save power.
 */
struct sprd_compr_stream {
	struct snd_compr_stream *cstream;
	struct sprd_compr_ops *compr_ops;
	struct sprd_compr_dma dma[SPRD_COMPR_DMA_CHANS];

	/* DMA engine channel number */
	int num_channels;

	/* Stage 0 IRAM buffer */
	struct snd_dma_buffer iram_buffer;
	/* Stage 1 DDR buffer */
	struct snd_dma_buffer compr_buffer;

	/* DSP play information IRAM buffer */
	dma_addr_t info_phys;
	void *info_area;
	int info_size;

	/* Data size copied to IRAM buffer */
	u64 copied_total;
	/* Total received data size from userspace */
	u64 received_total;
	/* Stage 0 IRAM buffer received data size */
	int received_stage0;
	/* Stage 1 DDR buffer received data size */
	int received_stage1;
	/* Stage 1 DDR buffer pointer */
	int stage1_pointer;
};

static int sprd_platform_compr_trigger(struct snd_soc_component *component,
				       struct snd_compr_stream *cstream,
				       int cmd);

static void sprd_platform_compr_drain_notify(void *arg)
{
	struct snd_compr_stream *cstream = arg;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;

	memset(stream->info_area, 0, sizeof(struct sprd_compr_playinfo));

	snd_compr_drain_notify(cstream);
}

static void sprd_platform_compr_dma_complete(void *data)
{
	struct snd_compr_stream *cstream = data;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct sprd_compr_dma *dma = &stream->dma[1];

	/* Update data size copied to IRAM buffer */
	stream->copied_total += dma->trans_len;
	if (stream->copied_total > stream->received_total)
		stream->copied_total = stream->received_total;

	snd_compr_fragment_elapsed(cstream);
}

static int sprd_platform_compr_dma_config(struct snd_soc_component *component,
					  struct snd_compr_stream *cstream,
					  struct snd_compr_params *params,
					  int channel)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct device *dev = component->dev;
	struct sprd_compr_data *data = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct sprd_pcm_dma_params *dma_params = data->dma_params;
	struct sprd_compr_dma *dma = &stream->dma[channel];
	struct dma_slave_config config = { };
	struct sprd_dma_linklist link = { };
	enum dma_transfer_direction dir;
	struct scatterlist *sg, *sgt;
	enum dma_slave_buswidth bus_width;
	int period, period_cnt, sg_num = 2;
	dma_addr_t src_addr, dst_addr;
	unsigned long flags;
	int ret, j;

	if (!dma_params) {
		dev_err(dev, "no dma parameters setting\n");
		return -EINVAL;
	}

	dma->chan = dma_request_slave_channel(dev,
					      dma_params->chan_name[channel]);
	if (!dma->chan) {
		dev_err(dev, "failed to request dma channel\n");
		return -ENODEV;
	}

	sgt = sg = kcalloc(sg_num, sizeof(*sg), GFP_KERNEL);
	if (!sg) {
		ret = -ENOMEM;
		goto sg_err;
	}

	switch (channel) {
	case 0:
		bus_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		period = (SPRD_COMPR_MCDT_FIFO_SIZE - SPRD_COMPR_MCDT_EMPTY_WMK) * 4;
		period_cnt = params->buffer.fragment_size / period;
		src_addr = stream->iram_buffer.addr;
		dst_addr = dma_params->dev_phys[channel];
		flags = SPRD_DMA_FLAGS(SPRD_DMA_SRC_CHN1,
				       SPRD_DMA_TRANS_DONE_TRG,
				       SPRD_DMA_FRAG_REQ,
				       SPRD_DMA_TRANS_INT);
		break;

	case 1:
		bus_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		period = params->buffer.fragment_size;
		period_cnt = params->buffer.fragments;
		src_addr = stream->compr_buffer.addr;
		dst_addr = stream->iram_buffer.addr;
		flags = SPRD_DMA_FLAGS(SPRD_DMA_DST_CHN1,
				       SPRD_DMA_TRANS_DONE_TRG,
				       SPRD_DMA_FRAG_REQ,
				       SPRD_DMA_TRANS_INT);
		break;

	default:
		ret = -EINVAL;
		goto config_err;
	}

	dma->trans_len = period * period_cnt;

	config.src_maxburst = period;
	config.src_addr_width = bus_width;
	config.dst_addr_width = bus_width;
	if (cstream->direction == SND_COMPRESS_PLAYBACK) {
		config.src_addr = src_addr;
		config.dst_addr = dst_addr;
		dir = DMA_MEM_TO_DEV;
	} else {
		config.src_addr = dst_addr;
		config.dst_addr = src_addr;
		dir = DMA_DEV_TO_MEM;
	}

	sg_init_table(sgt, sg_num);
	for (j = 0; j < sg_num; j++, sgt++) {
		sg_dma_len(sgt) = dma->trans_len;
		sg_dma_address(sgt) = dst_addr;
	}

	/*
	 * Configure the link-list address for the DMA engine link-list
	 * mode.
	 */
	link.virt_addr = (unsigned long)dma->virt;
	link.phy_addr = dma->phys;

	ret = dmaengine_slave_config(dma->chan, &config);
	if (ret) {
		dev_err(dev,
			"failed to set slave configuration: %d\n", ret);
		goto config_err;
	}

	/*
	 * We configure the DMA request mode, interrupt mode, channel
	 * mode and channel trigger mode by the flags.
	 */
	dma->desc = dma->chan->device->device_prep_slave_sg(dma->chan, sg,
							    sg_num, dir,
							    flags, &link);
	if (!dma->desc) {
		dev_err(dev, "failed to prepare slave sg\n");
		ret = -ENOMEM;
		goto config_err;
	}

	/* Only channel 1 transfer can wake up the AP system. */
	if (!params->no_wake_mode && channel == 1) {
		dma->desc->callback = sprd_platform_compr_dma_complete;
		dma->desc->callback_param = cstream;
	}

	kfree(sg);

	return 0;

config_err:
	kfree(sg);
sg_err:
	dma_release_channel(dma->chan);
	return ret;
}

static int sprd_platform_compr_set_params(struct snd_soc_component *component,
					  struct snd_compr_stream *cstream,
					  struct snd_compr_params *params)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct device *dev = component->dev;
	struct sprd_compr_params compr_params = { };
	int ret;

	/*
	 * Configure the DMA engine 2-stage transfer mode. Channel 1 set as the
	 * destination channel, and channel 0 set as the source channel, that
	 * means once the source channel's transaction is done, it will trigger
	 * the destination channel's transaction automatically.
	 */
	ret = sprd_platform_compr_dma_config(component, cstream, params, 1);
	if (ret) {
		dev_err(dev, "failed to config stage 1 DMA: %d\n", ret);
		return ret;
	}

	ret = sprd_platform_compr_dma_config(component, cstream, params, 0);
	if (ret) {
		dev_err(dev, "failed to config stage 0 DMA: %d\n", ret);
		goto config_err;
	}

	compr_params.direction = cstream->direction;
	compr_params.sample_rate = params->codec.sample_rate;
	compr_params.channels = stream->num_channels;
	compr_params.info_phys = stream->info_phys;
	compr_params.info_size = stream->info_size;
	compr_params.rate = params->codec.bit_rate;
	compr_params.format = params->codec.id;

	ret = stream->compr_ops->set_params(cstream->direction, &compr_params);
	if (ret) {
		dev_err(dev, "failed to set parameters: %d\n", ret);
		goto params_err;
	}

	return 0;

params_err:
	dma_release_channel(stream->dma[0].chan);
config_err:
	dma_release_channel(stream->dma[1].chan);
	return ret;
}

static int sprd_platform_compr_open(struct snd_soc_component *component,
				    struct snd_compr_stream *cstream)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	struct device *dev = component->dev;
	struct sprd_compr_data *data = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct sprd_compr_stream *stream;
	struct sprd_compr_callback cb;
	int stream_id = cstream->direction, ret;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	stream = devm_kzalloc(dev, sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	stream->cstream = cstream;
	stream->num_channels = 2;
	stream->compr_ops = data->ops;

	/*
	 * Allocate the stage 0 IRAM buffer size, including the DMA 0
	 * link-list size and play information of DSP address size.
	 */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_IRAM, dev,
				  SPRD_COMPR_IRAM_SIZE, &stream->iram_buffer);
	if (ret < 0)
		goto err_iram;

	/* Use to save link-list configuration for DMA 0. */
	stream->dma[0].virt = stream->iram_buffer.area + SPRD_COMPR_IRAM_SIZE;
	stream->dma[0].phys = stream->iram_buffer.addr + SPRD_COMPR_IRAM_SIZE;

	/* Use to update the current data offset of DSP. */
	stream->info_phys = stream->iram_buffer.addr + SPRD_COMPR_IRAM_SIZE +
		SPRD_COMPR_IRAM_LINKLIST_SIZE;
	stream->info_area = stream->iram_buffer.area + SPRD_COMPR_IRAM_SIZE +
		SPRD_COMPR_IRAM_LINKLIST_SIZE;
	stream->info_size = SPRD_COMPR_IRAM_INFO_SIZE;

	/*
	 * Allocate the stage 1 DDR buffer size, including the DMA 1 link-list
	 * size.
	 */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, dev,
				  SPRD_COMPR_AREA_SIZE, &stream->compr_buffer);
	if (ret < 0)
		goto err_compr;

	/* Use to save link-list configuration for DMA 1. */
	stream->dma[1].virt = stream->compr_buffer.area + SPRD_COMPR_AREA_SIZE;
	stream->dma[1].phys = stream->compr_buffer.addr + SPRD_COMPR_AREA_SIZE;

	cb.drain_notify = sprd_platform_compr_drain_notify;
	cb.drain_data = cstream;
	ret = stream->compr_ops->open(stream_id, &cb);
	if (ret) {
		dev_err(dev, "failed to open compress platform: %d\n", ret);
		goto err_open;
	}

	runtime->private_data = stream;
	return 0;

err_open:
	snd_dma_free_pages(&stream->compr_buffer);
err_compr:
	snd_dma_free_pages(&stream->iram_buffer);
err_iram:
	devm_kfree(dev, stream);

	return ret;
}

static int sprd_platform_compr_free(struct snd_soc_component *component,
				    struct snd_compr_stream *cstream)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct device *dev = component->dev;
	int stream_id = cstream->direction, i;

	for (i = 0; i < stream->num_channels; i++) {
		struct sprd_compr_dma *dma = &stream->dma[i];

		if (dma->chan) {
			dma_release_channel(dma->chan);
			dma->chan = NULL;
		}
	}

	snd_dma_free_pages(&stream->compr_buffer);
	snd_dma_free_pages(&stream->iram_buffer);

	stream->compr_ops->close(stream_id);

	devm_kfree(dev, stream);
	return 0;
}

static int sprd_platform_compr_trigger(struct snd_soc_component *component,
				       struct snd_compr_stream *cstream,
				       int cmd)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct device *dev = component->dev;
	int channels = stream->num_channels, ret = 0, i;
	int stream_id = cstream->direction;

	if (cstream->direction != SND_COMPRESS_PLAYBACK) {
		dev_err(dev, "unsupported compress direction\n");
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		for (i = channels - 1; i >= 0; i--) {
			struct sprd_compr_dma *dma = &stream->dma[i];

			if (!dma->desc)
				continue;

			dma->cookie = dmaengine_submit(dma->desc);
			ret = dma_submit_error(dma->cookie);
			if (ret) {
				dev_err(dev, "failed to submit request: %d\n",
					ret);
				return ret;
			}
		}

		for (i = channels - 1; i >= 0; i--) {
			struct sprd_compr_dma *dma = &stream->dma[i];

			if (dma->chan)
				dma_async_issue_pending(dma->chan);
		}

		ret = stream->compr_ops->start(stream_id);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		for (i = channels - 1; i >= 0; i--) {
			struct sprd_compr_dma *dma = &stream->dma[i];

			if (dma->chan)
				dmaengine_terminate_async(dma->chan);
		}

		stream->copied_total = 0;
		stream->stage1_pointer  = 0;
		stream->received_total = 0;
		stream->received_stage0 = 0;
		stream->received_stage1 = 0;

		ret = stream->compr_ops->stop(stream_id);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		for (i = channels - 1; i >= 0; i--) {
			struct sprd_compr_dma *dma = &stream->dma[i];

			if (dma->chan)
				dmaengine_pause(dma->chan);
		}

		ret = stream->compr_ops->pause(stream_id);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		for (i = channels - 1; i >= 0; i--) {
			struct sprd_compr_dma *dma = &stream->dma[i];

			if (dma->chan)
				dmaengine_resume(dma->chan);
		}

		ret = stream->compr_ops->pause_release(stream_id);
		break;

	case SND_COMPR_TRIGGER_PARTIAL_DRAIN:
	case SND_COMPR_TRIGGER_DRAIN:
		ret = stream->compr_ops->drain(stream->received_total);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int sprd_platform_compr_pointer(struct snd_soc_component *component,
				       struct snd_compr_stream *cstream,
				       struct snd_compr_tstamp64 *tstamp)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	struct sprd_compr_playinfo *info =
		(struct sprd_compr_playinfo *)stream->info_area;

	tstamp->copied_total = stream->copied_total;
	tstamp->pcm_io_frames = info->current_data_offset;

	return 0;
}

static int sprd_platform_compr_copy(struct snd_soc_component *component,
				    struct snd_compr_stream *cstream,
				    char __user *buf, size_t count)
{
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sprd_compr_stream *stream = runtime->private_data;
	int avail_bytes, data_count = count;
	void *dst;

	/*
	 * We usually set fragment size as 32K, and the stage 0 IRAM buffer
	 * size is 32K too. So if now the received data size of the stage 0
	 * IRAM buffer is less than 32K, that means we have some available
	 * spaces for the stage 0 IRAM buffer.
	 */
	if (stream->received_stage0 < runtime->fragment_size) {
		avail_bytes = runtime->fragment_size - stream->received_stage0;
		dst = stream->iram_buffer.area + stream->received_stage0;

		if (avail_bytes >= data_count) {
			/*
			 * Copy data to the stage 0 IRAM buffer directly if
			 * spaces are enough.
			 */
			if (copy_from_user(dst, buf, data_count))
				return -EFAULT;

			stream->received_stage0 += data_count;
			stream->copied_total += data_count;
			goto copy_done;
		} else {
			/*
			 * If the data count is larger than the available spaces
			 * of the stage 0 IRAM buffer, we should copy one
			 * partial data to the stage 0 IRAM buffer, and copy
			 * the left to the stage 1 DDR buffer.
			 */
			if (copy_from_user(dst, buf, avail_bytes))
				return -EFAULT;

			data_count -= avail_bytes;
			stream->received_stage0 += avail_bytes;
			stream->copied_total += avail_bytes;
			buf += avail_bytes;
		}
	}

	/*
	 * Copy data to the stage 1 DDR buffer if no spaces for the stage 0 IRAM
	 * buffer.
	 */
	dst = stream->compr_buffer.area + stream->stage1_pointer;
	if (data_count < stream->compr_buffer.bytes - stream->stage1_pointer) {
		if (copy_from_user(dst, buf, data_count))
			return -EFAULT;

		stream->stage1_pointer += data_count;
	} else {
		avail_bytes = stream->compr_buffer.bytes - stream->stage1_pointer;

		if (copy_from_user(dst, buf, avail_bytes))
			return -EFAULT;

		if (copy_from_user(stream->compr_buffer.area, buf + avail_bytes,
				   data_count - avail_bytes))
			return -EFAULT;

		stream->stage1_pointer = data_count - avail_bytes;
	}

	stream->received_stage1 += data_count;

copy_done:
	/* Update the copied data size. */
	stream->received_total += count;
	return count;
}

static int sprd_platform_compr_get_caps(struct snd_soc_component *component,
					struct snd_compr_stream *cstream,
					struct snd_compr_caps *caps)
{
	caps->direction = cstream->direction;
	caps->min_fragment_size = SPRD_COMPR_MIN_FRAGMENT_SIZE;
	caps->max_fragment_size = SPRD_COMPR_MAX_FRAGMENT_SIZE;
	caps->min_fragments = SPRD_COMPR_MIN_NUM_FRAGMENTS;
	caps->max_fragments = SPRD_COMPR_MAX_NUM_FRAGMENTS;
	caps->num_codecs = 2;
	caps->codecs[0] = SND_AUDIOCODEC_MP3;
	caps->codecs[1] = SND_AUDIOCODEC_AAC;

	return 0;
}

static int
sprd_platform_compr_get_codec_caps(struct snd_soc_component *component,
				   struct snd_compr_stream *cstream,
				   struct snd_compr_codec_caps *codec)
{
	switch (codec->codec) {
	case SND_AUDIOCODEC_MP3:
		codec->num_descriptors = 2;
		codec->descriptor[0].max_ch = 2;
		codec->descriptor[0].bit_rate[0] = 320;
		codec->descriptor[0].bit_rate[1] = 128;
		codec->descriptor[0].num_bitrates = 2;
		codec->descriptor[0].profiles = 0;
		codec->descriptor[0].modes = SND_AUDIOCHANMODE_MP3_STEREO;
		codec->descriptor[0].formats = 0;
		break;

	case SND_AUDIOCODEC_AAC:
		codec->num_descriptors = 2;
		codec->descriptor[1].max_ch = 2;
		codec->descriptor[1].bit_rate[0] = 320;
		codec->descriptor[1].bit_rate[1] = 128;
		codec->descriptor[1].num_bitrates = 2;
		codec->descriptor[1].profiles = 0;
		codec->descriptor[1].modes = 0;
		codec->descriptor[1].formats = 0;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

const struct snd_compress_ops sprd_platform_compress_ops = {
	.open = sprd_platform_compr_open,
	.free = sprd_platform_compr_free,
	.set_params = sprd_platform_compr_set_params,
	.trigger = sprd_platform_compr_trigger,
	.pointer = sprd_platform_compr_pointer,
	.copy = sprd_platform_compr_copy,
	.get_caps = sprd_platform_compr_get_caps,
	.get_codec_caps = sprd_platform_compr_get_codec_caps,
};

MODULE_DESCRIPTION("Spreadtrum ASoC Compress Platform Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:compress-platform");
