/*
 *  sst_platform.c - Intel MID Platform driver
 *
 *  Copyright (C) 2010-2013 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  Author: Harsha Priya <priya.harsha@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *
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
#include "sst_platform.h"

static struct sst_device *sst;
static DEFINE_MUTEX(sst_lock);

int sst_register_dsp(struct sst_device *dev)
{
	BUG_ON(!dev);
	if (!try_module_get(dev->dev->driver->owner))
		return -ENODEV;
	mutex_lock(&sst_lock);
	if (sst) {
		pr_err("we already have a device %s\n", sst->name);
		module_put(dev->dev->driver->owner);
		mutex_unlock(&sst_lock);
		return -EEXIST;
	}
	pr_debug("registering device %s\n", dev->name);
	sst = dev;
	mutex_unlock(&sst_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_register_dsp);

int sst_unregister_dsp(struct sst_device *dev)
{
	BUG_ON(!dev);
	if (dev != sst)
		return -EINVAL;

	mutex_lock(&sst_lock);

	if (!sst) {
		mutex_unlock(&sst_lock);
		return -EIO;
	}

	module_put(sst->dev->driver->owner);
	pr_debug("unreg %s\n", sst->name);
	sst = NULL;
	mutex_unlock(&sst_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_unregister_dsp);

static struct snd_pcm_hardware sst_platform_pcm_hw = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_DOUBLE |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_RESUME |
			SNDRV_PCM_INFO_MMAP|
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_SYNC_START),
	.formats = (SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_U16 |
			SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_U24 |
			SNDRV_PCM_FMTBIT_S32 | SNDRV_PCM_FMTBIT_U32),
	.rates = (SNDRV_PCM_RATE_8000|
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000),
	.rate_min = SST_MIN_RATE,
	.rate_max = SST_MAX_RATE,
	.channels_min =	SST_MIN_CHANNEL,
	.channels_max =	SST_MAX_CHANNEL,
	.buffer_bytes_max = SST_MAX_BUFFER,
	.period_bytes_min = SST_MIN_PERIOD_BYTES,
	.period_bytes_max = SST_MAX_PERIOD_BYTES,
	.periods_min = SST_MIN_PERIODS,
	.periods_max = SST_MAX_PERIODS,
	.fifo_size = SST_FIFO_SIZE,
};

/* MFLD - MSIC */
static struct snd_soc_dai_driver sst_platform_dai[] = {
{
	.name = "Headset-cpu-dai",
	.id = 0,
	.playback = {
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 5,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "Speaker-cpu-dai",
	.id = 1,
	.playback = {
		.channels_min = SST_MONO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "Vibra1-cpu-dai",
	.id = 2,
	.playback = {
		.channels_min = SST_MONO,
		.channels_max = SST_MONO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "Vibra2-cpu-dai",
	.id = 3,
	.playback = {
		.channels_min = SST_MONO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
},
{
	.name = "Compress-cpu-dai",
	.compress_dai = 1,
	.playback = {
		.channels_min = SST_STEREO,
		.channels_max = SST_STEREO,
		.rates = SNDRV_PCM_RATE_44100|SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
};

static const struct snd_soc_component_driver sst_component = {
	.name		= "sst",
};

/* helper functions */
static inline void sst_set_stream_status(struct sst_runtime_stream *stream,
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

static void sst_fill_pcm_params(struct snd_pcm_substream *substream,
				struct sst_pcm_params *param)
{

	param->codec = SST_CODEC_TYPE_PCM;
	param->num_chan = (u8) substream->runtime->channels;
	param->pcm_wd_sz = substream->runtime->sample_bits;
	param->reserved = 0;
	param->sfreq = substream->runtime->rate;
	param->ring_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	param->period_count = substream->runtime->period_size;
	param->ring_buffer_addr = virt_to_phys(substream->dma_buffer.area);
	pr_debug("period_cnt = %d\n", param->period_count);
	pr_debug("sfreq= %d, wd_sz = %d\n", param->sfreq, param->pcm_wd_sz);
}

static int sst_platform_alloc_stream(struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream =
			substream->runtime->private_data;
	struct sst_pcm_params param = {0};
	struct sst_stream_params str_params = {0};
	int ret_val;

	/* set codec params and inform SST driver the same */
	sst_fill_pcm_params(substream, &param);
	substream->runtime->dma_area = substream->dma_buffer.area;
	str_params.sparams = param;
	str_params.codec =  param.codec;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		str_params.ops = STREAM_OPS_PLAYBACK;
		str_params.device_type = substream->pcm->device + 1;
		pr_debug("Playbck stream,Device %d\n",
					substream->pcm->device);
	} else {
		str_params.ops = STREAM_OPS_CAPTURE;
		str_params.device_type = SND_SST_DEVICE_CAPTURE;
		pr_debug("Capture stream,Device %d\n",
					substream->pcm->device);
	}
	ret_val = stream->ops->open(&str_params);
	pr_debug("SST_SND_PLAY/CAPTURE ret_val = %x\n", ret_val);
	if (ret_val < 0)
		return ret_val;

	stream->stream_info.str_id = ret_val;
	pr_debug("str id :  %d\n", stream->stream_info.str_id);
	return ret_val;
}

static void sst_period_elapsed(void *mad_substream)
{
	struct snd_pcm_substream *substream = mad_substream;
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
	int ret_val;

	pr_debug("setting buffer ptr param\n");
	sst_set_stream_status(stream, SST_PLATFORM_INIT);
	stream->stream_info.period_elapsed = sst_period_elapsed;
	stream->stream_info.mad_substream = substream;
	stream->stream_info.buffer_ptr = 0;
	stream->stream_info.sfreq = substream->runtime->rate;
	ret_val = stream->ops->device_control(
			SST_SND_STREAM_INIT, &stream->stream_info);
	if (ret_val)
		pr_err("control_set ret error %d\n", ret_val);
	return ret_val;

}
/* end -- helper functions */

static int sst_platform_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sst_runtime_stream *stream;
	int ret_val;

	pr_debug("sst_platform_open called\n");

	snd_soc_set_runtime_hwparams(substream, &sst_platform_pcm_hw);
	ret_val = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret_val < 0)
		return ret_val;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;
	spin_lock_init(&stream->status_lock);

	/* get the sst ops */
	mutex_lock(&sst_lock);
	if (!sst) {
		pr_err("no device available to run\n");
		mutex_unlock(&sst_lock);
		kfree(stream);
		return -ENODEV;
	}
	if (!try_module_get(sst->dev->driver->owner)) {
		mutex_unlock(&sst_lock);
		kfree(stream);
		return -ENODEV;
	}
	stream->ops = sst->ops;
	mutex_unlock(&sst_lock);

	stream->stream_info.str_id = 0;
	sst_set_stream_status(stream, SST_PLATFORM_INIT);
	stream->stream_info.mad_substream = substream;
	/* allocate memory for SST API set */
	runtime->private_data = stream;

	return 0;
}

static int sst_platform_close(struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream;
	int ret_val = 0, str_id;

	pr_debug("sst_platform_close called\n");
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	if (str_id)
		ret_val = stream->ops->close(str_id);
	module_put(sst->dev->driver->owner);
	kfree(stream);
	return ret_val;
}

static int sst_platform_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream;
	int ret_val = 0, str_id;

	pr_debug("sst_platform_pcm_prepare called\n");
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	if (stream->stream_info.str_id) {
		ret_val = stream->ops->device_control(
				SST_SND_DROP, &str_id);
		return ret_val;
	}

	ret_val = sst_platform_alloc_stream(substream);
	if (ret_val < 0)
		return ret_val;
	snprintf(substream->pcm->id, sizeof(substream->pcm->id),
			"%d", stream->stream_info.str_id);

	ret_val = sst_platform_init_stream(substream);
	if (ret_val)
		return ret_val;
	substream->runtime->hw.info = SNDRV_PCM_INFO_BLOCK_TRANSFER;
	return ret_val;
}

static int sst_platform_pcm_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	int ret_val = 0, str_id;
	struct sst_runtime_stream *stream;
	int str_cmd, status;

	pr_debug("sst_platform_pcm_trigger called\n");
	stream = substream->runtime->private_data;
	str_id = stream->stream_info.str_id;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pr_debug("sst: Trigger Start\n");
		str_cmd = SST_SND_START;
		status = SST_PLATFORM_RUNNING;
		stream->stream_info.mad_substream = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pr_debug("sst: in stop\n");
		str_cmd = SST_SND_DROP;
		status = SST_PLATFORM_DROPPED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pr_debug("sst: in pause\n");
		str_cmd = SST_SND_PAUSE;
		status = SST_PLATFORM_PAUSED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pr_debug("sst: in pause release\n");
		str_cmd = SST_SND_RESUME;
		status = SST_PLATFORM_RUNNING;
		break;
	default:
		return -EINVAL;
	}
	ret_val = stream->ops->device_control(str_cmd, &str_id);
	if (!ret_val)
		sst_set_stream_status(stream, status);

