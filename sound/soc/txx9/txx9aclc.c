// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic TXx9 ACLC platform driver
 *
 * Copyright (C) 2009 Atsushi Nemoto
 *
 * Based on RBTX49xx patch from CELF patch archive.
 * (C) Copyright TOSHIBA CORPORATION 2004-2006
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "txx9aclc.h"

#define DRV_NAME "txx9aclc"

static struct txx9aclc_soc_device {
	struct txx9aclc_dmadata dmadata[2];
} txx9aclc_soc_device;

/* REVISIT: How to find txx9aclc_drvdata from snd_ac97? */
static struct txx9aclc_plat_drvdata *txx9aclc_drvdata;

static int txx9aclc_dma_init(struct txx9aclc_soc_device *dev,
			     struct txx9aclc_dmadata *dmadata);

static const struct snd_pcm_hardware txx9aclc_pcm_hardware = {
	/*
	 * REVISIT: SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID
	 * needs more works for noncoherent MIPS.
	 */
	.info		  = SNDRV_PCM_INFO_INTERLEAVED |
			    SNDRV_PCM_INFO_BATCH |
			    SNDRV_PCM_INFO_PAUSE,
	.period_bytes_min = 1024,
	.period_bytes_max = 8 * 1024,
	.periods_min	  = 2,
	.periods_max	  = 4096,
	.buffer_bytes_max = 32 * 1024,
};

static int txx9aclc_pcm_hw_params(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct txx9aclc_dmadata *dmadata = runtime->private_data;

	dev_dbg(component->dev,
		"runtime->dma_area = %#lx dma_addr = %#lx dma_bytes = %zd "
		"runtime->min_align %ld\n",
		(unsigned long)runtime->dma_area,
		(unsigned long)runtime->dma_addr, runtime->dma_bytes,
		runtime->min_align);
	dev_dbg(component->dev,
		"periods %d period_bytes %d stream %d\n",
		params_periods(params), params_period_bytes(params),
		substream->stream);

	dmadata->substream = substream;
	dmadata->pos = 0;
	return 0;
}

static int txx9aclc_pcm_prepare(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct txx9aclc_dmadata *dmadata = runtime->private_data;

	dmadata->dma_addr = runtime->dma_addr;
	dmadata->buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	dmadata->period_bytes = snd_pcm_lib_period_bytes(substream);

	if (dmadata->buffer_bytes == dmadata->period_bytes) {
		dmadata->frag_bytes = dmadata->period_bytes >> 1;
		dmadata->frags = 2;
	} else {
		dmadata->frag_bytes = dmadata->period_bytes;
		dmadata->frags = dmadata->buffer_bytes / dmadata->period_bytes;
	}
	dmadata->frag_count = 0;
	dmadata->pos = 0;
	return 0;
}

static void txx9aclc_dma_complete(void *arg)
{
	struct txx9aclc_dmadata *dmadata = arg;
	unsigned long flags;

	/* dma completion handler cannot submit new operations */
	spin_lock_irqsave(&dmadata->dma_lock, flags);
	if (dmadata->frag_count >= 0) {
		dmadata->dmacount--;
		if (!WARN_ON(dmadata->dmacount < 0))
			queue_work(system_highpri_wq, &dmadata->work);
	}
	spin_unlock_irqrestore(&dmadata->dma_lock, flags);
}

static struct dma_async_tx_descriptor *
txx9aclc_dma_submit(struct txx9aclc_dmadata *dmadata, dma_addr_t buf_dma_addr)
{
	struct dma_chan *chan = dmadata->dma_chan;
	struct dma_async_tx_descriptor *desc;
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, pfn_to_page(PFN_DOWN(buf_dma_addr)),
		    dmadata->frag_bytes, buf_dma_addr & (PAGE_SIZE - 1));
	sg_dma_address(&sg) = buf_dma_addr;
	desc = dmaengine_prep_slave_sg(chan, &sg, 1,
		dmadata->substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		DMA_MEM_TO_DEV : DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(&chan->dev->device, "cannot prepare slave dma\n");
		return NULL;
	}
	desc->callback = txx9aclc_dma_complete;
	desc->callback_param = dmadata;
	dmaengine_submit(desc);
	return desc;
}

#define NR_DMA_CHAIN		2

