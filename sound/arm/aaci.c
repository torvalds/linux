/*
 *  linux/sound/arm/aaci.c - ARM PrimeCell AACI PL041 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Documentation: ARM DDI 0173B
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/amba/bus.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "aaci.h"

#define DRIVER_NAME	"aaci-pl041"

#define FRAME_PERIOD_US	21

/*
 * PM support is not complete.  Turn it off.
 */
#undef CONFIG_PM

static void aaci_ac97_select_codec(struct aaci *aaci, struct snd_ac97 *ac97)
{
	u32 v, maincr = aaci->maincr | MAINCR_SCRA(ac97->num);

	/*
	 * Ensure that the slot 1/2 RX registers are empty.
	 */
	v = readl(aaci->base + AACI_SLFR);
	if (v & SLFR_2RXV)
		readl(aaci->base + AACI_SL2RX);
	if (v & SLFR_1RXV)
		readl(aaci->base + AACI_SL1RX);

	if (maincr != readl(aaci->base + AACI_MAINCR)) {
		writel(maincr, aaci->base + AACI_MAINCR);
		readl(aaci->base + AACI_MAINCR);
		udelay(1);
	}
}

/*
 * P29:
 *  The recommended use of programming the external codec through slot 1
 *  and slot 2 data is to use the channels during setup routines and the
 *  slot register at any other time.  The data written into slot 1, slot 2
 *  and slot 12 registers is transmitted only when their corresponding
 *  SI1TxEn, SI2TxEn and SI12TxEn bits are set in the AACI_MAINCR
 *  register.
 */
static void aaci_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
			    unsigned short val)
{
	struct aaci *aaci = ac97->private_data;
	int timeout;
	u32 v;

	if (ac97->num >= 4)
		return;

	mutex_lock(&aaci->ac97_sem);

	aaci_ac97_select_codec(aaci, ac97);

	/*
	 * P54: You must ensure that AACI_SL2TX is always written
	 * to, if required, before data is written to AACI_SL1TX.
	 */
	writel(val << 4, aaci->base + AACI_SL2TX);
	writel(reg << 12, aaci->base + AACI_SL1TX);

	/* Initially, wait one frame period */
	udelay(FRAME_PERIOD_US);

	/* And then wait an additional eight frame periods for it to be sent */
	timeout = FRAME_PERIOD_US * 8;
	do {
		udelay(1);
		v = readl(aaci->base + AACI_SLFR);
	} while ((v & (SLFR_1TXB|SLFR_2TXB)) && --timeout);

	if (v & (SLFR_1TXB|SLFR_2TXB))
		dev_err(&aaci->dev->dev,
			"timeout waiting for write to complete\n");

	mutex_unlock(&aaci->ac97_sem);
}

/*
 * Read an AC'97 register.
 */
static unsigned short aaci_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct aaci *aaci = ac97->private_data;
	int timeout, retries = 10;
	u32 v;

	if (ac97->num >= 4)
		return ~0;

	mutex_lock(&aaci->ac97_sem);

	aaci_ac97_select_codec(aaci, ac97);

	/*
	 * Write the register address to slot 1.
	 */
	writel((reg << 12) | (1 << 19), aaci->base + AACI_SL1TX);

	/* Initially, wait one frame period */
	udelay(FRAME_PERIOD_US);

	/* And then wait an additional eight frame periods for it to be sent */
	timeout = FRAME_PERIOD_US * 8;
	do {
		udelay(1);
		v = readl(aaci->base + AACI_SLFR);
	} while ((v & SLFR_1TXB) && --timeout);

	if (v & SLFR_1TXB) {
		dev_err(&aaci->dev->dev, "timeout on slot 1 TX busy\n");
		v = ~0;
		goto out;
	}

	/* Now wait for the response frame */
	udelay(FRAME_PERIOD_US);

	/* And then wait an additional eight frame periods for data */
	timeout = FRAME_PERIOD_US * 8;
	do {
		udelay(1);
		cond_resched();
		v = readl(aaci->base + AACI_SLFR) & (SLFR_1RXV|SLFR_2RXV);
	} while ((v != (SLFR_1RXV|SLFR_2RXV)) && --timeout);

	if (v != (SLFR_1RXV|SLFR_2RXV)) {
		dev_err(&aaci->dev->dev, "timeout on RX valid\n");
		v = ~0;
		goto out;
	}

	do {
		v = readl(aaci->base + AACI_SL1RX) >> 12;
		if (v == reg) {
			v = readl(aaci->base + AACI_SL2RX) >> 4;
			break;
		} else if (--retries) {
			dev_warn(&aaci->dev->dev,
				 "ac97 read back fail.  retry\n");
			continue;
		} else {
			dev_warn(&aaci->dev->dev,
				"wrong ac97 register read back (%x != %x)\n",
				v, reg);
			v = ~0;
		}
	} while (retries);
 out:
	mutex_unlock(&aaci->ac97_sem);
	return v;
}

