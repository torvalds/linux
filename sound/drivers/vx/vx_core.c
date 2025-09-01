// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VX soundcards
 *
 * Hardware core part
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/asoundef.h>
#include <sound/info.h>
#include <sound/vx_core.h>
#include "vx_cmd.h"

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Common routines for Digigram VX drivers");
MODULE_LICENSE("GPL");


/*
 * vx_check_reg_bit - wait for the specified bit is set/reset on a register
 * @reg: register to check
 * @mask: bit mask
 * @bit: resultant bit to be checked
 * @time: time-out of loop in msec
 *
 * returns zero if a bit matches, or a negative error code.
 */
int snd_vx_check_reg_bit(struct vx_core *chip, int reg, int mask, int bit, int time)
{
	unsigned long end_time = jiffies + (time * HZ + 999) / 1000;
	static const char * const reg_names[VX_REG_MAX] = {
		"ICR", "CVR", "ISR", "IVR", "RXH", "RXM", "RXL",
		"DMA", "CDSP", "RFREQ", "RUER/V2", "DATA", "MEMIRQ",
		"ACQ", "BIT0", "BIT1", "MIC0", "MIC1", "MIC2",
		"MIC3", "INTCSR", "CNTRL", "GPIOC",
		"LOFREQ", "HIFREQ", "CSUER", "RUER"
	};

	do {
		if ((snd_vx_inb(chip, reg) & mask) == bit)
			return 0;
		//msleep(10);
	} while (time_after_eq(end_time, jiffies));
	dev_dbg(chip->card->dev,
		"vx_check_reg_bit: timeout, reg=%s, mask=0x%x, val=0x%x\n",
		reg_names[reg], mask, snd_vx_inb(chip, reg));
	return -EIO;
}

EXPORT_SYMBOL(snd_vx_check_reg_bit);

/*
 * vx_send_irq_dsp - set command irq bit
 * @num: the requested IRQ type, IRQ_XXX
 *
 * this triggers the specified IRQ request
 * returns 0 if successful, or a negative error code.
 * 
 */
static int vx_send_irq_dsp(struct vx_core *chip, int num)
{
	int nirq;

	/* wait for Hc = 0 */
	if (snd_vx_check_reg_bit(chip, VX_CVR, CVR_HC, 0, 200) < 0)
		return -EIO;

	nirq = num;
	if (vx_has_new_dsp(chip))
		nirq += VXP_IRQ_OFFSET;
	vx_outb(chip, CVR, (nirq >> 1) | CVR_HC);
	return 0;
}


/*
 * vx_reset_chk - reset CHK bit on ISR
 *
 * returns 0 if successful, or a negative error code.
 */
static int vx_reset_chk(struct vx_core *chip)
{
	/* Reset irq CHK */
	if (vx_send_irq_dsp(chip, IRQ_RESET_CHK) < 0)
		return -EIO;
	/* Wait until CHK = 0 */
	if (vx_check_isr(chip, ISR_CHK, 0, 200) < 0)
		return -EIO;
	return 0;
}

/*
 * vx_transfer_end - terminate message transfer
 * @cmd: IRQ message to send (IRQ_MESS_XXX_END)
 *
 * returns 0 if successful, or a negative error code.
 * the error code can be VX-specific, retrieved via vx_get_error().
 * NB: call with mutex held!
 */
static int vx_transfer_end(struct vx_core *chip, int cmd)
{
	int err;

	err = vx_reset_chk(chip);
	if (err < 0)
		return err;

	/* irq MESS_READ/WRITE_END */
	err = vx_send_irq_dsp(chip, cmd);
	if (err < 0)
		return err;

	/* Wait CHK = 1 */
	err = vx_wait_isr_bit(chip, ISR_CHK);
	if (err < 0)
		return err;

	/* If error, Read RX */
	err = vx_inb(chip, ISR);
	if (err & ISR_ERR) {
		err = vx_wait_for_rx_full(chip);
		if (err < 0) {
			dev_dbg(chip->card->dev,
				"transfer_end: error in rx_full\n");
			return err;
		}
		err = vx_inb(chip, RXH) << 16;
		err |= vx_inb(chip, RXM) << 8;
		err |= vx_inb(chip, RXL);
		dev_dbg(chip->card->dev, "transfer_end: error = 0x%x\n", err);
		return -(VX_ERR_MASK | err);
	}
	return 0;
}

