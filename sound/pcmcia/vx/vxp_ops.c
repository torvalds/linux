/*
 * Driver for Digigram VXpocket soundcards
 *
 * lowlevel routines for VXpocket soundcards
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <asm/io.h>
#include "vxpocket.h"


static int vxp_reg_offset[VX_REG_MAX] = {
	[VX_ICR]	= 0x00,		// ICR
	[VX_CVR]	= 0x01,		// CVR
	[VX_ISR]	= 0x02,		// ISR
	[VX_IVR]	= 0x03,		// IVR
	[VX_RXH]	= 0x05,		// RXH
	[VX_RXM]	= 0x06,		// RXM
	[VX_RXL]	= 0x07,		// RXL
	[VX_DMA]	= 0x04,		// DMA
	[VX_CDSP]	= 0x08,		// CDSP
	[VX_LOFREQ]	= 0x09,		// LFREQ
	[VX_HIFREQ]	= 0x0a,		// HFREQ
	[VX_DATA]	= 0x0b,		// DATA
	[VX_MICRO]	= 0x0c,		// MICRO
	[VX_DIALOG]	= 0x0d,		// DIALOG
	[VX_CSUER]	= 0x0e,		// CSUER
	[VX_RUER]	= 0x0f,		// RUER
};


static inline unsigned long vxp_reg_addr(struct vx_core *_chip, int reg)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	return chip->port + vxp_reg_offset[reg];
}

/*
 * snd_vx_inb - read a byte from the register
 * @offset: register offset
 */
static unsigned char vxp_inb(struct vx_core *chip, int offset)
{
	return inb(vxp_reg_addr(chip, offset));
}

/*
 * snd_vx_outb - write a byte on the register
 * @offset: the register offset
 * @val: the value to write
 */
static void vxp_outb(struct vx_core *chip, int offset, unsigned char val)
{
	outb(val, vxp_reg_addr(chip, offset));
}

/*
 * redefine macros to call directly
 */
#undef vx_inb
#define vx_inb(chip,reg)	vxp_inb((struct vx_core *)(chip), VX_##reg)
#undef vx_outb
#define vx_outb(chip,reg,val)	vxp_outb((struct vx_core *)(chip), VX_##reg,val)


/*
 * vx_check_magic - check the magic word on xilinx
 *
 * returns zero if a magic word is detected, or a negative error code.
 */
static int vx_check_magic(struct vx_core *chip)
{
	unsigned long end_time = jiffies + HZ / 5;
	int c;
	do {
		c = vx_inb(chip, CDSP);
		if (c == CDSP_MAGIC)
			return 0;
		msleep(10);
	} while (time_after_eq(end_time, jiffies));
	snd_printk(KERN_ERR "cannot find xilinx magic word (%x)\n", c);
	return -EIO;
}


/*
 * vx_reset_dsp - reset the DSP
 */

#define XX_DSP_RESET_WAIT_TIME		2	/* ms */

static void vxp_reset_dsp(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* set the reset dsp bit to 1 */
	vx_outb(chip, CDSP, chip->regCDSP | VXP_CDSP_DSP_RESET_MASK);
	vx_inb(chip, CDSP);
	mdelay(XX_DSP_RESET_WAIT_TIME);
	/* reset the bit */
	chip->regCDSP &= ~VXP_CDSP_DSP_RESET_MASK;
	vx_outb(chip, CDSP, chip->regCDSP);
	vx_inb(chip, CDSP);
	mdelay(XX_DSP_RESET_WAIT_TIME);
}

/*
 * reset codec bit
 */
static void vxp_reset_codec(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* Set the reset CODEC bit to 1. */
	vx_outb(chip, CDSP, chip->regCDSP | VXP_CDSP_CODEC_RESET_MASK);
	vx_inb(chip, CDSP);
	msleep(10);
	/* Set the reset CODEC bit to 0. */
	chip->regCDSP &= ~VXP_CDSP_CODEC_RESET_MASK;
	vx_outb(chip, CDSP, chip->regCDSP);
	vx_inb(chip, CDSP);
	msleep(1);
}

