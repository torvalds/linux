// SPDX-License-Identifier: GPL-2.0
//
// Renesas RZ/G2L ASoC Serial Sound Interface (SSIF-2) Driver
//
// Copyright (C) 2021 Renesas Electronics Corp.
// Copyright (C) 2019 Chris Brandt.
//

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <sound/soc.h>

/* REGISTER OFFSET */
#define SSICR			0x000
#define SSISR			0x004
#define SSIFCR			0x010
#define SSIFSR			0x014
#define SSIFTDR			0x018
#define SSIFRDR			0x01c
#define SSIOFR			0x020
#define SSISCR			0x024

/* SSI REGISTER BITS */
#define SSICR_DWL(x)		(((x) & 0x7) << 19)
#define SSICR_SWL(x)		(((x) & 0x7) << 16)
#define SSICR_MST		BIT(14)
#define SSICR_CKDV(x)		(((x) & 0xf) << 4)

#define SSICR_CKS		BIT(30)
#define SSICR_TUIEN		BIT(29)
#define SSICR_TOIEN		BIT(28)
#define SSICR_RUIEN		BIT(27)
#define SSICR_ROIEN		BIT(26)
#define SSICR_MST		BIT(14)
#define SSICR_BCKP		BIT(13)
#define SSICR_LRCKP		BIT(12)
#define SSICR_CKDV(x)		(((x) & 0xf) << 4)
#define SSICR_TEN		BIT(1)
#define SSICR_REN		BIT(0)

#define SSISR_TUIRQ		BIT(29)
#define SSISR_TOIRQ		BIT(28)
#define SSISR_RUIRQ		BIT(27)
#define SSISR_ROIRQ		BIT(26)
#define SSISR_IIRQ		BIT(25)

#define SSIFCR_AUCKE		BIT(31)
#define SSIFCR_SSIRST		BIT(16)
#define SSIFCR_TIE		BIT(3)
#define SSIFCR_RIE		BIT(2)
#define SSIFCR_TFRST		BIT(1)
#define SSIFCR_RFRST		BIT(0)

#define SSIFSR_TDC_MASK		0x3f
#define SSIFSR_TDC_SHIFT	24
#define SSIFSR_RDC_MASK		0x3f
#define SSIFSR_RDC_SHIFT	8

#define SSIFSR_TDC(x)		(((x) & 0x1f) << 24)
#define SSIFSR_TDE		BIT(16)
#define SSIFSR_RDC(x)		(((x) & 0x1f) << 8)
#define SSIFSR_RDF		BIT(0)

#define SSIOFR_LRCONT		BIT(8)

#define SSISCR_TDES(x)		(((x) & 0x1f) << 8)
#define SSISCR_RDFS(x)		(((x) & 0x1f) << 0)

/* Pre allocated buffers sizes */
#define PREALLOC_BUFFER		(SZ_32K)
#define PREALLOC_BUFFER_MAX	(SZ_32K)

#define SSI_RATES		SNDRV_PCM_RATE_8000_48000 /* 8k-44.1kHz */
#define SSI_FMTS		SNDRV_PCM_FMTBIT_S16_LE
#define SSI_CHAN_MIN		2
#define SSI_CHAN_MAX		2
#define SSI_FIFO_DEPTH		32

struct rz_ssi_priv;

struct rz_ssi_stream {
	struct rz_ssi_priv *priv;
	struct snd_pcm_substream *substream;
	int fifo_sample_size;	/* sample capacity of SSI FIFO */
	int dma_buffer_pos;	/* The address for the next DMA descriptor */
	int period_counter;	/* for keeping track of periods transferred */
	int sample_width;
	int buffer_pos;		/* current frame position in the buffer */
	int running;		/* 0=stopped, 1=running */

	int uerr_num;
	int oerr_num;

	struct dma_chan *dma_ch;

	int (*transfer)(struct rz_ssi_priv *ssi, struct rz_ssi_stream *strm);
};

struct rz_ssi_priv {
	void __iomem *base;
	struct platform_device *pdev;
	struct reset_control *rstc;
	struct device *dev;
	struct clk *sfr_clk;
	struct clk *clk;

	phys_addr_t phys;
	int irq_int;
	int irq_tx;
	int irq_rx;

	spinlock_t lock;