	return ret_val;
}


static snd_pcm_uframes_t sst_platform_pcm_pointer
			(struct snd_pcm_substream *substream)
{
	struct sst_runtime_stream *stream;
	int ret_val, status;
	struct pcm_stream_info *str_info;

	stream = substream->runtime->private_data;
	status = sst_get_stream_status(stream);
	if (status == SST_PLATFORM_INIT)
		return 0;
	str_info = &stream->stream_info;
	ret_val = stream->ops->device_control(
				SST_SND_BUFFER_POINTER, str_info);
	if (ret_val) {
		pr_err("sst: error code = %d\n", ret_val);
		return ret_val;
	}
	return stream->stream_info.buffer_ptr;
}

static int sst_platform_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	memset(substream->runtime->dma_area, 0, params_buffer_bytes(params));

	return 0;
}

static int sst_platform_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_ops sst_platform_ops = {
	.open = sst_platform_open,
	.close = sst_platform_close,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = sst_platform_pcm_prepare,
	.trigger = sst_platform_pcm_trigger,
	.pointer = sst_platform_pcm_pointer,
	.hw_params = sst_platform_pcm_hw_params,
	.hw_free = sst_platform_pcm_hw_free,
};

static void sst_pcm_free(struct snd_pcm *pcm)
{
	pr_debug("sst_pcm_free called\n");
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int sst_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	int retval = 0;

	pr_debug("sst_pcm_new called\n");
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		retval =  snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL),
			SST_MIN_BUFFER, SST_MAX_BUFFER);
		if (retval) {
			pr_err("dma buffer allocationf fail\n");
			return retval;
		}
	}
	return retval;
}

