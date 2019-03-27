/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2005-2011 Atheros Communications, Inc.
 * All rights reserved.
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

#ifndef	__AH_REGDOMAIN_REGENUM_H__
#define	__AH_REGDOMAIN_REGENUM_H__

/*
 * Enumerated Regulatory Domain Information 8 bit values indicate that
 * the regdomain is really a pair of unitary regdomains.  12 bit values
 * are the real unitary regdomains and are the only ones which have the
 * frequency bitmasks and flags set.
 */
enum {
	/*
	 * The following regulatory domain definitions are
	 * found in the EEPROM. Each regulatory domain
	 * can operate in either a 5GHz or 2.4GHz wireless mode or
	 * both 5GHz and 2.4GHz wireless modes.
	 * In general, the value holds no special
	 * meaning and is used to decode into either specific
	 * 2.4GHz or 5GHz wireless mode for that particular
	 * regulatory domain.
	 */
	NO_ENUMRD	= 0x00,
	NULL1_WORLD	= 0x03,		/* For 11b-only countries (no 11a allowed) */
	NULL1_ETSIB	= 0x07,		/* Israel */
	NULL1_ETSIC	= 0x08,
	FCC1_FCCA	= 0x10,		/* USA */
	FCC1_WORLD	= 0x11,		/* Hong Kong */
	FCC4_FCCA	= 0x12,		/* USA - Public Safety */
	FCC5_FCCB	= 0x13,		/* USA w/ 1/2 and 1/4 width channels */
	FCC6_FCCA	= 0x14,		/* Canada for AP only */

	FCC2_FCCA	= 0x20,		/* Canada */
	FCC2_WORLD	= 0x21,		/* Australia & HK */
	FCC2_ETSIC	= 0x22,
	FCC_UBNT	= 0x2A,		/* Ubiquity PicoStation M2HP */
	FRANCE_RES	= 0x31,		/* Legacy France for OEM */
	FCC3_FCCA	= 0x3A,		/* USA & Canada w/5470 band, 11h, DFS enabled */
	FCC3_WORLD	= 0x3B,		/* USA & Canada w/5470 band, 11h, DFS enabled */

	ETSI1_WORLD	= 0x37,
	ETSI3_ETSIA	= 0x32,		/* France (optional) */
	ETSI2_WORLD	= 0x35,		/* Hungary & others */
	ETSI3_WORLD	= 0x36,		/* France & others */
	ETSI4_WORLD	= 0x30,
	ETSI4_ETSIC	= 0x38,
	ETSI5_WORLD	= 0x39,
	ETSI6_WORLD	= 0x34,		/* Bulgaria */
	ETSI8_WORLD	= 0x3D,		/* Russia */
	ETSI9_WORLD	= 0x3E,		/* Ukraine */
	ETSI_RESERVED	= 0x33,		/* Reserved (Do not used) */

	MKK1_MKKA	= 0x40,		/* Japan (JP1) */
	MKK1_MKKB	= 0x41,		/* Japan (JP0) */
	APL4_WORLD	= 0x42,		/* Singapore */
	MKK2_MKKA	= 0x43,		/* Japan with 4.9G channels */
	APL_RESERVED	= 0x44,		/* Reserved (Do not used)  */
	APL2_WORLD	= 0x45,		/* Korea */
	APL2_APLC	= 0x46,
	APL3_WORLD	= 0x47,
	MKK1_FCCA	= 0x48,		/* Japan (JP1-1) */
	APL2_APLD	= 0x49,		/* Korea with 2.3G channels */
	MKK1_MKKA1	= 0x4A,		/* Japan (JE1) */
	MKK1_MKKA2	= 0x4B,		/* Japan (JE2) */
	MKK1_MKKC	= 0x4C,		/* Japan (MKK1_MKKA,except Ch14) */
	APL2_FCCA	= 0x4D,		/* Mobile customer */

	APL3_FCCA	= 0x50,
	APL1_WORLD	= 0x52,		/* Latin America */
	APL1_FCCA	= 0x53,
	APL1_APLA	= 0x54,
	APL1_ETSIC	= 0x55,
	APL2_ETSIC	= 0x56,		/* Venezuela */
	APL5_WORLD	= 0x58,		/* Chile */
	APL6_WORLD	= 0x5B,		/* Singapore */
	APL7_FCCA	= 0x5C,		/* Taiwan 5.47 Band */
	APL8_WORLD	= 0x5D,		/* Malaysia 5GHz */
	APL9_WORLD	= 0x5E,		/* Korea 5GHz; before 11/2007; now APs only */
	APL10_WORLD	= 0x5F,		/* Korea 5GHz; After 11/2007; STAs only */

