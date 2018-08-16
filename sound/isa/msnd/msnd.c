/*********************************************************************
 *
 * 2002/06/30 Karsten Wiese:
 *	removed kernel-version dependencies.
 *	ripped from linux kernel 2.4.18 (OSS Implementation) by me.
 *	In the OSS Version, this file is compiled to a separate MODULE,
 *	that is used by the pinnacle and the classic driver.
 *	since there is no classic driver for alsa yet (i dont have a classic
 *	& writing one blindfold is difficult) this file's object is statically
 *	linked into the pinnacle-driver-module for now.	look for the string
 *		"uncomment this to make this a module again"
 *	to do guess what.
 *
 * the following is a copy of the 2.4.18 OSS FREE file-heading comment:
 *
 * msnd.c - Driver Base
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 *
 * Copyright (C) 1998 Andrew Veliath
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ********************************************************************/

#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "msnd.h"

#define LOGNAME			"msnd"


void snd_msnd_init_queue(void __iomem *base, int start, int size)
{
	writew(PCTODSP_BASED(start), base + JQS_wStart);
	writew(PCTODSP_OFFSET(size) - 1, base + JQS_wSize);
	writew(0, base + JQS_wHead);
	writew(0, base + JQS_wTail);
}
EXPORT_SYMBOL(snd_msnd_init_queue);

static int snd_msnd_wait_TXDE(struct snd_msnd *dev)
{
	unsigned int io = dev->io;
	int timeout = 1000;

	while (timeout-- > 0)
		if (inb(io + HP_ISR) & HPISR_TXDE)
			return 0;

	return -EIO;
}

static int snd_msnd_wait_HC0(struct snd_msnd *dev)
{
	unsigned int io = dev->io;
	int timeout = 1000;

	while (timeout-- > 0)
		if (!(inb(io + HP_CVR) & HPCVR_HC))
			return 0;

	return -EIO;
}

