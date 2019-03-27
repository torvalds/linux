/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
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

#ifndef _DEV_MII_RGEPHYREG_H_
#define	_DEV_MII_RGEPHYREG_H_

#define	RGEPHY_8211B		2
#define	RGEPHY_8211C		3
#define	RGEPHY_8211F		6

/*
 * RealTek 8169S/8110S gigE PHY registers
 */

#define RGEPHY_MII_BMCR		0x00
#define RGEPHY_BMCR_RESET	0x8000
#define RGEPHY_BMCR_LOOP	0x4000
#define RGEPHY_BMCR_SPD0	0x2000	/* speed select, lower bit */
#define RGEPHY_BMCR_AUTOEN	0x1000	/* Autoneg enabled */
#define RGEPHY_BMCR_PDOWN	0x0800	/* Power down */
#define RGEPHY_BMCR_ISO		0x0400	/* Isolate */
#define RGEPHY_BMCR_STARTNEG	0x0200	/* Restart autoneg */
#define RGEPHY_BMCR_FDX		0x0100	/* Duplex mode */
#define RGEPHY_BMCR_CTEST	0x0080	/* Collision test enable */
#define RGEPHY_BMCR_SPD1	0x0040	/* Speed select, upper bit */

#define RGEPHY_S1000		RGEPHY_BMCR_SPD1	/* 1000mbps */
#define RGEPHY_S100		RGEPHY_BMCR_SPD0	/* 100mpbs */
#define RGEPHY_S10		0			/* 10mbps */

#define RGEPHY_MII_BMSR		0x01
#define RGEPHY_BMSR_100T4	0x8000	/* 100 base T4 capable */
#define RGEPHY_BMSR_100TXFDX	0x4000	/* 100 base Tx full duplex capable */
#define RGEPHY_BMSR_100TXHDX	0x2000	/* 100 base Tx half duplex capable */
#define RGEPHY_BMSR_10TFDX	0x1000	/* 10 base T full duplex capable */
#define RGEPHY_BMSR_10THDX	0x0800	/* 10 base T half duplex capable */
#define RGEPHY_BMSR_100T2FDX	0x0400	/* 100 base T2 full duplex capable */
#define RGEPHY_BMSR_100T2HDX	0x0200	/* 100 base T2 half duplex capable */
#define RGEPHY_BMSR_EXTSTS	0x0100	/* Extended status present */
#define RGEPHY_BMSR_PRESUB	0x0040	/* Preamble surpression */
#define RGEPHY_BMSR_ACOMP	0x0020	/* Autoneg complete */
#define RGEPHY_BMSR_RFAULT	0x0010	/* Remote fault condition occurred */
#define RGEPHY_BMSR_ANEG	0x0008	/* Autoneg capable */
#define RGEPHY_BMSR_LINK	0x0004	/* Link status */
#define RGEPHY_BMSR_JABBER	0x0002	/* Jabber detected */
#define RGEPHY_BMSR_EXT		0x0001	/* Extended capability */

#define RGEPHY_MII_ANAR		0x04
#define RGEPHY_ANAR_NP		0x8000	/* Next page */
#define RGEPHY_ANAR_RF		0x2000	/* Remote fault */
#define RGEPHY_ANAR_ASP		0x0800	/* Asymmetric Pause */
#define RGEPHY_ANAR_PC		0x0400	/* Pause capable */
#define RGEPHY_ANAR_T4		0x0200	/* local device supports 100bT4 */
#define RGEPHY_ANAR_TX_FD	0x0100	/* local device supports 100bTx FD */
#define RGEPHY_ANAR_TX		0x0080	/* local device supports 100bTx */
#define RGEPHY_ANAR_10_FD	0x0040	/* local device supports 10bT FD */
#define RGEPHY_ANAR_10		0x0020	/* local device supports 10bT */
#define RGEPHY_ANAR_SEL		0x001F	/* selector field, 00001=Ethernet */

