/*	OpenBSD: qsphyreg.h,v 1.2 1999/03/09 00:02:45 jason Exp 	*/
/*	NetBSD: qsphyreg.h,v 1.1 1998/08/11 00:01:03 thorpej Exp 	*/
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

#ifndef _DEV_MII_QSPHYREG_H_
#define	_DEV_MII_QSPHYREG_H_

/*
 * Register definitions for the Quality Semiconductor QS6612
 * Further documentation can be found at:
 * 	http://www.qualitysemi.com/products/network.html
 */

#define	MII_QSPHY_MCTL		0x11	/* Mode control */
#define	MCTL_T4PRE		0x1000	/* 100baseT4 interface present */
#define	MCTL_BTEXT		0x0800	/* reduce 10baseT squelch level */
#define	MCTL_FACTTEST		0x0100	/* factory test mode */
#define	MCTL_PHYADDRMASK	0x00f8	/* PHY address */
#define	MCTL_FACTTEST2		0x0004	/* another factory test mode */
#define	MCTL_NLPDIS		0x0002	/* disable link pulse tx */
#define	MCTL_SQEDIS		0x0001	/* disable SQE */

#define	MII_QSPHY_ISRC		0x1d	/* Interrupt source */
#define	MII_QSPHY_IMASK		0x1e	/* Interrupt mask */
#define	IMASK_TLINTR		0x8000	/* ThunderLAN interrupt mode */
#define	IMASK_ANCPL		0x0040	/* autonegotiation complete */
#define	IMASK_RFD		0x0020	/* remote fault detected */
#define	IMASK_LD		0x0010	/* link down */
#define	IMASK_ANLPA		0x0008	/* autonegotiation LP ACK */
#define	IMASK_PDT		0x0004	/* parallel detection fault */
#define	IMASK_ANPR		0x0002	/* autonegotiation page received */
#define	IMASK_REF		0x0001	/* receive error counter full */

#define	MII_QSPHY_PCTL		0x1f	/* PHY control */
#define	PCTL_RXERDIS		0x2000	/* receive error counter disable */
#define	PCTL_ANC		0x1000	/* autonegotiation complete */
#define	PCTL_RLBEN		0x0200	/* remote coopback enable */
#define	PCTL_DCREN		0x0100	/* DC restoration enable */
#define	PCTL_4B5BEN		0x0040	/* 4b/5b encoding */
#define	PCTL_PHYISO		0x0020	/* isolate PHY */
#define	PCTL_OPMASK		0x001c	/* operation mode mask */
#define	PCTL_AN			0x0000	/* autonegotiation in-progress */
#define	PCTL_10_T		0x0004	/* 10baseT */
#define	PCTL_100_TX		0x0008	/* 100baseTX */
#define	PCTL_100_T4		0x0010	/* 100baseT4 */
#define	PCTL_10_T_FDX		0x0014	/* 10baseT-FDX */
#define	PCTL_100_TX_FDX		0x0018	/* 100baseTX-FDX */
#define	PCTL_MLT3DIS		0x0002	/* disable MLT3 */
#define	PCTL_SRCDIS		0x0001	/* disable scrambling */

#endif /* _DEV_MII_QSPHYREG_H_ */
