/*
 * Driver for Atmel AC97C
 *
 * Copyright (C) 2005-2009 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/atmel_pdc.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>
#include <sound/memalloc.h>

#include "ac97c.h"

/* Serialize access to opened variable */
static DEFINE_MUTEX(opened_mutex);

struct atmel_ac97c {
	struct clk			*pclk;
	struct platform_device		*pdev;

	struct snd_pcm_substream	*playback_substream;
	struct snd_pcm_substream	*capture_substream;
	struct snd_card			*card;
	struct snd_pcm			*pcm;
	struct snd_ac97			*ac97;
	struct snd_ac97_bus		*ac97_bus;

	u64				cur_format;
	unsigned int			cur_rate;
	int				playback_period, capture_period;
	/* Serialize access to opened variable */
	spinlock_t			lock;
	void __iomem			*regs;
	int				irq;
	int				opened;
	struct gpio_desc		*reset_pin;
};

#define get_chip(card) ((struct atmel_ac97c *)(card)->private_data)

#define ac97c_writel(chip, reg, val)			\
	__raw_writel((val), (chip)->regs + AC97C_##reg)
#define ac97c_readl(chip, reg)				\
	__raw_readl((chip)->regs + AC97C_##reg)

static const struct snd_pcm_hardware atmel_ac97c_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP
				  | SNDRV_PCM_INFO_MMAP_VALID
				  | SNDRV_PCM_INFO_INTERLEAVED
				  | SNDRV_PCM_INFO_BLOCK_TRANSFER
				  | SNDRV_PCM_INFO_JOINT_DUPLEX
				  | SNDRV_PCM_INFO_RESUME
				  | SNDRV_PCM_INFO_PAUSE),
	.formats		= (SNDRV_PCM_FMTBIT_S16_BE
				  | SNDRV_PCM_FMTBIT_S16_LE),
	.rates			= (SNDRV_PCM_RATE_CONTINUOUS),
	.rate_min		= 4000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 2 * 2 * 64 * 2048,
	.period_bytes_min	= 4096,
	.period_bytes_max	= 4096,
	.periods_min		= 6,
	.periods_max		= 64,
};

static int atmel_ac97c_playback_open(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	mutex_lock(&opened_mutex);
	chip->opened++;
	runtime->hw = atmel_ac97c_hw;
	if (chip->cur_rate) {
		runtime->hw.rate_min = chip->cur_rate;
		runtime->hw.rate_max = chip->cur_rate;
	}
	if (chip->cur_format)
		runtime->hw.formats = pcm_format_to_bits(chip->cur_format);
	mutex_unlock(&opened_mutex);
	chip->playback_substream = substream;
	return 0;
}

static int atmel_ac97c_capture_open(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	mutex_lock(&opened_mutex);
	chip->opened++;
	runtime->hw = atmel_ac97c_hw;
	if (chip->cur_rate) {
		runtime->hw.rate_min = chip->cur_rate;
		runtime->hw.rate_max = chip->cur_rate;
	}
	if (chip->cur_format)
		runtime->hw.formats = pcm_format_to_bits(chip->cur_format);
	mutex_unlock(&opened_mutex);
	chip->capture_substream = substream;
	return 0;
}

static int atmel_ac97c_playback_close(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);

	mutex_lock(&opened_mutex);
	chip->opened--;
	if (!chip->opened) {
		chip->cur_rate = 0;
		chip->cur_format = 0;
	}
	mutex_unlock(&opened_mutex);

	chip->playback_substream = NULL;

	return 0;
}

static int atmel_ac97c_capture_close(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);

	mutex_lock(&opened_mutex);
	chip->opened--;
	if (!chip->opened) {
		chip->cur_rate = 0;
		chip->cur_format = 0;
	}
	mutex_unlock(&opened_mutex);

	chip->capture_substream = NULL;

	return 0;
}

static int atmel_ac97c_playback_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	int retval;

	retval = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (retval < 0)
		return retval;

	/* Set restrictions to params. */
	mutex_lock(&opened_mutex);
	chip->cur_rate = params_rate(hw_params);
	chip->cur_format = params_format(hw_params);
	mutex_unlock(&opened_mutex);

	return retval;
}

static int atmel_ac97c_capture_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	int retval;

	retval = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (retval < 0)
		return retval;

	/* Set restrictions to params. */
	mutex_lock(&opened_mutex);
	chip->cur_rate = params_rate(hw_params);
	chip->cur_format = params_format(hw_params);
	mutex_unlock(&opened_mutex);

	return retval;
}

