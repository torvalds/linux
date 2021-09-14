// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 *
 * Linux multisound pinnacle/fiji driver for ALSA.
 *
 * 2002/06/30 Karsten Wiese:
 *	for now this is only used to build a pinnacle / fiji driver.
 *	the OSS parent of this code is designed to also support
 *	the multisound classic via the file msnd_classic.c.
 *	to make it easier for some brave heart to implemt classic
 *	support in alsa, i left all the MSND_CLASSIC tokens in this file.
 *	but for now this untested & undone.
 *
 * ripped from linux kernel 2.4.18 by Karsten Wiese.
 *
 * the following is a copy of the 2.4.18 OSS FREE file-heading comment:
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 * msnd_pinnacle.c / msnd_classic.c
 *
 * -- If MSND_CLASSIC is defined:
 *
 *     -> driver for Turtle Beach Classic/Monterey/Tahiti
 *
 * -- Else
 *
 *     -> driver for Turtle Beach Pinnacle/Fiji
 *
 * 12-3-2000  Modified IO port validation  Steve Sycamore
 *
 * Copyright (C) 1998 Andrew Veliath
 *
 ********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/firmware.h>
#include <linux/isa.h>
#include <linux/isapnp.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/asound.h>
#include <sound/pcm.h>
#include <sound/mpu401.h>

#ifdef MSND_CLASSIC
# ifndef __alpha__
#  define SLOWIO
# endif
#endif
#include "msnd.h"
#ifdef MSND_CLASSIC
#  include "msnd_classic.h"
#  define LOGNAME			"msnd_classic"
#  define DEV_NAME			"msnd-classic"
#else
#  include "msnd_pinnacle.h"
#  define LOGNAME			"snd_msnd_pinnacle"
#  define DEV_NAME			"msnd-pinnacle"
#endif

static void set_default_audio_parameters(struct snd_msnd *chip)
{
	chip->play_sample_size = snd_pcm_format_width(DEFSAMPLESIZE);
	chip->play_sample_rate = DEFSAMPLERATE;
	chip->play_channels = DEFCHANNELS;
	chip->capture_sample_size = snd_pcm_format_width(DEFSAMPLESIZE);
	chip->capture_sample_rate = DEFSAMPLERATE;
	chip->capture_channels = DEFCHANNELS;
}

static void snd_msnd_eval_dsp_msg(struct snd_msnd *chip, u16 wMessage)
{
	switch (HIBYTE(wMessage)) {
	case HIMT_PLAY_DONE: {
		if (chip->banksPlayed < 3)
			snd_printdd("%08X: HIMT_PLAY_DONE: %i\n",
				(unsigned)jiffies, LOBYTE(wMessage));

		if (chip->last_playbank == LOBYTE(wMessage)) {
			snd_printdd("chip.last_playbank == LOBYTE(wMessage)\n");
			break;
		}
		chip->banksPlayed++;

		if (test_bit(F_WRITING, &chip->flags))
			snd_msnd_DAPQ(chip, 0);

		chip->last_playbank = LOBYTE(wMessage);
		chip->playDMAPos += chip->play_period_bytes;
		if (chip->playDMAPos > chip->playLimit)
			chip->playDMAPos = 0;
		snd_pcm_period_elapsed(chip->playback_substream);

		break;
	}
	case HIMT_RECORD_DONE:
		if (chip->last_recbank == LOBYTE(wMessage))
			break;
		chip->last_recbank = LOBYTE(wMessage);
		chip->captureDMAPos += chip->capturePeriodBytes;
		if (chip->captureDMAPos > (chip->captureLimit))
			chip->captureDMAPos = 0;

		if (test_bit(F_READING, &chip->flags))
			snd_msnd_DARQ(chip, chip->last_recbank);

		snd_pcm_period_elapsed(chip->capture_substream);
		break;

	case HIMT_DSP:
		switch (LOBYTE(wMessage)) {
#ifndef MSND_CLASSIC
		case HIDSP_PLAY_UNDER:
#endif
		case HIDSP_INT_PLAY_UNDER:
			snd_printd(KERN_WARNING LOGNAME ": Play underflow %i\n",
				chip->banksPlayed);
			if (chip->banksPlayed > 2)
				clear_bit(F_WRITING, &chip->flags);
			break;

		case HIDSP_INT_RECORD_OVER:
			snd_printd(KERN_WARNING LOGNAME ": Record overflow\n");
			clear_bit(F_READING, &chip->flags);
			break;

		default:
			snd_printd(KERN_WARNING LOGNAME
				   ": DSP message %d 0x%02x\n",
				   LOBYTE(wMessage), LOBYTE(wMessage));
			break;
		}
		break;

	case HIMT_MIDI_IN_UCHAR:
		if (chip->msndmidi_mpu)
			snd_msndmidi_input_read(chip->msndmidi_mpu);
		break;

	default:
		snd_printd(KERN_WARNING LOGNAME ": HIMT message %d 0x%02x\n",
			   HIBYTE(wMessage), HIBYTE(wMessage));
		break;
	}
}

