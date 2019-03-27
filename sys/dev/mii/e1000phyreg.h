/* $FreeBSD$ */
/*-
 * Principal Author: Parag Patel
 * Copyright (c) 2001
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Additional Copyright (c) 2001 by Traakan Software under same licence.
 * Secondary Author: Matthew Jacob
 */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Derived by information released by Intel under the following license:
 *
 * Copyright (c) 1999 - 2001, Intel Corporation 
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 *  1. Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 * 
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 *  3. Neither the name of Intel Corporation nor the names of its contributors 
 *     may be used to endorse or promote products derived from this software 
 *     without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

/*
 * Marvell E1000 PHY registers
 */

#define E1000_MAX_REG_ADDRESS		0x1F

#define E1000_CR			0x00	/* control register */
#define E1000_CR_SPEED_SELECT_MSB	0x0040
#define E1000_CR_COLL_TEST_ENABLE	0x0080
#define E1000_CR_FULL_DUPLEX		0x0100
#define E1000_CR_RESTART_AUTO_NEG	0x0200
#define E1000_CR_ISOLATE		0x0400
#define E1000_CR_POWER_DOWN		0x0800
#define E1000_CR_AUTO_NEG_ENABLE	0x1000
#define E1000_CR_SPEED_SELECT_LSB	0x2000
#define E1000_CR_LOOPBACK		0x4000
#define E1000_CR_RESET			0x8000

#define E1000_CR_SPEED_1000		0x0040
#define E1000_CR_SPEED_100		0x2000
#define E1000_CR_SPEED_10		0x0000

#define E1000_SR			0x01	/* status register */
#define E1000_SR_EXTENDED		0x0001
#define E1000_SR_JABBER_DETECT		0x0002
#define E1000_SR_LINK_STATUS		0x0004
#define E1000_SR_AUTO_NEG		0x0008
#define E1000_SR_REMOTE_FAULT		0x0010
#define E1000_SR_AUTO_NEG_COMPLETE	0x0020
#define E1000_SR_PREAMBLE_SUPPRESS	0x0040
#define E1000_SR_EXTENDED_STATUS	0x0100
#define E1000_SR_100T2			0x0200
#define E1000_SR_100T2_FD		0x0400
#define E1000_SR_10T			0x0800
#define E1000_SR_10T_FD			0x1000
#define E1000_SR_100TX			0x2000
#define E1000_SR_100TX_FD		0x4000
#define E1000_SR_100T4			0x8000

#define E1000_ID1			0x02	/* ID register 1 */
#define E1000_ID2			0x03	/* ID register 2 */
#define E1000_ID_88E1000		0x01410C50
#define E1000_ID_88E1000S		0x01410C40
#define E1000_ID_88E1011		0x01410C20
#define E1000_ID_MASK			0xFFFFFFF0

#define E1000_AR			0x04	/* autonegotiation advertise reg */
#define E1000_AR_SELECTOR_FIELD		0x0001
#define E1000_AR_10T			0x0020
#define E1000_AR_10T_FD			0x0040
#define E1000_AR_100TX			0x0080
#define E1000_AR_100TX_FD		0x0100
#define E1000_AR_100T4			0x0200
#define E1000_AR_PAUSE			0x0400
#define E1000_AR_ASM_DIR		0x0800
#define E1000_AR_REMOTE_FAULT		0x2000
#define E1000_AR_NEXT_PAGE		0x8000
#define E1000_AR_SPEED_MASK		0x01E0

/* Autonegotiation register bits for fiber cards (Alaska Only!) */
#define E1000_FA_1000X_FD		0x0020
#define E1000_FA_1000X			0x0040
#define E1000_FA_SYM_PAUSE		0x0080
#define E1000_FA_ASYM_PAUSE		0x0100
#define E1000_FA_FAULT1			0x1000
#define E1000_FA_FAULT2			0x2000
#define E1000_FA_NEXT_PAGE		0x8000

#define E1000_LPAR			0x05	/* autoneg link partner abilities reg */
#define E1000_LPAR_SELECTOR_FIELD	0x0001
#define E1000_LPAR_10T			0x0020
#define E1000_LPAR_10T_FD		0x0040
#define E1000_LPAR_100TX		0x0080
#define E1000_LPAR_100TX_FD		0x0100
#define E1000_LPAR_100T4		0x0200
#define E1000_LPAR_PAUSE		0x0400
#define E1000_LPAR_ASM_DIR		0x0800
#define E1000_LPAR_REMOTE_FAULT		0x2000
#define E1000_LPAR_ACKNOWLEDGE		0x4000
#define E1000_LPAR_NEXT_PAGE		0x8000