static inline void
aaci_chan_wait_ready(struct aaci_runtime *aacirun, unsigned long mask)
{
	u32 val;
	int timeout = 5000;

	do {
		udelay(1);
		val = readl(aacirun->base + AACI_SR);
	} while (val & mask && timeout--);
}



/*
 * Interrupt support.
 */
static void aaci_fifo_irq(struct aaci *aaci, int channel, u32 mask)
{
	if (mask & ISR_ORINTR) {
		dev_warn(&aaci->dev->dev, "RX overrun on chan %d\n", channel);
		writel(ICLR_RXOEC1 << channel, aaci->base + AACI_INTCLR);
	}

	if (mask & ISR_RXTOINTR) {
		dev_warn(&aaci->dev->dev, "RX timeout on chan %d\n", channel);
		writel(ICLR_RXTOFEC1 << channel, aaci->base + AACI_INTCLR);
	}

	if (mask & ISR_RXINTR) {
		struct aaci_runtime *aacirun = &aaci->capture;
		bool period_elapsed = false;
		void *ptr;

		if (!aacirun->substream || !aacirun->start) {
			dev_warn(&aaci->dev->dev, "RX interrupt???\n");
			writel(0, aacirun->base + AACI_IE);
			return;
		}

		spin_lock(&aacirun->lock);

		ptr = aacirun->ptr;
		do {
			unsigned int len = aacirun->fifo_bytes;
			u32 val;

			if (aacirun->bytes <= 0) {
				aacirun->bytes += aacirun->period;
				period_elapsed = true;
			}
			if (!(aacirun->cr & CR_EN))
				break;

			val = readl(aacirun->base + AACI_SR);
			if (!(val & SR_RXHF))
				break;
			if (!(val & SR_RXFF))
				len >>= 1;

			aacirun->bytes -= len;

			/* reading 16 bytes at a time */
			for( ; len > 0; len -= 16) {
				asm(
					"ldmia	%1, {r0, r1, r2, r3}\n\t"
					"stmia	%0!, {r0, r1, r2, r3}"
					: "+r" (ptr)
					: "r" (aacirun->fifo)
					: "r0", "r1", "r2", "r3", "cc");

				if (ptr >= aacirun->end)
					ptr = aacirun->start;
			}
		} while(1);

		aacirun->ptr = ptr;

		spin_unlock(&aacirun->lock);

		if (period_elapsed)
			snd_pcm_period_elapsed(aacirun->substream);
	}

	if (mask & ISR_URINTR) {
		dev_dbg(&aaci->dev->dev, "TX underrun on chan %d\n", channel);
		writel(ICLR_TXUEC1 << channel, aaci->base + AACI_INTCLR);
	}

	if (mask & ISR_TXINTR) {
		struct aaci_runtime *aacirun = &aaci->playback;
		bool period_elapsed = false;
		void *ptr;

		if (!aacirun->substream || !aacirun->start) {
			dev_warn(&aaci->dev->dev, "TX interrupt???\n");
			writel(0, aacirun->base + AACI_IE);
			return;
		}

		spin_lock(&aacirun->lock);

		ptr = aacirun->ptr;
		do {
			unsigned int len = aacirun->fifo_bytes;
			u32 val;

			if (aacirun->bytes <= 0) {
				aacirun->bytes += aacirun->period;
				period_elapsed = true;
			}
			if (!(aacirun->cr & CR_EN))
				break;

			val = readl(aacirun->base + AACI_SR);
			if (!(val & SR_TXHE))
				break;
			if (!(val & SR_TXFE))
				len >>= 1;

			aacirun->bytes -= len;

			/* writing 16 bytes at a time */
			for ( ; len > 0; len -= 16) {
				asm(
					"ldmia	%0!, {r0, r1, r2, r3}\n\t"
					"stmia	%1, {r0, r1, r2, r3}"
					: "+r" (ptr)
					: "r" (aacirun->fifo)
					: "r0", "r1", "r2", "r3", "cc");

				if (ptr >= aacirun->end)
					ptr = aacirun->start;
			}
		} while (1);

		aacirun->ptr = ptr;

		spin_unlock(&aacirun->lock);

		if (period_elapsed)
			snd_pcm_period_elapsed(aacirun->substream);
	}
}

