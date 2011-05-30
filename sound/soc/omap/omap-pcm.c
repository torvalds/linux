/*
 * omap-pcm.c  --  ALSA PCM interface for the OMAP SoC
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jhnikula@gmail.com>
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <plat/dma.h>
#include "omap-pcm.h"

static const struct snd_pcm_hardware omap_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 64 * 1024,
	.periods_min		= 2,
	.periods_max		= 255,
	.buffer_bytes_max	= 128 * 1024,
};

struct omap_runtime_data {
	spinlock_t			lock;
	struct omap_pcm_dma_data	*dma_data;
	int				dma_ch;
	int				period_index;
};

static void omap_pcm_dma_irq(int ch, u16 stat, void *data)
{
	struct snd_pcm_substream *substream = data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd = runtime->private_data;
	unsigned long flags;

	if ((cpu_is_omap1510())) {
		/*
		 * OMAP1510 doesn't fully support DMA progress counter
		 * and there is no software emulation implemented yet,
		 * so have to maintain our own progress counters
		 * that can be used by omap_pcm_pointer() instead.
		 */
		spin_lock_irqsave(&prtd->lock, flags);
		if ((stat == OMAP_DMA_LAST_IRQ) &&
				(prtd->period_index == runtime->periods - 1)) {
			/* we are in sync, do nothing */
			spin_unlock_irqrestore(&prtd->lock, flags);
			return;
		}
		if (prtd->period_index >= 0) {
			if (stat & OMAP_DMA_BLOCK_IRQ) {
				/* end of buffer reached, loop back */
				prtd->period_index = 0;
			} else if (stat & OMAP_DMA_LAST_IRQ) {
				/* update the counter for the last period */
				prtd->period_index = runtime->periods - 1;
			} else if (++prtd->period_index >= runtime->periods) {
				/* end of buffer missed? loop back */
				prtd->period_index = 0;
			}
		}
		spin_unlock_irqrestore(&prtd->lock, flags);
	}

	snd_pcm_period_elapsed(substream);
}

/* this may get called several times by oss emulation */
static int omap_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct omap_runtime_data *prtd = runtime->private_data;
	struct omap_pcm_dma_data *dma_data;

	int err = 0;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma_data)
		return 0;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	if (prtd->dma_data)
		return 0;
	prtd->dma_data = dma_data;
	err = omap_request_dma(dma_data->dma_req, dma_data->name,
			       omap_pcm_dma_irq, substream, &prtd->dma_ch);
	if (!err) {
		/*
		 * Link channel with itself so DMA doesn't need any
		 * reprogramming while looping the buffer
		 */
		omap_dma_link_lch(prtd->dma_ch, prtd->dma_ch);
	}

	return err;
}

static int omap_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd = runtime->private_data;

	if (prtd->dma_data == NULL)
		return 0;

	omap_dma_unlink_lch(prtd->dma_ch, prtd->dma_ch);
	omap_free_dma(prtd->dma_ch);
	prtd->dma_data = NULL;

	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int omap_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd = runtime->private_data;
	struct omap_pcm_dma_data *dma_data = prtd->dma_data;
	struct omap_dma_channel_params dma_params;
	int bytes;

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!prtd->dma_data)
		return 0;

	memset(&dma_params, 0, sizeof(dma_params));
	dma_params.data_type			= dma_data->data_type;
	dma_params.trigger			= dma_data->dma_req;
	dma_params.sync_mode			= dma_data->sync_mode;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_params.src_amode		= OMAP_DMA_AMODE_POST_INC;
		dma_params.dst_amode		= OMAP_DMA_AMODE_CONSTANT;
		dma_params.src_or_dst_synch	= OMAP_DMA_DST_SYNC;
		dma_params.src_start		= runtime->dma_addr;
		dma_params.dst_start		= dma_data->port_addr;
		dma_params.dst_port		= OMAP_DMA_PORT_MPUI;
		dma_params.dst_fi		= dma_data->packet_size;
	} else {
		dma_params.src_amode		= OMAP_DMA_AMODE_CONSTANT;
		dma_params.dst_amode		= OMAP_DMA_AMODE_POST_INC;
		dma_params.src_or_dst_synch	= OMAP_DMA_SRC_SYNC;
		dma_params.src_start		= dma_data->port_addr;
		dma_params.dst_start		= runtime->dma_addr;
		dma_params.src_port		= OMAP_DMA_PORT_MPUI;
		dma_params.src_fi		= dma_data->packet_size;
	}
	/*
	 * Set DMA transfer frame size equal to ALSA period size and frame
	 * count as no. of ALSA periods. Then with DMA frame interrupt enabled,
	 * we can transfer the whole ALSA buffer with single DMA transfer but
	 * still can get an interrupt at each period bounary
	 */
	bytes = snd_pcm_lib_period_bytes(substream);
	dma_params.elem_count	= bytes >> dma_data->data_type;
	dma_params.frame_count	= runtime->periods;
	omap_set_dma_params(prtd->dma_ch, &dma_params);

	if ((cpu_is_omap1510()))
		omap_enable_dma_irq(prtd->dma_ch, OMAP_DMA_FRAME_IRQ |
			      OMAP_DMA_LAST_IRQ | OMAP_DMA_BLOCK_IRQ);
	else if (!substream->runtime->no_period_wakeup)
		omap_enable_dma_irq(prtd->dma_ch, OMAP_DMA_FRAME_IRQ);

	if (!(cpu_class_is_omap1())) {
		omap_set_dma_src_burst_mode(prtd->dma_ch,
						OMAP_DMA_DATA_BURST_16);
		omap_set_dma_dest_burst_mode(prtd->dma_ch,
						OMAP_DMA_DATA_BURST_16);
	}

	return 0;
}

