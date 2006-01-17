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

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "aaci.h"
#include "devdma.h"

#define DRIVER_NAME	"aaci-pl041"

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

	writel(maincr, aaci->base + AACI_MAINCR);
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
static void aaci_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val)
{
	struct aaci *aaci = ac97->private_data;
	u32 v;

	if (ac97->num >= 4)
		return;

	down(&aaci->ac97_sem);

	aaci_ac97_select_codec(aaci, ac97);

	/*
	 * P54: You must ensure that AACI_SL2TX is always written
	 * to, if required, before data is written to AACI_SL1TX.
	 */
	writel(val << 4, aaci->base + AACI_SL2TX);
	writel(reg << 12, aaci->base + AACI_SL1TX);

	/*
	 * Wait for the transmission of both slots to complete.
	 */
	do {
		v = readl(aaci->base + AACI_SLFR);
	} while (v & (SLFR_1TXB|SLFR_2TXB));

	up(&aaci->ac97_sem);
}

/*
 * Read an AC'97 register.
 */
static unsigned short aaci_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct aaci *aaci = ac97->private_data;
	u32 v;

	if (ac97->num >= 4)
		return ~0;

	down(&aaci->ac97_sem);

	aaci_ac97_select_codec(aaci, ac97);

	/*
	 * Write the register address to slot 1.
	 */
	writel((reg << 12) | (1 << 19), aaci->base + AACI_SL1TX);

	/*
	 * Wait for the transmission to complete.
	 */
	do {
		v = readl(aaci->base + AACI_SLFR);
	} while (v & SLFR_1TXB);

	/*
	 * Give the AC'97 codec more than enough time
	 * to respond. (42us = ~2 frames at 48kHz.)
	 */
	udelay(42);

	/*
	 * Wait for slot 2 to indicate data.
	 */
	do {
		cond_resched();
		v = readl(aaci->base + AACI_SLFR) & (SLFR_1RXV|SLFR_2RXV);
	} while (v != (SLFR_1RXV|SLFR_2RXV));

	v = readl(aaci->base + AACI_SL1RX) >> 12;
	if (v == reg) {
		v = readl(aaci->base + AACI_SL2RX) >> 4;
	} else {
		dev_err(&aaci->dev->dev,
			"wrong ac97 register read back (%x != %x)\n",
			v, reg);
		v = ~0;
	}

	up(&aaci->ac97_sem);
	return v;
}

static inline void aaci_chan_wait_ready(struct aaci_runtime *aacirun)
{
	u32 val;
	int timeout = 5000;

	do {
		val = readl(aacirun->base + AACI_SR);
	} while (val & (SR_TXB|SR_RXB) && timeout--);
}



/*
 * Interrupt support.
 */