static irqreturn_t aaci_irq(int irq, void *devid)
{
	struct aaci *aaci = devid;
	u32 mask;
	int i;

	mask = readl(aaci->base + AACI_ALLINTS);
	if (mask) {
		u32 m = mask;
		for (i = 0; i < 4; i++, m >>= 7) {
			if (m & 0x7f) {
				aaci_fifo_irq(aaci, i, m);
			}
		}
	}

	return mask ? IRQ_HANDLED : IRQ_NONE;
}



/*
 * ALSA support.
 */
static struct snd_pcm_hardware aaci_hw_info = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BLOCK_TRANSFER |
				  SNDRV_PCM_INFO_RESUME,

	/*
	 * ALSA doesn't support 18-bit or 20-bit packed into 32-bit
	 * words.  It also doesn't support 12-bit at all.
	 */
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,

	/* rates are setup from the AC'97 codec */
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= PAGE_SIZE,
	.periods_min		= 4,
	.periods_max		= PAGE_SIZE / 16,
};

/*
 * We can support two and four channel audio.  Unfortunately
 * six channel audio requires a non-standard channel ordering:
 *   2 -> FL(3), FR(4)
 *   4 -> FL(3), FR(4), SL(7), SR(8)
 *   6 -> FL(3), FR(4), SL(7), SR(8), C(6), LFE(9) (required)
 *        FL(3), FR(4), C(6), SL(7), SR(8), LFE(9) (actual)
 * This requires an ALSA configuration file to correct.
 */
static int aaci_rule_channels(struct snd_pcm_hw_params *p,
	struct snd_pcm_hw_rule *rule)
{
	static unsigned int channel_list[] = { 2, 4, 6 };
	struct aaci *aaci = rule->private;
	unsigned int mask = 1 << 0, slots;

	/* pcms[0] is the our 5.1 PCM instance. */
	slots = aaci->ac97_bus->pcms[0].r[0].slots;
	if (slots & (1 << AC97_SLOT_PCM_SLEFT)) {
		mask |= 1 << 1;
		if (slots & (1 << AC97_SLOT_LFE))
			mask |= 1 << 2;
	}

	return snd_interval_list(hw_param_interval(p, rule->var),
				 ARRAY_SIZE(channel_list), channel_list, mask);
}

static int aaci_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci *aaci = substream->private_data;
	struct aaci_runtime *aacirun;
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		aacirun = &aaci->playback;
	} else {
		aacirun = &aaci->capture;
	}

	aacirun->substream = substream;
	runtime->private_data = aacirun;
	runtime->hw = aaci_hw_info;
	runtime->hw.rates = aacirun->pcm->rates;
	snd_pcm_limit_hw_rates(runtime);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw.channels_max = 6;

		/* Add rule describing channel dependency. */
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  aaci_rule_channels, aaci,
					  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		if (ret)
			return ret;

		if (aacirun->pcm->r[1].slots)
			snd_ac97_pcm_double_rate_rules(runtime);
	}

	/*
	 * ALSA wants the byte-size of the FIFOs.  As we only support
	 * 16-bit samples, this is twice the FIFO depth irrespective
	 * of whether it's in compact mode or not.
	 */
	runtime->hw.fifo_size = aaci->fifo_depth * 2;

	mutex_lock(&aaci->irq_lock);
	if (!aaci->users++) {
		ret = request_irq(aaci->dev->irq[0], aaci_irq,
			   IRQF_SHARED, DRIVER_NAME, aaci);
		if (ret != 0)
			aaci->users--;
	}
	mutex_unlock(&aaci->irq_lock);

	return ret;
}


/*
 * Common ALSA stuff
 */
