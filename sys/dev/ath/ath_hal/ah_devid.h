/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_ATH_DEVID_H_
#define _DEV_ATH_DEVID_H_

#define ATHEROS_VENDOR_ID	0x168c		/* Atheros PCI vendor ID */
/*
 * NB: all Atheros-based devices should have a PCI vendor ID
 *     of 0x168c, but some vendors, in their infinite wisdom
 *     do not follow this so we must handle them specially.
 */
#define	ATHEROS_3COM_VENDOR_ID	0xa727		/* 3Com 3CRPAG175 vendor ID */
#define	ATHEROS_3COM2_VENDOR_ID	0x10b7		/* 3Com 3CRDAG675 vendor ID */

/* AR5210 (for reference) */
#define AR5210_DEFAULT          0x1107          /* No eeprom HW default */
#define AR5210_PROD             0x0007          /* Final device ID */
#define AR5210_AP               0x0207          /* Early AP11s */

/* AR5211 */
#define AR5211_DEFAULT          0x1112          /* No eeprom HW default */
#define AR5311_DEVID            0x0011          /* Final ar5311 devid */
#define AR5211_DEVID            0x0012          /* Final ar5211 devid */
#define AR5211_LEGACY           0xff12          /* Original emulation board */
#define AR5211_FPGA11B          0xf11b          /* 11b emulation board */

/* AR5212 */
#define AR5212_DEFAULT          0x1113          /* No eeprom HW default */
#define AR5212_DEVID            0x0013          /* Final ar5212 devid */
#define AR5212_FPGA             0xf013          /* Emulation board */
#define	AR5212_DEVID_IBM	0x1014          /* IBM minipci ID */
#define AR5212_AR5312_REV2      0x0052          /* AR5312 WMAC (AP31) */
#define AR5212_AR5312_REV7      0x0057          /* AR5312 WMAC (AP30-040) */
#define AR5212_AR2313_REV8      0x0058          /* AR2313 WMAC (AP43-030) */
#define AR5212_AR2315_REV6      0x0086          /* AR2315 WMAC (AP51-Light) */
#define AR5212_AR2315_REV7      0x0087          /* AR2315 WMAC (AP51-Full) */
#define AR5212_AR2317_REV1      0x0090          /* AR2317 WMAC (AP61-Light) */
#define AR5212_AR2317_REV2      0x0091          /* AR2317 WMAC (AP61-Full) */

/* AR5212 compatible devid's also attach to 5212 */
#define	AR5212_DEVID_0014	0x0014
#define	AR5212_DEVID_0015	0x0015
#define	AR5212_DEVID_0016	0x0016
#define	AR5212_DEVID_0017	0x0017
#define	AR5212_DEVID_0018	0x0018
#define	AR5212_DEVID_0019	0x0019
#define AR5212_AR2413      	0x001a          /* AR2413 aka Griffin-lite */
#define AR5212_AR5413		0x001b          /* Eagle */
#define AR5212_AR5424		0x001c          /* Condor (PCI express) */
#define AR5212_AR2417		0x001d          /* Nala, PCI */
#define AR5212_DEVID_FF19	0xff19          /* XXX PCI express */

/* AR5213 */
#define	AR5213_SREV_1_0		0x0055
#define	AR5213_SREV_REG		0x4020

/* AR5416 compatible devid's  */
#define AR5416_DEVID_PCI	0x0023          /* AR5416 PCI (MB/CB) Owl */
#define AR5416_DEVID_PCIE	0x0024          /* AR5418 PCI-E (XB) Owl */
#define	AR5416_AR9130_DEVID     0x000b          /* AR9130 SoC WiMAC */
#define AR9160_DEVID_PCI	0x0027          /* AR9160 PCI Sowl */
#define AR9280_DEVID_PCI	0x0029          /* AR9280 PCI Merlin */
#define AR9280_DEVID_PCIE	0x002a          /* AR9220 PCI-E Merlin */
#define AR9285_DEVID_PCIE	0x002b          /* AR9285 PCI-E Kite */
#define	AR2427_DEVID_PCIE	0x002c		/* AR2427 PCI-E w/ 802.11n bonded out */
#define	AR9287_DEVID_PCI	0x002d		/* AR9227 PCI Kiwi */
#define	AR9287_DEVID_PCIE	0x002e		/* AR9287 PCI-E Kiwi */

/* AR9300 */
#define	AR9300_DEVID_AR9380_PCIE	0x0030
#define	AR9300_DEVID_EMU_PCIE		0xabcd
#define	AR9300_DEVID_AR9340		0x0031
#define	AR9300_DEVID_AR9485_PCIE	0x0032
#define	AR9300_DEVID_AR9580_PCIE	0x0033
#define	AR9300_DEVID_AR946X_PCIE	0x0034
#define	AR9300_DEVID_AR9330		0x0035
#define	AR9300_DEVID_QCA9565		0x0036
#define	AR9300_DEVID_AR1111_PCIE	0x0037
#define	AR9300_DEVID_QCA955X		0x0039
#define	AR9300_DEVID_QCA953X		0x003d        /* Honey Bee */

#define	AR_SUBVENDOR_ID_NOG	0x0e11		/* No 11G subvendor ID */
#define AR_SUBVENDOR_ID_NEW_A	0x7065		/* Update device to new RD */
#endif /* _DEV_ATH_DEVID_H */
