/*
 * Intel Baytrail SST PCM Support
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "sst-baytrail-ipc.h"
#include "sst-dsp-priv.h"
#include "sst-dsp.h"

#define BYT_PCM_COUNT		2

static const struct snd_pcm_hardware sst_byt_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FORMAT_S24_LE,
	.period_bytes_min	= 384,
	.period_bytes_max	= 48000,
	.periods_min		= 2,
	.periods_max		= 250,
	.buffer_bytes_max	= 96000,
};

/* private data for each PCM DSP stream */
struct sst_byt_pcm_data {
	struct sst_byt_stream *stream;
	struct snd_pcm_substream *substream;
	struct mutex mutex;
};

/* private data for the driver */
struct sst_byt_priv_data {
	/* runtime DSP */
	struct sst_byt *byt;

	/* DAI data */
	struct sst_byt_pcm_data pcm[BYT_PCM_COUNT];
};

/* this may get called several times by oss emulation */
static int sst_byt_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sst_byt_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct sst_byt_pcm_data *pcm_data = snd_soc_pcm_get_drvdata(rtd);
	struct sst_byt *byt = pdata->byt;
	u32 rate, bits;
	u8 channels;
	int ret, playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	dev_dbg(rtd->dev, "PCM: hw_params, pcm_data %p\n", pcm_data);

	ret = sst_byt_stream_type(byt, pcm_data->stream,
				  1, 1, !playback);
	if (ret < 0) {
		dev_err(rtd->dev, "failed to set stream format %d\n", ret);
		return ret;
	}

	rate = params_rate(params);
	ret = sst_byt_stream_set_rate(byt, pcm_data->stream, rate);
	if (ret < 0) {
		dev_err(rtd->dev, "could not set rate %d\n", rate);
		return ret;
	}

	bits = snd_pcm_format_width(params_format(params));
	ret = sst_byt_stream_set_bits(byt, pcm_data->stream, bits);
	if (ret < 0) {
		dev_err(rtd->dev, "could not set formats %d\n",
			params_rate(params));
		return ret;
	}

	channels = (u8)(params_channels(params) & 0xF);
	ret = sst_byt_stream_set_channels(byt, pcm_data->stream, channels);
	if (ret < 0) {
		dev_err(rtd->dev, "could not set channels %d\n",
			params_rate(params));
		return ret;
	}

	snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));

	ret = sst_byt_stream_buffer(byt, pcm_data->stream,
				    substream->dma_buffer.addr,
				    params_buffer_bytes(params));
	if (ret < 0) {
		dev_err(rtd->dev, "PCM: failed to set DMA buffer %d\n", ret);
		return ret;
	}

	ret = sst_byt_stream_commit(byt, pcm_data->stream);
	if (ret < 0) {
		dev_err(rtd->dev, "PCM: failed stream commit %d\n", ret);
		return ret;
	}

	return 0;
}

static int sst_byt_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	dev_dbg(rtd->dev, "PCM: hw_free\n");
	snd_pcm_lib_free_pages(substream);

	return 0;
}

static int sst_byt_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sst_byt_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct sst_byt_pcm_data *pcm_data = snd_soc_pcm_get_drvdata(rtd);
	struct sst_byt *byt = pdata->byt;

	dev_dbg(rtd->dev, "PCM: trigger %d\n", cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		sst_byt_stream_start(byt, pcm_data->stream);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sst_byt_stream_resume(byt, pcm_data->stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		sst_byt_stream_stop(byt, pcm_data->stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sst_byt_stream_pause(byt, pcm_data->stream);
		break;
	default:
		break;
	}

	return 0;
}

static u32 byt_notify_pointer(struct sst_byt_stream *stream, void *data)
{
	struct sst_byt_pcm_data *pcm_data = data;
	struct snd_pcm_substream *substream = pcm_data->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 pos;

	pos = frames_to_bytes(runtime,
			      (runtime->control->appl_ptr %
			       runtime->buffer_size));

	dev_dbg(rtd->dev, "PCM: App pointer %d bytes\n", pos);

	snd_pcm_period_elapsed(substream);
	return pos;
}

static snd_pcm_uframes_t sst_byt_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sst_byt_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct sst_byt_pcm_data *pcm_data = snd_soc_pcm_get_drvdata(rtd);
	struct sst_byt *byt = pdata->byt;
	snd_pcm_uframes_t offset;
	int pos;

	pos = sst_byt_get_dsp_position(byt, pcm_data->stream,
				       snd_pcm_lib_buffer_bytes(substream));
	offset = bytes_to_frames(runtime, pos);

	dev_dbg(rtd->dev, "PCM: DMA pointer %zu bytes\n",
		frames_to_bytes(runtime, (u32)offset));
	return offset;
}

static int sst_byt_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sst_byt_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct sst_byt_pcm_data *pcm_data = snd_soc_pcm_get_drvdata(rtd);
	struct sst_byt *byt = pdata->byt;

	dev_dbg(rtd->dev, "PCM: open\n");

	pcm_data = &pdata->pcm[rtd->cpu_dai->id];
	mutex_lock(&pcm_data->mutex);

	snd_soc_pcm_set_drvdata(rtd, pcm_data);
	pcm_data->substream = substream;

	snd_soc_set_runtime_hwparams(substream, &sst_byt_pcm_hardware);

	pcm_data->stream = sst_byt_stream_new(byt, rtd->cpu_dai->id + 1,
					      byt_notify_pointer, pcm_data);
	if (pcm_data->stream == NULL) {
		dev_err(rtd->dev, "failed to create stream\n");
		mutex_unlock(&pcm_data->mutex);
		return -EINVAL;
	}

	mutex_unlock(&pcm_data->mutex);
	return 0;
}

