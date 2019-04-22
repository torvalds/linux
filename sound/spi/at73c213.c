/*
 * Driver for AT73C213 16-bit stereo DAC connected to Atmel SSC
 *
 * Copyright (C) 2006-2007 Atmel Norway
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*#define DEBUG*/

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <sound/initval.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include <linux/atmel-ssc.h>

#include <linux/spi/spi.h>
#include <linux/spi/at73c213.h>

#include "at73c213.h"

#define BITRATE_MIN	 8000 /* Hardware limit? */
#define BITRATE_TARGET	CONFIG_SND_AT73C213_TARGET_BITRATE
#define BITRATE_MAX	50000 /* Hardware limit. */

/* Initial (hardware reset) AT73C213 register values. */
static u8 snd_at73c213_original_image[18] =
{
	0x00,	/* 00 - CTRL    */
	0x05,	/* 01 - LLIG    */
	0x05,	/* 02 - RLIG    */
	0x08,	/* 03 - LPMG    */
	0x08,	/* 04 - RPMG    */
	0x00,	/* 05 - LLOG    */
	0x00,	/* 06 - RLOG    */
	0x22,	/* 07 - OLC     */
	0x09,	/* 08 - MC      */
	0x00,	/* 09 - CSFC    */
	0x00,	/* 0A - MISC    */
	0x00,	/* 0B -         */
	0x00,	/* 0C - PRECH   */
	0x05,	/* 0D - AUXG    */
	0x00,	/* 0E -         */
	0x00,	/* 0F -         */
	0x00,	/* 10 - RST     */
	0x00,	/* 11 - PA_CTRL */
};

struct snd_at73c213 {
	struct snd_card			*card;
	struct snd_pcm			*pcm;
	struct snd_pcm_substream	*substream;
	struct at73c213_board_info	*board;
	int				irq;
	int				period;
	unsigned long			bitrate;
	struct ssc_device		*ssc;
	struct spi_device		*spi;
	u8				spi_wbuffer[2];
	u8				spi_rbuffer[2];
	/* Image of the SPI registers in AT73C213. */
	u8				reg_image[18];
	/* Protect SSC registers against concurrent access. */
	spinlock_t			lock;
	/* Protect mixer registers against concurrent access. */
	struct mutex			mixer_lock;
};

#define get_chip(card) ((struct snd_at73c213 *)card->private_data)

static int
snd_at73c213_write_reg(struct snd_at73c213 *chip, u8 reg, u8 val)
{
	struct spi_message msg;
	struct spi_transfer msg_xfer = {
		.len		= 2,
		.cs_change	= 0,
	};
	int retval;

	spi_message_init(&msg);

	chip->spi_wbuffer[0] = reg;
	chip->spi_wbuffer[1] = val;

	msg_xfer.tx_buf = chip->spi_wbuffer;
	msg_xfer.rx_buf = chip->spi_rbuffer;
	spi_message_add_tail(&msg_xfer, &msg);

	retval = spi_sync(chip->spi, &msg);

	if (!retval)
		chip->reg_image[reg] = val;

	return retval;
}

static struct snd_pcm_hardware snd_at73c213_playback_hw = {
	.info		= SNDRV_PCM_INFO_INTERLEAVED |
			  SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats	= SNDRV_PCM_FMTBIT_S16_BE,
	.rates		= SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min	= 8000,  /* Replaced by chip->bitrate later. */
	.rate_max	= 50000, /* Replaced by chip->bitrate later. */
	.channels_min	= 1,
	.channels_max	= 2,
	.buffer_bytes_max = 64 * 1024 - 1,
	.period_bytes_min = 512,
	.period_bytes_max = 64 * 1024 - 1,
	.periods_min	= 4,
	.periods_max	= 1024,
};

/*
 * Calculate and set bitrate and divisions.
 */
