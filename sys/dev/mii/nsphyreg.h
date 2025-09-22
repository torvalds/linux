/*	$OpenBSD: nsphyreg.h,v 1.4 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD: nsphyreg.h,v 1.1 1998/08/10 23:58:39 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

#ifndef _DEV_MII_NSPHYREG_H_
#define	_DEV_MII_NSPHYREG_H_

/*
 * National Semiconductor DP83840 Ethernet PHY register definitions
 * Further documentation can be found at:
 *	http://www.national.com/pf/DP/DP83840A.html
 */

#define	MII_NSPHY_DCR		0x12	/* Disconnect counter */

#define	MII_NSPHY_FCSCR		0x13	/* False carrier sense counter */

#define	MII_NSPHY_RECR		0x15	/* Receive error counter */

#define	MII_NSPHY_SRR		0x16	/* Silicon revision */

#define	MII_NSPHY_PCR		0x17	/* PCS sub-layer configuration */
#define	PCR_NRZI		0x8000	/* NRZI encoding enabled for 100TX */
#define	PCR_DESCRTOSEL		0x4000	/* descrambler t/o select (2ms) */
#define	PCR_DESCRTODIS		0x2000	/* descrambler t/o disable */
#define	PCR_REPEATER		0x1000	/* repeater mode */
#define	PCR_ENCSEL		0x0800	/* encoder mode select */
#define	PCR_TXREADYSEL		0x0400	/* use internal txrdy signal */
#define	PCR_CONGCTRL		0x0100	/* congestion control */
#define	PCR_CLK25MDIS		0x0080	/* CLK25M disable */
#define	PCR_FLINK100		0x0040	/* force good link in 100mbps */
#define	PCR_CIMDIS		0x0020	/* carrier integrity monitor disable */
#define	PCR_TXOFF		0x0010	/* force transmit off */
#define	PCR_LED1MODE		0x0004	/* LED1 mode: see below */
#define	PCR_LED4MODE		0x0002	/* LED4 mode: see below */

/*
 * LED1 Mode:
 *
 *	1	LED1 output configured to PAR's CON_STATUS, useful for
 *		network management in 100baseTX mode.
 *
 *	0	Normal LED1 operation - 10baseTX and 100baseTX transmission
 *		activity.
 *
 * LED4 Mode:
 *
 *	1	LED4 output configured to indicate full-duplex in both
 *		10baseT and 100baseTX modes.
 *
 *	0	LED4 output configured to indicate polarity in 10baseT
 *		mode and full-duplex in 100baseTX mode.
 */

#define	MII_NSPHY_LBREMR	0x18	/* Loopback, bypass, error mask */
#define	LBREMR_BADSSDEN		0x8000	/* enable bad SSD detection */
#define	LBREMR_BP4B5B		0x4000	/* bypass 4b/5b encoding */
#define	LBREMR_BPSCR		0x2000	/* bypass scrambler */
#define	LBREMR_BPALIGN		0x1000	/* bypass alignment function */
#define	LBREMR_10LOOP		0x0800	/* 10baseT loopback */
#define	LBREMR_LB1		0x0200	/* loopback ctl 1 */
#define	LBREMR_LB0		0x0100	/* loopback ctl 0 */
#define	LBREMR_ALTCRS		0x0040	/* alt crs operation */
#define	LBREMR_LOOPXMTDIS	0x0020	/* disable transmit in 100TX loopbk */
#define	LBREMR_CODEERR		0x0010	/* code errors */
#define	LBREMR_PEERR		0x0008	/* premature end errors */
#define	LBREMR_LINKERR		0x0004	/* link errors */
#define	LBREMR_PKTERR		0x0002	/* packet errors */

#define	MII_NSPHY_PAR		0x19	/* Physical address and status */
#define	PAR_DISCRSJAB		0x0800	/* disable car sense during jab */
#define	PAR_ANENSTAT		0x0400	/* autoneg mode status */
#define	PAR_FEFIEN		0x0100	/* far end fault enable */
#define	PAR_FDX			0x0080	/* full duplex status */
#define	PAR_10			0x0040	/* 10mbps mode */
#define	PAR_CON			0x0020	/* connect status */
#define	PAR_AMASK		0x001f	/* PHY address bits */

#define	MII_NSPHY_10BTSR	0x1b	/* 10baseT status */
#define	MII_NSPHY_10BTCR	0x1c	/* 10baseT configuration */

#endif /* _DEV_MII_NSPHYREG_H_ */
