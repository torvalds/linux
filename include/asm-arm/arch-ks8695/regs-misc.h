/*
 * include/asm-arm/arch-ks8695/regs-misc.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 - Miscellaneous Registers
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_MISC_H
#define KS8695_MISC_H

#define KS8695_MISC_OFFSET	(0xF0000 + 0xEA00)
#define KS8695_MISC_VA		(KS8695_IO_VA + KS8695_MISC_OFFSET)
#define KS8695_MISC_PA		(KS8695_IO_PA + KS8695_MISC_OFFSET)


/*
 * Miscellaneous registers
 */
#define KS8695_DID		(0x00)		/* Device ID */
#define KS8695_RID		(0x04)		/* Revision ID */
#define KS8695_HMC		(0x08)		/* HPNA Miscellaneous Control [KS8695 only] */
#define KS8695_WMC		(0x0c)		/* WAN Miscellaneous Control */
#define KS8695_WPPM		(0x10)		/* WAN PHY Power Management */
#define KS8695_PPS		(0x1c)		/* PHY PowerSave */

/* Device ID Register */
#define DID_ID			(0xffff << 0)	/* Device ID */

/* Revision ID Register */
#define RID_SUBID		(0xf << 4)	/* Sub-Device ID */
#define RID_REVISION		(0xf << 0)	/* Revision ID */

/* HPNA Miscellaneous Control Register */
#define HMC_HSS			(1 << 1)	/* Speed */
#define HMC_HDS			(1 << 0)	/* Duplex */

/* WAN Miscellaneous Control Register */
#define WMC_WANC		(1 << 30)	/* Auto-negotiation complete */
#define WMC_WANR		(1 << 29)	/* Auto-negotiation restart */
#define WMC_WANAP		(1 << 28)	/* Advertise Pause */
#define WMC_WANA100F		(1 << 27)	/* Advertise 100 FDX */
#define WMC_WANA100H		(1 << 26)	/* Advertise 100 HDX */
#define WMC_WANA10F		(1 << 25)	/* Advertise 10 FDX */
#define WMC_WANA10H		(1 << 24)	/* Advertise 10 HDX */
#define WMC_WLS			(1 << 23)	/* Link status */
#define WMC_WDS			(1 << 22)	/* Duplex status */
#define WMC_WSS			(1 << 21)	/* Speed status */
#define WMC_WLPP		(1 << 20)	/* Link Partner Pause */
#define WMC_WLP100F		(1 << 19)	/* Link Partner 100 FDX */
#define WMC_WLP100H		(1 << 18)	/* Link Partner 100 HDX */
#define WMC_WLP10F		(1 << 17)	/* Link Partner 10 FDX */
#define WMC_WLP10H		(1 << 16)	/* Link Partner 10 HDX */
#define WMC_WAND		(1 << 15)	/* Auto-negotiation disable */
#define WMC_WANF100		(1 << 14)	/* Force 100 */
#define WMC_WANFF		(1 << 13)	/* Force FDX */
#define WMC_WLED1S		(7 <<  4)	/* LED1 Select */
#define		WLED1S_SPEED		(0 << 4)
#define		WLED1S_LINK		(1 << 4)
#define		WLED1S_DUPLEX		(2 << 4)
#define		WLED1S_COLLISION	(3 << 4)
#define		WLED1S_ACTIVITY		(4 << 4)
#define		WLED1S_FDX_COLLISION	(5 << 4)
#define		WLED1S_LINK_ACTIVITY	(6 << 4)
#define WMC_WLED0S		(7 << 0)	/* LED0 Select */
#define		WLED0S_SPEED		(0 << 0)
#define		WLED0S_LINK		(1 << 0)
#define		WLED0S_DUPLEX		(2 << 0)
#define		WLED0S_COLLISION	(3 << 0)
#define		WLED0S_ACTIVITY		(4 << 0)
#define		WLED0S_FDX_COLLISION	(5 << 0)
#define		WLED0S_LINK_ACTIVITY	(6 << 0)

/* WAN PHY Power Management Register */
#define WPPM_WLPBK		(1 << 14)	/* Local Loopback */
#define WPPM_WRLPKB		(1 << 13)	/* Remove Loopback */
#define WPPM_WPI		(1 << 12)	/* PHY isolate */
#define WPPM_WFL		(1 << 10)	/* Force link */
#define WPPM_MDIXS		(1 << 9)	/* MDIX Status */
#define WPPM_FEF		(1 << 8)	/* Far End Fault */
#define WPPM_AMDIXP		(1 << 7)	/* Auto MDIX Parameter */
#define WPPM_TXDIS		(1 << 6)	/* Disable transmitter */
#define WPPM_DFEF		(1 << 5)	/* Disable Far End Fault */
#define WPPM_PD			(1 << 4)	/* Power Down */
#define WPPM_DMDX		(1 << 3)	/* Disable Auto MDI/MDIX */
#define WPPM_FMDX		(1 << 2)	/* Force MDIX */
#define WPPM_LPBK		(1 << 1)	/* MAX Loopback */

/* PHY Power Save Register */
#define PPS_PPSM		(1 << 0)	/* PHY Power Save Mode */


#endif