/*
 * vx_load_xilinx_binary - load the xilinx binary image
 * the binary image is the binary array converted from the bitstream file.
 */
static int vxp_load_xilinx_binary(struct vx_core *_chip, const struct firmware *fw)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	unsigned int i;
	int c;
	int regCSUER, regRUER;
	unsigned char *image;
	unsigned char data;

	/* Switch to programmation mode */
	chip->regDIALOG |= VXP_DLG_XILINX_REPROG_MASK;
	vx_outb(chip, DIALOG, chip->regDIALOG);

	/* Save register CSUER and RUER */
	regCSUER = vx_inb(chip, CSUER);
	regRUER = vx_inb(chip, RUER);

	/* reset HF0 and HF1 */
	vx_outb(chip, ICR, 0);

	/* Wait for answer HF2 equal to 1 */
	snd_printdd(KERN_DEBUG "check ISR_HF2\n");
	if (vx_check_isr(_chip, ISR_HF2, ISR_HF2, 20) < 0)
		goto _error;

	/* set HF1 for loading xilinx binary */
	vx_outb(chip, ICR, ICR_HF1);
	image = fw->data;
	for (i = 0; i < fw->size; i++, image++) {
		data = *image;
		if (vx_wait_isr_bit(_chip, ISR_TX_EMPTY) < 0)
			goto _error;
		vx_outb(chip, TXL, data);
		/* wait for reading */
		if (vx_wait_for_rx_full(_chip) < 0)
			goto _error;
		c = vx_inb(chip, RXL);
		if (c != (int)data)
			snd_printk(KERN_ERR "vxpocket: load xilinx mismatch at %d: 0x%x != 0x%x\n", i, c, (int)data);
        }

	/* reset HF1 */
	vx_outb(chip, ICR, 0);

	/* wait for HF3 */
	if (vx_check_isr(_chip, ISR_HF3, ISR_HF3, 20) < 0)
		goto _error;

	/* read the number of bytes received */
	if (vx_wait_for_rx_full(_chip) < 0)
		goto _error;

	c = (int)vx_inb(chip, RXH) << 16;
	c |= (int)vx_inb(chip, RXM) << 8;
	c |= vx_inb(chip, RXL);

	snd_printdd(KERN_DEBUG "xilinx: dsp size received 0x%x, orig 0x%Zx\n", c, fw->size);

	vx_outb(chip, ICR, ICR_HF0);

	/* TEMPO 250ms : wait until Xilinx is downloaded */
	msleep(300);

	/* test magical word */
	if (vx_check_magic(_chip) < 0)
		goto _error;

	/* Restore register 0x0E and 0x0F (thus replacing COR and FCSR) */
	vx_outb(chip, CSUER, regCSUER);
	vx_outb(chip, RUER, regRUER);

	/* Reset the Xilinx's signal enabling IO access */
	chip->regDIALOG |= VXP_DLG_XILINX_REPROG_MASK;
	vx_outb(chip, DIALOG, chip->regDIALOG);
	vx_inb(chip, DIALOG);
	msleep(10);
	chip->regDIALOG &= ~VXP_DLG_XILINX_REPROG_MASK;
	vx_outb(chip, DIALOG, chip->regDIALOG);
	vx_inb(chip, DIALOG);

	/* Reset of the Codec */
	vxp_reset_codec(_chip);
	vx_reset_dsp(_chip);

	return 0;

 _error:
	vx_outb(chip, CSUER, regCSUER);
	vx_outb(chip, RUER, regRUER);
	chip->regDIALOG &= ~VXP_DLG_XILINX_REPROG_MASK;
	vx_outb(chip, DIALOG, chip->regDIALOG);
	return -EIO;
}


/*
 * vxp_load_dsp - load_dsp callback
 */
static int vxp_load_dsp(struct vx_core *vx, int index, const struct firmware *fw)
{
	int err;

	switch (index) {
	case 0:
		/* xilinx boot */
		if ((err = vx_check_magic(vx)) < 0)
			return err;
		if ((err = snd_vx_load_boot_image(vx, fw)) < 0)
			return err;
		return 0;
	case 1:
		/* xilinx image */
		return vxp_load_xilinx_binary(vx, fw);
	case 2:
		/* DSP boot */
		return snd_vx_dsp_boot(vx, fw);
	case 3:
		/* DSP image */
		return snd_vx_dsp_load(vx, fw);
	default:
		snd_BUG();
		return -EINVAL;
	}
}
		

