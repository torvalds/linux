/*	$OpenBSD: brgphyreg.h,v 1.19 2022/01/09 05:42:44 jsg Exp $	*/

/*
 * Copyright (c) 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: brgphyreg.h,v 1.4 2001/09/27 17:32:49 wpaul Exp $
 */

#ifndef _DEV_MII_BRGPHYREG_H_
#define	_DEV_MII_BRGPHYREG_H_

/*
 * Broadcom BCM5400 registers
 */

#define BRGPHY_MII_PHY_EXTCTL	0x10	/* PHY extended control */
#define BRGPHY_PHY_EXTCTL_MAC_PHY	0x8000	/* 10BIT/GMI-interface */
#define BRGPHY_PHY_EXTCTL_DIS_CROSS	0x4000	/* Disable MDI crossover */
#define BRGPHY_PHY_EXTCTL_TX_DIS	0x2000	/* Tx output disable d*/
#define BRGPHY_PHY_EXTCTL_INT_DIS	0x1000	/* Interrupts disabled */
#define BRGPHY_PHY_EXTCTL_F_INT		0x0800	/* Force interrupt */
#define BRGPHY_PHY_EXTCTL_BY_45		0x0400	/* Bypass 4B5B-Decoder */
#define BRGPHY_PHY_EXTCTL_BY_SCR	0x0200	/* Bypass scrambler */
#define BRGPHY_PHY_EXTCTL_BY_MLT3	0x0100	/* Bypass MLT3 encoder */
#define BRGPHY_PHY_EXTCTL_BY_RXA	0x0080	/* Bypass RX alignment */
#define BRGPHY_PHY_EXTCTL_RES_SCR	0x0040	/* Reset scrambler */
#define BRGPHY_PHY_EXTCTL_EN_LTR	0x0020	/* Enable LED traffic mode */
#define BRGPHY_PHY_EXTCTL_LED_ON	0x0010	/* Force LEDs on */
#define BRGPHY_PHY_EXTCTL_LED_OFF	0x0008	/* Force LEDs off */
#define BRGPHY_PHY_EXTCTL_EX_IPG	0x0004	/* Extended TX IPG mode */
#define BRGPHY_PHY_EXTCTL_3_LED		0x0002	/* Three link LED mode */
#define BRGPHY_PHY_EXTCTL_HIGH_LA	0x0001	/* GMII Fifo Elasticy (?) */

#define BRGPHY_MII_PHY_EXTSTS	0x11	/* PHY extended status */
#define BRGPHY_PHY_EXTSTS_CROSS_STAT	0x2000	/* MDI crossover status */
#define BRGPHY_PHY_EXTSTS_INT_STAT	0x1000	/* Interrupt status */
#define BRGPHY_PHY_EXTSTS_RRS		0x0800	/* Remote receiver status */
#define BRGPHY_PHY_EXTSTS_LRS		0x0400	/* Local receiver status */
#define BRGPHY_PHY_EXTSTS_LOCKED	0x0200	/* Locked */
#define BRGPHY_PHY_EXTSTS_LS		0x0100	/* Link status */
#define BRGPHY_PHY_EXTSTS_RF		0x0080	/* Remove fault */
#define BRGPHY_PHY_EXTSTS_CE_ER		0x0040	/* Carrier ext error */
#define BRGPHY_PHY_EXTSTS_BAD_SSD	0x0020	/* Bad SSD */
#define BRGPHY_PHY_EXTSTS_BAD_ESD	0x0010	/* Bad ESS */
#define BRGPHY_PHY_EXTSTS_RX_ER		0x0008	/* RX error */
#define BRGPHY_PHY_EXTSTS_TX_ER		0x0004	/* TX error */
#define BRGPHY_PHY_EXTSTS_LOCK_ER	0x0002	/* Lock error */
#define BRGPHY_PHY_EXTSTS_MLT3_ER	0x0001	/* MLT3 code error */

#define BRGPHY_MII_RXERRCNT	0x12	/* RX error counter */

#define BRGPHY_MII_FCERRCNT	0x13	/* false carrier sense counter */
#define BGRPHY_FCERRCNT		0x00FF	/* False carrier counter */