/* autoneg link partner ability register bits for fiber cards (Alaska Only!) */
#define E1000_FPAR_1000X_FD		0x0020
#define E1000_FPAR_1000X		0x0040
#define E1000_FPAR_SYM_PAUSE		0x0080
#define E1000_FPAR_ASYM_PAUSE		0x0100
#define E1000_FPAR_FAULT1		0x1000
#define E1000_FPAR_FAULT2		0x2000
#define E1000_FPAR_ACK			0x4000
#define E1000_FPAR_NEXT_PAGE		0x8000

#define E1000_ER			0x06	/* autoneg expansion reg */
#define E1000_ER_LP_NWAY		0x0001
#define E1000_ER_PAGE_RXD		0x0002
#define E1000_ER_NEXT_PAGE		0x0004
#define E1000_ER_LP_NEXT_PAGE		0x0008
#define E1000_ER_PAR_DETECT_FAULT	0x0100

#define E1000_NPTX			0x07	/* autoneg next page TX */
#define E1000_NPTX_MSG_CODE_FIELD	0x0001
#define E1000_NPTX_TOGGLE		0x0800
#define E1000_NPTX_ACKNOWLDGE2		0x1000
#define E1000_NPTX_MSG_PAGE		0x2000
#define E1000_NPTX_NEXT_PAGE		0x8000

#define E1000_RNPR			0x08	/* autoneg link-partner (?) next page */
#define E1000_RNPR_MSG_CODE_FIELD	0x0001
#define E1000_RNPR_TOGGLE		0x0800
#define E1000_RNPR_ACKNOWLDGE2		0x1000
#define E1000_RNPR_MSG_PAGE		0x2000
#define E1000_RNPR_ACKNOWLDGE		0x4000
#define E1000_RNPR_NEXT_PAGE		0x8000

#define E1000_1GCR			0x09	/* 1000T (1G) control reg */
#define E1000_1GCR_ASYM_PAUSE		0x0080
#define E1000_1GCR_1000T		0x0100
#define E1000_1GCR_1000T_FD		0x0200
#define E1000_1GCR_REPEATER_DTE		0x0400
#define E1000_1GCR_MS_VALUE		0x0800
#define E1000_1GCR_MS_ENABLE		0x1000
#define E1000_1GCR_TEST_MODE_NORMAL	0x0000
#define E1000_1GCR_TEST_MODE_1		0x2000
#define E1000_1GCR_TEST_MODE_2		0x4000
#define E1000_1GCR_TEST_MODE_3		0x6000
#define E1000_1GCR_TEST_MODE_4		0x8000
#define E1000_1GCR_SPEED_MASK		0x0300

#define E1000_1GSR			0x0A	/* 1000T (1G) status reg */
#define E1000_1GSR_IDLE_ERROR_CNT	0x0000
#define E1000_1GSR_ASYM_PAUSE_DIR	0x0100
#define E1000_1GSR_LP			0x0400
#define E1000_1GSR_LP_FD		0x0800
#define E1000_1GSR_REMOTE_RX_STATUS	0x1000
#define E1000_1GSR_LOCAL_RX_STATUS	0x2000
#define E1000_1GSR_MS_CONFIG_RES	0x4000
#define E1000_1GSR_MS_CONFIG_FAULT	0x8000

#define E1000_ESR			0x0F	/* IEEE extended status reg */
#define E1000_ESR_1000T			0x1000
#define E1000_ESR_1000T_FD		0x2000
#define E1000_ESR_1000X			0x4000
#define E1000_ESR_1000X_FD		0x8000

#define E1000_TX_POLARITY_MASK		0x0100
#define E1000_TX_NORMAL_POLARITY	0

#define E1000_AUTO_POLARITY_DISABLE	0x0010

