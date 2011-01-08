/*
 * tegra_pcm.c - Tegra PCM driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 * Vijay Mali <vmali@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra_pcm.h"

static const struct snd_pcm_hardware tegra_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min		= 2,
	.channels_max		= 2,
	.period_bytes_min	= 1024,
	.period_bytes_max	= PAGE_SIZE,
	.periods_min		= 2,
	.periods_max		= 8,
	.buffer_bytes_max	= PAGE_SIZE * 8,
	.fifo_size		= 4,
};

static void tegra_pcm_queue_dma(struct tegra_runtime_data *prtd)
{
	struct snd_pcm_substream *substream = prtd->substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	struct tegra_dma_req *dma_req;
	unsigned long addr;

	dma_req = &prtd->dma_req[prtd->dma_req_idx];
	prtd->dma_req_idx = 1 - prtd->dma_req_idx;

	addr = buf->addr + prtd->dma_pos;
	prtd->dma_pos += dma_req->size;
	if (prtd->dma_pos >= prtd->dma_pos_end)
		prtd->dma_pos = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_req->source_addr = addr;
	else
		dma_req->dest_addr = addr;

	tegra_dma_enqueue_req(prtd->dma_chan, dma_req);
}

static void dma_complete_callback(struct tegra_dma_req *req)
{
	struct tegra_runtime_data *prtd = (struct tegra_runtime_data *)req->dev;
	struct snd_pcm_substream *substream = prtd->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock(&prtd->lock);

	if (!prtd->running) {
		spin_unlock(&prtd->lock);
		return;
	}

	if (++prtd->period_index >= runtime->periods)
		prtd->period_index = 0;

	tegra_pcm_queue_dma(prtd);

	spin_unlock(&prtd->lock);

	snd_pcm_period_elapsed(substream);
}

static void setup_dma_tx_request(struct tegra_dma_req *req,
					struct tegra_pcm_dma_params * dmap)
{
	req->complete = dma_complete_callback;
	req->to_memory = false;
	req->dest_addr = dmap->addr;
	req->dest_wrap = dmap->wrap;
	req->source_bus_width = 32;
	req->source_wrap = 0;
	req->dest_bus_width = dmap->width;
	req->req_sel = dmap->req_sel;
}

static void setup_dma_rx_request(struct tegra_dma_req *req,
					struct tegra_pcm_dma_params * dmap)
{
	req->complete = dma_complete_callback;
	req->to_memory = true;
	req->source_addr = dmap->addr;
	req->dest_wrap = 0;
	req->source_bus_width = dmap->width;
	req->source_wrap = dmap->wrap;
	req->dest_bus_width = 32;
	req->req_sel = dmap->req_sel;
}

static int tegra_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct tegra_pcm_dma_params * dmap;
	int ret = 0;

	prtd = kzalloc(sizeof(struct tegra_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	runtime->private_data = prtd;
	prtd->substream = substream;

	spin_lock_init(&prtd->lock);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dmap = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
		setup_dma_tx_request(&prtd->dma_req[0], dmap);
		setup_dma_tx_request(&prtd->dma_req[1], dmap);
	} else {
		dmap = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
		setup_dma_rx_request(&prtd->dma_req[0], dmap);
		setup_dma_rx_request(&prtd->dma_req[1], dmap);
	}

	prtd->dma_req[0].dev = prtd;
	prtd->dma_req[1].dev = prtd;

	prtd->dma_chan = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT);
	if (IS_ERR(prtd->dma_chan)) {
		ret = PTR_ERR(prtd->dma_chan);
		goto err;
	}

	/* Set HW params now that initialization is complete */
	snd_soc_set_runtime_hwparams(substream, &tegra_pcm_hardware);

	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto err;

	return 0;

err:
	if (prtd->dma_chan) {
		tegra_dma_free_channel(prtd->dma_chan);
	}

	kfree(prtd);

	return ret;
}