static int omap_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd = runtime->private_data;
	struct omap_pcm_dma_data *dma_data = prtd->dma_data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&prtd->lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		prtd->period_index = 0;
		/* Configure McBSP internal buffer usage */
		if (dma_data->set_threshold)
			dma_data->set_threshold(substream);

		omap_start_dma(prtd->dma_ch);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		prtd->period_index = -1;
		omap_stop_dma(prtd->dma_ch);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&prtd->lock, flags);

	return ret;
}

static snd_pcm_uframes_t omap_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd = runtime->private_data;
	dma_addr_t ptr;
	snd_pcm_uframes_t offset;

	if (cpu_is_omap1510()) {
		offset = prtd->period_index * runtime->period_size;
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ptr = omap_get_dma_dst_pos(prtd->dma_ch);
		offset = bytes_to_frames(runtime, ptr - runtime->dma_addr);
	} else {
		ptr = omap_get_dma_src_pos(prtd->dma_ch);
		offset = bytes_to_frames(runtime, ptr - runtime->dma_addr);
	}

	if (offset >= runtime->buffer_size)
		offset = 0;

	return offset;
}

static int omap_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct omap_runtime_data *prtd;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &omap_pcm_hardware);

	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	spin_lock_init(&prtd->lock);
	runtime->private_data = prtd;

out:
	return ret;
}

static int omap_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	kfree(runtime->private_data);
	return 0;
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
	.close		= omap_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= omap_pcm_hw_params,
	.hw_free	= omap_pcm_hw_free,
	.prepare	= omap_pcm_prepare,
	.trigger	= omap_pcm_trigger,
	.pointer	= omap_pcm_pointer,
	.mmap		= omap_pcm_mmap,
};

static u64 omap_pcm_dmamask = DMA_BIT_MASK(64);

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

static int omap_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
		 struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &omap_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(64);

	if (dai->driver->playback.channels_min) {
		ret = omap_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->driver->capture.channels_min) {
		ret = omap_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static struct snd_soc_platform_driver omap_soc_platform = {
	.ops		= &omap_pcm_ops,
	.pcm_new	= omap_pcm_new,
	.pcm_free	= omap_pcm_free_dma_buffers,
};

static __devinit int omap_pcm_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev,
			&omap_soc_platform);
}

static int __devexit omap_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver omap_pcm_driver = {
	.driver = {
			.name = "omap-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = omap_pcm_probe,
	.remove = __devexit_p(omap_pcm_remove),
};

static int __init snd_omap_pcm_init(void)
{
	return platform_driver_register(&omap_pcm_driver);
}
module_init(snd_omap_pcm_init);

static void __exit snd_omap_pcm_exit(void)
{
	platform_driver_unregister(&omap_pcm_driver);
}
module_exit(snd_omap_pcm_exit);

MODULE_AUTHOR("Jarkko Nikula <jhnikula@gmail.com>");
MODULE_DESCRIPTION("OMAP PCM DMA module");
MODULE_LICENSE("GPL");
