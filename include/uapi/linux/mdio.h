/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * linux/mdio.h: definitions for MDIO (clause 45) transceivers
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef _UAPI__LINUX_MDIO_H__
#define _UAPI__LINUX_MDIO_H__

#include <linux/types.h>
#include <linux/mii.h>

/* MDIO Manageable Devices (MMDs). */
#define MDIO_MMD_PMAPMD		1	/* Physical Medium Attachment/
					 * Physical Medium Dependent */
#define MDIO_MMD_WIS		2	/* WAN Interface Sublayer */
#define MDIO_MMD_PCS		3	/* Physical Coding Sublayer */
#define MDIO_MMD_PHYXS		4	/* PHY Extender Sublayer */
#define MDIO_MMD_DTEXS		5	/* DTE Extender Sublayer */
#define MDIO_MMD_TC		6	/* Transmission Convergence */
#define MDIO_MMD_AN		7	/* Auto-Negotiation */
#define MDIO_MMD_C22EXT		29	/* Clause 22 extension */
#define MDIO_MMD_VEND1		30	/* Vendor specific 1 */
#define MDIO_MMD_VEND2		31	/* Vendor specific 2 */

/* Generic MDIO registers. */
#define MDIO_CTRL1		MII_BMCR
#define MDIO_STAT1		MII_BMSR
#define MDIO_DEVID1		MII_PHYSID1
#define MDIO_DEVID2		MII_PHYSID2
#define MDIO_SPEED		4	/* Speed ability */
#define MDIO_DEVS1		5	/* Devices in package */
#define MDIO_DEVS2		6
#define MDIO_CTRL2		7	/* 10G control 2 */
#define MDIO_STAT2		8	/* 10G status 2 */
#define MDIO_PMA_TXDIS		9	/* 10G PMA/PMD transmit disable */
#define MDIO_PMA_RXDET		10	/* 10G PMA/PMD receive signal detect */
#define MDIO_PMA_EXTABLE	11	/* 10G PMA/PMD extended ability */
#define MDIO_PKGID1		14	/* Package identifier */
#define MDIO_PKGID2		15
#define MDIO_AN_ADVERTISE	16	/* AN advertising (base page) */
#define MDIO_AN_LPA		19	/* AN LP abilities (base page) */
#define MDIO_PCS_EEE_ABLE	20	/* EEE Capability register */
#define MDIO_PCS_EEE_WK_ERR	22	/* EEE wake error counter */
#define MDIO_PHYXS_LNSTAT	24	/* PHY XGXS lane state */
#define MDIO_AN_EEE_ADV		60	/* EEE advertisement */
#define MDIO_AN_EEE_LPABLE	61	/* EEE link partner ability */

/* Media-dependent registers. */
#define MDIO_PMA_10GBT_SWAPPOL	130	/* 10GBASE-T pair swap & polarity */
#define MDIO_PMA_10GBT_TXPWR	131	/* 10GBASE-T TX power control */
#define MDIO_PMA_10GBT_SNR	133	/* 10GBASE-T SNR margin, lane A.
					 * Lanes B-D are numbered 134-136. */
#define MDIO_PMA_10GBR_FECABLE	170	/* 10GBASE-R FEC ability */
#define MDIO_PCS_10GBX_STAT1	24	/* 10GBASE-X PCS status 1 */
#define MDIO_PCS_10GBRT_STAT1	32	/* 10GBASE-R/-T PCS status 1 */
#define MDIO_PCS_10GBRT_STAT2	33	/* 10GBASE-R/-T PCS status 2 */
#define MDIO_AN_10GBT_CTRL	32	/* 10GBASE-T auto-negotiation control */
#define MDIO_AN_10GBT_STAT	33	/* 10GBASE-T auto-negotiation status */

/* LASI (Link Alarm Status Interrupt) registers, defined by XENPAK MSA. */
#define MDIO_PMA_LASI_RXCTRL	0x9000	/* RX_ALARM control */
#define MDIO_PMA_LASI_TXCTRL	0x9001	/* TX_ALARM control */
#define MDIO_PMA_LASI_CTRL	0x9002	/* LASI control */
#define MDIO_PMA_LASI_RXSTAT	0x9003	/* RX_ALARM status */
#define MDIO_PMA_LASI_TXSTAT	0x9004	/* TX_ALARM status */
#define MDIO_PMA_LASI_STAT	0x9005	/* LASI status */