/* compress stream operations */
static void sst_compr_fragment_elapsed(void *arg)
{
	struct snd_compr_stream *cstream = (struct snd_compr_stream *)arg;

	pr_debug("fragment elapsed by driver\n");
	if (cstream)
		snd_compr_fragment_elapsed(cstream);
}

static int sst_platform_compr_open(struct snd_compr_stream *cstream)
{

	int ret_val = 0;
	struct snd_compr_runtime *runtime = cstream->runtime;
	struct sst_runtime_stream *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	spin_lock_init(&stream->status_lock);

	/* get the sst ops */
	if (!sst || !try_module_get(sst->dev->driver->owner)) {
		pr_err("no device available to run\n");
		ret_val = -ENODEV;
		goto out_ops;
	}
	stream->compr_ops = sst->compr_ops;

	stream->id = 0;
	sst_set_stream_status(stream, SST_PLATFORM_INIT);
	runtime->private_data = stream;
	return 0;
out_ops:
	kfree(stream);
	return ret_val;
}

static int sst_platform_compr_free(struct snd_compr_stream *cstream)
{
	struct sst_runtime_stream *stream;
	int ret_val = 0, str_id;

	stream = cstream->runtime->private_data;
	/*need to check*/
	str_id = stream->id;
	if (str_id)
		ret_val = stream->compr_ops->close(str_id);
	module_put(sst->dev->driver->owner);
	kfree(stream);
	pr_debug("%s: %d\n", __func__, ret_val);
	return 0;
}

static int sst_platform_compr_set_params(struct snd_compr_stream *cstream,
					struct snd_compr_params *params)
{
	struct sst_runtime_stream *stream;
	int retval;
	struct snd_sst_params str_params;
	struct sst_compress_cb cb;

	stream = cstream->runtime->private_data;
	/* construct fw structure for this*/
	memset(&str_params, 0, sizeof(str_params));

	str_params.ops = STREAM_OPS_PLAYBACK;
	str_params.stream_type = SST_STREAM_TYPE_MUSIC;
	str_params.device_type = SND_SST_DEVICE_COMPRESS;

	switch (params->codec.id) {
	case SND_AUDIOCODEC_MP3: {
		str_params.codec = SST_CODEC_TYPE_MP3;
		str_params.sparams.uc.mp3_params.codec = SST_CODEC_TYPE_MP3;
		str_params.sparams.uc.mp3_params.num_chan = params->codec.ch_in;
		str_params.sparams.uc.mp3_params.pcm_wd_sz = 16;
		break;
	}

	case SND_AUDIOCODEC_AAC: {
		str_params.codec = SST_CODEC_TYPE_AAC;
		str_params.sparams.uc.aac_params.codec = SST_CODEC_TYPE_AAC;
		str_params.sparams.uc.aac_params.num_chan = params->codec.ch_in;
		str_params.sparams.uc.aac_params.pcm_wd_sz = 16;
		if (params->codec.format == SND_AUDIOSTREAMFORMAT_MP4ADTS)
			str_params.sparams.uc.aac_params.bs_format =
							AAC_BIT_STREAM_ADTS;
		else if (params->codec.format == SND_AUDIOSTREAMFORMAT_RAW)
			str_params.sparams.uc.aac_params.bs_format =
							AAC_BIT_STREAM_RAW;
		else {
			pr_err("Undefined format%d\n", params->codec.format);
			return -EINVAL;
		}
		str_params.sparams.uc.aac_params.externalsr =
						params->codec.sample_rate;
		break;
	}

	default:
		pr_err("codec not supported, id =%d\n", params->codec.id);
		return -EINVAL;
	}

