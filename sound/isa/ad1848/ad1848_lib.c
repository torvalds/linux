/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of AD1848/AD1847/CS4248
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <sound/core.h>
#include <sound/ad1848.h>
#include <sound/control.h>
#include <sound/pcm_params.h>

#include <asm/io.h>
#include <asm/dma.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Routines for control of AD1848/AD1847/CS4248");
MODULE_LICENSE("GPL");

#if 0
#define SNDRV_DEBUG_MCE
#endif

/*
 *  Some variables
 */

static unsigned char freq_bits[14] = {
	/* 5510 */	0x00 | AD1848_XTAL2,
	/* 6620 */	0x0E | AD1848_XTAL2,
	/* 8000 */	0x00 | AD1848_XTAL1,
	/* 9600 */	0x0E | AD1848_XTAL1,
	/* 11025 */	0x02 | AD1848_XTAL2,
	/* 16000 */	0x02 | AD1848_XTAL1,
	/* 18900 */	0x04 | AD1848_XTAL2,
	/* 22050 */	0x06 | AD1848_XTAL2,
	/* 27042 */	0x04 | AD1848_XTAL1,
	/* 32000 */	0x06 | AD1848_XTAL1,
	/* 33075 */	0x0C | AD1848_XTAL2,
	/* 37800 */	0x08 | AD1848_XTAL2,
	/* 44100 */	0x0A | AD1848_XTAL2,
	/* 48000 */	0x0C | AD1848_XTAL1
};

static unsigned int rates[14] = {
	5510, 6620, 8000, 9600, 11025, 16000, 18900, 22050,
	27042, 32000, 33075, 37800, 44100, 48000
};

static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	.count = 14,
	.list = rates,
	.mask = 0,
};

static unsigned char snd_ad1848_original_image[16] =
{
	0x00,			/* 00 - lic */
	0x00,			/* 01 - ric */
	0x9f,			/* 02 - la1ic */
	0x9f,			/* 03 - ra1ic */
	0x9f,			/* 04 - la2ic */
	0x9f,			/* 05 - ra2ic */
	0xbf,			/* 06 - loc */
	0xbf,			/* 07 - roc */
	0x20,			/* 08 - dfr */
	AD1848_AUTOCALIB,	/* 09 - ic */
	0x00,			/* 0a - pc */
	0x00,			/* 0b - ti */
	0x00,			/* 0c - mi */
	0x00,			/* 0d - lbc */
	0x00,			/* 0e - dru */
	0x00,			/* 0f - drl */
};

/*
 *  Basic I/O functions
 */