static int sst_byt_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sst_byt_priv_data *pdata =
		snd_soc_platform_get_drvdata(rtd->platform);
	struct sst_byt_pcm_data *pcm_data = snd_soc_pcm_get_drvdata(rtd);
	struct sst_byt *byt = pdata->byt;
	int ret;

	dev_dbg(rtd->dev, "PCM: close\n");

	mutex_lock(&pcm_data->mutex);
	ret = sst_byt_stream_free(byt, pcm_data->stream);
	if (ret < 0) {
		dev_dbg(rtd->dev, "Free stream fail\n");
		goto out;
	}
	pcm_data->stream = NULL;

out:
	mutex_unlock(&pcm_data->mutex);
	return ret;
}

static int sst_byt_pcm_mmap(struct snd_pcm_substream *substream,
			    struct vm_area_struct *vma)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	dev_dbg(rtd->dev, "PCM: mmap\n");
	return snd_pcm_lib_default_mmap(substream, vma);
}

static struct snd_pcm_ops sst_byt_pcm_ops = {
	.open		= sst_byt_pcm_open,
	.close		= sst_byt_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= sst_byt_pcm_hw_params,
	.hw_free	= sst_byt_pcm_hw_free,
	.trigger	= sst_byt_pcm_trigger,
	.pointer	= sst_byt_pcm_pointer,
	.mmap		= sst_byt_pcm_mmap,
};

static void sst_byt_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int sst_byt_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;
	size_t size;
	int ret = 0;

	ret = dma_coerce_mask_and_coherent(rtd->card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream ||
	    pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		size = sst_byt_pcm_hardware.buffer_bytes_max;
		ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
							    SNDRV_DMA_TYPE_DEV,
							    rtd->card->dev,
							    size, size);
		if (ret) {
			dev_err(rtd->dev, "dma buffer allocation failed %d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static struct snd_soc_dai_driver byt_dais[] = {
	{
		.name  = "Front-cpu-dai",
		.playback = {
			.stream_name = "System Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
	{
		.name  = "Mic1-cpu-dai",
		.capture = {
			.stream_name = "Analog Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static int sst_byt_pcm_probe(struct snd_soc_platform *platform)
{
	struct sst_pdata *plat_data = dev_get_platdata(platform->dev);
	struct sst_byt_priv_data *priv_data;
	int i;

	if (!plat_data)
		return -ENODEV;

	priv_data = devm_kzalloc(platform->dev, sizeof(*priv_data),
				 GFP_KERNEL);
	priv_data->byt = plat_data->dsp;
	snd_soc_platform_set_drvdata(platform, priv_data);

	for (i = 0; i < ARRAY_SIZE(byt_dais); i++)
		mutex_init(&priv_data->pcm[i].mutex);

	return 0;
}

static int sst_byt_pcm_remove(struct snd_soc_platform *platform)
{
	return 0;
}

static struct snd_soc_platform_driver byt_soc_platform = {
	.probe		= sst_byt_pcm_probe,
	.remove		= sst_byt_pcm_remove,
	.ops		= &sst_byt_pcm_ops,
	.pcm_new	= sst_byt_pcm_new,
	.pcm_free	= sst_byt_pcm_free,
};

static const struct snd_soc_component_driver byt_dai_component = {
	.name		= "byt-dai",
};

static int sst_byt_pcm_dev_probe(struct platform_device *pdev)
{
	struct sst_pdata *sst_pdata = dev_get_platdata(&pdev->dev);
	int ret;

	ret = sst_byt_dsp_init(&pdev->dev, sst_pdata);
	if (ret < 0)
		return -ENODEV;

	ret = snd_soc_register_platform(&pdev->dev, &byt_soc_platform);
	if (ret < 0)
		goto err_plat;

	ret = snd_soc_register_component(&pdev->dev, &byt_dai_component,
					 byt_dais, ARRAY_SIZE(byt_dais));
	if (ret < 0)
		goto err_comp;

	return 0;

err_comp:
	snd_soc_unregister_platform(&pdev->dev);
err_plat:
	sst_byt_dsp_free(&pdev->dev, sst_pdata);
	return ret;
}

static int sst_byt_pcm_dev_remove(struct platform_device *pdev)
{
	struct sst_pdata *sst_pdata = dev_get_platdata(&pdev->dev);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	sst_byt_dsp_free(&pdev->dev, sst_pdata);

	return 0;
}

static struct platform_driver sst_byt_pcm_driver = {
	.driver = {
		.name = "baytrail-pcm-audio",
		.owner = THIS_MODULE,
	},

	.probe = sst_byt_pcm_dev_probe,
	.remove = sst_byt_pcm_dev_remove,
};
module_platform_driver(sst_byt_pcm_driver);

MODULE_AUTHOR("Jarkko Nikula");
MODULE_DESCRIPTION("Baytrail PCM");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:baytrail-pcm-audio");