static void txx9aclc_dma_work(struct work_struct *work)
{
	struct txx9aclc_dmadata *dmadata =
		container_of(work, struct txx9aclc_dmadata, work);
	struct dma_chan *chan = dmadata->dma_chan;
	struct dma_async_tx_descriptor *desc;
	struct snd_pcm_substream *substream = dmadata->substream;
	u32 ctlbit = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		ACCTL_AUDODMA : ACCTL_AUDIDMA;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dmadata->dma_lock, flags);
	if (dmadata->frag_count < 0) {
		struct txx9aclc_plat_drvdata *drvdata = txx9aclc_drvdata;
		void __iomem *base = drvdata->base;

		spin_unlock_irqrestore(&dmadata->dma_lock, flags);
		dmaengine_terminate_all(chan);
		/* first time */
		for (i = 0; i < NR_DMA_CHAIN; i++) {
			desc = txx9aclc_dma_submit(dmadata,
				dmadata->dma_addr + i * dmadata->frag_bytes);
			if (!desc)
				return;
		}
		dmadata->dmacount = NR_DMA_CHAIN;
		dma_async_issue_pending(chan);
		spin_lock_irqsave(&dmadata->dma_lock, flags);
		__raw_writel(ctlbit, base + ACCTLEN);
		dmadata->frag_count = NR_DMA_CHAIN % dmadata->frags;
		spin_unlock_irqrestore(&dmadata->dma_lock, flags);
		return;
	}
	if (WARN_ON(dmadata->dmacount >= NR_DMA_CHAIN)) {
		spin_unlock_irqrestore(&dmadata->dma_lock, flags);
		return;
	}
	while (dmadata->dmacount < NR_DMA_CHAIN) {
		dmadata->dmacount++;
		spin_unlock_irqrestore(&dmadata->dma_lock, flags);
		desc = txx9aclc_dma_submit(dmadata,
			dmadata->dma_addr +
			dmadata->frag_count * dmadata->frag_bytes);
		if (!desc)
			return;
		dma_async_issue_pending(chan);

		spin_lock_irqsave(&dmadata->dma_lock, flags);
		dmadata->frag_count++;
		dmadata->frag_count %= dmadata->frags;
		dmadata->pos += dmadata->frag_bytes;
		dmadata->pos %= dmadata->buffer_bytes;
		if ((dmadata->frag_count * dmadata->frag_bytes) %
		    dmadata->period_bytes == 0)
			snd_pcm_period_elapsed(substream);
	}
	spin_unlock_irqrestore(&dmadata->dma_lock, flags);
}

static int txx9aclc_pcm_trigger(struct snd_soc_component *component,
				struct snd_pcm_substream *substream, int cmd)
{
	struct txx9aclc_dmadata *dmadata = substream->runtime->private_data;
	struct txx9aclc_plat_drvdata *drvdata = txx9aclc_drvdata;
	void __iomem *base = drvdata->base;
	unsigned long flags;
	int ret = 0;
	u32 ctlbit = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		ACCTL_AUDODMA : ACCTL_AUDIDMA;

	spin_lock_irqsave(&dmadata->dma_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dmadata->frag_count = -1;
		queue_work(system_highpri_wq, &dmadata->work);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		__raw_writel(ctlbit, base + ACCTLDIS);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		__raw_writel(ctlbit, base + ACCTLEN);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&dmadata->dma_lock, flags);
	return ret;
}

static snd_pcm_uframes_t
txx9aclc_pcm_pointer(struct snd_soc_component *component,
		     struct snd_pcm_substream *substream)
{
	struct txx9aclc_dmadata *dmadata = substream->runtime->private_data;

	return bytes_to_frames(substream->runtime, dmadata->pos);
}

static int txx9aclc_pcm_open(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct txx9aclc_soc_device *dev = &txx9aclc_soc_device;
	struct txx9aclc_dmadata *dmadata = &dev->dmadata[substream->stream];
	int ret;

	ret = snd_soc_set_runtime_hwparams(substream, &txx9aclc_pcm_hardware);
	if (ret)
		return ret;
	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;
	substream->runtime->private_data = dmadata;
	return 0;
}

static int txx9aclc_pcm_close(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct txx9aclc_dmadata *dmadata = substream->runtime->private_data;
	struct dma_chan *chan = dmadata->dma_chan;

	dmadata->frag_count = -1;
	dmaengine_terminate_all(chan);
	return 0;
}

