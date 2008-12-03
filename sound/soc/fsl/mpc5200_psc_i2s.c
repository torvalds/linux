/*
 * Freescale MPC5200 PSC in I2S mode
 * ALSA SoC Digital Audio Interface (DAI) driver
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

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("Freescale MPC5200 PSC in I2S mode ASoC Driver");
MODULE_LICENSE("GPL");

/**
 * PSC_I2S_RATES: sample rates supported by the I2S
 *
 * This driver currently only supports the PSC running in I2S slave mode,
 * which means the codec determines the sample rate.  Therefore, we tell
 * ALSA that we support all rates and let the codec driver decide what rates
 * are really supported.
 */
#define PSC_I2S_RATES (SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_192000 | \
			SNDRV_PCM_RATE_CONTINUOUS)

/**
 * PSC_I2S_FORMATS: audio formats supported by the PSC I2S mode
 */
#define PSC_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_BE | \
			 SNDRV_PCM_FMTBIT_S24_BE | SNDRV_PCM_FMTBIT_S24_BE | \
			 SNDRV_PCM_FMTBIT_S32_BE)

/**
 * psc_i2s_stream - Data specific to a single stream (playback or capture)
 * @active:		flag indicating if the stream is active
 * @psc_i2s:		pointer back to parent psc_i2s data structure
 * @bcom_task:		bestcomm task structure
 * @irq:		irq number for bestcomm task
 * @period_start:	physical address of start of DMA region
 * @period_end:		physical address of end of DMA region
 * @period_next_pt:	physical address of next DMA buffer to enqueue
 * @period_bytes:	size of DMA period in bytes
 */
struct psc_i2s_stream {
	int active;
	struct psc_i2s *psc_i2s;
	struct bcom_task *bcom_task;
	int irq;
	struct snd_pcm_substream *stream;
	dma_addr_t period_start;
	dma_addr_t period_end;
	dma_addr_t period_next_pt;
	dma_addr_t period_current_pt;
	int period_bytes;
};

/**
 * psc_i2s - Private driver data
 * @name: short name for this device ("PSC0", "PSC1", etc)
 * @psc_regs: pointer to the PSC's registers
 * @fifo_regs: pointer to the PSC's FIFO registers
 * @irq: IRQ of this PSC
 * @dev: struct device pointer
 * @dai: the CPU DAI for this device
 * @sicr: Base value used in serial interface control register; mode is ORed
 *        with this value.
 * @playback: Playback stream context data
 * @capture: Capture stream context data
 */
struct psc_i2s {
	char name[32];
	struct mpc52xx_psc __iomem *psc_regs;
	struct mpc52xx_psc_fifo __iomem *fifo_regs;
	unsigned int irq;
	struct device *dev;
	struct snd_soc_dai dai;
	spinlock_t lock;
	u32 sicr;

	/* per-stream data */
	struct psc_i2s_stream playback;
	struct psc_i2s_stream capture;

	/* Statistics */
	struct {
		int overrun_count;
		int underrun_count;
	} stats;
};

/*
 * Interrupt handlers
 */
static irqreturn_t psc_i2s_status_irq(int irq, void *_psc_i2s)
{
	struct psc_i2s *psc_i2s = _psc_i2s;
	struct mpc52xx_psc __iomem *regs = psc_i2s->psc_regs;
	u16 isr;

	isr = in_be16(&regs->mpc52xx_psc_isr);

	/* Playback underrun error */
	if (psc_i2s->playback.active && (isr & MPC52xx_PSC_IMR_TXEMP))
		psc_i2s->stats.underrun_count++;

	/* Capture overrun error */
	if (psc_i2s->capture.active && (isr & MPC52xx_PSC_IMR_ORERR))
		psc_i2s->stats.overrun_count++;

	out_8(&regs->command, 4 << 4);	/* reset the error status */

	return IRQ_HANDLED;
}

/**
 * psc_i2s_bcom_enqueue_next_buffer - Enqueue another audio buffer
 * @s: pointer to stream private data structure
 *
 * Enqueues another audio period buffer into the bestcomm queue.
 *
 * Note: The routine must only be called when there is space available in
 * the queue.  Otherwise the enqueue will fail and the audio ring buffer
 * will get out of sync
 */
