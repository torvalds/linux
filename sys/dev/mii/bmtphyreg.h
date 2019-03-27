/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 *
 *	from NetBSD: bmtphyreg.h,v 1.1 2001/06/02 21:42:10 thorpej Exp
 *
 * $FreeBSD$
 */

#ifndef _DEV_MII_BMTPHYREG_H_
#define	_DEV_MII_BMTPHYREG_H_

/*
 * BCM5201/BCM5202 registers.
 */

#define	MII_BMTPHY_AUX_CTL	0x10	/* auxiliary control */
#define	AUX_CTL_TXDIS		0x2000	/* transmitter disable */
#define	AUX_CTL_4B5B_BYPASS	0x0400	/* bypass 4b5b encoder */
#define	AUX_CTL_SCR_BYPASS	0x0200	/* bypass scrambler */
#define	AUX_CTL_NRZI_BYPASS	0x0100	/* bypass NRZI encoder */
#define	AUX_CTL_RXALIGN_BYPASS	0x0080	/* bypass rx symbol alignment */
#define	AUX_CTL_BASEWANDER_DIS	0x0040	/* disable baseline wander correction */
#define	AUX_CTL_FEF_EN		0x0020	/* far-end fault enable */


#define	MII_BMTPHY_AUX_STS	0x11	/* auxiliary status */
#define	AUX_STS_FX_MODE		0x0400	/* 100base-FX mode (strap pin) */
#define	AUX_STS_LOCKED		0x0200	/* descrambler locked */
#define	AUX_STS_100BASE_LINK	0x0100	/* 1 = 100base link */
#define	AUX_STS_REMFAULT	0x0080	/* remote fault */
#define	AUX_STS_DISCON_STATE	0x0040	/* disconnect state */
#define	AUX_STS_FCARDET		0x0020	/* false carrier detected */
#define	AUX_STS_BAD_ESD		0x0010	/* bad ESD detected */
#define	AUX_STS_RXERROR		0x0008	/* Rx error detected */
#define	AUX_STS_TXERROR		0x0004	/* Tx error detected */
#define	AUX_STS_LOCKERROR	0x0002	/* lock error detected */
#define	AUX_STS_MLT3ERROR	0x0001	/* MLT3 code error detected */


#define	MII_BMTPHY_RXERROR_CTR	0x12	/* 100base-X Rx error counter */
#define	RXERROR_CTR_MASK	0x00ff


#define	MII_BMTPHY_FCS_CTR	0x13	/* 100base-X false carrier counter */
#define	FCS_CTR_MASK		0x00ff


#define	MII_BMTPHY_DIS_CTR	0x14	/* 100base-X disconnect counter */
#define	DIS_CTR_MASK		0x00ff


#define	MII_BMTPHY_PTEST	0x17	/* PTEST */


#define	MII_BMTPHY_AUX_CSR	0x18	/* auxiliary control/status */
#define	AUX_CSR_JABBER_DIS	0x8000	/* jabber disable */
#define	AUX_CSR_FLINK		0x4000	/* force 10baseT link pass */
#define	AUX_CSR_HSQ		0x0080	/* SQ high */
#define	AUX_CSR_LSQ		0x0040	/* SQ low */
#define	AUX_CSR_ER1		0x0020	/* edge rate 1 */
#define	AUX_CSR_ER0		0x0010	/* edge rate 0 */
#define	AUX_CSR_ANEG		0x0008	/* auto-negotiation activated */
#define	AUX_CSR_F100		0x0004	/* force 100base */
#define	AUX_CSR_SPEED		0x0002	/* 1 = 100, 0 = 10 */
#define	AUX_CSR_FDX		0x0001	/* full-duplex */


#define	MII_BMTPHY_AUX_SS	0x19	/* auxiliary status summary */
#define	AUX_SS_ACOMP		0x8000	/* auto-negotiation complete */
#define	AUX_SS_ACOMP_ACK	0x4000	/* auto-negotiation compl. ack */
#define	AUX_SS_AACK_DET		0x2000	/* auto-neg. ack detected */
#define	AUX_SS_ANLPAD		0x1000	/* auto-neg. link part. ability det */
#define	AUX_SS_ANEG_PAUSE	0x0800	/* pause operation bit */
#define	AUX_SS_HCD		0x0700	/* highest common denominator */
#define	AUX_SS_HCD_NONE		0x0000	/*    none */
#define	AUX_SS_HCD_10T		0x0100	/*    10baseT */
#define	AUX_SS_HCD_10T_FDX	0x0200	/*    10baseT-FDX */
#define	AUX_SS_HCD_100TX	0x0300	/*    100baseTX-FDX */
#define	AUX_SS_HCD_100T4	0x0400	/*    100baseT4 */
#define	AUX_SS_HCD_100TX_FDX	0x0500	/*    100baseTX-FDX */
#define	AUX_SS_PDF		0x0080	/* parallel detection fault */
#define	AUX_SS_LPRF		0x0040	/* link partner remote fault */
#define	AUX_SS_LPPR		0x0020	/* link partner page received */
#define	AUX_SS_LPANA		0x0010	/* link partner auto-neg able */
#define	AUX_SS_SPEED		0x0008	/* 1 = 100, 0 = 10 */
#define	AUX_SS_LINK		0x0004	/* link pass */
#define	AUX_SS_ANEN		0x0002	/* auto-neg. enabled */
#define	AUX_SS_JABBER		0x0001	/* jabber detected */