#define BRGPHY_MII_RXNOCNT	0x14	/* RX not OK counter */
#define BRGPHY_RXNOCNT_LOCAL	0xFF00	/* Local RX not OK counter */
#define BRGPHY_RXNOCNT_REMOTE	0x00FF	/* Local RX not OK counter */

#define BRGPHY_MII_DSP_RW_PORT	0x15	/* DSP coefficient r/w port */

#define BRGPHY_MII_EPHY_PTEST	0x17	/* 5906 PHY register */
#define BRGPHY_MII_DSP_ADDR_REG	0x17	/* DSP coefficient addr register */

#define BRGPHY_DSP_TAP_NUMBER_MASK		0x00
#define BRGPHY_DSP_AGC_A			0x00
#define BRGPHY_DSP_AGC_B			0x01
#define BRGPHY_DSP_MSE_PAIR_STATUS		0x02
#define BRGPHY_DSP_SOFT_DECISION		0x03
#define BRGPHY_DSP_PHASE_REG			0x04
#define BRGPHY_DSP_SKEW				0x05
#define BRGPHY_DSP_POWER_SAVER_UPPER_BOUND	0x06
#define BRGPHY_DSP_POWER_SAVER_LOWER_BOUND	0x07
#define BRGPHY_DSP_LAST_ECHO			0x08
#define BRGPHY_DSP_FREQUENCY			0x09
#define BRGPHY_DSP_PLL_BANDWIDTH		0x0A
#define BRGPHY_DSP_PLL_PHASE_OFFSET		0x0B

#define BRGPHYDSP_FILTER_DCOFFSET	0x0C00
#define BRGPHY_DSP_FILTER_FEXT3		0x0B00
#define BRGPHY_DSP_FILTER_FEXT2		0x0A00
#define BRGPHY_DSP_FILTER_FEXT1		0x0900
#define BRGPHY_DSP_FILTER_FEXT0		0x0800
#define BRGPHY_DSP_FILTER_NEXT3		0x0700
#define BRGPHY_DSP_FILTER_NEXT2		0x0600
#define BRGPHY_DSP_FILTER_NEXT1		0x0500
#define BRGPHY_DSP_FILTER_NEXT0		0x0400
#define BRGPHY_DSP_FILTER_ECHO		0x0300
#define BRGPHY_DSP_FILTER_DFE		0x0200
#define BRGPHY_DSP_FILTER_FFE		0x0100

#define BRGPHY_DSP_CONTROL_ALL_FILTERS	0x1000

#define BRGPHY_DSP_SEL_CH_0		0x0000
#define BRGPHY_DSP_SEL_CH_1		0x2000
#define BRGPHY_DSP_SEL_CH_2		0x4000
#define BRGPHY_DSP_SEL_CH_3		0x6000

#define BRGPHY_MII_AUXCTL	0x18	/* AUX control */
#define BRGPHY_AUXCTL_LOW_SQ	0x8000	/* Low squelch */
#define BRGPHY_AUXCTL_LONG_PKT	0x4000	/* RX long packets */
#define BRGPHY_AUXCTL_ER_CTL	0x3000	/* Edgerate control */
#define BRGPHY_AUXCTL_TX_TST	0x0400	/* TX test, always 1 */
#define BRGPHY_AUXCTL_DIS_PRF	0x0080	/* dis part resp filter */
#define BRGPHY_AUXCTL_DIAG_MODE	0x0004	/* Diagnostic mode */

#define BRGPHY_MII_AUXSTS	0x19	/* AUX status */
#define BRGPHY_AUXSTS_ACOMP	0x8000	/* autoneg complete */
#define BRGPHY_AUXSTS_AN_ACK	0x4000	/* autoneg complete ack */
#define BRGPHY_AUXSTS_AN_ACK_D	0x2000	/* autoneg complete ack detect */
#define BRGPHY_AUXSTS_AN_NPW	0x1000	/* autoneg next page wait */
#define BRGPHY_AUXSTS_AN_RES	0x0700	/* autoneg HCD */
#define BRGPHY_AUXSTS_PDF	0x0080	/* Parallel detect. fault */
#define BRGPHY_AUXSTS_RF	0x0040	/* remote fault */
#define BRGPHY_AUXSTS_ANP_R	0x0020	/* AN page received */
#define BRGPHY_AUXSTS_LP_ANAB	0x0010	/* LP AN ability */
#define BRGPHY_AUXSTS_LP_NPAB	0x0008	/* LP Next page ability */
#define BRGPHY_AUXSTS_LINK	0x0004	/* Link status */
#define BRGPHY_AUXSTS_PRR	0x0002	/* Pause resolution-RX */
#define BRGPHY_AUXSTS_PRT	0x0001	/* Pause resolution-TX */