static void psc_i2s_bcom_enqueue_next_buffer(struct psc_i2s_stream *s)
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
static irqreturn_t psc_i2s_bcom_irq(int irq, void *_psc_i2s_stream)
{
	struct psc_i2s_stream *s = _psc_i2s_stream;

	/* For each finished period, dequeue the completed period buffer
	 * and enqueue a new one in it's place. */
	while (bcom_buffer_done(s->bcom_task)) {
		bcom_retrieve_buffer(s->bcom_task, NULL, NULL);
		s->period_current_pt += s->period_bytes;
		if (s->period_current_pt >= s->period_end)
			s->period_current_pt = s->period_start;
		psc_i2s_bcom_enqueue_next_buffer(s);
		bcom_enable(s->bcom_task);
	}

	/* If the stream is active, then also inform the PCM middle layer
	 * of the period finished event. */
	if (s->active)
		snd_pcm_period_elapsed(s->stream);

	return IRQ_HANDLED;
}

/**
 * psc_i2s_startup: create a new substream
 *
 * This is the first function called when a stream is opened.
 *
 * If this is the first stream open, then grab the IRQ and program most of
 * the PSC registers.
 */
static int psc_i2s_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	int rc;

	dev_dbg(psc_i2s->dev, "psc_i2s_startup(substream=%p)\n", substream);

	if (!psc_i2s->playback.active &&
	    !psc_i2s->capture.active) {
		/* Setup the IRQs */
		rc = request_irq(psc_i2s->irq, &psc_i2s_status_irq, IRQF_SHARED,
				 "psc-i2s-status", psc_i2s);
		rc |= request_irq(psc_i2s->capture.irq,
				  &psc_i2s_bcom_irq, IRQF_SHARED,
				  "psc-i2s-capture", &psc_i2s->capture);
		rc |= request_irq(psc_i2s->playback.irq,
				  &psc_i2s_bcom_irq, IRQF_SHARED,
				  "psc-i2s-playback", &psc_i2s->playback);
		if (rc) {
			free_irq(psc_i2s->irq, psc_i2s);
			free_irq(psc_i2s->capture.irq,
				 &psc_i2s->capture);
			free_irq(psc_i2s->playback.irq,
				 &psc_i2s->playback);
			return -ENODEV;
		}
	}

	return 0;
}

static int psc_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	u32 mode;

	dev_dbg(psc_i2s->dev, "%s(substream=%p) p_size=%i p_bytes=%i"
		" periods=%i buffer_size=%i  buffer_bytes=%i\n",
		__func__, substream, params_period_size(params),
		params_period_bytes(params), params_periods(params),
		params_buffer_size(params), params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_8;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_16;
		break;
	case SNDRV_PCM_FORMAT_S24_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_24;
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		mode = MPC52xx_PSC_SICR_SIM_CODEC_32;
		break;
	default:
		dev_dbg(psc_i2s->dev, "invalid format\n");
		return -EINVAL;
	}
	out_be32(&psc_i2s->psc_regs->sicr, psc_i2s->sicr | mode);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int psc_i2s_hw_free(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

/**
 * psc_i2s_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int psc_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct psc_i2s_stream *s;
	struct mpc52xx_psc __iomem *regs = psc_i2s->psc_regs;
	u16 imr;
	u8 psc_cmd;
	unsigned long flags;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_i2s->capture;
	else
		s = &psc_i2s->playback;

	dev_dbg(psc_i2s->dev, "psc_i2s_trigger(substream=%p, cmd=%i)"
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
			psc_i2s_bcom_enqueue_next_buffer(s);
		bcom_enable(s->bcom_task);

		/* Due to errata in the i2s mode; need to line up enabling
		 * the transmitter with a transition on the frame sync
		 * line */

		spin_lock_irqsave(&psc_i2s->lock, flags);
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
		spin_unlock_irqrestore(&psc_i2s->lock, flags);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
		/* Turn off the PSC */
		s->active = 0;
		if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE) {
			if (!psc_i2s->playback.active) {
				out_8(&regs->command, 2 << 4);	/* reset rx */
				out_8(&regs->command, 3 << 4);	/* reset tx */
				out_8(&regs->command, 4 << 4);	/* reset err */
			}
		} else {
			out_8(&regs->command, 3 << 4);	/* reset tx */
			out_8(&regs->command, 4 << 4);	/* reset err */
			if (!psc_i2s->capture.active)
				out_8(&regs->command, 2 << 4);	/* reset rx */
		}

		bcom_disable(s->bcom_task);
		while (!bcom_queue_empty(s->bcom_task))
			bcom_retrieve_buffer(s->bcom_task, NULL, NULL);

		break;

	default:
		dev_dbg(psc_i2s->dev, "invalid command\n");
		return -EINVAL;
	}

	/* Update interrupt enable settings */
	imr = 0;
	if (psc_i2s->playback.active)
		imr |= MPC52xx_PSC_IMR_TXEMP;
	if (psc_i2s->capture.active)
		imr |= MPC52xx_PSC_IMR_ORERR;
	out_be16(&regs->isr_imr.imr, imr);

	return 0;
}

