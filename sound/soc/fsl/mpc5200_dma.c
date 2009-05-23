/*
 * Freescale MPC5200 PSC DMA
 * ALSA SoC Platform driver
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-of-simple.h>

#include <sysdev/bestcomm/bestcomm.h>
#include <sysdev/bestcomm/gen_bd.h>
#include <asm/mpc52xx_psc.h>

#include "mpc5200_dma.h"

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("Freescale MPC5200 PSC in DMA mode ASoC Driver");
MODULE_LICENSE("GPL");

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

	out_8(&regs->command, 4 << 4);	/* reset the error status */

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
	bd->data[0] = s->period_next_pt;
	bcom_submit_next_buffer(s->bcom_task, NULL);

	/* Update for next period */
	s->period_next_pt += s->period_bytes;
	if (s->period_next_pt >= s->period_end)
		s->period_next_pt = s->period_start;
}

/* Bestcomm DMA irq handler */
static irqreturn_t psc_dma_bcom_irq(int irq, void *_psc_dma_stream)
{
	struct psc_dma_stream *s = _psc_dma_stream;

	/* For each finished period, dequeue the completed period buffer
	 * and enqueue a new one in it's place. */
	while (bcom_buffer_done(s->bcom_task)) {
		bcom_retrieve_buffer(s->bcom_task, NULL, NULL);
		s->period_current_pt += s->period_bytes;
		if (s->period_current_pt >= s->period_end)
			s->period_current_pt = s->period_start;
		psc_dma_bcom_enqueue_next_buffer(s);
		bcom_enable(s->bcom_task);
	}

	/* If the stream is active, then also inform the PCM middle layer
	 * of the period finished event. */
	if (s->active)
		snd_pcm_period_elapsed(s->stream);

	return IRQ_HANDLED;
}

/**
 * psc_dma_startup: create a new substream
 *
 * This is the first function called when a stream is opened.
 *
 * If this is the first stream open, then grab the IRQ and program most of
 * the PSC registers.
 */
int psc_dma_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	int rc;

	dev_dbg(psc_dma->dev, "psc_dma_startup(substream=%p)\n", substream);

	if (!psc_dma->playback.active &&
	    !psc_dma->capture.active) {
		/* Setup the IRQs */
		rc = request_irq(psc_dma->irq, &psc_dma_status_irq, IRQF_SHARED,
				 "psc-dma-status", psc_dma);
		rc |= request_irq(psc_dma->capture.irq,
				  &psc_dma_bcom_irq, IRQF_SHARED,
				  "psc-dma-capture", &psc_dma->capture);
		rc |= request_irq(psc_dma->playback.irq,
				  &psc_dma_bcom_irq, IRQF_SHARED,
				  "psc-dma-playback", &psc_dma->playback);
		if (rc) {
			free_irq(psc_dma->irq, psc_dma);
			free_irq(psc_dma->capture.irq,
				 &psc_dma->capture);
			free_irq(psc_dma->playback.irq,
				 &psc_dma->playback);
			return -ENODEV;
		}
	}

	return 0;
}

int psc_dma_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
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
int psc_dma_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct psc_dma_stream *s;
	struct mpc52xx_psc __iomem *regs = psc_dma->psc_regs;
	u16 imr;
	u8 psc_cmd;
	unsigned long flags;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	dev_dbg(psc_dma->dev, "psc_dma_trigger(substream=%p, cmd=%i)"
		" stream_id=%i\n",
		substream, cmd, substream->pstr->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		s->period_bytes = frames_to_bytes(runtime,
						  runtime->period_size);
		s->period_start = virt_to_phys(runtime->dma_area);
		s->period_end = s->period_start +
				(s->period_bytes * runtime->periods);
		s->period_next_pt = s->period_start;
		s->period_current_pt = s->period_start;
		s->active = 1;

		/* First; reset everything */
		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE) {
			out_8(&regs->command, MPC52xx_PSC_RST_RX);
			out_8(&regs->command, MPC52xx_PSC_RST_ERR_STAT);
		} else {
			out_8(&regs->command, MPC52xx_PSC_RST_TX);
			out_8(&regs->command, MPC52xx_PSC_RST_ERR_STAT);
		}

		/* Next, fill up the bestcomm bd queue and enable DMA.
		 * This will begin filling the PSC's fifo. */
		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
			bcom_gen_bd_rx_reset(s->bcom_task);
		else
			bcom_gen_bd_tx_reset(s->bcom_task);
		while (!bcom_queue_full(s->bcom_task))
			psc_dma_bcom_enqueue_next_buffer(s);
		bcom_enable(s->bcom_task);

		/* Due to errata in the dma mode; need to line up enabling
		 * the transmitter with a transition on the frame sync
		 * line */

		spin_lock_irqsave(&psc_dma->lock, flags);
		/* first make sure it is low */
		while ((in_8(&regs->ipcr_acr.ipcr) & 0x80) != 0)
			;
		/* then wait for the transition to high */
		while ((in_8(&regs->ipcr_acr.ipcr) & 0x80) == 0)
			;
		/* Finally, enable the PSC.
		 * Receiver must always be enabled; even when we only want
		 * transmit.  (see 15.3.2.3 of MPC5200B User's Guide) */
		psc_cmd = MPC52xx_PSC_RX_ENABLE;
		if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK)
			psc_cmd |= MPC52xx_PSC_TX_ENABLE;
		out_8(&regs->command, psc_cmd);
		spin_unlock_irqrestore(&psc_dma->lock, flags);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* Turn off the PSC */
		s->active = 0;
		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE) {
			if (!psc_dma->playback.active) {
				out_8(&regs->command, 2 << 4);	/* reset rx */
				out_8(&regs->command, 3 << 4);	/* reset tx */
				out_8(&regs->command, 4 << 4);	/* reset err */
			}
		} else {
			out_8(&regs->command, 3 << 4);	/* reset tx */
			out_8(&regs->command, 4 << 4);	/* reset err */
			if (!psc_dma->capture.active)
				out_8(&regs->command, 2 << 4);	/* reset rx */
		}

		bcom_disable(s->bcom_task);
		while (!bcom_queue_empty(s->bcom_task))
			bcom_retrieve_buffer(s->bcom_task, NULL, NULL);

		break;

	default:
		dev_dbg(psc_dma->dev, "invalid command\n");
		return -EINVAL;
	}

	/* Update interrupt enable settings */
	imr = 0;
	if (psc_dma->playback.active)
		imr |= MPC52xx_PSC_IMR_TXEMP;
	if (psc_dma->capture.active)
		imr |= MPC52xx_PSC_IMR_ORERR;
	out_be16(&regs->isr_imr.imr, imr);

	return 0;
}