	/*
	 * The SSI supports full-duplex transmission and reception.
	 * However, if an error occurs, channel reset (both transmission
	 * and reception reset) is required.
	 * So it is better to use as half-duplex (playing and recording
	 * should be done on separate channels).
	 */
	struct rz_ssi_stream playback;
	struct rz_ssi_stream capture;

	/* clock */
	unsigned long audio_mck;
	unsigned long audio_clk_1;
	unsigned long audio_clk_2;

	bool lrckp_fsync_fall;	/* LR clock polarity (SSICR.LRCKP) */
	bool bckp_rise;	/* Bit clock polarity (SSICR.BCKP) */
	bool dma_rt;
};

static void rz_ssi_dma_complete(void *data);

static void rz_ssi_reg_writel(struct rz_ssi_priv *priv, uint reg, u32 data)
{
	writel(data, (priv->base + reg));
}

static u32 rz_ssi_reg_readl(struct rz_ssi_priv *priv, uint reg)
{
	return readl(priv->base + reg);
}

static void rz_ssi_reg_mask_setl(struct rz_ssi_priv *priv, uint reg,
				 u32 bclr, u32 bset)
{
	u32 val;

	val = readl(priv->base + reg);
	val = (val & ~bclr) | bset;
	writel(val, (priv->base + reg));
}

static inline struct snd_soc_dai *
rz_ssi_get_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	return asoc_rtd_to_cpu(rtd, 0);
}

static inline bool rz_ssi_stream_is_play(struct rz_ssi_priv *ssi,
					 struct snd_pcm_substream *substream)
{
	return substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
}

static inline struct rz_ssi_stream *
rz_ssi_stream_get(struct rz_ssi_priv *ssi, struct snd_pcm_substream *substream)
{
	struct rz_ssi_stream *stream = &ssi->playback;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		stream = &ssi->capture;

	return stream;
}

static inline bool rz_ssi_is_dma_enabled(struct rz_ssi_priv *ssi)
{
	return (ssi->playback.dma_ch && (ssi->dma_rt || ssi->capture.dma_ch));
}

static int rz_ssi_stream_is_valid(struct rz_ssi_priv *ssi,
				  struct rz_ssi_stream *strm)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ssi->lock, flags);
	ret = !!(strm->substream && strm->substream->runtime);
	spin_unlock_irqrestore(&ssi->lock, flags);

	return ret;
}

static int rz_ssi_stream_init(struct rz_ssi_priv *ssi,
			      struct rz_ssi_stream *strm,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	strm->substream = substream;
	strm->sample_width = samples_to_bytes(runtime, 1);
	strm->dma_buffer_pos = 0;
	strm->period_counter = 0;
	strm->buffer_pos = 0;

	strm->oerr_num = 0;
	strm->uerr_num = 0;
	strm->running = 0;

	/* fifo init */
	strm->fifo_sample_size = SSI_FIFO_DEPTH;

	return 0;
}

static void rz_ssi_stream_quit(struct rz_ssi_priv *ssi,
			       struct rz_ssi_stream *strm)
{
	struct snd_soc_dai *dai = rz_ssi_get_dai(strm->substream);
	unsigned long flags;

	spin_lock_irqsave(&ssi->lock, flags);
	strm->substream = NULL;
	spin_unlock_irqrestore(&ssi->lock, flags);

	if (strm->oerr_num > 0)
		dev_info(dai->dev, "overrun = %d\n", strm->oerr_num);

	if (strm->uerr_num > 0)
		dev_info(dai->dev, "underrun = %d\n", strm->uerr_num);
}