static void aaci_fifo_irq(struct aaci *aaci, u32 mask)
{
	if (mask & ISR_URINTR) {
		writel(ICLR_TXUEC1, aaci->base + AACI_INTCLR);
	}

	if (mask & ISR_TXINTR) {
		struct aaci_runtime *aacirun = &aaci->playback;
		void *ptr;

		if (!aacirun->substream || !aacirun->start) {
			dev_warn(&aaci->dev->dev, "TX interrupt???");
			writel(0, aacirun->base + AACI_IE);
			return;
		}

		ptr = aacirun->ptr;
		do {
			unsigned int len = aacirun->fifosz;
			u32 val;

			if (aacirun->bytes <= 0) {
				aacirun->bytes += aacirun->period;
				aacirun->ptr = ptr;
				spin_unlock(&aaci->lock);
				snd_pcm_period_elapsed(aacirun->substream);
				spin_lock(&aaci->lock);
			}
			if (!(aacirun->cr & TXCR_TXEN))
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
	}
}

static irqreturn_t aaci_irq(int irq, void *devid, struct pt_regs *regs)
{
	struct aaci *aaci = devid;
	u32 mask;
	int i;

	spin_lock(&aaci->lock);
	mask = readl(aaci->base + AACI_ALLINTS);
	if (mask) {
		u32 m = mask;
		for (i = 0; i < 4; i++, m >>= 7) {
			if (m & 0x7f) {
				aaci_fifo_irq(aaci, m);
			}
		}
	}
	spin_unlock(&aaci->lock);

	return mask ? IRQ_HANDLED : IRQ_NONE;
}



/*
 * ALSA support.
 */

struct aaci_stream {
	unsigned char codec_idx;
	unsigned char rate_idx;
};

static struct aaci_stream aaci_streams[] = {
	[ACSTREAM_FRONT] = {
		.codec_idx	= 0,
		.rate_idx	= AC97_RATES_FRONT_DAC,
	},
	[ACSTREAM_SURROUND] = {
		.codec_idx	= 0,
		.rate_idx	= AC97_RATES_SURR_DAC,
	},
	[ACSTREAM_LFE] = {
		.codec_idx	= 0,
		.rate_idx	= AC97_RATES_LFE_DAC,
	},
};

static inline unsigned int aaci_rate_mask(struct aaci *aaci, int streamid)
{
	struct aaci_stream *s = aaci_streams + streamid;
	return aaci->ac97_bus->codec[s->codec_idx]->rates[s->rate_idx];
}

static unsigned int rate_list[] = {
	5512, 8000, 11025, 16000, 22050, 32000, 44100,
	48000, 64000, 88200, 96000, 176400, 192000
};

/*
 * Double-rate rule: we can support double rate iff channels == 2
 *  (unimplemented)
 */
static int
aaci_rule_rate_by_channels(struct snd_pcm_hw_params *p, struct snd_pcm_hw_rule *rule)
{
	struct aaci *aaci = rule->private;
	unsigned int rate_mask = SNDRV_PCM_RATE_8000_48000|SNDRV_PCM_RATE_5512;
	struct snd_interval *c = hw_param_interval(p, SNDRV_PCM_HW_PARAM_CHANNELS);

	switch (c->max) {
	case 6:
		rate_mask &= aaci_rate_mask(aaci, ACSTREAM_LFE);
	case 4:
		rate_mask &= aaci_rate_mask(aaci, ACSTREAM_SURROUND);
	case 2:
		rate_mask &= aaci_rate_mask(aaci, ACSTREAM_FRONT);
	}

	return snd_interval_list(hw_param_interval(p, rule->var),
				 ARRAY_SIZE(rate_list), rate_list,
				 rate_mask);
}

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

	/* should this be continuous or knot? */
	.rates			= SNDRV_PCM_RATE_CONTINUOUS,
	.rate_max		= 48000,
	.rate_min		= 4000,
	.channels_min		= 2,
	.channels_max		= 6,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= PAGE_SIZE,
	.periods_min		= 4,
	.periods_max		= PAGE_SIZE / 16,
};

static int aaci_pcm_open(struct aaci *aaci, struct snd_pcm_substream *substream,
			 struct aaci_runtime *aacirun)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	aacirun->substream = substream;
	runtime->private_data = aacirun;
	runtime->hw = aaci_hw_info;

	/*
	 * FIXME: ALSA specifies fifo_size in bytes.  If we're in normal
	 * mode, each 32-bit word contains one sample.  If we're in
	 * compact mode, each 32-bit word contains two samples, effectively
	 * halving the FIFO size.  However, we don't know for sure which
	 * we'll be using at this point.  We set this to the lower limit.
	 */
	runtime->hw.fifo_size = aaci->fifosize * 2;

	/*
	 * Add rule describing hardware rate dependency
	 * on the number of channels.
	 */
	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  aaci_rule_rate_by_channels, aaci,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (ret)
		goto out;

	ret = request_irq(aaci->dev->irq[0], aaci_irq, SA_SHIRQ|SA_INTERRUPT,
			  DRIVER_NAME, aaci);
	if (ret)
		goto out;

	return 0;

 out:
	return ret;
}


/*
 * Common ALSA stuff
 */
static int aaci_pcm_close(struct snd_pcm_substream *substream)
{
	struct aaci *aaci = substream->private_data;
	struct aaci_runtime *aacirun = substream->runtime->private_data;

	WARN_ON(aacirun->cr & TXCR_TXEN);

	aacirun->substream = NULL;
	free_irq(aaci->dev->irq[0], aaci);

	return 0;
}