static int snd_at73c213_set_bitrate(struct snd_at73c213 *chip)
{
	unsigned long ssc_rate = clk_get_rate(chip->ssc->clk);
	unsigned long dac_rate_new, ssc_div;
	int status;
	unsigned long ssc_div_max, ssc_div_min;
	int max_tries;

	/*
	 * We connect two clocks here, picking divisors so the I2S clocks
	 * out data at the same rate the DAC clocks it in ... and as close
	 * as practical to the desired target rate.
	 *
	 * The DAC master clock (MCLK) is programmable, and is either 256
	 * or (not here) 384 times the I2S output clock (BCLK).
	 */

	/* SSC clock / (bitrate * stereo * 16-bit). */
	ssc_div = ssc_rate / (BITRATE_TARGET * 2 * 16);
	ssc_div_min = ssc_rate / (BITRATE_MAX * 2 * 16);
	ssc_div_max = ssc_rate / (BITRATE_MIN * 2 * 16);
	max_tries = (ssc_div_max - ssc_div_min) / 2;

	if (max_tries < 1)
		max_tries = 1;

	/* ssc_div must be even. */
	ssc_div = (ssc_div + 1) & ~1UL;

	if ((ssc_rate / (ssc_div * 2 * 16)) < BITRATE_MIN) {
		ssc_div -= 2;
		if ((ssc_rate / (ssc_div * 2 * 16)) > BITRATE_MAX)
			return -ENXIO;
	}

	/* Search for a possible bitrate. */
	do {
		/* SSC clock / (ssc divider * 16-bit * stereo). */
		if ((ssc_rate / (ssc_div * 2 * 16)) < BITRATE_MIN)
			return -ENXIO;

		/* 256 / (2 * 16) = 8 */
		dac_rate_new = 8 * (ssc_rate / ssc_div);

		status = clk_round_rate(chip->board->dac_clk, dac_rate_new);
		if (status <= 0)
			return status;

		/* Ignore difference smaller than 256 Hz. */
		if ((status/256) == (dac_rate_new/256))
			goto set_rate;

		ssc_div += 2;
	} while (--max_tries);

	/* Not able to find a valid bitrate. */
	return -ENXIO;

set_rate:
	status = clk_set_rate(chip->board->dac_clk, status);
	if (status < 0)
		return status;

	/* Set divider in SSC device. */
	ssc_writel(chip->ssc->regs, CMR, ssc_div/2);

	/* SSC clock / (ssc divider * 16-bit * stereo). */
	chip->bitrate = ssc_rate / (ssc_div * 16 * 2);

	dev_info(&chip->spi->dev,
			"at73c213: supported bitrate is %lu (%lu divider)\n",
			chip->bitrate, ssc_div);

	return 0;
}

static int snd_at73c213_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	/* ensure buffer_size is a multiple of period_size */
	err = snd_pcm_hw_constraint_integer(runtime,
					SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	snd_at73c213_playback_hw.rate_min = chip->bitrate;
	snd_at73c213_playback_hw.rate_max = chip->bitrate;
	runtime->hw = snd_at73c213_playback_hw;
	chip->substream = substream;

	clk_enable(chip->ssc->clk);

	return 0;
}

static int snd_at73c213_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	chip->substream = NULL;
	clk_disable(chip->ssc->clk);
	return 0;
}

static int snd_at73c213_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	int channels = params_channels(hw_params);
	int val;

	val = ssc_readl(chip->ssc->regs, TFMR);
	val = SSC_BFINS(TFMR_DATNB, channels - 1, val);
	ssc_writel(chip->ssc->regs, TFMR, val);

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int snd_at73c213_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_at73c213_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int block_size;

	block_size = frames_to_bytes(runtime, runtime->period_size);

	chip->period = 0;

	ssc_writel(chip->ssc->regs, PDC_TPR,
			(long)runtime->dma_addr);
	ssc_writel(chip->ssc->regs, PDC_TCR,
			runtime->period_size * runtime->channels);
	ssc_writel(chip->ssc->regs, PDC_TNPR,
			(long)runtime->dma_addr + block_size);
	ssc_writel(chip->ssc->regs, PDC_TNCR,
			runtime->period_size * runtime->channels);

	return 0;
}

static int snd_at73c213_pcm_trigger(struct snd_pcm_substream *substream,
				   int cmd)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	int retval = 0;

	spin_lock(&chip->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ssc_writel(chip->ssc->regs, IER, SSC_BIT(IER_ENDTX));
		ssc_writel(chip->ssc->regs, PDC_PTCR, SSC_BIT(PDC_PTCR_TXTEN));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ssc_writel(chip->ssc->regs, PDC_PTCR, SSC_BIT(PDC_PTCR_TXTDIS));
		ssc_writel(chip->ssc->regs, IDR, SSC_BIT(IDR_ENDTX));
		break;
	default:
		dev_dbg(&chip->spi->dev, "spurious command %x\n", cmd);
		retval = -EINVAL;
		break;
	}

	spin_unlock(&chip->lock);

	return retval;
}

