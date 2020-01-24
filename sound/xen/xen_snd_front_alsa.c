// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <xen/xenbus.h>
#include <xen/xen-front-pgdir-shbuf.h>

#include "xen_snd_front.h"
#include "xen_snd_front_alsa.h"
#include "xen_snd_front_cfg.h"
#include "xen_snd_front_evtchnl.h"

struct xen_snd_front_pcm_stream_info {
	struct xen_snd_front_info *front_info;
	struct xen_snd_front_evtchnl_pair *evt_pair;

	/* This is the shared buffer with its backing storage. */
	struct xen_front_pgdir_shbuf shbuf;
	u8 *buffer;
	size_t buffer_sz;
	int num_pages;
	struct page **pages;

	int index;

	bool is_open;
	struct snd_pcm_hardware pcm_hw;

	/* Number of processed frames as reported by the backend. */
	snd_pcm_uframes_t be_cur_frame;
	/* Current HW pointer to be reported via .period callback. */
	atomic_t hw_ptr;
	/* Modulo of the number of processed frames - for period detection. */
	u32 out_frames;
};

struct xen_snd_front_pcm_instance_info {
	struct xen_snd_front_card_info *card_info;
	struct snd_pcm *pcm;
	struct snd_pcm_hardware pcm_hw;
	int num_pcm_streams_pb;
	struct xen_snd_front_pcm_stream_info *streams_pb;
	int num_pcm_streams_cap;
	struct xen_snd_front_pcm_stream_info *streams_cap;
};

struct xen_snd_front_card_info {
	struct xen_snd_front_info *front_info;
	struct snd_card *card;
	struct snd_pcm_hardware pcm_hw;
	int num_pcm_instances;
	struct xen_snd_front_pcm_instance_info *pcm_instances;
};

struct alsa_sndif_sample_format {
	u8 sndif;
	snd_pcm_format_t alsa;
};

struct alsa_sndif_hw_param {
	u8 sndif;
	snd_pcm_hw_param_t alsa;
};

static const struct alsa_sndif_sample_format ALSA_SNDIF_FORMATS[] = {
	{
		.sndif = XENSND_PCM_FORMAT_U8,
		.alsa = SNDRV_PCM_FORMAT_U8
	},
	{
		.sndif = XENSND_PCM_FORMAT_S8,
		.alsa = SNDRV_PCM_FORMAT_S8
	},
	{
		.sndif = XENSND_PCM_FORMAT_U16_LE,
		.alsa = SNDRV_PCM_FORMAT_U16_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_U16_BE,
		.alsa = SNDRV_PCM_FORMAT_U16_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S16_LE,
		.alsa = SNDRV_PCM_FORMAT_S16_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S16_BE,
		.alsa = SNDRV_PCM_FORMAT_S16_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_U24_LE,
		.alsa = SNDRV_PCM_FORMAT_U24_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_U24_BE,
		.alsa = SNDRV_PCM_FORMAT_U24_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S24_LE,
		.alsa = SNDRV_PCM_FORMAT_S24_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S24_BE,
		.alsa = SNDRV_PCM_FORMAT_S24_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_U32_LE,
		.alsa = SNDRV_PCM_FORMAT_U32_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_U32_BE,
		.alsa = SNDRV_PCM_FORMAT_U32_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S32_LE,
		.alsa = SNDRV_PCM_FORMAT_S32_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_S32_BE,
		.alsa = SNDRV_PCM_FORMAT_S32_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_A_LAW,
		.alsa = SNDRV_PCM_FORMAT_A_LAW
	},
	{
		.sndif = XENSND_PCM_FORMAT_MU_LAW,
		.alsa = SNDRV_PCM_FORMAT_MU_LAW
	},
	{
		.sndif = XENSND_PCM_FORMAT_F32_LE,
		.alsa = SNDRV_PCM_FORMAT_FLOAT_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_F32_BE,
		.alsa = SNDRV_PCM_FORMAT_FLOAT_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_F64_LE,
		.alsa = SNDRV_PCM_FORMAT_FLOAT64_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_F64_BE,
		.alsa = SNDRV_PCM_FORMAT_FLOAT64_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE,
		.alsa = SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE
	},
	{
		.sndif = XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE,
		.alsa = SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE
	},
	{
		.sndif = XENSND_PCM_FORMAT_IMA_ADPCM,
		.alsa = SNDRV_PCM_FORMAT_IMA_ADPCM
	},
	{
		.sndif = XENSND_PCM_FORMAT_MPEG,
		.alsa = SNDRV_PCM_FORMAT_MPEG
	},
	{
		.sndif = XENSND_PCM_FORMAT_GSM,
		.alsa = SNDRV_PCM_FORMAT_GSM
	},
};