#define E1000_SCR			0x10	/* special control register */
#define E1000_SCR_JABBER_DISABLE	0x0001
#define E1000_SCR_POLARITY_REVERSAL	0x0002
#define E1000_SCR_SQE_TEST		0x0004
#define E1000_SCR_INT_FIFO_DISABLE	0x0008
#define E1000_SCR_CLK125_DISABLE	0x0010
#define E1000_SCR_MDI_MANUAL_MODE	0x0000
#define E1000_SCR_MDIX_MANUAL_MODE	0x0020
#define E1000_SCR_AUTO_X_1000T		0x0040
#define E1000_SCR_AUTO_X_MODE		0x0060
#define E1000_SCR_10BT_EXT_ENABLE	0x0080
#define E1000_SCR_MII_5BIT_ENABLE	0x0100
#define E1000_SCR_SCRAMBLER_DISABLE	0x0200
#define E1000_SCR_FORCE_LINK_GOOD	0x0400
#define E1000_SCR_ASSERT_CRS_ON_TX	0x0800
#define E1000_SCR_RX_FIFO_DEPTH_6	0x0000
#define E1000_SCR_RX_FIFO_DEPTH_8	0x1000
#define E1000_SCR_RX_FIFO_DEPTH_10	0x2000
#define E1000_SCR_RX_FIFO_DEPTH_12	0x3000
#define E1000_SCR_TX_FIFO_DEPTH_6	0x0000
#define E1000_SCR_TX_FIFO_DEPTH_8	0x4000
#define E1000_SCR_TX_FIFO_DEPTH_10	0x8000
#define E1000_SCR_TX_FIFO_DEPTH_12	0xC000

/* 88E3016 only */
#define	E1000_SCR_AUTO_MDIX		0x0030
#define	E1000_SCR_SIGDET_POLARITY	0x0040
#define	E1000_SCR_EXT_DISTANCE		0x0080
#define	E1000_SCR_FEFI_DISABLE		0x0100
#define	E1000_SCR_NLP_GEN_DISABLE	0x0800
#define	E1000_SCR_LPNP			0x1000
#define	E1000_SCR_NLP_CHK_DISABLE	0x2000
#define	E1000_SCR_EN_DETECT		0x4000

#define E1000_SCR_EN_DETECT_MASK	0x0300

/* 88E1112 page 1 fiber specific control */
#define E1000_SCR_FIB_TX_DIS		0x0008
#define E1000_SCR_FIB_SIGDET_POLARITY	0x0200
#define E1000_SCR_FIB_FORCE_LINK	0x0400

/* 88E1112 page 2 */
#define E1000_SCR_MODE_MASK		0x0380
#define E1000_SCR_MODE_AUTO		0x0180
#define E1000_SCR_MODE_COPPER		0x0280
#define E1000_SCR_MODE_1000BX		0x0380

/* 88E1116 page 0 */
#define	E1000_SCR_POWER_DOWN		0x0004
/* 88E1116, 88E1149 page 2 */
#define	E1000_SCR_RGMII_POWER_UP	0x0008

/* 88E1116, 88E1149 page 3 */
#define E1000_SCR_LED_STAT0_MASK	0x000F
#define E1000_SCR_LED_STAT1_MASK	0x00F0
#define E1000_SCR_LED_INIT_MASK		0x0F00
#define E1000_SCR_LED_LOS_MASK		0xF000
#define E1000_SCR_LED_STAT0(x)		((x) & E1000_SCR_LED_STAT0_MASK)
#define E1000_SCR_LED_STAT1(x)		((x) & E1000_SCR_LED_STAT1_MASK)
#define E1000_SCR_LED_INIT(x)		((x) & E1000_SCR_LED_INIT_MASK)
#define E1000_SCR_LED_LOS(x)		((x) & E1000_SCR_LED_LOS_MASK)

#define E1000_SSR			0x11	/* special status register */
#define E1000_SSR_JABBER		0x0001
#define E1000_SSR_REV_POLARITY		0x0002
#define E1000_SSR_MDIX			0x0020
#define E1000_SSR_LINK			0x0400
#define E1000_SSR_SPD_DPLX_RESOLVED	0x0800
#define E1000_SSR_PAGE_RCVD		0x1000
#define E1000_SSR_DUPLEX		0x2000
#define E1000_SSR_SPEED			0xC000
#define E1000_SSR_10MBS			0x0000
#define E1000_SSR_100MBS		0x4000
#define E1000_SSR_1000MBS		0x8000

#define E1000_IER			0x12	/* interrupt enable reg */
#define E1000_IER_JABBER		0x0001
#define E1000_IER_POLARITY_CHANGE	0x0002
#define E1000_IER_MDIX_CHANGE		0x0040
#define E1000_IER_FIFO_OVER_UNDERUN	0x0080
#define E1000_IER_FALSE_CARRIER		0x0100
#define E1000_IER_SYMBOL_ERROR		0x0200
#define E1000_IER_LINK_STAT_CHANGE	0x0400
#define E1000_IER_AUTO_NEG_COMPLETE	0x0800
#define E1000_IER_PAGE_RECEIVED		0x1000
#define E1000_IER_DUPLEX_CHANGED	0x2000
#define E1000_IER_SPEED_CHANGED		0x4000
#define E1000_IER_AUTO_NEG_ERR		0x8000

