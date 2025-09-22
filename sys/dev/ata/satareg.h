/*	$OpenBSD: satareg.h,v 1.4 2025/07/15 13:40:02 jsg Exp $	*/
/*	$NetBSD: satareg.h,v 1.3 2004/05/23 23:07:59 wiz Exp $	*/

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of Wasabi Systems, Inc.
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

#ifndef _DEV_ATA_SATAREG_H_
#define	_DEV_ATA_SATAREG_H_

/*
 * Serial ATA register definitions.
 *
 * Reference:
 *
 *	Serial ATA: High Speed Serialized AT Attachment
 *	Revision 1.0 29-August-2001
 *	Serial ATA Working Group
 */

/*
 * SStatus (SCR0) --
 *	Serial ATA interface status register
 */
	/*
	 * The DET value indicates the interface device detection and
	 * PHY state.
	 */
#define	SStatus_DET_NODEV	(0x0 << 0)	/* no device connected */
#define	SStatus_DET_DEV_NE	(0x1 << 0)	/* device, but PHY comm not
						   established */
#define	SStatus_DET_DEV		(0x3 << 0)	/* device, PHY comm
						   established */
#define	SStatus_DET_OFFLINE	(0x4 << 0)	/* PHY in offline mode */
#define	SStatus_DET_mask	(0xf << 0)
#define	SStatus_DET_shift	0
	/*
	 * The SPD value indicates the negotiated interface communication
	 * speed established.
	 */
#define	SStatus_SPD_NONE	(0x0 << 4)	/* no negotiated speed */
#define	SStatus_SPD_G1		(0x1 << 4)	/* Generation 1 (1.5Gb/s) */
#define	SStatus_SPD_G2		(0x2 << 4)	/* Generation 2 (3.0Gb/s) */
#define	SStatus_SPD_mask	(0xf << 4)
#define	SStatus_SPD_shift	4
	/*
	 * The IPM value indicates the current interface power management
	 * state.
	 */
#define	SStatus_IPM_NODEV	(0x0 << 8)	/* no device connected */
#define	SStatus_IPM_ACTIVE	(0x1 << 8)	/* ACTIVE state */
#define	SStatus_IPM_PARTIAL	(0x2 << 8)	/* PARTIAL pm state */
#define	SStatus_IPM_SLUMBER	(0x6 << 8)	/* SLUMBER pm state */
#define	SStatus_IPM_mask	(0xf << 8)
#define	SStatus_IPM_shift	8

/*
 * SError (SCR1) --
 *	Serial ATA interface error register
 */
#define	SError_ERR_I		(1U << 0)	/* Recovered data integrity
						   error */
#define	SError_ERR_M		(1U << 1)	/* Recovered communications
						   error */
#define	SError_ERR_T		(1U << 8)	/* Non-recovered transient
						   data integrity error */
#define	SError_ERR_C		(1U << 9)	/* Non-recovered persistent
						   communication or data
						   integrity error */
#define	SError_ERR_P		(1U << 10)	/* Protocol error */
#define	SError_ERR_E		(1U << 11)	/* Internal error */
#define	SError_DIAG_N		(1U << 16)	/* PhyRdy change */
#define	SError_DIAG_I		(1U << 17)	/* PHY internal error */
#define	SError_DIAG_W		(1U << 18)	/* Comm Wake */
#define	SError_DIAG_B		(1U << 19)	/* 10b to 8b decode error */
#define	SError_DIAG_D		(1U << 20)	/* Disparity error */
#define	SError_DIAG_C		(1U << 21)	/* CRC error */
#define	SError_DIAG_H		(1U << 22)	/* Handshake error */
#define	SError_DIAG_S		(1U << 23)	/* Link sequence error */
#define	SError_DIAG_T		(1U << 24)	/* Transport state transition
						   error */
#define	SError_DIAG_F		(1U << 25)	/* Unrecognized FIS type */
#define	SError_DIAG_X		(1U << 26)	/* Device Exchanged */

/*
 * SControl (SCR2) --
 *	Serial ATA interface control register
 */
	/*
	 * The DET field controls the host adapter device detection
	 * and interface initialization.
	 */
#define	SControl_DET_NONE	(0x0 << 0)	/* No device detection or
						   initialization action
						   requested */
#define	SControl_DET_INIT	(0x1 << 0)	/* Initialize interface
						   communication (equiv
						   of a hard reset) */
#define	SControl_DET_DISABLE	(0x4 << 0)	/* disable interface and
						   take PHY offline */
	/*
	 * The SPD field represents the highest allowed communication
	 * speed the interface is allowed to negotiate when communication
	 * is established.
	 */
#define	SControl_SPD_ANY	(0x0 << 4)	/* No restrictions */
#define	SControl_SPD_G1		(0x1 << 4)	/* Generation 1 (1.5Gb/s) */
#define	SControl_SPD_G2		(0x2 << 4)	/* Generation 2 (3.0Gb/s) */
	/*
	 * The IPM field represents the enabled interface power management
	 * states that can be invoked via the Serial ATA interface power
	 * management capabilities.
	 */
#define	SControl_IPM_ANY	(0x0 << 8)	/* No restrictions */
#define	SControl_IPM_NOPARTIAL	(0x1 << 8)	/* PARTIAL disabled */
#define	SControl_IPM_NOSLUMBER	(0x2 << 8)	/* SLUMBER disabled */
#define	SControl_IPM_NONE	(0x3 << 8)	/* No power management */
	/*
	 * The SPM field selects a power management state.  A non-zero
	 * value written to this field causes initiation of the selected
	 * power management state.
	 */
#define	SControl_SPM_PARTIAL	(0x1 << 12)	/* transition to PARTIAL */
#define	SControl_SPM_SLUMBER	(0x2 << 12)	/* transition to SLUMBER */
#define	SControl_SPM_ComWake	(0x4 << 12)	/* transition from PM */
	/*
	 * The PMP field identifies the selected Port Multiplier Port
	 * for accessing the SActive register.
	 */
#define	SControl_PMP(x)		((x) << 16)

#endif /* _DEV_ATA_SATAREG_H_ */