static int to_sndif_format(snd_pcm_format_t format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ALSA_SNDIF_FORMATS); i++)
		if (ALSA_SNDIF_FORMATS[i].alsa == format)
			return ALSA_SNDIF_FORMATS[i].sndif;

	return -EINVAL;
}

static u64 to_sndif_formats_mask(u64 alsa_formats)
{
	u64 mask;
	int i;

	mask = 0;
	for (i = 0; i < ARRAY_SIZE(ALSA_SNDIF_FORMATS); i++)
		if (pcm_format_to_bits(ALSA_SNDIF_FORMATS[i].alsa) & alsa_formats)
			mask |= BIT_ULL(ALSA_SNDIF_FORMATS[i].sndif);

	return mask;
}

static u64 to_alsa_formats_mask(u64 sndif_formats)
{
	u64 mask;
	int i;

	mask = 0;
	for (i = 0; i < ARRAY_SIZE(ALSA_SNDIF_FORMATS); i++)
		if (BIT_ULL(ALSA_SNDIF_FORMATS[i].sndif) & sndif_formats)
			mask |= pcm_format_to_bits(ALSA_SNDIF_FORMATS[i].alsa);

	return mask;
}

static void stream_clear(struct xen_snd_front_pcm_stream_info *stream)
{
	stream->is_open = false;
	stream->be_cur_frame = 0;
	stream->out_frames = 0;
	atomic_set(&stream->hw_ptr, 0);
	xen_snd_front_evtchnl_pair_clear(stream->evt_pair);
	memset(&stream->shbuf, 0, sizeof(stream->shbuf));
	stream->buffer = NULL;
	stream->buffer_sz = 0;
	stream->pages = NULL;
	stream->num_pages = 0;
}

static void stream_free(struct xen_snd_front_pcm_stream_info *stream)
{
	xen_front_pgdir_shbuf_unmap(&stream->shbuf);
	xen_front_pgdir_shbuf_free(&stream->shbuf);
	if (stream->buffer)
		free_pages_exact(stream->buffer, stream->buffer_sz);
	kfree(stream->pages);
	stream_clear(stream);
}

static struct xen_snd_front_pcm_stream_info *
stream_get(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_instance_info *pcm_instance =
			snd_pcm_substream_chip(substream);
	struct xen_snd_front_pcm_stream_info *stream;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stream = &pcm_instance->streams_pb[substream->number];
	else
		stream = &pcm_instance->streams_cap[substream->number];

	return stream;
}

