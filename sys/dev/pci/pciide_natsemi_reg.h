/*	$OpenBSD: pciide_natsemi_reg.h,v 1.9 2022/08/29 06:08:04 jsg Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_PCIIDE_NATSEMI_REG_H_
#define _DEV_PCI_PCIIDE_NATSEMI_REG_H_

/*
 * Register definitions for National Semiconductor PC87415.  Definitions
 * based on "PC87415: PCI-IDE DMA Master Mode Interface Controller"
 * (March 1996) datasheet from their website.
 */

#define	NATSEMI_CTRL1	0x40	/* Control register1 */
#define	NATSEMI_CTRL1_SWRST	0x04		/* sw rst to ch1/ch2 on */
#define	NATSEMI_CTRL1_IDEPWR	0x08
#define	NATSEMI_CTRL1_CH1INTMAP	0x10
#define	NATSEMI_CTRL1_CH2INTMAP	0x20
#define	NATSEMI_CTRL1_INTAMASK	0x40
#define	NATSEMI_CTRL1_IDWR	0x80		/* write to did/vid enable */

#define	NATSEMI_CTRL2	0x41	/* Control register2 */
#define	NATSEMI_CTRL2_CH1MASK	0x01		/* channel 1 intr masked */
#define	NATSEMI_CTRL2_CH2MASK	0x02		/* channel 2 intr masked */
#define	NATSEMI_CTRL2_BARDIS	0x04		/* PCI BAR 2/3 disable */
#define	NATSEMI_CTRL2_WATCHDOG	0x08		/* enable watchdog timer */
#define	NATSEMI_CTRL2_BUF1BYP	0x10		/* bypass buffer 1 */
#define	NATSEMI_CTRL2_BUF2BYP	0x20		/* bypass buffer 2 */
#define	NATSEMI_CTRL2_IDE1MAP	0x40		/* IDE at bar 1 */
#define	NATSEMI_CTRL2_IDE2MAP	0x80		/* IDE at bar 2 */

#define	NATSEMI_CHMASK(chn)	(NATSEMI_CTRL2_CH1MASK << (chn))

#define	NATSEMI_CTRL3	0x42	/* Control register3 */
#define	NATSEMI_CTRL3_CH1PREDIS	0x01		/* channel 1 prefetch disable */
#define	NATSEMI_CTRL3_CH2PREDIS	0x02		/* channel 2 prefetch disable */
#define	NATSEMI_CTRL3_RSTIDLE	0x04		/* reset idle state */
#define	NATSEMI_CTRL3_C1D1DMARQ	0x10		/* c1d1 dmarq handshaking */
#define	NATSEMI_CTRL3_C1D2DMARQ	0x20		/* c1d2 dmarq handshaking */
#define	NATSEMI_CTRL3_C2D1DMARQ	0x40		/* c2d1 dmarq handshaking */
#define	NATSEMI_CTRL3_C2D2DMARQ	0x80		/* c2d2 dmarq handshaking */

#define	NATSEMI_WBS	0x43	/* Write buffer status */
#define	NATSEMI_WBS_WB1NMPTY	0x01		/* chan 1 write buf not empty */
#define	NATSEMI_WBS_WB2NMPTY	0x02		/* chan 2 write buf not empty */

#define	NATSEMI_C1D1DRT	0x44	/* Channel 1/device 1 data read timing */
#define	NATSEMI_C1D1DWT	0x45	/* Channel 1/device 1 data write timing */
#define	NATSEMI_C1D2DRT	0x48	/* Channel 1/device 2 data read timing */
#define	NATSEMI_C1D2DWT	0x49	/* Channel 1/device 2 data write timing */
#define	NATSEMI_C2D1DRT	0x4c	/* Channel 2/device 1 data read timing */
#define	NATSEMI_C2D1DWT	0x4d	/* Channel 2/device 1 data write timing */
#define	NATSEMI_C2D2DRT	0x50	/* Channel 2/device 2 data read timing */
#define	NATSEMI_C2D2DWT	0x51	/* Channel 2/device 2 data write timing */

#define	NATSEMI_CCBT	0x54	/* Command and control block timing */

#define	NATSEMI_SECT	0x55	/* Sector size */
#define	NATSEMI_SECT_C1UNUSED	0x0f		/* not used */
#define	NATSEMI_SECT_C1_512	0x0e		/* 512 bytes */
#define	NATSEMI_SECT_C1_1024	0x0c		/* 1024 bytes */
#define	NATSEMI_SECT_C1_2048	0x08		/* 2048 bytes */
#define	NATSEMI_SECT_C1_4096	0x00		/* 4096 bytes */
#define	NATSEMI_SECT_C2UNUSED	0xf0		/* not used */
#define	NATSEMI_SECT_C2_512	0xe0		/* 512 bytes */
#define	NATSEMI_SECT_C2_1024	0xc0		/* 1024 bytes */
#define	NATSEMI_SECT_C2_2048	0x80		/* 2048 bytes */
#define	NATSEMI_SECT_C2_4096	0x00		/* 4096 bytes */

#define	NATSEMI_RTREG(c,d)	(0x44 + (c * 8) + (d * 4) + 0)
#define	NATSEMI_WTREG(c,d)	(0x44 + (c * 8) + (d * 4) + 1)

/* 17 - N = number of clocks */
static u_int8_t natsemi_pio_pulse[] =	{ 7, 12, 13, 14, 14 };
static u_int8_t natsemi_dma_pulse[] =	{ 7, 10, 10 };
/* 16 - N = number of clocks */
static u_int8_t natsemi_pio_recover[] =	{ 6,  8, 11, 13, 15 };
static u_int8_t natsemi_dma_recover[] =	{ 6,  8,  9 };


/*
 * Register definitions for National Semiconductor SCx200 IDE found
 * on Geode SC1100 IAOC.
 */
#define SCx200_TIM_PIO(chan, drive)	(0x40 + 16 * (chan) + 8 * (drive))
#define SCx200_TIM_DMA(chan, drive)	(0x44 + 16 * (chan) + 8 * (drive))

#define SCx200_PIOFORMAT_SHIFT		31

/* PIO mode timings */
static const u_int32_t scx200_pio33[2][5] = {
    /* Format 0 */
    { 0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010 },
    /* Format 1 */
    { 0x9172d132, 0x21717121, 0x00803020, 0x20102010, 0x00100010 }};
static const u_int32_t scx200_pio66[2][5] = {
    /* Format 0 */
    { 0x0000f8e4, 0x000153f3, 0x000213f1, 0x00034231, 0x00041131 },
    /* Format 1 */
    { 0xf8e4f8e4, 0x53f3f353, 0x13f18141, 0x42314231, 0x11311131 }};

/* DMA mode timings */
static const u_int32_t scx200_dma33[] = { 0x00077771, 0x00012121, 0x00002020 };
static const u_int32_t scx200_dma66[] = { 0x000ffff3, 0x00035352, 0x00015151 };

/* UDMA mode timings */
static const u_int32_t scx200_udma33[] = { 0x00921250, 0x00911140, 0x00911030 };
static const u_int32_t scx200_udma66[] = { 0x009436a1, 0x00933481, 0x00923261 };

#endif	/* !_DEV_PCI_PCIIDE_NATSEMI_REG_H_ */
