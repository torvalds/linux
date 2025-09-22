/*	$OpenBSD: mii.h,v 1.14 2015/07/18 20:38:44 yuo Exp $	*/
/*	$NetBSD: mii.h,v 1.8 2001/05/31 03:06:46 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
 *
 * Modification to match BSD/OS 3.0 MII interface by Jason R. Thorpe,
 * Numerical Aerospace Simulation Facility, NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_MII_MII_H_
#define	_DEV_MII_MII_H_

/*
 * Registers common to all PHYs.
 */

#define	MII_NPHY	32	/* max # of PHYs per MII */

/*
 * MII commands, used if a device must drive the MII lines
 * manually.
 */
#define	MII_COMMAND_START	0x01
#define	MII_COMMAND_READ	0x02
#define	MII_COMMAND_WRITE	0x01
#define	MII_COMMAND_ACK		0x02

#define	MII_BMCR	0x00 	/* Basic mode control register (rw) */
#define	BMCR_RESET	0x8000	/* reset */
#define	BMCR_LOOP	0x4000	/* loopback */
#define	BMCR_SPEED0	0x2000	/* speed selection (LSB) */
#define	BMCR_AUTOEN	0x1000	/* autonegotiation enable */
#define	BMCR_PDOWN	0x0800	/* power down */
#define	BMCR_ISO	0x0400	/* isolate */
#define	BMCR_STARTNEG	0x0200	/* restart autonegotiation */
#define	BMCR_FDX	0x0100	/* Set duplex mode */
#define	BMCR_CTEST	0x0080	/* collision test */
#define	BMCR_SPEED1	0x0040	/* speed selection (MSB) */

#define	BMCR_S10	0x0000		/* 10 Mb/s */
#define	BMCR_S100	BMCR_SPEED0	/* 100 Mb/s */
#define	BMCR_S1000	BMCR_SPEED1	/* 1000 Mb/s */

#define	BMCR_SPEED(x)	((x) & (BMCR_SPEED0|BMCR_SPEED1))

#define	MII_BMSR	0x01	/* Basic mode status register (ro) */
#define	BMSR_100T4	0x8000	/* 100 base T4 capable */
#define	BMSR_100TXFDX	0x4000	/* 100 base Tx full duplex capable */
#define	BMSR_100TXHDX	0x2000	/* 100 base Tx half duplex capable */
#define	BMSR_10TFDX	0x1000	/* 10 base T full duplex capable */
#define	BMSR_10THDX	0x0800	/* 10 base T half duplex capable */
#define	BMSR_MFPS	0x0040	/* MII Frame Preamble Suppression */
#define	BMSR_100T2FDX	0x0400	/* 100 base T2 full duplex capable */
#define	BMSR_100T2HDX	0x0200	/* 100 base T2 half duplex capable */
#define	BMSR_EXTSTAT	0x0100	/* Extended status in register 15 */
#define	BMSR_ACOMP	0x0020	/* Autonegotiation complete */
#define	BMSR_RFAULT	0x0010	/* Link partner fault */
#define	BMSR_ANEG	0x0008	/* Autonegotiation capable */
#define	BMSR_LINK	0x0004	/* Link status */
#define	BMSR_JABBER	0x0002	/* Jabber detected */
#define	BMSR_EXTCAP	0x0001	/* Extended capability */

/*
 * Note that the EXTSTAT bit indicates that there is extended status
 * info available in register 15, but 802.3 section 22.2.4.3 also
 * states that all 1000 Mb/s capable PHYs will set this bit to 1.
 */

#define	BMSR_MEDIAMASK	(BMSR_100T4|BMSR_100TXFDX|BMSR_100TXHDX| \
			 BMSR_10TFDX|BMSR_10THDX|BMSR_100T2FDX|BMSR_100T2HDX)

/*
 * Convert BMSR media capabilities to ANAR bits for autonegotiation.
 * Note the shift chopps off the BMSR_ANEG bit.
 */
#define	BMSR_MEDIA_TO_ANAR(x)	(((x) & BMSR_MEDIAMASK) >> 6)

#define	MII_PHYIDR1	0x02	/* ID register 1 (ro) */

#define	MII_PHYIDR2	0x03	/* ID register 2 (ro) */
#define	IDR2_OUILSB	0xfc00	/* OUI LSB */
#define	IDR2_MODEL	0x03f0	/* vendor model */
#define	IDR2_REV	0x000f	/* vendor revision */