static int atmel_ac97c_playback_prepare(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int block_size = frames_to_bytes(runtime, runtime->period_size);
	unsigned long word = ac97c_readl(chip, OCA);
	int retval;

	chip->playback_period = 0;
	word &= ~(AC97C_CH_MASK(PCM_LEFT) | AC97C_CH_MASK(PCM_RIGHT));

	/* assign channels to AC97C channel A */
	switch (runtime->channels) {
	case 1:
		word |= AC97C_CH_ASSIGN(PCM_LEFT, A);
		break;
	case 2:
		word |= AC97C_CH_ASSIGN(PCM_LEFT, A)
			| AC97C_CH_ASSIGN(PCM_RIGHT, A);
		break;
	default:
		/* TODO: support more than two channels */
		return -EINVAL;
	}
	ac97c_writel(chip, OCA, word);

	/* configure sample format and size */
	word = ac97c_readl(chip, CAMR);
	if (chip->opened <= 1)
		word = AC97C_CMR_DMAEN | AC97C_CMR_SIZE_16;
	else
		word |= AC97C_CMR_DMAEN | AC97C_CMR_SIZE_16;

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S16_BE: /* fall through */
		word &= ~(AC97C_CMR_CEM_LITTLE);
		break;
	default:
		word = ac97c_readl(chip, OCA);
		word &= ~(AC97C_CH_MASK(PCM_LEFT) | AC97C_CH_MASK(PCM_RIGHT));
		ac97c_writel(chip, OCA, word);
		return -EINVAL;
	}

	/* Enable underrun interrupt on channel A */
	word |= AC97C_CSR_UNRUN;

	ac97c_writel(chip, CAMR, word);

	/* Enable channel A event interrupt */
	word = ac97c_readl(chip, IMR);
	word |= AC97C_SR_CAEVT;
	ac97c_writel(chip, IER, word);

	/* set variable rate if needed */
	if (runtime->rate != 48000) {
		word = ac97c_readl(chip, MR);
		word |= AC97C_MR_VRA;
		ac97c_writel(chip, MR, word);
	} else {
		word = ac97c_readl(chip, MR);
		word &= ~(AC97C_MR_VRA);
		ac97c_writel(chip, MR, word);
	}

	retval = snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE,
			runtime->rate);
	if (retval)
		dev_dbg(&chip->pdev->dev, "could not set rate %d Hz\n",
				runtime->rate);

	/* Initialize and start the PDC */
	writel(runtime->dma_addr, chip->regs + ATMEL_PDC_TPR);
	writel(block_size / 2, chip->regs + ATMEL_PDC_TCR);
	writel(runtime->dma_addr + block_size, chip->regs + ATMEL_PDC_TNPR);
	writel(block_size / 2, chip->regs + ATMEL_PDC_TNCR);

	return retval;
}

static int atmel_ac97c_capture_prepare(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int block_size = frames_to_bytes(runtime, runtime->period_size);
	unsigned long word = ac97c_readl(chip, ICA);
	int retval;

	chip->capture_period = 0;
	word &= ~(AC97C_CH_MASK(PCM_LEFT) | AC97C_CH_MASK(PCM_RIGHT));

	/* assign channels to AC97C channel A */
	switch (runtime->channels) {
	case 1:
		word |= AC97C_CH_ASSIGN(PCM_LEFT, A);
		break;
	case 2:
		word |= AC97C_CH_ASSIGN(PCM_LEFT, A)
			| AC97C_CH_ASSIGN(PCM_RIGHT, A);
		break;
	default:
		/* TODO: support more than two channels */
		return -EINVAL;
	}
	ac97c_writel(chip, ICA, word);

	/* configure sample format and size */
	word = ac97c_readl(chip, CAMR);
	if (chip->opened <= 1)
		word = AC97C_CMR_DMAEN | AC97C_CMR_SIZE_16;
	else
		word |= AC97C_CMR_DMAEN | AC97C_CMR_SIZE_16;

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S16_BE: /* fall through */
		word &= ~(AC97C_CMR_CEM_LITTLE);
		break;
	default:
		word = ac97c_readl(chip, ICA);
		word &= ~(AC97C_CH_MASK(PCM_LEFT) | AC97C_CH_MASK(PCM_RIGHT));
		ac97c_writel(chip, ICA, word);
		return -EINVAL;
	}

	/* Enable overrun interrupt on channel A */
	word |= AC97C_CSR_OVRUN;

	ac97c_writel(chip, CAMR, word);

	/* Enable channel A event interrupt */
	word = ac97c_readl(chip, IMR);
	word |= AC97C_SR_CAEVT;
	ac97c_writel(chip, IER, word);

	/* set variable rate if needed */
	if (runtime->rate != 48000) {
		word = ac97c_readl(chip, MR);
		word |= AC97C_MR_VRA;
		ac97c_writel(chip, MR, word);
	} else {
		word = ac97c_readl(chip, MR);
		word &= ~(AC97C_MR_VRA);
		ac97c_writel(chip, MR, word);
	}

	retval = snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE,
			runtime->rate);
	if (retval)
		dev_dbg(&chip->pdev->dev, "could not set rate %d Hz\n",
				runtime->rate);

	/* Initialize and start the PDC */
	writel(runtime->dma_addr, chip->regs + ATMEL_PDC_RPR);
	writel(block_size / 2, chip->regs + ATMEL_PDC_RCR);
	writel(runtime->dma_addr + block_size, chip->regs + ATMEL_PDC_RNPR);
	writel(block_size / 2, chip->regs + ATMEL_PDC_RNCR);

	return retval;
}