#define RGEPHY_MII_ANLPAR	0x05
#define RGEPHY_ANLPAR_NP	0x8000	/* Next page */
#define RGEPHY_ANLPAR_RF	0x2000	/* Remote fault */
#define RGEPHY_ANLPAR_ASP	0x0800	/* Asymmetric Pause */
#define RGEPHY_ANLPAR_PC	0x0400	/* Pause capable */
#define RGEPHY_ANLPAR_T4	0x0200	/* link partner supports 100bT4 */
#define RGEPHY_ANLPAR_TX_FD	0x0100	/* link partner supports 100bTx FD */
#define RGEPHY_ANLPAR_TX	0x0080	/* link partner supports 100bTx */
#define RGEPHY_ANLPAR_10_FD	0x0040	/* link partner supports 10bT FD */
#define RGEPHY_ANLPAR_10	0x0020	/* link partner supports 10bT */
#define RGEPHY_ANLPAR_SEL	0x001F	/* selector field, 00001=Ethernet */

#define RGEPHY_SEL_TYPE		0x0001	/* ethernet */

#define RGEPHY_MII_ANER		0x06
#define RGEPHY_ANER_PDF		0x0010	/* Parallel detection fault */
#define RGEPHY_ANER_LPNP	0x0008	/* Link partner can next page */
#define RGEPHY_ANER_NP		0x0004	/* Local PHY can next page */
#define RGEPHY_ANER_RX		0x0002	/* Next page received */
#define RGEPHY_ANER_LPAN	0x0001 	/* Link partner autoneg capable */

#define RGEPHY_MII_NEXTP	0x07	/* Next page */

#define RGEPHY_MII_NEXTP_LP	0x08	/* Next page of link partner */

#define RGEPHY_MII_1000CTL	0x09	/* 1000baseT control */
#define RGEPHY_1000CTL_TST	0xE000	/* test modes */
#define RGEPHY_1000CTL_MSE	0x1000	/* Master/Slave manual enable */
#define RGEPHY_1000CTL_MSC	0x0800	/* Master/Slave select */
#define RGEPHY_1000CTL_RD	0x0400	/* Repeater/DTE */
#define RGEPHY_1000CTL_AFD	0x0200	/* Advertise full duplex */
#define RGEPHY_1000CTL_AHD	0x0100	/* Advertise half duplex */

#define RGEPHY_TEST_TX_JITTER			0x2000
#define RGEPHY_TEST_TX_JITTER_MASTER_MODE	0x4000
#define RGEPHY_TEST_TX_JITTER_SLAVE_MODE	0x6000
#define RGEPHY_TEST_TX_DISTORTION		0x8000

#define RGEPHY_MII_1000STS	0x0A	/* 1000baseT status */
#define RGEPHY_1000STS_MSF	0x8000	/* Master/slave fault */
#define RGEPHY_1000STS_MSR	0x4000	/* Master/slave result */
#define RGEPHY_1000STS_LRS	0x2000	/* Local receiver status */
#define RGEPHY_1000STS_RRS	0x1000	/* Remote receiver status */
#define RGEPHY_1000STS_LPFD	0x0800	/* Link partner can FD */
#define RGEPHY_1000STS_LPHD	0x0400	/* Link partner can HD */
#define RGEPHY_1000STS_IEC	0x00FF	/* Idle error count */

#define RGEPHY_MII_EXTSTS	0x0F	/* Extended status */
#define RGEPHY_EXTSTS_X_FD_CAP	0x8000	/* 1000base-X FD capable */
#define RGEPHY_EXTSTS_X_HD_CAP	0x4000	/* 1000base-X HD capable */
#define RGEPHY_EXTSTS_T_FD_CAP	0x2000	/* 1000base-T FD capable */
#define RGEPHY_EXTSTS_T_HD_CAP	0x1000	/* 1000base-T HD capable */

