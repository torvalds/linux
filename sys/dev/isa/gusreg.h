/* $OpenBSD: gusreg.h,v 1.7 2022/01/09 05:42:42 jsg Exp $ */
/* $NetBSD: gusreg.h,v 1.6 1997/10/09 07:57:22 jtc Exp $ */

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register definitions of Gravis UltraSound card
 */

/*
 * MIDI control registers.  Essentially a MC6850 UART.  Note the MC6850's
 * "feature" of having read-only and write-only registers combined on one
 * address.
 */

#define GUS_IOH4_OFFSET		0x100
#define GUS_NPORT4		2

#define GUS_MIDI_CONTROL	(0x100-GUS_IOH4_OFFSET)
#define GUS_MIDI_STATUS		(0x100-GUS_IOH4_OFFSET)
#define GUS_MIDI_READ		(0x101-GUS_IOH4_OFFSET)
#define GUS_MIDI_WRITE		(0x101-GUS_IOH4_OFFSET)

/*
 * Joystick interface - note this is an absolute address, NOT an offset from
 * the GUS base address.
 */

#define GUS_JOYSTICK		0x201

/*
 * GUS control registers
 */

#define GUS_MIX_CONTROL		0x000
#define GUS_IRQ_STATUS		0x006
#define GUS_TIMER_CONTROL	0x008
#define GUS_TIMER_DATA		0x009
#define GUS_REG_CONTROL		0x00f	/* rev 3.4 or later only: select reg
					   at 2XB */
#define		GUS_REG_NORMAL		0x00 /* IRQ/DMA as usual */
#define		GUS_REG_IRQCTL		0x05 /* IRQ ctl: write 0 to clear IRQ state */
#define		GUS_REG_JUMPER		0x06 /* jumper control: */
#define		GUS_JUMPER_MIDIEN	0x02 /* bit: enable MIDI ports */
#define		GUS_JUMPER_JOYEN	0x04 /* bit: enable joystick ports */

#define GUS_IRQ_CONTROL		0x00b
#define GUS_DMA_CONTROL		0x00b
#define GUS_IRQCTL_CONTROL	0x00b
#define GUS_JUMPER_CONTROL	0x00b

#define GUS_NPORT1 16

#define GUS_IOH2_OFFSET		0x102
#define GUS_VOICE_SELECT	(0x102-GUS_IOH2_OFFSET)
#define GUS_REG_SELECT		(0x103-GUS_IOH2_OFFSET)
#define GUS_DATA_LOW		(0x104-GUS_IOH2_OFFSET)
#define GUS_DATA_HIGH		(0x105-GUS_IOH2_OFFSET)
/* GUS_MIXER_SELECT 106 */
#define GUS_DRAM_DATA		(0x107-GUS_IOH2_OFFSET)

#define GUS_NPORT2 6

/*
 * GUS on-board global registers
 */

#define GUSREG_DMA_CONTROL	0x41
#define GUSREG_DMA_START	0x42
#define GUSREG_DRAM_ADDR_LOW	0x43
#define GUSREG_DRAM_ADDR_HIGH	0x44
#define GUSREG_TIMER_CONTROL	0x45
#define GUSREG_TIMER1_COUNT	0x46	/* count-up, then interrupt, 80usec */
#define GUSREG_TIMER2_COUNT	0x47	/* count-up, then interrupt, 320usec */
#define GUSREG_SAMPLE_FREQ	0x48	/* 9878400/(16*(rate+2)) */
#define GUSREG_SAMPLE_CONTROL	0x49
#define GUSREG_JOYSTICK_TRIM	0x4b
#define GUSREG_RESET		0x4c

/*
 * GUS voice specific registers (some of which aren't!).  Add 0x80 to these
 * registers for reads
 */