/**
 * psc_dma_shutdown: shutdown the data transfer on a stream
 *
 * Shutdown the PSC if there are no other substreams open.
 */
void psc_dma_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;

	dev_dbg(psc_dma->dev, "psc_dma_shutdown(substream=%p)\n", substream);

	/*
	 * If this is the last active substream, disable the PSC and release
	 * the IRQ.
	 */
	if (!psc_dma->playback.active &&
	    !psc_dma->capture.active) {

		/* Disable all interrupts and reset the PSC */
		out_be16(&psc_dma->psc_regs->isr_imr.imr, 0);
		out_8(&psc_dma->psc_regs->command, 3 << 4); /* reset tx */
		out_8(&psc_dma->psc_regs->command, 2 << 4); /* reset rx */
		out_8(&psc_dma->psc_regs->command, 1 << 4); /* reset mode */
		out_8(&psc_dma->psc_regs->command, 4 << 4); /* reset error */

		/* Release irqs */
		free_irq(psc_dma->irq, psc_dma);
		free_irq(psc_dma->capture.irq, &psc_dma->capture);
		free_irq(psc_dma->playback.irq, &psc_dma->playback);
	}
}

/* ---------------------------------------------------------------------
 * The PSC DMA 'ASoC platform' driver
 *
 * Can be referenced by an 'ASoC machine' driver
 * This driver only deals with the audio bus; it doesn't have any
 * interaction with the attached codec
 */

static const struct snd_pcm_hardware psc_dma_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE |
		   SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S32_BE,
	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_max	= 1024 * 1024,
	.period_bytes_min	= 32,
	.periods_min		= 2,
	.periods_max		= 256,
	.buffer_bytes_max	= 2 * 1024 * 1024,
	.fifo_size		= 0,
};

static int psc_dma_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	struct psc_dma_stream *s;

	dev_dbg(psc_dma->dev, "psc_dma_pcm_open(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	snd_soc_set_runtime_hwparams(substream, &psc_dma_pcm_hardware);

	s->stream = substream;
	return 0;
}

static int psc_dma_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	struct psc_dma_stream *s;

	dev_dbg(psc_dma->dev, "psc_dma_pcm_close(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	s->stream = NULL;
	return 0;
}

static snd_pcm_uframes_t
psc_dma_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_dma *psc_dma = rtd->dai->cpu_dai->private_data;
	struct psc_dma_stream *s;
	dma_addr_t count;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_dma->capture;
	else
		s = &psc_dma->playback;

	count = s->period_current_pt - s->period_start;

	return bytes_to_frames(substream->runtime, count);
}

static struct snd_pcm_ops psc_dma_pcm_ops = {
	.open		= psc_dma_pcm_open,
	.close		= psc_dma_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.pointer	= psc_dma_pcm_pointer,
};

static u64 psc_dma_pcm_dmamask = 0xffffffff;
static int psc_dma_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
			   struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	size_t size = psc_dma_pcm_hardware.buffer_bytes_max;
	int rc = 0;

	dev_dbg(rtd->socdev->dev, "psc_dma_pcm_new(card=%p, dai=%p, pcm=%p)\n",
		card, dai, pcm);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &psc_dma_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (pcm->streams[0].substream) {
		rc = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->dev, size,
					&pcm->streams[0].substream->dma_buffer);
		if (rc)
			goto playback_alloc_err;
	}

	if (pcm->streams[1].substream) {
		rc = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pcm->dev, size,
					&pcm->streams[1].substream->dma_buffer);
		if (rc)
			goto capture_alloc_err;
	}

	return 0;

 capture_alloc_err:
	if (pcm->streams[0].substream)
		snd_dma_free_pages(&pcm->streams[0].substream->dma_buffer);
 playback_alloc_err:
	dev_err(card->dev, "Cannot allocate buffer(s)\n");
	return -ENOMEM;
}

static void psc_dma_pcm_free(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct snd_pcm_substream *substream;
	int stream;

	dev_dbg(rtd->socdev->dev, "psc_dma_pcm_free(pcm=%p)\n", pcm);

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (substream) {
			snd_dma_free_pages(&substream->dma_buffer);
			substream->dma_buffer.area = NULL;
			substream->dma_buffer.addr = 0;
		}
	}
}

struct snd_soc_platform psc_dma_pcm_soc_platform = {
	.name		= "mpc5200-psc-audio",
	.pcm_ops	= &psc_dma_pcm_ops,
	.pcm_new	= &psc_dma_pcm_new,
	.pcm_free	= &psc_dma_pcm_free,
};