static int rz_ssi_clk_setup(struct rz_ssi_priv *ssi, unsigned int rate,
			    unsigned int channels)
{
	static s8 ckdv[16] = { 1,  2,  4,  8, 16, 32, 64, 128,
			       6, 12, 24, 48, 96, -1, -1, -1 };
	unsigned int channel_bits = 32;	/* System Word Length */
	unsigned long bclk_rate = rate * channels * channel_bits;
	unsigned int div;
	unsigned int i;
	u32 ssicr = 0;
	u32 clk_ckdv;

	/* Clear AUCKE so we can set MST */
	rz_ssi_reg_writel(ssi, SSIFCR, 0);

	/* Continue to output LRCK pin even when idle */
	rz_ssi_reg_writel(ssi, SSIOFR, SSIOFR_LRCONT);
	if (ssi->audio_clk_1 && ssi->audio_clk_2) {
		if (ssi->audio_clk_1 % bclk_rate)
			ssi->audio_mck = ssi->audio_clk_2;
		else
			ssi->audio_mck = ssi->audio_clk_1;
	}

	/* Clock setting */
	ssicr |= SSICR_MST;
	if (ssi->audio_mck == ssi->audio_clk_1)
		ssicr |= SSICR_CKS;
	if (ssi->bckp_rise)
		ssicr |= SSICR_BCKP;
	if (ssi->lrckp_fsync_fall)
		ssicr |= SSICR_LRCKP;

	/* Determine the clock divider */
	clk_ckdv = 0;
	div = ssi->audio_mck / bclk_rate;
	/* try to find an match */
	for (i = 0; i < ARRAY_SIZE(ckdv); i++) {
		if (ckdv[i] == div) {
			clk_ckdv = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(ckdv)) {
		dev_err(ssi->dev, "Rate not divisible by audio clock source\n");
		return -EINVAL;
	}

	/*
	 * DWL: Data Word Length = 16 bits
	 * SWL: System Word Length = 32 bits
	 */
	ssicr |= SSICR_CKDV(clk_ckdv);
	ssicr |= SSICR_DWL(1) | SSICR_SWL(3);
	rz_ssi_reg_writel(ssi, SSICR, ssicr);
	rz_ssi_reg_writel(ssi, SSIFCR,
			  (SSIFCR_AUCKE | SSIFCR_TFRST | SSIFCR_RFRST));

	return 0;
}

static int rz_ssi_start(struct rz_ssi_priv *ssi, struct rz_ssi_stream *strm)
{
	bool is_play = rz_ssi_stream_is_play(ssi, strm->substream);
	u32 ssicr, ssifcr;

	ssicr = rz_ssi_reg_readl(ssi, SSICR);
	ssifcr = rz_ssi_reg_readl(ssi, SSIFCR) & ~0xF;

	/* FIFO interrupt thresholds */
	if (rz_ssi_is_dma_enabled(ssi))
		rz_ssi_reg_writel(ssi, SSISCR, 0);
	else
		rz_ssi_reg_writel(ssi, SSISCR,
				  SSISCR_TDES(strm->fifo_sample_size / 2 - 1) |
				  SSISCR_RDFS(0));

	/* enable IRQ */
	if (is_play) {
		ssicr |= SSICR_TUIEN | SSICR_TOIEN;
		ssifcr |= SSIFCR_TIE | SSIFCR_RFRST;
	} else {
		ssicr |= SSICR_RUIEN | SSICR_ROIEN;
		ssifcr |= SSIFCR_RIE | SSIFCR_TFRST;
	}

	rz_ssi_reg_writel(ssi, SSICR, ssicr);
	rz_ssi_reg_writel(ssi, SSIFCR, ssifcr);

	/* Clear all error flags */
	rz_ssi_reg_mask_setl(ssi, SSISR,
			     (SSISR_TOIRQ | SSISR_TUIRQ | SSISR_ROIRQ |
			      SSISR_RUIRQ), 0);

	strm->running = 1;
	ssicr |= is_play ? SSICR_TEN : SSICR_REN;
	rz_ssi_reg_writel(ssi, SSICR, ssicr);

	return 0;
}

static int rz_ssi_stop(struct rz_ssi_priv *ssi, struct rz_ssi_stream *strm)
{
	int timeout;

	strm->running = 0;

	/* Disable TX/RX */
	rz_ssi_reg_mask_setl(ssi, SSICR, SSICR_TEN | SSICR_REN, 0);

	/* Cancel all remaining DMA transactions */
	if (rz_ssi_is_dma_enabled(ssi))
		dmaengine_terminate_async(strm->dma_ch);

	/* Disable irqs */
	rz_ssi_reg_mask_setl(ssi, SSICR, SSICR_TUIEN | SSICR_TOIEN |
			     SSICR_RUIEN | SSICR_ROIEN, 0);
	rz_ssi_reg_mask_setl(ssi, SSIFCR, SSIFCR_TIE | SSIFCR_RIE, 0);

	/* Clear all error flags */
	rz_ssi_reg_mask_setl(ssi, SSISR,
			     (SSISR_TOIRQ | SSISR_TUIRQ | SSISR_ROIRQ |
			      SSISR_RUIRQ), 0);

	/* Wait for idle */
	timeout = 100;
	while (--timeout) {
		if (rz_ssi_reg_readl(ssi, SSISR) & SSISR_IIRQ)
			break;
		udelay(1);
	}

	if (!timeout)
		dev_info(ssi->dev, "timeout waiting for SSI idle\n");

	/* Hold FIFOs in reset */
	rz_ssi_reg_mask_setl(ssi, SSIFCR, 0,
			     SSIFCR_TFRST | SSIFCR_RFRST);

	return 0;
}

static void rz_ssi_pointer_update(struct rz_ssi_stream *strm, int frames)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime;
	int current_period;

	if (!strm->running || !substream || !substream->runtime)
		return;

	runtime = substream->runtime;
	strm->buffer_pos += frames;
	WARN_ON(strm->buffer_pos > runtime->buffer_size);

	/* ring buffer */
	if (strm->buffer_pos == runtime->buffer_size)
		strm->buffer_pos = 0;

	current_period = strm->buffer_pos / runtime->period_size;
	if (strm->period_counter != current_period) {
		snd_pcm_period_elapsed(strm->substream);
		strm->period_counter = current_period;
	}
}