static irqreturn_t snd_msnd_interrupt(int irq, void *dev_id)
{
	struct snd_msnd *chip = dev_id;
	void __iomem *pwDSPQData = chip->mappedbase + DSPQ_DATA_BUFF;
	u16 head, tail, size;

	/* Send ack to DSP */
	/* inb(chip->io + HP_RXL); */

	/* Evaluate queued DSP messages */
	head = readw(chip->DSPQ + JQS_wHead);
	tail = readw(chip->DSPQ + JQS_wTail);
	size = readw(chip->DSPQ + JQS_wSize);
	if (head > size || tail > size)
		goto out;
	while (head != tail) {
		snd_msnd_eval_dsp_msg(chip, readw(pwDSPQData + 2 * head));
		if (++head > size)
			head = 0;
		writew(head, chip->DSPQ + JQS_wHead);
	}
 out:
	/* Send ack to DSP */
	inb(chip->io + HP_RXL);
	return IRQ_HANDLED;
}


static int snd_msnd_reset_dsp(long io, unsigned char *info)
{
	int timeout = 100;

	outb(HPDSPRESET_ON, io + HP_DSPR);
	msleep(1);
#ifndef MSND_CLASSIC
	if (info)
		*info = inb(io + HP_INFO);
#endif
	outb(HPDSPRESET_OFF, io + HP_DSPR);
	msleep(1);
	while (timeout-- > 0) {
		if (inb(io + HP_CVR) == HP_CVR_DEF)
			return 0;
		msleep(1);
	}
	snd_printk(KERN_ERR LOGNAME ": Cannot reset DSP\n");

	return -EIO;
}

static int snd_msnd_probe(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	unsigned char info;
#ifndef MSND_CLASSIC
	char *xv, *rev = NULL;
	char *pin = "TB Pinnacle", *fiji = "TB Fiji";
	char *pinfiji = "TB Pinnacle/Fiji";
#endif

	if (!request_region(chip->io, DSP_NUMIO, "probing")) {
		snd_printk(KERN_ERR LOGNAME ": I/O port conflict\n");
		return -ENODEV;
	}

	if (snd_msnd_reset_dsp(chip->io, &info) < 0) {
		release_region(chip->io, DSP_NUMIO);
		return -ENODEV;
	}

#ifdef MSND_CLASSIC
	strcpy(card->shortname, "Classic/Tahiti/Monterey");
	strcpy(card->longname, "Turtle Beach Multisound");
	printk(KERN_INFO LOGNAME ": %s, "
	       "I/O 0x%lx-0x%lx, IRQ %d, memory mapped to 0x%lX-0x%lX\n",
	       card->shortname,
	       chip->io, chip->io + DSP_NUMIO - 1,
	       chip->irq,
	       chip->base, chip->base + 0x7fff);
#else
	switch (info >> 4) {
	case 0xf:
		xv = "<= 1.15";
		break;
	case 0x1:
		xv = "1.18/1.2";
		break;
	case 0x2:
		xv = "1.3";
		break;
	case 0x3:
		xv = "1.4";
		break;
	default:
		xv = "unknown";
		break;
	}

	switch (info & 0x7) {
	case 0x0:
		rev = "I";
		strcpy(card->shortname, pin);
		break;
	case 0x1:
		rev = "F";
		strcpy(card->shortname, pin);
		break;
	case 0x2:
		rev = "G";
		strcpy(card->shortname, pin);
		break;
	case 0x3:
		rev = "H";
		strcpy(card->shortname, pin);
		break;
	case 0x4:
		rev = "E";
		strcpy(card->shortname, fiji);
		break;
	case 0x5:
		rev = "C";
		strcpy(card->shortname, fiji);
		break;
	case 0x6:
		rev = "D";
		strcpy(card->shortname, fiji);
		break;
	case 0x7:
		rev = "A-B (Fiji) or A-E (Pinnacle)";
		strcpy(card->shortname, pinfiji);
		break;
	}
	strcpy(card->longname, "Turtle Beach Multisound Pinnacle");
	printk(KERN_INFO LOGNAME ": %s revision %s, Xilinx version %s, "
	       "I/O 0x%lx-0x%lx, IRQ %d, memory mapped to 0x%lX-0x%lX\n",
	       card->shortname,
	       rev, xv,
	       chip->io, chip->io + DSP_NUMIO - 1,
	       chip->irq,
	       chip->base, chip->base + 0x7fff);
#endif

	release_region(chip->io, DSP_NUMIO);
	return 0;
}