#define GUSREG_READ		0x80
#define GUSREG_VOICE_CNTL	0x00
#define GUSREG_FREQ_CONTROL	0x01
#define GUSREG_START_ADDR_HIGH	0x02
#define GUSREG_START_ADDR_LOW	0x03
#define GUSREG_END_ADDR_HIGH	0x04
#define GUSREG_END_ADDR_LOW	0x05
#define GUSREG_VOLUME_RATE	0x06
#define GUSREG_START_VOLUME	0x07
#define GUSREG_END_VOLUME	0x08
#define GUSREG_CUR_VOLUME	0x09
#define GUSREG_CUR_ADDR_HIGH	0x0a
#define GUSREG_CUR_ADDR_LOW	0x0b
#define GUSREG_PAN_POS		0x0c
#define GUSREG_VOLUME_CONTROL	0x0d
#define GUSREG_ACTIVE_VOICES	0x0e	/* voice-independent:set voice count */
#define GUSREG_IRQ_STATUS	0x8f	/* voice-independent */

#define GUS_PAN_FULL_LEFT	0x0
#define GUS_PAN_FULL_RIGHT	0xf

/*
 * GUS Bitmasks for reset register
 */

#define GUSMASK_MASTER_RESET	0x01
#define GUSMASK_DAC_ENABLE	0x02
#define GUSMASK_IRQ_ENABLE	0x04

/*
 * Bitmasks for IRQ status port
 */

#define GUSMASK_IRQ_MIDIXMIT	0x01		/* MIDI transmit IRQ */
#define GUSMASK_IRQ_MIDIRCVR	0x02		/* MIDI received IRQ */
#define GUSMASK_IRQ_TIMER1	0x04		/* timer 1 IRQ */
#define GUSMASK_IRQ_TIMER2	0x08		/* timer 2 IRQ */
#define GUSMASK_IRQ_RESERVED	0x10		/* Reserved (set to 0) */
#define GUSMASK_IRQ_VOICE	0x20		/* Wavetable IRQ (any voice) */
#define GUSMASK_IRQ_VOLUME	0x40		/* Volume ramp IRQ (any voc) */
#define GUSMASK_IRQ_DMATC	0x80		/* DMA transfer complete */

/*
 * Bitmasks for sampling control register
 */
#define	GUSMASK_SAMPLE_START	0x01		/* start sampling */
#define	GUSMASK_SAMPLE_STEREO	0x02		/* mono or stereo */
#define	GUSMASK_SAMPLE_DATA16	0x04		/* 16-bit DMA channel */
#define	GUSMASK_SAMPLE_IRQ	0x20		/* enable IRQ */
#define	GUSMASK_SAMPLE_DMATC	0x40		/* DMA transfer complete */
#define	GUSMASK_SAMPLE_INVBIT	0x80		/* invert MSbit */

/*
 * Bitmasks for IRQ status register (different than IRQ status _port_ - the
 * register is internal to the GUS)
 */

#define GUSMASK_WIRQ_VOLUME	0x40		/* Flag for volume interrupt */
#define GUSMASK_WIRQ_VOICE	0x80		/* Flag for voice interrupt */
#define GUSMASK_WIRQ_VOICEMASK	0x1f		/* Bits holding voice # */

/*
 * GUS bitmasks for built-in mixer control (separate from the ICS or CS chips)
 */

#define GUSMASK_LINE_IN		0x01		/* 0=enable */
#define GUSMASK_LINE_OUT	0x02		/* 0=enable */
#define GUSMASK_MIC_IN		0x04		/* 1=enable */
#define GUSMASK_LATCHES		0x08		/* enable IRQ latches */
#define GUSMASK_COMBINE		0x10		/* combine Ch 1 IRQ & Ch 2 (MIDI) */
#define GUSMASK_MIDI_LOOPBACK	0x20		/* MIDI loopback */
#define GUSMASK_CONTROL_SEL	0x40		/* Select control register */

#define GUSMASK_BOTH_RQ		0x40		/* Combine both RQ lines */

/*
 * GUS bitmasks for DMA control
 */

