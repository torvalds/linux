/*
 * Au12x0/Au1550 PSC ALSA ASoC audio support.
 *
 * (c) 2007-2008 MSC Vertriebsges.m.b.H.,
 *	Manuel Lauss <mano@roarinelk.homelinux.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * DMA glue for Au1x-PSC audio.
 *
 * NOTE: all of these drivers can only work with a SINGLE instance
 *	 of a PSC. Multiple independent audio devices are impossible
 *	 with ASoC v1.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#include "psc.h"

/*#define PCM_DEBUG*/

#define MSG(x...)	printk(KERN_INFO "au1xpsc_pcm: " x)
#ifdef PCM_DEBUG
#define DBG		MSG
#else
#define DBG(x...)	do {} while (0)
#endif

struct au1xpsc_audio_dmadata {
	/* DDMA control data */
	unsigned int ddma_id;		/* DDMA direction ID for this PSC */
	u32 ddma_chan;			/* DDMA context */

	/* PCM context (for irq handlers) */
	struct snd_pcm_substream *substream;
	unsigned long curr_period;	/* current segment DDMA is working on */
	unsigned long q_period;		/* queue period(s) */
	unsigned long dma_area;		/* address of queued DMA area */
	unsigned long dma_area_s;	/* start address of DMA area */
	unsigned long pos;		/* current byte position being played */
	unsigned long periods;		/* number of SG segments in total */
	unsigned long period_bytes;	/* size in bytes of one SG segment */

	/* runtime data */
	int msbits;
};

/* instance data. There can be only one, MacLeod!!!! */
static struct au1xpsc_audio_dmadata *au1xpsc_audio_pcmdma[2];

/*
 * These settings are somewhat okay, at least on my machine audio plays
 * almost skip-free. Especially the 64kB buffer seems to help a LOT.
 */
#define AU1XPSC_PERIOD_MIN_BYTES	1024
#define AU1XPSC_BUFFER_MIN_BYTES	65536

#define AU1XPSC_PCM_FMTS					\
	(SNDRV_PCM_FMTBIT_S8     | SNDRV_PCM_FMTBIT_U8 |	\
	 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |	\
	 SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE |	\
	 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |	\
	 SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_U32_BE |	\
	 0)

/* PCM hardware DMA capabilities - platform specific */
static const struct snd_pcm_hardware au1xpsc_pcm_hardware = {
	.info		  = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			    SNDRV_PCM_INFO_INTERLEAVED,
	.formats	  = AU1XPSC_PCM_FMTS,
	.period_bytes_min = AU1XPSC_PERIOD_MIN_BYTES,
	.period_bytes_max = 4096 * 1024 - 1,
	.periods_min	  = 2,
	.periods_max	  = 4096,	/* 2 to as-much-as-you-like */
	.buffer_bytes_max = 4096 * 1024 - 1,
	.fifo_size	  = 16,		/* fifo entries of AC97/I2S PSC */
};

static void au1x_pcm_queue_tx(struct au1xpsc_audio_dmadata *cd)
{
	au1xxx_dbdma_put_source_flags(cd->ddma_chan,
				(void *)phys_to_virt(cd->dma_area),
				cd->period_bytes, DDMA_FLAGS_IE);

	/* update next-to-queue period */
	++cd->q_period;
	cd->dma_area += cd->period_bytes;
	if (cd->q_period >= cd->periods) {
		cd->q_period = 0;
		cd->dma_area = cd->dma_area_s;
	}
}

static void au1x_pcm_queue_rx(struct au1xpsc_audio_dmadata *cd)
{
	au1xxx_dbdma_put_dest_flags(cd->ddma_chan,
				(void *)phys_to_virt(cd->dma_area),
				cd->period_bytes, DDMA_FLAGS_IE);

	/* update next-to-queue period */
	++cd->q_period;
	cd->dma_area += cd->period_bytes;
	if (cd->q_period >= cd->periods) {
		cd->q_period = 0;
		cd->dma_area = cd->dma_area_s;
	}
}

static void au1x_pcm_dmatx_cb(int irq, void *dev_id)
{
	struct au1xpsc_audio_dmadata *cd = dev_id;

	cd->pos += cd->period_bytes;
	if (++cd->curr_period >= cd->periods) {
		cd->pos = 0;
		cd->curr_period = 0;
	}
	snd_pcm_period_elapsed(cd->substream);
	au1x_pcm_queue_tx(cd);
}