static int rz_ssi_pio_recv(struct rz_ssi_priv *ssi, struct rz_ssi_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime;
	bool done = false;
	u16 *buf;
	int fifo_samples;
	int frames_left;
	int samples;
	int i;

	if (!rz_ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	runtime = substream->runtime;

	while (!done) {
		/* frames left in this period */
		frames_left = runtime->period_size -
			      (strm->buffer_pos % runtime->period_size);
		if (!frames_left)
			frames_left = runtime->period_size;

		/* Samples in RX FIFO */
		fifo_samples = (rz_ssi_reg_readl(ssi, SSIFSR) >>
				SSIFSR_RDC_SHIFT) & SSIFSR_RDC_MASK;

		/* Only read full frames at a time */
		samples = 0;
		while (frames_left && (fifo_samples >= runtime->channels)) {
			samples += runtime->channels;
			fifo_samples -= runtime->channels;
			frames_left--;
		}

		/* not enough samples yet */
		if (!samples)
			break;

		/* calculate new buffer index */
		buf = (u16 *)(runtime->dma_area);
		buf += strm->buffer_pos * runtime->channels;

		/* Note, only supports 16-bit samples */
		for (i = 0; i < samples; i++)
			*buf++ = (u16)(rz_ssi_reg_readl(ssi, SSIFRDR) >> 16);

		rz_ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_RDF, 0);
		rz_ssi_pointer_update(strm, samples / runtime->channels);

		/* check if there are no more samples in the RX FIFO */
		if (!(!frames_left && fifo_samples >= runtime->channels))
			done = true;
	}

	return 0;
}

static int rz_ssi_pio_send(struct rz_ssi_priv *ssi, struct rz_ssi_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int sample_space;
	int samples = 0;
	int frames_left;
	int i;
	u32 ssifsr;
	u16 *buf;

	if (!rz_ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	/* frames left in this period */
	frames_left = runtime->period_size - (strm->buffer_pos %
					      runtime->period_size);
	if (frames_left == 0)
		frames_left = runtime->period_size;

	sample_space = strm->fifo_sample_size;
	ssifsr = rz_ssi_reg_readl(ssi, SSIFSR);
	sample_space -= (ssifsr >> SSIFSR_TDC_SHIFT) & SSIFSR_TDC_MASK;

	/* Only add full frames at a time */
	while (frames_left && (sample_space >= runtime->channels)) {
		samples += runtime->channels;
		sample_space -= runtime->channels;
		frames_left--;
	}

	/* no space to send anything right now */
	if (samples == 0)
		return 0;

	/* calculate new buffer index */
	buf = (u16 *)(runtime->dma_area);
	buf += strm->buffer_pos * runtime->channels;

	/* Note, only supports 16-bit samples */
	for (i = 0; i < samples; i++)
		rz_ssi_reg_writel(ssi, SSIFTDR, ((u32)(*buf++) << 16));

	rz_ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_TDE, 0);
	rz_ssi_pointer_update(strm, samples / runtime->channels);

	return 0;
}

