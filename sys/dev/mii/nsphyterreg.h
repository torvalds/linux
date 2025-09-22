/*	$OpenBSD: nsphyterreg.h,v 1.2 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD: nsphyterreg.h,v 1.1 1999/12/07 19:36:37 thorpej Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
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

#ifndef _DEV_MII_NSPHYTERREG_H_
#define	_DEV_MII_NSPHYTERREG_H_

/*
 * DP83843 registers.
 */

#define	MII_NSPHYTER_PHYSTS	0x10	/* PHY status */
#define	PHYSTS_REL		0x8000	/* receive error latch */
#define	PHYSTS_CIML		0x4000	/* CIM latch */
#define	PHYSTS_FCSL		0x2000	/* false carrier sense latch */
#define	PHYSTS_DEVRDY		0x0800	/* device ready */
#define	PHYSTS_PGRX		0x0400	/* page received */
#define	PHYSTS_ANEGEN		0x0200	/* autoneg. enabled */
#define	PHYSTS_MIIINTR		0x0100	/* MII interrupt */
#define	PHYSTS_REMFAULT		0x0080	/* remote fault */
#define	PHYSTS_JABBER		0x0040	/* jabber detect */
#define	PHYSTS_NWAYCOMP		0x0020	/* NWAY complete */
#define	PHYSTS_RESETSTAT	0x0010	/* reset status */
#define	PHYSTS_LOOPBACK		0x0008	/* loopback status */
#define	PHYSTS_DUPLEX		0x0004	/* full duplex */
#define	PHYSTS_SPEED10		0x0002	/* speed == 10Mb/s */
#define	PHYSTS_LINK		0x0001	/* link up */


#define	MII_NSPHYTER_MIPSCR	0x11	/* MII interrupt PHY specific
					   control */

#define	MIPSCR_INTEN		0x0002	/* interrupt enable */
#define	MIPSCR_TINT		0x0001	/* test interrupt */


#define	MII_NSPHYTER_MIPGSR	0x12	/* MII interrupt PHY generic
					   status */
#define	MIPGSR_MINT		0x8000	/* MII interrupt pending */

#define	MII_NSPHYTER_DCR	0x13	/* Disconnect counter */

#define	MII_NSPHYTER_FCSCR	0x14	/* False carrier sense counter */

#define	MII_NSPHYTER_RECR	0x15	/* Receive error counter */


#define	MII_NSPHYTER_PCSR	0x16	/* PCS configuration and status */
#define	PCSR_SINGLE_SD		0x8000	/* single-ended SD mode */
#define	PCSR_FEFI_EN		0x4000	/* far end fault indication mode */
#define	PCSR_DESCR_TO_RST	0x2000	/* reset descrambler timeout counter */
#define	PCSR_DESCR_TO_SEL	0x1000	/* descrambler timer mode */
#define	PCSR_DESCR_TO_DIS	0x0800	/* descrambler timer disable */
#define	PCSR_LD_SCR_SD		0x0400	/* load scrambler seed */
#define	PCSR_TX_QUIET		0x0200	/* 100Mb/s transmit true quiet mode */
#define	PCSR_TX_PATTERN		0x0180	/* 100Mb/s transmit test pattern */
#define	PCSR_F_LINK_100		0x0040	/* force good link in 100Mb/s */
#define	PCSR_CIM_DIS		0x0020	/* carrier integrity monitor disable */
#define	PCSR_CIM_STATUS		0x0010	/* carrier integrity monitor status */
#define	PCSR_CODE_ERR		0x0008	/* code errors */
#define	PCSR_PME_ERR		0x0004	/* premature end errors */
#define	PCSR_LINK_ERR		0x0002	/* link errors */
#define	PCSR_PKT_ERR		0x0001	/* packet errors */


#define	MII_NSPHYTER_LBR	0x17	/* loopback and bypass */
#define	LBR_BP_STRETCH		0x4000	/* bypass LED stretching */
#define	LBR_BP_4B5B		0x2000	/* bypass encoding/decoding */
#define	LBR_BP_SCR		0x1000	/* bypass scrambler/descrambler */
#define	LBR_BP_RX		0x0800	/* bypass receive function */
#define	LBR_BP_TX		0x0400	/* bypass transmit function */
#define	LBR_100_DP_CTL		0x0380	/* 100Mb/s data patch control */
#define	LBR_TW_LBEN		0x0020	/* TWISTER loopback enable */
#define	LBR_10_ENDEC_LB		0x0010	/* 10Mb/s ENDEC loopback */


#define	MII_NSPHYTER_10BTSCR	0x18	/* 10baseT status and control */
#define	BTSCR_AUI_TPI		0x2000	/* TREX operating mode */
#define	BTSCR_RX_SERIAL		0x1000	/* 10baseT RX serial mode */
#define	BTSCR_TX_SERIAL		0x0800	/* 10baseT TX serial mode */
#define	BTSCR_POL_DS		0x0400	/* polarity detection and correction
					   disable */
#define	BTSCR_AUTOSW_EN		0x0200	/* AUI/TPI autoswitch */
#define	BTSCR_LP_DS		0x0100	/* link pulse disable */
#define	BTSCR_HB_DS		0x0080	/* heartbeat disabled */
#define	BTSCR_LS_SEL		0x0040	/* low squelch select */
#define	BTSCR_AUI_SEL		0x0020	/* AUI select */
#define	BTSCR_JAB_DS		0x0010	/* jabber disable */
#define	BTSCR_THIN_SEL		0x0008	/* thin ethernet select */
#define	BTSCR_TX_FILT_DS	0x0004	/* TPI receive filter disable */


#define	MII_NSPHYTER_PHYCTRL	0x19	/* PHY control */
#define	PHYCTRL_TW_EQSEL	0x3000	/* TWISTER e.q. select */
#define	PHYCTRL_BLW_DS		0x0800	/* TWISTER base line wander disable */
#define	PHYCTRL_REPEATER	0x0200	/* repeater mode */
#define	PHYCTRL_LED_TXRX_MODE	0x0180	/* LED TX/RX mode */
#define	PHYCTRL_LED_DUP_MODE	0x0040	/* LED DUP mode */
#define	PHYCTRL_FX_EN		0x0020	/* Fiber mode enable */
#define	PHYCTRL_PHYADDR		0x001f	/* PHY address */

#endif /* _DEV_MII_NSPHYTERREG_H_ */