/* Control register 1. */
/* Enable extended speed selection */
#define MDIO_CTRL1_SPEEDSELEXT		(BMCR_SPEED1000 | BMCR_SPEED100)
/* All speed selection bits */
#define MDIO_CTRL1_SPEEDSEL		(MDIO_CTRL1_SPEEDSELEXT | 0x003c)
#define MDIO_CTRL1_FULLDPLX		BMCR_FULLDPLX
#define MDIO_CTRL1_LPOWER		BMCR_PDOWN
#define MDIO_CTRL1_RESET		BMCR_RESET
#define MDIO_PMA_CTRL1_LOOPBACK		0x0001
#define MDIO_PMA_CTRL1_SPEED1000	BMCR_SPEED1000
#define MDIO_PMA_CTRL1_SPEED100		BMCR_SPEED100
#define MDIO_PCS_CTRL1_LOOPBACK		BMCR_LOOPBACK
#define MDIO_PHYXS_CTRL1_LOOPBACK	BMCR_LOOPBACK
#define MDIO_AN_CTRL1_RESTART		BMCR_ANRESTART
#define MDIO_AN_CTRL1_ENABLE		BMCR_ANENABLE
#define MDIO_AN_CTRL1_XNP		0x2000	/* Enable extended next page */
#define MDIO_PCS_CTRL1_CLKSTOP_EN	0x400	/* Stop the clock during LPI */

/* 10 Gb/s */
#define MDIO_CTRL1_SPEED10G		(MDIO_CTRL1_SPEEDSELEXT | 0x00)
/* 10PASS-TS/2BASE-TL */
#define MDIO_CTRL1_SPEED10P2B		(MDIO_CTRL1_SPEEDSELEXT | 0x04)

/* Status register 1. */
#define MDIO_STAT1_LPOWERABLE		0x0002	/* Low-power ability */
#define MDIO_STAT1_LSTATUS		BMSR_LSTATUS
#define MDIO_STAT1_FAULT		0x0080	/* Fault */
#define MDIO_AN_STAT1_LPABLE		0x0001	/* Link partner AN ability */
#define MDIO_AN_STAT1_ABLE		BMSR_ANEGCAPABLE
#define MDIO_AN_STAT1_RFAULT		BMSR_RFAULT
#define MDIO_AN_STAT1_COMPLETE		BMSR_ANEGCOMPLETE
#define MDIO_AN_STAT1_PAGE		0x0040	/* Page received */
#define MDIO_AN_STAT1_XNP		0x0080	/* Extended next page status */

/* Speed register. */
#define MDIO_SPEED_10G			0x0001	/* 10G capable */
#define MDIO_PMA_SPEED_2B		0x0002	/* 2BASE-TL capable */
#define MDIO_PMA_SPEED_10P		0x0004	/* 10PASS-TS capable */
#define MDIO_PMA_SPEED_1000		0x0010	/* 1000M capable */
#define MDIO_PMA_SPEED_100		0x0020	/* 100M capable */
#define MDIO_PMA_SPEED_10		0x0040	/* 10M capable */
#define MDIO_PCS_SPEED_10P2B		0x0002	/* 10PASS-TS/2BASE-TL capable */

/* Device present registers. */
#define MDIO_DEVS_PRESENT(devad)	(1 << (devad))
#define MDIO_DEVS_PMAPMD		MDIO_DEVS_PRESENT(MDIO_MMD_PMAPMD)
#define MDIO_DEVS_WIS			MDIO_DEVS_PRESENT(MDIO_MMD_WIS)
#define MDIO_DEVS_PCS			MDIO_DEVS_PRESENT(MDIO_MMD_PCS)
#define MDIO_DEVS_PHYXS			MDIO_DEVS_PRESENT(MDIO_MMD_PHYXS)
#define MDIO_DEVS_DTEXS			MDIO_DEVS_PRESENT(MDIO_MMD_DTEXS)
#define MDIO_DEVS_TC			MDIO_DEVS_PRESENT(MDIO_MMD_TC)
#define MDIO_DEVS_AN			MDIO_DEVS_PRESENT(MDIO_MMD_AN)
#define MDIO_DEVS_C22EXT		MDIO_DEVS_PRESENT(MDIO_MMD_C22EXT)

