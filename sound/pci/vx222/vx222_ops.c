/*
 * Driver for Digigram VX222 V2/Mic soundcards
 *
 * VX222-specific low-level routines
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
#include <linux/mutex.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include "vx222.h"


static int vx2_reg_offset[VX_REG_MAX] = {
	[VX_ICR]    = 0x00,
	[VX_CVR]    = 0x04,
	[VX_ISR]    = 0x08,
	[VX_IVR]    = 0x0c,
	[VX_RXH]    = 0x14,
	[VX_RXM]    = 0x18,
	[VX_RXL]    = 0x1c,
	[VX_DMA]    = 0x10,
	[VX_CDSP]   = 0x20,
	[VX_CFG]    = 0x24,
	[VX_RUER]   = 0x28,
	[VX_DATA]   = 0x2c,
	[VX_STATUS] = 0x30,
	[VX_LOFREQ] = 0x34,
	[VX_HIFREQ] = 0x38,
	[VX_CSUER]  = 0x3c,
	[VX_SELMIC] = 0x40,
	[VX_COMPOT] = 0x44, // Write: POTENTIOMETER ; Read: COMPRESSION LEVEL activate
	[VX_SCOMPR] = 0x48, // Read: COMPRESSION THRESHOLD activate
	[VX_GLIMIT] = 0x4c, // Read: LEVEL LIMITATION activate
	[VX_INTCSR] = 0x4c, // VX_INTCSR_REGISTER_OFFSET
	[VX_CNTRL]  = 0x50,		// VX_CNTRL_REGISTER_OFFSET
	[VX_GPIOC]  = 0x54,		// VX_GPIOC (new with PLX9030)
};

static int vx2_reg_index[VX_REG_MAX] = {
	[VX_ICR]	= 1,
	[VX_CVR]	= 1,
	[VX_ISR]	= 1,
	[VX_IVR]	= 1,
	[VX_RXH]	= 1,
	[VX_RXM]	= 1,
	[VX_RXL]	= 1,
	[VX_DMA]	= 1,
	[VX_CDSP]	= 1,
	[VX_CFG]	= 1,
	[VX_RUER]	= 1,
	[VX_DATA]	= 1,
	[VX_STATUS]	= 1,
	[VX_LOFREQ]	= 1,
	[VX_HIFREQ]	= 1,
	[VX_CSUER]	= 1,
	[VX_SELMIC]	= 1,
	[VX_COMPOT]	= 1,
	[VX_SCOMPR]	= 1,
	[VX_GLIMIT]	= 1,
	[VX_INTCSR]	= 0,	/* on the PLX */
	[VX_CNTRL]	= 0,	/* on the PLX */
	[VX_GPIOC]	= 0,	/* on the PLX */
};

static inline unsigned long vx2_reg_addr(struct vx_core *_chip, int reg)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	return chip->port[vx2_reg_index[reg]] + vx2_reg_offset[reg];
}

/**
 * snd_vx_inb - read a byte from the register
 * @chip: VX core instance
 * @offset: register enum
 */
static unsigned char vx2_inb(struct vx_core *chip, int offset)
{
	return inb(vx2_reg_addr(chip, offset));
}

/**
 * snd_vx_outb - write a byte on the register
 * @chip: VX core instance
 * @offset: the register offset
 * @val: the value to write
 */
static void vx2_outb(struct vx_core *chip, int offset, unsigned char val)
{
	outb(val, vx2_reg_addr(chip, offset));
	/*
	dev_dbg(chip->card->dev, "outb: %x -> %x\n", val, vx2_reg_addr(chip, offset));
	*/
}

/**
 * snd_vx_inl - read a 32bit word from the register
 * @chip: VX core instance
 * @offset: register enum
 */
static unsigned int vx2_inl(struct vx_core *chip, int offset)
{
	return inl(vx2_reg_addr(chip, offset));
}

/**
 * snd_vx_outl - write a 32bit word on the register
 * @chip: VX core instance
 * @offset: the register enum
 * @val: the value to write
 */
static void vx2_outl(struct vx_core *chip, int offset, unsigned int val)
{
	/*
	dev_dbg(chip->card->dev, "outl: %x -> %x\n", val, vx2_reg_addr(chip, offset));
	*/
	outl(val, vx2_reg_addr(chip, offset));
}

/*
 * redefine macros to call directly
 */
#undef vx_inb
#define vx_inb(chip,reg)	vx2_inb((struct vx_core*)(chip), VX_##reg)
#undef vx_outb
#define vx_outb(chip,reg,val)	vx2_outb((struct vx_core*)(chip), VX_##reg, val)
#undef vx_inl
#define vx_inl(chip,reg)	vx2_inl((struct vx_core*)(chip), VX_##reg)
#undef vx_outl
#define vx_outl(chip,reg,val)	vx2_outl((struct vx_core*)(chip), VX_##reg, val)


/*
 * vx_reset_dsp - reset the DSP
 */

#define XX_DSP_RESET_WAIT_TIME		2	/* ms */

static void vx2_reset_dsp(struct vx_core *_chip)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	/* set the reset dsp bit to 0 */
	vx_outl(chip, CDSP, chip->regCDSP & ~VX_CDSP_DSP_RESET_MASK);

	mdelay(XX_DSP_RESET_WAIT_TIME);

	chip->regCDSP |= VX_CDSP_DSP_RESET_MASK;
	/* set the reset dsp bit to 1 */
	vx_outl(chip, CDSP, chip->regCDSP);
}


