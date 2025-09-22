/*	$OpenBSD: acphyreg.h,v 1.2 2004/10/01 04:08:45 jsg Exp $	*/
/*	$NetBSD: acphyreg.h,v 1.1 2001/08/24 17:54:33 thorpej Exp $	*/

/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_MII_ACPHYREG_H_
#define	_DEV_MII_ACPHYREG_H_

/*
 * Altima AC101 PHY registers.
 *
 * Note the AC101 and the AMD Ac79c874 are the same PHY core.  There
 * are some registers documented in the AC101 manual that are not in
 * the Am79c874 manual, and vice-versa.  I have no idea how to tell
 * the two apart, but we don't really use the registers that fall into
 * this category, anyhow.
 */

#define	MII_ACPHY_PILR		0x10	/* polarity and interrupt control */
#define	PILR_REPEATER		0x8000	/* repeater mode */
#define	PILR_INTR_LEVL		0x4000	/* 1 = active high, 0 = active low */
#define	PILR_SQE_INHIBIT	0x0800	/* disable 10T SQE testing */
#define	PILR_10T_LOOP		0x0400	/* enable loopback in 10T */
#define	PILR_GPIO1_DATA		0x0200	/* GPIO1 pin */
#define	PILR_GPIO1_DIR		0x0100	/* 1 = input */
#define	PILR_GPIO0_DATA		0x0080	/* GPIO0 pin */
#define	PILR_GPIO0_DIR		0x0040	/* 1 = input */
#define	PILR_AUTO_POL_DIS	0x0020	/* disable auto-polarity */
#define	PILR_REVERSE_POL	0x0010	/* 1 = reverse, 0 = normal */
#define	PILR_RXCLK_CTRL		0x0001	/* disable RX_CLK when idle */


#define	MII_ACPHY_ICSR		0x11	/* interrupt control/status */
#define	ICSR_JABBER_IE		0x8000	/* jabber interrupt enable */
#define	ICSR_RX_ER_IE		0x4000	/* Rx error interrupt enable */
#define	ICSR_PAGE_RX_IE		0x2000	/* page received interrupt enable */
#define	ICSR_PD_FAULT_IE	0x1000	/* parallel detection fault int en */
#define	ICSR_LP_ACK_IE		0x0800	/* link partner ACK interrupt en */
#define	ICSR_LNK_NOT_OK_IE	0x0400	/* link not okay interrupt enable */
#define	ICSR_R_FAULT_IE		0x0200	/* remote fault interrupt enable */
#define	ICSR_ANEG_COMP_IE	0x0100	/* autonegotiation complete int en */
#define	ICSR_JABBER_INT		0x0080	/* jabber interrupt */
#define	ICSR_RX_ER_INT		0x0040	/* Rx error interrupt */
#define	ICSR_PAGE_RX_INT	0x0020	/* page received interrupt */
#define	ICSR_PD_FAULT_INT	0x0010	/* parallel detection fault interrupt */
#define	ICSR_LP_ACK_INT		0x0008	/* link partner ACK interrupt */
#define	ICSR_LNK_NOT_OK_INT	0x0004	/* link not okay interrupt */
#define	ICSR_R_FAULT_INT	0x0002	/* remote fault interrupt */
#define	ICSR_ANEG_COMP_INT	0x0001	/* autonegotiation complete interrupt */


#define	MII_ACPHY_DR		0x12	/* diagnostic register */
#define	DR_DPLX			0x0800	/* full-duplex resolved */
#define	DR_SPEED		0x0400	/* 100BASE-TX resolved */
#define	DR_RX_PASS		0x0200	/* manchester/signal received */
#define	DR_RX_LOCK		0x0100	/* PLL signal has been locked */


#define	MII_ACPHY_PLR		0x13	/* power/loopback register */
#define	PLR_TB125		0x0040	/* Tx transformer ratio 1.25:1 */
#define	PLR_LOW_POWER_MODE	0x0020	/* enable advanced power saving mode */
#define	PLR_TEST_LOOPBACK	0x0010	/* enable test loopback */
#define	PLR_DIGITAL_LOOPBACK	0x0008	/* enable loopback */
#define	PLR_LP_LPBK		0x0004	/* enable link pulse loopback */
#define	PLR_NLP_LINK_INT_TEST	0x0002	/* send NLP instead of FLP */
#define	PLR_REDUCE_TIMER	0x0001	/* reduce time constant for aneg */


/*	AC101 only	*/
#define	MII_ACPHY_CMR		0x14	/* cable measurement register */
#define	CMR_MASK		0x00f0	/* cable measurement mask */


#define	MII_ACPHY_MCR		0x15	/* mode control register */
#define	MCR_NLP_DISABLE		0x4000	/* force good 10BASE-T link */
#define	MCR_FORCE_LINK_UP	0x2000	/* force good 100BASE-TX link */
#define	MCR_JABBER_DISABLE	0x1000	/* disable jabber function */
#define	MCR_10BT_SEL		0x0800	/* enable 7-wire 10T operation */
#define	MCR_CONF_ALED		0x0400	/* 1 = ALED only Rx, 0 = ALED Rx/Tx */
#define	MCR_LED_SEL		0x0200	/* 1 = tqphy-compat LED config */
#define	MCR_FEF_DIS		0x0100	/* disable far-end-fault insertion */
#define	MCR_FORCE_FEF_TX	0x0080	/* force FEF transmission */
#define	MCR_RX_ER_CNT_FULL	0x0040	/* Rx error counter full */
#define	MCR_DIS_RX_ER_CNT	0x0020	/* disable Rx error counter */
#define	MCR_DIS_WDT		0x0010	/* disable the watchdog timer */
#define	MCR_EN_RPBK		0x0008	/* enable remote loopback */
#define	MCR_DIS_SCRM		0x0004	/* enable 100M data scrambling */
#define	MCR_PCSBP		0x0002	/* bypass PCS */
#define	MCR_FX_SEL		0x0001	/* FX mode selected */


/*	Am79c874 only	*/
#define	MII_ACPHY_DCR		0x17	/* disconnect counter register */


#define	MII_ACPHY_RECR		0x18	/* receive error counter register */


#endif /* _DEV_MII_ACPHYREG_H_ */
