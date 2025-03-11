// SPDX-License-Identifier: GPL-2.0-only
/*
 * Au12x0/Au1550 PSC ALSA ASoC audio support.
 *
 * (c) 2007-2008 MSC Vertriebsges.m.b.H.,
 *	Manuel Lauss <manuel.lauss@gmail.com>
 *
 * DMA glue for Au1x-PSC audio.
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

#define DRV_NAME "dbdma2"

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
	dma_addr_t dma_area;		/* address of queued DMA area */
	dma_addr_t dma_area_s;		/* start address of DMA area */
	unsigned long pos;		/* current byte position being played */
	unsigned long periods;		/* number of SG segments in total */
	unsigned long period_bytes;	/* size in bytes of one SG segment */

	/* runtime data */
	int msbits;
};

/*
 * These settings are somewhat okay, at least on my machine audio plays
 * almost skip-free. Especially the 64kB buffer seems to help a LOT.
 */
#define AU1XPSC_PERIOD_MIN_BYTES	1024
#define AU1XPSC_BUFFER_MIN_BYTES	65536

/* PCM hardware DMA capabilities - platform specific */
static const struct snd_pcm_hardware au1xpsc_pcm_hardware = {
	.info		  = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			    SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BATCH,
	.period_bytes_min = AU1XPSC_PERIOD_MIN_BYTES,
	.period_bytes_max = 4096 * 1024 - 1,
	.periods_min	  = 2,
	.periods_max	  = 4096,	/* 2 to as-much-as-you-like */
	.buffer_bytes_max = 4096 * 1024 - 1,
	.fifo_size	  = 16,		/* fifo entries of AC97/I2S PSC */
};

static void au1x_pcm_queue_tx(struct au1xpsc_audio_dmadata *cd)
{
	au1xxx_dbdma_put_source(cd->ddma_chan, cd->dma_area,
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
	au1xxx_dbdma_put_dest(cd->ddma_chan, cd->dma_area,
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

	if (stype == SNDRV_PCM_STREAM_CAPTURE)
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

static inline struct au1xpsc_audio_dmadata *to_dmadata(struct snd_pcm_substream *ss,
						       struct snd_soc_component *component)
{
	struct au1xpsc_audio_dmadata *pcd = snd_soc_component_get_drvdata(component);
	return &pcd[ss->stream];
}

static int au1xpsc_pcm_hw_params(struct snd_soc_component *component,
				 struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct au1xpsc_audio_dmadata *pcd;
	int stype, ret;

	stype = substream->stream;
	pcd = to_dmadata(substream, component);

	DBG("runtime->dma_area = 0x%08lx dma_addr_t = 0x%08lx dma_size = %zu "
	    "runtime->min_align %lu\n",
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
	pcd->dma_area_s = pcd->dma_area = runtime->dma_addr;
	pcd->q_period = 0;
	pcd->curr_period = 0;
	pcd->pos = 0;

	ret = 0;
out:
	return ret;
}

static int au1xpsc_pcm_prepare(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct au1xpsc_audio_dmadata *pcd = to_dmadata(substream, component);

	au1xxx_dbdma_reset(pcd->ddma_chan);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		au1x_pcm_queue_rx(pcd);
		au1x_pcm_queue_rx(pcd);
	} else {
		au1x_pcm_queue_tx(pcd);
		au1x_pcm_queue_tx(pcd);
	}

	return 0;
}

static int au1xpsc_pcm_trigger(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream, int cmd)
{
	u32 c = to_dmadata(substream, component)->ddma_chan;

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
au1xpsc_pcm_pointer(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream)
{
	return bytes_to_frames(substream->runtime,
			       to_dmadata(substream, component)->pos);
}

static int au1xpsc_pcm_open(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	struct au1xpsc_audio_dmadata *pcd = to_dmadata(substream, component);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int stype = substream->stream, *dmaids;

	dmaids = snd_soc_dai_get_dma_data(snd_soc_rtd_to_cpu(rtd, 0), substream);
	if (!dmaids)
		return -ENODEV;	/* whoa, has ordering changed? */

	pcd->ddma_id = dmaids[stype];

	snd_soc_set_runtime_hwparams(substream, &au1xpsc_pcm_hardware);
	return 0;
}

static int au1xpsc_pcm_close(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	au1x_pcm_dbdma_free(to_dmadata(substream, component));
	return 0;
}

static int au1xpsc_pcm_new(struct snd_soc_component *component,
			   struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
		card->dev, AU1XPSC_BUFFER_MIN_BYTES, (4096 * 1024) - 1);

	return 0;
}

/* au1xpsc audio platform */
static const struct snd_soc_component_driver au1xpsc_soc_component = {
	.name		= DRV_NAME,
	.open		= au1xpsc_pcm_open,
	.close		= au1xpsc_pcm_close,
	.hw_params	= au1xpsc_pcm_hw_params,
	.prepare	= au1xpsc_pcm_prepare,
	.trigger	= au1xpsc_pcm_trigger,
	.pointer	= au1xpsc_pcm_pointer,
	.pcm_construct	= au1xpsc_pcm_new,
};

static int au1xpsc_pcm_drvprobe(struct platform_device *pdev)
{
	struct au1xpsc_audio_dmadata *dmadata;

	dmadata = devm_kcalloc(&pdev->dev,
			       2, sizeof(struct au1xpsc_audio_dmadata),
			       GFP_KERNEL);
	if (!dmadata)
		return -ENOMEM;

	platform_set_drvdata(pdev, dmadata);

	return devm_snd_soc_register_component(&pdev->dev,
					&au1xpsc_soc_component, NULL, 0);
}

static struct platform_driver au1xpsc_pcm_driver = {
	.driver	= {
		.name	= "au1xpsc-pcm",
	},
	.probe		= au1xpsc_pcm_drvprobe,
};

module_platform_driver(au1xpsc_pcm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Au12x0/Au1550 PSC Audio DMA driver");
MODULE_AUTHOR("Manuel Lauss");