static int vx2_test_xilinx(struct vx_core *_chip)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	unsigned int data;

	dev_dbg(_chip->card->dev, "testing xilinx...\n");
	/* This test uses several write/read sequences on TEST0 and TEST1 bits
	 * to figure out whever or not the xilinx was correctly loaded
	 */

	/* We write 1 on CDSP.TEST0. We should get 0 on STATUS.TEST0. */
	vx_outl(chip, CDSP, chip->regCDSP | VX_CDSP_TEST0_MASK);
	vx_inl(chip, ISR);
	data = vx_inl(chip, STATUS);
	if ((data & VX_STATUS_VAL_TEST0_MASK) == VX_STATUS_VAL_TEST0_MASK) {
		dev_dbg(_chip->card->dev, "bad!\n");
		return -ENODEV;
	}

	/* We write 0 on CDSP.TEST0. We should get 1 on STATUS.TEST0. */
	vx_outl(chip, CDSP, chip->regCDSP & ~VX_CDSP_TEST0_MASK);
	vx_inl(chip, ISR);
	data = vx_inl(chip, STATUS);
	if (! (data & VX_STATUS_VAL_TEST0_MASK)) {
		dev_dbg(_chip->card->dev, "bad! #2\n");
		return -ENODEV;
	}

	if (_chip->type == VX_TYPE_BOARD) {
		/* not implemented on VX_2_BOARDS */
		/* We write 1 on CDSP.TEST1. We should get 0 on STATUS.TEST1. */
		vx_outl(chip, CDSP, chip->regCDSP | VX_CDSP_TEST1_MASK);
		vx_inl(chip, ISR);
		data = vx_inl(chip, STATUS);
		if ((data & VX_STATUS_VAL_TEST1_MASK) == VX_STATUS_VAL_TEST1_MASK) {
			dev_dbg(_chip->card->dev, "bad! #3\n");
			return -ENODEV;
		}

		/* We write 0 on CDSP.TEST1. We should get 1 on STATUS.TEST1. */
		vx_outl(chip, CDSP, chip->regCDSP & ~VX_CDSP_TEST1_MASK);
		vx_inl(chip, ISR);
		data = vx_inl(chip, STATUS);
		if (! (data & VX_STATUS_VAL_TEST1_MASK)) {
			dev_dbg(_chip->card->dev, "bad! #4\n");
			return -ENODEV;
		}
	}
	dev_dbg(_chip->card->dev, "ok, xilinx fine.\n");
	return 0;
}


/**
 * vx_setup_pseudo_dma - set up the pseudo dma read/write mode.
 * @chip: VX core instance
 * @do_write: 0 = read, 1 = set up for DMA write
 */
static void vx2_setup_pseudo_dma(struct vx_core *chip, int do_write)
{
	/* Interrupt mode and HREQ pin enabled for host transmit data transfers
	 * (in case of the use of the pseudo-dma facility).
	 */
	vx_outl(chip, ICR, do_write ? ICR_TREQ : ICR_RREQ);

	/* Reset the pseudo-dma register (in case of the use of the
	 * pseudo-dma facility).
	 */
	vx_outl(chip, RESET_DMA, 0);
}

/*
 * vx_release_pseudo_dma - disable the pseudo-DMA mode
 */
static inline void vx2_release_pseudo_dma(struct vx_core *chip)
{
	/* HREQ pin disabled. */
	vx_outl(chip, ICR, 0);
}



/* pseudo-dma write */
static void vx2_dma_write(struct vx_core *chip, struct snd_pcm_runtime *runtime,
			  struct vx_pipe *pipe, int count)
{
	unsigned long port = vx2_reg_addr(chip, VX_DMA);
	int offset = pipe->hw_ptr;
	u32 *addr = (u32 *)(runtime->dma_area + offset);

	if (snd_BUG_ON(count % 4))
		return;

	vx2_setup_pseudo_dma(chip, 1);

	/* Transfer using pseudo-dma.
	 */
	if (offset + count >= pipe->buffer_bytes) {
		int length = pipe->buffer_bytes - offset;
		count -= length;
		length >>= 2; /* in 32bit words */
		/* Transfer using pseudo-dma. */
		for (; length > 0; length--) {
			outl(*addr, port);
			addr++;
		}
		addr = (u32 *)runtime->dma_area;
		pipe->hw_ptr = 0;
	}
	pipe->hw_ptr += count;
	count >>= 2; /* in 32bit words */
	/* Transfer using pseudo-dma. */
	for (; count > 0; count--) {
		outl(*addr, port);
		addr++;
	}

	vx2_release_pseudo_dma(chip);
}


/* pseudo dma read */
static void vx2_dma_read(struct vx_core *chip, struct snd_pcm_runtime *runtime,
			 struct vx_pipe *pipe, int count)
{
	int offset = pipe->hw_ptr;
	u32 *addr = (u32 *)(runtime->dma_area + offset);
	unsigned long port = vx2_reg_addr(chip, VX_DMA);

	if (snd_BUG_ON(count % 4))
		return;

	vx2_setup_pseudo_dma(chip, 0);
	/* Transfer using pseudo-dma.
	 */
	if (offset + count >= pipe->buffer_bytes) {
		int length = pipe->buffer_bytes - offset;
		count -= length;
		length >>= 2; /* in 32bit words */
		/* Transfer using pseudo-dma. */
		for (; length > 0; length--)
			*addr++ = inl(port);
		addr = (u32 *)runtime->dma_area;
		pipe->hw_ptr = 0;
	}
	pipe->hw_ptr += count;
	count >>= 2; /* in 32bit words */
	/* Transfer using pseudo-dma. */
	for (; count > 0; count--)
		*addr++ = inl(port);

	vx2_release_pseudo_dma(chip);
}

#define VX_XILINX_RESET_MASK        0x40000000
#define VX_USERBIT0_MASK            0x00000004
#define VX_USERBIT1_MASK            0x00000020
#define VX_CNTRL_REGISTER_VALUE     0x00172012

/*
 * transfer counts bits to PLX
 */