/*
 * vx_read_status - return the status rmh
 * @rmh: rmh record to store the status
 *
 * returns 0 if successful, or a negative error code.
 * the error code can be VX-specific, retrieved via vx_get_error().
 * NB: call with mutex held!
 */
static int vx_read_status(struct vx_core *chip, struct vx_rmh *rmh)
{
	int i, err, val, size;

	/* no read necessary? */
	if (rmh->DspStat == RMH_SSIZE_FIXED && rmh->LgStat == 0)
		return 0;

	/* Wait for RX full (with timeout protection)
	 * The first word of status is in RX
	 */
	err = vx_wait_for_rx_full(chip);
	if (err < 0)
		return err;

	/* Read RX */
	val = vx_inb(chip, RXH) << 16;
	val |= vx_inb(chip, RXM) << 8;
	val |= vx_inb(chip, RXL);

	/* If status given by DSP, let's decode its size */
	switch (rmh->DspStat) {
	case RMH_SSIZE_ARG:
		size = val & 0xff;
		rmh->Stat[0] = val & 0xffff00;
		rmh->LgStat = size + 1;
		break;
	case RMH_SSIZE_MASK:
		/* Let's count the arg numbers from a mask */
		rmh->Stat[0] = val;
		size = 0;
		while (val) {
			if (val & 0x01)
				size++;
			val >>= 1;
		}
		rmh->LgStat = size + 1;
		break;
	default:
		/* else retrieve the status length given by the driver */
		size = rmh->LgStat;
		rmh->Stat[0] = val;  /* Val is the status 1st word */
		size--;              /* hence adjust remaining length */
		break;
        }

	if (size < 1)
		return 0;
	if (snd_BUG_ON(size >= SIZE_MAX_STATUS))
		return -EINVAL;

	for (i = 1; i <= size; i++) {
		/* trigger an irq MESS_WRITE_NEXT */
		err = vx_send_irq_dsp(chip, IRQ_MESS_WRITE_NEXT);
		if (err < 0)
			return err;
		/* Wait for RX full (with timeout protection) */
		err = vx_wait_for_rx_full(chip);
		if (err < 0)
			return err;
		rmh->Stat[i] = vx_inb(chip, RXH) << 16;
		rmh->Stat[i] |= vx_inb(chip, RXM) <<  8;
		rmh->Stat[i] |= vx_inb(chip, RXL);
	}

	return vx_transfer_end(chip, IRQ_MESS_WRITE_END);
}


#define MASK_MORE_THAN_1_WORD_COMMAND   0x00008000
#define MASK_1_WORD_COMMAND             0x00ff7fff

/*
 * vx_send_msg_nolock - send a DSP message and read back the status
 * @rmh: the rmh record to send and receive
 *
 * returns 0 if successful, or a negative error code.
 * the error code can be VX-specific, retrieved via vx_get_error().
 * 
 * this function doesn't call mutex lock at all.
 */
