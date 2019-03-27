/*	$OpenBSD: if_rtwnreg.h,v 1.3 2015/06/14 08:02:47 stsp Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * $FreeBSD$
 */

#ifndef R92CE_REG_H
#define R92CE_REG_H

#include <dev/rtwn/rtl8192c/r92c_reg.h>

/*
 * MAC registers.
 */
/* System Configuration. */
#define R92C_PCIE_MIO_INTF		0x0e4
#define R92C_PCIE_MIO_INTD		0x0e8
/* PCIe Configuration. */
#define R92C_PCIE_CTRL_REG		0x300
#define R92C_INT_MIG			0x304
#define R92C_BCNQ_DESA			0x308
#define R92C_HQ_DESA			0x310
#define R92C_MGQ_DESA			0x318
#define R92C_VOQ_DESA			0x320
#define R92C_VIQ_DESA			0x328
#define R92C_BEQ_DESA			0x330
#define R92C_BKQ_DESA			0x338
#define R92C_RX_DESA			0x340
#define R92C_DBI			0x348
#define R92C_MDIO			0x354
#define R92C_DBG_SEL			0x360
#define R92C_PCIE_HRPWM			0x361
#define R92C_PCIE_HCPWM			0x363
#define R92C_UART_CTRL			0x364
#define R92C_UART_TX_DES		0x370
#define R92C_UART_RX_DES		0x378


/* Bits for R92C_GPIO_MUXCFG. */
#define R92C_GPIO_MUXCFG_RFKILL		0x0008

/* Bits for R92C_GPIO_IO_SEL. */
#define R92C_GPIO_IO_SEL_RFKILL		0x0008

/* Bits for R92C_LEDCFG2. */
#define R92C_LEDCFG2_EN			0x60
#define R92C_LEDCFG2_DIS		0x68

/* Bits for R92C_HIMR. */
#define R92C_IMR_ROK		0x00000001	/* receive DMA OK */
#define R92C_IMR_VODOK		0x00000002	/* AC_VO DMA OK */
#define R92C_IMR_VIDOK		0x00000004	/* AC_VI DMA OK */
#define R92C_IMR_BEDOK		0x00000008	/* AC_BE DMA OK */
#define R92C_IMR_BKDOK		0x00000010	/* AC_BK DMA OK */
#define R92C_IMR_TXBDER		0x00000020	/* beacon transmit error */
#define R92C_IMR_MGNTDOK	0x00000040	/* management queue DMA OK */
#define R92C_IMR_TBDOK		0x00000080	/* beacon transmit OK */
#define R92C_IMR_HIGHDOK	0x00000100	/* high queue DMA OK */
#define R92C_IMR_BDOK		0x00000200	/* beacon queue DMA OK */
#define R92C_IMR_ATIMEND	0x00000400	/* ATIM window end interrupt */
#define R92C_IMR_RDU		0x00000800	/* Rx descriptor unavailable */
#define R92C_IMR_RXFOVW		0x00001000	/* receive FIFO overflow */
#define R92C_IMR_BCNINT		0x00002000	/* beacon DMA interrupt 0 */
#define R92C_IMR_PSTIMEOUT	0x00004000	/* powersave timeout */
#define R92C_IMR_TXFOVW		0x00008000	/* transmit FIFO overflow */
#define R92C_IMR_TIMEOUT1	0x00010000	/* timeout interrupt 1 */
#define R92C_IMR_TIMEOUT2	0x00020000	/* timeout interrupt 2 */
#define R92C_IMR_BCNDOK1	0x00040000	/* beacon queue DMA OK (1) */
#define R92C_IMR_BCNDOK2	0x00080000	/* beacon queue DMA OK (2) */
#define R92C_IMR_BCNDOK3	0x00100000	/* beacon queue DMA OK (3) */
#define R92C_IMR_BCNDOK4	0x00200000	/* beacon queue DMA OK (4) */
#define R92C_IMR_BCNDOK5	0x00400000	/* beacon queue DMA OK (5) */
#define R92C_IMR_BCNDOK6	0x00800000	/* beacon queue DMA OK (6) */
#define R92C_IMR_BCNDOK7	0x01000000	/* beacon queue DMA OK (7) */
#define R92C_IMR_BCNDOK8	0x02000000	/* beacon queue DMA OK (8) */
#define R92C_IMR_BCNDMAINT1	0x04000000	/* beacon DMA interrupt 1 */
#define R92C_IMR_BCNDMAINT2	0x08000000	/* beacon DMA interrupt 2 */
#define R92C_IMR_BCNDMAINT3	0x10000000	/* beacon DMA interrupt 3 */
#define R92C_IMR_BCNDMAINT4	0x20000000	/* beacon DMA interrupt 4 */
#define R92C_IMR_BCNDMAINT5	0x40000000	/* beacon DMA interrupt 5 */
#define R92C_IMR_BCNDMAINT6	0x80000000	/* beacon DMA interrupt 6 */

/* Shortcut. */
#define R92C_IBSS_INT_MASK	\
	(R92C_IMR_BCNINT | R92C_IMR_TBDOK | R92C_IMR_TBDER)

#endif	/* R92CE_REG_H */