static snd_pcm_uframes_t
snd_at73c213_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_at73c213 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	unsigned long bytes;

	bytes = ssc_readl(chip->ssc->regs, PDC_TPR)
		- (unsigned long)runtime->dma_addr;

	pos = bytes_to_frames(runtime, bytes);
	if (pos >= runtime->buffer_size)
		pos -= runtime->buffer_size;

	return pos;
}

static const struct snd_pcm_ops at73c213_playback_ops = {
	.open		= snd_at73c213_pcm_open,
	.close		= snd_at73c213_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_at73c213_pcm_hw_params,
	.hw_free	= snd_at73c213_pcm_hw_free,
	.prepare	= snd_at73c213_pcm_prepare,
	.trigger	= snd_at73c213_pcm_trigger,
	.pointer	= snd_at73c213_pcm_pointer,
};

static int snd_at73c213_pcm_new(struct snd_at73c213 *chip, int device)
{
	struct snd_pcm *pcm;
	int retval;

	retval = snd_pcm_new(chip->card, chip->card->shortname,
			device, 1, 0, &pcm);
	if (retval < 0)
		goto out;

	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_BLOCK_TRANSFER;
	strcpy(pcm->name, "at73c213");
	chip->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &at73c213_playback_ops);

	snd_pcm_lib_preallocate_pages_for_all(chip->pcm,
			SNDRV_DMA_TYPE_DEV, &chip->ssc->pdev->dev,
			64 * 1024, 64 * 1024);
out:
	return retval;
}

static irqreturn_t snd_at73c213_interrupt(int irq, void *dev_id)
{
	struct snd_at73c213 *chip = dev_id;
	struct snd_pcm_runtime *runtime = chip->substream->runtime;
	u32 status;
	int offset;
	int block_size;
	int next_period;
	int retval = IRQ_NONE;

	spin_lock(&chip->lock);

	block_size = frames_to_bytes(runtime, runtime->period_size);
	status = ssc_readl(chip->ssc->regs, IMR);

	if (status & SSC_BIT(IMR_ENDTX)) {
		chip->period++;
		if (chip->period == runtime->periods)
			chip->period = 0;
		next_period = chip->period + 1;
		if (next_period == runtime->periods)
			next_period = 0;

		offset = block_size * next_period;

		ssc_writel(chip->ssc->regs, PDC_TNPR,
				(long)runtime->dma_addr + offset);
		ssc_writel(chip->ssc->regs, PDC_TNCR,
				runtime->period_size * runtime->channels);
		retval = IRQ_HANDLED;
	}

	ssc_readl(chip->ssc->regs, IMR);
	spin_unlock(&chip->lock);

	if (status & SSC_BIT(IMR_ENDTX))
		snd_pcm_period_elapsed(chip->substream);

	return retval;
}

/*
 * Mixer functions.
 */
static int snd_at73c213_mono_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;

	mutex_lock(&chip->mixer_lock);

	ucontrol->value.integer.value[0] =
		(chip->reg_image[reg] >> shift) & mask;

	if (invert)
		ucontrol->value.integer.value[0] =
			mask - ucontrol->value.integer.value[0];

	mutex_unlock(&chip->mixer_lock);

	return 0;
}

static int snd_at73c213_mono_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change, retval;
	unsigned short val;

	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;

	mutex_lock(&chip->mixer_lock);

	val = (chip->reg_image[reg] & ~(mask << shift)) | val;
	change = val != chip->reg_image[reg];
	retval = snd_at73c213_write_reg(chip, reg, val);

	mutex_unlock(&chip->mixer_lock);

	if (retval)
		return retval;

	return change;
}

static int snd_at73c213_stereo_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	if (mask == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;

	return 0;
}

static int snd_at73c213_stereo_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;

	mutex_lock(&chip->mixer_lock);

	ucontrol->value.integer.value[0] =
		(chip->reg_image[left_reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] =
		(chip->reg_image[right_reg] >> shift_right) & mask;

	if (invert) {
		ucontrol->value.integer.value[0] =
			mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] =
			mask - ucontrol->value.integer.value[1];
	}

	mutex_unlock(&chip->mixer_lock);

	return 0;
}