static int snd_msnd_init_sma(struct snd_msnd *chip)
{
	static int initted;
	u16 mastVolLeft, mastVolRight;
	unsigned long flags;

#ifdef MSND_CLASSIC
	outb(chip->memid, chip->io + HP_MEMM);
#endif
	outb(HPBLKSEL_0, chip->io + HP_BLKS);
	/* Motorola 56k shared memory base */
	chip->SMA = chip->mappedbase + SMA_STRUCT_START;

	if (initted) {
		mastVolLeft = readw(chip->SMA + SMA_wCurrMastVolLeft);
		mastVolRight = readw(chip->SMA + SMA_wCurrMastVolRight);
	} else
		mastVolLeft = mastVolRight = 0;
	memset_io(chip->mappedbase, 0, 0x8000);

	/* Critical section: bank 1 access */
	spin_lock_irqsave(&chip->lock, flags);
	outb(HPBLKSEL_1, chip->io + HP_BLKS);
	memset_io(chip->mappedbase, 0, 0x8000);
	outb(HPBLKSEL_0, chip->io + HP_BLKS);
	spin_unlock_irqrestore(&chip->lock, flags);

	/* Digital audio play queue */
	chip->DAPQ = chip->mappedbase + DAPQ_OFFSET;
	snd_msnd_init_queue(chip->DAPQ, DAPQ_DATA_BUFF, DAPQ_BUFF_SIZE);

	/* Digital audio record queue */
	chip->DARQ = chip->mappedbase + DARQ_OFFSET;
	snd_msnd_init_queue(chip->DARQ, DARQ_DATA_BUFF, DARQ_BUFF_SIZE);

	/* MIDI out queue */
	chip->MODQ = chip->mappedbase + MODQ_OFFSET;
	snd_msnd_init_queue(chip->MODQ, MODQ_DATA_BUFF, MODQ_BUFF_SIZE);

	/* MIDI in queue */
	chip->MIDQ = chip->mappedbase + MIDQ_OFFSET;
	snd_msnd_init_queue(chip->MIDQ, MIDQ_DATA_BUFF, MIDQ_BUFF_SIZE);

	/* DSP -> host message queue */
	chip->DSPQ = chip->mappedbase + DSPQ_OFFSET;
	snd_msnd_init_queue(chip->DSPQ, DSPQ_DATA_BUFF, DSPQ_BUFF_SIZE);

	/* Setup some DSP values */
#ifndef MSND_CLASSIC
	writew(1, chip->SMA + SMA_wCurrPlayFormat);
	writew(chip->play_sample_size, chip->SMA + SMA_wCurrPlaySampleSize);
	writew(chip->play_channels, chip->SMA + SMA_wCurrPlayChannels);
	writew(chip->play_sample_rate, chip->SMA + SMA_wCurrPlaySampleRate);
#endif
	writew(chip->play_sample_rate, chip->SMA + SMA_wCalFreqAtoD);
	writew(mastVolLeft, chip->SMA + SMA_wCurrMastVolLeft);
	writew(mastVolRight, chip->SMA + SMA_wCurrMastVolRight);
#ifndef MSND_CLASSIC
	writel(0x00010000, chip->SMA + SMA_dwCurrPlayPitch);
	writel(0x00000001, chip->SMA + SMA_dwCurrPlayRate);
#endif
	writew(0x303, chip->SMA + SMA_wCurrInputTagBits);

	initted = 1;

	return 0;
}


static int upload_dsp_code(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	const struct firmware *init_fw = NULL, *perm_fw = NULL;
	int err;

	outb(HPBLKSEL_0, chip->io + HP_BLKS);

	err = request_firmware(&init_fw, INITCODEFILE, card->dev);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": Error loading " INITCODEFILE);
		goto cleanup1;
	}
	err = request_firmware(&perm_fw, PERMCODEFILE, card->dev);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": Error loading " PERMCODEFILE);
		goto cleanup;
	}

	memcpy_toio(chip->mappedbase, perm_fw->data, perm_fw->size);
	if (snd_msnd_upload_host(chip, init_fw->data, init_fw->size) < 0) {
		printk(KERN_WARNING LOGNAME ": Error uploading to DSP\n");
		err = -ENODEV;
		goto cleanup;
	}
	printk(KERN_INFO LOGNAME ": DSP firmware uploaded\n");
	err = 0;

cleanup:
	release_firmware(perm_fw);
cleanup1:
	release_firmware(init_fw);
	return err;
}

#ifdef MSND_CLASSIC
static void reset_proteus(struct snd_msnd *chip)
{
	outb(HPPRORESET_ON, chip->io + HP_PROR);
	msleep(TIME_PRO_RESET);
	outb(HPPRORESET_OFF, chip->io + HP_PROR);
	msleep(TIME_PRO_RESET_DONE);
}
#endif