#define BRGPHY_RES_1000FD	0x0700	/* 1000baseT full duplex */
#define BRGPHY_RES_1000HD	0x0600	/* 1000baseT half duplex */
#define BRGPHY_RES_100FD	0x0500	/* 100baseT full duplex */
#define BRGPHY_RES_100T4	0x0400	/* 100baseT4 */
#define BRGPHY_RES_100HD	0x0300	/* 100baseT half duplex */
#define BRGPHY_RES_10FD		0x0200	/* 10baseT full duplex */
#define BRGPHY_RES_10HD		0x0100	/* 10baseT half duplex */
/* 5906 */
#define BRGPHY_RES_100		0x0008	/* 100baseT */
#define BRGPHY_RES_FULL		0x0001	/* full duplex */

#define BRGPHY_MII_ISR		0x1A	/* interrupt status */
#define BRGPHY_ISR_PSERR	0x4000	/* Pair swap error */
#define BRGPHY_ISR_MDXI_SC	0x2000	/* MDIX Status Change */
#define BRGPHY_ISR_HCT		0x1000	/* counter above 32K */
#define BRGPHY_ISR_LCT		0x0800	/* all counter below 128 */
#define BRGPHY_ISR_AN_PR	0x0400	/* Autoneg page received */
#define BRGPHY_ISR_NO_HDCL	0x0200	/* No HCD Link */
#define BRGPHY_ISR_NO_HDC	0x0100	/* No HCD */
#define BRGPHY_ISR_USHDC	0x0080	/* Negotiated Unsupported HCD */
#define BRGPHY_ISR_SCR_S_ERR	0x0040	/* Scrambler sync error */
#define BRGPHY_ISR_RRS_CHG	0x0020	/* Remote RX status change */
#define BRGPHY_ISR_LRS_CHG	0x0010	/* Local RX status change */
#define BRGPHY_ISR_DUP_CHG	0x0008	/* Duplex mode change */
#define BRGPHY_ISR_LSP_CHG	0x0004	/* Link speed changed */
#define BRGPHY_ISR_LNK_CHG	0x0002	/* Link status change */
#define BRGPHY_ISR_CRCERR	0x0001	/* CEC error */

#define BRGPHY_MII_IMR		0x1B	/* interrupt mask */
#define BRGPHY_IMR_PSERR	0x4000	/* Pair swap error */
#define BRGPHY_IMR_MDXI_SC	0x2000	/* MDIX Status Change */
#define BRGPHY_IMR_HCT		0x1000	/* counter above 32K */
#define BRGPHY_IMR_LCT		0x0800	/* all counter below 128 */
#define BRGPHY_IMR_AN_PR	0x0400	/* Autoneg page received */
#define BRGPHY_IMR_NO_HDCL	0x0200	/* No HCD Link */
#define BRGPHY_IMR_NO_HDC	0x0100	/* No HCD */
#define BRGPHY_IMR_USHDC	0x0080	/* Negotiated Unsupported HCD */
#define BRGPHY_IMR_SCR_S_ERR	0x0040	/* Scrambler sync error */
#define BRGPHY_IMR_RRS_CHG	0x0020	/* Remote RX status change */
#define BRGPHY_IMR_LRS_CHG	0x0010	/* Local RX status change */
#define BRGPHY_IMR_DUP_CHG	0x0008	/* Duplex mode change */
#define BRGPHY_IMR_LSP_CHG	0x0004	/* Link speed changed */
#define BRGPHY_IMR_LNK_CHG	0x0002	/* Link status change */
#define BRGPHY_IMR_CRCERR	0x0001	/* CEC error */

/*******************************************************/
/* Begin: PHY register values for the 5706 PHY         */
/*******************************************************/