static int txx9aclc_pcm_new(struct snd_soc_component *component,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_pcm *pcm = rtd->pcm;
	struct platform_device *pdev = to_platform_device(component->dev);
	struct txx9aclc_soc_device *dev;
	struct resource *r;
	int i;
	int ret;

	/* at this point onwards the AC97 component has probed and this will be valid */
	dev = snd_soc_dai_get_drvdata(dai);

	dev->dmadata[0].stream = SNDRV_PCM_STREAM_PLAYBACK;
	dev->dmadata[1].stream = SNDRV_PCM_STREAM_CAPTURE;
	for (i = 0; i < 2; i++) {
		r = platform_get_resource(pdev, IORESOURCE_DMA, i);
		if (!r) {
			ret = -EBUSY;
			goto exit;
		}
		dev->dmadata[i].dma_res = r;
		ret = txx9aclc_dma_init(dev, &dev->dmadata[i]);
		if (ret)
			goto exit;
	}

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
		card->dev, 64 * 1024, 4 * 1024 * 1024);
	return 0;

exit:
	for (i = 0; i < 2; i++) {
		if (dev->dmadata[i].dma_chan)
			dma_release_channel(dev->dmadata[i].dma_chan);
		dev->dmadata[i].dma_chan = NULL;
	}
	return ret;
}

static bool filter(struct dma_chan *chan, void *param)
{
	struct txx9aclc_dmadata *dmadata = param;
	char *devname;
	bool found = false;

	devname = kasprintf(GFP_KERNEL, "%s.%d", dmadata->dma_res->name,
		(int)dmadata->dma_res->start);
	if (strcmp(dev_name(chan->device->dev), devname) == 0) {
		chan->private = &dmadata->dma_slave;
		found = true;
	}
	kfree(devname);
	return found;
}

static int txx9aclc_dma_init(struct txx9aclc_soc_device *dev,
			     struct txx9aclc_dmadata *dmadata)
{
	struct txx9aclc_plat_drvdata *drvdata = txx9aclc_drvdata;
	struct txx9dmac_slave *ds = &dmadata->dma_slave;
	dma_cap_mask_t mask;

	spin_lock_init(&dmadata->dma_lock);

	ds->reg_width = sizeof(u32);
	if (dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ds->tx_reg = drvdata->physbase + ACAUDODAT;
		ds->rx_reg = 0;
	} else {
		ds->tx_reg = 0;
		ds->rx_reg = drvdata->physbase + ACAUDIDAT;
	}

	/* Try to grab a DMA channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dmadata->dma_chan = dma_request_channel(mask, filter, dmadata);
	if (!dmadata->dma_chan) {
		printk(KERN_ERR
			"DMA channel for %s is not available\n",
			dmadata->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			"playback" : "capture");
		return -EBUSY;
	}
	INIT_WORK(&dmadata->work, txx9aclc_dma_work);
	return 0;
}

static int txx9aclc_pcm_probe(struct snd_soc_component *component)
{
	snd_soc_component_set_drvdata(component, &txx9aclc_soc_device);
	return 0;
}

static void txx9aclc_pcm_remove(struct snd_soc_component *component)
{
	struct txx9aclc_soc_device *dev = snd_soc_component_get_drvdata(component);
	struct txx9aclc_plat_drvdata *drvdata = txx9aclc_drvdata;
	void __iomem *base = drvdata->base;
	int i;

	/* disable all FIFO DMAs */
	__raw_writel(ACCTL_AUDODMA | ACCTL_AUDIDMA, base + ACCTLDIS);
	/* dummy R/W to clear pending DMAREQ if any */
	__raw_writel(__raw_readl(base + ACAUDIDAT), base + ACAUDODAT);

	for (i = 0; i < 2; i++) {
		struct txx9aclc_dmadata *dmadata = &dev->dmadata[i];
		struct dma_chan *chan = dmadata->dma_chan;

		if (chan) {
			dmadata->frag_count = -1;
			dmaengine_terminate_all(chan);
			dma_release_channel(chan);
		}
		dev->dmadata[i].dma_chan = NULL;
	}
}

static const struct snd_soc_component_driver txx9aclc_soc_component = {
	.name		= DRV_NAME,
	.probe		= txx9aclc_pcm_probe,
	.remove		= txx9aclc_pcm_remove,
	.open		= txx9aclc_pcm_open,
	.close		= txx9aclc_pcm_close,
	.hw_params	= txx9aclc_pcm_hw_params,
	.prepare	= txx9aclc_pcm_prepare,
	.trigger	= txx9aclc_pcm_trigger,
	.pointer	= txx9aclc_pcm_pointer,
	.pcm_construct	= txx9aclc_pcm_new,
};

static int txx9aclc_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					&txx9aclc_soc_component, NULL, 0);
}

static struct platform_driver txx9aclc_pcm_driver = {
	.driver = {
			.name = "txx9aclc-pcm-audio",
	},

	.probe = txx9aclc_soc_platform_probe,
};

module_platform_driver(txx9aclc_pcm_driver);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("TXx9 ACLC Audio DMA driver");
MODULE_LICENSE("GPL");
