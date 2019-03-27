/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD$
 */

#ifndef _DEV_MII_CIPHYREG_H_
#define	_DEV_MII_CIPHYREG_H_

/*
 * Register definitions for the Cicada CS8201 10/100/1000 gigE copper
 * PHY, embedded within the VIA Networks VT6122 controller.
 */

/* Command register */
#define CIPHY_MII_BMCR		0x00
#define CIPHY_BMCR_RESET	0x8000
#define CIPHY_BMCR_LOOP		0x4000
#define CIPHY_BMCR_SPD0		0x2000	/* speed select, lower bit */
#define CIPHY_BMCR_AUTOEN	0x1000	/* Autoneg enabled */
#define CIPHY_BMCR_PDOWN	0x0800	/* Power down */
#define CIPHY_BMCR_STARTNEG	0x0200	/* Restart autoneg */
#define CIPHY_BMCR_FDX		0x0100	/* Duplex mode */
#define CIPHY_BMCR_CTEST	0x0080	/* Collision test enable */
#define CIPHY_BMCR_SPD1		0x0040	/* Speed select, upper bit */

#define CIPHY_S1000		CIPHY_BMCR_SPD1	/* 1000mbps */
#define CIPHY_S100		CIPHY_BMCR_SPD0	/* 100mpbs */
#define CIPHY_S10		0		/* 10mbps */

/* Status register */
#define CIPHY_MII_BMSR		0x01
#define CIPHY_BMSR_100T4	0x8000	/* 100 base T4 capable */
#define CIPHY_BMSR_100TXFDX	0x4000	/* 100 base Tx full duplex capable */
#define CIPHY_BMSR_100TXHDX	0x2000	/* 100 base Tx half duplex capable */
#define CIPHY_BMSR_10TFDX	0x1000	/* 10 base T full duplex capable */
#define CIPHY_BMSR_10THDX	0x0800	/* 10 base T half duplex capable */
#define CIPHY_BMSR_100T2FDX	0x0400	/* 100 base T2 full duplex capable */
#define CIPHY_BMSR_100T2HDX	0x0200	/* 100 base T2 half duplex capable */
#define CIPHY_BMSR_EXTSTS	0x0100	/* Extended status present */
#define CIPHY_BMSR_PRESUB	0x0040	/* Preamble surpression */
#define CIPHY_BMSR_ACOMP	0x0020	/* Autoneg complete */
#define CIPHY_BMSR_RFAULT	0x0010	/* Remote fault condition occurred */
#define CIPHY_BMSR_ANEG		0x0008	/* Autoneg capable */
#define CIPHY_BMSR_LINK		0x0004	/* Link status */
#define CIPHY_BMSR_JABBER	0x0002	/* Jabber detected */
#define CIPHY_BMSR_EXT		0x0001	/* Extended capability */

/* PHY ID registers */
#define CIPHY_MII_PHYIDR1	0x02
#define CIPHY_MII_PHYIDR2	0x03

/* Autoneg advertisement */
#define CIPHY_MII_ANAR		0x04
#define CIPHY_ANAR_NP		0x8000	/* Next page */
#define CIPHY_ANAR_RF		0x2000	/* Remote fault */
#define CIPHY_ANAR_ASP		0x0800	/* Asymmetric Pause */
#define CIPHY_ANAR_PC		0x0400	/* Pause capable */
#define CIPHY_ANAR_T4		0x0200	/* local device supports 100bT4 */
#define CIPHY_ANAR_TX_FD	0x0100	/* local device supports 100bTx FD */
#define CIPHY_ANAR_TX		0x0080	/* local device supports 100bTx */
#define CIPHY_ANAR_10_FD	0x0040	/* local device supports 10bT FD */
#define CIPHY_ANAR_10		0x0020	/* local device supports 10bT */
#define CIPHY_ANAR_SEL		0x001F	/* selector field, 00001=Ethernet */

/* Autoneg link partner ability */
#define CIPHY_MII_ANLPAR	0x05
#define CIPHY_ANLPAR_NP		0x8000	/* Next page */
#define CIPHY_ANLPAR_ACK	0x4000	/* link partner acknowledge */
#define CIPHY_ANLPAR_RF		0x2000	/* Remote fault */
#define CIPHY_ANLPAR_ASP	0x0800	/* Asymmetric Pause */
#define CIPHY_ANLPAR_PC		0x0400	/* Pause capable */
#define CIPHY_ANLPAR_T4		0x0200	/* link partner supports 100bT4 */
#define CIPHY_ANLPAR_TX_FD	0x0100	/* link partner supports 100bTx FD */
#define CIPHY_ANLPAR_TX		0x0080	/* link partner supports 100bTx */
#define CIPHY_ANLPAR_10_FD	0x0040	/* link partner supports 10bT FD */
#define CIPHY_ANLPAR_10		0x0020	/* link partner supports 10bT */
#define CIPHY_ANLPAR_SEL	0x001F	/* selector field, 00001=Ethernet */

