/*
 * Freescale MPC5200 PSC DMA
 * ALSA SoC Platform driver
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 * Copyright (C) 2009 Jon Smirl, Digispeaker
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/of_platform.h>

#include <sound/soc.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/gen_bd.h>
#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"

/*
 * Interrupt handlers
 */
static irqreturn_t psc_dma_status_irq(int irq, void *_psc_dma)
{
	struct psc_dma *psc_dma = _psc_dma;
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;
	u16 isr;

	isr = in_be16(&regs->mpc52xx_psc_isr);

	/* Playback underrun error */
	if (psc_dma->playback.active && (isr & MPC52xx_PSC_IMR_TXEMP))
		psc_dma->stats.underrun_count++;

	/* Capture overrun error */
	if (psc_dma->capture.active && (isr & MPC52xx_PSC_IMR_ORERR))
		psc_dma->stats.overrun_count++;

	out_8(&regs->command, MPC52xx_PSC_RST_ERR_STAT);

	return IRQ_HANDLED;
}

/**
 * psc_dma_bcom_enqueue_next_buffer - Enqueue another audio buffer
 * @s: pointer to stream private data structure
 *
 * Enqueues another audio period buffer into the bestcomm queue.
 *
 * Note: The routine must only be called when there is space available in
 * the queue.  Otherwise the enqueue will fail and the audio ring buffer
 * will get out of sync
 */
static void psc_dma_bcom_enqueue_next_buffer(struct psc_dma_stream *s)
{
	struct bcom_bd *bd;

	/* Prepare and enqueue the next buffer descriptor */
	bd = bcom_prepare_next_buffer(s->bcom_task);
	bd->status = s->period_bytes;
	bd->data[0] = s->runtime->dma_addr + (s->period_next * s->period_bytes);
	bcom_submit_next_buffer(s->bcom_task, NULL);

	/* Update for next period */
	s->period_next = (s->period_next + 1) % s->runtime->periods;
}

/* Bestcomm DMA irq handler */
static irqreturn_t psc_dma_bcom_irq(int irq, void *_psc_dma_stream)
{
	struct psc_dma_stream *s = _psc_dma_stream;

	spin_lock(&s->psc_dma->lock);
	/* For each finished period, dequeue the completed period buffer
	 * and enqueue a new one in it's place. */
	while (bcom_buffer_done(s->bcom_task)) {
		bcom_retrieve_buffer(s->bcom_task, NULL, NULL);

		s->period_current = (s->period_current+1) % s->runtime->periods;
		s->period_count++;

		psc_dma_bcom_enqueue_next_buffer(s);
	}
	spin_unlock(&s->psc_dma->lock);

	/* If the stream is active, then also inform the PCM middle layer
	 * of the period finished event. */
	if (s->active)
		snd_pcm_period_elapsed(s->stream);

	return IRQ_HANDLED;
}

static int psc_dma_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

