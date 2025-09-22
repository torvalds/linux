/*	$OpenBSD: pciide_acer_reg.h,v 1.8 2010/07/23 07:47:13 jsg Exp $	*/
/*	$NetBSD: pciide_acer_reg.h,v 1.4 2001/07/26 20:02:22 bouyer Exp $	*/

/*
 * Copyright (c) 1999 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _DEV_PCI_PCIIDE_ACER_REG_H_
#define _DEV_PCI_PCIIDE_ACER_REG_H_

/*  class code attribute register 1 (1 byte) */
#define ACER_CCAR1	0x43
#define ACER_CHANSTATUS_RO            0x40
#define PCIIDE_CHAN_RO(chan)            (0x20 >> (chan))

/* from Linux, 80 pins cable detect */
#define ACER_0x4A	0x4a
/*
 * bit 0 is 0 -> primary has 80 pin cable
 * bit 1 is 0 -> secondary has 80 pin cable
 */
#define ACER_0x4A_80PIN(chan)	(0x1 << (chan))

/* From FreeBSD, for UDMA mode > 2 */
#define ACER_0x4B	0x4b
#define ACER_0x4B_UDMA66	0x01
/* From Linux */
#define ACER_0x4B_CDETECT	0x08

/* class code attribute register 2 (1 byte) */
#define ACER_CCAR2	0x4d
#define ACER_CHANSTATUSREGS_RO 0x80

/* class code attribute register 3 (1 byte) */
#define ACER_CCAR3	0x50
#define ACER_CCAR3_PI	0x02

/* flexible channel setting register */
#define ACER_FCS	0x52
#define ACER_FCS_TIMREG(chan,drv)	((0x8) >> ((drv) + (chan) * 2))

/* CD-ROM control register */
#define ACER_CDRC	0x53
#define ACER_CDRC_FIFO_DISABLE	0x02
#define ACER_CDRC_DMA_EN	0x01

/* Fifo threshold and Ultra-DMA settings (4 bytes). */
#define ACER_FTH_UDMA	0x54
#define ACER_FTH_VAL(chan, drv, val) \
	(((val) & 0x3) << ((drv) * 4 + (chan) * 8))
#define ACER_FTH_OPL(chan, drv, val) \
	(((val) & 0x3) << (2 + (drv) * 4 + (chan) * 8))
#define ACER_UDMA_EN(chan, drv) \
	(0x8 << (16 + (drv) * 4 + (chan) * 8))
#define ACER_UDMA_TIM(chan, drv, val) \
	(((val) & 0x7) << (16 + (drv) * 4 + (chan) * 8))

/* drives timings setup (1 byte) */
#define ACER_IDETIM(chan, drv) (0x5a + (drv) + (chan) * 4)

/* IRQ and drive select status */
#define ACER_CHIDS	0x75
#define ACER_CHIDS_DRV(channel)	((0x4) << (channel))
#define ACER_CHIDS_INT(channel)	((0x1) << (channel))

/* Linux: south-bridge's enable bit (m1533) */
#define ACER_0x79	0x79
#define ACER_0x79_REVC2_EN	0x4
#define ACER_0x79_EN		0x2

/*
 * IDE bus frequency (1 byte)
 * This should be setup by the BIOS - can we rely on this ?
 */
#define ACER_IDE_CLK	0x78

/* acer UDMA3/4/5 from FreeBSD */
static int8_t acer_udma[] = {0x4, 0x3, 0x2, 0x1, 0x0, 0x7};
static int8_t acer_pio[] = {0x0c, 0x58, 0x44, 0x33, 0x31};
#ifdef unused
static int8_t acer_dma[] = {0x08, 0x33, 0x31};
#endif

#endif	/* !_DEV_PCI_PCIIDE_ACER_REG_H_ */