static irqreturn_t rz_ssi_interrupt(int irq, void *data)
{
	struct rz_ssi_stream *strm = NULL;
	struct rz_ssi_priv *ssi = data;
	u32 ssisr = rz_ssi_reg_readl(ssi, SSISR);

	if (ssi->playback.substream)
		strm = &ssi->playback;
	else if (ssi->capture.substream)
		strm = &ssi->capture;
	else
		return IRQ_HANDLED; /* Left over TX/RX interrupt */

	if (irq == ssi->irq_int) { /* error or idle */
		if (ssisr & SSISR_TUIRQ)
			strm->uerr_num++;
		if (ssisr & SSISR_TOIRQ)
			strm->oerr_num++;
		if (ssisr & SSISR_RUIRQ)
			strm->uerr_num++;
		if (ssisr & SSISR_ROIRQ)
			strm->oerr_num++;

		if (ssisr & (SSISR_TUIRQ | SSISR_TOIRQ | SSISR_RUIRQ |
			     SSISR_ROIRQ)) {
			/* Error handling */
			/* You must reset (stop/restart) after each interrupt */
			rz_ssi_stop(ssi, strm);

			/* Clear all flags */
			rz_ssi_reg_mask_setl(ssi, SSISR, SSISR_TOIRQ |
					     SSISR_TUIRQ | SSISR_ROIRQ |
					     SSISR_RUIRQ, 0);

			/* Add/remove more data */
			strm->transfer(ssi, strm);

			/* Resume */
			rz_ssi_start(ssi, strm);
		}
	}

	if (!strm->running)
		return IRQ_HANDLED;

	/* tx data empty */
	if (irq == ssi->irq_tx)
		strm->transfer(ssi, &ssi->playback);

	/* rx data full */
	if (irq == ssi->irq_rx) {
		strm->transfer(ssi, &ssi->capture);
		rz_ssi_reg_mask_setl(ssi, SSIFSR, SSIFSR_RDF, 0);
	}

	return IRQ_HANDLED;
}

static int rz_ssi_dma_slave_config(struct rz_ssi_priv *ssi,
				   struct dma_chan *dma_ch, bool is_play)
{
	struct dma_slave_config cfg;

	memset(&cfg, 0, sizeof(cfg));

	cfg.direction = is_play ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	cfg.dst_addr = ssi->phys + SSIFTDR;
	cfg.src_addr = ssi->phys + SSIFRDR;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	return dmaengine_slave_config(dma_ch, &cfg);
}

static int rz_ssi_dma_transfer(struct rz_ssi_priv *ssi,
			       struct rz_ssi_stream *strm)
{
	struct snd_pcm_substream *substream = strm->substream;
	struct dma_async_tx_descriptor *desc;
	struct snd_pcm_runtime *runtime;
	enum dma_transfer_direction dir;
	u32 dma_paddr, dma_size;
	int amount;

	if (!rz_ssi_stream_is_valid(ssi, strm))
		return -EINVAL;

	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_DRAINING)
		/*
		 * Stream is ending, so do not queue up any more DMA
		 * transfers otherwise we play partial sound clips
		 * because we can't shut off the DMA quick enough.
		 */
		return 0;

	dir = rz_ssi_stream_is_play(ssi, substream) ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;

	/* Always transfer 1 period */
	amount = runtime->period_size;

	/* DMA physical address and size */
	dma_paddr = runtime->dma_addr + frames_to_bytes(runtime,
							strm->dma_buffer_pos);
	dma_size = frames_to_bytes(runtime, amount);
	desc = dmaengine_prep_slave_single(strm->dma_ch, dma_paddr, dma_size,
					   dir,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(ssi->dev, "dmaengine_prep_slave_single() fail\n");
		return -ENOMEM;
	}

	desc->callback = rz_ssi_dma_complete;
	desc->callback_param = strm;

	if (dmaengine_submit(desc) < 0) {
		dev_err(ssi->dev, "dmaengine_submit() fail\n");
		return -EIO;
	}

	/* Update DMA pointer */
	strm->dma_buffer_pos += amount;
	if (strm->dma_buffer_pos >= runtime->buffer_size)
		strm->dma_buffer_pos = 0;

	/* Start DMA */
	dma_async_issue_pending(strm->dma_ch);

	return 0;
}