static int alsa_hw_rule(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule)
{
	struct xen_snd_front_pcm_stream_info *stream = rule->private;
	struct device *dev = &stream->front_info->xb_dev->dev;
	struct snd_mask *formats =
			hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_interval *rates =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *period =
			hw_param_interval(params,
					  SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
	struct snd_interval *buffer =
			hw_param_interval(params,
					  SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
	struct xensnd_query_hw_param req;
	struct xensnd_query_hw_param resp;
	struct snd_interval interval;
	struct snd_mask mask;
	u64 sndif_formats;
	int changed, ret;

	/* Collect all the values we need for the query. */

	req.formats = to_sndif_formats_mask((u64)formats->bits[0] |
					    (u64)(formats->bits[1]) << 32);

	req.rates.min = rates->min;
	req.rates.max = rates->max;

	req.channels.min = channels->min;
	req.channels.max = channels->max;

	req.buffer.min = buffer->min;
	req.buffer.max = buffer->max;

	req.period.min = period->min;
	req.period.max = period->max;

	ret = xen_snd_front_stream_query_hw_param(&stream->evt_pair->req,
						  &req, &resp);
	if (ret < 0) {
		/* Check if this is due to backend communication error. */
		if (ret == -EIO || ret == -ETIMEDOUT)
			dev_err(dev, "Failed to query ALSA HW parameters\n");
		return ret;
	}

	/* Refine HW parameters after the query. */
	changed  = 0;

	sndif_formats = to_alsa_formats_mask(resp.formats);
	snd_mask_none(&mask);
	mask.bits[0] = (u32)sndif_formats;
	mask.bits[1] = (u32)(sndif_formats >> 32);
	ret = snd_mask_refine(formats, &mask);
	if (ret < 0)
		return ret;
	changed |= ret;

	interval.openmin = 0;
	interval.openmax = 0;
	interval.integer = 1;

	interval.min = resp.rates.min;
	interval.max = resp.rates.max;
	ret = snd_interval_refine(rates, &interval);
	if (ret < 0)
		return ret;
	changed |= ret;

	interval.min = resp.channels.min;
	interval.max = resp.channels.max;
	ret = snd_interval_refine(channels, &interval);
	if (ret < 0)
		return ret;
	changed |= ret;

	interval.min = resp.buffer.min;
	interval.max = resp.buffer.max;
	ret = snd_interval_refine(buffer, &interval);
	if (ret < 0)
		return ret;
	changed |= ret;

	interval.min = resp.period.min;
	interval.max = resp.period.max;
	ret = snd_interval_refine(period, &interval);
	if (ret < 0)
		return ret;
	changed |= ret;

	return changed;
}

static int alsa_open(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_instance_info *pcm_instance =
			snd_pcm_substream_chip(substream);
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xen_snd_front_info *front_info =
			pcm_instance->card_info->front_info;
	struct device *dev = &front_info->xb_dev->dev;
	int ret;

	/*
	 * Return our HW properties: override defaults with those configured
	 * via XenStore.
	 */
	runtime->hw = stream->pcm_hw;
	runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_DOUBLE |
			      SNDRV_PCM_INFO_BATCH |
			      SNDRV_PCM_INFO_NONINTERLEAVED |
			      SNDRV_PCM_INFO_RESUME |
			      SNDRV_PCM_INFO_PAUSE);
	runtime->hw.info |= SNDRV_PCM_INFO_INTERLEAVED;

	stream->evt_pair = &front_info->evt_pairs[stream->index];

	stream->front_info = front_info;

	stream->evt_pair->evt.u.evt.substream = substream;

	stream_clear(stream);

	xen_snd_front_evtchnl_pair_set_connected(stream->evt_pair, true);

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				  alsa_hw_rule, stream,
				  SNDRV_PCM_HW_PARAM_FORMAT, -1);
	if (ret) {
		dev_err(dev, "Failed to add HW rule for SNDRV_PCM_HW_PARAM_FORMAT\n");
		return ret;
	}

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  alsa_hw_rule, stream,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (ret) {
		dev_err(dev, "Failed to add HW rule for SNDRV_PCM_HW_PARAM_RATE\n");
		return ret;
	}

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  alsa_hw_rule, stream,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret) {
		dev_err(dev, "Failed to add HW rule for SNDRV_PCM_HW_PARAM_CHANNELS\n");
		return ret;
	}

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
				  alsa_hw_rule, stream,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (ret) {
		dev_err(dev, "Failed to add HW rule for SNDRV_PCM_HW_PARAM_PERIOD_SIZE\n");
		return ret;
	}

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				  alsa_hw_rule, stream,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, -1);
	if (ret) {
		dev_err(dev, "Failed to add HW rule for SNDRV_PCM_HW_PARAM_BUFFER_SIZE\n");
		return ret;
	}

	return 0;
}

static int alsa_close(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	xen_snd_front_evtchnl_pair_set_connected(stream->evt_pair, false);
	return 0;
}

static int shbuf_setup_backstore(struct xen_snd_front_pcm_stream_info *stream,
				 size_t buffer_sz)
{
	int i;

	stream->buffer = alloc_pages_exact(buffer_sz, GFP_KERNEL);
	if (!stream->buffer)
		return -ENOMEM;

	stream->buffer_sz = buffer_sz;
	stream->num_pages = DIV_ROUND_UP(stream->buffer_sz, PAGE_SIZE);
	stream->pages = kcalloc(stream->num_pages, sizeof(struct page *),
				GFP_KERNEL);
	if (!stream->pages)
		return -ENOMEM;

	for (i = 0; i < stream->num_pages; i++)
		stream->pages[i] = virt_to_page(stream->buffer + i * PAGE_SIZE);

	return 0;
}