int snd_msnd_send_dsp_cmd(struct snd_msnd *dev, u8 cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (snd_msnd_wait_HC0(dev) == 0) {
		outb(cmd, dev->io + HP_CVR);
		spin_unlock_irqrestore(&dev->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	snd_printd(KERN_ERR LOGNAME ": Send DSP command timeout\n");

	return -EIO;
}
EXPORT_SYMBOL(snd_msnd_send_dsp_cmd);

int snd_msnd_send_word(struct snd_msnd *dev, unsigned char high,
		   unsigned char mid, unsigned char low)
{
	unsigned int io = dev->io;

	if (snd_msnd_wait_TXDE(dev) == 0) {
		outb(high, io + HP_TXH);
		outb(mid, io + HP_TXM);
		outb(low, io + HP_TXL);
		return 0;
	}

	snd_printd(KERN_ERR LOGNAME ": Send host word timeout\n");

	return -EIO;
}
EXPORT_SYMBOL(snd_msnd_send_word);

int snd_msnd_upload_host(struct snd_msnd *dev, const u8 *bin, int len)
{
	int i;

	if (len % 3 != 0) {
		snd_printk(KERN_ERR LOGNAME
			   ": Upload host data not multiple of 3!\n");
		return -EINVAL;
	}

	for (i = 0; i < len; i += 3)
		if (snd_msnd_send_word(dev, bin[i], bin[i + 1], bin[i + 2]))
			return -EIO;

	inb(dev->io + HP_RXL);
	inb(dev->io + HP_CVR);

	return 0;
}
EXPORT_SYMBOL(snd_msnd_upload_host);

int snd_msnd_enable_irq(struct snd_msnd *dev)
{
	unsigned long flags;

	if (dev->irq_ref++)
		return 0;

	snd_printdd(LOGNAME ": Enabling IRQ\n");

	spin_lock_irqsave(&dev->lock, flags);
	if (snd_msnd_wait_TXDE(dev) == 0) {
		outb(inb(dev->io + HP_ICR) | HPICR_TREQ, dev->io + HP_ICR);
		if (dev->type == msndClassic)
			outb(dev->irqid, dev->io + HP_IRQM);

		outb(inb(dev->io + HP_ICR) & ~HPICR_TREQ, dev->io + HP_ICR);
		outb(inb(dev->io + HP_ICR) | HPICR_RREQ, dev->io + HP_ICR);
		enable_irq(dev->irq);
		snd_msnd_init_queue(dev->DSPQ, dev->dspq_data_buff,
				    dev->dspq_buff_size);
		spin_unlock_irqrestore(&dev->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	snd_printd(KERN_ERR LOGNAME ": Enable IRQ failed\n");

	return -EIO;
}
EXPORT_SYMBOL(snd_msnd_enable_irq);

int snd_msnd_disable_irq(struct snd_msnd *dev)
{
	unsigned long flags;

	if (--dev->irq_ref > 0)
		return 0;

	if (dev->irq_ref < 0)
		snd_printd(KERN_WARNING LOGNAME ": IRQ ref count is %d\n",
			   dev->irq_ref);

	snd_printdd(LOGNAME ": Disabling IRQ\n");

	spin_lock_irqsave(&dev->lock, flags);
	if (snd_msnd_wait_TXDE(dev) == 0) {
		outb(inb(dev->io + HP_ICR) & ~HPICR_RREQ, dev->io + HP_ICR);
		if (dev->type == msndClassic)
			outb(HPIRQ_NONE, dev->io + HP_IRQM);
		disable_irq(dev->irq);
		spin_unlock_irqrestore(&dev->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	snd_printd(KERN_ERR LOGNAME ": Disable IRQ failed\n");

	return -EIO;
}
EXPORT_SYMBOL(snd_msnd_disable_irq);

static inline long get_play_delay_jiffies(struct snd_msnd *chip, long size)
{
	long tmp = (size * HZ * chip->play_sample_size) / 8;
	return tmp / (chip->play_sample_rate * chip->play_channels);
}

static void snd_msnd_dsp_write_flush(struct snd_msnd *chip)
{
	if (!(chip->mode & FMODE_WRITE) || !test_bit(F_WRITING, &chip->flags))
		return;
	set_bit(F_WRITEFLUSH, &chip->flags);
/*	interruptible_sleep_on_timeout(
		&chip->writeflush,
		get_play_delay_jiffies(&chip, chip->DAPF.len));*/
	clear_bit(F_WRITEFLUSH, &chip->flags);
	if (!signal_pending(current))
		schedule_timeout_interruptible(
			get_play_delay_jiffies(chip, chip->play_period_bytes));
	clear_bit(F_WRITING, &chip->flags);
}

void snd_msnd_dsp_halt(struct snd_msnd *chip, struct file *file)
{
	if ((file ? file->f_mode : chip->mode) & FMODE_READ) {
		clear_bit(F_READING, &chip->flags);
		snd_msnd_send_dsp_cmd(chip, HDEX_RECORD_STOP);
		snd_msnd_disable_irq(chip);
		if (file) {
			snd_printd(KERN_INFO LOGNAME
				   ": Stopping read for %p\n", file);
			chip->mode &= ~FMODE_READ;
		}
		clear_bit(F_AUDIO_READ_INUSE, &chip->flags);
	}
	if ((file ? file->f_mode : chip->mode) & FMODE_WRITE) {
		if (test_bit(F_WRITING, &chip->flags)) {
			snd_msnd_dsp_write_flush(chip);
			snd_msnd_send_dsp_cmd(chip, HDEX_PLAY_STOP);
		}
		snd_msnd_disable_irq(chip);
		if (file) {
			snd_printd(KERN_INFO
				   LOGNAME ": Stopping write for %p\n", file);
			chip->mode &= ~FMODE_WRITE;
		}
		clear_bit(F_AUDIO_WRITE_INUSE, &chip->flags);
	}
}
EXPORT_SYMBOL(snd_msnd_dsp_halt);


int snd_msnd_DARQ(struct snd_msnd *chip, int bank)
{
	int /*size, n,*/ timeout = 3;
	u16 wTmp;
	/* void *DAQD; */

	/* Increment the tail and check for queue wrap */
	wTmp = readw(chip->DARQ + JQS_wTail) + PCTODSP_OFFSET(DAQDS__size);
	if (wTmp > readw(chip->DARQ + JQS_wSize))
		wTmp = 0;
	while (wTmp == readw(chip->DARQ + JQS_wHead) && timeout--)
		udelay(1);

	if (chip->capturePeriods == 2) {
		void __iomem *pDAQ = chip->mappedbase + DARQ_DATA_BUFF +
			     bank * DAQDS__size + DAQDS_wStart;
		unsigned short offset = 0x3000 + chip->capturePeriodBytes;

		if (readw(pDAQ) != PCTODSP_BASED(0x3000))
			offset = 0x3000;
		writew(PCTODSP_BASED(offset), pDAQ);
	}

	writew(wTmp, chip->DARQ + JQS_wTail);

#if 0
	/* Get our digital audio queue struct */
	DAQD = bank * DAQDS__size + chip->mappedbase + DARQ_DATA_BUFF;

	/* Get length of data */
	size = readw(DAQD + DAQDS_wSize);

	/* Read data from the head (unprotected bank 1 access okay
	   since this is only called inside an interrupt) */
	outb(HPBLKSEL_1, chip->io + HP_BLKS);
	n = msnd_fifo_write(&chip->DARF,
			    (char *)(chip->base + bank * DAR_BUFF_SIZE),
			    size, 0);
	if (n <= 0) {
		outb(HPBLKSEL_0, chip->io + HP_BLKS);
		return n;
	}
	outb(HPBLKSEL_0, chip->io + HP_BLKS);
#endif

	return 1;
}
EXPORT_SYMBOL(snd_msnd_DARQ);

int snd_msnd_DAPQ(struct snd_msnd *chip, int start)
{
	u16	DAPQ_tail;
	int	protect = start, nbanks = 0;
	void	__iomem *DAQD;
	static int play_banks_submitted;
	/* unsigned long flags;
	spin_lock_irqsave(&chip->lock, flags); not necessary */

	DAPQ_tail = readw(chip->DAPQ + JQS_wTail);
	while (DAPQ_tail != readw(chip->DAPQ + JQS_wHead) || start) {
		int bank_num = DAPQ_tail / PCTODSP_OFFSET(DAQDS__size);

		if (start) {
			start = 0;
			play_banks_submitted = 0;
		}

		/* Get our digital audio queue struct */
		DAQD = bank_num * DAQDS__size + chip->mappedbase +
			DAPQ_DATA_BUFF;

		/* Write size of this bank */
		writew(chip->play_period_bytes, DAQD + DAQDS_wSize);
		if (play_banks_submitted < 3)
			++play_banks_submitted;
		else if (chip->playPeriods == 2) {
			unsigned short offset = chip->play_period_bytes;

			if (readw(DAQD + DAQDS_wStart) != PCTODSP_BASED(0x0))
				offset = 0;

			writew(PCTODSP_BASED(offset), DAQD + DAQDS_wStart);
		}
		++nbanks;

		/* Then advance the tail */
		/*
		if (protect)
			snd_printd(KERN_INFO "B %X %lX\n",
				   bank_num, xtime.tv_usec);
		*/

		DAPQ_tail = (++bank_num % 3) * PCTODSP_OFFSET(DAQDS__size);
		writew(DAPQ_tail, chip->DAPQ + JQS_wTail);
		/* Tell the DSP to play the bank */
		snd_msnd_send_dsp_cmd(chip, HDEX_PLAY_START);
		if (protect)
			if (2 == bank_num)
				break;
	}
	/*
	if (protect)
		snd_printd(KERN_INFO "%lX\n", xtime.tv_usec);
	*/
	/* spin_unlock_irqrestore(&chip->lock, flags); not necessary */
	return nbanks;
}
EXPORT_SYMBOL(snd_msnd_DAPQ);

static void snd_msnd_play_reset_queue(struct snd_msnd *chip,
				      unsigned int pcm_periods,
				      unsigned int pcm_count)
{
	int	n;
	void	__iomem *pDAQ = chip->mappedbase + DAPQ_DATA_BUFF;

	chip->last_playbank = -1;
	chip->playLimit = pcm_count * (pcm_periods - 1);
	chip->playPeriods = pcm_periods;
	writew(PCTODSP_OFFSET(0 * DAQDS__size), chip->DAPQ + JQS_wHead);
	writew(PCTODSP_OFFSET(0 * DAQDS__size), chip->DAPQ + JQS_wTail);

	chip->play_period_bytes = pcm_count;

	for (n = 0; n < pcm_periods; ++n, pDAQ += DAQDS__size) {
		writew(PCTODSP_BASED((u32)(pcm_count * n)),
			pDAQ + DAQDS_wStart);
		writew(0, pDAQ + DAQDS_wSize);
		writew(1, pDAQ + DAQDS_wFormat);
		writew(chip->play_sample_size, pDAQ + DAQDS_wSampleSize);
		writew(chip->play_channels, pDAQ + DAQDS_wChannels);
		writew(chip->play_sample_rate, pDAQ + DAQDS_wSampleRate);
		writew(HIMT_PLAY_DONE * 0x100 + n, pDAQ + DAQDS_wIntMsg);
		writew(n, pDAQ + DAQDS_wFlags);
	}
}

static void snd_msnd_capture_reset_queue(struct snd_msnd *chip,
					 unsigned int pcm_periods,
					 unsigned int pcm_count)
{
	int		n;
	void		__iomem *pDAQ;
	/* unsigned long	flags; */

	/* snd_msnd_init_queue(chip->DARQ, DARQ_DATA_BUFF, DARQ_BUFF_SIZE); */

	chip->last_recbank = 2;
	chip->captureLimit = pcm_count * (pcm_periods - 1);
	chip->capturePeriods = pcm_periods;
	writew(PCTODSP_OFFSET(0 * DAQDS__size), chip->DARQ + JQS_wHead);
	writew(PCTODSP_OFFSET(chip->last_recbank * DAQDS__size),
		chip->DARQ + JQS_wTail);

#if 0 /* Critical section: bank 1 access. this is how the OSS driver does it:*/
	spin_lock_irqsave(&chip->lock, flags);
	outb(HPBLKSEL_1, chip->io + HP_BLKS);
	memset_io(chip->mappedbase, 0, DAR_BUFF_SIZE * 3);
	outb(HPBLKSEL_0, chip->io + HP_BLKS);
	spin_unlock_irqrestore(&chip->lock, flags);
#endif

	chip->capturePeriodBytes = pcm_count;
	snd_printdd("snd_msnd_capture_reset_queue() %i\n", pcm_count);

	pDAQ = chip->mappedbase + DARQ_DATA_BUFF;

	for (n = 0; n < pcm_periods; ++n, pDAQ += DAQDS__size) {
		u32 tmp = pcm_count * n;

		writew(PCTODSP_BASED(tmp + 0x3000), pDAQ + DAQDS_wStart);
		writew(pcm_count, pDAQ + DAQDS_wSize);
		writew(1, pDAQ + DAQDS_wFormat);
		writew(chip->capture_sample_size, pDAQ + DAQDS_wSampleSize);
		writew(chip->capture_channels, pDAQ + DAQDS_wChannels);
		writew(chip->capture_sample_rate, pDAQ + DAQDS_wSampleRate);
		writew(HIMT_RECORD_DONE * 0x100 + n, pDAQ + DAQDS_wIntMsg);
		writew(n, pDAQ + DAQDS_wFlags);
	}
}

static const struct snd_pcm_hardware snd_msnd_playback = {
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_BATCH,
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	0x3000,
	.period_bytes_min =	0x40,
	.period_bytes_max =	0x1800,
	.periods_min =		2,
	.periods_max =		3,
	.fifo_size =		0,
};

static const struct snd_pcm_hardware snd_msnd_capture = {
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_BATCH,
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	0x3000,
	.period_bytes_min =	0x40,
	.period_bytes_max =	0x1800,
	.periods_min =		2,
	.periods_max =		3,
	.fifo_size =		0,
};


static int snd_msnd_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	set_bit(F_AUDIO_WRITE_INUSE, &chip->flags);
	clear_bit(F_WRITING, &chip->flags);
	snd_msnd_enable_irq(chip);

	runtime->dma_area = (__force void *)chip->mappedbase;
	runtime->dma_bytes = 0x3000;

	chip->playback_substream = substream;
	runtime->hw = snd_msnd_playback;
	return 0;
}

static int snd_msnd_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	snd_msnd_disable_irq(chip);
	clear_bit(F_AUDIO_WRITE_INUSE, &chip->flags);
	return 0;
}


static int snd_msnd_playback_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	int	i;
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);
	void	__iomem *pDAQ =	chip->mappedbase + DAPQ_DATA_BUFF;

	chip->play_sample_size = snd_pcm_format_width(params_format(params));
	chip->play_channels = params_channels(params);
	chip->play_sample_rate = params_rate(params);

	for (i = 0; i < 3; ++i, pDAQ += DAQDS__size) {
		writew(chip->play_sample_size, pDAQ + DAQDS_wSampleSize);
		writew(chip->play_channels, pDAQ + DAQDS_wChannels);
		writew(chip->play_sample_rate, pDAQ + DAQDS_wSampleRate);
	}
	/* dont do this here:
	 * snd_msnd_calibrate_adc(chip->play_sample_rate);
	 */

	return 0;
}

static int snd_msnd_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);
	unsigned int pcm_size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int pcm_count = snd_pcm_lib_period_bytes(substream);
	unsigned int pcm_periods = pcm_size / pcm_count;

	snd_msnd_play_reset_queue(chip, pcm_periods, pcm_count);
	chip->playDMAPos = 0;
	return 0;
}