static int aaci_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct aaci_runtime *aacirun = substream->runtime->private_data;

	/*
	 * This must not be called with the device enabled.
	 */
	WARN_ON(aacirun->cr & TXCR_TXEN);

	if (aacirun->pcm_open)
		snd_ac97_pcm_close(aacirun->pcm);
	aacirun->pcm_open = 0;

	/*
	 * Clear out the DMA and any allocated buffers.
	 */
	devdma_hw_free(NULL, substream);

	return 0;
}

static int aaci_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct aaci_runtime *aacirun,
			      struct snd_pcm_hw_params *params)
{
	int err;

	aaci_pcm_hw_free(substream);

	err = devdma_hw_alloc(NULL, substream,
			      params_buffer_bytes(params));
	if (err < 0)
		goto out;

	err = snd_ac97_pcm_open(aacirun->pcm, params_rate(params),
				params_channels(params),
				aacirun->pcm->r[0].slots);
	if (err)
		goto out;

	aacirun->pcm_open = 1;

 out:
	return err;
}

static int aaci_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci_runtime *aacirun = runtime->private_data;

	aacirun->start	= (void *)runtime->dma_area;
	aacirun->end	= aacirun->start + runtime->dma_bytes;
	aacirun->ptr	= aacirun->start;
	aacirun->period	=
	aacirun->bytes	= frames_to_bytes(runtime, runtime->period_size);

	return 0;
}

static snd_pcm_uframes_t aaci_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aaci_runtime *aacirun = runtime->private_data;
	ssize_t bytes = aacirun->ptr - aacirun->start;

	return bytes_to_frames(runtime, bytes);
}

static int aaci_pcm_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
	return devdma_mmap(NULL, substream, vma);
}


/*
 * Playback specific ALSA stuff
 */
static const u32 channels_to_txmask[] = {
	[2] = TXCR_TX3 | TXCR_TX4,
	[4] = TXCR_TX3 | TXCR_TX4 | TXCR_TX7 | TXCR_TX8,
	[6] = TXCR_TX3 | TXCR_TX4 | TXCR_TX7 | TXCR_TX8 | TXCR_TX6 | TXCR_TX9,
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
static unsigned int channel_list[] = { 2, 4, 6 };

static int
aaci_rule_channels(struct snd_pcm_hw_params *p, struct snd_pcm_hw_rule *rule)
{
	struct aaci *aaci = rule->private;
	unsigned int chan_mask = 1 << 0, slots;

	/*
	 * pcms[0] is the our 5.1 PCM instance.
	 */
	slots = aaci->ac97_bus->pcms[0].r[0].slots;
	if (slots & (1 << AC97_SLOT_PCM_SLEFT)) {
		chan_mask |= 1 << 1;
		if (slots & (1 << AC97_SLOT_LFE))
			chan_mask |= 1 << 2;
	}

	return snd_interval_list(hw_param_interval(p, rule->var),
				 ARRAY_SIZE(channel_list), channel_list,
				 chan_mask);
}

static int aaci_pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct aaci *aaci = substream->private_data;
	int ret;

	/*
	 * Add rule describing channel dependency.
	 */
	ret = snd_pcm_hw_rule_add(substream->runtime, 0,
				  SNDRV_PCM_HW_PARAM_CHANNELS,
				  aaci_rule_channels, aaci,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret)
		return ret;

	return aaci_pcm_open(aaci, substream, &aaci->playback);
}

static int aaci_pcm_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct aaci *aaci = substream->private_data;
	struct aaci_runtime *aacirun = substream->runtime->private_data;
	unsigned int channels = params_channels(params);
	int ret;

	WARN_ON(channels >= ARRAY_SIZE(channels_to_txmask) ||
		!channels_to_txmask[channels]);

	ret = aaci_pcm_hw_params(substream, aacirun, params);

	/*
	 * Enable FIFO, compact mode, 16 bits per sample.
	 * FIXME: double rate slots?
	 */
	if (ret >= 0) {
		aacirun->cr = TXCR_FEN | TXCR_COMPACT | TXCR_TSZ16;
		aacirun->cr |= channels_to_txmask[channels];

		aacirun->fifosz	= aaci->fifosize * 4;
		if (aacirun->cr & TXCR_COMPACT)
			aacirun->fifosz >>= 1;
	}
	return ret;
}