static int put_xilinx_data(struct vx_core *chip, unsigned int port, unsigned int counts, unsigned char data)
{
	unsigned int i;

	for (i = 0; i < counts; i++) {
		unsigned int val;

		/* set the clock bit to 0. */
		val = VX_CNTRL_REGISTER_VALUE & ~VX_USERBIT0_MASK;
		vx2_outl(chip, port, val);
		vx2_inl(chip, port);
		udelay(1);

		if (data & (1 << i))
			val |= VX_USERBIT1_MASK;
		else
			val &= ~VX_USERBIT1_MASK;
		vx2_outl(chip, port, val);
		vx2_inl(chip, port);

		/* set the clock bit to 1. */
		val |= VX_USERBIT0_MASK;
		vx2_outl(chip, port, val);
		vx2_inl(chip, port);
		udelay(1);
	}
	return 0;
}

/*
 * load the xilinx image
 */
static int vx2_load_xilinx_binary(struct vx_core *chip, const struct firmware *xilinx)
{
	unsigned int i;
	unsigned int port;
	const unsigned char *image;

	/* XILINX reset (wait at least 1 millisecond between reset on and off). */
	vx_outl(chip, CNTRL, VX_CNTRL_REGISTER_VALUE | VX_XILINX_RESET_MASK);
	vx_inl(chip, CNTRL);
	msleep(10);
	vx_outl(chip, CNTRL, VX_CNTRL_REGISTER_VALUE);
	vx_inl(chip, CNTRL);
	msleep(10);

	if (chip->type == VX_TYPE_BOARD)
		port = VX_CNTRL;
	else
		port = VX_GPIOC; /* VX222 V2 and VX222_MIC_BOARD with new PLX9030 use this register */

	image = xilinx->data;
	for (i = 0; i < xilinx->size; i++, image++) {
		if (put_xilinx_data(chip, port, 8, *image) < 0)
			return -EINVAL;
		/* don't take too much time in this loop... */
		cond_resched();
	}
	put_xilinx_data(chip, port, 4, 0xff); /* end signature */

	msleep(200);

	/* test after loading (is buggy with VX222) */
	if (chip->type != VX_TYPE_BOARD) {
		/* Test if load successful: test bit 8 of register GPIOC (VX222: use CNTRL) ! */
		i = vx_inl(chip, GPIOC);
		if (i & 0x0100)
			return 0;
		dev_err(chip->card->dev,
			"xilinx test failed after load, GPIOC=0x%x\n", i);
		return -EINVAL;
	}

	return 0;
}

	
/*
 * load the boot/dsp images
 */