static void au1x_pcm_dmarx_cb(int irq, void *dev_id)
{
	struct au1xpsc_audio_dmadata *cd = dev_id;

	cd->pos += cd->period_bytes;
	if (++cd->curr_period >= cd->periods) {
		cd->pos = 0;
		cd->curr_period = 0;
	}
	snd_pcm_period_elapsed(cd->substream);
	au1x_pcm_queue_rx(cd);
}

static void au1x_pcm_dbdma_free(struct au1xpsc_audio_dmadata *pcd)
{
	if (pcd->ddma_chan) {
		au1xxx_dbdma_stop(pcd->ddma_chan);
		au1xxx_dbdma_reset(pcd->ddma_chan);
		au1xxx_dbdma_chan_free(pcd->ddma_chan);
		pcd->ddma_chan = 0;
		pcd->msbits = 0;
	}
}

/* in case of missing DMA ring or changed TX-source / RX-dest bit widths,
 * allocate (or reallocate) a 2-descriptor DMA ring with bit depth according
 * to ALSA-supplied sample depth.  This is due to limitations in the dbdma api
 * (cannot adjust source/dest widths of already allocated descriptor ring).
 */
static int au1x_pcm_dbdma_realloc(struct au1xpsc_audio_dmadata *pcd,
				 int stype, int msbits)
{
	/* DMA only in 8/16/32 bit widths */
	if (msbits == 24)
		msbits = 32;

	/* check current config: correct bits and descriptors allocated? */
	if ((pcd->ddma_chan) && (msbits == pcd->msbits))
		goto out;	/* all ok! */

	au1x_pcm_dbdma_free(pcd);

	if (stype == PCM_RX)
		pcd->ddma_chan = au1xxx_dbdma_chan_alloc(pcd->ddma_id,
					DSCR_CMD0_ALWAYS,
					au1x_pcm_dmarx_cb, (void *)pcd);
	else
		pcd->ddma_chan = au1xxx_dbdma_chan_alloc(DSCR_CMD0_ALWAYS,
					pcd->ddma_id,
					au1x_pcm_dmatx_cb, (void *)pcd);

	if (!pcd->ddma_chan)
		return -ENOMEM;

	au1xxx_dbdma_set_devwidth(pcd->ddma_chan, msbits);
	au1xxx_dbdma_ring_alloc(pcd->ddma_chan, 2);

	pcd->msbits = msbits;

	au1xxx_dbdma_stop(pcd->ddma_chan);
	au1xxx_dbdma_reset(pcd->ddma_chan);

out:
	return 0;
}

static int au1xpsc_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct au1xpsc_audio_dmadata *pcd;
	int stype, ret;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		goto out;

	stype = SUBSTREAM_TYPE(substream);
	pcd = au1xpsc_audio_pcmdma[stype];

	DBG("runtime->dma_area = 0x%08lx dma_addr_t = 0x%08lx dma_size = %d "
	    "runtime->min_align %d\n",
		(unsigned long)runtime->dma_area,
		(unsigned long)runtime->dma_addr, runtime->dma_bytes,
		runtime->min_align);

	DBG("bits %d  frags %d  frag_bytes %d  is_rx %d\n", params->msbits,
		params_periods(params), params_period_bytes(params), stype);

	ret = au1x_pcm_dbdma_realloc(pcd, stype, params->msbits);
	if (ret) {
		MSG("DDMA channel (re)alloc failed!\n");
		goto out;
	}

	pcd->substream = substream;
	pcd->period_bytes = params_period_bytes(params);
	pcd->periods = params_periods(params);
	pcd->dma_area_s = pcd->dma_area = (unsigned long)runtime->dma_addr;
	pcd->q_period = 0;
	pcd->curr_period = 0;
	pcd->pos = 0;

	ret = 0;
out:
	return ret;
}

static int au1xpsc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int au1xpsc_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct au1xpsc_audio_dmadata *pcd =
			au1xpsc_audio_pcmdma[SUBSTREAM_TYPE(substream)];

	au1xxx_dbdma_reset(pcd->ddma_chan);

	if (SUBSTREAM_TYPE(substream) == PCM_RX) {
		au1x_pcm_queue_rx(pcd);
		au1x_pcm_queue_rx(pcd);
	} else {
		au1x_pcm_queue_tx(pcd);
		au1x_pcm_queue_tx(pcd);
	}

	return 0;
}

