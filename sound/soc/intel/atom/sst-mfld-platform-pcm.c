// SPDX-License-Identifier: GPL-2.0-only
/*
 *  sst_mfld_platform.c - Intel MID Platform driver
 *
 *  Copyright (C) 2010-2014 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  Author: Harsha Priya <priya.harsha@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/platform_sst_audio.h>
#include "sst-mfld-platform.h"
#include "sst-atom-controls.h"

struct sst_device *sst;
static DEFINE_MUTEX(sst_lock);

int sst_register_dsp(struct sst_device *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;
	if (!try_module_get(dev->dev->driver->owner))
		return -ENODEV;
	mutex_lock(&sst_lock);
	if (sst) {
		dev_err(dev->dev, "we already have a device %s\n", sst->name);
		module_put(dev->dev->driver->owner);
		mutex_unlock(&sst_lock);
		return -EEXIST;
	}
	dev_dbg(dev->dev, "registering device %s\n", dev->name);
	sst = dev;
	mutex_unlock(&sst_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_register_dsp);

int sst_unregister_dsp(struct sst_device *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;
	if (dev != sst)
		return -EINVAL;

	mutex_lock(&sst_lock);

	if (!sst) {
		mutex_unlock(&sst_lock);
		return -EIO;
	}

	module_put(sst->dev->driver->owner);
	dev_dbg(dev->dev, "unreg %s\n", sst->name);
	sst = NULL;
	mutex_unlock(&sst_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_unregister_dsp);

static const struct snd_pcm_hardware sst_platform_pcm_hw = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_DOUBLE |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_RESUME |
			SNDRV_PCM_INFO_MMAP|
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_SYNC_START),
	.buffer_bytes_max = SST_MAX_BUFFER,
	.period_bytes_min = SST_MIN_PERIOD_BYTES,
	.period_bytes_max = SST_MAX_PERIOD_BYTES,
	.periods_min = SST_MIN_PERIODS,
	.periods_max = SST_MAX_PERIODS,
	.fifo_size = SST_FIFO_SIZE,
};

static struct sst_dev_stream_map dpcm_strm_map[] = {
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* Reserved, not in use */
	{MERR_DPCM_AUDIO, 0, SNDRV_PCM_STREAM_PLAYBACK, PIPE_MEDIA1_IN, SST_TASK_ID_MEDIA, 0},
	{MERR_DPCM_COMPR, 0, SNDRV_PCM_STREAM_PLAYBACK, PIPE_MEDIA0_IN, SST_TASK_ID_MEDIA, 0},
	{MERR_DPCM_AUDIO, 0, SNDRV_PCM_STREAM_CAPTURE, PIPE_PCM1_OUT, SST_TASK_ID_MEDIA, 0},
	{MERR_DPCM_DEEP_BUFFER, 0, SNDRV_PCM_STREAM_PLAYBACK, PIPE_MEDIA3_IN, SST_TASK_ID_MEDIA, 0},
};

static int sst_media_digital_mute(struct snd_soc_dai *dai, int mute, int stream)
{

	return sst_send_pipe_gains(dai, stream, mute);
}

/* helper functions */
void sst_set_stream_status(struct sst_runtime_stream *stream,
					int state)
{
	unsigned long flags;
	spin_lock_irqsave(&stream->status_lock, flags);
	stream->stream_status = state;
	spin_unlock_irqrestore(&stream->status_lock, flags);
}

static inline int sst_get_stream_status(struct sst_runtime_stream *stream)
{
	int state;
	unsigned long flags;

	spin_lock_irqsave(&stream->status_lock, flags);
	state = stream->stream_status;
	spin_unlock_irqrestore(&stream->status_lock, flags);
	return state;
}

static void sst_fill_alloc_params(struct snd_pcm_substream *substream,
				struct snd_sst_alloc_params_ext *alloc_param)
{
	unsigned int channels;
	snd_pcm_uframes_t period_size;
	ssize_t periodbytes;
	ssize_t buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	u32 buffer_addr = virt_to_phys(substream->runtime->dma_area);