/**
 * psc_dma_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int psc_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct psc_dma_stream *s = to_psc_dma_stream(substream, psc_dma);
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;
	u16 imr;
	unsigned long flags;
	int i;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_dbg(psc_dma->dev, "START: stream=%i fbits=%u ps=%u #p=%u\n",
			substream->pstr->stream, runtime->frame_bits,
			(int)runtime->period_size, runtime->periods);
		s->period_bytes = frames_to_bytes(runtime,
						  runtime->period_size);
		s->period_next = 0;
		s->period_current = 0;
		s->active = 1;
		s->period_count = 0;
		s->runtime = runtime;

		/* Fill up the bestcomm bd queue and enable DMA.
		 * This will begin filling the PSC's fifo.
		 */
		spin_lock_irqsave(&psc_dma->lock, flags);

		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
			bcom_gen_bd_rx_reset(s->bcom_task);
		else
			bcom_gen_bd_tx_reset(s->bcom_task);

		for (i = 0; i < runtime->periods; i++)
			if (!bcom_queue_full(s->bcom_task))
				psc_dma_bcom_enqueue_next_buffer(s);

		bcom_enable(s->bcom_task);
		spin_unlock_irqrestore(&psc_dma->lock, flags);

		out_8(&regs->command, MPC52xx_PSC_RST_ERR_STAT);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(psc_dma->dev, "STOP: stream=%i periods_count=%i\n",
			substream->pstr->stream, s->period_count);
		s->active = 0;

		spin_lock_irqsave(&psc_dma->lock, flags);
		bcom_disable(s->bcom_task);
		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
			bcom_gen_bd_rx_reset(s->bcom_task);
		else
			bcom_gen_bd_tx_reset(s->bcom_task);
		spin_unlock_irqrestore(&psc_dma->lock, flags);

		break;

	default:
		dev_dbg(psc_dma->dev, "unhandled trigger: stream=%i cmd=%i\n",
			substream->pstr->stream, cmd);
		return -EINVAL;
	}

	/* Update interrupt enable settings */
	imr = 0;
	if (psc_dma->playback.active)
		imr |= MPC52xx_PSC_IMR_TXEMP;
	if (psc_dma->capture.active)
		imr |= MPC52xx_PSC_IMR_ORERR;
	out_be16(&regs->isr_imr.imr, psc_dma->imr | imr);

	return 0;
}


/* ---------------------------------------------------------------------
 * The PSC DMA 'ASoC platform' driver
 *
 * Can be referenced by an 'ASoC machine' driver
 * This driver only deals with the audio bus; it doesn't have any
 * interaction with the attached codec
 */

static const struct snd_pcm_hardware psc_dma_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE |
		SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S32_BE,
	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.period_bytes_max	= 1024 * 1024,
	.period_bytes_min	= 32,
	.periods_min		= 2,
	.periods_max		= 256,
	.buffer_bytes_max	= 2 * 1024 * 1024,
	.fifo_size		= 512,
};

static int psc_dma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct psc_dma_stream *s;
	int rc;

	dev_dbg(psc_dma->dev, "psc_dma_open(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	snd_soc_set_runtime_hwparams(substream, &psc_dma_hardware);

	rc = snd_pcm_hw_constraint_integer(runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
	if (rc < 0) {
		dev_err(substream->pcm->card->dev, "invalid buffer size\n");
		return rc;
	}

	s->stream = substream;
	return 0;
}

static int psc_dma_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct psc_dma_stream *s;

	dev_dbg(psc_dma->dev, "psc_dma_close(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	if (!psc_dma->playback.active &&
	    !psc_dma->capture.active) {

		/* Disable all interrupts and reset the PSC */
		out_be16(&psc_dma->psc_regs->isr_imr.imr, psc_dma->imr);
		out_8(&psc_dma->psc_regs->command, 4 << 4); /* reset error */
	}
	s->stream = NULL;
	return 0;
}

static snd_pcm_uframes_t
psc_dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	struct psc_dma_stream *s;
	dma_addr_t count;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	count = s->period_current * s->period_bytes;

	return bytes_to_frames(substream->runtime, count);
}

static int
psc_dma_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static struct snd_pcm_ops psc_dma_ops = {
	.open		= psc_dma_open,
	.close		= psc_dma_close,
	.hw_free	= psc_dma_hw_free,
	.ioctl		= snd_pcm_lib_ioctl,
	.pointer	= psc_dma_pointer,
	.trigger	= psc_dma_trigger,
	.hw_params	= psc_dma_hw_params,
};