/**
 * psc_i2s_shutdown: shutdown the data transfer on a stream
 *
 * Shutdown the PSC if there are no other substreams open.
 */
static void psc_i2s_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;

	dev_dbg(psc_i2s->dev, "psc_i2s_shutdown(substream=%p)\n", substream);

	/*
	 * If this is the last active substream, disable the PSC and release
	 * the IRQ.
	 */
	if (!psc_i2s->playback.active &&
	    !psc_i2s->capture.active) {

		/* Disable all interrupts and reset the PSC */
		out_be16(&psc_i2s->psc_regs->isr_imr.imr, 0);
		out_8(&psc_i2s->psc_regs->command, 3 << 4); /* reset tx */
		out_8(&psc_i2s->psc_regs->command, 2 << 4); /* reset rx */
		out_8(&psc_i2s->psc_regs->command, 1 << 4); /* reset mode */
		out_8(&psc_i2s->psc_regs->command, 4 << 4); /* reset error */

		/* Release irqs */
		free_irq(psc_i2s->irq, psc_i2s);
		free_irq(psc_i2s->capture.irq, &psc_i2s->capture);
		free_irq(psc_i2s->playback.irq, &psc_i2s->playback);
	}
}

/**
 * psc_i2s_set_sysclk: set the clock frequency and direction
 *
 * This function is called by the machine driver to tell us what the clock
 * frequency and direction are.
 *
 * Currently, we only support operating as a clock slave (SND_SOC_CLOCK_IN),
 * and we don't care about the frequency.  Return an error if the direction
 * is not SND_SOC_CLOCK_IN.
 *
 * @clk_id: reserved, should be zero
 * @freq: the frequency of the given clock ID, currently ignored
 * @dir: SND_SOC_CLOCK_IN (clock slave) or SND_SOC_CLOCK_OUT (clock master)
 */
static int psc_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
			      int clk_id, unsigned int freq, int dir)
{
	struct psc_i2s *psc_i2s = cpu_dai->private_data;
	dev_dbg(psc_i2s->dev, "psc_i2s_set_sysclk(cpu_dai=%p, dir=%i)\n",
				cpu_dai, dir);
	return (dir == SND_SOC_CLOCK_IN) ? 0 : -EINVAL;
}

/**
 * psc_i2s_set_fmt: set the serial format.
 *
 * This function is called by the machine driver to tell us what serial
 * format to use.
 *
 * This driver only supports I2S mode.  Return an error if the format is
 * not SND_SOC_DAIFMT_I2S.
 *
 * @format: one of SND_SOC_DAIFMT_xxx
 */
static int psc_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int format)
{
	struct psc_i2s *psc_i2s = cpu_dai->private_data;
	dev_dbg(psc_i2s->dev, "psc_i2s_set_fmt(cpu_dai=%p, format=%i)\n",
				cpu_dai, format);
	return (format == SND_SOC_DAIFMT_I2S) ? 0 : -EINVAL;
}

/* ---------------------------------------------------------------------
 * ALSA SoC Bindings
 *
 * - Digital Audio Interface (DAI) template
 * - create/destroy dai hooks
 */

/**
 * psc_i2s_dai_template: template CPU Digital Audio Interface
 */
