/*
 * linux/sound/soc/pxa/mmp-sspa.h
 *
 * Copyright (C) 2011 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef _MMP_SSPA_H
#define _MMP_SSPA_H

/*
 * SSPA Registers
 */
#define SSPA_RXD		(0x00)
#define SSPA_RXID		(0x04)
#define SSPA_RXCTL		(0x08)
#define SSPA_RXSP		(0x0c)
#define SSPA_RXFIFO_UL		(0x10)
#define SSPA_RXINT_MASK		(0x14)
#define SSPA_RXC		(0x18)
#define SSPA_RXFIFO_NOFS	(0x1c)
#define SSPA_RXFIFO_SIZE	(0x20)

#define SSPA_TXD		(0x80)
#define SSPA_TXID		(0x84)
#define SSPA_TXCTL		(0x88)
#define SSPA_TXSP		(0x8c)
#define SSPA_TXFIFO_LL		(0x90)
#define SSPA_TXINT_MASK		(0x94)
#define SSPA_TXC		(0x98)
#define SSPA_TXFIFO_NOFS	(0x9c)
#define SSPA_TXFIFO_SIZE	(0xa0)

/* SSPA Control Register */
#define	SSPA_CTL_XPH		(1 << 31)	/* Read Phase */
#define	SSPA_CTL_XFIG		(1 << 15)	/* Transmit Zeros when FIFO Empty */
#define	SSPA_CTL_JST		(1 << 3)	/* Audio Sample Justification */
#define	SSPA_CTL_XFRLEN2_MASK	(7 << 24)
#define	SSPA_CTL_XFRLEN2(x)	((x) << 24)	/* Transmit Frame Length in Phase 2 */
#define	SSPA_CTL_XWDLEN2_MASK	(7 << 21)
#define	SSPA_CTL_XWDLEN2(x)	((x) << 21)	/* Transmit Word Length in Phase 2 */
#define	SSPA_CTL_XDATDLY(x)	((x) << 19)	/* Tansmit Data Delay */
#define	SSPA_CTL_XSSZ2_MASK	(7 << 16)
#define	SSPA_CTL_XSSZ2(x)	((x) << 16)	/* Transmit Sample Audio Size */
#define	SSPA_CTL_XFRLEN1_MASK	(7 << 8)
#define	SSPA_CTL_XFRLEN1(x)	((x) << 8)	/* Transmit Frame Length in Phase 1 */
#define	SSPA_CTL_XWDLEN1_MASK	(7 << 5)
#define	SSPA_CTL_XWDLEN1(x)	((x) << 5)	/* Transmit Word Length in Phase 1 */
#define	SSPA_CTL_XSSZ1_MASK	(7 << 0)
#define	SSPA_CTL_XSSZ1(x)	((x) << 0)	/* XSSZ1 */

#define SSPA_CTL_8_BITS		(0x0)		/* Sample Size */
#define SSPA_CTL_12_BITS	(0x1)
#define SSPA_CTL_16_BITS	(0x2)
#define SSPA_CTL_20_BITS	(0x3)
#define SSPA_CTL_24_BITS	(0x4)
#define SSPA_CTL_32_BITS	(0x5)

/* SSPA Serial Port Register */
#define	SSPA_SP_WEN		(1 << 31)	/* Write Configuration Enable */
#define	SSPA_SP_MSL		(1 << 18)	/* Master Slave Configuration */
#define	SSPA_SP_CLKP		(1 << 17)	/* CLKP Polarity Clock Edge Select */
#define	SSPA_SP_FSP		(1 << 16)	/* FSP Polarity Clock Edge Select */
#define	SSPA_SP_FFLUSH		(1 << 2)	/* FIFO Flush */
#define	SSPA_SP_S_RST		(1 << 1)	/* Active High Reset Signal */
#define	SSPA_SP_S_EN		(1 << 0)	/* Serial Clock Domain Enable */
#define	SSPA_SP_FWID(x)		((x) << 20)	/* Frame-Sync Width */
#define	SSPA_TXSP_FPER(x)	((x) << 4)	/* Frame-Sync Active */

/* sspa clock sources */
#define MMP_SSPA_CLK_PLL	0
#define MMP_SSPA_CLK_VCXO	1
#define MMP_SSPA_CLK_AUDIO	3

/* sspa pll id */
#define MMP_SYSCLK		0
#define MMP_SSPA_CLK		1

#endif /* _MMP_SSPA_H */