static int aaci_pcm_close(struct snd_pcm_substream *substream)
{
	struct aaci *aaci = substream->private_data;
	struct aaci_runtime *aacirun = substream->runtime->private_data;

	WARN_ON(aacirun->cr & CR_EN);

	aacirun->substream = NULL;

	mutex_lock(&aaci->irq_lock);
	if (!--aaci->users)
		free_irq(aaci->dev->irq[0], aaci);
	mutex_unlock(&aaci->irq_lock);

	return 0;
}

static int aaci_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct aaci_runtime *aacirun = substream->runtime->private_data;

	/*
	 * This must not be called with the device enabled.
	 */
	WARN_ON(aacirun->cr & CR_EN);

	if (aacirun->pcm_open)
		snd_ac97_pcm_close(aacirun->pcm);
	aacirun->pcm_open = 0;

	/*
	 * Clear out the DMA and any allocated buffers.
	 */
	snd_pcm_lib_free_pages(substream);

	return 0;
}

/* Channel to slot mask */
static const u32 channels_to_slotmask[] = {
	[2] = CR_SL3 | CR_SL4,
	[4] = CR_SL3 | CR_SL4 | CR_SL7 | CR_SL8,
	[6] = CR_SL3 | CR_SL4 | CR_SL7 | CR_SL8 | CR_SL6 | CR_SL9,
};

static int aaci_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct aaci_runtime *aacirun = substream->runtime->private_data;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	int dbl = rate > 48000;
	int err;

	aaci_pcm_hw_free(substream);
	if (aacirun->pcm_open) {
		snd_ac97_pcm_close(aacirun->pcm);
		aacirun->pcm_open = 0;
	}

	/* channels is already limited to 2, 4, or 6 by aaci_rule_channels */
	if (dbl && channels != 2)
		return -EINVAL;

	err = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(params));
	if (err >= 0) {
		struct aaci *aaci = substream->private_data;

		err = snd_ac97_pcm_open(aacirun->pcm, rate, channels,
					aacirun->pcm->r[dbl].slots);

		aacirun->pcm_open = err == 0;
		aacirun->cr = CR_FEN | CR_COMPACT | CR_SZ16;
		aacirun->cr |= channels_to_slotmask[channels + dbl * 2];

		/*
		 * fifo_bytes is the number of bytes we transfer to/from
		 * the FIFO, including padding.  So that's x4.  As we're
		 * in compact mode, the FIFO is half the size.
		 */
		aacirun->fifo_bytes = aaci->fifo_depth * 4 / 2;
	}

	return err;
}

static int aaci_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci_runtime *aacirun = runtime->private_data;

	aacirun->period	= snd_pcm_lib_period_bytes(substream);
	aacirun->start	= runtime->dma_area;
	aacirun->end	= aacirun->start + snd_pcm_lib_buffer_bytes(substream);
	aacirun->ptr	= aacirun->start;
	aacirun->bytes	= aacirun->period;

	return 0;
}

static snd_pcm_uframes_t aaci_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci_runtime *aacirun = runtime->private_data;
	ssize_t bytes = aacirun->ptr - aacirun->start;

	return bytes_to_frames(runtime, bytes);
}


/*
 * Playback specific ALSA stuff
 */
static void aaci_pcm_playback_stop(struct aaci_runtime *aacirun)
{
	u32 ie;

	ie = readl(aacirun->base + AACI_IE);
	ie &= ~(IE_URIE|IE_TXIE);
	writel(ie, aacirun->base + AACI_IE);
	aacirun->cr &= ~CR_EN;
	aaci_chan_wait_ready(aacirun, SR_TXB);
	writel(aacirun->cr, aacirun->base + AACI_TXCR);
}

static void aaci_pcm_playback_start(struct aaci_runtime *aacirun)
{
	u32 ie;

	aaci_chan_wait_ready(aacirun, SR_TXB);
	aacirun->cr |= CR_EN;

	ie = readl(aacirun->base + AACI_IE);
	ie |= IE_URIE | IE_TXIE;
	writel(ie, aacirun->base + AACI_IE);
	writel(aacirun->cr, aacirun->base + AACI_TXCR);
}

