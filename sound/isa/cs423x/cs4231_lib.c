/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of CS4231(A)/CS4232/InterWave & compatible chips
 *
 *  Bugs:
 *     - sometimes record brokes playback with WSS portion of 
 *       Yamaha OPL3-SA3 chip
 *     - CS4231 (GUS MAX) - still trouble with occasional noises
 *                        - broken initialization?
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

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <sound/core.h>
#include <sound/cs4231.h>
#include <sound/pcm_params.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Routines for control of CS4231(A)/CS4232/InterWave & compatible chips");
MODULE_LICENSE("GPL");

#if 0
#define SNDRV_DEBUG_MCE
#endif

/*
 *  Some variables
 */

static unsigned char freq_bits[14] = {
	/* 5510 */	0x00 | CS4231_XTAL2,
	/* 6620 */	0x0E | CS4231_XTAL2,
	/* 8000 */	0x00 | CS4231_XTAL1,
	/* 9600 */	0x0E | CS4231_XTAL1,
	/* 11025 */	0x02 | CS4231_XTAL2,
	/* 16000 */	0x02 | CS4231_XTAL1,
	/* 18900 */	0x04 | CS4231_XTAL2,
	/* 22050 */	0x06 | CS4231_XTAL2,
	/* 27042 */	0x04 | CS4231_XTAL1,
	/* 32000 */	0x06 | CS4231_XTAL1,
	/* 33075 */	0x0C | CS4231_XTAL2,
	/* 37800 */	0x08 | CS4231_XTAL2,
	/* 44100 */	0x0A | CS4231_XTAL2,
	/* 48000 */	0x0C | CS4231_XTAL1
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

static int snd_cs4231_xrate(snd_pcm_runtime_t *runtime)
{
	return snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
}

static unsigned char snd_cs4231_original_image[32] =
{
	0x00,			/* 00/00 - lic */
	0x00,			/* 01/01 - ric */
	0x9f,			/* 02/02 - la1ic */
	0x9f,			/* 03/03 - ra1ic */
	0x9f,			/* 04/04 - la2ic */
	0x9f,			/* 05/05 - ra2ic */
	0xbf,			/* 06/06 - loc */
	0xbf,			/* 07/07 - roc */
	0x20,			/* 08/08 - pdfr */
	CS4231_AUTOCALIB,	/* 09/09 - ic */
	0x00,			/* 0a/10 - pc */
	0x00,			/* 0b/11 - ti */
	CS4231_MODE2,		/* 0c/12 - mi */
	0xfc,			/* 0d/13 - lbc */
	0x00,			/* 0e/14 - pbru */
	0x00,			/* 0f/15 - pbrl */
	0x80,			/* 10/16 - afei */
	0x01,			/* 11/17 - afeii */
	0x9f,			/* 12/18 - llic */
	0x9f,			/* 13/19 - rlic */
	0x00,			/* 14/20 - tlb */
	0x00,			/* 15/21 - thb */
	0x00,			/* 16/22 - la3mic/reserved */
	0x00,			/* 17/23 - ra3mic/reserved */
	0x00,			/* 18/24 - afs */
	0x00,			/* 19/25 - lamoc/version */
	0xcf,			/* 1a/26 - mioc */
	0x00,			/* 1b/27 - ramoc/reserved */
	0x20,			/* 1c/28 - cdfr */
	0x00,			/* 1d/29 - res4 */
	0x00,			/* 1e/30 - cbru */
	0x00,			/* 1f/31 - cbrl */
};

/*
 *  Basic I/O functions
 */

#if !defined(EBUS_SUPPORT) && !defined(SBUS_SUPPORT)
#define __CS4231_INLINE__ inline
#else
#define __CS4231_INLINE__ /* nothing */
#endif

static __CS4231_INLINE__ void cs4231_outb(cs4231_t *chip, u8 offset, u8 val)
{
#ifdef EBUS_SUPPORT
	if (chip->ebus->flag) {
		writeb(val, chip->port + (offset << 2));
	} else {
#endif
#ifdef SBUS_SUPPORT
		sbus_writeb(val, chip->port + (offset << 2));
#endif
#ifdef EBUS_SUPPORT
	}
#endif
#ifdef LEGACY_SUPPORT
	outb(val, chip->port + offset);
#endif
}

static __CS4231_INLINE__ u8 cs4231_inb(cs4231_t *chip, u8 offset)
{
#ifdef EBUS_SUPPORT
	if (chip->ebus_flag) {
		return readb(chip->port + (offset << 2));
	} else {
#endif
#ifdef SBUS_SUPPORT
		return sbus_readb(chip->port + (offset << 2));
#endif
#ifdef EBUS_SUPPORT
	}
#endif
#ifdef LEGACY_SUPPORT
	return inb(chip->port + offset);
#endif
}

static void snd_cs4231_outm(cs4231_t *chip, unsigned char reg,
			    unsigned char mask, unsigned char value)
{
	int timeout;
	unsigned char tmp;

	for (timeout = 250;
	     timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
		snd_printk("outm: auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	if (chip->calibrate_mute) {
		chip->image[reg] &= mask;
		chip->image[reg] |= value;
	} else {
		cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | reg);
		mb();
		tmp = (chip->image[reg] & mask) | value;
		cs4231_outb(chip, CS4231P(REG), tmp);
		chip->image[reg] = tmp;
		mb();
	}
}

static void snd_cs4231_dout(cs4231_t *chip, unsigned char reg, unsigned char value)
{
	int timeout;

	for (timeout = 250;
	     timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(10);
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | reg);
	cs4231_outb(chip, CS4231P(REG), value);
	mb();
}

void snd_cs4231_out(cs4231_t *chip, unsigned char reg, unsigned char value)
{
	int timeout;

	for (timeout = 250;
	     timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
		snd_printk("out: auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | reg);
	cs4231_outb(chip, CS4231P(REG), value);
	chip->image[reg] = value;
	mb();
#if 0
	printk("codec out - reg 0x%x = 0x%x\n", chip->mce_bit | reg, value);
#endif
}

unsigned char snd_cs4231_in(cs4231_t *chip, unsigned char reg)
{
	int timeout;

	for (timeout = 250;
	     timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
		snd_printk("in: auto calibration time out - reg = 0x%x\n", reg);
#endif
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | reg);
	mb();
	return cs4231_inb(chip, CS4231P(REG));
}

void snd_cs4236_ext_out(cs4231_t *chip, unsigned char reg, unsigned char val)
{
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | 0x17);
	cs4231_outb(chip, CS4231P(REG), reg | (chip->image[CS4236_EXT_REG] & 0x01));
	cs4231_outb(chip, CS4231P(REG), val);
	chip->eimage[CS4236_REG(reg)] = val;
#if 0
	printk("ext out : reg = 0x%x, val = 0x%x\n", reg, val);
#endif
}

unsigned char snd_cs4236_ext_in(cs4231_t *chip, unsigned char reg)
{
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | 0x17);
	cs4231_outb(chip, CS4231P(REG), reg | (chip->image[CS4236_EXT_REG] & 0x01));
#if 1
	return cs4231_inb(chip, CS4231P(REG));
#else
	{
		unsigned char res;
		res = cs4231_inb(chip, CS4231P(REG));
		printk("ext in : reg = 0x%x, val = 0x%x\n", reg, res);
		return res;
	}
#endif
}

#if 0

static void snd_cs4231_debug(cs4231_t *chip)
{
	printk("CS4231 REGS:      INDEX = 0x%02x  ", cs4231_inb(chip, CS4231P(REGSEL)));
	printk("                 STATUS = 0x%02x\n", cs4231_inb(chip, CS4231P(STATUS)));
	printk("  0x00: left input      = 0x%02x  ", snd_cs4231_in(chip, 0x00));
	printk("  0x10: alt 1 (CFIG 2)  = 0x%02x\n", snd_cs4231_in(chip, 0x10));
	printk("  0x01: right input     = 0x%02x  ", snd_cs4231_in(chip, 0x01));
	printk("  0x11: alt 2 (CFIG 3)  = 0x%02x\n", snd_cs4231_in(chip, 0x11));
	printk("  0x02: GF1 left input  = 0x%02x  ", snd_cs4231_in(chip, 0x02));
	printk("  0x12: left line in    = 0x%02x\n", snd_cs4231_in(chip, 0x12));
	printk("  0x03: GF1 right input = 0x%02x  ", snd_cs4231_in(chip, 0x03));
	printk("  0x13: right line in   = 0x%02x\n", snd_cs4231_in(chip, 0x13));
	printk("  0x04: CD left input   = 0x%02x  ", snd_cs4231_in(chip, 0x04));
	printk("  0x14: timer low       = 0x%02x\n", snd_cs4231_in(chip, 0x14));
	printk("  0x05: CD right input  = 0x%02x  ", snd_cs4231_in(chip, 0x05));
	printk("  0x15: timer high      = 0x%02x\n", snd_cs4231_in(chip, 0x15));
	printk("  0x06: left output     = 0x%02x  ", snd_cs4231_in(chip, 0x06));
	printk("  0x16: left MIC (PnP)  = 0x%02x\n", snd_cs4231_in(chip, 0x16));
	printk("  0x07: right output    = 0x%02x  ", snd_cs4231_in(chip, 0x07));
	printk("  0x17: right MIC (PnP) = 0x%02x\n", snd_cs4231_in(chip, 0x17));
	printk("  0x08: playback format = 0x%02x  ", snd_cs4231_in(chip, 0x08));
	printk("  0x18: IRQ status      = 0x%02x\n", snd_cs4231_in(chip, 0x18));
	printk("  0x09: iface (CFIG 1)  = 0x%02x  ", snd_cs4231_in(chip, 0x09));
	printk("  0x19: left line out   = 0x%02x\n", snd_cs4231_in(chip, 0x19));
	printk("  0x0a: pin control     = 0x%02x  ", snd_cs4231_in(chip, 0x0a));
	printk("  0x1a: mono control    = 0x%02x\n", snd_cs4231_in(chip, 0x1a));
	printk("  0x0b: init & status   = 0x%02x  ", snd_cs4231_in(chip, 0x0b));
	printk("  0x1b: right line out  = 0x%02x\n", snd_cs4231_in(chip, 0x1b));
	printk("  0x0c: revision & mode = 0x%02x  ", snd_cs4231_in(chip, 0x0c));
	printk("  0x1c: record format   = 0x%02x\n", snd_cs4231_in(chip, 0x1c));
	printk("  0x0d: loopback        = 0x%02x  ", snd_cs4231_in(chip, 0x0d));
	printk("  0x1d: var freq (PnP)  = 0x%02x\n", snd_cs4231_in(chip, 0x1d));
	printk("  0x0e: ply upr count   = 0x%02x  ", snd_cs4231_in(chip, 0x0e));
	printk("  0x1e: ply lwr count   = 0x%02x\n", snd_cs4231_in(chip, 0x1e));
	printk("  0x0f: rec upr count   = 0x%02x  ", snd_cs4231_in(chip, 0x0f));
	printk("  0x1f: rec lwr count   = 0x%02x\n", snd_cs4231_in(chip, 0x1f));
}

#endif

/*
 *  CS4231 detection / MCE routines
 */

static void snd_cs4231_busy_wait(cs4231_t *chip)
{
	int timeout;

	/* huh.. looks like this sequence is proper for CS4231A chip (GUS MAX) */
	for (timeout = 5; timeout > 0; timeout--)
		cs4231_inb(chip, CS4231P(REGSEL));
	/* end of cleanup sequence */
	for (timeout = 250;
	     timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(10);
}

void snd_cs4231_mce_up(cs4231_t *chip)
{
	unsigned long flags;
	int timeout;

	for (timeout = 250; timeout > 0 && (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT); timeout--)
		udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
		snd_printk("mce_up - auto calibration time out (0)\n");
#endif
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->mce_bit |= CS4231_MCE;
	timeout = cs4231_inb(chip, CS4231P(REGSEL));
	if (timeout == 0x80)
		snd_printk("mce_up [0x%lx]: serious init problem - codec still busy\n", chip->port);
	if (!(timeout & CS4231_MCE))
		cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | (timeout & 0x1f));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

void snd_cs4231_mce_down(cs4231_t *chip)
{
	unsigned long flags;
	int timeout;

	snd_cs4231_busy_wait(chip);
#if 0
	printk("(1) timeout = %i\n", timeout);
#endif
#ifdef CONFIG_SND_DEBUG
	if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
		snd_printk("mce_down [0x%lx] - auto calibration time out (0)\n", (long)CS4231P(REGSEL));
#endif
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->mce_bit &= ~CS4231_MCE;
	timeout = cs4231_inb(chip, CS4231P(REGSEL));
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | (timeout & 0x1f));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (timeout == 0x80)
		snd_printk("mce_down [0x%lx]: serious init problem - codec still busy\n", chip->port);
	if ((timeout & CS4231_MCE) == 0 ||
	    !(chip->hardware & (CS4231_HW_CS4231_MASK | CS4231_HW_CS4232_MASK))) {
		return;
	}
	snd_cs4231_busy_wait(chip);

	/* calibration process */

	for (timeout = 500; timeout > 0 && (snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) == 0; timeout--)
		udelay(10);
	if ((snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) == 0) {
		snd_printd("cs4231_mce_down - auto calibration time out (1)\n");
		return;
	}
#if 0
	printk("(2) timeout = %i, jiffies = %li\n", timeout, jiffies);
#endif
	/* in 10 ms increments, check condition, up to 250 ms */
	timeout = 25;
	while (snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) {
		if (--timeout < 0) {
			snd_printk("mce_down - auto calibration time out (2)\n");
			return;
		}
		msleep(10);
	}
#if 0
	printk("(3) jiffies = %li\n", jiffies);
#endif
	/* in 10 ms increments, check condition, up to 100 ms */
	timeout = 10;
	while (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT) {
		if (--timeout < 0) {
			snd_printk(KERN_ERR "mce_down - auto calibration time out (3)\n");
			return;
		}
		msleep(10);
	}
#if 0
	printk("(4) jiffies = %li\n", jiffies);
	snd_printk("mce_down - exit = 0x%x\n", cs4231_inb(chip, CS4231P(REGSEL)));
#endif
}

static unsigned int snd_cs4231_get_count(unsigned char format, unsigned int size)
{
	switch (format & 0xe0) {
	case CS4231_LINEAR_16:
	case CS4231_LINEAR_16_BIG:
		size >>= 1;
		break;
	case CS4231_ADPCM_16:
		return size >> 2;
	}
	if (format & CS4231_STEREO)
		size >>= 1;
	return size;
}

static int snd_cs4231_trigger(snd_pcm_substream_t *substream,
			      int cmd)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	int result = 0;
	unsigned int what;
	struct list_head *pos;
	snd_pcm_substream_t *s;
	int do_start;

#if 0
	printk("codec trigger!!! - what = %i, enable = %i, status = 0x%x\n", what, enable, cs4231_inb(chip, CS4231P(STATUS)));
#endif

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		do_start = 1; break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		do_start = 0; break;
	default:
		return -EINVAL;
	}

	what = 0;
	snd_pcm_group_for_each(pos, substream) {
		s = snd_pcm_group_substream_entry(pos);
		if (s == chip->playback_substream) {
			what |= CS4231_PLAYBACK_ENABLE;
			snd_pcm_trigger_done(s, substream);
		} else if (s == chip->capture_substream) {
			what |= CS4231_RECORD_ENABLE;
			snd_pcm_trigger_done(s, substream);
		}
	}
	spin_lock(&chip->reg_lock);
	if (do_start) {
		chip->image[CS4231_IFACE_CTRL] |= what;
		if (chip->trigger)
			chip->trigger(chip, what, 1);
	} else {
		chip->image[CS4231_IFACE_CTRL] &= ~what;
		if (chip->trigger)
			chip->trigger(chip, what, 0);
	}
	snd_cs4231_out(chip, CS4231_IFACE_CTRL, chip->image[CS4231_IFACE_CTRL]);
	spin_unlock(&chip->reg_lock);