	/*
	 * World mode SKUs
	 */
	WOR0_WORLD	= 0x60,		/* World0 (WO0 SKU) */
	WOR1_WORLD	= 0x61,		/* World1 (WO1 SKU) */
	WOR2_WORLD	= 0x62,		/* World2 (WO2 SKU) */
	WOR3_WORLD	= 0x63,		/* World3 (WO3 SKU) */
	WOR4_WORLD	= 0x64,		/* World4 (WO4 SKU) */
	WOR5_ETSIC	= 0x65,		/* World5 (WO5 SKU) */

	WOR01_WORLD	= 0x66,		/* World0-1 (WW0-1 SKU) */
	WOR02_WORLD	= 0x67,		/* World0-2 (WW0-2 SKU) */
	EU1_WORLD	= 0x68,		/* Same as World0-2 (WW0-2 SKU), except active scan ch1-13. No ch14 */

	WOR9_WORLD	= 0x69,		/* World9 (WO9 SKU) */
	WORA_WORLD	= 0x6A,		/* WorldA (WOA SKU) */
	WORB_WORLD	= 0x6B,		/* WorldB (WOB SKU) */
	WORC_WORLD	= 0x6C,		/* WorldC (WOC SKU) */

	MKK3_MKKB	= 0x80,		/* Japan UNI-1 even + MKKB */
	MKK3_MKKA2	= 0x81,		/* Japan UNI-1 even + MKKA2 */
	MKK3_MKKC	= 0x82,		/* Japan UNI-1 even + MKKC */

	MKK4_MKKB	= 0x83,		/* Japan UNI-1 even + UNI-2 + MKKB */
	MKK4_MKKA2	= 0x84,		/* Japan UNI-1 even + UNI-2 + MKKA2 */
	MKK4_MKKC	= 0x85,		/* Japan UNI-1 even + UNI-2 + MKKC */

	MKK5_MKKB	= 0x86,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKB */
	MKK5_MKKA2	= 0x87,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKA2 */
	MKK5_MKKC	= 0x88,		/* Japan UNI-1 even + UNI-2 + mid-band + MKKC */

	MKK6_MKKB	= 0x89,		/* Japan UNI-1 even + UNI-1 odd MKKB */
	MKK6_MKKA2	= 0x8A,		/* Japan UNI-1 even + UNI-1 odd + MKKA2 */
	MKK6_MKKC	= 0x8B,		/* Japan UNI-1 even + UNI-1 odd + MKKC */

	MKK7_MKKB	= 0x8C,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKB */
	MKK7_MKKA2	= 0x8D,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA2 */
	MKK7_MKKC	= 0x8E,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKC */

	MKK8_MKKB	= 0x8F,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKB */
	MKK8_MKKA2	= 0x90,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKA2 */
	MKK8_MKKC	= 0x91,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKC */

	MKK14_MKKA1	= 0x92,		/* Japan UNI-1 even + UNI-1 odd + 4.9GHz + MKKA1 */
	MKK15_MKKA1	= 0x93,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + 4.9GHz + MKKA1 */

	MKK10_FCCA	= 0xD0,		/* Japan UNI-1 even + UNI-2 + 4.9GHz + FCCA */
	MKK10_MKKA1	= 0xD1,		/* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKA1 */
	MKK10_MKKC	= 0xD2,		/* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKC */
	MKK10_MKKA2	= 0xD3,		/* Japan UNI-1 even + UNI-2 + 4.9GHz + MKKA2 */

	MKK11_MKKA	= 0xD4,		/* Japan UNI-1 even + UNI-2 + mid-band + 4.9GHz + MKKA */
	MKK11_FCCA	= 0xD5,		/* Japan UNI-1 even + UNI-2 + mid-band + 4.9GHz + FCCA */
	MKK11_MKKA1	= 0xD6,		/* Japan UNI-1 even + UNI-2 + mid-band + 4.9GHz + MKKA1 */
	MKK11_MKKC	= 0xD7,		/* Japan UNI-1 even + UNI-2 + mid-band + 4.9GHz + MKKC */
	MKK11_MKKA2	= 0xD8,		/* Japan UNI-1 even + UNI-2 + mid-band + 4.9GHz + MKKA2 */

	MKK12_MKKA	= 0xD9,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9GHz + MKKA */
	MKK12_FCCA	= 0xDA,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9GHz + FCCA */
	MKK12_MKKA1	= 0xDB,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9GHz + MKKA1 */
	MKK12_MKKC	= 0xDC,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9GHz + MKKC */
	MKK12_MKKA2	= 0xDD,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + 4.9GHz + MKKA2 */

	MKK13_MKKB	= 0xDE,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKB + All passive + no adhoc */