static int snd_msnd_playback_trigger(struct snd_pcm_substream *substream,
				     int cmd)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);
	int	result = 0;

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		snd_printdd("snd_msnd_playback_trigger(START)\n");
		chip->banksPlayed = 0;
		set_bit(F_WRITING, &chip->flags);
		snd_msnd_DAPQ(chip, 1);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		snd_printdd("snd_msnd_playback_trigger(STop)\n");
		/* interrupt diagnostic, comment this out later */
		clear_bit(F_WRITING, &chip->flags);
		snd_msnd_send_dsp_cmd(chip, HDEX_PLAY_STOP);
	} else {
		snd_printd(KERN_ERR "snd_msnd_playback_trigger(?????)\n");
		result = -EINVAL;
	}

	snd_printdd("snd_msnd_playback_trigger() ENDE\n");
	return result;
}

static snd_pcm_uframes_t
snd_msnd_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	return bytes_to_frames(substream->runtime, chip->playDMAPos);
}


static const struct snd_pcm_ops snd_msnd_playback_ops = {
	.open =		snd_msnd_playback_open,
	.close =	snd_msnd_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_msnd_playback_hw_params,
	.prepare =	snd_msnd_playback_prepare,
	.trigger =	snd_msnd_playback_trigger,
	.pointer =	snd_msnd_playback_pointer,
};