#if 0
	snd_cs4231_debug(chip);
#endif
	return result;
}

/*
 *  CODEC I/O
 */

static unsigned char snd_cs4231_get_rate(unsigned int rate)
{
	int i;

	for (i = 0; i < 14; i++)
		if (rate == rates[i])
			return freq_bits[i];
	// snd_BUG();
	return freq_bits[13];
}

static unsigned char snd_cs4231_get_format(cs4231_t *chip,
				           int format,
                                           int channels)
{
	unsigned char rformat;

	rformat = CS4231_LINEAR_8;
	switch (format) {
	case SNDRV_PCM_FORMAT_MU_LAW:	rformat = CS4231_ULAW_8; break;
	case SNDRV_PCM_FORMAT_A_LAW:	rformat = CS4231_ALAW_8; break;
	case SNDRV_PCM_FORMAT_S16_LE:	rformat = CS4231_LINEAR_16; break;
	case SNDRV_PCM_FORMAT_S16_BE:	rformat = CS4231_LINEAR_16_BIG; break;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:	rformat = CS4231_ADPCM_16; break;
	}
	if (channels > 1)
		rformat |= CS4231_STEREO;
#if 0
	snd_printk("get_format: 0x%x (mode=0x%x)\n", format, mode);
#endif
	return rformat;
}