static int
atmel_ac97c_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	unsigned long camr, ptcr = 0;

	camr = ac97c_readl(chip, CAMR);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		ptcr = ATMEL_PDC_TXTEN;
		camr |= AC97C_CMR_CENA | AC97C_CSR_ENDTX;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		ptcr |= ATMEL_PDC_TXTDIS;
		if (chip->opened <= 1)
			camr &= ~AC97C_CMR_CENA;
		break;
	default:
		return -EINVAL;
	}

	ac97c_writel(chip, CAMR, camr);
	writel(ptcr, chip->regs + ATMEL_PDC_PTCR);
	return 0;
}

static int
atmel_ac97c_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct atmel_ac97c *chip = snd_pcm_substream_chip(substream);
	unsigned long camr, ptcr = 0;

	camr = ac97c_readl(chip, CAMR);
	ptcr = readl(chip->regs + ATMEL_PDC_PTSR);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		ptcr = ATMEL_PDC_RXTEN;
		camr |= AC97C_CMR_CENA | AC97C_CSR_ENDRX;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		ptcr |= ATMEL_PDC_RXTDIS;
		if (chip->opened <= 1)
			camr &= ~AC97C_CMR_CENA;
		break;
	default:
		return -EINVAL;
	}

	ac97c_writel(chip, CAMR, camr);
	writel(ptcr, chip->regs + ATMEL_PDC_PTCR);
	return 0;
}

static snd_pcm_uframes_t
atmel_ac97c_playback_pointer(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c	*chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime	*runtime = substream->runtime;
	snd_pcm_uframes_t	frames;
	unsigned long		bytes;

	bytes = readl(chip->regs + ATMEL_PDC_TPR);
	bytes -= runtime->dma_addr;

	frames = bytes_to_frames(runtime, bytes);
	if (frames >= runtime->buffer_size)
		frames -= runtime->buffer_size;
	return frames;
}

static snd_pcm_uframes_t
atmel_ac97c_capture_pointer(struct snd_pcm_substream *substream)
{
	struct atmel_ac97c	*chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime	*runtime = substream->runtime;
	snd_pcm_uframes_t	frames;
	unsigned long		bytes;

	bytes = readl(chip->regs + ATMEL_PDC_RPR);
	bytes -= runtime->dma_addr;

	frames = bytes_to_frames(runtime, bytes);
	if (frames >= runtime->buffer_size)
		frames -= runtime->buffer_size;
	return frames;
}

static const struct snd_pcm_ops atmel_ac97_playback_ops = {
	.open		= atmel_ac97c_playback_open,
	.close		= atmel_ac97c_playback_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= atmel_ac97c_playback_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.prepare	= atmel_ac97c_playback_prepare,
	.trigger	= atmel_ac97c_playback_trigger,
	.pointer	= atmel_ac97c_playback_pointer,
};

static const struct snd_pcm_ops atmel_ac97_capture_ops = {
	.open		= atmel_ac97c_capture_open,
	.close		= atmel_ac97c_capture_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= atmel_ac97c_capture_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.prepare	= atmel_ac97c_capture_prepare,
	.trigger	= atmel_ac97c_capture_trigger,
	.pointer	= atmel_ac97c_capture_pointer,
};