static int vx2_load_dsp(struct vx_core *vx, int index, const struct firmware *dsp)
{
	int err;

	switch (index) {
	case 1:
		/* xilinx image */
		if ((err = vx2_load_xilinx_binary(vx, dsp)) < 0)
			return err;
		if ((err = vx2_test_xilinx(vx)) < 0)
			return err;
		return 0;
	case 2:
		/* DSP boot */
		return snd_vx_dsp_boot(vx, dsp);
	case 3:
		/* DSP image */
		return snd_vx_dsp_load(vx, dsp);
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
static int vx2_test_and_ack(struct vx_core *chip)
{
	/* not booted yet? */
	if (! (chip->chip_status & VX_STAT_XILINX_LOADED))
		return -ENXIO;

	if (! (vx_inl(chip, STATUS) & VX_STATUS_MEMIRQ_MASK))
		return -EIO;
	
	/* ok, interrupts generated, now ack it */
	/* set ACQUIT bit up and down */
	vx_outl(chip, STATUS, 0);
	/* useless read just to spend some time and maintain
	 * the ACQUIT signal up for a while ( a bus cycle )
	 */
	vx_inl(chip, STATUS);
	/* ack */
	vx_outl(chip, STATUS, VX_STATUS_MEMIRQ_MASK);
	/* useless read just to spend some time and maintain
	 * the ACQUIT signal up for a while ( a bus cycle ) */
	vx_inl(chip, STATUS);
	/* clear */
	vx_outl(chip, STATUS, 0);

	return 0;
}


/*
 * vx_validate_irq - enable/disable IRQ
 */
static void vx2_validate_irq(struct vx_core *_chip, int enable)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	/* Set the interrupt enable bit to 1 in CDSP register */
	if (enable) {
		/* Set the PCI interrupt enable bit to 1.*/
		vx_outl(chip, INTCSR, VX_INTCSR_VALUE|VX_PCI_INTERRUPT_MASK);
		chip->regCDSP |= VX_CDSP_VALID_IRQ_MASK;
	} else {
		/* Set the PCI interrupt enable bit to 0. */
		vx_outl(chip, INTCSR, VX_INTCSR_VALUE&~VX_PCI_INTERRUPT_MASK);
		chip->regCDSP &= ~VX_CDSP_VALID_IRQ_MASK;
	}
	vx_outl(chip, CDSP, chip->regCDSP);
}


/*
 * write an AKM codec data (24bit)
 */
static void vx2_write_codec_reg(struct vx_core *chip, unsigned int data)
{
	unsigned int i;

	vx_inl(chip, HIFREQ);

	/* We have to send 24 bits (3 x 8 bits). Start with most signif. Bit */
	for (i = 0; i < 24; i++, data <<= 1)
		vx_outl(chip, DATA, ((data & 0x800000) ? VX_DATA_CODEC_MASK : 0));
	/* Terminate access to codec registers */
	vx_inl(chip, RUER);
}


#define AKM_CODEC_POWER_CONTROL_CMD 0xA007
#define AKM_CODEC_RESET_ON_CMD      0xA100
#define AKM_CODEC_RESET_OFF_CMD     0xA103
#define AKM_CODEC_CLOCK_FORMAT_CMD  0xA240
#define AKM_CODEC_MUTE_CMD          0xA38D
#define AKM_CODEC_UNMUTE_CMD        0xA30D
#define AKM_CODEC_LEFT_LEVEL_CMD    0xA400
#define AKM_CODEC_RIGHT_LEVEL_CMD   0xA500

static const u8 vx2_akm_gains_lut[VX2_AKM_LEVEL_MAX+1] = {
    0x7f,       // [000] =  +0.000 dB  ->  AKM(0x7f) =  +0.000 dB  error(+0.000 dB)
    0x7d,       // [001] =  -0.500 dB  ->  AKM(0x7d) =  -0.572 dB  error(-0.072 dB)
    0x7c,       // [002] =  -1.000 dB  ->  AKM(0x7c) =  -0.873 dB  error(+0.127 dB)
    0x7a,       // [003] =  -1.500 dB  ->  AKM(0x7a) =  -1.508 dB  error(-0.008 dB)
    0x79,       // [004] =  -2.000 dB  ->  AKM(0x79) =  -1.844 dB  error(+0.156 dB)
    0x77,       // [005] =  -2.500 dB  ->  AKM(0x77) =  -2.557 dB  error(-0.057 dB)
    0x76,       // [006] =  -3.000 dB  ->  AKM(0x76) =  -2.937 dB  error(+0.063 dB)
    0x75,       // [007] =  -3.500 dB  ->  AKM(0x75) =  -3.334 dB  error(+0.166 dB)
    0x73,       // [008] =  -4.000 dB  ->  AKM(0x73) =  -4.188 dB  error(-0.188 dB)
    0x72,       // [009] =  -4.500 dB  ->  AKM(0x72) =  -4.648 dB  error(-0.148 dB)
    0x71,       // [010] =  -5.000 dB  ->  AKM(0x71) =  -5.134 dB  error(-0.134 dB)
    0x70,       // [011] =  -5.500 dB  ->  AKM(0x70) =  -5.649 dB  error(-0.149 dB)
    0x6f,       // [012] =  -6.000 dB  ->  AKM(0x6f) =  -6.056 dB  error(-0.056 dB)
    0x6d,       // [013] =  -6.500 dB  ->  AKM(0x6d) =  -6.631 dB  error(-0.131 dB)
    0x6c,       // [014] =  -7.000 dB  ->  AKM(0x6c) =  -6.933 dB  error(+0.067 dB)
    0x6a,       // [015] =  -7.500 dB  ->  AKM(0x6a) =  -7.571 dB  error(-0.071 dB)
    0x69,       // [016] =  -8.000 dB  ->  AKM(0x69) =  -7.909 dB  error(+0.091 dB)
    0x67,       // [017] =  -8.500 dB  ->  AKM(0x67) =  -8.626 dB  error(-0.126 dB)
    0x66,       // [018] =  -9.000 dB  ->  AKM(0x66) =  -9.008 dB  error(-0.008 dB)
    0x65,       // [019] =  -9.500 dB  ->  AKM(0x65) =  -9.407 dB  error(+0.093 dB)
    0x64,       // [020] = -10.000 dB  ->  AKM(0x64) =  -9.826 dB  error(+0.174 dB)
    0x62,       // [021] = -10.500 dB  ->  AKM(0x62) = -10.730 dB  error(-0.230 dB)
    0x61,       // [022] = -11.000 dB  ->  AKM(0x61) = -11.219 dB  error(-0.219 dB)
    0x60,       // [023] = -11.500 dB  ->  AKM(0x60) = -11.738 dB  error(-0.238 dB)
    0x5f,       // [024] = -12.000 dB  ->  AKM(0x5f) = -12.149 dB  error(-0.149 dB)
    0x5e,       // [025] = -12.500 dB  ->  AKM(0x5e) = -12.434 dB  error(+0.066 dB)
    0x5c,       // [026] = -13.000 dB  ->  AKM(0x5c) = -13.033 dB  error(-0.033 dB)
    0x5b,       // [027] = -13.500 dB  ->  AKM(0x5b) = -13.350 dB  error(+0.150 dB)
    0x59,       // [028] = -14.000 dB  ->  AKM(0x59) = -14.018 dB  error(-0.018 dB)
    0x58,       // [029] = -14.500 dB  ->  AKM(0x58) = -14.373 dB  error(+0.127 dB)
    0x56,       // [030] = -15.000 dB  ->  AKM(0x56) = -15.130 dB  error(-0.130 dB)
    0x55,       // [031] = -15.500 dB  ->  AKM(0x55) = -15.534 dB  error(-0.034 dB)
    0x54,       // [032] = -16.000 dB  ->  AKM(0x54) = -15.958 dB  error(+0.042 dB)
    0x53,       // [033] = -16.500 dB  ->  AKM(0x53) = -16.404 dB  error(+0.096 dB)
    0x52,       // [034] = -17.000 dB  ->  AKM(0x52) = -16.874 dB  error(+0.126 dB)
    0x51,       // [035] = -17.500 dB  ->  AKM(0x51) = -17.371 dB  error(+0.129 dB)
    0x50,       // [036] = -18.000 dB  ->  AKM(0x50) = -17.898 dB  error(+0.102 dB)
    0x4e,       // [037] = -18.500 dB  ->  AKM(0x4e) = -18.605 dB  error(-0.105 dB)
    0x4d,       // [038] = -19.000 dB  ->  AKM(0x4d) = -18.905 dB  error(+0.095 dB)
    0x4b,       // [039] = -19.500 dB  ->  AKM(0x4b) = -19.538 dB  error(-0.038 dB)
    0x4a,       // [040] = -20.000 dB  ->  AKM(0x4a) = -19.872 dB  error(+0.128 dB)
    0x48,       // [041] = -20.500 dB  ->  AKM(0x48) = -20.583 dB  error(-0.083 dB)
    0x47,       // [042] = -21.000 dB  ->  AKM(0x47) = -20.961 dB  error(+0.039 dB)
    0x46,       // [043] = -21.500 dB  ->  AKM(0x46) = -21.356 dB  error(+0.144 dB)
    0x44,       // [044] = -22.000 dB  ->  AKM(0x44) = -22.206 dB  error(-0.206 dB)
    0x43,       // [045] = -22.500 dB  ->  AKM(0x43) = -22.664 dB  error(-0.164 dB)
    0x42,       // [046] = -23.000 dB  ->  AKM(0x42) = -23.147 dB  error(-0.147 dB)
    0x41,       // [047] = -23.500 dB  ->  AKM(0x41) = -23.659 dB  error(-0.159 dB)
    0x40,       // [048] = -24.000 dB  ->  AKM(0x40) = -24.203 dB  error(-0.203 dB)
    0x3f,       // [049] = -24.500 dB  ->  AKM(0x3f) = -24.635 dB  error(-0.135 dB)
    0x3e,       // [050] = -25.000 dB  ->  AKM(0x3e) = -24.935 dB  error(+0.065 dB)
    0x3c,       // [051] = -25.500 dB  ->  AKM(0x3c) = -25.569 dB  error(-0.069 dB)
    0x3b,       // [052] = -26.000 dB  ->  AKM(0x3b) = -25.904 dB  error(+0.096 dB)
    0x39,       // [053] = -26.500 dB  ->  AKM(0x39) = -26.615 dB  error(-0.115 dB)
    0x38,       // [054] = -27.000 dB  ->  AKM(0x38) = -26.994 dB  error(+0.006 dB)
    0x37,       // [055] = -27.500 dB  ->  AKM(0x37) = -27.390 dB  error(+0.110 dB)
    0x36,       // [056] = -28.000 dB  ->  AKM(0x36) = -27.804 dB  error(+0.196 dB)
    0x34,       // [057] = -28.500 dB  ->  AKM(0x34) = -28.699 dB  error(-0.199 dB)
    0x33,       // [058] = -29.000 dB  ->  AKM(0x33) = -29.183 dB  error(-0.183 dB)
    0x32,       // [059] = -29.500 dB  ->  AKM(0x32) = -29.696 dB  error(-0.196 dB)
    0x31,       // [060] = -30.000 dB  ->  AKM(0x31) = -30.241 dB  error(-0.241 dB)
    0x31,       // [061] = -30.500 dB  ->  AKM(0x31) = -30.241 dB  error(+0.259 dB)
    0x30,       // [062] = -31.000 dB  ->  AKM(0x30) = -30.823 dB  error(+0.177 dB)
    0x2e,       // [063] = -31.500 dB  ->  AKM(0x2e) = -31.610 dB  error(-0.110 dB)
    0x2d,       // [064] = -32.000 dB  ->  AKM(0x2d) = -31.945 dB  error(+0.055 dB)
    0x2b,       // [065] = -32.500 dB  ->  AKM(0x2b) = -32.659 dB  error(-0.159 dB)
    0x2a,       // [066] = -33.000 dB  ->  AKM(0x2a) = -33.038 dB  error(-0.038 dB)
    0x29,       // [067] = -33.500 dB  ->  AKM(0x29) = -33.435 dB  error(+0.065 dB)
    0x28,       // [068] = -34.000 dB  ->  AKM(0x28) = -33.852 dB  error(+0.148 dB)
    0x27,       // [069] = -34.500 dB  ->  AKM(0x27) = -34.289 dB  error(+0.211 dB)
    0x25,       // [070] = -35.000 dB  ->  AKM(0x25) = -35.235 dB  error(-0.235 dB)
    0x24,       // [071] = -35.500 dB  ->  AKM(0x24) = -35.750 dB  error(-0.250 dB)
    0x24,       // [072] = -36.000 dB  ->  AKM(0x24) = -35.750 dB  error(+0.250 dB)
    0x23,       // [073] = -36.500 dB  ->  AKM(0x23) = -36.297 dB  error(+0.203 dB)
    0x22,       // [074] = -37.000 dB  ->  AKM(0x22) = -36.881 dB  error(+0.119 dB)
    0x21,       // [075] = -37.500 dB  ->  AKM(0x21) = -37.508 dB  error(-0.008 dB)
    0x20,       // [076] = -38.000 dB  ->  AKM(0x20) = -38.183 dB  error(-0.183 dB)
    0x1f,       // [077] = -38.500 dB  ->  AKM(0x1f) = -38.726 dB  error(-0.226 dB)
    0x1e,       // [078] = -39.000 dB  ->  AKM(0x1e) = -39.108 dB  error(-0.108 dB)
    0x1d,       // [079] = -39.500 dB  ->  AKM(0x1d) = -39.507 dB  error(-0.007 dB)
    0x1c,       // [080] = -40.000 dB  ->  AKM(0x1c) = -39.926 dB  error(+0.074 dB)
    0x1b,       // [081] = -40.500 dB  ->  AKM(0x1b) = -40.366 dB  error(+0.134 dB)
    0x1a,       // [082] = -41.000 dB  ->  AKM(0x1a) = -40.829 dB  error(+0.171 dB)
    0x19,       // [083] = -41.500 dB  ->  AKM(0x19) = -41.318 dB  error(+0.182 dB)
    0x18,       // [084] = -42.000 dB  ->  AKM(0x18) = -41.837 dB  error(+0.163 dB)
    0x17,       // [085] = -42.500 dB  ->  AKM(0x17) = -42.389 dB  error(+0.111 dB)
    0x16,       // [086] = -43.000 dB  ->  AKM(0x16) = -42.978 dB  error(+0.022 dB)
    0x15,       // [087] = -43.500 dB  ->  AKM(0x15) = -43.610 dB  error(-0.110 dB)
    0x14,       // [088] = -44.000 dB  ->  AKM(0x14) = -44.291 dB  error(-0.291 dB)
    0x14,       // [089] = -44.500 dB  ->  AKM(0x14) = -44.291 dB  error(+0.209 dB)
    0x13,       // [090] = -45.000 dB  ->  AKM(0x13) = -45.031 dB  error(-0.031 dB)
    0x12,       // [091] = -45.500 dB  ->  AKM(0x12) = -45.840 dB  error(-0.340 dB)
    0x12,       // [092] = -46.000 dB  ->  AKM(0x12) = -45.840 dB  error(+0.160 dB)
    0x11,       // [093] = -46.500 dB  ->  AKM(0x11) = -46.731 dB  error(-0.231 dB)
    0x11,       // [094] = -47.000 dB  ->  AKM(0x11) = -46.731 dB  error(+0.269 dB)
    0x10,       // [095] = -47.500 dB  ->  AKM(0x10) = -47.725 dB  error(-0.225 dB)
    0x10,       // [096] = -48.000 dB  ->  AKM(0x10) = -47.725 dB  error(+0.275 dB)
    0x0f,       // [097] = -48.500 dB  ->  AKM(0x0f) = -48.553 dB  error(-0.053 dB)
    0x0e,       // [098] = -49.000 dB  ->  AKM(0x0e) = -49.152 dB  error(-0.152 dB)
    0x0d,       // [099] = -49.500 dB  ->  AKM(0x0d) = -49.796 dB  error(-0.296 dB)
    0x0d,       // [100] = -50.000 dB  ->  AKM(0x0d) = -49.796 dB  error(+0.204 dB)
    0x0c,       // [101] = -50.500 dB  ->  AKM(0x0c) = -50.491 dB  error(+0.009 dB)
    0x0b,       // [102] = -51.000 dB  ->  AKM(0x0b) = -51.247 dB  error(-0.247 dB)
    0x0b,       // [103] = -51.500 dB  ->  AKM(0x0b) = -51.247 dB  error(+0.253 dB)
    0x0a,       // [104] = -52.000 dB  ->  AKM(0x0a) = -52.075 dB  error(-0.075 dB)
    0x0a,       // [105] = -52.500 dB  ->  AKM(0x0a) = -52.075 dB  error(+0.425 dB)
    0x09,       // [106] = -53.000 dB  ->  AKM(0x09) = -52.990 dB  error(+0.010 dB)
    0x09,       // [107] = -53.500 dB  ->  AKM(0x09) = -52.990 dB  error(+0.510 dB)
    0x08,       // [108] = -54.000 dB  ->  AKM(0x08) = -54.013 dB  error(-0.013 dB)
    0x08,       // [109] = -54.500 dB  ->  AKM(0x08) = -54.013 dB  error(+0.487 dB)
    0x07,       // [110] = -55.000 dB  ->  AKM(0x07) = -55.173 dB  error(-0.173 dB)
    0x07,       // [111] = -55.500 dB  ->  AKM(0x07) = -55.173 dB  error(+0.327 dB)
    0x06,       // [112] = -56.000 dB  ->  AKM(0x06) = -56.512 dB  error(-0.512 dB)
    0x06,       // [113] = -56.500 dB  ->  AKM(0x06) = -56.512 dB  error(-0.012 dB)
    0x06,       // [114] = -57.000 dB  ->  AKM(0x06) = -56.512 dB  error(+0.488 dB)
    0x05,       // [115] = -57.500 dB  ->  AKM(0x05) = -58.095 dB  error(-0.595 dB)
    0x05,       // [116] = -58.000 dB  ->  AKM(0x05) = -58.095 dB  error(-0.095 dB)
    0x05,       // [117] = -58.500 dB  ->  AKM(0x05) = -58.095 dB  error(+0.405 dB)
    0x05,       // [118] = -59.000 dB  ->  AKM(0x05) = -58.095 dB  error(+0.905 dB)
    0x04,       // [119] = -59.500 dB  ->  AKM(0x04) = -60.034 dB  error(-0.534 dB)
    0x04,       // [120] = -60.000 dB  ->  AKM(0x04) = -60.034 dB  error(-0.034 dB)
    0x04,       // [121] = -60.500 dB  ->  AKM(0x04) = -60.034 dB  error(+0.466 dB)
    0x04,       // [122] = -61.000 dB  ->  AKM(0x04) = -60.034 dB  error(+0.966 dB)
    0x03,       // [123] = -61.500 dB  ->  AKM(0x03) = -62.532 dB  error(-1.032 dB)
    0x03,       // [124] = -62.000 dB  ->  AKM(0x03) = -62.532 dB  error(-0.532 dB)
    0x03,       // [125] = -62.500 dB  ->  AKM(0x03) = -62.532 dB  error(-0.032 dB)
    0x03,       // [126] = -63.000 dB  ->  AKM(0x03) = -62.532 dB  error(+0.468 dB)
    0x03,       // [127] = -63.500 dB  ->  AKM(0x03) = -62.532 dB  error(+0.968 dB)
    0x03,       // [128] = -64.000 dB  ->  AKM(0x03) = -62.532 dB  error(+1.468 dB)
    0x02,       // [129] = -64.500 dB  ->  AKM(0x02) = -66.054 dB  error(-1.554 dB)
    0x02,       // [130] = -65.000 dB  ->  AKM(0x02) = -66.054 dB  error(-1.054 dB)
    0x02,       // [131] = -65.500 dB  ->  AKM(0x02) = -66.054 dB  error(-0.554 dB)
    0x02,       // [132] = -66.000 dB  ->  AKM(0x02) = -66.054 dB  error(-0.054 dB)
    0x02,       // [133] = -66.500 dB  ->  AKM(0x02) = -66.054 dB  error(+0.446 dB)
    0x02,       // [134] = -67.000 dB  ->  AKM(0x02) = -66.054 dB  error(+0.946 dB)
    0x02,       // [135] = -67.500 dB  ->  AKM(0x02) = -66.054 dB  error(+1.446 dB)
    0x02,       // [136] = -68.000 dB  ->  AKM(0x02) = -66.054 dB  error(+1.946 dB)
    0x02,       // [137] = -68.500 dB  ->  AKM(0x02) = -66.054 dB  error(+2.446 dB)
    0x02,       // [138] = -69.000 dB  ->  AKM(0x02) = -66.054 dB  error(+2.946 dB)
    0x01,       // [139] = -69.500 dB  ->  AKM(0x01) = -72.075 dB  error(-2.575 dB)
    0x01,       // [140] = -70.000 dB  ->  AKM(0x01) = -72.075 dB  error(-2.075 dB)
    0x01,       // [141] = -70.500 dB  ->  AKM(0x01) = -72.075 dB  error(-1.575 dB)
    0x01,       // [142] = -71.000 dB  ->  AKM(0x01) = -72.075 dB  error(-1.075 dB)
    0x01,       // [143] = -71.500 dB  ->  AKM(0x01) = -72.075 dB  error(-0.575 dB)
    0x01,       // [144] = -72.000 dB  ->  AKM(0x01) = -72.075 dB  error(-0.075 dB)
    0x01,       // [145] = -72.500 dB  ->  AKM(0x01) = -72.075 dB  error(+0.425 dB)
    0x01,       // [146] = -73.000 dB  ->  AKM(0x01) = -72.075 dB  error(+0.925 dB)
    0x00};      // [147] = -73.500 dB  ->  AKM(0x00) =  mute       error(+infini)

/*
 * pseudo-codec write entry
 */
static void vx2_write_akm(struct vx_core *chip, int reg, unsigned int data)
{
	unsigned int val;

	if (reg == XX_CODEC_DAC_CONTROL_REGISTER) {
		vx2_write_codec_reg(chip, data ? AKM_CODEC_MUTE_CMD : AKM_CODEC_UNMUTE_CMD);
		return;
	}

	/* `data' is a value between 0x0 and VX2_AKM_LEVEL_MAX = 0x093, in the case of the AKM codecs, we need
	   a look up table, as there is no linear matching between the driver codec values
	   and the real dBu value
	*/
	if (snd_BUG_ON(data >= sizeof(vx2_akm_gains_lut)))
		return;

	switch (reg) {
	case XX_CODEC_LEVEL_LEFT_REGISTER:
		val = AKM_CODEC_LEFT_LEVEL_CMD;
		break;
	case XX_CODEC_LEVEL_RIGHT_REGISTER:
		val = AKM_CODEC_RIGHT_LEVEL_CMD;
		break;
	default:
		snd_BUG();
		return;
	}
	val |= vx2_akm_gains_lut[data];

	vx2_write_codec_reg(chip, val);
}


/*
 * write codec bit for old VX222 board
 */
static void vx2_old_write_codec_bit(struct vx_core *chip, int codec, unsigned int data)
{
	int i;

	/* activate access to codec registers */
	vx_inl(chip, HIFREQ);

	for (i = 0; i < 24; i++, data <<= 1)
		vx_outl(chip, DATA, ((data & 0x800000) ? VX_DATA_CODEC_MASK : 0));

	/* Terminate access to codec registers */
	vx_inl(chip, RUER);
}


/*
 * reset codec bit
 */
static void vx2_reset_codec(struct vx_core *_chip)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	/* Set the reset CODEC bit to 0. */
	vx_outl(chip, CDSP, chip->regCDSP &~ VX_CDSP_CODEC_RESET_MASK);
	vx_inl(chip, CDSP);
	msleep(10);
	/* Set the reset CODEC bit to 1. */
	chip->regCDSP |= VX_CDSP_CODEC_RESET_MASK;
	vx_outl(chip, CDSP, chip->regCDSP);
	vx_inl(chip, CDSP);
	if (_chip->type == VX_TYPE_BOARD) {
		msleep(1);
		return;
	}

	msleep(5);  /* additionnel wait time for AKM's */

	vx2_write_codec_reg(_chip, AKM_CODEC_POWER_CONTROL_CMD); /* DAC power up, ADC power up, Vref power down */
	
	vx2_write_codec_reg(_chip, AKM_CODEC_CLOCK_FORMAT_CMD); /* default */
	vx2_write_codec_reg(_chip, AKM_CODEC_MUTE_CMD); /* Mute = ON ,Deemphasis = OFF */
	vx2_write_codec_reg(_chip, AKM_CODEC_RESET_OFF_CMD); /* DAC and ADC normal operation */

	if (_chip->type == VX_TYPE_MIC) {
		/* set up the micro input selector */
		chip->regSELMIC =  MICRO_SELECT_INPUT_NORM |
			MICRO_SELECT_PREAMPLI_G_0 |
			MICRO_SELECT_NOISE_T_52DB;

		/* reset phantom power supply */
		chip->regSELMIC &= ~MICRO_SELECT_PHANTOM_ALIM;

		vx_outl(_chip, SELMIC, chip->regSELMIC);
	}
}