#define	MII_ANAR	0x04	/* Autonegotiation advertisement (rw) */
		/* section 28.2.4.1 and 37.2.6.1 */
#define ANAR_NP		0x8000	/* Next page (ro) */
#define	ANAR_ACK	0x4000	/* link partner abilities acknowledged (ro) */
#define ANAR_RF		0x2000	/* remote fault (ro) */
#define	ANAR_FC		0x0400	/* local device supports PAUSE */
#define ANAR_T4		0x0200	/* local device supports 100bT4 */
#define ANAR_TX_FD	0x0100	/* local device supports 100bTx FD */
#define ANAR_TX		0x0080	/* local device supports 100bTx */
#define ANAR_10_FD	0x0040	/* local device supports 10bT FD */
#define ANAR_10		0x0020	/* local device supports 10bT */
#define	ANAR_CSMA	0x0001	/* protocol selector CSMA/CD */
#define	ANAR_PAUSE_NONE		(0 << 10)
#define	ANAR_PAUSE_SYM		(1 << 10)
#define	ANAR_PAUSE_ASYM		(2 << 10)
#define	ANAR_PAUSE_TOWARDS	(3 << 10)

#define	ANAR_X_FD	0x0020	/* local device supports 1000BASE-X FD */
#define	ANAR_X_HD	0x0040	/* local device supports 1000BASE-X HD */
#define	ANAR_X_PAUSE_NONE	(0 << 7)
#define	ANAR_X_PAUSE_SYM	(1 << 7)
#define	ANAR_X_PAUSE_ASYM	(2 << 7)
#define	ANAR_X_PAUSE_TOWARDS	(3 << 7)

#define	MII_ANLPAR	0x05	/* Autonegotiation lnk partner abilities (rw) */
		/* section 28.2.4.1 and 37.2.6.1 */
#define ANLPAR_NP	0x8000	/* Next page (ro) */
#define	ANLPAR_ACK	0x4000	/* link partner accepted ACK (ro) */
#define ANLPAR_RF	0x2000	/* remote fault (ro) */
#define	ANLPAR_FC	0x0400	/* link partner supports PAUSE */
#define ANLPAR_T4	0x0200	/* link partner supports 100bT4 */
#define ANLPAR_TX_FD	0x0100	/* link partner supports 100bTx FD */
#define ANLPAR_TX	0x0080	/* link partner supports 100bTx */
#define ANLPAR_10_FD	0x0040	/* link partner supports 10bT FD */
#define ANLPAR_10	0x0020	/* link partner supports 10bT */
#define	ANLPAR_CSMA	0x0001	/* protocol selector CSMA/CD */
#define	ANLPAR_PAUSE_MASK	(3 << 10)
#define	ANLPAR_PAUSE_NONE	(0 << 10)
#define	ANLPAR_PAUSE_SYM	(1 << 10)
#define	ANLPAR_PAUSE_ASYM	(2 << 10)
#define	ANLPAR_PAUSE_TOWARDS	(3 << 10)

#define	ANLPAR_X_FD	0x0020	/* local device supports 1000BASE-X FD */
#define	ANLPAR_X_HD	0x0040	/* local device supports 1000BASE-X HD */
#define	ANLPAR_X_PAUSE_MASK	(3 << 7)
#define	ANLPAR_X_PAUSE_NONE	(0 << 7)
#define	ANLPAR_X_PAUSE_SYM	(1 << 7)
#define	ANLPAR_X_PAUSE_ASYM	(2 << 7)
#define	ANLPAR_X_PAUSE_TOWARDS	(3 << 7)

#define	MII_ANER	0x06	/* Autonegotiation expansion (ro) */
		/* section 28.2.4.1 and 37.2.6.1 */
#define ANER_MLF	0x0010	/* multiple link detection fault */
#define ANER_LPNP	0x0008	/* link parter next page-able */
#define ANER_NP		0x0004	/* next page-able */
#define ANER_PAGE_RX	0x0002	/* Page received */
#define ANER_LPAN	0x0001	/* link parter autoneg-able */

#define	MII_ANNP	0x07	/* Autonegotiation next page */
		/* section 28.2.4.1 and 37.2.6.1 */

#define	MII_ANLPRNP	0x08	/* Autonegotiation link partner rx next page */
		/* section 32.5.1 and 37.2.6.1 */

			/* This is also the 1000baseT control register */
