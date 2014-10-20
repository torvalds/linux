/*
 *  sst_drv_interface.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-14 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/math64.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../sst-dsp.h"



#define NUM_CODEC 2
#define MIN_FRAGMENT 2
#define MAX_FRAGMENT 4
#define MIN_FRAGMENT_SIZE (50 * 1024)
#define MAX_FRAGMENT_SIZE (1024 * 1024)
#define SST_GET_BYTES_PER_SAMPLE(pcm_wd_sz)  (((pcm_wd_sz + 15) >> 4) << 1)

int free_stream_context(struct intel_sst_drv *ctx, unsigned int str_id)
{
	struct stream_info *stream;
	int ret = 0;

	stream = get_stream_info(ctx, str_id);
	if (stream) {
		/* str_id is valid, so stream is alloacted */
		ret = sst_free_stream(ctx, str_id);
		if (ret)
			sst_clean_stream(&ctx->streams[str_id]);
		return ret;
	} else {
		dev_err(ctx->dev, "we tried to free stream context %d which was freed!!!\n", str_id);
	}
	return ret;
}

int sst_get_stream_allocated(struct intel_sst_drv *ctx,
	struct snd_sst_params *str_param,
	struct snd_sst_lib_download **lib_dnld)
{
	int retval;

	retval = ctx->ops->alloc_stream(ctx, str_param);
	if (retval > 0)
		dev_dbg(ctx->dev, "Stream allocated %d\n", retval);
	return retval;

}

/*
 * sst_get_sfreq - this function returns the frequency of the stream
 *
 * @str_param : stream params
 */