/*
 * change the audio source
 */
static void vx2_change_audio_source(struct vx_core *_chip, int src)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	switch (src) {
	case VX_AUDIO_SRC_DIGITAL:
		chip->regCFG |= VX_CFG_DATAIN_SEL_MASK;
		break;
	default:
		chip->regCFG &= ~VX_CFG_DATAIN_SEL_MASK;
		break;
	}
	vx_outl(chip, CFG, chip->regCFG);
}


/*
 * set the clock source
 */
static void vx2_set_clock_source(struct vx_core *_chip, int source)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	if (source == INTERNAL_QUARTZ)
		chip->regCFG &= ~VX_CFG_CLOCKIN_SEL_MASK;
	else
		chip->regCFG |= VX_CFG_CLOCKIN_SEL_MASK;
	vx_outl(chip, CFG, chip->regCFG);
}

/*
 * reset the board
 */
static void vx2_reset_board(struct vx_core *_chip, int cold_reset)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;

	/* initialize the register values */
	chip->regCDSP = VX_CDSP_CODEC_RESET_MASK | VX_CDSP_DSP_RESET_MASK ;
	chip->regCFG = 0;
}



/*
 * input level controls for VX222 Mic
 */

/* Micro level is specified to be adjustable from -96dB to 63 dB (board coded 0x00 ... 318),
 * 318 = 210 + 36 + 36 + 36   (210 = +9dB variable) (3 * 36 = 3 steps of 18dB pre ampli)
 * as we will mute if less than -110dB, so let's simply use line input coded levels and add constant offset !
 */