static int snd_msnd_initialize(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	int err, timeout;

#ifdef MSND_CLASSIC
	outb(HPWAITSTATE_0, chip->io + HP_WAIT);
	outb(HPBITMODE_16, chip->io + HP_BITM);

	reset_proteus(chip);
#endif
	err = snd_msnd_init_sma(chip);
	if (err < 0) {
		printk(KERN_WARNING LOGNAME ": Cannot initialize SMA\n");
		return err;
	}

	err = snd_msnd_reset_dsp(chip->io, NULL);
	if (err < 0)
		return err;

	err = upload_dsp_code(card);
	if (err < 0) {
		printk(KERN_WARNING LOGNAME ": Cannot upload DSP code\n");
		return err;
	}

	timeout = 200;

	while (readw(chip->mappedbase)) {
		msleep(1);
		if (!timeout--) {
			snd_printd(KERN_ERR LOGNAME ": DSP reset timeout\n");
			return -EIO;
		}
	}

	snd_msndmix_setup(chip);
	return 0;
}

static int snd_msnd_dsp_full_reset(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	int rv;

	if (test_bit(F_RESETTING, &chip->flags) || ++chip->nresets > 10)
		return 0;

	set_bit(F_RESETTING, &chip->flags);
	snd_msnd_dsp_halt(chip, NULL);	/* Unconditionally halt */

	rv = snd_msnd_initialize(card);
	if (rv)
		printk(KERN_WARNING LOGNAME ": DSP reset failed\n");
	snd_msndmix_force_recsrc(chip, 0);
	clear_bit(F_RESETTING, &chip->flags);
	return rv;
}


static int snd_msnd_send_dsp_cmd_chk(struct snd_msnd *chip, u8 cmd)
{
	if (snd_msnd_send_dsp_cmd(chip, cmd) == 0)
		return 0;
	snd_msnd_dsp_full_reset(chip->card);
	return snd_msnd_send_dsp_cmd(chip, cmd);
}

static int snd_msnd_calibrate_adc(struct snd_msnd *chip, u16 srate)
{
	snd_printdd("snd_msnd_calibrate_adc(%i)\n", srate);
	writew(srate, chip->SMA + SMA_wCalFreqAtoD);
	if (chip->calibrate_signal == 0)
		writew(readw(chip->SMA + SMA_wCurrHostStatusFlags)
		       | 0x0001, chip->SMA + SMA_wCurrHostStatusFlags);
	else
		writew(readw(chip->SMA + SMA_wCurrHostStatusFlags)
		       & ~0x0001, chip->SMA + SMA_wCurrHostStatusFlags);
	if (snd_msnd_send_word(chip, 0, 0, HDEXAR_CAL_A_TO_D) == 0 &&
	    snd_msnd_send_dsp_cmd_chk(chip, HDEX_AUX_REQ) == 0) {
		schedule_timeout_interruptible(msecs_to_jiffies(333));
		return 0;
	}
	printk(KERN_WARNING LOGNAME ": ADC calibration failed\n");
	return -EIO;
}

/*
 * ALSA callback function, called when attempting to open the MIDI device.
 */
static int snd_msnd_mpu401_open(struct snd_mpu401 *mpu)
{
	snd_msnd_enable_irq(mpu->private_data);
	snd_msnd_send_dsp_cmd(mpu->private_data, HDEX_MIDI_IN_START);
	return 0;
}

static void snd_msnd_mpu401_close(struct snd_mpu401 *mpu)
{
	snd_msnd_send_dsp_cmd(mpu->private_data, HDEX_MIDI_IN_STOP);
	snd_msnd_disable_irq(mpu->private_data);
}