static void snd_cs4231_calibrate_mute(cs4231_t *chip, int mute)
{
	unsigned long flags;

	mute = mute ? 1 : 0;
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->calibrate_mute == mute) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return;
	}
	if (!mute) {
		snd_cs4231_dout(chip, CS4231_LEFT_INPUT, chip->image[CS4231_LEFT_INPUT]);
		snd_cs4231_dout(chip, CS4231_RIGHT_INPUT, chip->image[CS4231_RIGHT_INPUT]);
		snd_cs4231_dout(chip, CS4231_LOOPBACK, chip->image[CS4231_LOOPBACK]);
	}
	snd_cs4231_dout(chip, CS4231_AUX1_LEFT_INPUT, mute ? 0x80 : chip->image[CS4231_AUX1_LEFT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX1_RIGHT_INPUT, mute ? 0x80 : chip->image[CS4231_AUX1_RIGHT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX2_LEFT_INPUT, mute ? 0x80 : chip->image[CS4231_AUX2_LEFT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX2_RIGHT_INPUT, mute ? 0x80 : chip->image[CS4231_AUX2_RIGHT_INPUT]);
	snd_cs4231_dout(chip, CS4231_LEFT_OUTPUT, mute ? 0x80 : chip->image[CS4231_LEFT_OUTPUT]);
	snd_cs4231_dout(chip, CS4231_RIGHT_OUTPUT, mute ? 0x80 : chip->image[CS4231_RIGHT_OUTPUT]);
	snd_cs4231_dout(chip, CS4231_LEFT_LINE_IN, mute ? 0x80 : chip->image[CS4231_LEFT_LINE_IN]);
	snd_cs4231_dout(chip, CS4231_RIGHT_LINE_IN, mute ? 0x80 : chip->image[CS4231_RIGHT_LINE_IN]);
	snd_cs4231_dout(chip, CS4231_MONO_CTRL, mute ? 0xc0 : chip->image[CS4231_MONO_CTRL]);
	if (chip->hardware == CS4231_HW_INTERWAVE) {
		snd_cs4231_dout(chip, CS4231_LEFT_MIC_INPUT, mute ? 0x80 : chip->image[CS4231_LEFT_MIC_INPUT]);
		snd_cs4231_dout(chip, CS4231_RIGHT_MIC_INPUT, mute ? 0x80 : chip->image[CS4231_RIGHT_MIC_INPUT]);		
		snd_cs4231_dout(chip, CS4231_LINE_LEFT_OUTPUT, mute ? 0x80 : chip->image[CS4231_LINE_LEFT_OUTPUT]);
		snd_cs4231_dout(chip, CS4231_LINE_RIGHT_OUTPUT, mute ? 0x80 : chip->image[CS4231_LINE_RIGHT_OUTPUT]);
	}
	chip->calibrate_mute = mute;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs4231_playback_format(cs4231_t *chip,
				       snd_pcm_hw_params_t *params,
				       unsigned char pdfr)
{
	unsigned long flags;
	int full_calib = 1;

	down(&chip->mce_mutex);
	snd_cs4231_calibrate_mute(chip, 1);
	if (chip->hardware == CS4231_HW_CS4231A ||
	    (chip->hardware & CS4231_HW_CS4232_MASK)) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		if ((chip->image[CS4231_PLAYBK_FORMAT] & 0x0f) == (pdfr & 0x0f)) {	/* rate is same? */
			snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] | 0x10);
			snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT, chip->image[CS4231_PLAYBK_FORMAT] = pdfr);
			snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] &= ~0x10);
			udelay(100); /* Fixes audible clicks at least on GUS MAX */
			full_calib = 0;
		}
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	if (full_calib) {
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->reg_lock, flags);
		if (chip->hardware != CS4231_HW_INTERWAVE && !chip->single_dma) {
			snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT,
					(chip->image[CS4231_IFACE_CTRL] & CS4231_RECORD_ENABLE) ?
					(pdfr & 0xf0) | (chip->image[CS4231_REC_FORMAT] & 0x0f) :
				        pdfr);
		} else {
			snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT, chip->image[CS4231_PLAYBK_FORMAT] = pdfr);
		}
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		snd_cs4231_mce_down(chip);
	}
	snd_cs4231_calibrate_mute(chip, 0);
	up(&chip->mce_mutex);
}

static void snd_cs4231_capture_format(cs4231_t *chip,
				      snd_pcm_hw_params_t *params,
                                      unsigned char cdfr)
{
	unsigned long flags;
	int full_calib = 1;

	down(&chip->mce_mutex);
	snd_cs4231_calibrate_mute(chip, 1);
	if (chip->hardware == CS4231_HW_CS4231A ||
	    (chip->hardware & CS4231_HW_CS4232_MASK)) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		if ((chip->image[CS4231_PLAYBK_FORMAT] & 0x0f) == (cdfr & 0x0f) ||	/* rate is same? */
		    (chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE)) {
			snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] | 0x20);
			snd_cs4231_out(chip, CS4231_REC_FORMAT, chip->image[CS4231_REC_FORMAT] = cdfr);
			snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] &= ~0x20);
			full_calib = 0;
		}
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	if (full_calib) {
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->reg_lock, flags);
		if (chip->hardware != CS4231_HW_INTERWAVE) {
			if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE)) {
				snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT,
					       ((chip->single_dma ? cdfr : chip->image[CS4231_PLAYBK_FORMAT]) & 0xf0) |
					       (cdfr & 0x0f));
				spin_unlock_irqrestore(&chip->reg_lock, flags);
				snd_cs4231_mce_down(chip);
				snd_cs4231_mce_up(chip);
				spin_lock_irqsave(&chip->reg_lock, flags);
			}
		}
		snd_cs4231_out(chip, CS4231_REC_FORMAT, cdfr);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		snd_cs4231_mce_down(chip);
	}
	snd_cs4231_calibrate_mute(chip, 0);
	up(&chip->mce_mutex);
}