int vx_send_msg_nolock(struct vx_core *chip, struct vx_rmh *rmh)
{
	int i, err;
	
	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	err = vx_reset_chk(chip);
	if (err < 0) {
		dev_dbg(chip->card->dev, "vx_send_msg: vx_reset_chk error\n");
		return err;
	}

	/* Check bit M is set according to length of the command */
	if (rmh->LgCmd > 1)
		rmh->Cmd[0] |= MASK_MORE_THAN_1_WORD_COMMAND;
	else
		rmh->Cmd[0] &= MASK_1_WORD_COMMAND;

	/* Wait for TX empty */
	err = vx_wait_isr_bit(chip, ISR_TX_EMPTY);
	if (err < 0) {
		dev_dbg(chip->card->dev, "vx_send_msg: wait tx empty error\n");
		return err;
	}

	/* Write Cmd[0] */
	vx_outb(chip, TXH, (rmh->Cmd[0] >> 16) & 0xff);
	vx_outb(chip, TXM, (rmh->Cmd[0] >> 8) & 0xff);
	vx_outb(chip, TXL, rmh->Cmd[0] & 0xff);

	/* Trigger irq MESSAGE */
	err = vx_send_irq_dsp(chip, IRQ_MESSAGE);
	if (err < 0) {
		dev_dbg(chip->card->dev,
			"vx_send_msg: send IRQ_MESSAGE error\n");
		return err;
	}

	/* Wait for CHK = 1 */
	err = vx_wait_isr_bit(chip, ISR_CHK);
	if (err < 0)
		return err;

	/* If error, get error value from RX */
	if (vx_inb(chip, ISR) & ISR_ERR) {
		err = vx_wait_for_rx_full(chip);
		if (err < 0) {
			dev_dbg(chip->card->dev,
				"vx_send_msg: rx_full read error\n");
			return err;
		}
		err = vx_inb(chip, RXH) << 16;
		err |= vx_inb(chip, RXM) << 8;
		err |= vx_inb(chip, RXL);
		dev_dbg(chip->card->dev,
			"msg got error = 0x%x at cmd[0]\n", err);
		err = -(VX_ERR_MASK | err);
		return err;
	}

	/* Send the other words */
	if (rmh->LgCmd > 1) {
		for (i = 1; i < rmh->LgCmd; i++) {
			/* Wait for TX ready */
			err = vx_wait_isr_bit(chip, ISR_TX_READY);
			if (err < 0) {
				dev_dbg(chip->card->dev,
					"vx_send_msg: tx_ready error\n");
				return err;
			}

			/* Write Cmd[i] */
			vx_outb(chip, TXH, (rmh->Cmd[i] >> 16) & 0xff);
			vx_outb(chip, TXM, (rmh->Cmd[i] >> 8) & 0xff);
			vx_outb(chip, TXL, rmh->Cmd[i] & 0xff);

			/* Trigger irq MESS_READ_NEXT */
			err = vx_send_irq_dsp(chip, IRQ_MESS_READ_NEXT);
			if (err < 0) {
				dev_dbg(chip->card->dev,
					"vx_send_msg: IRQ_READ_NEXT error\n");
				return err;
			}
		}
		/* Wait for TX empty */
		err = vx_wait_isr_bit(chip, ISR_TX_READY);
		if (err < 0) {
			dev_dbg(chip->card->dev,
				"vx_send_msg: TX_READY error\n");
			return err;
		}
		/* End of transfer */
		err = vx_transfer_end(chip, IRQ_MESS_READ_END);
		if (err < 0)
			return err;
	}

	return vx_read_status(chip, rmh);
}


/*
 * vx_send_msg - send a DSP message with mutex
 * @rmh: the rmh record to send and receive
 *
 * returns 0 if successful, or a negative error code.
 * see vx_send_msg_nolock().
 */
int vx_send_msg(struct vx_core *chip, struct vx_rmh *rmh)
{
	guard(mutex)(&chip->lock);
	return vx_send_msg_nolock(chip, rmh);
}


/*
 * vx_send_rih_nolock - send an RIH to xilinx
 * @cmd: the command to send
 *
 * returns 0 if successful, or a negative error code.
 * the error code can be VX-specific, retrieved via vx_get_error().
 *
 * this function doesn't call mutex at all.
 *
 * unlike RMH, no command is sent to DSP.
 */
int vx_send_rih_nolock(struct vx_core *chip, int cmd)
{
	int err;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	err = vx_reset_chk(chip);
	if (err < 0)
		return err;
	/* send the IRQ */
	err = vx_send_irq_dsp(chip, cmd);
	if (err < 0)
		return err;
	/* Wait CHK = 1 */
	err = vx_wait_isr_bit(chip, ISR_CHK);
	if (err < 0)
		return err;
	/* If error, read RX */
	if (vx_inb(chip, ISR) & ISR_ERR) {
		err = vx_wait_for_rx_full(chip);
		if (err < 0)
			return err;
		err = vx_inb(chip, RXH) << 16;
		err |= vx_inb(chip, RXM) << 8;
		err |= vx_inb(chip, RXL);
		return -(VX_ERR_MASK | err);
	}
	return 0;
}


/*
 * vx_send_rih - send an RIH with mutex
 * @cmd: the command to send
 *
 * see vx_send_rih_nolock().
 */
int vx_send_rih(struct vx_core *chip, int cmd)
{
	guard(mutex)(&chip->lock);
	return vx_send_rih_nolock(chip, cmd);
}

#define END_OF_RESET_WAIT_TIME		500	/* us */

/**
 * snd_vx_load_boot_image - boot up the xilinx interface
 * @chip: VX core instance
 * @boot: the boot record to load
 */