static int snd_at73c213_stereo_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change, retval;
	unsigned short val1, val2;

	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;

	mutex_lock(&chip->mixer_lock);

	val1 = (chip->reg_image[left_reg] & ~(mask << shift_left)) | val1;
	val2 = (chip->reg_image[right_reg] & ~(mask << shift_right)) | val2;
	change = val1 != chip->reg_image[left_reg]
		|| val2 != chip->reg_image[right_reg];
	retval = snd_at73c213_write_reg(chip, left_reg, val1);
	if (retval) {
		mutex_unlock(&chip->mixer_lock);
		goto out;
	}
	retval = snd_at73c213_write_reg(chip, right_reg, val2);
	if (retval) {
		mutex_unlock(&chip->mixer_lock);
		goto out;
	}

	mutex_unlock(&chip->mixer_lock);

	return change;

out:
	return retval;
}

#define snd_at73c213_mono_switch_info	snd_ctl_boolean_mono_info

static int snd_at73c213_mono_switch_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;

	mutex_lock(&chip->mixer_lock);

	ucontrol->value.integer.value[0] =
		(chip->reg_image[reg] >> shift) & 0x01;

	if (invert)
		ucontrol->value.integer.value[0] =
			0x01 - ucontrol->value.integer.value[0];

	mutex_unlock(&chip->mixer_lock);

	return 0;
}

static int snd_at73c213_mono_switch_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_at73c213 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change, retval;
	unsigned short val;

	if (ucontrol->value.integer.value[0])
		val = mask;
	else
		val = 0;

	if (invert)
		val = mask - val;
	val <<= shift;

	mutex_lock(&chip->mixer_lock);

	val |= (chip->reg_image[reg] & ~(mask << shift));
	change = val != chip->reg_image[reg];

	retval = snd_at73c213_write_reg(chip, reg, val);

	mutex_unlock(&chip->mixer_lock);

	if (retval)
		return retval;

	return change;
}

static int snd_at73c213_pa_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = ((kcontrol->private_value >> 16) & 0xff) - 1;

	return 0;
}

static int snd_at73c213_line_capture_volume_info(
		struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	/* When inverted will give values 0x10001 => 0. */
	uinfo->value.integer.min = 14;
	uinfo->value.integer.max = 31;

	return 0;
}

static int snd_at73c213_aux_capture_volume_info(
		struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	/* When inverted will give values 0x10001 => 0. */
	uinfo->value.integer.min = 14;
	uinfo->value.integer.max = 31;

	return 0;
}

#define AT73C213_MONO_SWITCH(xname, xindex, reg, shift, mask, invert)	\
{									\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name = xname,							\
	.index = xindex,						\
	.info = snd_at73c213_mono_switch_info,				\
	.get = snd_at73c213_mono_switch_get,				\
	.put = snd_at73c213_mono_switch_put,				\
	.private_value = (reg | (shift << 8) | (mask << 16) | (invert << 24)) \
}

#define AT73C213_STEREO(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{									\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name = xname,							\
	.index = xindex,						\
	.info = snd_at73c213_stereo_info,				\
	.get = snd_at73c213_stereo_get,					\
	.put = snd_at73c213_stereo_put,					\
	.private_value = (left_reg | (right_reg << 8)			\
			| (shift_left << 16) | (shift_right << 19)	\
			| (mask << 24) | (invert << 22))		\
}

