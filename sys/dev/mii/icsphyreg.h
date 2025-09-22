/*	$OpenBSD: icsphyreg.h,v 1.3 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD: icsphyreg.h,v 1.1 1998/11/02 23:46:20 thorpej Exp $	*/

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

#ifndef _DEV_MII_ICSPHYREG_H_
#define	_DEV_MII_ICSPHYREG_H_

/*
 * Register definitions for the ICS1890 Ethernet PHY
 * Further documentation can be found at:
 *	http://www.icst.com/products/pinfo/1890.htm
 */

#define	MII_ICSPHY_ECR		0x10	/* Extended Control Register */
#define	ECR_OVR			0x8000	/* disable command reg overwrites */
#define	ECR_PHYADDR_MASK	0x07c0	/* PHY address mask */
#define	ECR_CTEST		0x0020	/* Stream Cipher Test Mode */
#define	ECR_IECT		0x0004	/* Invalid Error Code Test */
#define	ECR_SSD			0x0001	/* Stream Cipher Disable */

#define	MII_ICSPHY_QPR		0x11	/* Quick Poll Register */
#define	QPR_SPEED		0x8000	/* 100Mbps */
#define	QPR_FDX			0x4000	/* Full dupled */
#define	QPR_ANB2		0x2000	/* Autoneg monitor bit 2 */
#define	QPR_ANB1		0x1000	/* Autoneg monitor bit 1 */
#define	QPR_ANB0		0x0800	/* Autoneg monitor bit 0 */
#define	QPR_RXERR		0x0400	/* Receive signal lost */
#define	QPR_PLLERR		0x0200	/* PLL error */
#define	QPR_FCARR		0x0100	/* False carrier detected */
#define	QPR_INVALSYM		0x0080	/* Invalid Symbol Detected */
#define	QPR_HALT		0x0040	/* Halt Symbol Detected */
#define	QPR_PREEM		0x0020	/* Two Idle Symbols together */
#define	QPR_ACOMP		0x0010	/* Autonegotiation complete */
#define	QPR_JABBER		0x0004	/* Jabber detected */
#define	QPR_RFAULT		0x0002	/* Remote Fault */
#define	QPR_LINK		0x0001	/* Link */

#define	MII_ICSPHY_TTR		0x12	/* 10baseT Operations Register */
#define	TTR_RJABBER		0x8000	/* Remote Jabber */
#define	TTR_POLARITY		0x4000	/* Polarity Reversed */
#define	TTR_NOJABBER		0x0020	/* Disable Jabber Check */
#define	TTR_LOOP		0x0010	/* Loopback mode */
#define	TTR_NOAPOLARITY		0x0008	/* Disable auto polarity correction */
#define	TTR_NOSQE		0x0004	/* Disable SQE check */
#define	TTR_NOLINK		0x0002	/* Disable Link check */
#define	TTR_NOSQUELCH		0x0001	/* Disable squelch */

#define	MII_ICSPHY_ECR2		0x13	/* Extended Control Register 2 */
#define	ECR2_REPEATER		0x8000	/* Repeater Mode */
#define	ECR2_HWSW		0x4000	/* hw/sw config priority */
#define	ECR2_LPRF		0x2000	/* link partner supports rem fault */
#define	ECR2_FORCERF		0x0400	/* Force transmit of rem fault */
#define	ECR2_RFPUP		0x0010	/* Rem fault on power up */
#define	ECR2_Q10T		0x0004	/* Qualified 10baseT data */
#define	ECR2_10TPROT		0x0002	/* 10baseT protect */
#define	ECR2_AUTOPWRDN		0x0001	/* automatic power down */

#endif /* _DEV_MII_ICSPHYREG_H_ */