static int tegra_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd = runtime->private_data;

	tegra_dma_free_channel(prtd->dma_chan);

	kfree(prtd);

	return 0;
}

static int tegra_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd = runtime->private_data;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	prtd->dma_req[0].size = params_period_bytes(params);
	prtd->dma_req[1].size = prtd->dma_req[0].size;

	return 0;
}

static int tegra_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int tegra_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd = runtime->private_data;
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		prtd->dma_pos = 0;
		prtd->dma_pos_end = frames_to_bytes(runtime, runtime->periods * runtime->period_size);
		prtd->period_index = 0;
		prtd->dma_req_idx = 0;
		/* Fall-through */
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&prtd->lock, flags);
		prtd->running = 1;
		spin_unlock_irqrestore(&prtd->lock, flags);
		tegra_pcm_queue_dma(prtd);
		tegra_pcm_queue_dma(prtd);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irqsave(&prtd->lock, flags);
		prtd->running = 0;
		spin_unlock_irqrestore(&prtd->lock, flags);
		tegra_dma_dequeue_req(prtd->dma_chan, &prtd->dma_req[0]);
		tegra_dma_dequeue_req(prtd->dma_chan, &prtd->dma_req[1]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t tegra_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct tegra_runtime_data *prtd = runtime->private_data;

	return prtd->period_index * runtime->period_size;
}


static int tegra_pcm_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
					runtime->dma_area,
					runtime->dma_addr,
					runtime->dma_bytes);
}

static struct snd_pcm_ops tegra_pcm_ops = {
	.open		= tegra_pcm_open,
	.close		= tegra_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= tegra_pcm_hw_params,
	.hw_free	= tegra_pcm_hw_free,
	.trigger	= tegra_pcm_trigger,
	.pointer	= tegra_pcm_pointer,
	.mmap		= tegra_pcm_mmap,
};

static int tegra_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = tegra_pcm_hardware.buffer_bytes_max;

	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
						&buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->bytes = size;

	return 0;
}

static void tegra_pcm_deallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	if (!buf->area)
		return;

	dma_free_writecombine(pcm->card->dev, buf->bytes,
				buf->area, buf->addr);
	buf->area = NULL;
}

static u64 tegra_dma_mask = DMA_BIT_MASK(32);

static int tegra_pcm_new(struct snd_card *card,
				struct snd_soc_dai *dai, struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &tegra_dma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->driver->playback.channels_min) {
		ret = tegra_pcm_preallocate_dma_buffer(pcm,
						SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto err;
	}

	if (dai->driver->capture.channels_min) {
		ret = tegra_pcm_preallocate_dma_buffer(pcm,
						SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto err_free_play;
	}

	return 0;

err_free_play:
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_PLAYBACK);
err:
	return ret;
}

static void tegra_pcm_free(struct snd_pcm *pcm)
{
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_CAPTURE);
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_PLAYBACK);
}

struct snd_soc_platform_driver tegra_pcm_platform = {
	.ops		= &tegra_pcm_ops,
	.pcm_new	= tegra_pcm_new,
	.pcm_free	= tegra_pcm_free,
};

static int __devinit tegra_pcm_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &tegra_pcm_platform);
}

static int __devexit tegra_pcm_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver tegra_pcm_driver = {
	.driver = {
		.name = "tegra-pcm-audio",
		.owner = THIS_MODULE,
	},
	.probe = tegra_pcm_platform_probe,
	.remove = __devexit_p(tegra_pcm_platform_remove),
};

static int __init snd_tegra_pcm_init(void)
{
	return platform_driver_register(&tegra_pcm_driver);
}
module_init(snd_tegra_pcm_init);

static void __exit snd_tegra_pcm_exit(void)
{
	platform_driver_unregister(&tegra_pcm_driver);
}
module_exit(snd_tegra_pcm_exit);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra PCM ASoC driver");
MODULE_LICENSE("GPL");