/*
 *  Timer interface
 */

static unsigned long snd_cs4231_timer_resolution(snd_timer_t * timer)
{
	cs4231_t *chip = snd_timer_chip(timer);
	if (chip->hardware & CS4231_HW_CS4236B_MASK)
		return 14467;
	else
		return chip->image[CS4231_PLAYBK_FORMAT] & 1 ? 9969 : 9920;
}

static int snd_cs4231_timer_start(snd_timer_t * timer)
{
	unsigned long flags;
	unsigned int ticks;
	cs4231_t *chip = snd_timer_chip(timer);
	spin_lock_irqsave(&chip->reg_lock, flags);
	ticks = timer->sticks;
	if ((chip->image[CS4231_ALT_FEATURE_1] & CS4231_TIMER_ENABLE) == 0 ||
	    (unsigned char)(ticks >> 8) != chip->image[CS4231_TIMER_HIGH] ||
	    (unsigned char)ticks != chip->image[CS4231_TIMER_LOW]) {
		snd_cs4231_out(chip, CS4231_TIMER_HIGH, chip->image[CS4231_TIMER_HIGH] = (unsigned char) (ticks >> 8));
		snd_cs4231_out(chip, CS4231_TIMER_LOW, chip->image[CS4231_TIMER_LOW] = (unsigned char) ticks);
		snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] | CS4231_TIMER_ENABLE);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4231_timer_stop(snd_timer_t * timer)
{
	unsigned long flags;
	cs4231_t *chip = snd_timer_chip(timer);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1] &= ~CS4231_TIMER_ENABLE);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static void snd_cs4231_init(cs4231_t *chip)
{
	unsigned long flags;

	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("init: (1)\n");
#endif
	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
			     CS4231_RECORD_ENABLE | CS4231_RECORD_PIO |
			     CS4231_CALIB_MODE);
	chip->image[CS4231_IFACE_CTRL] |= CS4231_AUTOCALIB;
	snd_cs4231_out(chip, CS4231_IFACE_CTRL, chip->image[CS4231_IFACE_CTRL]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("init: (2)\n");
#endif

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("init: (3) - afei = 0x%x\n", chip->image[CS4231_ALT_FEATURE_1]);
#endif

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_2, chip->image[CS4231_ALT_FEATURE_2]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT, chip->image[CS4231_PLAYBK_FORMAT]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("init: (4)\n");
#endif

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_REC_FORMAT, chip->image[CS4231_REC_FORMAT]);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printk("init: (5)\n");
#endif
}

static int snd_cs4231_open(cs4231_t *chip, unsigned int mode)
{
	unsigned long flags;

	down(&chip->open_mutex);
	if ((chip->mode & mode) ||
	    ((chip->mode & CS4231_MODE_OPEN) && chip->single_dma)) {
		up(&chip->open_mutex);
		return -EAGAIN;
	}
	if (chip->mode & CS4231_MODE_OPEN) {
		chip->mode |= mode;
		up(&chip->open_mutex);
		return 0;
	}
	/* ok. now enable and ack CODEC IRQ */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, CS4231_PLAYBACK_IRQ |
		       CS4231_RECORD_IRQ |
		       CS4231_TIMER_IRQ);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	chip->image[CS4231_PIN_CTRL] |= CS4231_IRQ_ENABLE;
	snd_cs4231_out(chip, CS4231_PIN_CTRL, chip->image[CS4231_PIN_CTRL]);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, CS4231_PLAYBACK_IRQ |
		       CS4231_RECORD_IRQ |
		       CS4231_TIMER_IRQ);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->mode = mode;
	up(&chip->open_mutex);
	return 0;
}

static void snd_cs4231_close(cs4231_t *chip, unsigned int mode)
{
	unsigned long flags;

	down(&chip->open_mutex);
	chip->mode &= ~mode;
	if (chip->mode & CS4231_MODE_OPEN) {
		up(&chip->open_mutex);
		return;
	}
	snd_cs4231_calibrate_mute(chip, 1);

	/* disable IRQ */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	chip->image[CS4231_PIN_CTRL] &= ~CS4231_IRQ_ENABLE;
	snd_cs4231_out(chip, CS4231_PIN_CTRL, chip->image[CS4231_PIN_CTRL]);

	/* now disable record & playback */

	if (chip->image[CS4231_IFACE_CTRL] & (CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
					       CS4231_RECORD_ENABLE | CS4231_RECORD_PIO)) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->reg_lock, flags);
		chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
						     CS4231_RECORD_ENABLE | CS4231_RECORD_PIO);
		snd_cs4231_out(chip, CS4231_IFACE_CTRL, chip->image[CS4231_IFACE_CTRL]);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		snd_cs4231_mce_down(chip);
		spin_lock_irqsave(&chip->reg_lock, flags);
	}

	/* clear IRQ again */
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	cs4231_outb(chip, CS4231P(STATUS), 0);	/* clear IRQ */
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	snd_cs4231_calibrate_mute(chip, 0);

	chip->mode = 0;
	up(&chip->open_mutex);
}

/*
 *  timer open/close
 */

static int snd_cs4231_timer_open(snd_timer_t * timer)
{
	cs4231_t *chip = snd_timer_chip(timer);
	snd_cs4231_open(chip, CS4231_MODE_TIMER);
	return 0;
}

static int snd_cs4231_timer_close(snd_timer_t * timer)
{
	cs4231_t *chip = snd_timer_chip(timer);
	snd_cs4231_close(chip, CS4231_MODE_TIMER);
	return 0;
}

static struct _snd_timer_hardware snd_cs4231_timer_table =
{
	.flags =	SNDRV_TIMER_HW_AUTO,
	.resolution =	9945,
	.ticks =	65535,
	.open =		snd_cs4231_timer_open,
	.close =	snd_cs4231_timer_close,
	.c_resolution = snd_cs4231_timer_resolution,
	.start =	snd_cs4231_timer_start,
	.stop =		snd_cs4231_timer_stop,
};

/*
 *  ok.. exported functions..
 */

static int snd_cs4231_playback_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	unsigned char new_pdfr;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	new_pdfr = snd_cs4231_get_format(chip, params_format(hw_params), params_channels(hw_params)) |
		   snd_cs4231_get_rate(params_rate(hw_params));
	chip->set_playback_format(chip, hw_params, new_pdfr);
	return 0;
}

static int snd_cs4231_playback_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

#ifdef LEGACY_SUPPORT
static int snd_cs4231_playback_prepare(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->p_dma_size = size;
	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO);
	snd_dma_program(chip->dma1, runtime->dma_addr, size, DMA_MODE_WRITE | DMA_AUTOINIT);
	count = snd_cs4231_get_count(chip->image[CS4231_PLAYBK_FORMAT], count) - 1;
	snd_cs4231_out(chip, CS4231_PLY_LWR_CNT, (unsigned char) count);
	snd_cs4231_out(chip, CS4231_PLY_UPR_CNT, (unsigned char) (count >> 8));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#if 0
	snd_cs4231_debug(chip);
#endif
	return 0;
}
#endif /* LEGACY_SUPPORT */

static int snd_cs4231_capture_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	unsigned char new_cdfr;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	new_cdfr = snd_cs4231_get_format(chip, params_format(hw_params), params_channels(hw_params)) |
		   snd_cs4231_get_rate(params_rate(hw_params));
	chip->set_capture_format(chip, hw_params, new_cdfr);
	return 0;
}

static int snd_cs4231_capture_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