#define CIPHY_SEL_TYPE		0x0001	/* ethernet */

/* Antoneg expansion register */
#define CIPHY_MII_ANER		0x06
#define CIPHY_ANER_PDF		0x0010	/* Parallel detection fault */
#define CIPHY_ANER_LPNP		0x0008	/* Link partner can next page */
#define CIPHY_ANER_NP		0x0004	/* Local PHY can next page */
#define CIPHY_ANER_RX		0x0002	/* Next page received */
#define CIPHY_ANER_LPAN		0x0001 	/* Link partner autoneg capable */

/* Autoneg next page transmit regisyer */
#define CIPHY_MII_NEXTP		0x07
#define CIPHY_NEXTP_MOREP	0x8000	/* More pages to follow */
#define CIPHY_NEXTP_MESS	0x2000	/* 1 = message page, 0 = unformatted */
#define CIPHY_NEXTP_ACK2	0x1000	/* MAC acknowledge */
#define CIPHY_NEXTP_TOGGLE	0x0800	/* Toggle */
#define CIPHY_NEXTP_CODE	0x07FF	/* Code bits */

/* Autoneg link partner next page receive register */
#define CIPHY_MII_NEXTP_LP	0x08
#define CIPHY_NEXTPLP_MOREP	0x8000	/* More pages to follow */
#define CIPHY_NEXTPLP_MESS	0x2000	/* 1 = message page, 0 = unformatted */
#define CIPHY_NEXTPLP_ACK2	0x1000	/* MAC acknowledge */
#define CIPHY_NEXTPLP_TOGGLE	0x0800	/* Toggle */
#define CIPHY_NEXTPLP_CODE	0x07FF	/* Code bits */

/* 1000BT control register */
#define CIPHY_MII_1000CTL	0x09
#define CIPHY_1000CTL_TST	0xE000	/* test modes */
#define CIPHY_1000CTL_MSE	0x1000	/* Master/Slave manual enable */
#define CIPHY_1000CTL_MSC	0x0800	/* Master/Slave select */
#define CIPHY_1000CTL_RD	0x0400	/* Repeater/DTE */
#define CIPHY_1000CTL_AFD	0x0200	/* Advertise full duplex */
#define CIPHY_1000CTL_AHD	0x0100	/* Advertise half duplex */

#define CIPHY_TEST_TX_JITTER			0x2000
#define CIPHY_TEST_TX_JITTER_MASTER_MODE	0x4000
#define CIPHY_TEST_TX_JITTER_SLAVE_MODE		0x6000
#define CIPHY_TEST_TX_DISTORTION		0x8000

/* 1000BT status register */
#define CIPHY_MII_1000STS	0x0A
#define CIPHY_1000STS_MSF	0x8000	/* Master/slave fault */
#define CIPHY_1000STS_MSR	0x4000	/* Master/slave result */
#define CIPHY_1000STS_LRS	0x2000	/* Local receiver status */
#define CIPHY_1000STS_RRS	0x1000	/* Remote receiver status */
#define CIPHY_1000STS_LPFD	0x0800	/* Link partner can FD */
#define CIPHY_1000STS_LPHD	0x0400	/* Link partner can HD */
#define CIPHY_1000STS_IEC	0x00FF	/* Idle error count */

#define CIPHY_MII_EXTSTS	0x0F	/* Extended status */
#define CIPHY_EXTSTS_X_FD_CAP	0x8000	/* 1000base-X FD capable */
#define CIPHY_EXTSTS_X_HD_CAP	0x4000	/* 1000base-X HD capable */
#define CIPHY_EXTSTS_T_FD_CAP	0x2000	/* 1000base-T FD capable */
#define CIPHY_EXTSTS_T_HD_CAP	0x1000	/* 1000base-T HD capable */