	/*
	 * Following definitions are used only by s/w to map old
	 * Japan SKUs.
	 */
	MKK3_MKKA	= 0xF0,		/* Japan UNI-1 even + MKKA */
	MKK3_MKKA1	= 0xF1,		/* Japan UNI-1 even + MKKA1 */
	MKK3_FCCA	= 0xF2,		/* Japan UNI-1 even + FCCA */
	MKK4_MKKA	= 0xF3,		/* Japan UNI-1 even + UNI-2 + MKKA */
	MKK4_MKKA1	= 0xF4,		/* Japan UNI-1 even + UNI-2 + MKKA1 */
	MKK4_FCCA	= 0xF5,		/* Japan UNI-1 even + UNI-2 + FCCA */
	MKK9_MKKA	= 0xF6,		/* Japan UNI-1 even + 4.9GHz */
	MKK10_MKKA	= 0xF7,		/* Japan UNI-1 even + UNI-2 + 4.9GHz */
	MKK6_MKKA1	= 0xF8,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA1 */
	MKK6_FCCA	= 0xF9,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + FCCA */
	MKK7_MKKA1	= 0xFA,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA1 */
	MKK7_FCCA	= 0xFB,		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + FCCA */
	MKK9_FCCA	= 0xFC,		/* Japan UNI-1 even + 4.9GHz + FCCA */
	MKK9_MKKA1	= 0xFD,		/* Japan UNI-1 even + 4.9GHz + MKKA1 */
	MKK9_MKKC	= 0xFE,		/* Japan UNI-1 even + 4.9GHz + MKKC */
	MKK9_MKKA2	= 0xFF,		/* Japan UNI-1 even + 4.9GHz + MKKA2 */

	/*
	 * Regulator domains ending in a number (e.g. APL1,
	 * MK1, ETSI4, etc) apply to 5GHz channel and power
	 * information.  Regulator domains ending in a letter
	 * (e.g. APLA, FCCA, etc) apply to 2.4GHz channel and
	 * power information.
	 */
	APL1		= 0x0150,	/* LAT & Asia */
	APL2		= 0x0250,	/* LAT & Asia */
	APL3		= 0x0350,	/* Taiwan */
	APL4		= 0x0450,	/* Jordan */
	APL5		= 0x0550,	/* Chile */
	APL6		= 0x0650,	/* Singapore */
	APL7		= 0x0750,	/* Taiwan, disable ch52 */
	APL8		= 0x0850,	/* Malaysia */
	APL9		= 0x0950,	/* Korea. Before 11/2007. Now used only by APs */
	APL10		= 0x1050,	/* Korea. After 11/2007. For STAs only */

	ETSI1		= 0x0130,	/* Europe & others */
	ETSI2		= 0x0230,	/* Europe & others */
	ETSI3		= 0x0330,	/* Europe & others */
	ETSI4		= 0x0430,	/* Europe & others */
	ETSI5		= 0x0530,	/* Europe & others */
	ETSI6		= 0x0630,	/* Europe & others */
	ETSI8		= 0x0830,	/* Russia */
	ETSI9		= 0x0930,	/* Ukraine */
	ETSIA		= 0x0A30,	/* France */
	ETSIB		= 0x0B30,	/* Israel */
	ETSIC		= 0x0C30,	/* Latin America */

	FCC1		= 0x0110,	/* US & others */
	FCC2		= 0x0120,	/* Canada, Australia & New Zealand */
	FCC3		= 0x0160,	/* US w/new middle band & DFS */
	FCC4		= 0x0165,	/* US Public Safety */
	FCC5		= 0x0166,	/* US w/ 1/2 and 1/4 width channels */
	FCC6		= 0x0610,	/* Canada and Australia */
	FCCA		= 0x0A10,
	FCCB		= 0x0A11,	/* US w/ 1/2 and 1/4 width channels */

	APLD		= 0x0D50,	/* South Korea */

	MKK1		= 0x0140,	/* Japan (UNI-1 odd)*/
	MKK2		= 0x0240,	/* Japan (4.9 GHz + UNI-1 odd) */
	MKK3		= 0x0340,	/* Japan (UNI-1 even) */
	MKK4		= 0x0440,	/* Japan (UNI-1 even + UNI-2) */
	MKK5		= 0x0540,	/* Japan (UNI-1 even + UNI-2 + mid-band) */
	MKK6		= 0x0640,	/* Japan (UNI-1 odd + UNI-1 even) */
	MKK7		= 0x0740,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 */
	MKK8		= 0x0840,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 + mid-band) */
	MKK9		= 0x0940,	/* Japan (UNI-1 even + 4.9 GHZ) */
	MKK10		= 0x0B40,	/* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	MKK11		= 0x1140,	/* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	MKK12		= 0x1240,	/* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	MKK13		= 0x0C40,	/* Same as MKK8 but all passive and no adhoc 11a */
	MKK14		= 0x1440,	/* Japan UNI-1 even + UNI-1 odd + 4.9GHz */
	MKK15		= 0x1540,	/* Japan UNI-1 even + UNI-1 odd + UNI-2 + 4.9GHz */

	MKKA		= 0x0A40,	/* Japan */
	MKKC		= 0x0A50,

	NULL1		= 0x0198,
	WORLD		= 0x0199,
	DEBUG_REG_DMN	= 0x01ff,
};

#endif