static struct snd_kcontrol_new snd_at73c213_controls[] = {
AT73C213_STEREO("Master Playback Volume", 0, DAC_LMPG, DAC_RMPG, 0, 0, 0x1f, 1),
AT73C213_STEREO("Master Playback Switch", 0, DAC_LMPG, DAC_RMPG, 5, 5, 1, 1),
AT73C213_STEREO("PCM Playback Volume", 0, DAC_LLOG, DAC_RLOG, 0, 0, 0x1f, 1),
AT73C213_STEREO("PCM Playback Switch", 0, DAC_LLOG, DAC_RLOG, 5, 5, 1, 1),
AT73C213_MONO_SWITCH("Mono PA Playback Switch", 0, DAC_CTRL, DAC_CTRL_ONPADRV,
		     0x01, 0),
{
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= "PA Playback Volume",
	.index	= 0,
	.info	= snd_at73c213_pa_volume_info,
	.get	= snd_at73c213_mono_get,
	.put	= snd_at73c213_mono_put,
	.private_value	= PA_CTRL | (PA_CTRL_APAGAIN << 8) | \
		(0x0f << 16) | (1 << 24),
},
AT73C213_MONO_SWITCH("PA High Gain Playback Switch", 0, PA_CTRL, PA_CTRL_APALP,
		     0x01, 1),
AT73C213_MONO_SWITCH("PA Playback Switch", 0, PA_CTRL, PA_CTRL_APAON, 0x01, 0),
{
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= "Aux Capture Volume",
	.index	= 0,
	.info	= snd_at73c213_aux_capture_volume_info,
	.get	= snd_at73c213_mono_get,
	.put	= snd_at73c213_mono_put,
	.private_value	= DAC_AUXG | (0 << 8) | (0x1f << 16) | (1 << 24),
},
AT73C213_MONO_SWITCH("Aux Capture Switch", 0, DAC_CTRL, DAC_CTRL_ONAUXIN,
		     0x01, 0),
{
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= "Line Capture Volume",
	.index	= 0,
	.info	= snd_at73c213_line_capture_volume_info,
	.get	= snd_at73c213_stereo_get,
	.put	= snd_at73c213_stereo_put,
	.private_value	= DAC_LLIG | (DAC_RLIG << 8) | (0 << 16) | (0 << 19)
		| (0x1f << 24) | (1 << 22),
},
AT73C213_MONO_SWITCH("Line Capture Switch", 0, DAC_CTRL, 0, 0x03, 0),
};

static int snd_at73c213_mixer(struct snd_at73c213 *chip)
{
	struct snd_card *card;
	int errval, idx;

	if (chip == NULL || chip->pcm == NULL)
		return -EINVAL;

	card = chip->card;

	strcpy(card->mixername, chip->pcm->name);

	for (idx = 0; idx < ARRAY_SIZE(snd_at73c213_controls); idx++) {
		errval = snd_ctl_add(card,
				snd_ctl_new1(&snd_at73c213_controls[idx],
					chip));
		if (errval < 0)
			goto cleanup;
	}

	return 0;

cleanup:
	for (idx = 1; idx < ARRAY_SIZE(snd_at73c213_controls) + 1; idx++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_find_numid(card, idx);
		if (kctl)
			snd_ctl_remove(card, kctl);
	}
	return errval;
}

/*
 * Device functions
 */
static int snd_at73c213_ssc_init(struct snd_at73c213 *chip)
{
	/*
	 * Continuous clock output.
	 * Starts on falling TF.
	 * Delay 1 cycle (1 bit).
	 * Periode is 16 bit (16 - 1).
	 */
	ssc_writel(chip->ssc->regs, TCMR,
			SSC_BF(TCMR_CKO, 1)
			| SSC_BF(TCMR_START, 4)
			| SSC_BF(TCMR_STTDLY, 1)
			| SSC_BF(TCMR_PERIOD, 16 - 1));
	/*
	 * Data length is 16 bit (16 - 1).
	 * Transmit MSB first.
	 * Transmit 2 words each transfer.
	 * Frame sync length is 16 bit (16 - 1).
	 * Frame starts on negative pulse.
	 */
	ssc_writel(chip->ssc->regs, TFMR,
			SSC_BF(TFMR_DATLEN, 16 - 1)
			| SSC_BIT(TFMR_MSBF)
			| SSC_BF(TFMR_DATNB, 1)
			| SSC_BF(TFMR_FSLEN, 16 - 1)
			| SSC_BF(TFMR_FSOS, 1));

	return 0;
}