/* 1000BT status extension register #1 */
#define CIPHY_MII_1000STS1	0x0F
#define CIPHY_1000STS1_1000XFDX	0x8000	/* 1000baseX FDX capable */
#define CIPHY_1000STS1_1000XHDX	0x4000	/* 1000baseX HDX capable */
#define CIPHY_1000STS1_1000TFDX	0x2000	/* 1000baseT FDX capable */
#define CIPHY_1000STS1_1000THDX	0x1000	/* 1000baseT HDX capable */

/* Vendor-specific PHY registers */

/* 100baseTX status extension register */
#define CIPHY_MII_100STS	0x10
#define CIPHY_100STS_DESLCK	0x8000	/* descrambler locked */
#define CIPHY_100STS_LKCERR	0x4000	/* lock error detected/lock lost */
#define CIPHY_100STS_DISC	0x2000	/* disconnect state */
#define CIPHY_100STS_LINK	0x1000	/* current link state */
#define CIPHY_100STS_RXERR	0x0800	/* receive error detected */
#define CIPHY_100STS_TXERR	0x0400	/* transmit error detected */
#define CIPHY_100STS_SSDERR	0x0200	/* false carrier error detected */
#define CIPHY_100STS_ESDERR	0x0100	/* premature end of stream error */

/* 1000BT status extension register #2 */
#define CIPHY_MII_1000STS2	0x11
#define CIPHY_1000STS2_DESLCK	0x8000	/* descrambler locked */
#define CIPHY_1000STS2_LKCERR	0x4000	/* lock error detected/lock lost */
#define CIPHY_1000STS2_DISC	0x2000	/* disconnect state */
#define CIPHY_1000STS2_LINK	0x1000	/* current link state */
#define CIPHY_1000STS2_RXERR	0x0800	/* receive error detected */
#define CIPHY_1000STS2_TXERR	0x0400	/* transmit error detected */
#define CIPHY_1000STS2_SSDERR	0x0200	/* false carrier error detected */
#define CIPHY_1000STS2_ESDERR	0x0100	/* premature end of stream error */
#define CIPHY_1000STS2_CARREXT	0x0080	/* carrier extension err detected */
#define CIPHY_1000STS2_BCM5400	0x0040	/* non-complient BCM5400 detected */

/* Bypass control register */
#define CIPHY_MII_BYPASS	0x12
#define CIPHY_BYPASS_TX		0x8000	/* transmit disable */
#define CIPHY_BYPASS_4B5B	0x4000	/* bypass the 4B5B encoder */
#define CIPHY_BYPASS_SCRAM	0x2000	/* bypass scrambler */
#define CIPHY_BYPASS_DSCAM	0x1000	/* bypass descrambler */
#define CIPHY_BYPASS_PCSRX	0x0800	/* bypass PCS receive */
#define CIPHY_BYPASS_PCSTX	0x0400	/* bypass PCS transmit */
#define CIPHY_BYPASS_LFI	0x0200	/* bypass LFI timer */
#define CIPHY_BYPASS_TXCLK	0x0100	/* enable transmit clock on LED4 pin */
#define CIPHY_BYPASS_BCM5400_F	0x0080	/* force BCM5400 detect */
#define CIPHY_BYPASS_BCM5400	0x0040	/* bypass BCM5400 detect */
#define CIPHY_BYPASS_PAIRSWAP	0x0020	/* disable automatic pair swap */
#define CIPHY_BYPASS_POLARITY	0x0010	/* disable polarity correction */
#define CIPHY_BYPASS_PARALLEL	0x0008	/* parallel detect enable */
#define CIPHY_BYPASS_PULSE	0x0004	/* disable pulse shaping filter */
#define CIPHY_BYPASS_1000BNP	0x0002	/* disable 1000BT next page exchange */

/* RX error count register */
#define CIPHY_MII_RXERR		0x13

/* False carrier sense count register */
#define CIPHY_MII_FCSERR	0x14

/* Ddisconnect error counter */
#define CIPHY_MII_DISCERR	0x15

/* 10baseT control/status register */
#define CIPHY_MII_10BTCSR	0x16
#define CIPHY_10BTCSR_DLIT	0x8000	/* Disable data link integrity test */
#define CIPHY_10BTCSR_JABBER	0x4000	/* Disable jabber detect */
#define CIPHY_10BTCSR_ECHO	0x2000	/* Disable echo mode */
#define CIPHY_10BTCSR_SQE	0x1000	/* Disable signal quality error */
#define CIPHY_10BTCSR_SQUENCH	0x0C00	/* Squelch control */
#define CIPHY_10BTCSR_EOFERR	0x0100	/* End of Frame error */
#define CIPHY_10BTCSR_DISC	0x0080	/* Disconnect status */
#define CIPHY_10BTCSR_LINK	0x0040	/* current link state */
#define CIPHY_10BTCSR_ITRIM	0x0038	/* current reference trim */
#define CIPHY_10BTCSR_CSR	0x0006	/* CSR behavior control */