/*
 * vx_test_and_ack - test and acknowledge interrupt
 *
 * called from irq hander, too
 *
 * spinlock held!
 */
static int vxp_test_and_ack(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* not booted yet? */
	if (! (_chip->chip_status & VX_STAT_XILINX_LOADED))
		return -ENXIO;

	if (! (vx_inb(chip, DIALOG) & VXP_DLG_MEMIRQ_MASK))
		return -EIO;
	
	/* ok, interrupts generated, now ack it */
	/* set ACQUIT bit up and down */
	vx_outb(chip, DIALOG, chip->regDIALOG | VXP_DLG_ACK_MEMIRQ_MASK);
	/* useless read just to spend some time and maintain
	 * the ACQUIT signal up for a while ( a bus cycle )
	 */
	vx_inb(chip, DIALOG);
	vx_outb(chip, DIALOG, chip->regDIALOG & ~VXP_DLG_ACK_MEMIRQ_MASK);

	return 0;
}


/*
 * vx_validate_irq - enable/disable IRQ
 */
static void vxp_validate_irq(struct vx_core *_chip, int enable)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* Set the interrupt enable bit to 1 in CDSP register */
	if (enable)
		chip->regCDSP |= VXP_CDSP_VALID_IRQ_MASK;
	else
		chip->regCDSP &= ~VXP_CDSP_VALID_IRQ_MASK;
	vx_outb(chip, CDSP, chip->regCDSP);
}

/*
 * vx_setup_pseudo_dma - set up the pseudo dma read/write mode.
 * @do_write: 0 = read, 1 = set up for DMA write
 */
static void vx_setup_pseudo_dma(struct vx_core *_chip, int do_write)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* Interrupt mode and HREQ pin enabled for host transmit / receive data transfers */
	vx_outb(chip, ICR, do_write ? ICR_TREQ : ICR_RREQ);
	/* Reset the pseudo-dma register */
	vx_inb(chip, ISR);
	vx_outb(chip, ISR, 0);

	/* Select DMA in read/write transfer mode and in 16-bit accesses */
	chip->regDIALOG |= VXP_DLG_DMA16_SEL_MASK;
	chip->regDIALOG |= do_write ? VXP_DLG_DMAWRITE_SEL_MASK : VXP_DLG_DMAREAD_SEL_MASK;
	vx_outb(chip, DIALOG, chip->regDIALOG);

}

/*
 * vx_release_pseudo_dma - disable the pseudo-DMA mode
 */
static void vx_release_pseudo_dma(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	/* Disable DMA and 16-bit accesses */
	chip->regDIALOG &= ~(VXP_DLG_DMAWRITE_SEL_MASK|
			     VXP_DLG_DMAREAD_SEL_MASK|
			     VXP_DLG_DMA16_SEL_MASK);
	vx_outb(chip, DIALOG, chip->regDIALOG);
	/* HREQ pin disabled. */
	vx_outb(chip, ICR, 0);
}

/*
 * vx_pseudo_dma_write - write bulk data on pseudo-DMA mode
 * @count: data length to transfer in bytes
 *
 * data size must be aligned to 6 bytes to ensure the 24bit alignment on DSP.
 * NB: call with a certain lock!
 */
static void vxp_dma_write(struct vx_core *chip, struct snd_pcm_runtime *runtime,
			  struct vx_pipe *pipe, int count)
{
	long port = vxp_reg_addr(chip, VX_DMA);
	int offset = pipe->hw_ptr;
	unsigned short *addr = (unsigned short *)(runtime->dma_area + offset);