static irqreturn_t atmel_ac97c_interrupt(int irq, void *dev)
{
	struct atmel_ac97c	*chip  = (struct atmel_ac97c *)dev;
	irqreturn_t		retval = IRQ_NONE;
	u32			sr     = ac97c_readl(chip, SR);
	u32			casr   = ac97c_readl(chip, CASR);
	u32			cosr   = ac97c_readl(chip, COSR);
	u32			camr   = ac97c_readl(chip, CAMR);

	if (sr & AC97C_SR_CAEVT) {
		struct snd_pcm_runtime *runtime;
		int offset, next_period, block_size;
		dev_dbg(&chip->pdev->dev, "channel A event%s%s%s%s%s%s\n",
				casr & AC97C_CSR_OVRUN   ? " OVRUN"   : "",
				casr & AC97C_CSR_RXRDY   ? " RXRDY"   : "",
				casr & AC97C_CSR_UNRUN   ? " UNRUN"   : "",
				casr & AC97C_CSR_TXEMPTY ? " TXEMPTY" : "",
				casr & AC97C_CSR_TXRDY   ? " TXRDY"   : "",
				!casr                    ? " NONE"    : "");
		if ((casr & camr) & AC97C_CSR_ENDTX) {
			runtime = chip->playback_substream->runtime;
			block_size = frames_to_bytes(runtime, runtime->period_size);
			chip->playback_period++;

			if (chip->playback_period == runtime->periods)
				chip->playback_period = 0;
			next_period = chip->playback_period + 1;
			if (next_period == runtime->periods)
				next_period = 0;

			offset = block_size * next_period;

			writel(runtime->dma_addr + offset, chip->regs + ATMEL_PDC_TNPR);
			writel(block_size / 2, chip->regs + ATMEL_PDC_TNCR);

			snd_pcm_period_elapsed(chip->playback_substream);
		}
		if ((casr & camr) & AC97C_CSR_ENDRX) {
			runtime = chip->capture_substream->runtime;
			block_size = frames_to_bytes(runtime, runtime->period_size);
			chip->capture_period++;

			if (chip->capture_period == runtime->periods)
				chip->capture_period = 0;
			next_period = chip->capture_period + 1;
			if (next_period == runtime->periods)
				next_period = 0;

			offset = block_size * next_period;

			writel(runtime->dma_addr + offset, chip->regs + ATMEL_PDC_RNPR);
			writel(block_size / 2, chip->regs + ATMEL_PDC_RNCR);
			snd_pcm_period_elapsed(chip->capture_substream);
		}
		retval = IRQ_HANDLED;
	}

	if (sr & AC97C_SR_COEVT) {
		dev_info(&chip->pdev->dev, "codec channel event%s%s%s%s%s\n",
				cosr & AC97C_CSR_OVRUN   ? " OVRUN"   : "",
				cosr & AC97C_CSR_RXRDY   ? " RXRDY"   : "",
				cosr & AC97C_CSR_TXEMPTY ? " TXEMPTY" : "",
				cosr & AC97C_CSR_TXRDY   ? " TXRDY"   : "",
				!cosr                    ? " NONE"    : "");
		retval = IRQ_HANDLED;
	}

	if (retval == IRQ_NONE) {
		dev_err(&chip->pdev->dev, "spurious interrupt sr 0x%08x "
				"casr 0x%08x cosr 0x%08x\n", sr, casr, cosr);
	}

	return retval;
}

static const struct ac97_pcm at91_ac97_pcm_defs[] = {
	/* Playback */
	{
		.exclusive = 1,
		.r = { {
			.slots = ((1 << AC97_SLOT_PCM_LEFT)
				  | (1 << AC97_SLOT_PCM_RIGHT)),
		} },
	},
	/* PCM in */
	{
		.stream = 1,
		.exclusive = 1,
		.r = { {
			.slots = ((1 << AC97_SLOT_PCM_LEFT)
					| (1 << AC97_SLOT_PCM_RIGHT)),
		} }
	},
	/* Mic in */
	{
		.stream = 1,
		.exclusive = 1,
		.r = { {
			.slots = (1<<AC97_SLOT_MIC),
		} }
	},
};