#define	MII_100T2CR	0x09	/* 100base-T2 control register */
#define	GTCR_TEST_MASK	0xe000	/* see 802.3ab ss. 40.6.1.1.2 */
#define	GTCR_MAN_MS	0x1000	/* enable manual master/slave control */
#define	GTCR_ADV_MS	0x0800	/* 1 = adv. master, 0 = adv. slave */
#define	GTCR_PORT_TYPE	0x0400	/* 1 = DCE, 0 = DTE (NIC) */
#define	GTCR_ADV_1000TFDX 0x0200 /* adv. 1000baseT FDX */
#define	GTCR_ADV_1000THDX 0x0100 /* adv. 1000baseT HDX */

			/* This is also the 1000baseT status register */
#define	MII_100T2SR	0x0a	/* 100base-T2 status register */
#define	GTSR_MAN_MS_FLT	0x8000	/* master/slave config fault */
#define	GTSR_MS_RES	0x4000	/* result: 1 = master, 0 = slave */
#define	GTSR_LRS	0x2000	/* local rx status, 1 = ok */
#define	GTSR_RRS	0x1000	/* remove rx status, 1 = ok */
#define	GTSR_LP_1000TFDX 0x0800	/* link partner 1000baseT FDX capable */
#define	GTSR_LP_1000THDX 0x0400	/* link partner 1000baseT HDX capable */
#define	GTSR_LP_ASM_DIR	0x0200	/* link partner asym. pause dir. capable */
#define	GTSR_IDLE_ERR	0x00ff	/* IDLE error count */

#define	MII_PSECR	0x0b	/* PSE control register */
#define	PSECR_PACTLMASK	0x000c	/* pair control mask */
#define	PSECR_PSEENMASK	0x0003	/* PSE enable mask */
#define	PSECR_PINOUTB	0x0008	/* PSE pinout Alternative B */
#define	PSECR_PINOUTA	0x0004	/* PSE pinout Alternative A */
#define	PSECR_FOPOWTST	0x0002	/* Force Power Test Mode */
#define	PSECR_PSEEN	0x0001	/* PSE Enabled */
#define	PSECR_PSEDIS	0x0000	/* PSE Disabled */

#define	MII_PSESR	0x0c	/* PSE status register */
#define	PSESR_PWRDENIED	0x1000	/* Power Denied */
#define	PSESR_VALSIG	0x0800	/* Valid PD signature detected */
#define	PSESR_INVALSIG	0x0400	/* Invalid PD signature detected */
#define	PSESR_SHORTCIRC	0x0200	/* Short circuit condition detected */
#define	PSESR_OVERLOAD	0x0100	/* Overload condition detected */
#define	PSESR_MPSABSENT	0x0080	/* MPS absent condition detected */
#define	PSESR_PDCLMASK	0x0070	/* PD Class mask */
#define	PSESR_STATMASK	0x000e	/* PSE Status mask */
#define	PSESR_PAIRCTABL	0x0001	/* PAIR Control Ability */
#define	PSESR_PDCL_4		(4 << 4)	/* Class 4 */
#define	PSESR_PDCL_3		(3 << 4)	/* Class 3 */
#define	PSESR_PDCL_2		(2 << 4)	/* Class 2 */
#define	PSESR_PDCL_1		(1 << 4)	/* Class 1 */
#define	PSESR_PDCL_0		(0 << 4)	/* Class 0 */

#define	MII_MMDACR	0x0d	/* MMD access control register */
#define	MMDACR_FUNCMASK		0xc000	/* function */
#define	MMDACR_DADDRMASK 	0x001f	/* device address */
#define	MMDACR_FN_ADDRESS	(0 << 14) /* address */
#define	MMDACR_FN_DATANPI	(1 << 14) /* data, no post increment */
#define	MMDACR_FN_DATAPIRW	(2 << 14) /* data, post increment on r/w */
#define	MMDACR_FN_DATAPIW	(3 << 14) /* data, post increment on wr only */

#define	MII_MMDAADR	0x0e	/* MMD access address data register */

#define	MII_EXTSR	0x0f	/* Extended status register */
#define	EXTSR_1000XFDX	0x8000	/* 1000X full-duplex capable */
#define	EXTSR_1000XHDX	0x4000	/* 1000X half-duplex capable */
#define	EXTSR_1000TFDX	0x2000	/* 1000T full-duplex capable */
#define	EXTSR_1000THDX	0x1000	/* 1000T half-duplex capable */

#define	EXTSR_MEDIAMASK	(EXTSR_1000XFDX|EXTSR_1000XHDX| \
			 EXTSR_1000TFDX|EXTSR_1000THDX)

#endif /* _DEV_MII_MII_H_ */