int snd_vx_load_boot_image(struct vx_core *chip, const struct firmware *boot)
{
	unsigned int i;
	int no_fillup = vx_has_new_dsp(chip);

	/* check the length of boot image */
	if (boot->size <= 0)
		return -EINVAL;
	if (boot->size % 3)
		return -EINVAL;
#if 0
	{
		/* more strict check */
		unsigned int c = ((u32)boot->data[0] << 16) | ((u32)boot->data[1] << 8) | boot->data[2];
		if (boot->size != (c + 2) * 3)
			return -EINVAL;
	}
#endif

	/* reset dsp */
	vx_reset_dsp(chip);
	
	udelay(END_OF_RESET_WAIT_TIME); /* another wait? */

	/* download boot strap */
	for (i = 0; i < 0x600; i += 3) {
		if (i >= boot->size) {
			if (no_fillup)
				break;
			if (vx_wait_isr_bit(chip, ISR_TX_EMPTY) < 0) {
				dev_err(chip->card->dev, "dsp boot failed at %d\n", i);
				return -EIO;
			}
			vx_outb(chip, TXH, 0);
			vx_outb(chip, TXM, 0);
			vx_outb(chip, TXL, 0);
		} else {
			const unsigned char *image = boot->data + i;
			if (vx_wait_isr_bit(chip, ISR_TX_EMPTY) < 0) {
				dev_err(chip->card->dev, "dsp boot failed at %d\n", i);
				return -EIO;
			}
			vx_outb(chip, TXH, image[0]);
			vx_outb(chip, TXM, image[1]);
			vx_outb(chip, TXL, image[2]);
		}
	}
	return 0;
}

EXPORT_SYMBOL(snd_vx_load_boot_image);

/*
 * vx_test_irq_src - query the source of interrupts
 *
 * called from irq handler only
 */
static int vx_test_irq_src(struct vx_core *chip, unsigned int *ret)
{
	int err;

	vx_init_rmh(&chip->irq_rmh, CMD_TEST_IT);
	guard(mutex)(&chip->lock);
	err = vx_send_msg_nolock(chip, &chip->irq_rmh);
	if (err < 0)
		*ret = 0;
	else
		*ret = chip->irq_rmh.Stat[0];
	return err;
}


/*
 * snd_vx_threaded_irq_handler - threaded irq handler
 */