static u64 psc_dma_dmamask = 0xffffffff;
static int psc_dma_new(struct snd_card *card, struct snd_soc_dai *dai,
			   struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct psc_dma *psc_dma = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	size_t size = psc_dma_hardware.buffer_bytes_max;
	int rc = 0;

	dev_dbg(rtd->platform->dev, "psc_dma_new(card=%p, dai=%p, pcm=%p)\n",
		card, dai, pcm);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &psc_dma_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (pcm->streams[0].substream) {
		rc = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->card->dev,
				size, &pcm->streams[0].substream->dma_buffer);
		if (rc)
			goto playback_alloc_err;
	}

	if (pcm->streams[1].substream) {
		rc = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->card->dev,
				size, &pcm->streams[1].substream->dma_buffer);
		if (rc)
			goto capture_alloc_err;
	}

	if (rtd->codec->ac97)
		rtd->codec->ac97->private_data = psc_dma;

	return 0;

 capture_alloc_err:
	if (pcm->streams[0].substream)
		snd_dma_free_pages(&pcm->streams[0].substream->dma_buffer);

 playback_alloc_err:
	dev_err(card->dev, "Cannot allocate buffer(s)\n");

	return -ENOMEM;
}

static void psc_dma_free(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct snd_pcm_substream *substream;
	int stream;

	dev_dbg(rtd->platform->dev, "psc_dma_free(pcm=%p)\n", pcm);

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (substream) {
			snd_dma_free_pages(&substream->dma_buffer);
			substream->dma_buffer.area = NULL;
			substream->dma_buffer.addr = 0;
		}
	}
}

static struct snd_soc_platform_driver mpc5200_audio_dma_platform = {
	.ops		= &psc_dma_ops,
	.pcm_new	= &psc_dma_new,
	.pcm_free	= &psc_dma_free,
};

static int mpc5200_hpcd_probe(struct of_device *op,
		const struct of_device_id *match)
{
	phys_addr_t fifo;
	struct psc_dma *psc_dma;
	struct resource res;
	int size, irq, rc;
	const __be32 *prop;
	void __iomem *regs;
	int ret;

	/* Fetch the registers and IRQ of the PSC */
	irq = irq_of_parse_and_map(op->dev.of_node, 0);
	if (of_address_to_resource(op->dev.of_node, 0, &res)) {
		dev_err(&op->dev, "Missing reg property\n");
		return -ENODEV;
	}
	regs = ioremap(res.start, 1 + res.end - res.start);
	if (!regs) {
		dev_err(&op->dev, "Could not map registers\n");
		return -ENODEV;
	}

	/* Allocate and initialize the driver private data */
	psc_dma = kzalloc(sizeof *psc_dma, GFP_KERNEL);
	if (!psc_dma) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	/* Get the PSC ID */
	prop = of_get_property(op->dev.of_node, "cell-index", &size);
	if (!prop || size < sizeof *prop) {
		ret = -ENODEV;
		goto out_free;
	}

	spin_lock_init(&psc_dma->lock);
	mutex_init(&psc_dma->mutex);
	psc_dma->id = be32_to_cpu(*prop);
	psc_dma->irq = irq;
	psc_dma->psc_regs = regs;
	psc_dma->fifo_regs = regs + sizeof *psc_dma->psc_regs;
	psc_dma->dev = &op->dev;
	psc_dma->playback.psc_dma = psc_dma;
	psc_dma->capture.psc_dma = psc_dma;
	snprintf(psc_dma->name, sizeof psc_dma->name, "PSC%u", psc_dma->id);

	/* Find the address of the fifo data registers and setup the
	 * DMA tasks */
	fifo = res.start + offsetof(struct mpc52xx_psc, buffer.buffer_32);
	psc_dma->capture.bcom_task =
		bcom_psc_gen_bd_rx_init(psc_dma->id, 10, fifo, 512);
	psc_dma->playback.bcom_task =
		bcom_psc_gen_bd_tx_init(psc_dma->id, 10, fifo);
	if (!psc_dma->capture.bcom_task ||
	    !psc_dma->playback.bcom_task) {
		dev_err(&op->dev, "Could not allocate bestcomm tasks\n");
		ret = -ENODEV;
		goto out_free;
	}

	/* Disable all interrupts and reset the PSC */
	out_be16(&psc_dma->psc_regs->isr_imr.imr, psc_dma->imr);
	 /* reset receiver */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_RX);
	 /* reset transmitter */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_TX);
	 /* reset error */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_RST_ERR_STAT);
	 /* reset mode */
	out_8(&psc_dma->psc_regs->command, MPC52xx_PSC_SEL_MODE_REG_1);

	/* Set up mode register;
	 * First write: RxRdy (FIFO Alarm) generates rx FIFO irq
	 * Second write: register Normal mode for non loopback
	 */
	out_8(&psc_dma->psc_regs->mode, 0);
	out_8(&psc_dma->psc_regs->mode, 0);

	/* Set the TX and RX fifo alarm thresholds */
	out_be16(&psc_dma->fifo_regs->rfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->rfcntl, 0x4);
	out_be16(&psc_dma->fifo_regs->tfalarm, 0x100);
	out_8(&psc_dma->fifo_regs->tfcntl, 0x7);

	/* Lookup the IRQ numbers */
	psc_dma->playback.irq =
		bcom_get_task_irq(psc_dma->playback.bcom_task);
	psc_dma->capture.irq =
		bcom_get_task_irq(psc_dma->capture.bcom_task);

	rc = request_irq(psc_dma->irq, &psc_dma_status_irq, IRQF_SHARED,
			 "psc-dma-status", psc_dma);
	rc |= request_irq(psc_dma->capture.irq, &psc_dma_bcom_irq, IRQF_SHARED,
			  "psc-dma-capture", &psc_dma->capture);
	rc |= request_irq(psc_dma->playback.irq, &psc_dma_bcom_irq, IRQF_SHARED,
			  "psc-dma-playback", &psc_dma->playback);
	if (rc) {
		ret = -ENODEV;
		goto out_irq;
	}

	/* Save what we've done so it can be found again later */
	dev_set_drvdata(&op->dev, psc_dma);

	/* Tell the ASoC OF helpers about it */
	return snd_soc_register_platform(&op->dev, &mpc5200_audio_dma_platform);