#define	MII_BMTPHY_INTR		0x1a	/* interrupt register */
#define	INTR_FDX_LED		0x8000	/* full-duplex led enable */
#define	INTR_INTR_EN		0x4000	/* interrupt enable */
#define	INTR_FDX_MASK		0x0800	/* full-dupled intr mask */
#define	INTR_SPD_MASK		0x0400	/* speed intr mask */
#define	INTR_LINK_MASK		0x0200	/* link intr mask */
#define	INTR_INTR_MASK		0x0100	/* master interrupt mask */
#define	INTR_FDX_CHANGE		0x0008	/* full-duplex change */
#define	INTR_SPD_CHANGE		0x0004	/* speed change */
#define	INTR_LINK_CHANGE	0x0002	/* link change */
#define	INTR_INTR_STATUS	0x0001	/* interrupt status */


#define	MII_BMTPHY_AUX2		0x1b	/* auliliary mode 2 */
#define	AUX2_BLOCK_RXDV		0x0200	/* block RXDV mode enabled */
#define	AUX2_ANPDQ		0x0100	/* auto-neg parallel detection Q mode */
#define	AUX2_TRAFFIC_LED	0x0040	/* traffic meter led enable */
#define	AUX2_FXMTRCV_LED	0x0020	/* force Tx and Rx LEDs */
#define	AUX2_HS_TOKEN		0x0010	/* high-speed token ring mode */
#define	AUX2_AUTO_LP		0x0008	/* auto low-power mode */
#define	AUX2_TWOLINK_LED	0x0004	/* two link LEDs */
#define	AUX2_SQE_DIS		0x0002	/* disable SQE pulse */


#define	MII_BMTPHY_AUXERR	0x1c	/* auxiliary error */
#define	AUXERR_MANCHESTER	0x0400	/* Manchester code error */
#define	AUXERR_EOF		0x0200	/* EOF detection error */
#define	AUXERR_POLARITY		0x0100	/* polarity inversion */
#define	AUXERR_ANEG		0x0008	/* autonegotiation enabled */
#define	AUXERR_F100		0x0004	/* force 100base */
#define	AUXERR_SPEED		0x0002	/* 1 = 100, 0 = 10 */
#define	AUXERR_FDX		0x0001	/* full-duplex */


#define	MII_BMTPHY_AUXMODE	0x1d	/* auxiliary mode */
#define	AUXMODE_ACT_LED_DIS	0x0010	/* activity LED disable */
#define	AUXMODE_LINK_LED_DIS	0x0008	/* link LED disable */
#define	AUXMODE_BLOCK_TXEN	0x0002	/* enable block TXEN */


#define	MII_BMTPHY_AUXMPHY	0x1e	/* auxiliary multiple phy register */
#define	AUXMPHY_HCD_TX_FDX	0x8000	/* res. is 100baseTX-FDX */
#define	AUXMPHY_HCD_T4		0x4000	/* res. is 100baseT4 */
#define	AUXMPHY_HCD_TX		0x2000	/* res. is 100baseTX */
#define	AUXMPHY_HCD_10T_FDX	0x1000	/* res. is 10baseT-FDX */
#define	AUXMPHY_HCD_10T		0x0800	/* res. is 10baseT */
#define	AUXMPHY_RES_ANEG	0x0100	/* restart auto-negotiation */
#define	AUXMPHY_ANEG_COMP	0x0080	/* auto-negotiation complete */
#define	AUXMPHY_ACK_COMP	0x0040	/* acknowledge complete */
#define	AUXMPHY_ACK_DET		0x0020	/* acknowledge detected */
#define	AUXMPHY_ABILITY_DET	0x0010	/* waiting for LP ability */
#define	AUXMPHY_SUPER_ISO	0x0008	/* super-isolate mode */
#define	AUXMPHY_10T_SERIAL	0x0002	/* 10baseT serial mode */


#define	MII_BMTPHY_TEST		0x1d	/* Broadcom test register */


#endif /* _DEV_MII_BMTPHYREG_H_ */