	vx_setup_pseudo_dma(chip, 1);
	if (offset + count > pipe->buffer_bytes) {
		int length = pipe->buffer_bytes - offset;
		count -= length;
		length >>= 1; /* in 16bit words */
		/* Transfer using pseudo-dma. */
		while (length-- > 0) {
			outw(cpu_to_le16(*addr), port);
			addr++;
		}
		addr = (unsigned short *)runtime->dma_area;
		pipe->hw_ptr = 0;
	}
	pipe->hw_ptr += count;
	count >>= 1; /* in 16bit words */
	/* Transfer using pseudo-dma. */
	while (count-- > 0) {
		outw(cpu_to_le16(*addr), port);
		addr++;
	}
	vx_release_pseudo_dma(chip);
}


/*
 * vx_pseudo_dma_read - read bulk data on pseudo DMA mode
 * @offset: buffer offset in bytes
 * @count: data length to transfer in bytes
 *
 * the read length must be aligned to 6 bytes, as well as write.
 * NB: call with a certain lock!
 */
static void vxp_dma_read(struct vx_core *chip, struct snd_pcm_runtime *runtime,
			 struct vx_pipe *pipe, int count)
{
	struct snd_vxpocket *pchip = (struct snd_vxpocket *)chip;
	long port = vxp_reg_addr(chip, VX_DMA);
	int offset = pipe->hw_ptr;
	unsigned short *addr = (unsigned short *)(runtime->dma_area + offset);

	snd_assert(count % 2 == 0, return);
	vx_setup_pseudo_dma(chip, 0);
	if (offset + count > pipe->buffer_bytes) {
		int length = pipe->buffer_bytes - offset;
		count -= length;
		length >>= 1; /* in 16bit words */
		/* Transfer using pseudo-dma. */
		while (length-- > 0)
			*addr++ = le16_to_cpu(inw(port));
		addr = (unsigned short *)runtime->dma_area;
		pipe->hw_ptr = 0;
	}
	pipe->hw_ptr += count;
	count >>= 1; /* in 16bit words */
	/* Transfer using pseudo-dma. */
	while (count-- > 1)
		*addr++ = le16_to_cpu(inw(port));
	/* Disable DMA */
	pchip->regDIALOG &= ~VXP_DLG_DMAREAD_SEL_MASK;
	vx_outb(chip, DIALOG, pchip->regDIALOG);
	/* Read the last word (16 bits) */
	*addr = le16_to_cpu(inw(port));
	/* Disable 16-bit accesses */
	pchip->regDIALOG &= ~VXP_DLG_DMA16_SEL_MASK;
	vx_outb(chip, DIALOG, pchip->regDIALOG);
	/* HREQ pin disabled. */
	vx_outb(chip, ICR, 0);
}


/*
 * write a codec data (24bit)
 */
static void vxp_write_codec_reg(struct vx_core *chip, int codec, unsigned int data)
{
	int i;

	/* Activate access to the corresponding codec register */
	if (! codec)
		vx_inb(chip, LOFREQ);
	else
		vx_inb(chip, CODEC2);
		
	/* We have to send 24 bits (3 x 8 bits). Start with most signif. Bit */
	for (i = 0; i < 24; i++, data <<= 1)
		vx_outb(chip, DATA, ((data & 0x800000) ? VX_DATA_CODEC_MASK : 0));
	
	/* Terminate access to codec registers */
	vx_inb(chip, HIFREQ);
}


/*
 * vx_set_mic_boost - set mic boost level (on vxp440 only)
 * @boost: 0 = 20dB, 1 = +38dB
 */