void snd_ad1848_out(ad1848_t *chip,
			   unsigned char reg,
			   unsigned char value)
{
	int timeout;

	for (timeout = 250; timeout > 0 && (inb(AD1848P(chip, REGSEL)) & AD1848_INIT); timeout--)
		udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (inb(AD1848P(chip, REGSEL)) & AD1848_INIT)
		snd_printk("auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	outb(chip->mce_bit | reg, AD1848P(chip, REGSEL));
	outb(chip->image[reg] = value, AD1848P(chip, REG));
	mb();
#if 0
	printk("codec out - reg 0x%x = 0x%x\n", chip->mce_bit | reg, value);
#endif
}

static void snd_ad1848_dout(ad1848_t *chip,
			    unsigned char reg, unsigned char value)
{
	int timeout;

	for (timeout = 250; timeout > 0 && (inb(AD1848P(chip, REGSEL)) & AD1848_INIT); timeout--)
		udelay(100);
	outb(chip->mce_bit | reg, AD1848P(chip, REGSEL));
	outb(value, AD1848P(chip, REG));
	mb();
}

static unsigned char snd_ad1848_in(ad1848_t *chip, unsigned char reg)
{
	int timeout;

	for (timeout = 250; timeout > 0 && (inb(AD1848P(chip, REGSEL)) & AD1848_INIT); timeout--)
		udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (inb(AD1848P(chip, REGSEL)) & AD1848_INIT)
		snd_printk("auto calibration time out - reg = 0x%x\n", reg);
#endif
	outb(chip->mce_bit | reg, AD1848P(chip, REGSEL));
	mb();
	return inb(AD1848P(chip, REG));
}

#if 0

static void snd_ad1848_debug(ad1848_t *chip)
{
	printk("AD1848 REGS:      INDEX = 0x%02x  ", inb(AD1848P(chip, REGSEL)));
	printk("                 STATUS = 0x%02x\n", inb(AD1848P(chip, STATUS)));
	printk("  0x00: left input      = 0x%02x  ", snd_ad1848_in(chip, 0x00));
	printk("  0x08: playback format = 0x%02x\n", snd_ad1848_in(chip, 0x08));
	printk("  0x01: right input     = 0x%02x  ", snd_ad1848_in(chip, 0x01));
	printk("  0x09: iface (CFIG 1)  = 0x%02x\n", snd_ad1848_in(chip, 0x09));
	printk("  0x02: AUXA left       = 0x%02x  ", snd_ad1848_in(chip, 0x02));
	printk("  0x0a: pin control     = 0x%02x\n", snd_ad1848_in(chip, 0x0a));
	printk("  0x03: AUXA right      = 0x%02x  ", snd_ad1848_in(chip, 0x03));
	printk("  0x0b: init & status   = 0x%02x\n", snd_ad1848_in(chip, 0x0b));
	printk("  0x04: AUXB left       = 0x%02x  ", snd_ad1848_in(chip, 0x04));
	printk("  0x0c: revision & mode = 0x%02x\n", snd_ad1848_in(chip, 0x0c));
	printk("  0x05: AUXB right      = 0x%02x  ", snd_ad1848_in(chip, 0x05));
	printk("  0x0d: loopback        = 0x%02x\n", snd_ad1848_in(chip, 0x0d));
	printk("  0x06: left output     = 0x%02x  ", snd_ad1848_in(chip, 0x06));
	printk("  0x0e: data upr count  = 0x%02x\n", snd_ad1848_in(chip, 0x0e));
	printk("  0x07: right output    = 0x%02x  ", snd_ad1848_in(chip, 0x07));
	printk("  0x0f: data lwr count  = 0x%02x\n", snd_ad1848_in(chip, 0x0f));
}

#endif

/*
 *  AD1848 detection / MCE routines
 */

static void snd_ad1848_mce_up(ad1848_t *chip)
{
	unsigned long flags;
	int timeout;

	for (timeout = 250; timeout > 0 && (inb(AD1848P(chip, REGSEL)) & AD1848_INIT); timeout--)
		udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (inb(AD1848P(chip, REGSEL)) & AD1848_INIT)
		snd_printk("mce_up - auto calibration time out (0)\n");
#endif
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->mce_bit |= AD1848_MCE;
	timeout = inb(AD1848P(chip, REGSEL));
	if (timeout == 0x80)
		snd_printk("mce_up [0x%lx]: serious init problem - codec still busy\n", chip->port);
	if (!(timeout & AD1848_MCE))
		outb(chip->mce_bit | (timeout & 0x1f), AD1848P(chip, REGSEL));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_ad1848_mce_down(ad1848_t *chip)
{
	unsigned long flags;
	int timeout;
	signed long time;

	spin_lock_irqsave(&chip->reg_lock, flags);
	for (timeout = 5; timeout > 0; timeout--)
		inb(AD1848P(chip, REGSEL));
	/* end of cleanup sequence */
	for (timeout = 12000; timeout > 0 && (inb(AD1848P(chip, REGSEL)) & AD1848_INIT); timeout--)
		udelay(100);
#if 0
	printk("(1) timeout = %i\n", timeout);
#endif
#ifdef CONFIG_SND_DEBUG
	if (inb(AD1848P(chip, REGSEL)) & AD1848_INIT)
		snd_printk("mce_down [0x%lx] - auto calibration time out (0)\n", AD1848P(chip, REGSEL));
#endif
	chip->mce_bit &= ~AD1848_MCE;
	timeout = inb(AD1848P(chip, REGSEL));
	outb(chip->mce_bit | (timeout & 0x1f), AD1848P(chip, REGSEL));
	if (timeout == 0x80)
		snd_printk("mce_down [0x%lx]: serious init problem - codec still busy\n", chip->port);
	if ((timeout & AD1848_MCE) == 0) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return;
	}
	/* calibration process */

	for (timeout = 500; timeout > 0 && (snd_ad1848_in(chip, AD1848_TEST_INIT) & AD1848_CALIB_IN_PROGRESS) == 0; timeout--);
	if ((snd_ad1848_in(chip, AD1848_TEST_INIT) & AD1848_CALIB_IN_PROGRESS) == 0) {
		snd_printd("mce_down - auto calibration time out (1)\n");
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return;
	}
#if 0
	printk("(2) timeout = %i, jiffies = %li\n", timeout, jiffies);
#endif
	time = HZ / 4;
	while (snd_ad1848_in(chip, AD1848_TEST_INIT) & AD1848_CALIB_IN_PROGRESS) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		if (time <= 0) {
			snd_printk("mce_down - auto calibration time out (2)\n");
			return;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		time = schedule_timeout(time);
		spin_lock_irqsave(&chip->reg_lock, flags);
	}
#if 0
	printk("(3) jiffies = %li\n", jiffies);
#endif
	time = HZ / 10;
	while (inb(AD1848P(chip, REGSEL)) & AD1848_INIT) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		if (time <= 0) {
			snd_printk("mce_down - auto calibration time out (3)\n");
			return;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		time = schedule_timeout(time);
		spin_lock_irqsave(&chip->reg_lock, flags);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#if 0
	printk("(4) jiffies = %li\n", jiffies);
	snd_printk("mce_down - exit = 0x%x\n", inb(AD1848P(chip, REGSEL)));
#endif
}

static unsigned int snd_ad1848_get_count(unsigned char format,
				         unsigned int size)
{
	switch (format & 0xe0) {
	case AD1848_LINEAR_16:
		size >>= 1;
		break;
	}
	if (format & AD1848_STEREO)
		size >>= 1;
	return size;
}

static int snd_ad1848_trigger(ad1848_t *chip, unsigned char what,
			      int channel, int cmd)
{
	int result = 0;

#if 0
	printk("codec trigger!!! - what = %i, enable = %i, status = 0x%x\n", what, enable, inb(AD1848P(card, STATUS)));
#endif
	spin_lock(&chip->reg_lock);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (chip->image[AD1848_IFACE_CTRL] & what) {
			spin_unlock(&chip->reg_lock);
			return 0;
		}
		snd_ad1848_out(chip, AD1848_IFACE_CTRL, chip->image[AD1848_IFACE_CTRL] |= what);
		chip->mode |= AD1848_MODE_RUNNING;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		if (!(chip->image[AD1848_IFACE_CTRL] & what)) {
			spin_unlock(&chip->reg_lock);
			return 0;
		}
		snd_ad1848_out(chip, AD1848_IFACE_CTRL, chip->image[AD1848_IFACE_CTRL] &= ~what);
		chip->mode &= ~AD1848_MODE_RUNNING;
	} else {
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

/*
 *  CODEC I/O
 */

static unsigned char snd_ad1848_get_rate(unsigned int rate)
{
	int i;

	for (i = 0; i < 14; i++)
		if (rate == rates[i])
			return freq_bits[i];
	snd_BUG();
	return freq_bits[13];
}

static int snd_ad1848_ioctl(snd_pcm_substream_t * substream,
			    unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static unsigned char snd_ad1848_get_format(int format, int channels)
{
	unsigned char rformat;

	rformat = AD1848_LINEAR_8;
	switch (format) {
	case SNDRV_PCM_FORMAT_A_LAW:	rformat = AD1848_ALAW_8; break;
	case SNDRV_PCM_FORMAT_MU_LAW:	rformat = AD1848_ULAW_8; break;
	case SNDRV_PCM_FORMAT_S16_LE:	rformat = AD1848_LINEAR_16; break;
	}
	if (channels > 1)
		rformat |= AD1848_STEREO;
#if 0
	snd_printk("get_format: 0x%x (mode=0x%x)\n", format, mode);
#endif
	return rformat;
}

static void snd_ad1848_calibrate_mute(ad1848_t *chip, int mute)
{
	unsigned long flags;
	
	mute = mute ? 1 : 0;
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->calibrate_mute == mute) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return;
	}
	if (!mute) {
		snd_ad1848_dout(chip, AD1848_LEFT_INPUT, chip->image[AD1848_LEFT_INPUT]);
		snd_ad1848_dout(chip, AD1848_RIGHT_INPUT, chip->image[AD1848_RIGHT_INPUT]);
	}
	snd_ad1848_dout(chip, AD1848_AUX1_LEFT_INPUT, mute ? 0x80 : chip->image[AD1848_AUX1_LEFT_INPUT]);
	snd_ad1848_dout(chip, AD1848_AUX1_RIGHT_INPUT, mute ? 0x80 : chip->image[AD1848_AUX1_RIGHT_INPUT]);
	snd_ad1848_dout(chip, AD1848_AUX2_LEFT_INPUT, mute ? 0x80 : chip->image[AD1848_AUX2_LEFT_INPUT]);
	snd_ad1848_dout(chip, AD1848_AUX2_RIGHT_INPUT, mute ? 0x80 : chip->image[AD1848_AUX2_RIGHT_INPUT]);
	snd_ad1848_dout(chip, AD1848_LEFT_OUTPUT, mute ? 0x80 : chip->image[AD1848_LEFT_OUTPUT]);
	snd_ad1848_dout(chip, AD1848_RIGHT_OUTPUT, mute ? 0x80 : chip->image[AD1848_RIGHT_OUTPUT]);
	chip->calibrate_mute = mute;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_ad1848_set_data_format(ad1848_t *chip, snd_pcm_hw_params_t *hw_params)
{
	if (hw_params == NULL) {
		chip->image[AD1848_DATA_FORMAT] = 0x20;
	} else {
		chip->image[AD1848_DATA_FORMAT] =
		    snd_ad1848_get_format(params_format(hw_params), params_channels(hw_params)) |
		    snd_ad1848_get_rate(params_rate(hw_params));
	}
	// snd_printk(">>> pmode = 0x%x, dfr = 0x%x\n", pstr->mode, chip->image[AD1848_DATA_FORMAT]);
}

static int snd_ad1848_open(ad1848_t *chip, unsigned int mode)
{
	unsigned long flags;

	down(&chip->open_mutex);
	if (chip->mode & AD1848_MODE_OPEN) {
		up(&chip->open_mutex);
		return -EAGAIN;
	}
	snd_ad1848_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("open: (1)\n");
#endif
	snd_ad1848_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->image[AD1848_IFACE_CTRL] &= ~(AD1848_PLAYBACK_ENABLE | AD1848_PLAYBACK_PIO |
			     AD1848_CAPTURE_ENABLE | AD1848_CAPTURE_PIO |
			     AD1848_CALIB_MODE);
	chip->image[AD1848_IFACE_CTRL] |= AD1848_AUTOCALIB;
	snd_ad1848_out(chip, AD1848_IFACE_CTRL, chip->image[AD1848_IFACE_CTRL]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("open: (2)\n");
#endif

	snd_ad1848_set_data_format(chip, NULL);

	snd_ad1848_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ad1848_out(chip, AD1848_DATA_FORMAT, chip->image[AD1848_DATA_FORMAT]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("open: (3)\n");
#endif

	/* ok. now enable and ack CODEC IRQ */
	spin_lock_irqsave(&chip->reg_lock, flags);
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	chip->image[AD1848_PIN_CTRL] |= AD1848_IRQ_ENABLE;
	snd_ad1848_out(chip, AD1848_PIN_CTRL, chip->image[AD1848_PIN_CTRL]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->mode = mode;
	up(&chip->open_mutex);

	return 0;
}

static void snd_ad1848_close(ad1848_t *chip)
{
	unsigned long flags;

	down(&chip->open_mutex);
	if (!chip->mode) {
		up(&chip->open_mutex);
		return;
	}
	/* disable IRQ */
	spin_lock_irqsave(&chip->reg_lock, flags);
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	chip->image[AD1848_PIN_CTRL] &= ~AD1848_IRQ_ENABLE;
	snd_ad1848_out(chip, AD1848_PIN_CTRL, chip->image[AD1848_PIN_CTRL]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/* now disable capture & playback */

	snd_ad1848_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->image[AD1848_IFACE_CTRL] &= ~(AD1848_PLAYBACK_ENABLE | AD1848_PLAYBACK_PIO |
			     AD1848_CAPTURE_ENABLE | AD1848_CAPTURE_PIO);
	snd_ad1848_out(chip, AD1848_IFACE_CTRL, chip->image[AD1848_IFACE_CTRL]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_down(chip);

	/* clear IRQ again */
	spin_lock_irqsave(&chip->reg_lock, flags);
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	outb(0, AD1848P(chip, STATUS));	/* clear IRQ */
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->mode = 0;
	up(&chip->open_mutex);
}

/*
 *  ok.. exported functions..
 */

static int snd_ad1848_playback_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	return snd_ad1848_trigger(chip, AD1848_PLAYBACK_ENABLE, SNDRV_PCM_STREAM_PLAYBACK, cmd);
}

static int snd_ad1848_capture_trigger(snd_pcm_substream_t * substream,
				      int cmd)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	return snd_ad1848_trigger(chip, AD1848_CAPTURE_ENABLE, SNDRV_PCM_STREAM_CAPTURE, cmd);
}

static int snd_ad1848_playback_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	snd_ad1848_calibrate_mute(chip, 1);
	snd_ad1848_set_data_format(chip, hw_params);
	snd_ad1848_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ad1848_out(chip, AD1848_DATA_FORMAT, chip->image[AD1848_DATA_FORMAT]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_down(chip);
	snd_ad1848_calibrate_mute(chip, 0);
	return 0;
}

static int snd_ad1848_playback_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_ad1848_playback_prepare(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma_size = size;
	chip->image[AD1848_IFACE_CTRL] &= ~(AD1848_PLAYBACK_ENABLE | AD1848_PLAYBACK_PIO);
	snd_dma_program(chip->dma, runtime->dma_addr, size, DMA_MODE_WRITE | DMA_AUTOINIT);
	count = snd_ad1848_get_count(chip->image[AD1848_DATA_FORMAT], count) - 1;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ad1848_out(chip, AD1848_DATA_LWR_CNT, (unsigned char) count);
	snd_ad1848_out(chip, AD1848_DATA_UPR_CNT, (unsigned char) (count >> 8));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ad1848_capture_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	snd_ad1848_calibrate_mute(chip, 1);
	snd_ad1848_set_data_format(chip, hw_params);
	snd_ad1848_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ad1848_out(chip, AD1848_DATA_FORMAT, chip->image[AD1848_DATA_FORMAT]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_down(chip);
	snd_ad1848_calibrate_mute(chip, 0);
	return 0;
}

static int snd_ad1848_capture_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_ad1848_capture_prepare(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma_size = size;
	chip->image[AD1848_IFACE_CTRL] &= ~(AD1848_CAPTURE_ENABLE | AD1848_CAPTURE_PIO);
	snd_dma_program(chip->dma, runtime->dma_addr, size, DMA_MODE_READ | DMA_AUTOINIT);
	count = snd_ad1848_get_count(chip->image[AD1848_DATA_FORMAT], count) - 1;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ad1848_out(chip, AD1848_DATA_LWR_CNT, (unsigned char) count);
	snd_ad1848_out(chip, AD1848_DATA_UPR_CNT, (unsigned char) (count >> 8));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static irqreturn_t snd_ad1848_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ad1848_t *chip = dev_id;

	if ((chip->mode & AD1848_MODE_PLAY) && chip->playback_substream &&
	    (chip->mode & AD1848_MODE_RUNNING))
		snd_pcm_period_elapsed(chip->playback_substream);
	if ((chip->mode & AD1848_MODE_CAPTURE) && chip->capture_substream &&
	    (chip->mode & AD1848_MODE_RUNNING))
		snd_pcm_period_elapsed(chip->capture_substream);
	outb(0, AD1848P(chip, STATUS));	/* clear global interrupt bit */
	return IRQ_HANDLED;
}

static snd_pcm_uframes_t snd_ad1848_playback_pointer(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	
	if (!(chip->image[AD1848_IFACE_CTRL] & AD1848_PLAYBACK_ENABLE))
		return 0;
	ptr = snd_dma_pointer(chip->dma, chip->dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ad1848_capture_pointer(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(chip->image[AD1848_IFACE_CTRL] & AD1848_CAPTURE_ENABLE))
		return 0;
	ptr = snd_dma_pointer(chip->dma, chip->dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

/*

 */

static void snd_ad1848_thinkpad_twiddle(ad1848_t *chip, int on) {

	int tmp;

	if (!chip->thinkpad_flag) return;

	outb(0x1c, AD1848_THINKPAD_CTL_PORT1);
	tmp = inb(AD1848_THINKPAD_CTL_PORT2);

	if (on)
		/* turn it on */
		tmp |= AD1848_THINKPAD_CS4248_ENABLE_BIT;
	else
		/* turn it off */
		tmp &= ~AD1848_THINKPAD_CS4248_ENABLE_BIT;
	
	outb(tmp, AD1848_THINKPAD_CTL_PORT2);

}

#ifdef CONFIG_PM
static int snd_ad1848_suspend(snd_card_t *card, pm_message_t state)
{
	ad1848_t *chip = card->pm_private_data;

	snd_pcm_suspend_all(chip->pcm);
	/* FIXME: save registers? */

	if (chip->thinkpad_flag)
		snd_ad1848_thinkpad_twiddle(chip, 0);

	return 0;
}

static int snd_ad1848_resume(snd_card_t *card)
{
	ad1848_t *chip = card->pm_private_data;

	if (chip->thinkpad_flag)
		snd_ad1848_thinkpad_twiddle(chip, 1);

	/* FIXME: restore registers? */

	return 0;
}
#endif /* CONFIG_PM */

static int snd_ad1848_probe(ad1848_t * chip)
{
	unsigned long flags;
	int i, id, rev, ad1847;
	unsigned char *ptr;

#if 0
	snd_ad1848_debug(chip);
#endif
	id = ad1847 = 0;
	for (i = 0; i < 1000; i++) {
		mb();
		if (inb(AD1848P(chip, REGSEL)) & AD1848_INIT)
			udelay(500);
		else {
			spin_lock_irqsave(&chip->reg_lock, flags);
			snd_ad1848_out(chip, AD1848_MISC_INFO, 0x00);
			snd_ad1848_out(chip, AD1848_LEFT_INPUT, 0xaa);
			snd_ad1848_out(chip, AD1848_RIGHT_INPUT, 0x45);
			rev = snd_ad1848_in(chip, AD1848_RIGHT_INPUT);
			if (rev == 0x65) {
				spin_unlock_irqrestore(&chip->reg_lock, flags);
				id = 1;
				ad1847 = 1;
				break;
			}
			if (snd_ad1848_in(chip, AD1848_LEFT_INPUT) == 0xaa && rev == 0x45) {
				spin_unlock_irqrestore(&chip->reg_lock, flags);
				id = 1;
				break;
			}
			spin_unlock_irqrestore(&chip->reg_lock, flags);
		}
	}
	if (id != 1)
		return -ENODEV;	/* no valid device found */
	if (chip->hardware == AD1848_HW_DETECT) {
		if (ad1847) {
			chip->hardware = AD1848_HW_AD1847;
		} else {
			chip->hardware = AD1848_HW_AD1848;
			rev = snd_ad1848_in(chip, AD1848_MISC_INFO);
			if (rev & 0x80) {
				chip->hardware = AD1848_HW_CS4248;
			} else if ((rev & 0x0f) == 0x0a) {
				snd_ad1848_out(chip, AD1848_MISC_INFO, 0x40);
				for (i = 0; i < 16; ++i) {
					if (snd_ad1848_in(chip, i) != snd_ad1848_in(chip, i + 16)) {
						chip->hardware = AD1848_HW_CMI8330;
						break;
					}
				}
				snd_ad1848_out(chip, AD1848_MISC_INFO, 0x00);
			}
		}
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	inb(AD1848P(chip, STATUS));	/* clear any pendings IRQ */
	outb(0, AD1848P(chip, STATUS));
	mb();
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->image[AD1848_MISC_INFO] = 0x00;
	chip->image[AD1848_IFACE_CTRL] =
	    (chip->image[AD1848_IFACE_CTRL] & ~AD1848_SINGLE_DMA) | AD1848_SINGLE_DMA;
	ptr = (unsigned char *) &chip->image;
	snd_ad1848_mce_down(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 16; i++)	/* ok.. fill all AD1848 registers */
		snd_ad1848_out(chip, i, *ptr++);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_ad1848_mce_up(chip);
	snd_ad1848_mce_down(chip);
	return 0;		/* all things are ok.. */
}

/*

 */

static snd_pcm_hardware_t snd_ad1848_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE),
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5510,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_ad1848_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE),
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5510,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*

 */

static int snd_ad1848_playback_open(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = snd_ad1848_open(chip, AD1848_MODE_PLAY)) < 0)
		return err;
	chip->playback_substream = substream;
	runtime->hw = snd_ad1848_playback;
	snd_pcm_limit_isa_dma_size(chip->dma, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(chip->dma, &runtime->hw.period_bytes_max);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return 0;
}

static int snd_ad1848_capture_open(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = snd_ad1848_open(chip, AD1848_MODE_CAPTURE)) < 0)
		return err;
	chip->capture_substream = substream;
	runtime->hw = snd_ad1848_capture;
	snd_pcm_limit_isa_dma_size(chip->dma, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(chip->dma, &runtime->hw.period_bytes_max);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return 0;
}

static int snd_ad1848_playback_close(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);

	chip->mode &= ~AD1848_MODE_PLAY;
	chip->playback_substream = NULL;
	snd_ad1848_close(chip);
	return 0;
}

static int snd_ad1848_capture_close(snd_pcm_substream_t * substream)
{
	ad1848_t *chip = snd_pcm_substream_chip(substream);

	chip->mode &= ~AD1848_MODE_CAPTURE;
	chip->capture_substream = NULL;
	snd_ad1848_close(chip);
	return 0;
}

static int snd_ad1848_free(ad1848_t *chip)
{
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *) chip);
	if (chip->dma >= 0) {
		snd_dma_disable(chip->dma);
		free_dma(chip->dma);
	}
	kfree(chip);
	return 0;
}

static int snd_ad1848_dev_free(snd_device_t *device)
{
	ad1848_t *chip = device->device_data;
	return snd_ad1848_free(chip);
}

static const char *snd_ad1848_chip_id(ad1848_t *chip)
{
	switch (chip->hardware) {
	case AD1848_HW_AD1847:	return "AD1847";
	case AD1848_HW_AD1848:	return "AD1848";
	case AD1848_HW_CS4248:	return "CS4248";
	case AD1848_HW_CMI8330: return "CMI8330/C3D";
	default:		return "???";
	}
}

int snd_ad1848_create(snd_card_t * card,
		      unsigned long port,
		      int irq, int dma,
		      unsigned short hardware,
		      ad1848_t ** rchip)
{
	static snd_device_ops_t ops = {
		.dev_free =	snd_ad1848_dev_free,
	};
	ad1848_t *chip;
	int err;

	*rchip = NULL;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	init_MUTEX(&chip->open_mutex);
	chip->card = card;
	chip->port = port;
	chip->irq = -1;
	chip->dma = -1;
	chip->hardware = hardware;
	memcpy(&chip->image, &snd_ad1848_original_image, sizeof(snd_ad1848_original_image));
	
	if ((chip->res_port = request_region(port, 4, "AD1848")) == NULL) {
		snd_printk(KERN_ERR "ad1848: can't grab port 0x%lx\n", port);
		snd_ad1848_free(chip);
		return -EBUSY;
	}
	if (request_irq(irq, snd_ad1848_interrupt, SA_INTERRUPT, "AD1848", (void *) chip)) {
		snd_printk(KERN_ERR "ad1848: can't grab IRQ %d\n", irq);
		snd_ad1848_free(chip);
		return -EBUSY;
	}
	chip->irq = irq;
	if (request_dma(dma, "AD1848")) {
		snd_printk(KERN_ERR "ad1848: can't grab DMA %d\n", dma);
		snd_ad1848_free(chip);
		return -EBUSY;
	}
	chip->dma = dma;

	if (hardware == AD1848_HW_THINKPAD) {
		chip->thinkpad_flag = 1;
		chip->hardware = AD1848_HW_DETECT; /* reset */
		snd_ad1848_thinkpad_twiddle(chip, 1);
		snd_card_set_isa_pm_callback(card, snd_ad1848_suspend, snd_ad1848_resume, chip);
	}

	if (snd_ad1848_probe(chip) < 0) {
		snd_ad1848_free(chip);
		return -ENODEV;
	}

	/* Register device */
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_ad1848_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static snd_pcm_ops_t snd_ad1848_playback_ops = {
	.open =		snd_ad1848_playback_open,
	.close =	snd_ad1848_playback_close,
	.ioctl =	snd_ad1848_ioctl,
	.hw_params =	snd_ad1848_playback_hw_params,
	.hw_free =	snd_ad1848_playback_hw_free,
	.prepare =	snd_ad1848_playback_prepare,
	.trigger =	snd_ad1848_playback_trigger,
	.pointer =	snd_ad1848_playback_pointer,
};

static snd_pcm_ops_t snd_ad1848_capture_ops = {
	.open =		snd_ad1848_capture_open,
	.close =	snd_ad1848_capture_close,
	.ioctl =	snd_ad1848_ioctl,
	.hw_params =	snd_ad1848_capture_hw_params,
	.hw_free =	snd_ad1848_capture_hw_free,
	.prepare =	snd_ad1848_capture_prepare,
	.trigger =	snd_ad1848_capture_trigger,
	.pointer =	snd_ad1848_capture_pointer,
};

static void snd_ad1848_pcm_free(snd_pcm_t *pcm)
{
	ad1848_t *chip = pcm->private_data;
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_ad1848_pcm(ad1848_t *chip, int device, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "AD1848", device, 1, 1, &pcm)) < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ad1848_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ad1848_capture_ops);

	pcm->private_free = snd_ad1848_pcm_free;
	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	strcpy(pcm->name, snd_ad1848_chip_id(chip));

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, chip->dma > 3 ? 128*1024 : 64*1024);

	chip->pcm = pcm;
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

const snd_pcm_ops_t *snd_ad1848_get_pcm_ops(int direction)
{
	return direction == SNDRV_PCM_STREAM_PLAYBACK ?
		&snd_ad1848_playback_ops : &snd_ad1848_capture_ops;
}

/*
 *  MIXER part
 */

static int snd_ad1848_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = {
		"Line", "Aux", "Mic", "Mix"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ad1848_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.enumerated.item[0] = (chip->image[AD1848_LEFT_INPUT] & AD1848_MIXS_ALL) >> 6;
	ucontrol->value.enumerated.item[1] = (chip->image[AD1848_RIGHT_INPUT] & AD1848_MIXS_ALL) >> 6;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ad1848_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned short left, right;
	int change;
	
	if (ucontrol->value.enumerated.item[0] > 3 ||
	    ucontrol->value.enumerated.item[1] > 3)
		return -EINVAL;
	left = ucontrol->value.enumerated.item[0] << 6;
	right = ucontrol->value.enumerated.item[1] << 6;
	spin_lock_irqsave(&chip->reg_lock, flags);
	left = (chip->image[AD1848_LEFT_INPUT] & ~AD1848_MIXS_ALL) | left;
	right = (chip->image[AD1848_RIGHT_INPUT] & ~AD1848_MIXS_ALL) | right;
	change = left != chip->image[AD1848_LEFT_INPUT] ||
	         right != chip->image[AD1848_RIGHT_INPUT];
	snd_ad1848_out(chip, AD1848_LEFT_INPUT, left);
	snd_ad1848_out(chip, AD1848_RIGHT_INPUT, right);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static int snd_ad1848_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ad1848_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (chip->image[reg] >> shift) & mask;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_ad1848_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned short val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;
	spin_lock_irqsave(&chip->reg_lock, flags);
	val = (chip->image[reg] & ~(mask << shift)) | val;
	change = val != chip->image[reg];
	snd_ad1848_out(chip, reg, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static int snd_ad1848_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ad1848_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (chip->image[left_reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (chip->image[right_reg] >> shift_right) & mask;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_ad1848_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ad1848_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned short val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (left_reg != right_reg) {
		val1 = (chip->image[left_reg] & ~(mask << shift_left)) | val1;
		val2 = (chip->image[right_reg] & ~(mask << shift_right)) | val2;
		change = val1 != chip->image[left_reg] || val2 != chip->image[right_reg];
		snd_ad1848_out(chip, left_reg, val1);
		snd_ad1848_out(chip, right_reg, val2);
	} else {
		val1 = (chip->image[left_reg] & ~((mask << shift_left) | (mask << shift_right))) | val1 | val2;
		change = val1 != chip->image[left_reg];
		snd_ad1848_out(chip, left_reg, val1);		
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

/*
 */
int snd_ad1848_add_ctl(ad1848_t *chip, const char *name, int index, int type, unsigned long value)
{
	static snd_kcontrol_new_t newctls[] = {
		[AD1848_MIX_SINGLE] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_ad1848_info_single,
			.get = snd_ad1848_get_single,
			.put = snd_ad1848_put_single,
		},
		[AD1848_MIX_DOUBLE] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_ad1848_info_double,
			.get = snd_ad1848_get_double,
			.put = snd_ad1848_put_double,
		},
		[AD1848_MIX_CAPTURE] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_ad1848_info_mux,
			.get = snd_ad1848_get_mux,
			.put = snd_ad1848_put_mux,
		},
	};
	snd_kcontrol_t *ctl;
	int err;

	ctl = snd_ctl_new1(&newctls[type], chip);
	if (! ctl)
		return -ENOMEM;
	strlcpy(ctl->id.name, name, sizeof(ctl->id.name));
	ctl->id.index = index;
	ctl->private_value = value;
	if ((err = snd_ctl_add(chip->card, ctl)) < 0) {
		snd_ctl_free_one(ctl);
		return err;
	}
	return 0;
}


static struct ad1848_mix_elem snd_ad1848_controls[] = {
AD1848_DOUBLE("PCM Playback Switch", 0, AD1848_LEFT_OUTPUT, AD1848_RIGHT_OUTPUT, 7, 7, 1, 1),
AD1848_DOUBLE("PCM Playback Volume", 0, AD1848_LEFT_OUTPUT, AD1848_RIGHT_OUTPUT, 0, 0, 63, 1),
AD1848_DOUBLE("Aux Playback Switch", 0, AD1848_AUX1_LEFT_INPUT, AD1848_AUX1_RIGHT_INPUT, 7, 7, 1, 1),
AD1848_DOUBLE("Aux Playback Volume", 0, AD1848_AUX1_LEFT_INPUT, AD1848_AUX1_RIGHT_INPUT, 0, 0, 31, 1),
AD1848_DOUBLE("Aux Playback Switch", 1, AD1848_AUX2_LEFT_INPUT, AD1848_AUX2_RIGHT_INPUT, 7, 7, 1, 1),
AD1848_DOUBLE("Aux Playback Volume", 1, AD1848_AUX2_LEFT_INPUT, AD1848_AUX2_RIGHT_INPUT, 0, 0, 31, 1),
AD1848_DOUBLE("Capture Volume", 0, AD1848_LEFT_INPUT, AD1848_RIGHT_INPUT, 0, 0, 15, 0),
{
	.name = "Capture Source",
	.type = AD1848_MIX_CAPTURE,
},
AD1848_SINGLE("Loopback Capture Switch", 0, AD1848_LOOPBACK, 0, 1, 0),
AD1848_SINGLE("Loopback Capture Volume", 0, AD1848_LOOPBACK, 1, 63, 0)
};
                                        
int snd_ad1848_mixer(ad1848_t *chip)
{
	snd_card_t *card;
	snd_pcm_t *pcm;
	unsigned int idx;
	int err;

	snd_assert(chip != NULL && chip->pcm != NULL, return -EINVAL);

	pcm = chip->pcm;
	card = chip->card;

	strcpy(card->mixername, pcm->name);

	for (idx = 0; idx < ARRAY_SIZE(snd_ad1848_controls); idx++)
		if ((err = snd_ad1848_add_ctl_elem(chip, &snd_ad1848_controls[idx])) < 0)
			return err;

	return 0;
}

EXPORT_SYMBOL(snd_ad1848_out);
EXPORT_SYMBOL(snd_ad1848_create);
EXPORT_SYMBOL(snd_ad1848_pcm);
EXPORT_SYMBOL(snd_ad1848_get_pcm_ops);
EXPORT_SYMBOL(snd_ad1848_mixer);
EXPORT_SYMBOL(snd_ad1848_add_ctl);

/*
 *  INIT part
 */

static int __init alsa_ad1848_init(void)
{
	return 0;
}

static void __exit alsa_ad1848_exit(void)
{
}

module_init(alsa_ad1848_init)
module_exit(alsa_ad1848_exit)