static void rz_ssi_dma_complete(void *data)
{
	struct rz_ssi_stream *strm = (struct rz_ssi_stream *)data;

	if (!strm->running || !strm->substream || !strm->substream->runtime)
		return;

	/* Note that next DMA transaction has probably already started */
	rz_ssi_pointer_update(strm, strm->substream->runtime->period_size);

	/* Queue up another DMA transaction */
	rz_ssi_dma_transfer(strm->priv, strm);
}

static void rz_ssi_release_dma_channels(struct rz_ssi_priv *ssi)
{
	if (ssi->playback.dma_ch) {
		dma_release_channel(ssi->playback.dma_ch);
		ssi->playback.dma_ch = NULL;
		if (ssi->dma_rt)
			ssi->dma_rt = false;
	}

	if (ssi->capture.dma_ch) {
		dma_release_channel(ssi->capture.dma_ch);
		ssi->capture.dma_ch = NULL;
	}
}

static int rz_ssi_dma_request(struct rz_ssi_priv *ssi, struct device *dev)
{
	ssi->playback.dma_ch = dma_request_chan(dev, "tx");
	if (IS_ERR(ssi->playback.dma_ch))
		ssi->playback.dma_ch = NULL;

	ssi->capture.dma_ch = dma_request_chan(dev, "rx");
	if (IS_ERR(ssi->capture.dma_ch))
		ssi->capture.dma_ch = NULL;

	if (!ssi->playback.dma_ch && !ssi->capture.dma_ch) {
		ssi->playback.dma_ch = dma_request_chan(dev, "rt");
		if (IS_ERR(ssi->playback.dma_ch)) {
			ssi->playback.dma_ch = NULL;
			goto no_dma;
		}

		ssi->dma_rt = true;
	}

	if (!rz_ssi_is_dma_enabled(ssi))
		goto no_dma;

	if (ssi->playback.dma_ch &&
	    (rz_ssi_dma_slave_config(ssi, ssi->playback.dma_ch, true) < 0))
		goto no_dma;

	if (ssi->capture.dma_ch &&
	    (rz_ssi_dma_slave_config(ssi, ssi->capture.dma_ch, false) < 0))
		goto no_dma;

	return 0;

no_dma:
	rz_ssi_release_dma_channels(ssi);

	return -ENODEV;
}

static int rz_ssi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct rz_ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);
	struct rz_ssi_stream *strm = rz_ssi_stream_get(ssi, substream);
	int ret = 0, i, num_transfer = 1;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Soft Reset */
		rz_ssi_reg_mask_setl(ssi, SSIFCR, 0, SSIFCR_SSIRST);
		rz_ssi_reg_mask_setl(ssi, SSIFCR, SSIFCR_SSIRST, 0);
		udelay(5);

		ret = rz_ssi_stream_init(ssi, strm, substream);
		if (ret)
			goto done;

		if (ssi->dma_rt) {
			bool is_playback;

			is_playback = rz_ssi_stream_is_play(ssi, substream);
			ret = rz_ssi_dma_slave_config(ssi, ssi->playback.dma_ch,
						      is_playback);
			/* Fallback to pio */
			if (ret < 0) {
				ssi->playback.transfer = rz_ssi_pio_send;
				ssi->capture.transfer = rz_ssi_pio_recv;
				rz_ssi_release_dma_channels(ssi);
			}
		}

		/* For DMA, queue up multiple DMA descriptors */
		if (rz_ssi_is_dma_enabled(ssi))
			num_transfer = 4;

		for (i = 0; i < num_transfer; i++) {
			ret = strm->transfer(ssi, strm);
			if (ret)
				goto done;
		}

		ret = rz_ssi_start(ssi, strm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		rz_ssi_stop(ssi, strm);
		rz_ssi_stream_quit(ssi, strm);
		break;
	}

done:
	return ret;
}

static int rz_ssi_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rz_ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		break;
	default:
		dev_err(ssi->dev, "Codec should be clk and frame consumer\n");
		return -EINVAL;
	}

	/*
	 * set clock polarity
	 *
	 * "normal" BCLK = Signal is available at rising edge of BCLK
	 * "normal" FSYNC = (I2S) Left ch starts with falling FSYNC edge
	 */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		ssi->bckp_rise = false;
		ssi->lrckp_fsync_fall = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		ssi->bckp_rise = false;
		ssi->lrckp_fsync_fall = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		ssi->bckp_rise = true;
		ssi->lrckp_fsync_fall = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		ssi->bckp_rise = true;
		ssi->lrckp_fsync_fall = true;
		break;
	default:
		return -EINVAL;
	}

	/* only i2s support */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		dev_err(ssi->dev, "Only I2S mode is supported.\n");
		return -EINVAL;
	}

	return 0;
}