static struct snd_soc_dai psc_i2s_dai_template = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PSC_I2S_RATES,
		.formats = PSC_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = PSC_I2S_RATES,
		.formats = PSC_I2S_FORMATS,
	},
	.ops = {
		.startup = psc_i2s_startup,
		.hw_params = psc_i2s_hw_params,
		.hw_free = psc_i2s_hw_free,
		.shutdown = psc_i2s_shutdown,
		.trigger = psc_i2s_trigger,
		.set_sysclk = psc_i2s_set_sysclk,
		.set_fmt = psc_i2s_set_fmt,
	},
};

/* ---------------------------------------------------------------------
 * The PSC I2S 'ASoC platform' driver
 *
 * Can be referenced by an 'ASoC machine' driver
 * This driver only deals with the audio bus; it doesn't have any
 * interaction with the attached codec
 */

static const struct snd_pcm_hardware psc_i2s_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
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

static int psc_i2s_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	struct psc_i2s_stream *s;

	dev_dbg(psc_i2s->dev, "psc_i2s_pcm_open(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_i2s->capture;
	else
		s = &psc_i2s->playback;

	snd_soc_set_runtime_hwparams(substream, &psc_i2s_pcm_hardware);

	s->stream = substream;
	return 0;
}

static int psc_i2s_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	struct psc_i2s_stream *s;

	dev_dbg(psc_i2s->dev, "psc_i2s_pcm_close(substream=%p)\n", substream);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_i2s->capture;
	else
		s = &psc_i2s->playback;

	s->stream = NULL;
	return 0;
}

static snd_pcm_uframes_t
psc_i2s_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct psc_i2s *psc_i2s = rtd->dai->cpu_dai->private_data;
	struct psc_i2s_stream *s;
	dma_addr_t count;

	if (substream->pstr->stream == SNDRV_PCM_STREAM_CAPTURE)
		s = &psc_i2s->capture;
	else
		s = &psc_i2s->playback;

	count = s->period_current_pt - s->period_start;

	return bytes_to_frames(substream->runtime, count);
}

static struct snd_pcm_ops psc_i2s_pcm_ops = {
	.open		= psc_i2s_pcm_open,
	.close		= psc_i2s_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.pointer	= psc_i2s_pcm_pointer,
};

static u64 psc_i2s_pcm_dmamask = 0xffffffff;
static int psc_i2s_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
			   struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	size_t size = psc_i2s_pcm_hardware.buffer_bytes_max;
	int rc = 0;

	dev_dbg(rtd->socdev->dev, "psc_i2s_pcm_new(card=%p, dai=%p, pcm=%p)\n",
		card, dai, pcm);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &psc_i2s_pcm_dmamask;
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

static void psc_i2s_pcm_free(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = pcm->private_data;
	struct snd_pcm_substream *substream;
	int stream;

	dev_dbg(rtd->socdev->dev, "psc_i2s_pcm_free(pcm=%p)\n", pcm);

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (substream) {
			snd_dma_free_pages(&substream->dma_buffer);
			substream->dma_buffer.area = NULL;
			substream->dma_buffer.addr = 0;
		}
	}
}

struct snd_soc_platform psc_i2s_pcm_soc_platform = {
	.name		= "mpc5200-psc-audio",
	.pcm_ops	= &psc_i2s_pcm_ops,
	.pcm_new	= &psc_i2s_pcm_new,
	.pcm_free	= &psc_i2s_pcm_free,
};

/* ---------------------------------------------------------------------
 * Sysfs attributes for debugging
 */

static ssize_t psc_i2s_status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct psc_i2s *psc_i2s = dev_get_drvdata(dev);

	return sprintf(buf, "status=%.4x sicr=%.8x rfnum=%i rfstat=0x%.4x "
			"tfnum=%i tfstat=0x%.4x\n",
			in_be16(&psc_i2s->psc_regs->sr_csr.status),
			in_be32(&psc_i2s->psc_regs->sicr),
			in_be16(&psc_i2s->fifo_regs->rfnum) & 0x1ff,
			in_be16(&psc_i2s->fifo_regs->rfstat),
			in_be16(&psc_i2s->fifo_regs->tfnum) & 0x1ff,
			in_be16(&psc_i2s->fifo_regs->tfstat));
}

static int *psc_i2s_get_stat_attr(struct psc_i2s *psc_i2s, const char *name)
{
	if (strcmp(name, "playback_underrun") == 0)
		return &psc_i2s->stats.underrun_count;
	if (strcmp(name, "capture_overrun") == 0)
		return &psc_i2s->stats.overrun_count;

	return NULL;
}