	channels = substream->runtime->channels;
	period_size = substream->runtime->period_size;
	periodbytes = samples_to_bytes(substream->runtime, period_size);
	alloc_param->ring_buf_info[0].addr = buffer_addr;
	alloc_param->ring_buf_info[0].size = buffer_bytes;
	alloc_param->sg_count = 1;
	alloc_param->reserved = 0;
	alloc_param->frag_size = periodbytes * channels;

}
static void sst_fill_pcm_params(struct snd_pcm_substream *substream,
				struct snd_sst_stream_params *param)
{
	param->uc.pcm_params.num_chan = (u8) substream->runtime->channels;
	param->uc.pcm_params.pcm_wd_sz = substream->runtime->sample_bits;
	param->uc.pcm_params.sfreq = substream->runtime->rate;

	/* PCM stream via ALSA interface */
	param->uc.pcm_params.use_offload_path = 0;
	param->uc.pcm_params.reserved2 = 0;
	memset(param->uc.pcm_params.channel_map, 0, sizeof(u8));

}

static int sst_get_stream_mapping(int dev, int sdev, int dir,
	struct sst_dev_stream_map *map, int size)
{
	int i;

	if (map == NULL)
		return -EINVAL;


	/* index 0 is not used in stream map */
	for (i = 1; i < size; i++) {
		if ((map[i].dev_num == dev) && (map[i].direction == dir))
			return i;
	}
	return 0;
}

int sst_fill_stream_params(void *substream,
	const struct sst_data *ctx, struct snd_sst_params *str_params, bool is_compress)
{
	int map_size;
	int index;
	struct sst_dev_stream_map *map;
	struct snd_pcm_substream *pstream = NULL;
	struct snd_compr_stream *cstream = NULL;

	map = ctx->pdata->pdev_strm_map;
	map_size = ctx->pdata->strm_map_size;

	if (is_compress)
		cstream = (struct snd_compr_stream *)substream;
	else
		pstream = (struct snd_pcm_substream *)substream;

	str_params->stream_type = SST_STREAM_TYPE_MUSIC;

	/* For pcm streams */
	if (pstream) {
		index = sst_get_stream_mapping(pstream->pcm->device,
					  pstream->number, pstream->stream,
					  map, map_size);
		if (index <= 0)
			return -EINVAL;

		str_params->stream_id = index;
		str_params->device_type = map[index].device_id;
		str_params->task = map[index].task_id;

		str_params->ops = (u8)pstream->stream;
	}

	if (cstream) {
		index = sst_get_stream_mapping(cstream->device->device,
					       0, cstream->direction,
					       map, map_size);
		if (index <= 0)
			return -EINVAL;
		str_params->stream_id = index;
		str_params->device_type = map[index].device_id;
		str_params->task = map[index].task_id;

		str_params->ops = (u8)cstream->direction;
	}
	return 0;
}

static int sst_platform_alloc_stream(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sst_runtime_stream *stream =
			substream->runtime->private_data;
	struct snd_sst_stream_params param = {{{0,},},};
	struct snd_sst_params str_params = {0};
	struct snd_sst_alloc_params_ext alloc_params = {0};
	int ret_val = 0;
	struct sst_data *ctx = snd_soc_dai_get_drvdata(dai);

	/* set codec params and inform SST driver the same */
	sst_fill_pcm_params(substream, &param);
	sst_fill_alloc_params(substream, &alloc_params);
	str_params.sparams = param;
	str_params.aparams = alloc_params;
	str_params.codec = SST_CODEC_TYPE_PCM;

	/* fill the device type and stream id to pass to SST driver */
	ret_val = sst_fill_stream_params(substream, ctx, &str_params, false);
	if (ret_val < 0)
		return ret_val;

	stream->stream_info.str_id = str_params.stream_id;

	ret_val = stream->ops->open(sst->dev, &str_params);
	if (ret_val <= 0)
		return ret_val;


	return ret_val;
}

static void sst_period_elapsed(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct sst_runtime_stream *stream;
	int status;

	if (!substream || !substream->runtime)
		return;
	stream = substream->runtime->private_data;
	if (!stream)
		return;
	status = sst_get_stream_status(stream);
	if (status != SST_PLATFORM_RUNNING)
		return;
	snd_pcm_period_elapsed(substream);
}