static int rz_ssi_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct rz_ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);
	unsigned int sample_bits = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;
	unsigned int channels = params_channels(params);

	if (sample_bits != 16) {
		dev_err(ssi->dev, "Unsupported sample width: %d\n",
			sample_bits);
		return -EINVAL;
	}

	if (channels != 2) {
		dev_err(ssi->dev, "Number of channels not matched: %d\n",
			channels);
		return -EINVAL;
	}

	return rz_ssi_clk_setup(ssi, params_rate(params),
				params_channels(params));
}

static const struct snd_soc_dai_ops rz_ssi_dai_ops = {
	.trigger	= rz_ssi_dai_trigger,
	.set_fmt	= rz_ssi_dai_set_fmt,
	.hw_params	= rz_ssi_dai_hw_params,
};

static const struct snd_pcm_hardware rz_ssi_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED	|
				  SNDRV_PCM_INFO_MMAP		|
				  SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= PREALLOC_BUFFER,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.channels_min		= SSI_CHAN_MIN,
	.channels_max		= SSI_CHAN_MAX,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 32 * 2,
};

static int rz_ssi_pcm_open(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &rz_ssi_pcm_hardware);

	return snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
}

static snd_pcm_uframes_t rz_ssi_pcm_pointer(struct snd_soc_component *component,
					    struct snd_pcm_substream *substream)
{
	struct snd_soc_dai *dai = rz_ssi_get_dai(substream);
	struct rz_ssi_priv *ssi = snd_soc_dai_get_drvdata(dai);
	struct rz_ssi_stream *strm = rz_ssi_stream_get(ssi, substream);

	return strm->buffer_pos;
}

static int rz_ssi_pcm_new(struct snd_soc_component *component,
			  struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       rtd->card->snd_card->dev,
				       PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
	return 0;
}

static struct snd_soc_dai_driver rz_ssi_soc_dai[] = {
	{
		.name			= "rz-ssi-dai",
		.playback = {
			.rates		= SSI_RATES,
			.formats	= SSI_FMTS,
			.channels_min	= SSI_CHAN_MIN,
			.channels_max	= SSI_CHAN_MAX,
		},
		.capture = {
			.rates		= SSI_RATES,
			.formats	= SSI_FMTS,
			.channels_min	= SSI_CHAN_MIN,
			.channels_max	= SSI_CHAN_MAX,
		},
		.ops = &rz_ssi_dai_ops,
	},
};

static const struct snd_soc_component_driver rz_ssi_soc_component = {
	.name		= "rz-ssi",
	.open		= rz_ssi_pcm_open,
	.pointer	= rz_ssi_pcm_pointer,
	.pcm_construct	= rz_ssi_pcm_new,
};