	str_params.aparams.ring_buf_info[0].addr  =
					virt_to_phys(cstream->runtime->buffer);
	str_params.aparams.ring_buf_info[0].size =
					cstream->runtime->buffer_size;
	str_params.aparams.sg_count = 1;
	str_params.aparams.frag_size = cstream->runtime->fragment_size;

	cb.param = cstream;
	cb.compr_cb = sst_compr_fragment_elapsed;

	retval = stream->compr_ops->open(&str_params, &cb);
	if (retval < 0) {
		pr_err("stream allocation failed %d\n", retval);
		return retval;
	}

	stream->id = retval;
	return 0;
}

static int sst_platform_compr_trigger(struct snd_compr_stream *cstream, int cmd)
{
	struct sst_runtime_stream *stream =
		cstream->runtime->private_data;

	return stream->compr_ops->control(cmd, stream->id);
}

static int sst_platform_compr_pointer(struct snd_compr_stream *cstream,
					struct snd_compr_tstamp *tstamp)
{
	struct sst_runtime_stream *stream;

	stream  = cstream->runtime->private_data;
	stream->compr_ops->tstamp(stream->id, tstamp);
	tstamp->byte_offset = tstamp->copied_total %
				 (u32)cstream->runtime->buffer_size;
	pr_debug("calc bytes offset/copied bytes as %d\n", tstamp->byte_offset);
	return 0;
}

static int sst_platform_compr_ack(struct snd_compr_stream *cstream,
					size_t bytes)
{
	struct sst_runtime_stream *stream;

	stream  = cstream->runtime->private_data;
	stream->compr_ops->ack(stream->id, (unsigned long)bytes);
	stream->bytes_written += bytes;

	return 0;
}

static int sst_platform_compr_get_caps(struct snd_compr_stream *cstream,
					struct snd_compr_caps *caps)
{
	struct sst_runtime_stream *stream =
		cstream->runtime->private_data;

	return stream->compr_ops->get_caps(caps);
}

static int sst_platform_compr_get_codec_caps(struct snd_compr_stream *cstream,
					struct snd_compr_codec_caps *codec)
{
	struct sst_runtime_stream *stream =
		cstream->runtime->private_data;

	return stream->compr_ops->get_codec_caps(codec);
}

static int sst_platform_compr_set_metadata(struct snd_compr_stream *cstream,
					struct snd_compr_metadata *metadata)
{
	struct sst_runtime_stream *stream  =
		 cstream->runtime->private_data;

	return stream->compr_ops->set_metadata(stream->id, metadata);
}

static struct snd_compr_ops sst_platform_compr_ops = {

	.open = sst_platform_compr_open,
	.free = sst_platform_compr_free,
	.set_params = sst_platform_compr_set_params,
	.set_metadata = sst_platform_compr_set_metadata,
	.trigger = sst_platform_compr_trigger,
	.pointer = sst_platform_compr_pointer,
	.ack = sst_platform_compr_ack,
	.get_caps = sst_platform_compr_get_caps,
	.get_codec_caps = sst_platform_compr_get_codec_caps,
};

static struct snd_soc_platform_driver sst_soc_platform_drv = {
	.ops		= &sst_platform_ops,
	.compr_ops	= &sst_platform_compr_ops,
	.pcm_new	= sst_pcm_new,
	.pcm_free	= sst_pcm_free,
};

static int sst_platform_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("sst_platform_probe called\n");
	sst = NULL;
	ret = snd_soc_register_platform(&pdev->dev, &sst_soc_platform_drv);
	if (ret) {
		pr_err("registering soc platform failed\n");
		return ret;
	}

	ret = snd_soc_register_component(&pdev->dev, &sst_component,
				sst_platform_dai, ARRAY_SIZE(sst_platform_dai));
	if (ret) {
		pr_err("registering cpu dais failed\n");
		snd_soc_unregister_platform(&pdev->dev);
	}
	return ret;
}

static int sst_platform_remove(struct platform_device *pdev)
{

	snd_soc_unregister_component(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);
	pr_debug("sst_platform_remove success\n");
	return 0;
}

static struct platform_driver sst_platform_driver = {
	.driver		= {
		.name		= "sst-platform",
		.owner		= THIS_MODULE,
	},
	.probe		= sst_platform_probe,
	.remove		= sst_platform_remove,
};

module_platform_driver(sst_platform_driver);

MODULE_DESCRIPTION("ASoC Intel(R) MID Platform driver");
MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sst-platform");