static void aaci_pcm_playback_stop(struct aaci_runtime *aacirun)
{
	u32 ie;

	ie = readl(aacirun->base + AACI_IE);
	ie &= ~(IE_URIE|IE_TXIE);
	writel(ie, aacirun->base + AACI_IE);
	aacirun->cr &= ~TXCR_TXEN;
	aaci_chan_wait_ready(aacirun);
	writel(aacirun->cr, aacirun->base + AACI_TXCR);
}

static void aaci_pcm_playback_start(struct aaci_runtime *aacirun)
{
	u32 ie;

	aaci_chan_wait_ready(aacirun);
	aacirun->cr |= TXCR_TXEN;

	ie = readl(aacirun->base + AACI_IE);
	ie |= IE_URIE | IE_TXIE;
	writel(ie, aacirun->base + AACI_IE);
	writel(aacirun->cr, aacirun->base + AACI_TXCR);
}

static int aaci_pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct aaci *aaci = substream->private_data;
	struct aaci_runtime *aacirun = substream->runtime->private_data;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&aaci->lock, flags);
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
	spin_unlock_irqrestore(&aaci->lock, flags);

	return ret;
}

static struct snd_pcm_ops aaci_playback_ops = {
	.open		= aaci_pcm_playback_open,
	.close		= aaci_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= aaci_pcm_playback_hw_params,
	.hw_free	= aaci_pcm_hw_free,
	.prepare	= aaci_pcm_prepare,
	.trigger	= aaci_pcm_playback_trigger,
	.pointer	= aaci_pcm_pointer,
	.mmap		= aaci_pcm_mmap,
};



/*
 * Power Management.
 */
#ifdef CONFIG_PM
static int aaci_do_suspend(struct snd_card *card, unsigned int state)
{
	struct aaci *aaci = card->private_data;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3cold);
	snd_pcm_suspend_all(aaci->pcm);
	return 0;
}