/* Control register 2. */
#define MDIO_PMA_CTRL2_TYPE		0x000f	/* PMA/PMD type selection */
#define MDIO_PMA_CTRL2_10GBCX4		0x0000	/* 10GBASE-CX4 type */
#define MDIO_PMA_CTRL2_10GBEW		0x0001	/* 10GBASE-EW type */
#define MDIO_PMA_CTRL2_10GBLW		0x0002	/* 10GBASE-LW type */
#define MDIO_PMA_CTRL2_10GBSW		0x0003	/* 10GBASE-SW type */
#define MDIO_PMA_CTRL2_10GBLX4		0x0004	/* 10GBASE-LX4 type */
#define MDIO_PMA_CTRL2_10GBER		0x0005	/* 10GBASE-ER type */
#define MDIO_PMA_CTRL2_10GBLR		0x0006	/* 10GBASE-LR type */
#define MDIO_PMA_CTRL2_10GBSR		0x0007	/* 10GBASE-SR type */
#define MDIO_PMA_CTRL2_10GBLRM		0x0008	/* 10GBASE-LRM type */
#define MDIO_PMA_CTRL2_10GBT		0x0009	/* 10GBASE-T type */
#define MDIO_PMA_CTRL2_10GBKX4		0x000a	/* 10GBASE-KX4 type */
#define MDIO_PMA_CTRL2_10GBKR		0x000b	/* 10GBASE-KR type */
#define MDIO_PMA_CTRL2_1000BT		0x000c	/* 1000BASE-T type */
#define MDIO_PMA_CTRL2_1000BKX		0x000d	/* 1000BASE-KX type */
#define MDIO_PMA_CTRL2_100BTX		0x000e	/* 100BASE-TX type */
#define MDIO_PMA_CTRL2_10BT		0x000f	/* 10BASE-T type */
#define MDIO_PCS_CTRL2_TYPE		0x0003	/* PCS type selection */
#define MDIO_PCS_CTRL2_10GBR		0x0000	/* 10GBASE-R type */
#define MDIO_PCS_CTRL2_10GBX		0x0001	/* 10GBASE-X type */
#define MDIO_PCS_CTRL2_10GBW		0x0002	/* 10GBASE-W type */
#define MDIO_PCS_CTRL2_10GBT		0x0003	/* 10GBASE-T type */

/* Status register 2. */
#define MDIO_STAT2_RXFAULT		0x0400	/* Receive fault */
#define MDIO_STAT2_TXFAULT		0x0800	/* Transmit fault */
#define MDIO_STAT2_DEVPRST		0xc000	/* Device present */
#define MDIO_STAT2_DEVPRST_VAL		0x8000	/* Device present value */
#define MDIO_PMA_STAT2_LBABLE		0x0001	/* PMA loopback ability */
#define MDIO_PMA_STAT2_10GBEW		0x0002	/* 10GBASE-EW ability */
#define MDIO_PMA_STAT2_10GBLW		0x0004	/* 10GBASE-LW ability */
#define MDIO_PMA_STAT2_10GBSW		0x0008	/* 10GBASE-SW ability */
#define MDIO_PMA_STAT2_10GBLX4		0x0010	/* 10GBASE-LX4 ability */
#define MDIO_PMA_STAT2_10GBER		0x0020	/* 10GBASE-ER ability */
#define MDIO_PMA_STAT2_10GBLR		0x0040	/* 10GBASE-LR ability */
#define MDIO_PMA_STAT2_10GBSR		0x0080	/* 10GBASE-SR ability */
#define MDIO_PMD_STAT2_TXDISAB		0x0100	/* PMD TX disable ability */
#define MDIO_PMA_STAT2_EXTABLE		0x0200	/* Extended abilities */
#define MDIO_PMA_STAT2_RXFLTABLE	0x1000	/* Receive fault ability */
#define MDIO_PMA_STAT2_TXFLTABLE	0x2000	/* Transmit fault ability */
#define MDIO_PCS_STAT2_10GBR		0x0001	/* 10GBASE-R capable */
#define MDIO_PCS_STAT2_10GBX		0x0002	/* 10GBASE-X capable */
#define MDIO_PCS_STAT2_10GBW		0x0004	/* 10GBASE-W capable */
#define MDIO_PCS_STAT2_RXFLTABLE	0x1000	/* Receive fault ability */
#define MDIO_PCS_STAT2_TXFLTABLE	0x2000	/* Transmit fault ability */