static ssize_t psc_i2s_stat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct psc_i2s *psc_i2s = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_i2s_get_stat_attr(psc_i2s, attr->attr.name);
	if (!attrib)
		return 0;

	return sprintf(buf, "%i\n", *attrib);
}

static ssize_t psc_i2s_stat_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct psc_i2s *psc_i2s = dev_get_drvdata(dev);
	int *attrib;

	attrib = psc_i2s_get_stat_attr(psc_i2s, attr->attr.name);
	if (!attrib)
		return 0;

	*attrib = simple_strtoul(buf, NULL, 0);
	return count;
}

static DEVICE_ATTR(status, 0644, psc_i2s_status_show, NULL);
static DEVICE_ATTR(playback_underrun, 0644, psc_i2s_stat_show,
			psc_i2s_stat_store);
static DEVICE_ATTR(capture_overrun, 0644, psc_i2s_stat_show,
			psc_i2s_stat_store);

/* ---------------------------------------------------------------------
 * OF platform bus binding code:
 * - Probe/remove operations
 * - OF device match table
 */
static int __devinit psc_i2s_of_probe(struct of_device *op,
				      const struct of_device_id *match)
{
	phys_addr_t fifo;
	struct psc_i2s *psc_i2s;
	struct resource res;
	int size, psc_id, irq, rc;
	const __be32 *prop;
	void __iomem *regs;

	dev_dbg(&op->dev, "probing psc i2s device\n");

	/* Get the PSC ID */
	prop = of_get_property(op->node, "cell-index", &size);
	if (!prop || size < sizeof *prop)
		return -ENODEV;
	psc_id = be32_to_cpu(*prop);

	/* Fetch the registers and IRQ of the PSC */
	irq = irq_of_parse_and_map(op->node, 0);
	if (of_address_to_resource(op->node, 0, &res)) {
		dev_err(&op->dev, "Missing reg property\n");
		return -ENODEV;
	}
	regs = ioremap(res.start, 1 + res.end - res.start);
	if (!regs) {
		dev_err(&op->dev, "Could not map registers\n");
		return -ENODEV;
	}

	/* Allocate and initialize the driver private data */
	psc_i2s = kzalloc(sizeof *psc_i2s, GFP_KERNEL);
	if (!psc_i2s) {
		iounmap(regs);
		return -ENOMEM;
	}
	spin_lock_init(&psc_i2s->lock);
	psc_i2s->irq = irq;
	psc_i2s->psc_regs = regs;
	psc_i2s->fifo_regs = regs + sizeof *psc_i2s->psc_regs;
	psc_i2s->dev = &op->dev;
	psc_i2s->playback.psc_i2s = psc_i2s;
	psc_i2s->capture.psc_i2s = psc_i2s;
	snprintf(psc_i2s->name, sizeof psc_i2s->name, "PSC%u", psc_id+1);

	/* Fill out the CPU DAI structure */
	memcpy(&psc_i2s->dai, &psc_i2s_dai_template, sizeof psc_i2s->dai);
	psc_i2s->dai.private_data = psc_i2s;
	psc_i2s->dai.name = psc_i2s->name;
	psc_i2s->dai.id = psc_id;

	/* Find the address of the fifo data registers and setup the
	 * DMA tasks */
	fifo = res.start + offsetof(struct mpc52xx_psc, buffer.buffer_32);
	psc_i2s->capture.bcom_task =
		bcom_psc_gen_bd_rx_init(psc_id, 10, fifo, 512);
	psc_i2s->playback.bcom_task =
		bcom_psc_gen_bd_tx_init(psc_id, 10, fifo);
	if (!psc_i2s->capture.bcom_task ||
	    !psc_i2s->playback.bcom_task) {
		dev_err(&op->dev, "Could not allocate bestcomm tasks\n");
		iounmap(regs);
		kfree(psc_i2s);
		return -ENODEV;
	}

	/* Disable all interrupts and reset the PSC */
	out_be16(&psc_i2s->psc_regs->isr_imr.imr, 0);
	out_8(&psc_i2s->psc_regs->command, 3 << 4); /* reset transmitter */
	out_8(&psc_i2s->psc_regs->command, 2 << 4); /* reset receiver */
	out_8(&psc_i2s->psc_regs->command, 1 << 4); /* reset mode */
	out_8(&psc_i2s->psc_regs->command, 4 << 4); /* reset error */

	/* Configure the serial interface mode; defaulting to CODEC8 mode */
	psc_i2s->sicr = MPC52xx_PSC_SICR_DTS1 | MPC52xx_PSC_SICR_I2S |
			MPC52xx_PSC_SICR_CLKPOL;
	if (of_get_property(op->node, "fsl,cellslave", NULL))
		psc_i2s->sicr |= MPC52xx_PSC_SICR_CELLSLAVE |
				 MPC52xx_PSC_SICR_GENCLK;
	out_be32(&psc_i2s->psc_regs->sicr,
		 psc_i2s->sicr | MPC52xx_PSC_SICR_SIM_CODEC_8);

	/* Check for the codec handle.  If it is not present then we
	 * are done */
	if (!of_get_property(op->node, "codec-handle", NULL))
		return 0;

	/* Set up mode register;
	 * First write: RxRdy (FIFO Alarm) generates rx FIFO irq
	 * Second write: register Normal mode for non loopback
	 */
	out_8(&psc_i2s->psc_regs->mode, 0);
	out_8(&psc_i2s->psc_regs->mode, 0);

	/* Set the TX and RX fifo alarm thresholds */
	out_be16(&psc_i2s->fifo_regs->rfalarm, 0x100);
	out_8(&psc_i2s->fifo_regs->rfcntl, 0x4);
	out_be16(&psc_i2s->fifo_regs->tfalarm, 0x100);
	out_8(&psc_i2s->fifo_regs->tfcntl, 0x7);

	/* Lookup the IRQ numbers */
	psc_i2s->playback.irq =
		bcom_get_task_irq(psc_i2s->playback.bcom_task);
	psc_i2s->capture.irq =
		bcom_get_task_irq(psc_i2s->capture.bcom_task);

	/* Save what we've done so it can be found again later */
	dev_set_drvdata(&op->dev, psc_i2s);

	/* Register the SYSFS files */
	rc = device_create_file(psc_i2s->dev, &dev_attr_status);
	rc |= device_create_file(psc_i2s->dev, &dev_attr_capture_overrun);
	rc |= device_create_file(psc_i2s->dev, &dev_attr_playback_underrun);
	if (rc)
		dev_info(psc_i2s->dev, "error creating sysfs files\n");

	snd_soc_register_platform(&psc_i2s_pcm_soc_platform);

	/* Tell the ASoC OF helpers about it */
	of_snd_soc_register_platform(&psc_i2s_pcm_soc_platform, op->node,
				     &psc_i2s->dai);

	return 0;
}