/* 88E1116, 88E1149 page 3, LED timer control. */
#define	E1000_PULSE_MASK	0x7000
#define	E1000_PULSE_NO_STR	0	/* no pulse stretching */
#define	E1000_PULSE_21MS	1	/* 21 ms to 42 ms */
#define	E1000_PULSE_42MS	2	/* 42 ms to 84 ms */
#define	E1000_PULSE_84MS	3	/* 84 ms to 170 ms */
#define	E1000_PULSE_170MS	4	/* 170 ms to 340 ms */
#define	E1000_PULSE_340MS	5	/* 340 ms to 670 ms */
#define	E1000_PULSE_670MS	6	/* 670 ms to 1300 ms */
#define	E1000_PULSE_1300MS	7	/* 1300 ms to 2700 ms */
#define	E1000_PULSE_DUR(x)	((x) &	E1000_PULSE_MASK) 

#define	E1000_BLINK_MASK	0x0700
#define	E1000_BLINK_42MS	0	/* 42 ms */
#define	E1000_BLINK_84MS	1	/* 84 ms */
#define	E1000_BLINK_170MS	2	/* 170 ms */
#define	E1000_BLINK_340MS	3	/* 340 ms */
#define	E1000_BLINK_670MS	4	/* 670 ms */
#define	E1000_BLINK_RATE(x)	((x) &	E1000_BLINK_MASK) 

#define E1000_ISR			0x13	/* interrupt status reg */
#define E1000_ISR_JABBER		0x0001
#define E1000_ISR_POLARITY_CHANGE	0x0002
#define E1000_ISR_MDIX_CHANGE		0x0040
#define E1000_ISR_FIFO_OVER_UNDERUN	0x0080
#define E1000_ISR_FALSE_CARRIER		0x0100
#define E1000_ISR_SYMBOL_ERROR		0x0200
#define E1000_ISR_LINK_STAT_CHANGE	0x0400
#define E1000_ISR_AUTO_NEG_COMPLETE	0x0800
#define E1000_ISR_PAGE_RECEIVED		0x1000
#define E1000_ISR_DUPLEX_CHANGED	0x2000
#define E1000_ISR_SPEED_CHANGED		0x4000
#define E1000_ISR_AUTO_NEG_ERR		0x8000

#define E1000_ESCR			0x14	/* extended special control reg */
#define E1000_ESCR_FIBER_LOOPBACK	0x4000
#define E1000_ESCR_DOWN_NO_IDLE		0x8000
#define E1000_ESCR_TX_CLK_2_5		0x0060
#define E1000_ESCR_TX_CLK_25		0x0070
#define E1000_ESCR_TX_CLK_0		0x0000

#define E1000_RECR			0x15	/* RX error counter reg */

#define E1000_EADR			0x16	/* extended address reg */

#define E1000_LCR			0x18	/* LED control reg */
#define E1000_LCR_LED_TX		0x0001
#define E1000_LCR_LED_RX		0x0002
#define E1000_LCR_LED_DUPLEX		0x0004
#define E1000_LCR_LINK			0x0008
#define E1000_LCR_BLINK_42MS		0x0000
#define E1000_LCR_BLINK_84MS		0x0100
#define E1000_LCR_BLINK_170MS		0x0200
#define E1000_LCR_BLINK_340MS		0x0300
#define E1000_LCR_BLINK_670MS		0x0400
#define E1000_LCR_PULSE_OFF		0x0000
#define E1000_LCR_PULSE_21_42MS		0x1000
#define E1000_LCR_PULSE_42_84MS		0x2000
#define E1000_LCR_PULSE_84_170MS	0x3000
#define E1000_LCR_PULSE_170_340MS	0x4000
#define E1000_LCR_PULSE_340_670MS	0x5000
#define E1000_LCR_PULSE_670_13S		0x6000
#define E1000_LCR_PULSE_13_26S		0x7000

/* The following register is found only on the 88E1011 Alaska PHY */
#define E1000_ESSR			0x1B	/* Extended PHY specific sts */
#define E1000_ESSR_FIBER_LINK		0x2000
#define E1000_ESSR_GMII_COPPER		0x000f
#define E1000_ESSR_GMII_FIBER		0x0007
#define E1000_ESSR_TBI_COPPER		0x000d
#define E1000_ESSR_TBI_FIBER		0x0005