static int aaci_do_resume(struct snd_card *card, unsigned int state)
{
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static int aaci_suspend(struct amba_device *dev, pm_message_t state)
{
	struct snd_card *card = amba_get_drvdata(dev);
	return card ? aaci_do_suspend(card) : 0;
}

static int aaci_resume(struct amba_device *dev)
{
	struct snd_card *card = amba_get_drvdata(dev);
	return card ? aaci_do_resume(card) : 0;
}
#else
#define aaci_do_suspend		NULL
#define aaci_do_resume		NULL
#define aaci_suspend		NULL
#define aaci_resume		NULL
#endif


static struct ac97_pcm ac97_defs[] __devinitdata = {
	[0] = {		/* Front PCM */
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

static int __devinit aaci_probe_ac97(struct aaci *aaci)
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
	udelay(42);

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

	/*
	 * Disable AC97 PC Beep input on audio codecs.
	 */
	if (ac97_is_audio(ac97))
		snd_ac97_write_cache(ac97, AC97_PC_BEEP, 0x801e);

	ret = snd_ac97_pcm_assign(ac97_bus, ARRAY_SIZE(ac97_defs), ac97_defs);
	if (ret)
		goto out;

	aaci->playback.pcm = &ac97_bus->pcms[0];

 out:
	return ret;
}

static void aaci_free_card(struct snd_card *card)
{
	struct aaci *aaci = card->private_data;
	if (aaci->base)
		iounmap(aaci->base);
}

static struct aaci * __devinit aaci_init_card(struct amba_device *dev)
{
	struct aaci *aaci;
	struct snd_card *card;

	card = snd_card_new(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			    THIS_MODULE, sizeof(struct aaci));
	if (card == NULL)
		return ERR_PTR(-ENOMEM);

	card->private_free = aaci_free_card;

	strlcpy(card->driver, DRIVER_NAME, sizeof(card->driver));
	strlcpy(card->shortname, "ARM AC'97 Interface", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%08lx, irq %d",
		 card->shortname, dev->res.start, dev->irq[0]);

	aaci = card->private_data;
	init_MUTEX(&aaci->ac97_sem);
	spin_lock_init(&aaci->lock);
	aaci->card = card;
	aaci->dev = dev;

	/* Set MAINCR to allow slot 1 and 2 data IO */
	aaci->maincr = MAINCR_IE | MAINCR_SL1RXEN | MAINCR_SL1TXEN |
		       MAINCR_SL2RXEN | MAINCR_SL2TXEN;

	return aaci;
}

static int __devinit aaci_init_pcm(struct aaci *aaci)
{
	struct snd_pcm *pcm;
	int ret;

	ret = snd_pcm_new(aaci->card, "AACI AC'97", 0, 1, 0, &pcm);
	if (ret == 0) {
		aaci->pcm = pcm;
		pcm->private_data = aaci;
		pcm->info_flags = 0;

		strlcpy(pcm->name, DRIVER_NAME, sizeof(pcm->name));

		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &aaci_playback_ops);
	}

	return ret;
}

static unsigned int __devinit aaci_size_fifo(struct aaci *aaci)
{
	void __iomem *base = aaci->base + AACI_CSCH1;
	int i;

	writel(TXCR_FEN | TXCR_TSZ16 | TXCR_TXEN, base + AACI_TXCR);

	for (i = 0; !(readl(base + AACI_SR) & SR_TXFF) && i < 4096; i++)
		writel(0, aaci->base + AACI_DR1);

	writel(0, base + AACI_TXCR);

	/*
	 * Re-initialise the AACI after the FIFO depth test, to
	 * ensure that the FIFOs are empty.  Unfortunately, merely
	 * disabling the channel doesn't clear the FIFO.
	 */
	writel(aaci->maincr & ~MAINCR_IE, aaci->base + AACI_MAINCR);
	writel(aaci->maincr, aaci->base + AACI_MAINCR);

	/*
	 * If we hit 4096, we failed.  Go back to the specified
	 * fifo depth.
	 */
	if (i == 4096)
		i = 8;

	return i;
}

static int __devinit aaci_probe(struct amba_device *dev, void *id)
{
	struct aaci *aaci;
	int ret, i;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		return ret;

	aaci = aaci_init_card(dev);
	if (IS_ERR(aaci)) {
		ret = PTR_ERR(aaci);
		goto out;
	}

	aaci->base = ioremap(dev->res.start, SZ_4K);
	if (!aaci->base) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Playback uses AACI channel 0
	 */
	aaci->playback.base = aaci->base + AACI_CSCH1;
	aaci->playback.fifo = aaci->base + AACI_DR1;

	for (i = 0; i < 4; i++) {
		void __iomem *base = aaci->base + i * 0x14;

		writel(0, base + AACI_IE);
		writel(0, base + AACI_TXCR);
		writel(0, base + AACI_RXCR);
	}

	writel(0x1fff, aaci->base + AACI_INTCLR);
	writel(aaci->maincr, aaci->base + AACI_MAINCR);

	/*
	 * Size the FIFOs.
	 */
	aaci->fifosize = aaci_size_fifo(aaci);

	ret = aaci_probe_ac97(aaci);
	if (ret)
		goto out;

	ret = aaci_init_pcm(aaci);
	if (ret)
		goto out;

	snd_card_set_dev(aaci->card, &dev->dev);

	ret = snd_card_register(aaci->card);
	if (ret == 0) {
		dev_info(&dev->dev, "%s, fifo %d\n", aaci->card->longname,
			aaci->fifosize);
		amba_set_drvdata(dev, aaci->card);
		return ret;
	}

 out:
	if (aaci)
		snd_card_free(aaci->card);
	amba_release_regions(dev);
	return ret;
}

static int __devexit aaci_remove(struct amba_device *dev)
{
	struct snd_card *card = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

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

static struct amba_driver aaci_driver = {
	.drv		= {
		.name	= DRIVER_NAME,
	},
	.probe		= aaci_probe,
	.remove		= __devexit_p(aaci_remove),
	.suspend	= aaci_suspend,
	.resume		= aaci_resume,
	.id_table	= aaci_ids,
};

static int __init aaci_init(void)
{
	return amba_driver_register(&aaci_driver);
}

static void __exit aaci_exit(void)
{
	amba_driver_unregister(&aaci_driver);
}

module_init(aaci_init);
module_exit(aaci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM PrimeCell PL041 Advanced Audio CODEC Interface driver");