static int atmel_ac97c_pcm_new(struct atmel_ac97c *chip)
{
	struct snd_pcm		*pcm;
	struct snd_pcm_hardware	hw = atmel_ac97c_hw;
	int			retval;

	retval = snd_ac97_pcm_assign(chip->ac97_bus,
				     ARRAY_SIZE(at91_ac97_pcm_defs),
				     at91_ac97_pcm_defs);
	if (retval)
		return retval;

	retval = snd_pcm_new(chip->card, chip->card->shortname, 0, 1, 1, &pcm);
	if (retval)
		return retval;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &atmel_ac97_capture_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &atmel_ac97_playback_ops);

	retval = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
			&chip->pdev->dev, hw.periods_min * hw.period_bytes_min,
			hw.buffer_bytes_max);
	if (retval)
		return retval;

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;

	return 0;
}

static int atmel_ac97c_mixer_new(struct atmel_ac97c *chip)
{
	struct snd_ac97_template template;
	memset(&template, 0, sizeof(template));
	template.private_data = chip;
	return snd_ac97_mixer(chip->ac97_bus, &template, &chip->ac97);
}

static void atmel_ac97c_write(struct snd_ac97 *ac97, unsigned short reg,
		unsigned short val)
{
	struct atmel_ac97c *chip = get_chip(ac97);
	unsigned long word;
	int timeout = 40;

	word = (reg & 0x7f) << 16 | val;

	do {
		if (ac97c_readl(chip, COSR) & AC97C_CSR_TXRDY) {
			ac97c_writel(chip, COTHR, word);
			return;
		}
		udelay(1);
	} while (--timeout);

	dev_dbg(&chip->pdev->dev, "codec write timeout\n");
}

static unsigned short atmel_ac97c_read(struct snd_ac97 *ac97,
		unsigned short reg)
{
	struct atmel_ac97c *chip = get_chip(ac97);
	unsigned long word;
	int timeout = 40;
	int write = 10;

	word = (0x80 | (reg & 0x7f)) << 16;

	if ((ac97c_readl(chip, COSR) & AC97C_CSR_RXRDY) != 0)
		ac97c_readl(chip, CORHR);

retry_write:
	timeout = 40;

	do {
		if ((ac97c_readl(chip, COSR) & AC97C_CSR_TXRDY) != 0) {
			ac97c_writel(chip, COTHR, word);
			goto read_reg;
		}
		udelay(10);
	} while (--timeout);

	if (!--write)
		goto timed_out;
	goto retry_write;

read_reg:
	do {
		if ((ac97c_readl(chip, COSR) & AC97C_CSR_RXRDY) != 0) {
			unsigned short val = ac97c_readl(chip, CORHR);
			return val;
		}
		udelay(10);
	} while (--timeout);

	if (!--write)
		goto timed_out;
	goto retry_write;

timed_out:
	dev_dbg(&chip->pdev->dev, "codec read timeout\n");
	return 0xffff;
}

static void atmel_ac97c_reset(struct atmel_ac97c *chip)
{
	ac97c_writel(chip, MR,   0);
	ac97c_writel(chip, MR,   AC97C_MR_ENA);
	ac97c_writel(chip, CAMR, 0);
	ac97c_writel(chip, COMR, 0);

	if (!IS_ERR(chip->reset_pin)) {
		gpiod_set_value(chip->reset_pin, 0);
		/* AC97 v2.2 specifications says minimum 1 us. */
		udelay(2);
		gpiod_set_value(chip->reset_pin, 1);
	} else {
		ac97c_writel(chip, MR, AC97C_MR_WRST | AC97C_MR_ENA);
		udelay(2);
		ac97c_writel(chip, MR, AC97C_MR_ENA);
	}
}

static const struct of_device_id atmel_ac97c_dt_ids[] = {
	{ .compatible = "atmel,at91sam9263-ac97c", },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_ac97c_dt_ids);

