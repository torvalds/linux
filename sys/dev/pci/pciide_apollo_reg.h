/*	$OpenBSD: pciide_apollo_reg.h,v 1.10 2010/07/23 07:47:13 jsg Exp $	*/
/*	$NetBSD: pciide_apollo_reg.h,v 1.8 2001/01/05 18:04:43 bouyer Exp $	*/

/*
 * Copyright (c) 1998 Manuel Bouyer.
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

#ifndef _DEV_PCI_PCIIDE_APOLLO_REG_H_
#define _DEV_PCI_PCIIDE_APOLLO_REG_H_

/*
 * Registers definitions for VIA technologies's Apollo controllers (VT82V580VO,
 * VT82C586A and VT82C586B).
 *
 * UDMA1/2/3/4 capable
 * http://www.via.com.tw/pdf/productinfo/686a.pdf
 * http://www.via.com.tw/pdf/productinfo/596b.pdf
 *
 * UDMA1/2 capable
 * http://www.via.com.tw/pdf/productinfo/586b.pdf
 * http://www.via.com.tw/pdf/productinfo/586a.pdf
 */

/* misc. configuration registers */
#define APO_IDECONF 0x40
#define APO_IDECONF_EN(channel) (0x00000001 << (1 - (channel)))
#define APO_IDECONF_SERR_EN	0x00000100 /* 580 only */
#define APO_IDECONF_DS_SOURCE	0x00000200 /* 580 only */
#define APO_IDECONF_ALT_INTR_EN	0x00000400 /* 580 only */
#define APO_IDECONF_PERR_EN	0x00000800 /* 580 only */
#define APO_IDECONF_WR_BUFF_EN(channel) (0x00001000 << ((1 - (channel)) << 1))
#define APO_IDECONF_RD_PREF_EN(channel) (0x00002000 << ((1 - (channel)) << 1))
#define APO_IDECONF_DEVSEL_TME	0x00010000 /* 580 only */
#define APO_IDECONF_MAS_CMD_MON	0x00020000 /* 580 only */
#define APO_IDECONF_IO_NAT(channel) \
	(0x00400000 << (1 - (channel))) /* 580 only */
#define APO_IDECONF_FIFO_TRSH(channel, x) \
	((x) & 0x3) << ((1 - (channel)) << 1 + 24)
#define APO_IDECONF_FIFO_CONF_MASK 0x60000000

/* Misc. controls register */
#define APO_CTLMISC 0x44
#define APO_CTLMISC_BM_STS_RTY	0x00000008
#define APO_CTLMISC_FIFO_HWS	0x00000010
#define APO_CTLMISC_WR_IRDY_WS	0x00000020
#define APO_CTLMISC_RD_IRDY_WS	0x00000040
#define APO_CTLMISC_INTR_SWP	0x00004000
#define APO_CTLMISC_DRDY_TIME_MASK 0x00030000
#define APO_CTLMISC_FIFO_FLSH_RD(channel) (0x00100000 << (1 - (channel)))
#define APO_CTLMISC_FIFO_FLSH_DMA(channel) (0x00400000 << (1 - (channel)))

/* data port timings controls */
#define APO_DATATIM 0x48
#define APO_DATATIM_MASK(channel) (0xffff << ((1 - (channel)) << 4))
#define APO_DATATIM_RECOV(channel, drive, x) (((x) & 0xf) << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3)))
#define APO_DATATIM_PULSE(channel, drive, x) (((x) & 0xf) << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3) + 4))

/* misc timings control */
#define APO_MISCTIM 0x4c

/* UltraDMA control (586A/B and higher only) */
#define APO_UDMA 0x50
#define APO_UDMA_MASK(channel) (0xffff << ((1 - (channel)) << 4))
#define APO_UDMA_TIME(channel, drive, x) (((x) & 0xf) << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3)))
#define APO_UDMA_PIO_MODE(channel, drive) (0x20 << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3)))
#define APO_UDMA_EN(channel, drive) (0x40 << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3)))
#define APO_UDMA_EN_MTH(channel, drive) (0x80 << \
	(((1 - (channel)) << 4) + ((1 - (drive)) << 3)))
#define APO_UDMA_CLK66(channel) (0x08 << ((1 - (channel)) << 4))

static int8_t apollo_udma133_tim[] = {0x07, 0x07, 0x06, 0x04, 0x02, 0x01, 0x00};
static int8_t apollo_udma100_tim[] = {0x07, 0x07, 0x04, 0x02, 0x01, 0x00};
static int8_t apollo_udma66_tim[] = {0x03, 0x03, 0x02, 0x01, 0x00};
static int8_t apollo_udma33_tim[] = {0x03, 0x02, 0x00};
static int8_t apollo_pio_set[] = {0x0a, 0x0a, 0x0a, 0x02, 0x02};
static int8_t apollo_pio_rec[] = {0x08, 0x08, 0x08, 0x02, 0x00};

#endif	/* !_DEV_PCI_PCIIDE_APOLLO_REG_H_ */