static int sst_platform_init_stream(struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream =
			substream->runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret_val;

	dev_dbg(rtd->dev, "setting buffer ptr param\n");
	sst_set_stream_status(stream, SST_PLATFORM_INIT);
	stream->stream_info.period_elapsed = sst_period_elapsed;
	stream->stream_info.arg = substream;
	stream->stream_info.buffer_ptr = 0;
	stream->stream_info.sfreq = substream->runtime->rate;
	ret_val = stream->ops->stream_init(sst->dev, &stream->stream_info);
	if (ret_val)
		dev_err(rtd->dev, "control_set ret error %d\n", ret_val);
	return ret_val;

}

static int power_up_sst(struct sst_runtime_stream *stream)
{
	return stream->ops->power(sst->dev, true);
}

static void power_down_sst(struct sst_runtime_stream *stream)
{
	stream->ops->power(sst->dev, false);
}

static int sst_media_open(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	int ret_val = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sst_runtime_stream *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;
	spin_lock_init(&stream->status_lock);

	/* get the sst ops */
	mutex_lock(&sst_lock);
	if (!sst ||
	    !try_module_get(sst->dev->driver->owner)) {
		dev_err(dai->dev, "no device available to run\n");
		ret_val = -ENODEV;
		goto out_ops;
	}
	stream->ops = sst->ops;
	mutex_unlock(&sst_lock);

	stream->stream_info.str_id = 0;

	stream->stream_info.arg = substream;
	/* allocate memory for SST API set */
	runtime->private_data = stream;

	ret_val = power_up_sst(stream);
	if (ret_val < 0)
		goto out_power_up;

	/*
	 * Make sure the period to be multiple of 1ms to align the
	 * design of firmware. Apply same rule to buffer size to make
	 * sure alsa could always find a value for period size
	 * regardless the buffer size given by user space.
	 */
	snd_pcm_hw_constraint_step(substream->runtime, 0,
			   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 48);
	snd_pcm_hw_constraint_step(substream->runtime, 0,
			   SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 48);

	/* Make sure, that the period size is always even */
	snd_pcm_hw_constraint_step(substream->runtime, 0,
			   SNDRV_PCM_HW_PARAM_PERIODS, 2);

	return snd_pcm_hw_constraint_integer(runtime,
			 SNDRV_PCM_HW_PARAM_PERIODS);
out_ops:
	mutex_unlock(&sst_lock);
out_power_up:
	kfree(stream);
	return ret_val;
}

static void sst_media_close(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sst_runtime_stream *stream;
	int str_id;

	stream = substream->runtime->private_data;
	power_down_sst(stream);

	str_id = stream->stream_info.str_id;
	if (str_id)
		stream->ops->close(sst->dev, str_id);
	module_put(sst->dev->driver->owner);
	kfree(stream);
}

static int sst_media_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct sst_runtime_stream *stream;
	int ret_val, str_id;

	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	if (stream->stream_info.str_id) {
		ret_val = stream->ops->stream_drop(sst->dev, str_id);
		return ret_val;
	}

	ret_val = sst_platform_alloc_stream(substream, dai);
	if (ret_val <= 0)
		return ret_val;
	snprintf(substream->pcm->id, sizeof(substream->pcm->id),
			"%d", stream->stream_info.str_id);

	ret_val = sst_platform_init_stream(substream);
	if (ret_val)
		return ret_val;
	substream->runtime->hw.info = SNDRV_PCM_INFO_BLOCK_TRANSFER;
	return 0;
}

static int sst_enable_ssp(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	int ret = 0;

	if (!snd_soc_dai_active(dai)) {
		ret = sst_handle_vb_timer(dai, true);
		sst_fill_ssp_defaults(dai);
	}
	return ret;
}

static int sst_be_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	if (snd_soc_dai_active(dai) == 1)
		ret = send_ssp_cmd(dai, dai->name, 1);
	return ret;
}

static int sst_set_format(struct snd_soc_dai *dai, unsigned int fmt)
{
	int ret = 0;

	if (!snd_soc_dai_active(dai))
		return 0;

	ret = sst_fill_ssp_config(dai, fmt);
	if (ret < 0)
		dev_err(dai->dev, "sst_set_format failed..\n");

	return ret;
}