/* RTL8211B(L)/RTL8211C(L) */
#define RGEPHY_MII_PCR		0x10	/* PHY Specific control register */
#define RGEPHY_PCR_ASSERT_CRS	0x0800
#define RGEPHY_PCR_FORCE_LINK	0x0400
#define RGEPHY_PCR_MDI_MASK	0x0060
#define RGEPHY_PCR_MDIX_AUTO	0x0040
#define RGEPHY_PCR_MDIX_MANUAL	0x0020
#define RGEPHY_PCR_MDI_MANUAL	0x0000
#define RGEPHY_PCR_CLK125_DIS	0x0010
#define RGEPHY_PCR_JABBER_DIS	0x0001

/* RTL8211B(L)/RTL8211C(L) */
#define RGEPHY_MII_SSR		0x11	/* PHY Specific status register */
#define	RGEPHY_SSR_S1000	0x8000	/* 1000Mbps */
#define	RGEPHY_SSR_S100		0x4000	/* 100Mbps */
#define	RGEPHY_SSR_S10		0x0000	/* 10Mbps */
#define	RGEPHY_SSR_SPD_MASK	0xc000
#define	RGEPHY_SSR_FDX		0x2000	/* full duplex */
#define	RGEPHY_SSR_PAGE_RECEIVED	0x1000	/* new page received */
#define	RGEPHY_SSR_SPD_DPLX_RESOLVED	0x0800	/* speed/duplex resolved */
#define	RGEPHY_SSR_LINK		0x0400	/* link up */
#define	RGEPHY_SSR_MDI_XOVER	0x0040	/* MDI crossover */
#define	RGEPHY_SSR_ALDPS	0x0008	/* RTL8211C(L) only */
#define	RGEPHY_SSR_JABBER	0x0001	/* Jabber */

/* RTL8211F */
#define	RGEPHY_F_MII_PCR1	0x18	/* PHY Specific control register 1 */
#define	RGEPHY_F_PCR1_MDI_MM	0x0200	/* MDI / MDIX Manual Mode */
#define	RGEPHY_F_PCR1_MDI_MODE	0x0100	/* MDI Mode (0=MDIX,1=MDI) */
#define	RGEPHY_F_PCR1_ALDPS_EN	0x0004	/* Link Down Power Saving Enable */

/* RTL8211F */
#define	RGEPHY_F_MII_SSR	0x1A	/* PHY Specific status register */
#define	RGEPHY_F_SSR_S1000	0x0020	/* 1000Mbps */
#define	RGEPHY_F_SSR_S100	0x0010	/* 100Mbps */
#define	RGEPHY_F_SSR_S10	0x0000	/* 10Mbps */
#define	RGEPHY_F_SSR_SPD_MASK	0x0030
#define	RGEPHY_F_SSR_FDX	0x0008	/* full duplex */
#define	RGEPHY_F_SSR_LINK	0x0004	/* link up */
#define	RGEPHY_F_SSR_MDI	0x0002	/* MDI/MDIX */
#define	RGEPHY_F_SSR_JABBER	0x0001	/* Jabber */

/* RTL8211F */
#define	RGEPHY_F_EPAGSR		0x1F	/* Extension page select register */

/* RTL8211F */
#define	RGEPHY_F_MMD_DEV_7	0x07

/* RTL8211F MMD device 7 */
#define	RGEPHY_F_MMD_EEEAR	0x3C	/* EEE advertisement */
#define	EEEAR_1000T		0x0004	/* adv. 1000baseT EEE */
#define	EEEAR_100TX		0x0002	/* adv. 100baseTX EEE */

/* RTL8211F MMD device 7 */
#define	RGEPHY_F_MMD_EEELPAR	0x3D	/* EEE link partner abilities */
#define	EEELPAR_1000T		0x0004	/* link partner 1000baseT EEE capable */
#define	EEELPAR_100TX		0x0002	/* link partner 100baseTX EEE capable */

/* RTL8153 */
#define	URE_GMEDIASTAT		0xe908	/* media status register */

#endif /* _DEV_RGEPHY_MIIREG_H_ */
