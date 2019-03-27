/*	OpenBSD: lxtphyreg.h,v 1.1 1998/11/11 19:34:47 jason Exp 	*/
/*	NetBSD: lxtphyreg.h,v 1.1 1998/10/24 00:33:17 thorpej Exp 	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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

#ifndef _DEV_MII_LXTPHYREG_H_
#define	_DEV_MII_LXTPHYREG_H_

/*
 * LXT970 registers.
 */

#define	MII_LXTPHY_MIRROR	0x10	/* Mirror register */
	/* All bits user-defined */

#define	MII_LXTPHY_IER		0x11	/* Interrupt Enable Register */
#define	IER_MIIDRVLVL		0x0008	/* Rediced MII driver levels */
#define	IER_LNK_CRITERIA	0x0004	/* Enhanced Link Loss Criteria */
#define	IER_INTEN		0x0002	/* Interrupt Enable */
#define	IER_TINT		0x0001	/* Force Interrupt */

#define	MII_LXTPHY_ISR		0x12	/* Interrupt Status Register */
#define	ISR_MINT		0x8000	/* MII Interrupt Pending */
#define	ISR_XTALOK		0x4000	/* Clocks OK */

#define	MII_LXTPHY_CONFIG	0x13	/* Configuration Register */
#define	CONFIG_TXMIT_TEST	0x4000	/* 100base-T Transmit Test */
#define	CONFIG_REPEATER		0x2000	/* Repeater Mode */
#define	CONFIG_MDIO_INT		0x1000	/* Enable intr signalling on MDIO */
#define	CONFIG_TPLOOP		0x0800	/* Disable 10base-T Loopback */
#define	CONFIG_SQE		0x0400	/* Enable SQE */
#define	CONFIG_DISJABBER	0x0200	/* Disable Jabber */
#define	CONFIG_DISLINKTEST	0x0100	/* Disable Link Test */
#define	CONFIG_LEDC1		0x0080	/* LEDC configuration */
#define	CONFIG_LEDC0		0x0040	/* ... */
					/* 0 0 LEDC indicates collision */
					/* 0 1 LEDC is off */
					/* 1 0 LEDC indicates activity */
					/* 1 1 LEDC is on */
#define	CONFIG_ADVTXCLK		0x0020	/* Advance TX clock */
#define	CONFIG_5BSYMBOL		0x0010	/* 5-bit Symbol mode */
#define	CONFIG_SCRAMBLER	0x0008	/* Bypass scrambler */
#define	CONFIG_100BASEFX	0x0004	/* 100base-FX */
#define	CONFIG_TXDISCON		0x0001	/* Disconnect TP transmitter */

#define	MII_LXTPHY_CSR		0x14	/* Chip Status Register */
#define	CSR_LINK		0x2000	/* Link is up */
#define	CSR_DUPLEX		0x1000	/* Full-duplex */
#define	CSR_SPEED		0x0800	/* 100Mbps */
#define	CSR_ACOMP		0x0400	/* Autonegotiation complete */
#define	CSR_PAGERCVD		0x0200	/* Link page received */
#define	CSR_LOWVCC		0x0004	/* Low Voltage Fault */

#endif /* _DEV_MII_LXTPHYREG_H_ */