static int snd_at73c213_chip_init(struct snd_at73c213 *chip)
{
	int retval;
	unsigned char dac_ctrl = 0;

	retval = snd_at73c213_set_bitrate(chip);
	if (retval)
		goto out;

	/* Enable DAC master clock. */
	clk_enable(chip->board->dac_clk);

	/* Initialize at73c213 on SPI bus. */
	retval = snd_at73c213_write_reg(chip, DAC_RST, 0x04);
	if (retval)
		goto out_clk;
	msleep(1);
	retval = snd_at73c213_write_reg(chip, DAC_RST, 0x03);
	if (retval)
		goto out_clk;

	/* Precharge everything. */
	retval = snd_at73c213_write_reg(chip, DAC_PRECH, 0xff);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, PA_CTRL, (1<<PA_CTRL_APAPRECH));
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_CTRL,
			(1<<DAC_CTRL_ONLNOL) | (1<<DAC_CTRL_ONLNOR));
	if (retval)
		goto out_clk;

	msleep(50);

	/* Stop precharging PA. */
	retval = snd_at73c213_write_reg(chip, PA_CTRL,
			(1<<PA_CTRL_APALP) | 0x0f);
	if (retval)
		goto out_clk;

	msleep(450);

	/* Stop precharging DAC, turn on master power. */
	retval = snd_at73c213_write_reg(chip, DAC_PRECH, (1<<DAC_PRECH_ONMSTR));
	if (retval)
		goto out_clk;

	msleep(1);

	/* Turn on DAC. */
	dac_ctrl = (1<<DAC_CTRL_ONDACL) | (1<<DAC_CTRL_ONDACR)
		| (1<<DAC_CTRL_ONLNOL) | (1<<DAC_CTRL_ONLNOR);

	retval = snd_at73c213_write_reg(chip, DAC_CTRL, dac_ctrl);
	if (retval)
		goto out_clk;

	/* Mute sound. */
	retval = snd_at73c213_write_reg(chip, DAC_LMPG, 0x3f);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_RMPG, 0x3f);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_LLOG, 0x3f);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_RLOG, 0x3f);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_LLIG, 0x11);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_RLIG, 0x11);
	if (retval)
		goto out_clk;
	retval = snd_at73c213_write_reg(chip, DAC_AUXG, 0x11);
	if (retval)
		goto out_clk;

	/* Enable I2S device, i.e. clock output. */
	ssc_writel(chip->ssc->regs, CR, SSC_BIT(CR_TXEN));

	goto out;

out_clk:
	clk_disable(chip->board->dac_clk);
out:
	return retval;
}

static int snd_at73c213_dev_free(struct snd_device *device)
{
	struct snd_at73c213 *chip = device->device_data;

	ssc_writel(chip->ssc->regs, CR, SSC_BIT(CR_TXDIS));
	if (chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}

	return 0;
}

static int snd_at73c213_dev_init(struct snd_card *card,
				 struct spi_device *spi)
{
	static struct snd_device_ops ops = {
		.dev_free	= snd_at73c213_dev_free,
	};
	struct snd_at73c213 *chip = get_chip(card);
	int irq, retval;

	irq = chip->ssc->irq;
	if (irq < 0)
		return irq;

	spin_lock_init(&chip->lock);
	mutex_init(&chip->mixer_lock);
	chip->card = card;
	chip->irq = -1;

	clk_enable(chip->ssc->clk);

	retval = request_irq(irq, snd_at73c213_interrupt, 0, "at73c213", chip);
	if (retval) {
		dev_dbg(&chip->spi->dev, "unable to request irq %d\n", irq);
		goto out;
	}
	chip->irq = irq;

	memcpy(&chip->reg_image, &snd_at73c213_original_image,
			sizeof(snd_at73c213_original_image));

	retval = snd_at73c213_ssc_init(chip);
	if (retval)
		goto out_irq;

	retval = snd_at73c213_chip_init(chip);
	if (retval)
		goto out_irq;

	retval = snd_at73c213_pcm_new(chip, 0);
	if (retval)
		goto out_irq;

	retval = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (retval)
		goto out_irq;

	retval = snd_at73c213_mixer(chip);
	if (retval)
		goto out_snd_dev;

	goto out;

out_snd_dev:
	snd_device_free(card, chip);
out_irq:
	free_irq(chip->irq, chip);
	chip->irq = -1;
out:
	clk_disable(chip->ssc->clk);

	return retval;
}