static int sst_platform_set_ssp_slot(struct snd_soc_dai *dai,
			unsigned int tx_mask, unsigned int rx_mask,
			int slots, int slot_width) {
	int ret = 0;

	if (!snd_soc_dai_active(dai))
		return ret;

	ret = sst_fill_ssp_slot(dai, tx_mask, rx_mask, slots, slot_width);
	if (ret < 0)
		dev_err(dai->dev, "sst_fill_ssp_slot failed..%d\n", ret);

	return ret;
}

static void sst_disable_ssp(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	if (!snd_soc_dai_active(dai)) {
		send_ssp_cmd(dai, dai->name, 0);
		sst_handle_vb_timer(dai, false);
	}
}

static const struct snd_soc_dai_ops sst_media_dai_ops = {
	.startup = sst_media_open,
	.shutdown = sst_media_close,
	.prepare = sst_media_prepare,
	.mute_stream = sst_media_digital_mute,
};

static const struct snd_soc_dai_ops sst_compr_dai_ops = {
	.mute_stream = sst_media_digital_mute,
};

static const struct snd_soc_dai_ops sst_be_dai_ops = {
	.startup = sst_enable_ssp,
	.hw_params = sst_be_hw_params,
	.set_fmt = sst_set_format,
	.set_tdm_slot = sst_platform_set_ssp_slot,
	.shutdown = sst_disable_ssp,
};

static struct snd_soc_dai_driver sst_platform_dai[] = {
{
	.name = "media-cpu-dai",
	.ops = &sst_media_dai_ops,
	.playback = {
		.stream_name = "Headset Playback",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Headset Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "deepbuffer-cpu-dai",
	.ops = &sst_media_dai_ops,
	.playback = {
		.stream_name = "Deepbuffer Playback",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "compress-cpu-dai",
	.compress_new = snd_soc_new_compress,
	.ops = &sst_compr_dai_ops,
	.playback = {
		.stream_name = "Compress Playback",
		.channels_min = 1,
	},
},
/* BE CPU  Dais */
{
	.name = "ssp0-port",
	.ops = &sst_be_dai_ops,
	.playback = {
		.stream_name = "ssp0 Tx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp0 Rx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "ssp1-port",
	.ops = &sst_be_dai_ops,
	.playback = {
		.stream_name = "ssp1 Tx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp1 Rx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "ssp2-port",
	.ops = &sst_be_dai_ops,
	.playback = {
		.stream_name = "ssp2 Tx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "ssp2 Rx",
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
};

static int sst_soc_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;

	if (substream->pcm->internal)
		return 0;

	runtime = substream->runtime;
	runtime->hw = sst_platform_pcm_hw;
	return 0;
}

static int sst_soc_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	int ret_val = 0, str_id;
	struct sst_runtime_stream *stream;
	int status;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	dev_dbg(rtd->dev, "%s called\n", __func__);
	if (substream->pcm->internal)
		return 0;
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_dbg(rtd->dev, "sst: Trigger Start\n");
		status = SST_PLATFORM_RUNNING;
		stream->stream_info.arg = substream;
		ret_val = stream->ops->stream_start(sst->dev, str_id);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(rtd->dev, "sst: in stop\n");
		status = SST_PLATFORM_DROPPED;
		ret_val = stream->ops->stream_drop(sst->dev, str_id);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		dev_dbg(rtd->dev, "sst: in pause\n");
		status = SST_PLATFORM_PAUSED;
		ret_val = stream->ops->stream_pause(sst->dev, str_id);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		dev_dbg(rtd->dev, "sst: in pause release\n");
		status = SST_PLATFORM_RUNNING;
		ret_val = stream->ops->stream_pause_release(sst->dev, str_id);
		break;
	default:
		return -EINVAL;
	}

	if (!ret_val)
		sst_set_stream_status(stream, status);

	return ret_val;
}


static snd_pcm_uframes_t sst_soc_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream;
	int ret_val, status;
	struct pcm_stream_info *str_info;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	stream = substream->runtime->private_data;
	status = sst_get_stream_status(stream);
	if (status == SST_PLATFORM_INIT)
		return 0;
	str_info = &stream->stream_info;
	ret_val = stream->ops->stream_read_tstamp(sst->dev, str_info);
	if (ret_val) {
		dev_err(rtd->dev, "sst: error code = %d\n", ret_val);
		return ret_val;
	}
	substream->runtime->delay = str_info->pcm_delay;
	return str_info->buffer_ptr;
}

static int sst_soc_pcm_new(struct snd_soc_component *component,
			   struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_pcm *pcm = rtd->pcm;

	if (dai->driver->playback.channels_min ||
			dai->driver->capture.channels_min) {
		snd_pcm_set_managed_buffer_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_DMA),
			SST_MIN_BUFFER, SST_MAX_BUFFER);
	}
	return 0;
}