static int snd_msnd_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	set_bit(F_AUDIO_READ_INUSE, &chip->flags);
	snd_msnd_enable_irq(chip);
	runtime->dma_area = (__force void *)chip->mappedbase + 0x3000;
	runtime->dma_bytes = 0x3000;
	memset(runtime->dma_area, 0, runtime->dma_bytes);
	chip->capture_substream = substream;
	runtime->hw = snd_msnd_capture;
	return 0;
}

static int snd_msnd_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	snd_msnd_disable_irq(chip);
	clear_bit(F_AUDIO_READ_INUSE, &chip->flags);
	return 0;
}

static int snd_msnd_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);
	unsigned int pcm_size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int pcm_count = snd_pcm_lib_period_bytes(substream);
	unsigned int pcm_periods = pcm_size / pcm_count;

	snd_msnd_capture_reset_queue(chip, pcm_periods, pcm_count);
	chip->captureDMAPos = 0;
	return 0;
}

static int snd_msnd_capture_trigger(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		chip->last_recbank = -1;
		set_bit(F_READING, &chip->flags);
		if (snd_msnd_send_dsp_cmd(chip, HDEX_RECORD_START) == 0)
			return 0;

		clear_bit(F_READING, &chip->flags);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		clear_bit(F_READING, &chip->flags);
		snd_msnd_send_dsp_cmd(chip, HDEX_RECORD_STOP);
		return 0;
	}
	return -EINVAL;
}


