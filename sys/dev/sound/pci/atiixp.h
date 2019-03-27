/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ariff Abdullah <ariff@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _ATIIXP_H_
#define _ATIIXP_H_

/*
 * Constants, pretty much FreeBSD specific.
 */
 
/* Number of playback / recording channel */
#define ATI_IXP_NPCHAN		1
#define ATI_IXP_NRCHAN		1
#define ATI_IXP_NCHANS		(ATI_IXP_NPCHAN + ATI_IXP_NRCHAN)

/*
 * Maximum segments/descriptors is 256, but 2 for
 * each channel should be more than enough for us.
 */
#define ATI_IXP_DMA_CHSEGS	2
#define ATI_IXP_DMA_CHSEGS_MIN	2
#define ATI_IXP_DMA_CHSEGS_MAX	256

#define ATI_VENDOR_ID		0x1002	/* ATI Technologies */

#define ATI_IXP_200_ID		0x4341
#define ATI_IXP_300_ID		0x4361
#define ATI_IXP_400_ID		0x4370
#define ATI_IXP_SB600_ID	0x4382

#define ATI_IXP_BASE_RATE	48000

/* 
 * Register definitions for ATI IXP
 *
 * References: ALSA snd-atiixp.c , OpenBSD/NetBSD auixp-*.h
 */

#define ATI_IXP_CODECS 3

#define ATI_REG_ISR			0x00		/* interrupt source */
#define  ATI_REG_ISR_IN_XRUN		(1U<<0)
#define  ATI_REG_ISR_IN_STATUS		(1U<<1)
#define  ATI_REG_ISR_OUT_XRUN		(1U<<2)
#define  ATI_REG_ISR_OUT_STATUS		(1U<<3)
#define  ATI_REG_ISR_SPDF_XRUN		(1U<<4)
#define  ATI_REG_ISR_SPDF_STATUS	(1U<<5)
#define  ATI_REG_ISR_PHYS_INTR		(1U<<8)
#define  ATI_REG_ISR_PHYS_MISMATCH	(1U<<9)
#define  ATI_REG_ISR_CODEC0_NOT_READY	(1U<<10)
#define  ATI_REG_ISR_CODEC1_NOT_READY	(1U<<11)
#define  ATI_REG_ISR_CODEC2_NOT_READY	(1U<<12)
#define  ATI_REG_ISR_NEW_FRAME		(1U<<13)

#define ATI_REG_IER			0x04		/* interrupt enable */
#define  ATI_REG_IER_IN_XRUN_EN		(1U<<0)
#define  ATI_REG_IER_IO_STATUS_EN	(1U<<1)
#define  ATI_REG_IER_OUT_XRUN_EN	(1U<<2)
#define  ATI_REG_IER_OUT_XRUN_COND	(1U<<3)
#define  ATI_REG_IER_SPDF_XRUN_EN	(1U<<4)
#define  ATI_REG_IER_SPDF_STATUS_EN	(1U<<5)
#define  ATI_REG_IER_PHYS_INTR_EN	(1U<<8)
#define  ATI_REG_IER_PHYS_MISMATCH_EN	(1U<<9)
#define  ATI_REG_IER_CODEC0_INTR_EN	(1U<<10)
#define  ATI_REG_IER_CODEC1_INTR_EN	(1U<<11)
#define  ATI_REG_IER_CODEC2_INTR_EN	(1U<<12)
#define  ATI_REG_IER_NEW_FRAME_EN	(1U<<13)	/* (RO) */
#define  ATI_REG_IER_SET_BUS_BUSY	(1U<<14)	/* (WO) audio is running */

#define ATI_REG_CMD			0x08		/* command */
#define  ATI_REG_CMD_POWERDOWN		(1U<<0)
#define  ATI_REG_CMD_RECEIVE_EN		(1U<<1)
#define  ATI_REG_CMD_SEND_EN		(1U<<2)
#define  ATI_REG_CMD_STATUS_MEM		(1U<<3)
#define  ATI_REG_CMD_SPDF_OUT_EN	(1U<<4)
#define  ATI_REG_CMD_SPDF_STATUS_MEM	(1U<<5)
#define  ATI_REG_CMD_SPDF_THRESHOLD	(3U<<6)
#define  ATI_REG_CMD_SPDF_THRESHOLD_SHIFT	6
#define  ATI_REG_CMD_IN_DMA_EN		(1U<<8)
#define  ATI_REG_CMD_OUT_DMA_EN		(1U<<9)
#define  ATI_REG_CMD_SPDF_DMA_EN	(1U<<10)
#define  ATI_REG_CMD_SPDF_OUT_STOPPED	(1U<<11)
#define  ATI_REG_CMD_SPDF_CONFIG_MASK	(7U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_34	(1U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_78	(2U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_69	(3U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_01	(4U<<12)
#define  ATI_REG_CMD_INTERLEAVE_SPDF	(1U<<16)
#define  ATI_REG_CMD_AUDIO_PRESENT	(1U<<20)
#define  ATI_REG_CMD_INTERLEAVE_IN	(1U<<21)
#define  ATI_REG_CMD_INTERLEAVE_OUT	(1U<<22)
#define  ATI_REG_CMD_LOOPBACK_EN	(1U<<23)
#define  ATI_REG_CMD_PACKED_DIS		(1U<<24)
#define  ATI_REG_CMD_BURST_EN		(1U<<25)
#define  ATI_REG_CMD_PANIC_EN		(1U<<26)
#define  ATI_REG_CMD_MODEM_PRESENT	(1U<<27)
#define  ATI_REG_CMD_ACLINK_ACTIVE	(1U<<28)
#define  ATI_REG_CMD_AC_SOFT_RESET	(1U<<29)
#define  ATI_REG_CMD_AC_SYNC		(1U<<30)
#define  ATI_REG_CMD_AC_RESET		(1U<<31)