#define CIPHY_SQUELCH_300MV	0x0000
#define CIPHY_SQUELCH_197MV	0x0400
#define CIPHY_SQUELCH_450MV	0x0800
#define CIPHY_SQUELCH_RSVD	0x0C00

#define CIPHY_ITRIM_PLUS2	0x0000
#define CIPHY_ITRIM_PLUS4	0x0008
#define CIPHY_ITRIM_PLUS6	0x0010
#define CIPHY_ITRIM_PLUS6_	0x0018
#define CIPHY_ITRIM_MINUS4	0x0020
#define CIPHY_ITRIM_MINUS4_	0x0028
#define CIPHY_ITRIM_MINUS2	0x0030
#define CIPHY_ITRIM_ZERO	0x0038

/* Extended PHY control register #1 */
#define CIPHY_MII_ECTL1		0x17
#define CIPHY_ECTL1_ACTIPHY	0x0020	/* Enable ActiPHY power saving */
#define CIPHY_ECTL1_IOVOL	0x0e00	/* MAC interface and I/O voltage select */
#define CIPHY_ECTL1_INTSEL	0xf000	/* select MAC interface */

#define CIPHY_IOVOL_3300MV	0x0000	/* 3.3V for I/O pins */
#define CIPHY_IOVOL_2500MV	0x0200	/* 2.5V for I/O pins */

#define CIPHY_INTSEL_GMII	0x0000	/* GMII/MII */
#define CIPHY_INTSEL_RGMII	0x1000
#define CIPHY_INTSEL_TBI	0x2000
#define CIPHY_INTSEL_RTBI	0x3000

/* Extended PHY control register #2 */
#define CIPHY_MII_ECTL2		0x18
#define CIPHY_ECTL2_ERATE	0xE000	/* 10/1000 edge rate control */
#define CIPHY_ECTL2_VTRIM	0x1C00	/* voltage reference trim */
#define CIPHY_ECTL2_CABLELEN	0x000E	/* Cable quality/length */
#define CIPHY_ECTL2_ANALOGLOOP	0x0001	/* 1000BT analog loopback */

#define CIPHY_CABLELEN_0TO10M		0x0000
#define CIPHY_CABLELEN_10TO20M		0x0002
#define CIPHY_CABLELEN_20TO40M		0x0004
#define CIPHY_CABLELEN_40TO80M		0x0006
#define CIPHY_CABLELEN_80TO100M		0x0008
#define CIPHY_CABLELEN_100TO140M	0x000A
#define CIPHY_CABLELEN_140TO180M	0x000C
#define CIPHY_CABLELEN_OVER180M		0x000E

/* Interrupt mask register */
#define CIPHY_MII_IMR		0x19
#define CIPHY_IMR_PINENABLE	0x8000	/* Interrupt pin enable */
#define CIPHY_IMR_SPEED		0x4000	/* speed changed event */
#define CIPHY_IMR_LINK		0x2000	/* link change/ActiPHY event */
#define CIPHY_IMR_DPX		0x1000	/* duplex change event */
#define CIPHY_IMR_ANEGERR	0x0800	/* autoneg error event */
#define CIPHY_IMR_ANEGDONE	0x0400	/* autoneg done event */
#define CIPHY_IMR_NPRX		0x0200	/* page received event */
#define CIPHY_IMR_SYMERR	0x0100	/* symbol error event */
#define CIPHY_IMR_LOCKERR	0x0080	/* descrambler lock lost event */
#define CIPHY_IMR_XOVER		0x0040	/* MDI crossover change event */
#define CIPHY_IMR_POLARITY	0x0020	/* polarity change event */
#define CIPHY_IMR_JABBER	0x0010	/* jabber detect event */
#define CIPHY_IMR_SSDERR	0x0008	/* false carrier detect event */
#define CIPHY_IMR_ESDERR	0x0004	/* parallel detect error event */
#define CIPHY_IMR_MASTERSLAVE	0x0002	/* master/slave resolve done event */
#define CIPHY_IMR_RXERR		0x0001	/* RX error event */