static int atmel_ac97c_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	struct snd_card			*card;
	struct atmel_ac97c		*chip;
	struct resource			*regs;
	struct clk			*pclk;
	static struct snd_ac97_bus_ops	ops = {
		.write	= atmel_ac97c_write,
		.read	= atmel_ac97c_read,
	};
	int				retval;
	int				irq;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_dbg(&pdev->dev, "no memory resource\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_dbg(&pdev->dev, "could not get irq: %d\n", irq);
		return irq;
	}

	pclk = clk_get(&pdev->dev, "ac97_clk");
	if (IS_ERR(pclk)) {
		dev_dbg(&pdev->dev, "no peripheral clock\n");
		return PTR_ERR(pclk);
	}
	retval = clk_prepare_enable(pclk);
	if (retval)
		goto err_prepare_enable;

	retval = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1,
			      SNDRV_DEFAULT_STR1, THIS_MODULE,
			      sizeof(struct atmel_ac97c), &card);
	if (retval) {
		dev_dbg(&pdev->dev, "could not create sound card device\n");
		goto err_snd_card_new;
	}

	chip = get_chip(card);

	retval = request_irq(irq, atmel_ac97c_interrupt, 0, "AC97C", chip);
	if (retval) {
		dev_dbg(&pdev->dev, "unable to request irq %d\n", irq);
		goto err_request_irq;
	}
	chip->irq = irq;

	spin_lock_init(&chip->lock);

	strcpy(card->driver, "Atmel AC97C");
	strcpy(card->shortname, "Atmel AC97C");
	sprintf(card->longname, "Atmel AC97 controller");

	chip->card = card;
	chip->pclk = pclk;
	chip->pdev = pdev;
	chip->regs = ioremap(regs->start, resource_size(regs));

	if (!chip->regs) {
		dev_dbg(&pdev->dev, "could not remap register memory\n");
		retval = -ENOMEM;
		goto err_ioremap;
	}

	chip->reset_pin = devm_gpiod_get_index(dev, "ac97", 2, GPIOD_OUT_HIGH);
	if (IS_ERR(chip->reset_pin))
		dev_dbg(dev, "reset pin not available\n");

	atmel_ac97c_reset(chip);

	/* Enable overrun interrupt from codec channel */
	ac97c_writel(chip, COMR, AC97C_CSR_OVRUN);
	ac97c_writel(chip, IER, ac97c_readl(chip, IMR) | AC97C_SR_COEVT);

	retval = snd_ac97_bus(card, 0, &ops, chip, &chip->ac97_bus);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register on ac97 bus\n");
		goto err_ac97_bus;
	}

	retval = atmel_ac97c_mixer_new(chip);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register ac97 mixer\n");
		goto err_ac97_bus;
	}

	retval = atmel_ac97c_pcm_new(chip);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register ac97 pcm device\n");
		goto err_ac97_bus;
	}

	retval = snd_card_register(card);
	if (retval) {
		dev_dbg(&pdev->dev, "could not register sound card\n");
		goto err_ac97_bus;
	}

	platform_set_drvdata(pdev, card);

	dev_info(&pdev->dev, "Atmel AC97 controller at 0x%p, irq = %d\n",
			chip->regs, irq);

	return 0;

err_ac97_bus:
	iounmap(chip->regs);
err_ioremap:
	free_irq(irq, chip);
err_request_irq:
	snd_card_free(card);
err_snd_card_new:
	clk_disable_unprepare(pclk);
err_prepare_enable:
	clk_put(pclk);
	return retval;
}

#ifdef CONFIG_PM_SLEEP
static int atmel_ac97c_suspend(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct atmel_ac97c *chip = card->private_data;

	clk_disable_unprepare(chip->pclk);
	return 0;
}

static int atmel_ac97c_resume(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct atmel_ac97c *chip = card->private_data;
	int ret = clk_prepare_enable(chip->pclk);

	return ret;
}

static SIMPLE_DEV_PM_OPS(atmel_ac97c_pm, atmel_ac97c_suspend, atmel_ac97c_resume);
#define ATMEL_AC97C_PM_OPS	&atmel_ac97c_pm
#else
#define ATMEL_AC97C_PM_OPS	NULL
#endif

static int atmel_ac97c_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct atmel_ac97c *chip = get_chip(card);

	ac97c_writel(chip, CAMR, 0);
	ac97c_writel(chip, COMR, 0);
	ac97c_writel(chip, MR,   0);

	clk_disable_unprepare(chip->pclk);
	clk_put(chip->pclk);
	iounmap(chip->regs);
	free_irq(chip->irq, chip);

	snd_card_free(card);

	return 0;
}

static struct platform_driver atmel_ac97c_driver = {
	.probe		= atmel_ac97c_probe,
	.remove		= atmel_ac97c_remove,
	.driver		= {
		.name	= "atmel_ac97c",
		.pm	= ATMEL_AC97C_PM_OPS,
		.of_match_table = atmel_ac97c_dt_ids,
	},
};
module_platform_driver(atmel_ac97c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for Atmel AC97 controller");
MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");