static int sst_soc_probe(struct snd_soc_component *component)
{
	struct sst_data *drv = dev_get_drvdata(component->dev);

	drv->soc_card = component->card;
	return sst_dsp_init_v2_dpcm(component);
}

static void sst_soc_remove(struct snd_soc_component *component)
{
	struct sst_data *drv = dev_get_drvdata(component->dev);

	drv->soc_card = NULL;
}

static const struct snd_soc_component_driver sst_soc_platform_drv  = {
	.name		= DRV_NAME,
	.probe		= sst_soc_probe,
	.remove		= sst_soc_remove,
	.open		= sst_soc_open,
	.trigger	= sst_soc_trigger,
	.pointer	= sst_soc_pointer,
	.compress_ops	= &sst_platform_compress_ops,
	.pcm_construct	= sst_soc_pcm_new,
};

static int sst_platform_probe(struct platform_device *pdev)
{
	struct sst_data *drv;
	int ret;
	struct sst_platform_data *pdata;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (drv == NULL) {
		return -ENOMEM;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		return -ENOMEM;
	}

	pdata->pdev_strm_map = dpcm_strm_map;
	pdata->strm_map_size = ARRAY_SIZE(dpcm_strm_map);
	drv->pdata = pdata;
	drv->pdev = pdev;
	mutex_init(&drv->lock);
	dev_set_drvdata(&pdev->dev, drv);

	ret = devm_snd_soc_register_component(&pdev->dev, &sst_soc_platform_drv,
				sst_platform_dai, ARRAY_SIZE(sst_platform_dai));
	if (ret)
		dev_err(&pdev->dev, "registering cpu dais failed\n");

	return ret;
}

static int sst_platform_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "sst_platform_remove success\n");
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int sst_soc_prepare(struct device *dev)
{
	struct sst_data *drv = dev_get_drvdata(dev);
	struct snd_soc_pcm_runtime *rtd;

	if (!drv->soc_card)
		return 0;

	/* suspend all pcms first */
	snd_soc_suspend(drv->soc_card->dev);
	snd_soc_poweroff(drv->soc_card->dev);

	/* set the SSPs to idle */
	for_each_card_rtds(drv->soc_card, rtd) {
		struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);

		if (snd_soc_dai_active(dai)) {
			send_ssp_cmd(dai, dai->name, 0);
			sst_handle_vb_timer(dai, false);
		}
	}

	return 0;
}

static void sst_soc_complete(struct device *dev)
{
	struct sst_data *drv = dev_get_drvdata(dev);
	struct snd_soc_pcm_runtime *rtd;

	if (!drv->soc_card)
		return;

	/* restart SSPs */
	for_each_card_rtds(drv->soc_card, rtd) {
		struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);

		if (snd_soc_dai_active(dai)) {
			sst_handle_vb_timer(dai, true);
			send_ssp_cmd(dai, dai->name, 1);
		}
	}
	snd_soc_resume(drv->soc_card->dev);
}

#else

#define sst_soc_prepare NULL
#define sst_soc_complete NULL

#endif


static const struct dev_pm_ops sst_platform_pm = {
	.prepare	= sst_soc_prepare,
	.complete	= sst_soc_complete,
};

static struct platform_driver sst_platform_driver = {
	.driver		= {
		.name		= "sst-mfld-platform",
		.pm             = &sst_platform_pm,
	},
	.probe		= sst_platform_probe,
	.remove		= sst_platform_remove,
};

module_platform_driver(sst_platform_driver);

MODULE_DESCRIPTION("ASoC Intel(R) MID Platform driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sst-atom-hifi2-platform");
MODULE_ALIAS("platform:sst-mfld-platform");