#ifdef LEGACY_SUPPORT
static int snd_cs4231_capture_prepare(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->c_dma_size = size;
	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_RECORD_ENABLE | CS4231_RECORD_PIO);
	snd_dma_program(chip->dma2, runtime->dma_addr, size, DMA_MODE_READ | DMA_AUTOINIT);
	count = snd_cs4231_get_count(chip->image[CS4231_REC_FORMAT], count) - 1;
	if (chip->single_dma && chip->hardware != CS4231_HW_INTERWAVE) {
		snd_cs4231_out(chip, CS4231_PLY_LWR_CNT, (unsigned char) count);
		snd_cs4231_out(chip, CS4231_PLY_UPR_CNT, (unsigned char) (count >> 8));
	} else {
		snd_cs4231_out(chip, CS4231_REC_LWR_CNT, (unsigned char) count);
		snd_cs4231_out(chip, CS4231_REC_UPR_CNT, (unsigned char) (count >> 8));
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}
#endif

static void snd_cs4231_overrange(cs4231_t *chip)
{
	unsigned long flags;
	unsigned char res;

	spin_lock_irqsave(&chip->reg_lock, flags);
	res = snd_cs4231_in(chip, CS4231_TEST_INIT);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (res & (0x08 | 0x02))	/* detect overrange only above 0dB; may be user selectable? */
		chip->capture_substream->runtime->overrange++;
}

irqreturn_t snd_cs4231_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	cs4231_t *chip = dev_id;
	unsigned char status;

	status = snd_cs4231_in(chip, CS4231_IRQ_STATUS);
	if (status & CS4231_TIMER_IRQ) {
		if (chip->timer)
			snd_timer_interrupt(chip->timer, chip->timer->sticks);
	}		
	if (chip->single_dma && chip->hardware != CS4231_HW_INTERWAVE) {
		if (status & CS4231_PLAYBACK_IRQ) {
			if (chip->mode & CS4231_MODE_PLAY) {
				if (chip->playback_substream)
					snd_pcm_period_elapsed(chip->playback_substream);
			}
			if (chip->mode & CS4231_MODE_RECORD) {
				if (chip->capture_substream) {
					snd_cs4231_overrange(chip);
					snd_pcm_period_elapsed(chip->capture_substream);
				}
			}
		}
	} else {
		if (status & CS4231_PLAYBACK_IRQ) {
			if (chip->playback_substream)
				snd_pcm_period_elapsed(chip->playback_substream);
		}
		if (status & CS4231_RECORD_IRQ) {
			if (chip->capture_substream) {
				snd_cs4231_overrange(chip);
				snd_pcm_period_elapsed(chip->capture_substream);
			}
		}
	}

	spin_lock(&chip->reg_lock);
	snd_cs4231_outm(chip, CS4231_IRQ_STATUS, ~CS4231_ALL_IRQS | ~status, 0);
	spin_unlock(&chip->reg_lock);
	return IRQ_HANDLED;
}

#ifdef LEGACY_SUPPORT
static snd_pcm_uframes_t snd_cs4231_playback_pointer(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE))
		return 0;
	ptr = snd_dma_pointer(chip->dma1, chip->p_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_cs4231_capture_pointer(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	
	if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_RECORD_ENABLE))
		return 0;
	ptr = snd_dma_pointer(chip->dma2, chip->c_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}
#endif /* LEGACY_SUPPORT */

/*

 */

static int snd_cs4231_probe(cs4231_t *chip)
{
	unsigned long flags;
	int i, id, rev;
	unsigned char *ptr;
	unsigned int hw;

#if 0
	snd_cs4231_debug(chip);
#endif
	id = 0;
	for (i = 0; i < 50; i++) {
		mb();
		if (cs4231_inb(chip, CS4231P(REGSEL)) & CS4231_INIT)
			udelay(2000);
		else {
			spin_lock_irqsave(&chip->reg_lock, flags);
			snd_cs4231_out(chip, CS4231_MISC_INFO, CS4231_MODE2);
			id = snd_cs4231_in(chip, CS4231_MISC_INFO) & 0x0f;
			spin_unlock_irqrestore(&chip->reg_lock, flags);
			if (id == 0x0a)
				break;	/* this is valid value */
		}
	}
	snd_printdd("cs4231: port = 0x%lx, id = 0x%x\n", chip->port, id);
	if (id != 0x0a)
		return -ENODEV;	/* no valid device found */

	if (((hw = chip->hardware) & CS4231_HW_TYPE_MASK) == CS4231_HW_DETECT) {
		rev = snd_cs4231_in(chip, CS4231_VERSION) & 0xe7;
		snd_printdd("CS4231: VERSION (I25) = 0x%x\n", rev);
		if (rev == 0x80) {
			unsigned char tmp = snd_cs4231_in(chip, 23);
			snd_cs4231_out(chip, 23, ~tmp);
			if (snd_cs4231_in(chip, 23) != tmp)
				chip->hardware = CS4231_HW_AD1845;
			else
				chip->hardware = CS4231_HW_CS4231;
		} else if (rev == 0xa0) {
			chip->hardware = CS4231_HW_CS4231A;
		} else if (rev == 0xa2) {
			chip->hardware = CS4231_HW_CS4232;
		} else if (rev == 0xb2) {
			chip->hardware = CS4231_HW_CS4232A;
		} else if (rev == 0x83) {
			chip->hardware = CS4231_HW_CS4236;
		} else if (rev == 0x03) {
			chip->hardware = CS4231_HW_CS4236B;
		} else {
			snd_printk("unknown CS chip with version 0x%x\n", rev);
			return -ENODEV;		/* unknown CS4231 chip? */
		}
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	cs4231_inb(chip, CS4231P(STATUS));	/* clear any pendings IRQ */
	cs4231_outb(chip, CS4231P(STATUS), 0);
	mb();
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->image[CS4231_MISC_INFO] = CS4231_MODE2;
	switch (chip->hardware) {
	case CS4231_HW_INTERWAVE:
		chip->image[CS4231_MISC_INFO] = CS4231_IW_MODE3;
		break;
	case CS4231_HW_CS4235:
	case CS4231_HW_CS4236B:
	case CS4231_HW_CS4237B:
	case CS4231_HW_CS4238B:
	case CS4231_HW_CS4239:
		if (hw == CS4231_HW_DETECT3)
			chip->image[CS4231_MISC_INFO] = CS4231_4236_MODE3;
		else
			chip->hardware = CS4231_HW_CS4236;
		break;
	}

	chip->image[CS4231_IFACE_CTRL] =
	    (chip->image[CS4231_IFACE_CTRL] & ~CS4231_SINGLE_DMA) |
	    (chip->single_dma ? CS4231_SINGLE_DMA : 0);
	chip->image[CS4231_ALT_FEATURE_1] = 0x80;
	chip->image[CS4231_ALT_FEATURE_2] = chip->hardware == CS4231_HW_INTERWAVE ? 0xc2 : 0x01;
	ptr = (unsigned char *) &chip->image;
	snd_cs4231_mce_down(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 32; i++)	/* ok.. fill all CS4231 registers */
		snd_cs4231_out(chip, i, *ptr++);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_cs4231_mce_up(chip);
	snd_cs4231_mce_down(chip);

	mdelay(2);

	/* ok.. try check hardware version for CS4236+ chips */
	if ((hw & CS4231_HW_TYPE_MASK) == CS4231_HW_DETECT) {
		if (chip->hardware == CS4231_HW_CS4236B) {
			rev = snd_cs4236_ext_in(chip, CS4236_VERSION);
			snd_cs4236_ext_out(chip, CS4236_VERSION, 0xff);
			id = snd_cs4236_ext_in(chip, CS4236_VERSION);
			snd_cs4236_ext_out(chip, CS4236_VERSION, rev);
			snd_printdd("CS4231: ext version; rev = 0x%x, id = 0x%x\n", rev, id);
			if ((id & 0x1f) == 0x1d) {	/* CS4235 */
				chip->hardware = CS4231_HW_CS4235;
				switch (id >> 5) {
				case 4:
				case 5:
				case 6:
					break;
				default:
					snd_printk("unknown CS4235 chip (enhanced version = 0x%x)\n", id);
				}
			} else if ((id & 0x1f) == 0x0b) {	/* CS4236/B */
				switch (id >> 5) {
				case 4:
				case 5:
				case 6:
				case 7:
					chip->hardware = CS4231_HW_CS4236B;
					break;
				default:
					snd_printk("unknown CS4236 chip (enhanced version = 0x%x)\n", id);
				}
			} else if ((id & 0x1f) == 0x08) {	/* CS4237B */
				chip->hardware = CS4231_HW_CS4237B;
				switch (id >> 5) {
				case 4:
				case 5:
				case 6:
				case 7:
					break;
				default:
					snd_printk("unknown CS4237B chip (enhanced version = 0x%x)\n", id);
				}
			} else if ((id & 0x1f) == 0x09) {	/* CS4238B */
				chip->hardware = CS4231_HW_CS4238B;
				switch (id >> 5) {
				case 5:
				case 6:
				case 7:
					break;
				default:
					snd_printk("unknown CS4238B chip (enhanced version = 0x%x)\n", id);
				}
			} else if ((id & 0x1f) == 0x1e) {	/* CS4239 */
				chip->hardware = CS4231_HW_CS4239;
				switch (id >> 5) {
				case 4:
				case 5:
				case 6:
					break;
				default:
					snd_printk("unknown CS4239 chip (enhanced version = 0x%x)\n", id);
				}
			} else {
				snd_printk("unknown CS4236/CS423xB chip (enhanced version = 0x%x)\n", id);
			}
		}
	}
	return 0;		/* all things are ok.. */
}

/*

 */

static snd_pcm_hardware_t snd_cs4231_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_SYNC_START),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW | SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE),
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