#define GUSMASK_DMA_ENABLE	0x01		/* Enable DMA transfer */
#define GUSMASK_DMA_READ	0x02		/* 1=read, 0=write */
#define GUSMASK_DMA_WRITE	0x00		/* for consistency */
#define GUSMASK_DMA_WIDTH	0x04		/* Data transfer width */
#define GUSMASK_DMA_R0		0x00		/* Various DMA speeds */
#define GUSMASK_DMA_R1		0x08
#define GUSMASK_DMA_R2		0x10
#define GUSMASK_DMA_R3		0x18
#define GUSMASK_DMA_IRQ		0x20		/* Enable DMA to IRQ */
#define GUSMASK_DMA_IRQPEND	0x40		/* DMA IRQ pending */
#define GUSMASK_DMA_DATA_SIZE	0x40		/* 0=8 bit, 1=16 bit */
#define GUSMASK_DMA_INVBIT	0x80		/* invert high bit */

/*
 * GUS bitmasks for voice control
 */

#define GUSMASK_VOICE_STOPPED	0x01		/* The voice is stopped */
#define GUSMASK_STOP_VOICE	0x02		/* Force voice to stop */
#define GUSMASK_DATA_SIZE16	0x04		/* 1=16 bit, 0=8 bit data */
#define GUSMASK_LOOP_ENABLE	0x08		/* Loop voice at end */
#define	GUSMASK_VOICE_BIDIR	0x10		/* Bi-directional looping */
#define GUSMASK_VOICE_IRQ	0x20		/* Enable the voice IRQ */
#define GUSMASK_INCR_DIR	0x40		/* Direction of address incr */
#define GUSMASK_VOICE_IRQPEND	0x80		/* Pending IRQ for voice */

/*
 * Bitmasks for volume control
 */

#define GUSMASK_VOLUME_STOPPED	0x01		/* Volume ramping stopped */
#define GUSMASK_STOP_VOLUME	0x02		/* Manually stop volume */
#define GUSMASK_VOICE_ROLL	0x04		/* Roll over/low water condition */
#define GUSMASK_VOLUME_LOOP	0x08		/* Volume ramp looping */
#define GUSMASK_VOLUME_BIDIR	0x10		/* Bi-dir volume looping */
#define GUSMASK_VOLUME_IRQ	0x20		/* IRQ on end of volume ramp */
#define GUSMASK_VOLUME_DIR	0x40		/* Direction of volume ramp */
#define GUSMASK_VOLUME_IRQPEND	0x80		/* Pending volume IRQ */
#define MIDI_RESET		0x03

/*
 * ICS Mixer registers
 */

#define GUS_IOH3_OFFSET		0x506
#define GUS_NPORT3		1

#define GUS_MIXER_SELECT	(0x506-GUS_IOH3_OFFSET)		/* read=board rev, wr=mixer */
#define GUS_BOARD_REV		(0x506-GUS_IOH3_OFFSET)
#define GUS_MIXER_DATA		(0x106-GUS_IOH2_OFFSET)		/* data for mixer control */

#define GUSMIX_CHAN_MIC		ICSMIX_CHAN_0
#define GUSMIX_CHAN_LINE	ICSMIX_CHAN_1
#define GUSMIX_CHAN_CD		ICSMIX_CHAN_2
#define GUSMIX_CHAN_DAC		ICSMIX_CHAN_3
#define GUSMIX_CHAN_MASTER	ICSMIX_CHAN_5

/*
 * Codec/Mixer registers
 */

#define GUS_MAX_CODEC_BASE		0x10C
#define GUS_DAUGHTER_CODEC_BASE		0x530
#define GUS_DAUGHTER_CODEC_BASE2	0x604
#define GUS_DAUGHTER_CODEC_BASE3	0xE80
#define GUS_DAUGHTER_CODEC_BASE4	0xF40

#define GUS_CODEC_SELECT	0
#define GUS_CODEC_DATA		1
#define GUS_CODEC_STATUS	2
#define GUS_CODEC_PIO		3

#define GUS_MAX_CTRL		0x106
#define	GUS_MAX_BASEBITS	0xf	/* sets middle nibble of 3X6 */
#define	GUS_MAX_RECCHAN16	0x10	/* 0=8bit DMA read, 1=16bit DMA read */
#define	GUS_MAX_PLAYCHAN16	0x20	/* 0=8bit, 1=16bit */
#define GUS_MAX_CODEC_ENABLE	0x40	/* 0=disable, 1=enable */