irqreturn_t snd_vx_threaded_irq_handler(int irq, void *dev)
{
	struct vx_core *chip = dev;
	unsigned int events;
		
	if (chip->chip_status & VX_STAT_IS_STALE)
		return IRQ_HANDLED;

	if (vx_test_irq_src(chip, &events) < 0)
		return IRQ_HANDLED;
    
	/* We must prevent any application using this DSP
	 * and block any further request until the application
	 * either unregisters or reloads the DSP
	 */
	if (events & FATAL_DSP_ERROR) {
		dev_err(chip->card->dev, "vx_core: fatal DSP error!!\n");
		return IRQ_HANDLED;
	}

	/* The start on time code conditions are filled (ie the time code
	 * received by the board is equal to one of those given to it).
	 */
	if (events & TIME_CODE_EVENT_PENDING) {
		; /* so far, nothing to do yet */
	}

	/* The frequency has changed on the board (UER mode). */
	if (events & FREQUENCY_CHANGE_EVENT_PENDING)
		vx_change_frequency(chip);

	/* update the pcm streams */
	vx_pcm_update_intr(chip, events);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(snd_vx_threaded_irq_handler);

/**
 * snd_vx_irq_handler - interrupt handler
 * @irq: irq number
 * @dev: VX core instance
 */
irqreturn_t snd_vx_irq_handler(int irq, void *dev)
{
	struct vx_core *chip = dev;

	if (! (chip->chip_status & VX_STAT_CHIP_INIT) ||
	    (chip->chip_status & VX_STAT_IS_STALE))
		return IRQ_NONE;
	if (! vx_test_and_ack(chip))
		return IRQ_WAKE_THREAD;
	return IRQ_NONE;
}

EXPORT_SYMBOL(snd_vx_irq_handler);

/*
 */
static void vx_reset_board(struct vx_core *chip, int cold_reset)
{
	if (snd_BUG_ON(!chip->ops->reset_board))
		return;

	/* current source, later sync'ed with target */
	chip->audio_source = VX_AUDIO_SRC_LINE;
	if (cold_reset) {
		chip->audio_source_target = chip->audio_source;
		chip->clock_source = INTERNAL_QUARTZ;
		chip->clock_mode = VX_CLOCK_MODE_AUTO;
		chip->freq = 48000;
		chip->uer_detected = VX_UER_MODE_NOT_PRESENT;
		chip->uer_bits = SNDRV_PCM_DEFAULT_CON_SPDIF;
	}

	chip->ops->reset_board(chip, cold_reset);

	vx_reset_codec(chip, cold_reset);

	vx_set_internal_clock(chip, chip->freq);

	/* Reset the DSP */
	vx_reset_dsp(chip);

	if (vx_is_pcmcia(chip)) {
		/* Acknowledge any pending IRQ and reset the MEMIRQ flag. */
		vx_test_and_ack(chip);
		vx_validate_irq(chip, 1);
	}

	/* init CBits */
	vx_set_iec958_status(chip, chip->uer_bits);
}


/*
 * proc interface
 */

static void vx_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct vx_core *chip = entry->private_data;
	static const char * const audio_src_vxp[] = { "Line", "Mic", "Digital" };
	static const char * const audio_src_vx2[] = { "Analog", "Analog", "Digital" };
	static const char * const clock_mode[] = { "Auto", "Internal", "External" };
	static const char * const clock_src[] = { "Internal", "External" };
	static const char * const uer_type[] = { "Consumer", "Professional", "Not Present" };
	
	snd_iprintf(buffer, "%s\n", chip->card->longname);
	snd_iprintf(buffer, "Xilinx Firmware: %s\n",
		    (chip->chip_status & VX_STAT_XILINX_LOADED) ? "Loaded" : "No");
	snd_iprintf(buffer, "Device Initialized: %s\n",
		    (chip->chip_status & VX_STAT_DEVICE_INIT) ? "Yes" : "No");
	snd_iprintf(buffer, "DSP audio info:");
	if (chip->audio_info & VX_AUDIO_INFO_REAL_TIME)
		snd_iprintf(buffer, " realtime");
	if (chip->audio_info & VX_AUDIO_INFO_OFFLINE)
		snd_iprintf(buffer, " offline");
	if (chip->audio_info & VX_AUDIO_INFO_MPEG1)
		snd_iprintf(buffer, " mpeg1");
	if (chip->audio_info & VX_AUDIO_INFO_MPEG2)
		snd_iprintf(buffer, " mpeg2");
	if (chip->audio_info & VX_AUDIO_INFO_LINEAR_8)
		snd_iprintf(buffer, " linear8");
	if (chip->audio_info & VX_AUDIO_INFO_LINEAR_16)
		snd_iprintf(buffer, " linear16");
	if (chip->audio_info & VX_AUDIO_INFO_LINEAR_24)
		snd_iprintf(buffer, " linear24");
	snd_iprintf(buffer, "\n");
	snd_iprintf(buffer, "Input Source: %s\n", vx_is_pcmcia(chip) ?
		    audio_src_vxp[chip->audio_source] :
		    audio_src_vx2[chip->audio_source]);
	snd_iprintf(buffer, "Clock Mode: %s\n", clock_mode[chip->clock_mode]);
	snd_iprintf(buffer, "Clock Source: %s\n", clock_src[chip->clock_source]);
	snd_iprintf(buffer, "Frequency: %d\n", chip->freq);
	snd_iprintf(buffer, "Detected Frequency: %d\n", chip->freq_detected);
	snd_iprintf(buffer, "Detected UER type: %s\n", uer_type[chip->uer_detected]);
	snd_iprintf(buffer, "Min/Max/Cur IBL: %d/%d/%d (granularity=%d)\n",
		    chip->ibl.min_size, chip->ibl.max_size, chip->ibl.size,
		    chip->ibl.granularity);
}

static void vx_proc_init(struct vx_core *chip)
{
	snd_card_ro_proc_new(chip->card, "vx-status", chip, vx_proc_read);
}


/**
 * snd_vx_dsp_boot - load the DSP boot
 * @chip: VX core instance
 * @boot: firmware data
 */
int snd_vx_dsp_boot(struct vx_core *chip, const struct firmware *boot)
{
	int err;
	int cold_reset = !(chip->chip_status & VX_STAT_DEVICE_INIT);

	vx_reset_board(chip, cold_reset);
	vx_validate_irq(chip, 0);

	err = snd_vx_load_boot_image(chip, boot);
	if (err < 0)
		return err;
	msleep(10);

	return 0;
}