static int __devexit psc_i2s_of_remove(struct of_device *op)
{
	struct psc_i2s *psc_i2s = dev_get_drvdata(&op->dev);

	dev_dbg(&op->dev, "psc_i2s_remove()\n");

	snd_soc_unregister_platform(&psc_i2s_pcm_soc_platform);

	bcom_gen_bd_rx_release(psc_i2s->capture.bcom_task);
	bcom_gen_bd_tx_release(psc_i2s->playback.bcom_task);

	iounmap(psc_i2s->psc_regs);
	iounmap(psc_i2s->fifo_regs);
	kfree(psc_i2s);
	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id psc_i2s_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5200-psc-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, psc_i2s_match);

static struct of_platform_driver psc_i2s_driver = {
	.match_table = psc_i2s_match,
	.probe = psc_i2s_of_probe,
	.remove = __devexit_p(psc_i2s_of_remove),
	.driver = {
		.name = "mpc5200-psc-i2s",
		.owner = THIS_MODULE,
	},
};

/* ---------------------------------------------------------------------
 * Module setup and teardown; simply register the of_platform driver
 * for the PSC in I2S mode.
 */
static int __init psc_i2s_init(void)
{
	return of_register_platform_driver(&psc_i2s_driver);
}
module_init(psc_i2s_init);

static void __exit psc_i2s_exit(void)
{
	of_unregister_platform_driver(&psc_i2s_driver);
}
module_exit(psc_i2s_exit);


