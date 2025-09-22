/*	$OpenBSD: sqphyreg.h,v 1.5 2008/08/31 09:54:32 jsg Exp $	*/
/*	$NetBSD: sqphyreg.h,v 1.1 1998/11/03 23:51:29 thorpej Exp $	*/

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

#ifndef _DEV_MII_SQPHYREG_H_
#define	_DEV_MII_SQPHYREG_H_

/*
 * Seeq 80220 Register definitions.
 */

#define	MII_SQPHY_CONFIG1	0x10	/* Configuration 1 Register */
#define	CONFIG1_LNK_DIS		0x8000	/* Link Detect Disable */
#define	CONFIG1_XMT_DIS		0x4000	/* TP Transmitter Disable */
#define	CONFIG1_XMT_PDN		0x2000	/* TP Transmitter Powerdown */
#define	CONFIG1_TXEN_CRS	0x1000	/* TX_EN to CRS Loopback Disable */
#define	CONFIG1_BYP_ENC		0x0800	/* Bypass Encoder */
#define	CONFIG1_BYP_SCR		0x0400	/* Bypass Scrambler */
#define	CONFIG1_UNSCR_DIS	0x0200	/* Unscr. Idle Reception Disable */
#define	CONFIG1_EQLZR		0x0100	/* Rx Equalizer Disable */
#define	CONFIG1_CABLE		0x0080	/* Cable: 1 = STP, 0 = UTP */
#define	CONFIG1_RLVL0		0x0040	/* Receive Level Adjust */
#define	CONFIG1_TLVL3		0x0020	/* Transmit output level adjust */
#define	CONFIG1_TLVL2		0x0010
#define	CONFIG1_TLVL1		0x0008
#define	CONFIG1_TLVL0		0x0004
#define	CONFIG1_TRF1		0x0002	/* Transmitter Rise/Fall Adjust */
#define	CONFIG1_TRF0		0x0001

#define	MII_SQPHY_CONFIG2	0x11	/* Configuration 2 Register */
#define	CONFIG2_PLED3_1		0x8000	/* PLED3 configuration */
#define	CONFIG2_PLED3_0		0x4000
					/* 1 1 LINK100 (default) */
					/* 1 0 Blink */
					/* 0 1 On */
					/* 0 0 Off */
#define	CONFIG2_PLED2_1		0x2000	/* PLED2 configuration */
#define	CONFIG2_PLED2_0		0x1000
					/* 1 1 Activity (default) */
					/* 1 0 Blink */
					/* 0 1 On */
					/* 0 0 Off */
#define	CONFIG2_PLED1_1		0x0800	/* PLED1 configuration */
#define	CONFIG2_PLED1_0		0x0400
					/* 1 1 Full duplex (default) */
					/* 1 0 Blink */
					/* 0 1 On */
					/* 0 0 Off */
#define	CONFIG2_PLED0_1		0x0200	/* PLED0 configuration */
#define	CONFIG2_PLED0_0		0x0100
					/* 1 1 LINK10 (default) */
					/* 1 0 Blink */
					/* 0 1 On */
					/* 0 0 Off */
#define	CONFIG2_LED_DEF1	0x0080	/* LED Normal Function Select */
#define	CONFIG2_LED_DEF0	0x0040
#define	CONFIG2_APOL_DIS	0x0020	/* Auto Polarity Correct Disable */
#define	CONFIG2_JAB_DIS		0x0010	/* Jabber Disable */
#define	CONFIG2_MREG		0x0008	/* Multiple Register Access Enable */
#define	CONFIG2_INT_MDIO	0x0004	/* MDIO Interrupt when idle */
#define	CONFIG2_RJ_CFG		0x0002	/* R/J Configuration Select */

#define	MII_SQPHY_STATUS	0x12	/* Status Output Register */
#define	STATUS_INT		0x8000	/* Interrupt Detect */
#define	STATUS_LNK_FAIL		0x4000	/* Link Fail */
#define	STATUS_LOSS_SYNC	0x2000	/* Descrambler lost synchronization */
#define	STATUS_CWRD		0x1000	/* Codeword Error */
#define	STATUS_SSD		0x0800	/* Start of Stream Error */
#define	STATUS_ESD		0x0400	/* End of Stream Error */
#define	STATUS_RPOL		0x0200	/* Reverse Polarity Detected */
#define	STATUS_JAB		0x0100	/* Jabber Detected */
#define	STATUS_SPD_DET		0x0080	/* 100Mbps */
#define	STATUS_DPLX_DET		0x0040	/* Full Duplex */

#define	MII_SQPHY_MASK		0x13	/* Mask Register */
#define	MASK_INT		0x8000	/* mask INT */
#define	MASK_LNK_FAIL		0x4000	/* mask LNK_FAIL */
#define	MASK_LOSS_SYNC		0x2000	/* mask LOSS_SYNC */
#define	MASK_CWRD		0x1000	/* mask CWRD */
#define	MASK_SSD		0x0800	/* mask SSD */
#define	MASK_ESD		0x0400	/* mask ESD */
#define	MASK_RPOL		0x0200	/* mask RPOL */
#define	MASK_JAB		0x0100	/* mask JAB */
#define	MASK_SPD_DET		0x0080	/* mask SPD_DET */
#define	MASK_DPLX_DET		0x0040	/* mask DPLX_DET */
#define	MASK_ANEG_STS1		0x0020	/* mask ANEG_STS1 */
#define	MASK_ANEG_STS0		0x0010	/* mask ANEG_STS0 */

#define	MII_SQPHY_RESERVED	0x14	/* Reserved Register */
	/* All bits must be 0 */

#endif /* _DEV_MII_SQPHYREG_H_ */