/*
 * Aux control shadow register, bits 0-2 select function (0x00 to
 * 0x07).
 */
#define BRGPHY_AUXCTL_SHADOW_MISC	0x07
#define BRGPHY_AUXCTL_MISC_DATA_MASK	0x7ff8
#define BRGPHY_AUXCTL_MISC_READ_SHIFT	12
#define BRGPHY_AUXCTL_MISC_WRITE_EN	0x8000
#define BRGPHY_AUXCTL_MISC_RGMII_SKEW_EN 0x0200
#define BRGPHY_AUXCTL_MISC_WIRESPEED_EN	0x0010

/* 
 * Shadow register 0x1C, bit 15 is write enable,
 * bits 14-10 select function (0x00 to 0x1F).
 */
#define BRGPHY_MII_SHADOW_1C		0x1C
#define BRGPHY_SHADOW_1C_WRITE_EN	0x8000
#define BRGPHY_SHADOW_1C_SELECT_MASK	0x7C00
#define BRGPHY_SHADOW_1C_DATA_MASK	0x03FF

/* Shadow 0x1C Clock Alignment Control Register (select value 0x03) */
#define BRGPHY_SHADOW_1C_CLK_CTRL	(0x03 << 10)
#define BRGPHY_SHADOW_1C_GTXCLK_EN	0x0200

/* Shadow 0x1C Mode Control Register (select value 0x1F) */
#define BRGPHY_SHADOW_1C_MODE_CTRL	(0x1F << 10)
/* When set, Regs 0-0x0F are 1000X, else 1000T */
#define BRGPHY_SHADOW_1C_ENA_1000X	0x0001

#define BRGPHY_TEST1		0x1E
#define BRGPHY_TEST1_TRIM_EN	0x0010
#define BRGPHY_TEST1_CRC_EN	0x8000

#define BRGPHY_MII_TEST2	0x1F

/*******************************************************/
/* End: PHY register values for the 5706 PHY           */
/*******************************************************/

/*******************************************************/
/* Begin: PHY register values for the 5708S SerDes PHY */
/*******************************************************/

#define BRGPHY_5708S_BMCR_2500			0x20

/* Autoneg Next Page Transmit 1 Register */
#define BRGPHY_5708S_ANEG_NXT_PG_XMIT1		0x0B
#define BRGPHY_5708S_ANEG_NXT_PG_XMIT1_25G	0x0001

/* Use the BLOCK_ADDR register to select the page for registers 0x10 to 0x1E */
#define BRGPHY_5708S_BLOCK_ADDR			0x1f
#define BRGPHY_5708S_DIG_PG0 			0x0000
#define BRGPHY_5708S_DIG3_PG2			0x0002
#define BRGPHY_5708S_TX_MISC_PG5		0x0005

/* 5708S SerDes "Digital" Registers (page 0) */
#define BRGPHY_5708S_PG0_1000X_CTL1		0x10
#define BRGPHY_5708S_PG0_1000X_CTL1_FIBER_MODE	0x0001
#define BRGPHY_5708S_PG0_1000X_CTL1_AUTODET_EN	0x0010

#define BRGPHY_5708S_PG0_1000X_STAT1		0x14
#define BRGPHY_5708S_PG0_1000X_STAT1_SGMII	0x0001
#define BRGPHY_5708S_PG0_1000X_STAT1_LINK	0x0002
#define BRGPHY_5708S_PG0_1000X_STAT1_FDX	0x0004
#define BRGPHY_5708S_PG0_1000X_STAT1_SPEED_MASK	0x0018
#define BRGPHY_5708S_PG0_1000X_STAT1_SPEED_10	(0x0 << 3)
#define BRGPHY_5708S_PG0_1000X_STAT1_SPEED_100	(0x1 << 3)
#define BRGPHY_5708S_PG0_1000X_STAT1_SPEED_1G	(0x2 << 3)
#define BRGPHY_5708S_PG0_1000X_STAT1_SPEED_25G	(0x3 << 3)
#define BRGPHY_5708S_PG0_1000X_STAT1_TX_PAUSE	0x0020
#define BRGPHY_5708S_PG0_1000X_STAT1_RX_PAUSE	0x0040