#define ATI_REG_PHYS_OUT_ADDR		0x0c
#define  ATI_REG_PHYS_OUT_CODEC_MASK	(3U<<0)
#define  ATI_REG_PHYS_OUT_RW		(1U<<2)
#define  ATI_REG_PHYS_OUT_ADDR_EN	(1U<<8)
#define  ATI_REG_PHYS_OUT_ADDR_SHIFT	9
#define  ATI_REG_PHYS_OUT_DATA_SHIFT	16

#define ATI_REG_PHYS_IN_ADDR		0x10
#define  ATI_REG_PHYS_IN_READ_FLAG	(1U<<8)
#define  ATI_REG_PHYS_IN_ADDR_SHIFT	9
#define  ATI_REG_PHYS_IN_DATA_SHIFT	16

#define ATI_REG_SLOTREQ			0x14

#define ATI_REG_COUNTER			0x18
#define  ATI_REG_COUNTER_SLOT		(3U<<0)		/* slot # */
#define  ATI_REG_COUNTER_BITCLOCK	(31U<<8)

#define ATI_REG_IN_FIFO_THRESHOLD	0x1c

#define ATI_REG_IN_DMA_LINKPTR		0x20
#define ATI_REG_IN_DMA_DT_START		0x24		/* RO */
#define ATI_REG_IN_DMA_DT_NEXT		0x28		/* RO */
#define ATI_REG_IN_DMA_DT_CUR		0x2c		/* RO */
#define ATI_REG_IN_DMA_DT_SIZE		0x30

#define ATI_REG_OUT_DMA_SLOT		0x34
#define  ATI_REG_OUT_DMA_SLOT_BIT(x)	(1U << ((x) - 3))
#define  ATI_REG_OUT_DMA_SLOT_MASK	0x1ff
#define  ATI_REG_OUT_DMA_THRESHOLD_MASK	0xf800
#define  ATI_REG_OUT_DMA_THRESHOLD_SHIFT	11

#define ATI_REG_OUT_DMA_LINKPTR		0x38
#define ATI_REG_OUT_DMA_DT_START	0x3c		/* RO */
#define ATI_REG_OUT_DMA_DT_NEXT		0x40		/* RO */
#define ATI_REG_OUT_DMA_DT_CUR		0x44		/* RO */
#define ATI_REG_OUT_DMA_DT_SIZE		0x48

#define ATI_REG_SPDF_CMD		0x4c
#define  ATI_REG_SPDF_CMD_LFSR		(1U<<4)
#define  ATI_REG_SPDF_CMD_SINGLE_CH	(1U<<5)
#define  ATI_REG_SPDF_CMD_LFSR_ACC	(0xff<<8)	/* RO */

#define ATI_REG_SPDF_DMA_LINKPTR	0x50
#define ATI_REG_SPDF_DMA_DT_START	0x54		/* RO */
#define ATI_REG_SPDF_DMA_DT_NEXT	0x58		/* RO */
#define ATI_REG_SPDF_DMA_DT_CUR		0x5c		/* RO */
#define ATI_REG_SPDF_DMA_DT_SIZE	0x60

#define ATI_REG_MODEM_MIRROR		0x7c
#define ATI_REG_AUDIO_MIRROR		0x80

#define ATI_REG_6CH_REORDER		0x84		/* reorder slots for 6ch */
#define  ATI_REG_6CH_REORDER_EN		(1U<<0)		/* 3,4,7,8,6,9 -> 3,4,6,9,7,8 */

#define ATI_REG_FIFO_FLUSH		0x88
#define  ATI_REG_FIFO_OUT_FLUSH		(1U<<0)
#define  ATI_REG_FIFO_IN_FLUSH		(1U<<1)

/* LINKPTR */
#define  ATI_REG_LINKPTR_EN		(1U<<0)

/* [INT|OUT|SPDIF]_DMA_DT_SIZE */
#define  ATI_REG_DMA_DT_SIZE		(0xffffU<<0)
#define  ATI_REG_DMA_FIFO_USED		(0x1fU<<16)
#define  ATI_REG_DMA_FIFO_FREE		(0x1fU<<21)
#define  ATI_REG_DMA_STATE		(7U<<26)

#define ATI_MAX_DESCRIPTORS	256	/* max number of descriptor packets */

/* codec detection constant indicating the interrupt flags */
#define ALL_CODECS_NOT_READY \
    (ATI_REG_ISR_CODEC0_NOT_READY | ATI_REG_ISR_CODEC1_NOT_READY |\
     ATI_REG_ISR_CODEC2_NOT_READY)
#define CODEC_CHECK_BITS (ALL_CODECS_NOT_READY|ATI_REG_ISR_NEW_FRAME)

#endif