out_irq:
	free_irq(psc_dma->irq, psc_dma);
	free_irq(psc_dma->capture.irq, &psc_dma->capture);
	free_irq(psc_dma->playback.irq, &psc_dma->playback);
out_free:
	kfree(psc_dma);
out_unmap:
	iounmap(regs);
	return ret;
}

static int mpc5200_hpcd_remove(struct of_device *op)
{
	struct psc_dma *psc_dma = dev_get_drvdata(&op->dev);

	dev_dbg(&op->dev, "mpc5200_audio_dma_destroy()\n");

	snd_soc_unregister_platform(&op->dev);

	bcom_gen_bd_rx_release(psc_dma->capture.bcom_task);
	bcom_gen_bd_tx_release(psc_dma->playback.bcom_task);

	/* Release irqs */
	free_irq(psc_dma->irq, psc_dma);
	free_irq(psc_dma->capture.irq, &psc_dma->capture);
	free_irq(psc_dma->playback.irq, &psc_dma->playback);

	iounmap(psc_dma->psc_regs);
	kfree(psc_dma);
	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

static struct of_device_id mpc5200_hpcd_match[] = {
	{
		.compatible = "fsl,mpc5200-pcm",
	},
	{}
};
MODULE_DEVICE_TABLE(of, mpc5200_hpcd_match);

static struct of_platform_driver mpc5200_hpcd_of_driver = {
	.owner		= THIS_MODULE,
	.name		= "mpc5200-pcm-audio",
	.match_table    = mpc5200_hpcd_match,
	.probe		= mpc5200_hpcd_probe,
	.remove		= mpc5200_hpcd_remove,
};

static int __init mpc5200_hpcd_init(void)
{
	return of_register_platform_driver(&mpc5200_hpcd_of_driver);
}

static void __exit mpc5200_hpcd_exit(void)
{
	of_unregister_platform_driver(&mpc5200_hpcd_of_driver);
}

module_init(mpc5200_hpcd_init);
module_exit(mpc5200_hpcd_exit);

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("Freescale MPC5200 PSC in DMA mode ASoC Driver");
MODULE_LICENSE("GPL");