int sst_get_sfreq(struct snd_sst_params *str_param)
{
	switch (str_param->codec) {
	case SST_CODEC_TYPE_PCM:
		return str_param->sparams.uc.pcm_params.sfreq;
	case SST_CODEC_TYPE_AAC:
		return str_param->sparams.uc.aac_params.externalsr;
	case SST_CODEC_TYPE_MP3:
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * sst_get_num_channel - get number of channels for the stream
 *
 * @str_param : stream params
 */
int sst_get_num_channel(struct snd_sst_params *str_param)
{
	switch (str_param->codec) {
	case SST_CODEC_TYPE_PCM:
		return str_param->sparams.uc.pcm_params.num_chan;
	case SST_CODEC_TYPE_MP3:
		return str_param->sparams.uc.mp3_params.num_chan;
	case SST_CODEC_TYPE_AAC:
		return str_param->sparams.uc.aac_params.num_chan;
	default:
		return -EINVAL;
	}
}

/*
 * sst_get_stream - this function prepares for stream allocation
 *
 * @str_param : stream param
 */
int sst_get_stream(struct intel_sst_drv *ctx,
			struct snd_sst_params *str_param)
{
	int retval;
	struct stream_info *str_info;

	/* stream is not allocated, we are allocating */
	retval = ctx->ops->alloc_stream(ctx, str_param);
	if (retval <= 0) {
		return -EIO;
	}
	/* store sampling freq */
	str_info = &ctx->streams[retval];
	str_info->sfreq = sst_get_sfreq(str_param);

	return retval;
}

static int sst_power_control(struct device *dev, bool state)
{
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	dev_dbg(ctx->dev, "state:%d", state);
	if (state == true)
		return pm_runtime_get_sync(dev);
	else
		return sst_pm_runtime_put(ctx);
}

/*
 * sst_open_pcm_stream - Open PCM interface
 *
 * @str_param: parameters of pcm stream
 *
 * This function is called by MID sound card driver to open
 * a new pcm interface
 */
static int sst_open_pcm_stream(struct device *dev,
		struct snd_sst_params *str_param)
{
	int retval;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (!str_param)
		return -EINVAL;

	retval = pm_runtime_get_sync(ctx->dev);
	if (retval < 0)
		return retval;
	retval = sst_get_stream(ctx, str_param);
	if (retval > 0) {
		ctx->stream_cnt++;
	} else {
		dev_err(ctx->dev, "sst_get_stream returned err %d\n", retval);
		sst_pm_runtime_put(ctx);
	}

	return retval;
}

/*
 * sst_close_pcm_stream - Close PCM interface
 *
 * @str_id: stream id to be closed
 *
 * This function is called by MID sound card driver to close
 * an existing pcm interface
 */
static int sst_close_pcm_stream(struct device *dev, unsigned int str_id)
{
	struct stream_info *stream;
	int retval = 0;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	stream = get_stream_info(ctx, str_id);
	if (!stream) {
		dev_err(ctx->dev, "stream info is NULL for str %d!!!\n", str_id);
		return -EINVAL;
	}

	if (stream->status == STREAM_RESET) {
		/* silently fail here as we have cleaned the stream earlier */
		dev_dbg(ctx->dev, "stream in reset state...\n");

		retval = 0;
		goto put;
	}

	retval = free_stream_context(ctx, str_id);
put:
	stream->pcm_substream = NULL;
	stream->status = STREAM_UN_INIT;
	stream->period_elapsed = NULL;
	ctx->stream_cnt--;

	sst_pm_runtime_put(ctx);

	dev_dbg(ctx->dev, "Exit\n");
	return 0;
}

static inline int sst_calc_tstamp(struct intel_sst_drv *ctx,
		struct pcm_stream_info *info,
		struct snd_pcm_substream *substream,
		struct snd_sst_tstamp *fw_tstamp)
{
	size_t delay_bytes, delay_frames;
	size_t buffer_sz;
	u32 pointer_bytes, pointer_samples;

	dev_dbg(ctx->dev, "mrfld ring_buffer_counter %llu in bytes\n",
			fw_tstamp->ring_buffer_counter);
	dev_dbg(ctx->dev, "mrfld hardware_counter %llu in bytes\n",
			 fw_tstamp->hardware_counter);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		delay_bytes = (size_t) (fw_tstamp->ring_buffer_counter -
					fw_tstamp->hardware_counter);
	else
		delay_bytes = (size_t) (fw_tstamp->hardware_counter -
					fw_tstamp->ring_buffer_counter);
	delay_frames = bytes_to_frames(substream->runtime, delay_bytes);
	buffer_sz = snd_pcm_lib_buffer_bytes(substream);
	div_u64_rem(fw_tstamp->ring_buffer_counter, buffer_sz, &pointer_bytes);
	pointer_samples = bytes_to_samples(substream->runtime, pointer_bytes);

	dev_dbg(ctx->dev, "pcm delay %zu in bytes\n", delay_bytes);

	info->buffer_ptr = pointer_samples / substream->runtime->channels;

	info->pcm_delay = delay_frames / substream->runtime->channels;
	dev_dbg(ctx->dev, "buffer ptr %llu pcm_delay rep: %llu\n",
			info->buffer_ptr, info->pcm_delay);
	return 0;
}

static int sst_read_timestamp(struct device *dev, struct pcm_stream_info *info)
{
	struct stream_info *stream;
	struct snd_pcm_substream *substream;
	struct snd_sst_tstamp fw_tstamp;
	unsigned int str_id;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	str_id = info->str_id;
	stream = get_stream_info(ctx, str_id);
	if (!stream)
		return -EINVAL;

	if (!stream->pcm_substream)
		return -EINVAL;
	substream = stream->pcm_substream;

	memcpy_fromio(&fw_tstamp,
		((void *)(ctx->mailbox + ctx->tstamp)
			+ (str_id * sizeof(fw_tstamp))),
		sizeof(fw_tstamp));
	return sst_calc_tstamp(ctx, info, substream, &fw_tstamp);
}

static int sst_stream_start(struct device *dev, int str_id)
{
	struct stream_info *str_info;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (ctx->sst_state != SST_FW_RUNNING)
		return 0;
	str_info = get_stream_info(ctx, str_id);
	if (!str_info)
		return -EINVAL;
	str_info->prev = str_info->status;
	str_info->status = STREAM_RUNNING;
	sst_start_stream(ctx, str_id);

	return 0;
}

static int sst_stream_drop(struct device *dev, int str_id)
{
	struct stream_info *str_info;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (ctx->sst_state != SST_FW_RUNNING)
		return 0;

	str_info = get_stream_info(ctx, str_id);
	if (!str_info)
		return -EINVAL;
	str_info->prev = STREAM_UN_INIT;
	str_info->status = STREAM_INIT;
	return sst_drop_stream(ctx, str_id);
}

static int sst_stream_init(struct device *dev, struct pcm_stream_info *str_info)
{
	int str_id = 0;
	struct stream_info *stream;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	str_id = str_info->str_id;

	if (ctx->sst_state != SST_FW_RUNNING)
		return 0;

	stream = get_stream_info(ctx, str_id);
	if (!stream)
		return -EINVAL;

	dev_dbg(ctx->dev, "setting the period ptrs\n");
	stream->pcm_substream = str_info->arg;
	stream->period_elapsed = str_info->period_elapsed;
	stream->sfreq = str_info->sfreq;
	stream->prev = stream->status;
	stream->status = STREAM_INIT;
	dev_dbg(ctx->dev,
		"pcm_substream %p, period_elapsed %p, sfreq %d, status %d\n",
		stream->pcm_substream, stream->period_elapsed,
		stream->sfreq, stream->status);

	return 0;
}

/*
 * sst_set_byte_stream - Set generic params
 *
 * @cmd: control cmd to be set
 * @arg: command argument
 *
 * This function is called by MID sound card driver to configure
 * SST runtime params.
 */
static int sst_send_byte_stream(struct device *dev,
		struct snd_sst_bytes_v2 *bytes)
{
	int ret_val = 0;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (NULL == bytes)
		return -EINVAL;
	ret_val = pm_runtime_get_sync(ctx->dev);
	if (ret_val < 0)
		return ret_val;

	ret_val = sst_send_byte_stream_mrfld(ctx, bytes);
	sst_pm_runtime_put(ctx);

	return ret_val;
}

static struct sst_ops pcm_ops = {
	.open = sst_open_pcm_stream,
	.stream_init = sst_stream_init,
	.stream_start = sst_stream_start,
	.stream_drop = sst_stream_drop,
	.stream_read_tstamp = sst_read_timestamp,
	.send_byte_stream = sst_send_byte_stream,
	.close = sst_close_pcm_stream,
	.power = sst_power_control,
};

static struct sst_device sst_dsp_device = {
	.name = "Intel(R) SST LPE",
	.dev = NULL,
	.ops = &pcm_ops,
};

/*
 * sst_register - function to register DSP
 *
 * This functions registers DSP with the platform driver
 */
int sst_register(struct device *dev)
{
	int ret_val;

	sst_dsp_device.dev = dev;
	ret_val = sst_register_dsp(&sst_dsp_device);
	if (ret_val)
		dev_err(dev, "Unable to register DSP with platform driver\n");

	return ret_val;
}

int sst_unregister(struct device *dev)
{
	return sst_unregister_dsp(&sst_dsp_device);
}