static long mpu_io[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;

static int snd_msnd_attach(struct snd_card *card)
{
	struct snd_msnd *chip = card->private_data;
	int err;

	err = devm_request_irq(card->dev, chip->irq, snd_msnd_interrupt, 0,
			       card->shortname, chip);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": Couldn't grab IRQ %d\n", chip->irq);
		return err;
	}
	card->sync_irq = chip->irq;
	if (!devm_request_region(card->dev, chip->io, DSP_NUMIO,
				 card->shortname))
		return -EBUSY;

	if (!devm_request_mem_region(card->dev, chip->base, BUFFSIZE,
				     card->shortname)) {
		printk(KERN_ERR LOGNAME
			": unable to grab memory region 0x%lx-0x%lx\n",
			chip->base, chip->base + BUFFSIZE - 1);
		return -EBUSY;
	}
	chip->mappedbase = devm_ioremap(card->dev, chip->base, 0x8000);
	if (!chip->mappedbase) {
		printk(KERN_ERR LOGNAME
			": unable to map memory region 0x%lx-0x%lx\n",
			chip->base, chip->base + BUFFSIZE - 1);
		return -EIO;
	}

	err = snd_msnd_dsp_full_reset(card);
	if (err < 0)
		return err;

	err = snd_msnd_pcm(card, 0);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": error creating new PCM device\n");
		return err;
	}

	err = snd_msndmix_new(card);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": error creating new Mixer device\n");
		return err;
	}


	if (mpu_io[0] != SNDRV_AUTO_PORT) {
		struct snd_mpu401 *mpu;

		err = snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
					  mpu_io[0],
					  MPU401_MODE_INPUT |
					  MPU401_MODE_OUTPUT,
					  mpu_irq[0],
					  &chip->rmidi);
		if (err < 0) {
			printk(KERN_ERR LOGNAME
				": error creating new Midi device\n");
			return err;
		}
		mpu = chip->rmidi->private_data;

		mpu->open_input = snd_msnd_mpu401_open;
		mpu->close_input = snd_msnd_mpu401_close;
		mpu->private_data = chip;
	}

	disable_irq(chip->irq);
	snd_msnd_calibrate_adc(chip, chip->play_sample_rate);
	snd_msndmix_force_recsrc(chip, 0);

	err = snd_card_register(card);
	if (err < 0)
		return err;

	return 0;
}


#ifndef MSND_CLASSIC

/* Pinnacle/Fiji Logical Device Configuration */

static int snd_msnd_write_cfg(int cfg, int reg, int value)
{
	outb(reg, cfg);
	outb(value, cfg + 1);
	if (value != inb(cfg + 1)) {
		printk(KERN_ERR LOGNAME ": snd_msnd_write_cfg: I/O error\n");
		return -EIO;
	}
	return 0;
}

static int snd_msnd_write_cfg_io0(int cfg, int num, u16 io)
{
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IO0_BASEHI, HIBYTE(io)))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IO0_BASELO, LOBYTE(io)))
		return -EIO;
	return 0;
}

static int snd_msnd_write_cfg_io1(int cfg, int num, u16 io)
{
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IO1_BASEHI, HIBYTE(io)))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IO1_BASELO, LOBYTE(io)))
		return -EIO;
	return 0;
}

static int snd_msnd_write_cfg_irq(int cfg, int num, u16 irq)
{
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IRQ_NUMBER, LOBYTE(irq)))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_IRQ_TYPE, IRQTYPE_EDGE))
		return -EIO;
	return 0;
}

static int snd_msnd_write_cfg_mem(int cfg, int num, int mem)
{
	u16 wmem;

	mem >>= 8;
	wmem = (u16)(mem & 0xfff);
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_MEMBASEHI, HIBYTE(wmem)))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_MEMBASELO, LOBYTE(wmem)))
		return -EIO;
	if (wmem && snd_msnd_write_cfg(cfg, IREG_MEMCONTROL,
				       MEMTYPE_HIADDR | MEMTYPE_16BIT))
		return -EIO;
	return 0;
}

static int snd_msnd_activate_logical(int cfg, int num)
{
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg(cfg, IREG_ACTIVATE, LD_ACTIVATE))
		return -EIO;
	return 0;
}

static int snd_msnd_write_cfg_logical(int cfg, int num, u16 io0,
				      u16 io1, u16 irq, int mem)
{
	if (snd_msnd_write_cfg(cfg, IREG_LOGDEVICE, num))
		return -EIO;
	if (snd_msnd_write_cfg_io0(cfg, num, io0))
		return -EIO;
	if (snd_msnd_write_cfg_io1(cfg, num, io1))
		return -EIO;
	if (snd_msnd_write_cfg_irq(cfg, num, irq))
		return -EIO;
	if (snd_msnd_write_cfg_mem(cfg, num, mem))
		return -EIO;
	if (snd_msnd_activate_logical(cfg, num))
		return -EIO;
	return 0;
}

static int snd_msnd_pinnacle_cfg_reset(int cfg)
{
	int i;

	/* Reset devices if told to */
	printk(KERN_INFO LOGNAME ": Resetting all devices\n");
	for (i = 0; i < 4; ++i)
		if (snd_msnd_write_cfg_logical(cfg, i, 0, 0, 0, 0))
			return -EIO;

	return 0;
}
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for msnd_pinnacle soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for msnd_pinnacle soundcard.");