EXPORT_SYMBOL(snd_vx_dsp_boot);

/**
 * snd_vx_dsp_load - load the DSP image
 * @chip: VX core instance
 * @dsp: firmware data
 */
int snd_vx_dsp_load(struct vx_core *chip, const struct firmware *dsp)
{
	unsigned int i;
	int err;
	unsigned int csum = 0;
	const unsigned char *image, *cptr;

	if (dsp->size % 3)
		return -EINVAL;

	vx_toggle_dac_mute(chip, 1);

	/* Transfert data buffer from PC to DSP */
	for (i = 0; i < dsp->size; i += 3) {
		image = dsp->data + i;
		/* Wait DSP ready for a new read */
		err = vx_wait_isr_bit(chip, ISR_TX_EMPTY);
		if (err < 0) {
			dev_err(chip->card->dev,
				"dsp loading error at position %d\n", i);
			return err;
		}
		cptr = image;
		csum ^= *cptr;
		csum = (csum >> 24) | (csum << 8);
		vx_outb(chip, TXH, *cptr++);
		csum ^= *cptr;
		csum = (csum >> 24) | (csum << 8);
		vx_outb(chip, TXM, *cptr++);
		csum ^= *cptr;
		csum = (csum >> 24) | (csum << 8);
		vx_outb(chip, TXL, *cptr++);
	}

	msleep(200);

	err = vx_wait_isr_bit(chip, ISR_CHK);
	if (err < 0)
		return err;

	vx_toggle_dac_mute(chip, 0);

	vx_test_and_ack(chip);
	vx_validate_irq(chip, 1);

	return 0;
}

EXPORT_SYMBOL(snd_vx_dsp_load);

#ifdef CONFIG_PM
/*
 * suspend
 */
int snd_vx_suspend(struct vx_core *chip)
{
	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D3hot);
	chip->chip_status |= VX_STAT_IN_SUSPEND;

	return 0;
}

EXPORT_SYMBOL(snd_vx_suspend);

/*
 * resume
 */
int snd_vx_resume(struct vx_core *chip)
{
	int i, err;

	chip->chip_status &= ~VX_STAT_CHIP_INIT;

	for (i = 0; i < 4; i++) {
		if (! chip->firmware[i])
			continue;
		err = chip->ops->load_dsp(chip, i, chip->firmware[i]);
		if (err < 0) {
			dev_err(chip->card->dev,
				"vx: firmware resume error at DSP %d\n", i);
			return -EIO;
		}
	}

	chip->chip_status |= VX_STAT_CHIP_INIT;
	chip->chip_status &= ~VX_STAT_IN_SUSPEND;

	snd_power_change_state(chip->card, SNDRV_CTL_POWER_D0);
	return 0;
}

EXPORT_SYMBOL(snd_vx_resume);
#endif

static void snd_vx_release(struct device *dev, void *data)
{
	snd_vx_free_firmware(data);
}

/**
 * snd_vx_create - constructor for struct vx_core
 * @card: card instance
 * @hw: hardware specific record
 * @ops: VX ops pointer
 * @extra_size: extra byte size to allocate appending to chip
 *
 * this function allocates the instance and prepare for the hardware
 * initialization.
 *
 * The object is managed via devres, and will be automatically released.
 *
 * return the instance pointer if successful, NULL in error.
 */
struct vx_core *snd_vx_create(struct snd_card *card,
			      const struct snd_vx_hardware *hw,
			      const struct snd_vx_ops *ops,
			      int extra_size)
{
	struct vx_core *chip;

	if (snd_BUG_ON(!card || !hw || !ops))
		return NULL;

	chip = devres_alloc(snd_vx_release, sizeof(*chip) + extra_size,
			    GFP_KERNEL);
	if (!chip)
		return NULL;
	mutex_init(&chip->lock);
	chip->irq = -1;
	chip->hw = hw;
	chip->type = hw->type;
	chip->ops = ops;
	mutex_init(&chip->mixer_mutex);

	chip->card = card;
	card->private_data = chip;
	strscpy(card->driver, hw->name);
	sprintf(card->shortname, "Digigram %s", hw->name);

	vx_proc_init(chip);

	return chip;
}

EXPORT_SYMBOL(snd_vx_create);