static snd_pcm_hardware_t snd_cs4231_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_SYNC_START),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW | SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE),
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

static int snd_cs4231_playback_open(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	runtime->hw = snd_cs4231_playback;

	/* hardware bug in InterWave chipset */
	if (chip->hardware == CS4231_HW_INTERWAVE && chip->dma1 > 3)
	    	runtime->hw.formats &= ~SNDRV_PCM_FMTBIT_MU_LAW;
	
	/* hardware limitation of cheap chips */
	if (chip->hardware == CS4231_HW_CS4235 ||
	    chip->hardware == CS4231_HW_CS4239)
		runtime->hw.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE;

#ifdef LEGACY_SUPPORT
	snd_pcm_limit_isa_dma_size(chip->dma1, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(chip->dma1, &runtime->hw.period_bytes_max);

	if (chip->claim_dma) {
		if ((err = chip->claim_dma(chip, chip->dma_private_data, chip->dma1)) < 0)
			return err;
	}
#endif

	if ((err = snd_cs4231_open(chip, CS4231_MODE_PLAY)) < 0) {
#ifdef LEGACY_SUPPORT
		if (chip->release_dma)
			chip->release_dma(chip, chip->dma_private_data, chip->dma1);
#endif
		snd_free_pages(runtime->dma_area, runtime->dma_bytes);
		return err;
	}
	chip->playback_substream = substream;
#if defined(SBUS_SUPPORT) || defined(EBUS_SUPPORT)
	chip->p_periods_sent = 0;
#endif
	snd_pcm_set_sync(substream);
	chip->rate_constraint(runtime);
	return 0;
}

static int snd_cs4231_capture_open(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	runtime->hw = snd_cs4231_capture;

	/* hardware limitation of cheap chips */
	if (chip->hardware == CS4231_HW_CS4235 ||
	    chip->hardware == CS4231_HW_CS4239)
		runtime->hw.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE;

#ifdef LEGACY_SUPPORT
	snd_pcm_limit_isa_dma_size(chip->dma2, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(chip->dma2, &runtime->hw.period_bytes_max);

	if (chip->claim_dma) {
		if ((err = chip->claim_dma(chip, chip->dma_private_data, chip->dma2)) < 0)
			return err;
	}
#endif

	if ((err = snd_cs4231_open(chip, CS4231_MODE_RECORD)) < 0) {
#ifdef LEGACY_SUPPORT
		if (chip->release_dma)
			chip->release_dma(chip, chip->dma_private_data, chip->dma2);
#endif
		snd_free_pages(runtime->dma_area, runtime->dma_bytes);
		return err;
	}
	chip->capture_substream = substream;
#if defined(SBUS_SUPPORT) || defined(EBUS_SUPPORT)
	chip->c_periods_sent = 0;
#endif
	snd_pcm_set_sync(substream);
	chip->rate_constraint(runtime);
	return 0;
}

static int snd_cs4231_playback_close(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	snd_cs4231_close(chip, CS4231_MODE_PLAY);
	return 0;
}

static int snd_cs4231_capture_close(snd_pcm_substream_t * substream)
{
	cs4231_t *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	snd_cs4231_close(chip, CS4231_MODE_RECORD);
	return 0;
}

#ifdef CONFIG_PM

/* lowlevel suspend callback for CS4231 */
static void snd_cs4231_suspend(cs4231_t *chip)
{
	int reg;
	unsigned long flags;
	
	if (chip->pcm)
		snd_pcm_suspend_all(chip->pcm);
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (reg = 0; reg < 32; reg++)
		chip->image[reg] = snd_cs4231_in(chip, reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/* lowlevel resume callback for CS4231 */
static void snd_cs4231_resume(cs4231_t *chip)
{
	int reg;
	unsigned long flags;
	int timeout;
	
	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (reg = 0; reg < 32; reg++) {
		switch (reg) {
		case CS4231_VERSION:
			break;
		default:
			snd_cs4231_out(chip, reg, chip->image[reg]);
			break;
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#if 0
	snd_cs4231_mce_down(chip);
#else
	/* The following is a workaround to avoid freeze after resume on TP600E.
	   This is the first half of copy of snd_cs4231_mce_down(), but doesn't
	   include rescheduling.  -- iwai
	   */
	snd_cs4231_busy_wait(chip);
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->mce_bit &= ~CS4231_MCE;
	timeout = cs4231_inb(chip, CS4231P(REGSEL));
	cs4231_outb(chip, CS4231P(REGSEL), chip->mce_bit | (timeout & 0x1f));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (timeout == 0x80)
		snd_printk("down [0x%lx]: serious init problem - codec still busy\n", chip->port);
	if ((timeout & CS4231_MCE) == 0 ||
	    !(chip->hardware & (CS4231_HW_CS4231_MASK | CS4231_HW_CS4232_MASK))) {
		return;
	}
	snd_cs4231_busy_wait(chip);
#endif
}

static int snd_cs4231_pm_suspend(snd_card_t *card, pm_message_t state)
{
	cs4231_t *chip = card->pm_private_data;
	if (chip->suspend)
		chip->suspend(chip);
	return 0;
}

static int snd_cs4231_pm_resume(snd_card_t *card)
{
	cs4231_t *chip = card->pm_private_data;
	if (chip->resume)
		chip->resume(chip);
	return 0;
}
#endif /* CONFIG_PM */

#ifdef LEGACY_SUPPORT

static int snd_cs4231_free(cs4231_t *chip)
{
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->res_cport) {
		release_resource(chip->res_cport);
		kfree_nocheck(chip->res_cport);
	}
	if (chip->irq >= 0) {
		disable_irq(chip->irq);
		if (!(chip->hwshare & CS4231_HWSHARE_IRQ))
			free_irq(chip->irq, (void *) chip);
	}
	if (!(chip->hwshare & CS4231_HWSHARE_DMA1) && chip->dma1 >= 0) {
		snd_dma_disable(chip->dma1);
		free_dma(chip->dma1);
	}
	if (!(chip->hwshare & CS4231_HWSHARE_DMA2) && chip->dma2 >= 0 && chip->dma2 != chip->dma1) {
		snd_dma_disable(chip->dma2);
		free_dma(chip->dma2);
	}
	if (chip->timer)
		snd_device_free(chip->card, chip->timer);
	kfree(chip);
	return 0;
}

static int snd_cs4231_dev_free(snd_device_t *device)
{
	cs4231_t *chip = device->device_data;
	return snd_cs4231_free(chip);	
}

#endif /* LEGACY_SUPPORT */

const char *snd_cs4231_chip_id(cs4231_t *chip)
{
	switch (chip->hardware) {
	case CS4231_HW_CS4231:	return "CS4231";
	case CS4231_HW_CS4231A: return "CS4231A";
	case CS4231_HW_CS4232:	return "CS4232";
	case CS4231_HW_CS4232A:	return "CS4232A";
	case CS4231_HW_CS4235:	return "CS4235";
	case CS4231_HW_CS4236:  return "CS4236";
	case CS4231_HW_CS4236B: return "CS4236B";
	case CS4231_HW_CS4237B: return "CS4237B";
	case CS4231_HW_CS4238B: return "CS4238B";
	case CS4231_HW_CS4239:	return "CS4239";
	case CS4231_HW_INTERWAVE: return "AMD InterWave";
	case CS4231_HW_OPL3SA2: return chip->card->shortname;
	case CS4231_HW_AD1845: return "AD1845";
	default: return "???";
	}
}

static int snd_cs4231_new(snd_card_t * card,
			  unsigned short hardware,
			  unsigned short hwshare,
			  cs4231_t ** rchip)
{
	cs4231_t *chip;

	*rchip = NULL;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->hardware = hardware;
	chip->hwshare = hwshare;

	spin_lock_init(&chip->reg_lock);
	init_MUTEX(&chip->mce_mutex);
	init_MUTEX(&chip->open_mutex);
	chip->card = card;
	chip->rate_constraint = snd_cs4231_xrate;
	chip->set_playback_format = snd_cs4231_playback_format;
	chip->set_capture_format = snd_cs4231_capture_format;
        memcpy(&chip->image, &snd_cs4231_original_image, sizeof(snd_cs4231_original_image));
        
        *rchip = chip;
        return 0;
}

#ifdef LEGACY_SUPPORT

int snd_cs4231_create(snd_card_t * card,
	              unsigned long port,
	              unsigned long cport,
		      int irq, int dma1, int dma2,
		      unsigned short hardware,
		      unsigned short hwshare,
		      cs4231_t ** rchip)
{
	static snd_device_ops_t ops = {
		.dev_free =	snd_cs4231_dev_free,
	};
	cs4231_t *chip;
	int err;

	err = snd_cs4231_new(card, hardware, hwshare, &chip);
	if (err < 0)
		return err;
	
	chip->irq = -1;
	chip->dma1 = -1;
	chip->dma2 = -1;

	if ((chip->res_port = request_region(port, 4, "CS4231")) == NULL) {
		snd_printk(KERN_ERR "cs4231: can't grab port 0x%lx\n", port);
		snd_cs4231_free(chip);
		return -EBUSY;
	}
	chip->port = port;
	if ((long)cport >= 0 && (chip->res_cport = request_region(cport, 8, "CS4232 Control")) == NULL) {
		snd_printk(KERN_ERR "cs4231: can't grab control port 0x%lx\n", cport);
		snd_cs4231_free(chip);
		return -ENODEV;
	}
	chip->cport = cport;
	if (!(hwshare & CS4231_HWSHARE_IRQ) && request_irq(irq, snd_cs4231_interrupt, SA_INTERRUPT, "CS4231", (void *) chip)) {
		snd_printk(KERN_ERR "cs4231: can't grab IRQ %d\n", irq);
		snd_cs4231_free(chip);
		return -EBUSY;
	}
	chip->irq = irq;
	if (!(hwshare & CS4231_HWSHARE_DMA1) && request_dma(dma1, "CS4231 - 1")) {
		snd_printk(KERN_ERR "cs4231: can't grab DMA1 %d\n", dma1);
		snd_cs4231_free(chip);
		return -EBUSY;
	}
	chip->dma1 = dma1;
	if (!(hwshare & CS4231_HWSHARE_DMA2) && dma1 != dma2 && dma2 >= 0 && request_dma(dma2, "CS4231 - 2")) {
		snd_printk(KERN_ERR "cs4231: can't grab DMA2 %d\n", dma2);
		snd_cs4231_free(chip);
		return -EBUSY;
	}
	if (dma1 == dma2 || dma2 < 0) {
		chip->single_dma = 1;
		chip->dma2 = chip->dma1;
	} else
		chip->dma2 = dma2;

	/* global setup */
	if (snd_cs4231_probe(chip) < 0) {
		snd_cs4231_free(chip);
		return -ENODEV;
	}
	snd_cs4231_init(chip);

	if (chip->hardware & CS4231_HW_CS4232_MASK) {
		if (chip->res_cport == NULL)
			snd_printk("CS4232 control port features are not accessible\n");
	}

	/* Register device */
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_cs4231_free(chip);
		return err;
	}

#ifdef CONFIG_PM
	/* Power Management */
	chip->suspend = snd_cs4231_suspend;
	chip->resume = snd_cs4231_resume;
	snd_card_set_isa_pm_callback(card, snd_cs4231_pm_suspend, snd_cs4231_pm_resume, chip);
#endif

	*rchip = chip;
	return 0;
}

#endif /* LEGACY_SUPPORT */

static snd_pcm_ops_t snd_cs4231_playback_ops = {
	.open =		snd_cs4231_playback_open,
	.close =	snd_cs4231_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cs4231_playback_hw_params,
	.hw_free =	snd_cs4231_playback_hw_free,
	.prepare =	snd_cs4231_playback_prepare,
	.trigger =	snd_cs4231_trigger,
	.pointer =	snd_cs4231_playback_pointer,
};

static snd_pcm_ops_t snd_cs4231_capture_ops = {
	.open =		snd_cs4231_capture_open,
	.close =	snd_cs4231_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cs4231_capture_hw_params,
	.hw_free =	snd_cs4231_capture_hw_free,
	.prepare =	snd_cs4231_capture_prepare,
	.trigger =	snd_cs4231_trigger,
	.pointer =	snd_cs4231_capture_pointer,
};

static void snd_cs4231_pcm_free(snd_pcm_t *pcm)
{
	cs4231_t *chip = pcm->private_data;
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_cs4231_pcm(cs4231_t *chip, int device, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS4231", device, 1, 1, &pcm)) < 0)
		return err;

	spin_lock_init(&chip->reg_lock);
	init_MUTEX(&chip->mce_mutex);
	init_MUTEX(&chip->open_mutex);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs4231_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cs4231_capture_ops);
	
	/* global setup */
	pcm->private_data = chip;
	pcm->private_free = snd_cs4231_pcm_free;
	pcm->info_flags = 0;
	if (chip->single_dma)
		pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;
	if (chip->hardware != CS4231_HW_INTERWAVE)
		pcm->info_flags |= SNDRV_PCM_INFO_JOINT_DUPLEX;
	strcpy(pcm->name, snd_cs4231_chip_id(chip));

#ifdef LEGACY_SUPPORT
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, chip->dma1 > 3 || chip->dma2 > 3 ? 128*1024 : 64*1024);
#else
#  ifdef EBUS_SUPPORT
        if (chip->ebus_flag) {
                snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
                				      chip->dev_u.pdev,
						      64*1024, 128*1024);
        } else {
#  endif
#  ifdef SBUS_SUPPORT
                snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_SBUS,
                				      chip->dev_u.sdev,
						      64*1024, 128*1024);
#  endif
#  ifdef EBUS_SUPPORT
        }
#  endif
#endif

	chip->pcm = pcm;
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static void snd_cs4231_timer_free(snd_timer_t *timer)
{
	cs4231_t *chip = timer->private_data;
	chip->timer = NULL;
}

int snd_cs4231_timer(cs4231_t *chip, int device, snd_timer_t **rtimer)
{
	snd_timer_t *timer;
	snd_timer_id_t tid;
	int err;

	/* Timer initialization */
	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = chip->card->number;
	tid.device = device;
	tid.subdevice = 0;
	if ((err = snd_timer_new(chip->card, "CS4231", &tid, &timer)) < 0)
		return err;
	strcpy(timer->name, snd_cs4231_chip_id(chip));
	timer->private_data = chip;
	timer->private_free = snd_cs4231_timer_free;
	timer->hw = snd_cs4231_timer_table;
	chip->timer = timer;
	if (rtimer)
		*rtimer = timer;
	return 0;
}
	
/*
 *  MIXER part
 */

static int snd_cs4231_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = {
		"Line", "Aux", "Mic", "Mix"
	};
	static char *opl3sa_texts[4] = {
		"Line", "CD", "Mic", "Mix"
	};
	static char *gusmax_texts[4] = {
		"Line", "Synth", "Mic", "Mix"
	};
	char **ptexts = texts;
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);

	snd_assert(chip->card != NULL, return -EINVAL);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	if (!strcmp(chip->card->driver, "GUS MAX"))
		ptexts = gusmax_texts;
	switch (chip->hardware) {
	case CS4231_HW_INTERWAVE: ptexts = gusmax_texts; break;
	case CS4231_HW_OPL3SA2: ptexts = opl3sa_texts; break;
	}
	strcpy(uinfo->value.enumerated.name, ptexts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_cs4231_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.enumerated.item[0] = (chip->image[CS4231_LEFT_INPUT] & CS4231_MIXS_ALL) >> 6;
	ucontrol->value.enumerated.item[1] = (chip->image[CS4231_RIGHT_INPUT] & CS4231_MIXS_ALL) >> 6;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4231_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned short left, right;
	int change;
	
	if (ucontrol->value.enumerated.item[0] > 3 ||
	    ucontrol->value.enumerated.item[1] > 3)
		return -EINVAL;
	left = ucontrol->value.enumerated.item[0] << 6;
	right = ucontrol->value.enumerated.item[1] << 6;
	spin_lock_irqsave(&chip->reg_lock, flags);
	left = (chip->image[CS4231_LEFT_INPUT] & ~CS4231_MIXS_ALL) | left;
	right = (chip->image[CS4231_RIGHT_INPUT] & ~CS4231_MIXS_ALL) | right;
	change = left != chip->image[CS4231_LEFT_INPUT] ||
	         right != chip->image[CS4231_RIGHT_INPUT];
	snd_cs4231_out(chip, CS4231_LEFT_INPUT, left);
	snd_cs4231_out(chip, CS4231_RIGHT_INPUT, right);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

int snd_cs4231_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

int snd_cs4231_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
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

int snd_cs4231_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
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
	snd_cs4231_out(chip, reg, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

int snd_cs4231_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

int snd_cs4231_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
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

int snd_cs4231_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4231_t *chip = snd_kcontrol_chip(kcontrol);
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
	val1 = (chip->image[left_reg] & ~(mask << shift_left)) | val1;
	val2 = (chip->image[right_reg] & ~(mask << shift_right)) | val2;
	change = val1 != chip->image[left_reg] || val2 != chip->image[right_reg];
	snd_cs4231_out(chip, left_reg, val1);
	snd_cs4231_out(chip, right_reg, val2);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_cs4231_controls[] = {
CS4231_DOUBLE("PCM Playback Switch", 0, CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 7, 7, 1, 1),
CS4231_DOUBLE("PCM Playback Volume", 0, CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 0, 0, 63, 1),
CS4231_DOUBLE("Line Playback Switch", 0, CS4231_LEFT_LINE_IN, CS4231_RIGHT_LINE_IN, 7, 7, 1, 1),
CS4231_DOUBLE("Line Playback Volume", 0, CS4231_LEFT_LINE_IN, CS4231_RIGHT_LINE_IN, 0, 0, 31, 1),
CS4231_DOUBLE("Aux Playback Switch", 0, CS4231_AUX1_LEFT_INPUT, CS4231_AUX1_RIGHT_INPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Aux Playback Volume", 0, CS4231_AUX1_LEFT_INPUT, CS4231_AUX1_RIGHT_INPUT, 0, 0, 31, 1),
CS4231_DOUBLE("Aux Playback Switch", 1, CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Aux Playback Volume", 1, CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 0, 0, 31, 1),
CS4231_SINGLE("Mono Playback Switch", 0, CS4231_MONO_CTRL, 7, 1, 1),
CS4231_SINGLE("Mono Playback Volume", 0, CS4231_MONO_CTRL, 0, 15, 1),
CS4231_SINGLE("Mono Output Playback Switch", 0, CS4231_MONO_CTRL, 6, 1, 1),
CS4231_SINGLE("Mono Output Playback Bypass", 0, CS4231_MONO_CTRL, 5, 1, 0),
CS4231_DOUBLE("Capture Volume", 0, CS4231_LEFT_INPUT, CS4231_RIGHT_INPUT, 0, 0, 15, 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_cs4231_info_mux,
	.get = snd_cs4231_get_mux,
	.put = snd_cs4231_put_mux,
},
CS4231_DOUBLE("Mic Boost", 0, CS4231_LEFT_INPUT, CS4231_RIGHT_INPUT, 5, 5, 1, 0),
CS4231_SINGLE("Loopback Capture Switch", 0, CS4231_LOOPBACK, 0, 1, 0),
CS4231_SINGLE("Loopback Capture Volume", 0, CS4231_LOOPBACK, 2, 63, 1)
};
                                        
int snd_cs4231_mixer(cs4231_t *chip)
{
	snd_card_t *card;
	unsigned int idx;
	int err;

	snd_assert(chip != NULL && chip->pcm != NULL, return -EINVAL);

	card = chip->card;

	strcpy(card->mixername, chip->pcm->name);

	for (idx = 0; idx < ARRAY_SIZE(snd_cs4231_controls); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_cs4231_controls[idx], chip))) < 0)
			return err;
	}
	return 0;
}

EXPORT_SYMBOL(snd_cs4231_out);
EXPORT_SYMBOL(snd_cs4231_in);
EXPORT_SYMBOL(snd_cs4236_ext_out);
EXPORT_SYMBOL(snd_cs4236_ext_in);
EXPORT_SYMBOL(snd_cs4231_mce_up);
EXPORT_SYMBOL(snd_cs4231_mce_down);
EXPORT_SYMBOL(snd_cs4231_interrupt);
EXPORT_SYMBOL(snd_cs4231_chip_id);
EXPORT_SYMBOL(snd_cs4231_create);
EXPORT_SYMBOL(snd_cs4231_pcm);
EXPORT_SYMBOL(snd_cs4231_mixer);
EXPORT_SYMBOL(snd_cs4231_timer);
EXPORT_SYMBOL(snd_cs4231_info_single);
EXPORT_SYMBOL(snd_cs4231_get_single);
EXPORT_SYMBOL(snd_cs4231_put_single);
EXPORT_SYMBOL(snd_cs4231_info_double);
EXPORT_SYMBOL(snd_cs4231_get_double);
EXPORT_SYMBOL(snd_cs4231_put_double);

/*
 *  INIT part
 */

static int __init alsa_cs4231_init(void)
{
	return 0;
}

static void __exit alsa_cs4231_exit(void)
{
}

module_init(alsa_cs4231_init)
module_exit(alsa_cs4231_exit)