static long io[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static long mem[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;

#ifndef MSND_CLASSIC
static long cfg[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;

/* Extra Peripheral Configuration (Default: Disable) */
static long ide_io0[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static long ide_io1[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int ide_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;

static long joystick_io[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
/* If we have the digital daugherboard... */
static int digital[SNDRV_CARDS];

/* Extra Peripheral Configuration */
static int reset[SNDRV_CARDS];
#endif

static int write_ndelay[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 1 };

static int calibrate_signal;

#ifdef CONFIG_PNP
static bool isapnp[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "ISA PnP detection for specified soundcard.");
#define has_isapnp(x) isapnp[x]
#else
#define has_isapnp(x) 0
#endif

MODULE_AUTHOR("Karsten Wiese <annabellesgarden@yahoo.de>");
MODULE_DESCRIPTION("Turtle Beach " LONGNAME " Linux Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(INITCODEFILE);
MODULE_FIRMWARE(PERMCODEFILE);

module_param_hw_array(io, long, ioport, NULL, 0444);
MODULE_PARM_DESC(io, "IO port #");
module_param_hw_array(irq, int, irq, NULL, 0444);
module_param_hw_array(mem, long, iomem, NULL, 0444);
module_param_array(write_ndelay, int, NULL, 0444);
module_param(calibrate_signal, int, 0444);
#ifndef MSND_CLASSIC
module_param_array(digital, int, NULL, 0444);
module_param_hw_array(cfg, long, ioport, NULL, 0444);
module_param_array(reset, int, NULL, 0444);
module_param_hw_array(mpu_io, long, ioport, NULL, 0444);
module_param_hw_array(mpu_irq, int, irq, NULL, 0444);
module_param_hw_array(ide_io0, long, ioport, NULL, 0444);
module_param_hw_array(ide_io1, long, ioport, NULL, 0444);
module_param_hw_array(ide_irq, int, irq, NULL, 0444);
module_param_hw_array(joystick_io, long, ioport, NULL, 0444);
#endif


static int snd_msnd_isa_match(struct device *pdev, unsigned int i)
{
	if (io[i] == SNDRV_AUTO_PORT)
		return 0;

	if (irq[i] == SNDRV_AUTO_PORT || mem[i] == SNDRV_AUTO_PORT) {
		printk(KERN_WARNING LOGNAME ": io, irq and mem must be set\n");
		return 0;
	}

#ifdef MSND_CLASSIC
	if (!(io[i] == 0x290 ||
	      io[i] == 0x260 ||
	      io[i] == 0x250 ||
	      io[i] == 0x240 ||
	      io[i] == 0x230 ||
	      io[i] == 0x220 ||
	      io[i] == 0x210 ||
	      io[i] == 0x3e0)) {
		printk(KERN_ERR LOGNAME ": \"io\" - DSP I/O base must be set "
			" to 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x290, "
			"or 0x3E0\n");
		return 0;
	}
#else
	if (io[i] < 0x100 || io[i] > 0x3e0 || (io[i] % 0x10) != 0) {
		printk(KERN_ERR LOGNAME
			": \"io\" - DSP I/O base must within the range 0x100 "
			"to 0x3E0 and must be evenly divisible by 0x10\n");
		return 0;
	}
#endif /* MSND_CLASSIC */

	if (!(irq[i] == 5 ||
	      irq[i] == 7 ||
	      irq[i] == 9 ||
	      irq[i] == 10 ||
	      irq[i] == 11 ||
	      irq[i] == 12)) {
		printk(KERN_ERR LOGNAME
			": \"irq\" - must be set to 5, 7, 9, 10, 11 or 12\n");
		return 0;
	}

	if (!(mem[i] == 0xb0000 ||
	      mem[i] == 0xc8000 ||
	      mem[i] == 0xd0000 ||
	      mem[i] == 0xd8000 ||
	      mem[i] == 0xe0000 ||
	      mem[i] == 0xe8000)) {
		printk(KERN_ERR LOGNAME ": \"mem\" - must be set to "
		       "0xb0000, 0xc8000, 0xd0000, 0xd8000, 0xe0000 or "
		       "0xe8000\n");
		return 0;
	}

#ifndef MSND_CLASSIC
	if (cfg[i] == SNDRV_AUTO_PORT) {
		printk(KERN_INFO LOGNAME ": Assuming PnP mode\n");
	} else if (cfg[i] != 0x250 && cfg[i] != 0x260 && cfg[i] != 0x270) {
		printk(KERN_INFO LOGNAME
			": Config port must be 0x250, 0x260 or 0x270 "
			"(or unspecified for PnP mode)\n");
		return 0;
	}
#endif /* MSND_CLASSIC */

	return 1;
}

static int snd_msnd_isa_probe(struct device *pdev, unsigned int idx)
{
	int err;
	struct snd_card *card;
	struct snd_msnd *chip;

	if (has_isapnp(idx)
#ifndef MSND_CLASSIC
	    || cfg[idx] == SNDRV_AUTO_PORT
#endif
	    ) {
		printk(KERN_INFO LOGNAME ": Assuming PnP mode\n");
		return -ENODEV;
	}

	err = snd_devm_card_new(pdev, index[idx], id[idx], THIS_MODULE,
				sizeof(struct snd_msnd), &card);
	if (err < 0)
		return err;

	chip = card->private_data;
	chip->card = card;

#ifdef MSND_CLASSIC
	switch (irq[idx]) {
	case 5:
		chip->irqid = HPIRQ_5; break;
	case 7:
		chip->irqid = HPIRQ_7; break;
	case 9:
		chip->irqid = HPIRQ_9; break;
	case 10:
		chip->irqid = HPIRQ_10; break;
	case 11:
		chip->irqid = HPIRQ_11; break;
	case 12:
		chip->irqid = HPIRQ_12; break;
	}

	switch (mem[idx]) {
	case 0xb0000:
		chip->memid = HPMEM_B000; break;
	case 0xc8000:
		chip->memid = HPMEM_C800; break;
	case 0xd0000:
		chip->memid = HPMEM_D000; break;
	case 0xd8000:
		chip->memid = HPMEM_D800; break;
	case 0xe0000:
		chip->memid = HPMEM_E000; break;
	case 0xe8000:
		chip->memid = HPMEM_E800; break;
	}
#else
	printk(KERN_INFO LOGNAME ": Non-PnP mode: configuring at port 0x%lx\n",
			cfg[idx]);

	if (!devm_request_region(card->dev, cfg[idx], 2,
				 "Pinnacle/Fiji Config")) {
		printk(KERN_ERR LOGNAME ": Config port 0x%lx conflict\n",
			   cfg[idx]);
		return -EIO;
	}
	if (reset[idx])
		if (snd_msnd_pinnacle_cfg_reset(cfg[idx]))
			return -EIO;

	/* DSP */
	err = snd_msnd_write_cfg_logical(cfg[idx], 0,
					 io[idx], 0,
					 irq[idx], mem[idx]);

	if (err)
		return err;

	/* The following are Pinnacle specific */

	/* MPU */
	if (mpu_io[idx] != SNDRV_AUTO_PORT
	    && mpu_irq[idx] != SNDRV_AUTO_IRQ) {
		printk(KERN_INFO LOGNAME
		       ": Configuring MPU to I/O 0x%lx IRQ %d\n",
		       mpu_io[idx], mpu_irq[idx]);
		err = snd_msnd_write_cfg_logical(cfg[idx], 1,
						 mpu_io[idx], 0,
						 mpu_irq[idx], 0);

		if (err)
			return err;
	}

	/* IDE */
	if (ide_io0[idx] != SNDRV_AUTO_PORT
	    && ide_io1[idx] != SNDRV_AUTO_PORT
	    && ide_irq[idx] != SNDRV_AUTO_IRQ) {
		printk(KERN_INFO LOGNAME
		       ": Configuring IDE to I/O 0x%lx, 0x%lx IRQ %d\n",
		       ide_io0[idx], ide_io1[idx], ide_irq[idx]);
		err = snd_msnd_write_cfg_logical(cfg[idx], 2,
						 ide_io0[idx], ide_io1[idx],
						 ide_irq[idx], 0);

		if (err)
			return err;
	}

	/* Joystick */
	if (joystick_io[idx] != SNDRV_AUTO_PORT) {
		printk(KERN_INFO LOGNAME
		       ": Configuring joystick to I/O 0x%lx\n",
		       joystick_io[idx]);
		err = snd_msnd_write_cfg_logical(cfg[idx], 3,
						 joystick_io[idx], 0,
						 0, 0);

		if (err)
			return err;
	}

#endif /* MSND_CLASSIC */

	set_default_audio_parameters(chip);
#ifdef MSND_CLASSIC
	chip->type = msndClassic;
#else
	chip->type = msndPinnacle;
#endif
	chip->io = io[idx];
	chip->irq = irq[idx];
	chip->base = mem[idx];

	chip->calibrate_signal = calibrate_signal ? 1 : 0;
	chip->recsrc = 0;
	chip->dspq_data_buff = DSPQ_DATA_BUFF;
	chip->dspq_buff_size = DSPQ_BUFF_SIZE;
	if (write_ndelay[idx])
		clear_bit(F_DISABLE_WRITE_NDELAY, &chip->flags);
	else
		set_bit(F_DISABLE_WRITE_NDELAY, &chip->flags);
#ifndef MSND_CLASSIC
	if (digital[idx])
		set_bit(F_HAVEDIGITAL, &chip->flags);
#endif
	spin_lock_init(&chip->lock);
	err = snd_msnd_probe(card);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": Probe failed\n");
		return err;
	}

	err = snd_msnd_attach(card);
	if (err < 0) {
		printk(KERN_ERR LOGNAME ": Attach failed\n");
		return err;
	}
	dev_set_drvdata(pdev, card);

	return 0;
}

static struct isa_driver snd_msnd_driver = {
	.match		= snd_msnd_isa_match,
	.probe		= snd_msnd_isa_probe,
	/* FIXME: suspend, resume */
	.driver		= {
		.name	= DEV_NAME
	},
};

#ifdef CONFIG_PNP
static int snd_msnd_pnp_detect(struct pnp_card_link *pcard,
			       const struct pnp_card_device_id *pid)
{
	static int idx;
	struct pnp_dev *pnp_dev;
	struct pnp_dev *mpu_dev;
	struct snd_card *card;
	struct snd_msnd *chip;
	int ret;

	for ( ; idx < SNDRV_CARDS; idx++) {
		if (has_isapnp(idx))
			break;
	}
	if (idx >= SNDRV_CARDS)
		return -ENODEV;

	/*
	 * Check that we still have room for another sound card ...
	 */
	pnp_dev = pnp_request_card_device(pcard, pid->devs[0].id, NULL);
	if (!pnp_dev)
		return -ENODEV;

	mpu_dev = pnp_request_card_device(pcard, pid->devs[1].id, NULL);
	if (!mpu_dev)
		return -ENODEV;

	if (!pnp_is_active(pnp_dev) && pnp_activate_dev(pnp_dev) < 0) {
		printk(KERN_INFO "msnd_pinnacle: device is inactive\n");
		return -EBUSY;
	}

	if (!pnp_is_active(mpu_dev) && pnp_activate_dev(mpu_dev) < 0) {
		printk(KERN_INFO "msnd_pinnacle: MPU device is inactive\n");
		return -EBUSY;
	}

	/*
	 * Create a new ALSA sound card entry, in anticipation
	 * of detecting our hardware ...
	 */
	ret = snd_devm_card_new(&pcard->card->dev,
				index[idx], id[idx], THIS_MODULE,
				sizeof(struct snd_msnd), &card);
	if (ret < 0)
		return ret;

	chip = card->private_data;
	chip->card = card;

	/*
	 * Read the correct parameters off the ISA PnP bus ...
	 */
	io[idx] = pnp_port_start(pnp_dev, 0);
	irq[idx] = pnp_irq(pnp_dev, 0);
	mem[idx] = pnp_mem_start(pnp_dev, 0);
	mpu_io[idx] = pnp_port_start(mpu_dev, 0);
	mpu_irq[idx] = pnp_irq(mpu_dev, 0);

	set_default_audio_parameters(chip);
#ifdef MSND_CLASSIC
	chip->type = msndClassic;
#else
	chip->type = msndPinnacle;
#endif
	chip->io = io[idx];
	chip->irq = irq[idx];
	chip->base = mem[idx];

	chip->calibrate_signal = calibrate_signal ? 1 : 0;
	chip->recsrc = 0;
	chip->dspq_data_buff = DSPQ_DATA_BUFF;
	chip->dspq_buff_size = DSPQ_BUFF_SIZE;
	if (write_ndelay[idx])
		clear_bit(F_DISABLE_WRITE_NDELAY, &chip->flags);
	else
		set_bit(F_DISABLE_WRITE_NDELAY, &chip->flags);
#ifndef MSND_CLASSIC
	if (digital[idx])
		set_bit(F_HAVEDIGITAL, &chip->flags);
#endif
	spin_lock_init(&chip->lock);
	ret = snd_msnd_probe(card);
	if (ret < 0) {
		printk(KERN_ERR LOGNAME ": Probe failed\n");
		return ret;
	}

	ret = snd_msnd_attach(card);
	if (ret < 0) {
		printk(KERN_ERR LOGNAME ": Attach failed\n");
		return ret;
	}

	pnp_set_card_drvdata(pcard, card);
	++idx;
	return 0;
}

static int isa_registered;
static int pnp_registered;

static const struct pnp_card_device_id msnd_pnpids[] = {
	/* Pinnacle PnP */
	{ .id = "BVJ0440", .devs = { { "TBS0000" }, { "TBS0001" } } },
	{ .id = "" }	/* end */
};

MODULE_DEVICE_TABLE(pnp_card, msnd_pnpids);

static struct pnp_card_driver msnd_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DO_NOT_CHANGE,
	.name = "msnd_pinnacle",
	.id_table = msnd_pnpids,
	.probe = snd_msnd_pnp_detect,
};
#endif /* CONFIG_PNP */

static int __init snd_msnd_init(void)
{
	int err;

	err = isa_register_driver(&snd_msnd_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;

	err = pnp_register_card_driver(&msnd_pnpc_driver);
	if (!err)
		pnp_registered = 1;

	if (isa_registered)
		err = 0;
#endif
	return err;
}

static void __exit snd_msnd_exit(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_card_driver(&msnd_pnpc_driver);
	if (isa_registered)
#endif
		isa_unregister_driver(&snd_msnd_driver);
}

module_init(snd_msnd_init);
module_exit(snd_msnd_exit);