/* Interrupt status register */
#define CIPHY_MII_ISR		0x1A
#define CIPHY_ISR_IPENDING	0x8000	/* Interrupt is pending */
#define CIPHY_ISR_SPEED		0x4000	/* speed changed event */
#define CIPHY_ISR_LINK		0x2000	/* link change/ActiPHY event */
#define CIPHY_ISR_DPX		0x1000	/* duplex change event */
#define CIPHY_ISR_ANEGERR	0x0800	/* autoneg error event */
#define CIPHY_ISR_ANEGDONE	0x0400	/* autoneg done event */
#define CIPHY_ISR_NPRX		0x0200	/* page received event */
#define CIPHY_ISR_SYMERR	0x0100	/* symbol error event */
#define CIPHY_ISR_LOCKERR	0x0080	/* descrambler lock lost event */
#define CIPHY_ISR_XOVER		0x0040	/* MDI crossover change event */
#define CIPHY_ISR_POLARITY	0x0020	/* polarity change event */
#define CIPHY_ISR_JABBER	0x0010	/* jabber detect event */
#define CIPHY_ISR_SSDERR	0x0008	/* false carrier detect event */
#define CIPHY_ISR_ESDERR	0x0004	/* parallel detect error event */
#define CIPHY_ISR_MASTERSLAVE	0x0002	/* master/slave resolve done event */
#define CIPHY_ISR_RXERR		0x0001	/* RX error event */

/* LED control register */
#define CIPHY_MII_LED		0x1B
#define CIPHY_LED_LINK10FORCE	0x8000	/* Force on link10 LED */
#define CIPHY_LED_LINK10DIS	0x4000	/* Disable link10 LED */
#define CIPHY_LED_LINK100FORCE	0x2000	/* Force on link10 LED */
#define CIPHY_LED_LINK100DIS	0x1000	/* Disable link100 LED */
#define CIPHY_LED_LINK1000FORCE	0x0800	/* Force on link1000 LED */
#define CIPHY_LED_LINK1000DIS	0x0400	/* Disable link1000 LED */
#define CIPHY_LED_FDXFORCE	0x0200	/* Force on duplex LED */
#define CIPHY_LED_FDXDIS	0x0100	/* Disable duplex LED */
#define CIPHY_LED_ACTFORCE	0x0080	/* Force on activity LED */
#define CIPHY_LED_ACTDIS	0x0040	/* Disable activity LED */
#define CIPHY_LED_PULSE		0x0008	/* LED pulse enable */
#define CIPHY_LED_LINKACTBLINK	0x0004	/* enable link/activity LED blink */
#define CIPHY_LED_BLINKRATE	0x0002	/* blink rate 0=10hz, 1=5hz */

/* Auxiliary control and status register */
#define CIPHY_MII_AUXCSR	0x1C
#define CIPHY_AUXCSR_ANEGDONE	0x8000	/* Autoneg complete */
#define CIPHY_AUXCSR_ANEGOFF	0x4000	/* Autoneg disabled */
#define CIPHY_AUXCSR_XOVER	0x2000	/* MDI/MDI-X crossover indication */
#define CIPHY_AUXCSR_PAIRSWAP	0x1000	/* pair swap indication */
#define CIPHY_AUXCSR_APOLARITY	0x0800	/* polarity inversion pair A */
#define CIPHY_AUXCSR_BPOLARITY	0x0400	/* polarity inversion pair B */
#define CIPHY_AUXCSR_CPOLARITY	0x0200	/* polarity inversion pair C */
#define CIPHY_AUXCSR_DPOLARITY	0x0100	/* polarity inversion pair D */
#define CIPHY_AUXCSR_FDX	0x0020	/* duplex 1=full, 0=half */
#define CIPHY_AUXCSR_SPEED	0x0018	/* speed */
#define CIPHY_AUXCSR_MDPPS	0x0004	/* No idea, not documented */
#define CIPHY_AUXCSR_STICKYREST 0x0002	/* reset clears sticky bits */

#define CIPHY_SPEED10		0x0000
#define CIPHY_SPEED100		0x0008
#define CIPHY_SPEED1000		0x0010

/* Delay skew status register */
#define CIPHY_MII_DSKEW		0x1D
#define CIPHY_DSKEW_PAIRA	0x7000	/* Pair A skew in symbol times */
#define CIPHY_DSKEW_PAIRB	0x0700	/* Pair B skew in symbol times */
#define CIPHY_DSKEW_PAIRC	0x0070	/* Pair C skew in symbol times */
#define CIPHY_DSKEW_PAIRD	0x0007	/* Pair D skew in symbol times */

#endif /* _DEV_CIPHY_MIIREG_H_ */
