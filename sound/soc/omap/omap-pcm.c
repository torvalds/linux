/*
 * omap-pcm.c  --  ALSA PCM interface for the OMAP SoC
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
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

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/omap-dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/omap-pcm.h>

#ifdef CONFIG_ARCH_OMAP1
#define pcm_omap1510()	cpu_is_omap1510()
#else
#define pcm_omap1510()	0
#endif

static struct snd_pcm_hardware omap_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.period_bytes_min	= 32,
	.period_bytes_max	= 64 * 1024,
	.periods_min		= 2,
	.periods_max		= 255,
	.buffer_bytes_max	= 128 * 1024,
};

/* sDMA supports only 1, 2, and 4 byte transfer elements. */
static void omap_pcm_limit_supported_formats(void)
{
	int i;

	for (i = 0; i < SNDRV_PCM_FORMAT_LAST; i++) {
		switch (snd_pcm_format_physical_width(i)) {
		case 8:
		case 16:
		case 32:
			omap_pcm_hardware.formats |= (1LL << i);
			break;
		default:
			break;
		}
	}
}

/* this may get called several times by oss emulation */
static int omap_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct omap_pcm_dma_data *dma_data;
	struct dma_slave_config config;
	struct dma_chan *chan;
	int err = 0;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma_data)
		return 0;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	chan = snd_dmaengine_pcm_get_chan(substream);
	if (!chan)
		return -EINVAL;

	/* fills in addr_width and direction */
	err = snd_hwparams_to_dma_slave_config(substream, params, &config);
	if (err)
		return err;

	snd_dmaengine_pcm_set_config_from_dai_data(substream,
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream),
			&config);

	return dmaengine_slave_config(chan, &config);
}

static int omap_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static snd_pcm_uframes_t omap_pcm_pointer(struct snd_pcm_substream *substream)
{
	snd_pcm_uframes_t offset;

	if (pcm_omap1510())
		offset = snd_dmaengine_pcm_pointer_no_residue(substream);
	else
		offset = snd_dmaengine_pcm_pointer(substream);

	return offset;
}

static int omap_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &omap_pcm_hardware);

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* DT boot: filter_data is the DMA name */
	if (rtd->cpu_dai->dev->of_node) {
		struct dma_chan *chan;

		chan = dma_request_slave_channel(rtd->cpu_dai->dev,
						 dma_data->filter_data);
		ret = snd_dmaengine_pcm_open(substream, chan);
	} else {
		ret = snd_dmaengine_pcm_open_request_chan(substream,
							  omap_dma_filter_fn,
							  dma_data->filter_data);
	}
	return ret;
}

static int omap_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops omap_pcm_ops = {
	.open		= omap_pcm_open,
	.close		= snd_dmaengine_pcm_close_release_chan,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= omap_pcm_hw_params,
	.hw_free	= omap_pcm_hw_free,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= omap_pcm_pointer,
	.mmap		= omap_pcm_mmap,
};

static int omap_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = omap_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static void omap_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
	}
}

static int omap_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = omap_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = omap_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	/* free preallocated buffers in case of error */
	if (ret)
		omap_pcm_free_dma_buffers(pcm);

	return ret;
}

static struct snd_soc_platform_driver omap_soc_platform = {
	.ops		= &omap_pcm_ops,
	.pcm_new	= omap_pcm_new,
	.pcm_free	= omap_pcm_free_dma_buffers,
};

int omap_pcm_platform_register(struct device *dev)
{
	omap_pcm_limit_supported_formats();
	return devm_snd_soc_register_platform(dev, &omap_soc_platform);
}
EXPORT_SYMBOL_GPL(omap_pcm_platform_register);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@bitmer.com>");
MODULE_DESCRIPTION("OMAP PCM DMA module");
MODULE_LICENSE("GPL");