static int aaci_pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct aaci_runtime *aacirun = substream->runtime->private_data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&aacirun->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		aaci_pcm_playback_start(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
		aaci_pcm_playback_start(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		aaci_pcm_playback_stop(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		aaci_pcm_playback_stop(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;

	default:
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&aacirun->lock, flags);

	return ret;
}

static struct snd_pcm_ops aaci_playback_ops = {
	.open		= aaci_pcm_open,
	.close		= aaci_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= aaci_pcm_hw_params,
	.hw_free	= aaci_pcm_hw_free,
	.prepare	= aaci_pcm_prepare,
	.trigger	= aaci_pcm_playback_trigger,
	.pointer	= aaci_pcm_pointer,
};

static void aaci_pcm_capture_stop(struct aaci_runtime *aacirun)
{
	u32 ie;

	aaci_chan_wait_ready(aacirun, SR_RXB);

	ie = readl(aacirun->base + AACI_IE);
	ie &= ~(IE_ORIE | IE_RXIE);
	writel(ie, aacirun->base+AACI_IE);

	aacirun->cr &= ~CR_EN;

	writel(aacirun->cr, aacirun->base + AACI_RXCR);
}

static void aaci_pcm_capture_start(struct aaci_runtime *aacirun)
{
	u32 ie;

	aaci_chan_wait_ready(aacirun, SR_RXB);

#ifdef DEBUG
	/* RX Timeout value: bits 28:17 in RXCR */
	aacirun->cr |= 0xf << 17;
#endif

	aacirun->cr |= CR_EN;
	writel(aacirun->cr, aacirun->base + AACI_RXCR);

	ie = readl(aacirun->base + AACI_IE);
	ie |= IE_ORIE |IE_RXIE; // overrun and rx interrupt -- half full
	writel(ie, aacirun->base + AACI_IE);
}

static int aaci_pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct aaci_runtime *aacirun = substream->runtime->private_data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&aacirun->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		aaci_pcm_capture_start(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
		aaci_pcm_capture_start(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		aaci_pcm_capture_stop(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		aaci_pcm_capture_stop(aacirun);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;

	default:
		ret = -EINVAL;
	}

	spin_unlock_irqrestore(&aacirun->lock, flags);

	return ret;
}

static int aaci_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci *aaci = substream->private_data;

	aaci_pcm_prepare(substream);

	/* allow changing of sample rate */
	aaci_ac97_write(aaci->ac97, AC97_EXTENDED_STATUS, 0x0001); /* VRA */
	aaci_ac97_write(aaci->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	aaci_ac97_write(aaci->ac97, AC97_PCM_MIC_ADC_RATE, runtime->rate);

	/* Record select: Mic: 0, Aux: 3, Line: 4 */
	aaci_ac97_write(aaci->ac97, AC97_REC_SEL, 0x0404);

	return 0;
}

static struct snd_pcm_ops aaci_capture_ops = {
	.open		= aaci_pcm_open,
	.close		= aaci_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= aaci_pcm_hw_params,
	.hw_free	= aaci_pcm_hw_free,
	.prepare	= aaci_pcm_capture_prepare,
	.trigger	= aaci_pcm_capture_trigger,
	.pointer	= aaci_pcm_pointer,
};

/*
 * Power Management.
 */
#ifdef CONFIG_PM
static int aaci_do_suspend(struct snd_card *card)
{
	struct aaci *aaci = card->private_data;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3cold);
	snd_pcm_suspend_all(aaci->pcm);
	return 0;
}

static int aaci_do_resume(struct snd_card *card)
{
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static int aaci_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	return card ? aaci_do_suspend(card) : 0;
}

static int aaci_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	return card ? aaci_do_resume(card) : 0;
}

static SIMPLE_DEV_PM_OPS(aaci_dev_pm_ops, aaci_suspend, aaci_resume);
#define AACI_DEV_PM_OPS (&aaci_dev_pm_ops)
#else
#define AACI_DEV_PM_OPS NULL
#endif


static struct ac97_pcm ac97_defs[] = {
	[0] = {	/* Front PCM */
		.exclusive = 1,
		.r = {
			[0] = {
				.slots	= (1 << AC97_SLOT_PCM_LEFT) |
					  (1 << AC97_SLOT_PCM_RIGHT) |
					  (1 << AC97_SLOT_PCM_CENTER) |
					  (1 << AC97_SLOT_PCM_SLEFT) |
					  (1 << AC97_SLOT_PCM_SRIGHT) |
					  (1 << AC97_SLOT_LFE),
			},
			[1] = {
				.slots	= (1 << AC97_SLOT_PCM_LEFT) |
					  (1 << AC97_SLOT_PCM_RIGHT) |
					  (1 << AC97_SLOT_PCM_LEFT_0) |
					  (1 << AC97_SLOT_PCM_RIGHT_0),
			},
		},
	},
	[1] = {	/* PCM in */
		.stream = 1,
		.exclusive = 1,
		.r = {
			[0] = {
				.slots	= (1 << AC97_SLOT_PCM_LEFT) |
					  (1 << AC97_SLOT_PCM_RIGHT),
			},
		},
	},
	[2] = {	/* Mic in */
		.stream = 1,
		.exclusive = 1,
		.r = {
			[0] = {
				.slots	= (1 << AC97_SLOT_MIC),
			},
		},
	}
};

static struct snd_ac97_bus_ops aaci_bus_ops = {
	.write	= aaci_ac97_write,
	.read	= aaci_ac97_read,
};

static int aaci_probe_ac97(struct aaci *aaci)
{
	struct snd_ac97_template ac97_template;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97;
	int ret;

	/*
	 * Assert AACIRESET for 2us
	 */
	writel(0, aaci->base + AACI_RESET);
	udelay(2);
	writel(RESET_NRST, aaci->base + AACI_RESET);

	/*
	 * Give the AC'97 codec more than enough time
	 * to wake up. (42us = ~2 frames at 48kHz.)
	 */
	udelay(FRAME_PERIOD_US * 2);

	ret = snd_ac97_bus(aaci->card, 0, &aaci_bus_ops, aaci, &ac97_bus);
	if (ret)
		goto out;

	ac97_bus->clock = 48000;
	aaci->ac97_bus = ac97_bus;

	memset(&ac97_template, 0, sizeof(struct snd_ac97_template));
	ac97_template.private_data = aaci;
	ac97_template.num = 0;
	ac97_template.scaps = AC97_SCAP_SKIP_MODEM;

	ret = snd_ac97_mixer(ac97_bus, &ac97_template, &ac97);
	if (ret)
		goto out;
	aaci->ac97 = ac97;

	/*
	 * Disable AC97 PC Beep input on audio codecs.
	 */
	if (ac97_is_audio(ac97))
		snd_ac97_write_cache(ac97, AC97_PC_BEEP, 0x801e);

	ret = snd_ac97_pcm_assign(ac97_bus, ARRAY_SIZE(ac97_defs), ac97_defs);
	if (ret)
		goto out;

	aaci->playback.pcm = &ac97_bus->pcms[0];
	aaci->capture.pcm  = &ac97_bus->pcms[1];

 out:
	return ret;
}

static void aaci_free_card(struct snd_card *card)
{
	struct aaci *aaci = card->private_data;

	iounmap(aaci->base);
}

static struct aaci *aaci_init_card(struct amba_device *dev)
{
	struct aaci *aaci;
	struct snd_card *card;
	int err;

	err = snd_card_new(&dev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, sizeof(struct aaci), &card);
	if (err < 0)
		return NULL;

	card->private_free = aaci_free_card;

	strlcpy(card->driver, DRIVER_NAME, sizeof(card->driver));
	strlcpy(card->shortname, "ARM AC'97 Interface", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s PL%03x rev%u at 0x%08llx, irq %d",
		 card->shortname, amba_part(dev), amba_rev(dev),
		 (unsigned long long)dev->res.start, dev->irq[0]);

	aaci = card->private_data;
	mutex_init(&aaci->ac97_sem);
	mutex_init(&aaci->irq_lock);
	aaci->card = card;
	aaci->dev = dev;

	/* Set MAINCR to allow slot 1 and 2 data IO */
	aaci->maincr = MAINCR_IE | MAINCR_SL1RXEN | MAINCR_SL1TXEN |
		       MAINCR_SL2RXEN | MAINCR_SL2TXEN;

	return aaci;
}

static int aaci_init_pcm(struct aaci *aaci)
{
	struct snd_pcm *pcm;
	int ret;

	ret = snd_pcm_new(aaci->card, "AACI AC'97", 0, 1, 1, &pcm);
	if (ret == 0) {
		aaci->pcm = pcm;
		pcm->private_data = aaci;
		pcm->info_flags = 0;

		strlcpy(pcm->name, DRIVER_NAME, sizeof(pcm->name));

		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &aaci_playback_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &aaci_capture_ops);
		snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						      NULL, 0, 64 * 1024);
	}

	return ret;
}