/* Transmit disable register. */
#define MDIO_PMD_TXDIS_GLOBAL		0x0001	/* Global PMD TX disable */
#define MDIO_PMD_TXDIS_0		0x0002	/* PMD TX disable 0 */
#define MDIO_PMD_TXDIS_1		0x0004	/* PMD TX disable 1 */
#define MDIO_PMD_TXDIS_2		0x0008	/* PMD TX disable 2 */
#define MDIO_PMD_TXDIS_3		0x0010	/* PMD TX disable 3 */

/* Receive signal detect register. */
#define MDIO_PMD_RXDET_GLOBAL		0x0001	/* Global PMD RX signal detect */
#define MDIO_PMD_RXDET_0		0x0002	/* PMD RX signal detect 0 */
#define MDIO_PMD_RXDET_1		0x0004	/* PMD RX signal detect 1 */
#define MDIO_PMD_RXDET_2		0x0008	/* PMD RX signal detect 2 */
#define MDIO_PMD_RXDET_3		0x0010	/* PMD RX signal detect 3 */

/* Extended abilities register. */
#define MDIO_PMA_EXTABLE_10GCX4		0x0001	/* 10GBASE-CX4 ability */
#define MDIO_PMA_EXTABLE_10GBLRM	0x0002	/* 10GBASE-LRM ability */
#define MDIO_PMA_EXTABLE_10GBT		0x0004	/* 10GBASE-T ability */
#define MDIO_PMA_EXTABLE_10GBKX4	0x0008	/* 10GBASE-KX4 ability */
#define MDIO_PMA_EXTABLE_10GBKR		0x0010	/* 10GBASE-KR ability */
#define MDIO_PMA_EXTABLE_1000BT		0x0020	/* 1000BASE-T ability */
#define MDIO_PMA_EXTABLE_1000BKX	0x0040	/* 1000BASE-KX ability */
#define MDIO_PMA_EXTABLE_100BTX		0x0080	/* 100BASE-TX ability */
#define MDIO_PMA_EXTABLE_10BT		0x0100	/* 10BASE-T ability */

/* PHY XGXS lane state register. */
#define MDIO_PHYXS_LNSTAT_SYNC0		0x0001
#define MDIO_PHYXS_LNSTAT_SYNC1		0x0002
#define MDIO_PHYXS_LNSTAT_SYNC2		0x0004
#define MDIO_PHYXS_LNSTAT_SYNC3		0x0008
#define MDIO_PHYXS_LNSTAT_ALIGN		0x1000

/* PMA 10GBASE-T pair swap & polarity */
#define MDIO_PMA_10GBT_SWAPPOL_ABNX	0x0001	/* Pair A/B uncrossed */
#define MDIO_PMA_10GBT_SWAPPOL_CDNX	0x0002	/* Pair C/D uncrossed */
#define MDIO_PMA_10GBT_SWAPPOL_AREV	0x0100	/* Pair A polarity reversed */
#define MDIO_PMA_10GBT_SWAPPOL_BREV	0x0200	/* Pair B polarity reversed */
#define MDIO_PMA_10GBT_SWAPPOL_CREV	0x0400	/* Pair C polarity reversed */
#define MDIO_PMA_10GBT_SWAPPOL_DREV	0x0800	/* Pair D polarity reversed */

/* PMA 10GBASE-T TX power register. */
#define MDIO_PMA_10GBT_TXPWR_SHORT	0x0001	/* Short-reach mode */

/* PMA 10GBASE-T SNR registers. */
/* Value is SNR margin in dB, clamped to range [-127, 127], plus 0x8000. */
#define MDIO_PMA_10GBT_SNR_BIAS		0x8000
#define MDIO_PMA_10GBT_SNR_MAX		127

/* PMA 10GBASE-R FEC ability register. */
#define MDIO_PMA_10GBR_FECABLE_ABLE	0x0001	/* FEC ability */
#define MDIO_PMA_10GBR_FECABLE_ERRABLE	0x0002	/* FEC error indic. ability */

/* PCS 10GBASE-R/-T status register 1. */
#define MDIO_PCS_10GBRT_STAT1_BLKLK	0x0001	/* Block lock attained */

/* PCS 10GBASE-R/-T status register 2. */
#define MDIO_PCS_10GBRT_STAT2_ERR	0x00ff
#define MDIO_PCS_10GBRT_STAT2_BER	0x3f00

/* AN 10GBASE-T control register. */
#define MDIO_AN_10GBT_CTRL_ADV10G	0x1000	/* Advertise 10GBASE-T */