#define V2_MICRO_LEVEL_RANGE        (318 - 255)

static void vx2_set_input_level(struct snd_vx222 *chip)
{
	int i, miclevel, preamp;
	unsigned int data;

	miclevel = chip->mic_level;
	miclevel += V2_MICRO_LEVEL_RANGE; /* add 318 - 0xff */
	preamp = 0;
        while (miclevel > 210) { /* limitation to +9dB of 3310 real gain */
		preamp++;	/* raise pre ampli + 18dB */
		miclevel -= (18 * 2);   /* lower level 18 dB (*2 because of 0.5 dB steps !) */
        }
	if (snd_BUG_ON(preamp >= 4))
		return;

	/* set pre-amp level */
	chip->regSELMIC &= ~MICRO_SELECT_PREAMPLI_MASK;
	chip->regSELMIC |= (preamp << MICRO_SELECT_PREAMPLI_OFFSET) & MICRO_SELECT_PREAMPLI_MASK;
	vx_outl(chip, SELMIC, chip->regSELMIC);

	data = (unsigned int)miclevel << 16 |
		(unsigned int)chip->input_level[1] << 8 |
		(unsigned int)chip->input_level[0];
	vx_inl(chip, DATA); /* Activate input level programming */

	/* We have to send 32 bits (4 x 8 bits) */
	for (i = 0; i < 32; i++, data <<= 1)
		vx_outl(chip, DATA, ((data & 0x80000000) ? VX_DATA_CODEC_MASK : 0));

	vx_inl(chip, RUER); /* Terminate input level programming */
}