static int snd_at73c213_probe(struct spi_device *spi)
{
	struct snd_card			*card;
	struct snd_at73c213		*chip;
	struct at73c213_board_info	*board;
	int				retval;
	char				id[16];

	board = spi->dev.platform_data;
	if (!board) {
		dev_dbg(&spi->dev, "no platform_data\n");
		return -ENXIO;
	}

	if (!board->dac_clk) {
		dev_dbg(&spi->dev, "no DAC clk\n");
		return -ENXIO;
	}

	if (IS_ERR(board->dac_clk)) {
		dev_dbg(&spi->dev, "no DAC clk\n");
		return PTR_ERR(board->dac_clk);
	}

	/* Allocate "card" using some unused identifiers. */
	snprintf(id, sizeof id, "at73c213_%d", board->ssc_id);
	retval = snd_card_new(&spi->dev, -1, id, THIS_MODULE,
			      sizeof(struct snd_at73c213), &card);
	if (retval < 0)
		goto out;

	chip = card->private_data;
	chip->spi = spi;
	chip->board = board;

	chip->ssc = ssc_request(board->ssc_id);
	if (IS_ERR(chip->ssc)) {
		dev_dbg(&spi->dev, "could not get ssc%d device\n",
				board->ssc_id);
		retval = PTR_ERR(chip->ssc);
		goto out_card;
	}

	retval = snd_at73c213_dev_init(card, spi);
	if (retval)
		goto out_ssc;

	strcpy(card->driver, "at73c213");
	strcpy(card->shortname, board->shortname);
	sprintf(card->longname, "%s on irq %d", card->shortname, chip->irq);

	retval = snd_card_register(card);
	if (retval)
		goto out_ssc;

	dev_set_drvdata(&spi->dev, card);

	goto out;

out_ssc:
	ssc_free(chip->ssc);
out_card:
	snd_card_free(card);
out:
	return retval;
}

static int snd_at73c213_remove(struct spi_device *spi)
{
	struct snd_card *card = dev_get_drvdata(&spi->dev);
	struct snd_at73c213 *chip = card->private_data;
	int retval;

	/* Stop playback. */
	clk_enable(chip->ssc->clk);
	ssc_writel(chip->ssc->regs, CR, SSC_BIT(CR_TXDIS));
	clk_disable(chip->ssc->clk);

	/* Mute sound. */
	retval = snd_at73c213_write_reg(chip, DAC_LMPG, 0x3f);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_RMPG, 0x3f);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_LLOG, 0x3f);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_RLOG, 0x3f);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_LLIG, 0x11);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_RLIG, 0x11);
	if (retval)
		goto out;
	retval = snd_at73c213_write_reg(chip, DAC_AUXG, 0x11);
	if (retval)
		goto out;

	/* Turn off PA. */
	retval = snd_at73c213_write_reg(chip, PA_CTRL,
					chip->reg_image[PA_CTRL] | 0x0f);
	if (retval)
		goto out;
	msleep(10);
	retval = snd_at73c213_write_reg(chip, PA_CTRL,
					(1 << PA_CTRL_APALP) | 0x0f);
	if (retval)
		goto out;

	/* Turn off external DAC. */
	retval = snd_at73c213_write_reg(chip, DAC_CTRL, 0x0c);
	if (retval)
		goto out;
	msleep(2);
	retval = snd_at73c213_write_reg(chip, DAC_CTRL, 0x00);
	if (retval)
		goto out;

	/* Turn off master power. */
	retval = snd_at73c213_write_reg(chip, DAC_PRECH, 0x00);
	if (retval)
		goto out;

out:
	/* Stop DAC master clock. */
	clk_disable(chip->board->dac_clk);

	ssc_free(chip->ssc);
	snd_card_free(card);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int snd_at73c213_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_at73c213 *chip = card->private_data;

	ssc_writel(chip->ssc->regs, CR, SSC_BIT(CR_TXDIS));
	clk_disable(chip->ssc->clk);
	clk_disable(chip->board->dac_clk);

	return 0;
}

static int snd_at73c213_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_at73c213 *chip = card->private_data;

	clk_enable(chip->board->dac_clk);
	clk_enable(chip->ssc->clk);
	ssc_writel(chip->ssc->regs, CR, SSC_BIT(CR_TXEN));

	return 0;
}

static SIMPLE_DEV_PM_OPS(at73c213_pm_ops, snd_at73c213_suspend,
		snd_at73c213_resume);
#define AT73C213_PM_OPS (&at73c213_pm_ops)

#else
#define AT73C213_PM_OPS NULL
#endif

static struct spi_driver at73c213_driver = {
	.driver		= {
		.name	= "at73c213",
		.pm	= AT73C213_PM_OPS,
	},
	.probe		= snd_at73c213_probe,
	.remove		= snd_at73c213_remove,
};

module_spi_driver(at73c213_driver);

MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");
MODULE_DESCRIPTION("Sound driver for AT73C213 with Atmel SSC");
MODULE_LICENSE("GPL");