static unsigned int aaci_size_fifo(struct aaci *aaci)
{
	struct aaci_runtime *aacirun = &aaci->playback;
	int i;

	/*
	 * Enable the channel, but don't assign it to any slots, so
	 * it won't empty onto the AC'97 link.
	 */
	writel(CR_FEN | CR_SZ16 | CR_EN, aacirun->base + AACI_TXCR);

	for (i = 0; !(readl(aacirun->base + AACI_SR) & SR_TXFF) && i < 4096; i++)
		writel(0, aacirun->fifo);

	writel(0, aacirun->base + AACI_TXCR);

	/*
	 * Re-initialise the AACI after the FIFO depth test, to
	 * ensure that the FIFOs are empty.  Unfortunately, merely
	 * disabling the channel doesn't clear the FIFO.
	 */
	writel(aaci->maincr & ~MAINCR_IE, aaci->base + AACI_MAINCR);
	readl(aaci->base + AACI_MAINCR);
	udelay(1);
	writel(aaci->maincr, aaci->base + AACI_MAINCR);

	/*
	 * If we hit 4096 entries, we failed.  Go back to the specified
	 * fifo depth.
	 */
	if (i == 4096)
		i = 8;

	return i;
}

static int aaci_probe(struct amba_device *dev,
		      const struct amba_id *id)
{
	struct aaci *aaci;
	int ret, i;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		return ret;

	aaci = aaci_init_card(dev);
	if (!aaci) {
		ret = -ENOMEM;
		goto out;
	}

	aaci->base = ioremap(dev->res.start, resource_size(&dev->res));
	if (!aaci->base) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Playback uses AACI channel 0
	 */
	spin_lock_init(&aaci->playback.lock);
	aaci->playback.base = aaci->base + AACI_CSCH1;
	aaci->playback.fifo = aaci->base + AACI_DR1;

	/*
	 * Capture uses AACI channel 0
	 */
	spin_lock_init(&aaci->capture.lock);
	aaci->capture.base = aaci->base + AACI_CSCH1;
	aaci->capture.fifo = aaci->base + AACI_DR1;

	for (i = 0; i < 4; i++) {
		void __iomem *base = aaci->base + i * 0x14;

		writel(0, base + AACI_IE);
		writel(0, base + AACI_TXCR);
		writel(0, base + AACI_RXCR);
	}

	writel(0x1fff, aaci->base + AACI_INTCLR);
	writel(aaci->maincr, aaci->base + AACI_MAINCR);
	/*
	 * Fix: ac97 read back fail errors by reading
	 * from any arbitrary aaci register.
	 */
	readl(aaci->base + AACI_CSCH1);
	ret = aaci_probe_ac97(aaci);
	if (ret)
		goto out;

	/*
	 * Size the FIFOs (must be multiple of 16).
	 * This is the number of entries in the FIFO.
	 */
	aaci->fifo_depth = aaci_size_fifo(aaci);
	if (aaci->fifo_depth & 15) {
		printk(KERN_WARNING "AACI: FIFO depth %d not supported\n",
		       aaci->fifo_depth);
		ret = -ENODEV;
		goto out;
	}

	ret = aaci_init_pcm(aaci);
	if (ret)
		goto out;

	ret = snd_card_register(aaci->card);
	if (ret == 0) {
		dev_info(&dev->dev, "%s\n", aaci->card->longname);
		dev_info(&dev->dev, "FIFO %u entries\n", aaci->fifo_depth);
		amba_set_drvdata(dev, aaci->card);
		return ret;
	}

 out:
	if (aaci)
		snd_card_free(aaci->card);
	amba_release_regions(dev);
	return ret;
}

static int aaci_remove(struct amba_device *dev)
{
	struct snd_card *card = amba_get_drvdata(dev);

	if (card) {
		struct aaci *aaci = card->private_data;
		writel(0, aaci->base + AACI_MAINCR);

		snd_card_free(card);
		amba_release_regions(dev);
	}

	return 0;
}

static struct amba_id aaci_ids[] = {
	{
		.id	= 0x00041041,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, aaci_ids);

static struct amba_driver aaci_driver = {
	.drv		= {
		.name	= DRIVER_NAME,
		.pm	= AACI_DEV_PM_OPS,
	},
	.probe		= aaci_probe,
	.remove		= aaci_remove,
	.id_table	= aaci_ids,
};

module_amba_driver(aaci_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM PrimeCell PL041 Advanced Audio CODEC Interface driver");