#define MIC_LEVEL_MAX	0xff

static const DECLARE_TLV_DB_SCALE(db_scale_mic, -6450, 50, 0);

/*
 * controls API for input levels
 */

/* input levels */
static int vx_input_level_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = MIC_LEVEL_MAX;
	return 0;
}

static int vx_input_level_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	mutex_lock(&_chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->input_level[0];
	ucontrol->value.integer.value[1] = chip->input_level[1];
	mutex_unlock(&_chip->mixer_mutex);
	return 0;
}

static int vx_input_level_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] > MIC_LEVEL_MAX)
		return -EINVAL;
	if (ucontrol->value.integer.value[1] < 0 ||
	    ucontrol->value.integer.value[1] > MIC_LEVEL_MAX)
		return -EINVAL;
	mutex_lock(&_chip->mixer_mutex);
	if (chip->input_level[0] != ucontrol->value.integer.value[0] ||
	    chip->input_level[1] != ucontrol->value.integer.value[1]) {
		chip->input_level[0] = ucontrol->value.integer.value[0];
		chip->input_level[1] = ucontrol->value.integer.value[1];
		vx2_set_input_level(chip);
		mutex_unlock(&_chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&_chip->mixer_mutex);
	return 0;
}

/* mic level */
static int vx_mic_level_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = MIC_LEVEL_MAX;
	return 0;
}