static int alsa_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	struct xen_snd_front_info *front_info = stream->front_info;
	struct xen_front_pgdir_shbuf_cfg buf_cfg;
	int ret;

	/*
	 * This callback may be called multiple times,
	 * so free the previously allocated shared buffer if any.
	 */
	stream_free(stream);
	ret = shbuf_setup_backstore(stream, params_buffer_bytes(params));
	if (ret < 0)
		goto fail;

	memset(&buf_cfg, 0, sizeof(buf_cfg));
	buf_cfg.xb_dev = front_info->xb_dev;
	buf_cfg.pgdir = &stream->shbuf;
	buf_cfg.num_pages = stream->num_pages;
	buf_cfg.pages = stream->pages;

	ret = xen_front_pgdir_shbuf_alloc(&buf_cfg);
	if (ret < 0)
		goto fail;

	ret = xen_front_pgdir_shbuf_map(&stream->shbuf);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	stream_free(stream);
	dev_err(&front_info->xb_dev->dev,
		"Failed to allocate buffers for stream with index %d\n",
		stream->index);
	return ret;
}

static int alsa_hw_free(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	int ret;

	ret = xen_snd_front_stream_close(&stream->evt_pair->req);
	stream_free(stream);
	return ret;
}

static int alsa_prepare(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	if (!stream->is_open) {
		struct snd_pcm_runtime *runtime = substream->runtime;
		u8 sndif_format;
		int ret;

		ret = to_sndif_format(runtime->format);
		if (ret < 0) {
			dev_err(&stream->front_info->xb_dev->dev,
				"Unsupported sample format: %d\n",
				runtime->format);
			return ret;
		}
		sndif_format = ret;

		ret = xen_snd_front_stream_prepare(&stream->evt_pair->req,
						   &stream->shbuf,
						   sndif_format,
						   runtime->channels,
						   runtime->rate,
						   snd_pcm_lib_buffer_bytes(substream),
						   snd_pcm_lib_period_bytes(substream));
		if (ret < 0)
			return ret;

		stream->is_open = true;
	}

	return 0;
}

static int alsa_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	int type;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		type = XENSND_OP_TRIGGER_START;
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
		type = XENSND_OP_TRIGGER_RESUME;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		type = XENSND_OP_TRIGGER_STOP;
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		type = XENSND_OP_TRIGGER_PAUSE;
		break;

	default:
		return -EINVAL;
	}

	return xen_snd_front_stream_trigger(&stream->evt_pair->req, type);
}

void xen_snd_front_alsa_handle_cur_pos(struct xen_snd_front_evtchnl *evtchnl,
				       u64 pos_bytes)
{
	struct snd_pcm_substream *substream = evtchnl->u.evt.substream;
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	snd_pcm_uframes_t delta, new_hw_ptr, cur_frame;

	cur_frame = bytes_to_frames(substream->runtime, pos_bytes);

	delta = cur_frame - stream->be_cur_frame;
	stream->be_cur_frame = cur_frame;

	new_hw_ptr = (snd_pcm_uframes_t)atomic_read(&stream->hw_ptr);
	new_hw_ptr = (new_hw_ptr + delta) % substream->runtime->buffer_size;
	atomic_set(&stream->hw_ptr, (int)new_hw_ptr);

	stream->out_frames += delta;
	if (stream->out_frames > substream->runtime->period_size) {
		stream->out_frames %= substream->runtime->period_size;
		snd_pcm_period_elapsed(substream);
	}
}

static snd_pcm_uframes_t alsa_pointer(struct snd_pcm_substream *substream)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	return (snd_pcm_uframes_t)atomic_read(&stream->hw_ptr);
}

static int alsa_pb_copy_user(struct snd_pcm_substream *substream,
			     int channel, unsigned long pos, void __user *src,
			     unsigned long count)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	if (unlikely(pos + count > stream->buffer_sz))
		return -EINVAL;

	if (copy_from_user(stream->buffer + pos, src, count))
		return -EFAULT;

	return xen_snd_front_stream_write(&stream->evt_pair->req, pos, count);
}