void vx_set_mic_boost(struct vx_core *chip, int boost)
{
	struct snd_vxpocket *pchip = (struct snd_vxpocket *)chip;
	unsigned long flags;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return;

	spin_lock_irqsave(&chip->lock, flags);
	if (pchip->regCDSP & P24_CDSP_MICS_SEL_MASK) {
		if (boost) {
			/* boost: 38 dB */
			pchip->regCDSP &= ~P24_CDSP_MIC20_SEL_MASK;
			pchip->regCDSP |=  P24_CDSP_MIC38_SEL_MASK;
		} else {
			/* minimum value: 20 dB */
			pchip->regCDSP |=  P24_CDSP_MIC20_SEL_MASK;
			pchip->regCDSP &= ~P24_CDSP_MIC38_SEL_MASK;
                }
		vx_outb(chip, CDSP, pchip->regCDSP);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

/*
 * remap the linear value (0-8) to the actual value (0-15)
 */
static int vx_compute_mic_level(int level)
{
	switch (level) {
	case 5: level = 6 ; break;
	case 6: level = 8 ; break;
	case 7: level = 11; break;
	case 8: level = 15; break;
	default: break ;
	}
	return level;
}

/*
 * vx_set_mic_level - set mic level (on vxpocket only)
 * @level: the mic level = 0 - 8 (max)
 */
void vx_set_mic_level(struct vx_core *chip, int level)
{
	struct snd_vxpocket *pchip = (struct snd_vxpocket *)chip;
	unsigned long flags;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return;

	spin_lock_irqsave(&chip->lock, flags);
	if (pchip->regCDSP & VXP_CDSP_MIC_SEL_MASK) {
		level = vx_compute_mic_level(level);
		vx_outb(chip, MICRO, level);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}


/*
 * change the input audio source
 */
static void vxp_change_audio_source(struct vx_core *_chip, int src)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	switch (src) {
	case VX_AUDIO_SRC_DIGITAL:
		chip->regCDSP |= VXP_CDSP_DATAIN_SEL_MASK;
		vx_outb(chip, CDSP, chip->regCDSP);
		break;
	case VX_AUDIO_SRC_LINE:
		chip->regCDSP &= ~VXP_CDSP_DATAIN_SEL_MASK;
		if (_chip->type == VX_TYPE_VXP440)
			chip->regCDSP &= ~P24_CDSP_MICS_SEL_MASK;
		else
			chip->regCDSP &= ~VXP_CDSP_MIC_SEL_MASK;
		vx_outb(chip, CDSP, chip->regCDSP);
		break;
	case VX_AUDIO_SRC_MIC:
		chip->regCDSP &= ~VXP_CDSP_DATAIN_SEL_MASK;
		/* reset mic levels */
		if (_chip->type == VX_TYPE_VXP440) {
			chip->regCDSP &= ~P24_CDSP_MICS_SEL_MASK;
			if (chip->mic_level)
				chip->regCDSP |=  P24_CDSP_MIC38_SEL_MASK;
			else
				chip->regCDSP |= P24_CDSP_MIC20_SEL_MASK;
			vx_outb(chip, CDSP, chip->regCDSP);
		} else {
			chip->regCDSP |= VXP_CDSP_MIC_SEL_MASK;
			vx_outb(chip, CDSP, chip->regCDSP);
			vx_outb(chip, MICRO, vx_compute_mic_level(chip->mic_level));
		}
		break;
	}
}

/*
 * change the clock source
 * source = INTERNAL_QUARTZ or UER_SYNC
 */
static void vxp_set_clock_source(struct vx_core *_chip, int source)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	if (source == INTERNAL_QUARTZ)
		chip->regCDSP &= ~VXP_CDSP_CLOCKIN_SEL_MASK;
	else
		chip->regCDSP |= VXP_CDSP_CLOCKIN_SEL_MASK;
	vx_outb(chip, CDSP, chip->regCDSP);
}


/*
 * reset the board
 */
static void vxp_reset_board(struct vx_core *_chip, int cold_reset)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;

	chip->regCDSP = 0;
	chip->regDIALOG = 0;
}


/*
 * callbacks
 */
/* exported */
struct snd_vx_ops snd_vxpocket_ops = {
	.in8 = vxp_inb,
	.out8 = vxp_outb,
	.test_and_ack = vxp_test_and_ack,
	.validate_irq = vxp_validate_irq,
	.write_codec = vxp_write_codec_reg,
	.reset_codec = vxp_reset_codec,
	.change_audio_source = vxp_change_audio_source,
	.set_clock_source = vxp_set_clock_source,
	.load_dsp = vxp_load_dsp,
	.add_controls = vxp_add_mic_controls,
	.reset_dsp = vxp_reset_dsp,
	.reset_board = vxp_reset_board,
	.dma_write = vxp_dma_write,
	.dma_read = vxp_dma_read,
};