static int vx_mic_level_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	ucontrol->value.integer.value[0] = chip->mic_level;
	return 0;
}

static int vx_mic_level_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] > MIC_LEVEL_MAX)
		return -EINVAL;
	mutex_lock(&_chip->mixer_mutex);
	if (chip->mic_level != ucontrol->value.integer.value[0]) {
		chip->mic_level = ucontrol->value.integer.value[0];
		vx2_set_input_level(chip);
		mutex_unlock(&_chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&_chip->mixer_mutex);
	return 0;
}

static struct snd_kcontrol_new vx_control_input_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"Capture Volume",
	.info =		vx_input_level_info,
	.get =		vx_input_level_get,
	.put =		vx_input_level_put,
	.tlv = { .p = db_scale_mic },
};

static struct snd_kcontrol_new vx_control_mic_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"Mic Capture Volume",
	.info =		vx_mic_level_info,
	.get =		vx_mic_level_get,
	.put =		vx_mic_level_put,
	.tlv = { .p = db_scale_mic },
};

/*
 * FIXME: compressor/limiter implementation is missing yet...
 */

static int vx2_add_mic_controls(struct vx_core *_chip)
{
	struct snd_vx222 *chip = (struct snd_vx222 *)_chip;
	int err;

	if (_chip->type != VX_TYPE_MIC)
		return 0;

	/* mute input levels */
	chip->input_level[0] = chip->input_level[1] = 0;
	chip->mic_level = 0;
	vx2_set_input_level(chip);

	/* controls */
	if ((err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_input_level, chip))) < 0)
		return err;
	if ((err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_mic_level, chip))) < 0)
		return err;

	return 0;
}


/*
 * callbacks
 */
struct snd_vx_ops vx222_ops = {
	.in8 = vx2_inb,
	.in32 = vx2_inl,
	.out8 = vx2_outb,
	.out32 = vx2_outl,
	.test_and_ack = vx2_test_and_ack,
	.validate_irq = vx2_validate_irq,
	.akm_write = vx2_write_akm,
	.reset_codec = vx2_reset_codec,
	.change_audio_source = vx2_change_audio_source,
	.set_clock_source = vx2_set_clock_source,
	.load_dsp = vx2_load_dsp,
	.reset_dsp = vx2_reset_dsp,
	.reset_board = vx2_reset_board,
	.dma_write = vx2_dma_write,
	.dma_read = vx2_dma_read,
	.add_controls = vx2_add_mic_controls,
};

/* for old VX222 board */
struct snd_vx_ops vx222_old_ops = {
	.in8 = vx2_inb,
	.in32 = vx2_inl,
	.out8 = vx2_outb,
	.out32 = vx2_outl,
	.test_and_ack = vx2_test_and_ack,
	.validate_irq = vx2_validate_irq,
	.write_codec = vx2_old_write_codec_bit,
	.reset_codec = vx2_reset_codec,
	.change_audio_source = vx2_change_audio_source,
	.set_clock_source = vx2_set_clock_source,
	.load_dsp = vx2_load_dsp,
	.reset_dsp = vx2_reset_dsp,
	.reset_board = vx2_reset_board,
	.dma_write = vx2_dma_write,
	.dma_read = vx2_dma_read,
};