/* AN 10GBASE-T status register. */
#define MDIO_AN_10GBT_STAT_LPTRR	0x0200	/* LP training reset req. */
#define MDIO_AN_10GBT_STAT_LPLTABLE	0x0400	/* LP loop timing ability */
#define MDIO_AN_10GBT_STAT_LP10G	0x0800	/* LP is 10GBT capable */
#define MDIO_AN_10GBT_STAT_REMOK	0x1000	/* Remote OK */
#define MDIO_AN_10GBT_STAT_LOCOK	0x2000	/* Local OK */
#define MDIO_AN_10GBT_STAT_MS		0x4000	/* Master/slave config */
#define MDIO_AN_10GBT_STAT_MSFLT	0x8000	/* Master/slave config fault */

/* EEE Supported/Advertisement/LP Advertisement registers.
 *
 * EEE capability Register (3.20), Advertisement (7.60) and
 * Link partner ability (7.61) registers have and can use the same identical
 * bit masks.
 */
#define MDIO_AN_EEE_ADV_100TX	0x0002	/* Advertise 100TX EEE cap */
#define MDIO_AN_EEE_ADV_1000T	0x0004	/* Advertise 1000T EEE cap */
/* Note: the two defines above can be potentially used by the user-land
 * and cannot remove them now.
 * So, we define the new generic MDIO_EEE_100TX and MDIO_EEE_1000T macros
 * using the previous ones (that can be considered obsolete).
 */
#define MDIO_EEE_100TX		MDIO_AN_EEE_ADV_100TX	/* 100TX EEE cap */
#define MDIO_EEE_1000T		MDIO_AN_EEE_ADV_1000T	/* 1000T EEE cap */
#define MDIO_EEE_10GT		0x0008	/* 10GT EEE cap */
#define MDIO_EEE_1000KX		0x0010	/* 1000KX EEE cap */
#define MDIO_EEE_10GKX4		0x0020	/* 10G KX4 EEE cap */
#define MDIO_EEE_10GKR		0x0040	/* 10G KR EEE cap */

/* LASI RX_ALARM control/status registers. */
#define MDIO_PMA_LASI_RX_PHYXSLFLT	0x0001	/* PHY XS RX local fault */
#define MDIO_PMA_LASI_RX_PCSLFLT	0x0008	/* PCS RX local fault */
#define MDIO_PMA_LASI_RX_PMALFLT	0x0010	/* PMA/PMD RX local fault */
#define MDIO_PMA_LASI_RX_OPTICPOWERFLT	0x0020	/* RX optical power fault */
#define MDIO_PMA_LASI_RX_WISLFLT	0x0200	/* WIS local fault */

/* LASI TX_ALARM control/status registers. */
#define MDIO_PMA_LASI_TX_PHYXSLFLT	0x0001	/* PHY XS TX local fault */
#define MDIO_PMA_LASI_TX_PCSLFLT	0x0008	/* PCS TX local fault */
#define MDIO_PMA_LASI_TX_PMALFLT	0x0010	/* PMA/PMD TX local fault */
#define MDIO_PMA_LASI_TX_LASERPOWERFLT	0x0080	/* Laser output power fault */
#define MDIO_PMA_LASI_TX_LASERTEMPFLT	0x0100	/* Laser temperature fault */
#define MDIO_PMA_LASI_TX_LASERBICURRFLT	0x0200	/* Laser bias current fault */

/* LASI control/status registers. */
#define MDIO_PMA_LASI_LSALARM		0x0001	/* LS_ALARM enable/status */
#define MDIO_PMA_LASI_TXALARM		0x0002	/* TX_ALARM enable/status */
#define MDIO_PMA_LASI_RXALARM		0x0004	/* RX_ALARM enable/status */

/* Mapping between MDIO PRTAD/DEVAD and mii_ioctl_data::phy_id */

#define MDIO_PHY_ID_C45			0x8000
#define MDIO_PHY_ID_PRTAD		0x03e0
#define MDIO_PHY_ID_DEVAD		0x001f
#define MDIO_PHY_ID_C45_MASK						\
	(MDIO_PHY_ID_C45 | MDIO_PHY_ID_PRTAD | MDIO_PHY_ID_DEVAD)

static inline __u16 mdio_phy_id_c45(int prtad, int devad)
{
	return MDIO_PHY_ID_C45 | (prtad << 5) | devad;
}

#endif /* _UAPI__LINUX_MDIO_H__ */