#define BRGPHY_5708S_PG0_1000X_CTL2		0x11
#define BRGPHY_5708S_PG0_1000X_CTL2_PAR_DET_EN	0x0001

/* 5708S SerDes "Digital 3" Registers (page 2) */
#define BRGPHY_5708S_PG2_DIGCTL_3_0		0x10
#define BRGPHY_5708S_PG2_DIGCTL_3_0_USE_IEEE	0x0001

/* 5708S SerDes "TX Misc" Registers (page 5) */
#define BRGPHY_5708S_PG5_2500STATUS1		0x10

#define BRGPHY_5708S_PG5_TXACTL1		0x15
#define BRGPHY_5708S_PG5_TXACTL1_VCM		0x30

#define BRGPHY_5708S_PG5_TXACTL3		0x17

/*******************************************************/
/* End: PHY register values for the 5708S SerDes PHY   */
/*******************************************************/

/*******************************************************/
/* Begin: PHY register values for the 5709S SerDes PHY */
/*******************************************************/

/* 5709S SerDes "General Purpose Status" Registers */
#define BRGPHY_BLOCK_ADDR_GP_STATUS		0x8120
#define BRGPHY_GP_STATUS_TOP_ANEG_STATUS	0x1B
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_MASK	0x3F00
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_10	0x0000
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_100	0x0100
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_1G	0x0200
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_25G	0x0300
#define BRGPHY_GP_STATUS_TOP_ANEG_SPEED_1GKX	0x0D00
#define BRGPHY_GP_STATUS_TOP_ANEG_FDX		0x0008
#define BRGPHY_GP_STATUS_TOP_ANEG_LINK_UP	0x0004
#define BRGPHY_GP_STATUS_TOP_ANEG_CL73_COMP	0x0001

/* 5709S SerDes "SerDes Digital" Registers */
#define BRGPHY_BLOCK_ADDR_SERDES_DIG		0x8300
#define BRGPHY_SERDES_DIG_1000X_CTL1		0x0010
#define BRGPHY_SD_DIG_1000X_CTL1_AUTODET	0x0010
#define BRGPHY_SD_DIG_1000X_CTL1_FIBER		0x0001

/* 5709S SerDes "Over 1G" Registers */
#define BRGPHY_BLOCK_ADDR_OVER_1G		0x8320
#define BRGPHY_OVER_1G_UNFORMAT_PG1		0x19

/* 5709S SerDes "Multi-Rate Backplane Ethernet" Registers */
#define BRGPHY_BLOCK_ADDR_MRBE			0x8350
#define BRGPHY_MRBE_MSG_PG5_NP			0x10
#define BRGPHY_MRBE_MSG_PG5_NP_MBRE		0x0001
#define BRGPHY_MRBE_MSG_PG5_NP_T2		0x0001

/* 5709S SerDes "IEEE Clause 73 User B0" Registers */
#define BRGPHY_BLOCK_ADDR_CL73_USER_B0		0x8370
#define BRGPHY_CL73_USER_B0_MBRE_CTL1		0x12
#define BRGPHY_CL73_USER_B0_MBRE_CTL1_NP_AFT_BP	0x2000
#define BRGPHY_CL73_USER_B0_MBRE_CTL1_STA_MGR	0x4000
#define BRGPHY_CL73_USER_B0_MBRE_CTL1_ANEG	0x8000

/* 5709S SerDes "IEEE Clause 73 User B0" Registers */
#define BRGPHY_BLOCK_ADDR_ADDR_EXT		0xFFD0

/* 5709S SerDes "Combo IEEE 0" Registers */
#define BRGPHY_BLOCK_ADDR_COMBO_IEEE0		0xFFE0

#define BRGPHY_ADDR_EXT				0x1E
#define BRGPHY_BLOCK_ADDR			0x1F

#define BRGPHY_ADDR_EXT_AN_MMD			0x3800

/*******************************************************/
/* End: PHY register values for the 5709S SerDes PHY   */
/*******************************************************/

#define BRGPHY_INTRS	\
	~(BRGPHY_IMR_LNK_CHG|BRGPHY_IMR_LSP_CHG|BRGPHY_IMR_DUP_CHG)

#endif /* _DEV_BRGPHY_MIIREG_H_ */