static snd_pcm_uframes_t
snd_msnd_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);

	return bytes_to_frames(runtime, chip->captureDMAPos);
}


static int snd_msnd_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	int		i;
	struct snd_msnd *chip = snd_pcm_substream_chip(substream);
	void		__iomem *pDAQ = chip->mappedbase + DARQ_DATA_BUFF;

	chip->capture_sample_size = snd_pcm_format_width(params_format(params));
	chip->capture_channels = params_channels(params);
	chip->capture_sample_rate = params_rate(params);

	for (i = 0; i < 3; ++i, pDAQ += DAQDS__size) {
		writew(chip->capture_sample_size, pDAQ + DAQDS_wSampleSize);
		writew(chip->capture_channels, pDAQ + DAQDS_wChannels);
		writew(chip->capture_sample_rate, pDAQ + DAQDS_wSampleRate);
	}
	return 0;
}


static const struct snd_pcm_ops snd_msnd_capture_ops = {
	.open =		snd_msnd_capture_open,
	.close =	snd_msnd_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_msnd_capture_hw_params,
	.prepare =	snd_msnd_capture_prepare,
	.trigger =	snd_msnd_capture_trigger,
	.pointer =	snd_msnd_capture_pointer,
};


int snd_msnd_pcm(struct snd_card *card, int device)
{
	struct snd_msnd *chip = card->private_data;
	struct snd_pcm	*pcm;
	int err;

	err = snd_pcm_new(card, "MSNDPINNACLE", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_msnd_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_msnd_capture_ops);

	pcm->private_data = chip;
	strcpy(pcm->name, "Hurricane");

	return 0;
}
EXPORT_SYMBOL(snd_msnd_pcm);

MODULE_DESCRIPTION("Common routines for Turtle Beach Multisound drivers");
MODULE_LICENSE("GPL");