static int au1xpsc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	u32 c = au1xpsc_audio_pcmdma[SUBSTREAM_TYPE(substream)]->ddma_chan;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		au1xxx_dbdma_start(c);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		au1xxx_dbdma_stop(c);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t
au1xpsc_pcm_pointer(struct snd_pcm_substream *substream)
{
	return bytes_to_frames(substream->runtime,
		au1xpsc_audio_pcmdma[SUBSTREAM_TYPE(substream)]->pos);
}

static int au1xpsc_pcm_open(struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &au1xpsc_pcm_hardware);
	return 0;
}

static int au1xpsc_pcm_close(struct snd_pcm_substream *substream)
{
	au1x_pcm_dbdma_free(au1xpsc_audio_pcmdma[SUBSTREAM_TYPE(substream)]);
	return 0;
}

struct snd_pcm_ops au1xpsc_pcm_ops = {
	.open		= au1xpsc_pcm_open,
	.close		= au1xpsc_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= au1xpsc_pcm_hw_params,
	.hw_free	= au1xpsc_pcm_hw_free,
	.prepare	= au1xpsc_pcm_prepare,
	.trigger	= au1xpsc_pcm_trigger,
	.pointer	= au1xpsc_pcm_pointer,
};

static void au1xpsc_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int au1xpsc_pcm_new(struct snd_card *card,
			   struct snd_soc_dai *dai,
			   struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
		card->dev, AU1XPSC_BUFFER_MIN_BYTES, (4096 * 1024) - 1);

	return 0;
}

static int au1xpsc_pcm_probe(struct platform_device *pdev)
{
	struct resource *r;
	int ret;

	if (au1xpsc_audio_pcmdma[PCM_TX] || au1xpsc_audio_pcmdma[PCM_RX])
		return -EBUSY;

	/* TX DMA */
	au1xpsc_audio_pcmdma[PCM_TX]
		= kzalloc(sizeof(struct au1xpsc_audio_dmadata), GFP_KERNEL);
	if (!au1xpsc_audio_pcmdma[PCM_TX])
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!r) {
		ret = -ENODEV;
		goto out1;
	}
	(au1xpsc_audio_pcmdma[PCM_TX])->ddma_id = r->start;

	/* RX DMA */
	au1xpsc_audio_pcmdma[PCM_RX]
		= kzalloc(sizeof(struct au1xpsc_audio_dmadata), GFP_KERNEL);
	if (!au1xpsc_audio_pcmdma[PCM_RX])
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!r) {
		ret = -ENODEV;
		goto out2;
	}
	(au1xpsc_audio_pcmdma[PCM_RX])->ddma_id = r->start;

	return 0;

out2:
	kfree(au1xpsc_audio_pcmdma[PCM_RX]);
	au1xpsc_audio_pcmdma[PCM_RX] = NULL;
out1:
	kfree(au1xpsc_audio_pcmdma[PCM_TX]);
	au1xpsc_audio_pcmdma[PCM_TX] = NULL;
	return ret;
}

static int au1xpsc_pcm_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (au1xpsc_audio_pcmdma[i]) {
			au1x_pcm_dbdma_free(au1xpsc_audio_pcmdma[i]);
			kfree(au1xpsc_audio_pcmdma[i]);
			au1xpsc_audio_pcmdma[i] = NULL;
		}
	}

	return 0;
}

/* au1xpsc audio platform */
struct snd_soc_platform au1xpsc_soc_platform = {
	.name		= "au1xpsc-pcm-dbdma",
	.probe		= au1xpsc_pcm_probe,
	.remove		= au1xpsc_pcm_remove,
	.pcm_ops 	= &au1xpsc_pcm_ops,
	.pcm_new	= au1xpsc_pcm_new,
	.pcm_free	= au1xpsc_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(au1xpsc_soc_platform);

static int __init au1xpsc_audio_dbdma_init(void)
{
	au1xpsc_audio_pcmdma[PCM_TX] = NULL;
	au1xpsc_audio_pcmdma[PCM_RX] = NULL;
	return snd_soc_register_platform(&au1xpsc_soc_platform);
}

static void __exit au1xpsc_audio_dbdma_exit(void)
{
	snd_soc_unregister_platform(&au1xpsc_soc_platform);
}

module_init(au1xpsc_audio_dbdma_init);
module_exit(au1xpsc_audio_dbdma_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au12x0/Au1550 PSC Audio DMA driver");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