static int alsa_pb_copy_kernel(struct snd_pcm_substream *substream,
			       int channel, unsigned long pos, void *src,
			       unsigned long count)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	if (unlikely(pos + count > stream->buffer_sz))
		return -EINVAL;

	memcpy(stream->buffer + pos, src, count);

	return xen_snd_front_stream_write(&stream->evt_pair->req, pos, count);
}

static int alsa_cap_copy_user(struct snd_pcm_substream *substream,
			      int channel, unsigned long pos, void __user *dst,
			      unsigned long count)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	int ret;

	if (unlikely(pos + count > stream->buffer_sz))
		return -EINVAL;

	ret = xen_snd_front_stream_read(&stream->evt_pair->req, pos, count);
	if (ret < 0)
		return ret;

	return copy_to_user(dst, stream->buffer + pos, count) ?
		-EFAULT : 0;
}

static int alsa_cap_copy_kernel(struct snd_pcm_substream *substream,
				int channel, unsigned long pos, void *dst,
				unsigned long count)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);
	int ret;

	if (unlikely(pos + count > stream->buffer_sz))
		return -EINVAL;

	ret = xen_snd_front_stream_read(&stream->evt_pair->req, pos, count);
	if (ret < 0)
		return ret;

	memcpy(dst, stream->buffer + pos, count);

	return 0;
}

static int alsa_pb_fill_silence(struct snd_pcm_substream *substream,
				int channel, unsigned long pos,
				unsigned long count)
{
	struct xen_snd_front_pcm_stream_info *stream = stream_get(substream);

	if (unlikely(pos + count > stream->buffer_sz))
		return -EINVAL;

	memset(stream->buffer + pos, 0, count);

	return xen_snd_front_stream_write(&stream->evt_pair->req, pos, count);
}

/*
 * FIXME: The mmaped data transfer is asynchronous and there is no
 * ack signal from user-space when it is done. This is the
 * reason it is not implemented in the PV driver as we do need
 * to know when the buffer can be transferred to the backend.
 */

static const struct snd_pcm_ops snd_drv_alsa_playback_ops = {
	.open		= alsa_open,
	.close		= alsa_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= alsa_hw_params,
	.hw_free	= alsa_hw_free,
	.prepare	= alsa_prepare,
	.trigger	= alsa_trigger,
	.pointer	= alsa_pointer,
	.copy_user	= alsa_pb_copy_user,
	.copy_kernel	= alsa_pb_copy_kernel,
	.fill_silence	= alsa_pb_fill_silence,
};

static const struct snd_pcm_ops snd_drv_alsa_capture_ops = {
	.open		= alsa_open,
	.close		= alsa_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= alsa_hw_params,
	.hw_free	= alsa_hw_free,
	.prepare	= alsa_prepare,
	.trigger	= alsa_trigger,
	.pointer	= alsa_pointer,
	.copy_user	= alsa_cap_copy_user,
	.copy_kernel	= alsa_cap_copy_kernel,
};

static int new_pcm_instance(struct xen_snd_front_card_info *card_info,
			    struct xen_front_cfg_pcm_instance *instance_cfg,
			    struct xen_snd_front_pcm_instance_info *pcm_instance_info)
{
	struct snd_pcm *pcm;
	int ret, i;

	dev_dbg(&card_info->front_info->xb_dev->dev,
		"New PCM device \"%s\" with id %d playback %d capture %d",
		instance_cfg->name,
		instance_cfg->device_id,
		instance_cfg->num_streams_pb,
		instance_cfg->num_streams_cap);

	pcm_instance_info->card_info = card_info;

	pcm_instance_info->pcm_hw = instance_cfg->pcm_hw;

	if (instance_cfg->num_streams_pb) {
		pcm_instance_info->streams_pb =
				devm_kcalloc(&card_info->card->card_dev,
					     instance_cfg->num_streams_pb,
					     sizeof(struct xen_snd_front_pcm_stream_info),
					     GFP_KERNEL);
		if (!pcm_instance_info->streams_pb)
			return -ENOMEM;
	}