static int rz_ssi_probe(struct platform_device *pdev)
{
	struct rz_ssi_priv *ssi;
	struct clk *audio_clk;
	struct resource *res;
	int ret;

	ssi = devm_kzalloc(&pdev->dev, sizeof(*ssi), GFP_KERNEL);
	if (!ssi)
		return -ENOMEM;

	ssi->pdev = pdev;
	ssi->dev = &pdev->dev;
	ssi->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ssi->base))
		return PTR_ERR(ssi->base);

	ssi->phys = res->start;
	ssi->clk = devm_clk_get(&pdev->dev, "ssi");
	if (IS_ERR(ssi->clk))
		return PTR_ERR(ssi->clk);

	ssi->sfr_clk = devm_clk_get(&pdev->dev, "ssi_sfr");
	if (IS_ERR(ssi->sfr_clk))
		return PTR_ERR(ssi->sfr_clk);

	audio_clk = devm_clk_get(&pdev->dev, "audio_clk1");
	if (IS_ERR(audio_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(audio_clk),
				     "no audio clk1");

	ssi->audio_clk_1 = clk_get_rate(audio_clk);
	audio_clk = devm_clk_get(&pdev->dev, "audio_clk2");
	if (IS_ERR(audio_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(audio_clk),
				     "no audio clk2");

	ssi->audio_clk_2 = clk_get_rate(audio_clk);
	if (!(ssi->audio_clk_1 || ssi->audio_clk_2))
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "no audio clk1 or audio clk2");

	ssi->audio_mck = ssi->audio_clk_1 ? ssi->audio_clk_1 : ssi->audio_clk_2;

	/* Detect DMA support */
	ret = rz_ssi_dma_request(ssi, &pdev->dev);
	if (ret < 0) {
		dev_warn(&pdev->dev, "DMA not available, using PIO\n");
		ssi->playback.transfer = rz_ssi_pio_send;
		ssi->capture.transfer = rz_ssi_pio_recv;
	} else {
		dev_info(&pdev->dev, "DMA enabled");
		ssi->playback.transfer = rz_ssi_dma_transfer;
		ssi->capture.transfer = rz_ssi_dma_transfer;
	}

	ssi->playback.priv = ssi;
	ssi->capture.priv = ssi;

	/* Error Interrupt */
	ssi->irq_int = platform_get_irq_byname(pdev, "int_req");
	if (ssi->irq_int < 0)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "Unable to get SSI int_req IRQ\n");

	ret = devm_request_irq(&pdev->dev, ssi->irq_int, &rz_ssi_interrupt,
			       0, dev_name(&pdev->dev), ssi);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "irq request error (int_req)\n");

	if (!rz_ssi_is_dma_enabled(ssi)) {
		/* Tx and Rx interrupts (pio only) */
		ssi->irq_tx = platform_get_irq_byname(pdev, "dma_tx");
		if (ssi->irq_tx < 0)
			return dev_err_probe(&pdev->dev, -ENODEV,
					     "Unable to get SSI dma_tx IRQ\n");

		ret = devm_request_irq(&pdev->dev, ssi->irq_tx,
				       &rz_ssi_interrupt, 0,
				       dev_name(&pdev->dev), ssi);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret,
					     "irq request error (dma_tx)\n");

		ssi->irq_rx = platform_get_irq_byname(pdev, "dma_rx");
		if (ssi->irq_rx < 0)
			return dev_err_probe(&pdev->dev, -ENODEV,
					     "Unable to get SSI dma_rx IRQ\n");

		ret = devm_request_irq(&pdev->dev, ssi->irq_rx,
				       &rz_ssi_interrupt, 0,
				       dev_name(&pdev->dev), ssi);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret,
					     "irq request error (dma_rx)\n");
	}

	ssi->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(ssi->rstc))
		return PTR_ERR(ssi->rstc);

	reset_control_deassert(ssi->rstc);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume_and_get(&pdev->dev);

	spin_lock_init(&ssi->lock);
	dev_set_drvdata(&pdev->dev, ssi);
	ret = devm_snd_soc_register_component(&pdev->dev, &rz_ssi_soc_component,
					      rz_ssi_soc_dai,
					      ARRAY_SIZE(rz_ssi_soc_dai));
	if (ret < 0) {
		rz_ssi_release_dma_channels(ssi);

		pm_runtime_put(ssi->dev);
		pm_runtime_disable(ssi->dev);
		reset_control_assert(ssi->rstc);
		dev_err(&pdev->dev, "failed to register snd component\n");
	}

	return ret;
}

static int rz_ssi_remove(struct platform_device *pdev)
{
	struct rz_ssi_priv *ssi = dev_get_drvdata(&pdev->dev);

	rz_ssi_release_dma_channels(ssi);

	pm_runtime_put(ssi->dev);
	pm_runtime_disable(ssi->dev);
	reset_control_assert(ssi->rstc);

	return 0;
}

static const struct of_device_id rz_ssi_of_match[] = {
	{ .compatible = "renesas,rz-ssi", },
	{/* Sentinel */},
};
MODULE_DEVICE_TABLE(of, rz_ssi_of_match);

static struct platform_driver rz_ssi_driver = {
	.driver	= {
		.name	= "rz-ssi-pcm-audio",
		.of_match_table = rz_ssi_of_match,
	},
	.probe		= rz_ssi_probe,
	.remove		= rz_ssi_remove,
};

module_platform_driver(rz_ssi_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas RZ/G2L ASoC Serial Sound Interface Driver");
MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