	if (instance_cfg->num_streams_cap) {
		pcm_instance_info->streams_cap =
				devm_kcalloc(&card_info->card->card_dev,
					     instance_cfg->num_streams_cap,
					     sizeof(struct xen_snd_front_pcm_stream_info),
					     GFP_KERNEL);
		if (!pcm_instance_info->streams_cap)
			return -ENOMEM;
	}

	pcm_instance_info->num_pcm_streams_pb =
			instance_cfg->num_streams_pb;
	pcm_instance_info->num_pcm_streams_cap =
			instance_cfg->num_streams_cap;

	for (i = 0; i < pcm_instance_info->num_pcm_streams_pb; i++) {
		pcm_instance_info->streams_pb[i].pcm_hw =
			instance_cfg->streams_pb[i].pcm_hw;
		pcm_instance_info->streams_pb[i].index =
			instance_cfg->streams_pb[i].index;
	}

	for (i = 0; i < pcm_instance_info->num_pcm_streams_cap; i++) {
		pcm_instance_info->streams_cap[i].pcm_hw =
			instance_cfg->streams_cap[i].pcm_hw;
		pcm_instance_info->streams_cap[i].index =
			instance_cfg->streams_cap[i].index;
	}

	ret = snd_pcm_new(card_info->card, instance_cfg->name,
			  instance_cfg->device_id,
			  instance_cfg->num_streams_pb,
			  instance_cfg->num_streams_cap,
			  &pcm);
	if (ret < 0)
		return ret;

	pcm->private_data = pcm_instance_info;
	pcm->info_flags = 0;
	/* we want to handle all PCM operations in non-atomic context */
	pcm->nonatomic = true;
	strncpy(pcm->name, "Virtual card PCM", sizeof(pcm->name));

	if (instance_cfg->num_streams_pb)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_drv_alsa_playback_ops);

	if (instance_cfg->num_streams_cap)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_drv_alsa_capture_ops);

	pcm_instance_info->pcm = pcm;
	return 0;
}

int xen_snd_front_alsa_init(struct xen_snd_front_info *front_info)
{
	struct device *dev = &front_info->xb_dev->dev;
	struct xen_front_cfg_card *cfg = &front_info->cfg;
	struct xen_snd_front_card_info *card_info;
	struct snd_card *card;
	int ret, i;

	dev_dbg(dev, "Creating virtual sound card\n");

	ret = snd_card_new(dev, 0, XENSND_DRIVER_NAME, THIS_MODULE,
			   sizeof(struct xen_snd_front_card_info), &card);
	if (ret < 0)
		return ret;

	card_info = card->private_data;
	card_info->front_info = front_info;
	front_info->card_info = card_info;
	card_info->card = card;
	card_info->pcm_instances =
			devm_kcalloc(dev, cfg->num_pcm_instances,
				     sizeof(struct xen_snd_front_pcm_instance_info),
				     GFP_KERNEL);
	if (!card_info->pcm_instances) {
		ret = -ENOMEM;
		goto fail;
	}

	card_info->num_pcm_instances = cfg->num_pcm_instances;
	card_info->pcm_hw = cfg->pcm_hw;

	for (i = 0; i < cfg->num_pcm_instances; i++) {
		ret = new_pcm_instance(card_info, &cfg->pcm_instances[i],
				       &card_info->pcm_instances[i]);
		if (ret < 0)
			goto fail;
	}

	strncpy(card->driver, XENSND_DRIVER_NAME, sizeof(card->driver));
	strncpy(card->shortname, cfg->name_short, sizeof(card->shortname));
	strncpy(card->longname, cfg->name_long, sizeof(card->longname));

	ret = snd_card_register(card);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	snd_card_free(card);
	return ret;
}

void xen_snd_front_alsa_fini(struct xen_snd_front_info *front_info)
{
	struct xen_snd_front_card_info *card_info;
	struct snd_card *card;

	card_info = front_info->card_info;
	if (!card_info)
		return;

	card = card_info->card;
	if (!card)
		return;

	dev_dbg(&front_info->xb_dev->dev, "Removing virtual sound card %d\n",
		card->number);
	snd_card_free(card);

	/* Card_info will be freed when destroying front_info->xb_dev->dev. */
	card_info->card = NULL;
}
