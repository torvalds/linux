/*-
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#include "ah.h"

#include <sys/param.h>

#include <net80211/_ieee80211.h>
#include <net80211/ieee80211_regdomain.h>

#include "ah_internal.h"
#include "ah_eeprom_v3.h"		/* XXX */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int		ath_hal_debug = 0;
HAL_CTRY_CODE	cc = CTRY_DEFAULT;
HAL_REG_DOMAIN	rd = 169;		/* FCC */
HAL_BOOL	Amode = 1;
HAL_BOOL	Bmode = 1;
HAL_BOOL	Gmode = 1;
HAL_BOOL	HT20mode = 1;
HAL_BOOL	HT40mode = 1;
HAL_BOOL	turbo5Disable = AH_FALSE;
HAL_BOOL	turbo2Disable = AH_FALSE;

u_int16_t	_numCtls = 8;
u_int16_t	_ctl[32] =
	{ 0x10, 0x13, 0x40, 0x30, 0x11, 0x31, 0x12, 0x32 };
RD_EDGES_POWER	_rdEdgesPower[NUM_EDGES*NUM_CTLS] = {
	{ 5180, 28, 0 },	/* 0x10 */
	{ 5240, 60, 0 },
	{ 5260, 36, 0 },
	{ 5320, 27, 0 },
	{ 5745, 36, 0 },
	{ 5765, 36, 0 },
	{ 5805, 36, 0 },
	{ 5825, 36, 0 },

	{ 5210, 28, 0 },	/* 0x13 */
	{ 5250, 28, 0 },
	{ 5290, 30, 0 },
	{ 5760, 36, 0 },
	{ 5800, 36, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },

	{ 5170, 60, 0 },	/* 0x40 */
	{ 5230, 60, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },

	{ 5180, 33, 0 },	/* 0x30 */
	{ 5320, 33, 0 },
	{ 5500, 34, 0 },
	{ 5700, 34, 0 },
	{ 5745, 35, 0 },
	{ 5765, 35, 0 },
	{ 5785, 35, 0 },
	{ 5825, 35, 0 },

	{ 2412, 36, 0 },	/* 0x11 */
	{ 2417, 36, 0 },
	{ 2422, 36, 0 },
	{ 2432, 36, 0 },
	{ 2442, 36, 0 },
	{ 2457, 36, 0 },
	{ 2467, 36, 0 },
	{ 2472, 36, 0 },

	{ 2412, 36, 0 },	/* 0x31 */
	{ 2417, 36, 0 },
	{ 2422, 36, 0 },
	{ 2432, 36, 0 },
	{ 2442, 36, 0 },
	{ 2457, 36, 0 },
	{ 2467, 36, 0 },
	{ 2472, 36, 0 },

	{ 2412, 36, 0 },	/* 0x12 */
	{ 2417, 36, 0 },
	{ 2422, 36, 0 },
	{ 2432, 36, 0 },
	{ 2442, 36, 0 },
	{ 2457, 36, 0 },
	{ 2467, 36, 0 },
	{ 2472, 36, 0 },

	{ 2412, 28, 0 },	/* 0x32 */
	{ 2417, 28, 0 },
	{ 2422, 28, 0 },
	{ 2432, 28, 0 },
	{ 2442, 28, 0 },
	{ 2457, 28, 0 },
	{ 2467, 28, 0 },
	{ 2472, 28, 0 },
};

u_int16_t	turbo2WMaxPower5 = 32;
u_int16_t	turbo2WMaxPower2;
int8_t		antennaGainMax[2] = { 0, 0 };	/* XXX */
int		eeversion = AR_EEPROM_VER3_1;
TRGT_POWER_ALL_MODES tpow = {
	8, {
	    { 22, 24, 28, 32, 5180 },
	    { 22, 24, 28, 32, 5200 },
	    { 22, 24, 28, 32, 5320 },
	    { 26, 30, 34, 34, 5500 },
	    { 26, 30, 34, 34, 5700 },
	    { 20, 30, 34, 36, 5745 },
	    { 20, 30, 34, 36, 5825 },
	    { 20, 30, 34, 36, 5850 },
	},
	2, {
	    { 23, 27, 31, 34, 2412 },
	    { 23, 27, 31, 34, 2447 },
	},
	2, {
	    { 36, 36, 36, 36, 2412 },
	    { 36, 36, 36, 36, 2484 },
	}
};
#define	numTargetPwr_11a	tpow.numTargetPwr_11a
#define	trgtPwr_11a		tpow.trgtPwr_11a
#define	numTargetPwr_11g	tpow.numTargetPwr_11g
#define	trgtPwr_11g		tpow.trgtPwr_11g
#define	numTargetPwr_11b	tpow.numTargetPwr_11b
#define	trgtPwr_11b		tpow.trgtPwr_11b

static HAL_BOOL
getChannelEdges(struct ath_hal *ah, u_int16_t flags, u_int16_t *low, u_int16_t *high)
{
	struct ath_hal_private *ahp = AH_PRIVATE(ah);
	HAL_CAPABILITIES *pCap = &ahp->ah_caps;

	if (flags & IEEE80211_CHAN_5GHZ) {
		*low = pCap->halLow5GhzChan;
		*high = pCap->halHigh5GhzChan;
		return AH_TRUE;
	}
	if (flags & IEEE80211_CHAN_2GHZ) {
		*low = pCap->halLow2GhzChan;
		*high = pCap->halHigh2GhzChan;
		return AH_TRUE;
	}
	return AH_FALSE;
}

static u_int
getWirelessModes(struct ath_hal *ah)
{
	u_int mode = 0;

	if (Amode) {
		mode = HAL_MODE_11A;
		if (!turbo5Disable)
			mode |= HAL_MODE_TURBO;
	}
	if (Bmode)
		mode |= HAL_MODE_11B;
	if (Gmode) {
		mode |= HAL_MODE_11G;
		if (!turbo2Disable) 
			mode |= HAL_MODE_108G;
	}
	if (HT20mode)
		mode |= HAL_MODE_11NG_HT20|HAL_MODE_11NA_HT20;
	if (HT40mode)
		mode |= HAL_MODE_11NG_HT40PLUS|HAL_MODE_11NA_HT40PLUS
		     |  HAL_MODE_11NG_HT40MINUS|HAL_MODE_11NA_HT40MINUS
		     ;
	return mode;
}

/* Enumerated Regulatory Domain Information 8 bit values indicate that
 * the regdomain is really a pair of unitary regdomains.  12 bit values
 * are the real unitary regdomains and are the only ones which have the
 * frequency bitmasks and flags set.
 */

enum EnumRd {
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

	FCC2_FCCA	= 0x20,		/* Canada */
	FCC2_WORLD	= 0x21,		/* Australia & HK */
	FCC2_ETSIC	= 0x22,
	FRANCE_RES	= 0x31,		/* Legacy France for OEM */
	FCC3_FCCA	= 0x3A,		/* USA & Canada w/5470 band, 11h, DFS enabled */
	FCC3_WORLD  = 0x3B,     /* USA & Canada w/5470 band, 11h, DFS enabled */

	ETSI1_WORLD	= 0x37,
	ETSI3_ETSIA	= 0x32,		/* France (optional) */
	ETSI2_WORLD	= 0x35,		/* Hungary & others */
	ETSI3_WORLD	= 0x36,		/* France & others */
	ETSI4_WORLD	= 0x30,
	ETSI4_ETSIC	= 0x38,
	ETSI5_WORLD	= 0x39,
	ETSI6_WORLD	= 0x34,		/* Bulgaria */
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

	APL3_FCCA   = 0x50,
	APL1_WORLD	= 0x52,		/* Latin America */
	APL1_FCCA	= 0x53,
	APL1_APLA	= 0x54,
	APL1_ETSIC	= 0x55,
	APL2_ETSIC	= 0x56,		/* Venezuela */
	APL5_WORLD	= 0x58,		/* Chile */
	APL6_WORLD	= 0x5B,		/* Singapore */
	APL7_FCCA   = 0x5C,     /* Taiwan 5.47 Band */
	APL8_WORLD  = 0x5D,     /* Malaysia 5GHz */
	APL9_WORLD  = 0x5E,     /* Korea 5GHz */

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

	/* Following definitions are used only by s/w to map old
 	 * Japan SKUs.
	 */
	MKK3_MKKA       = 0xF0,         /* Japan UNI-1 even + MKKA */
	MKK3_MKKA1      = 0xF1,         /* Japan UNI-1 even + MKKA1 */
	MKK3_FCCA       = 0xF2,         /* Japan UNI-1 even + FCCA */
	MKK4_MKKA       = 0xF3,         /* Japan UNI-1 even + UNI-2 + MKKA */
	MKK4_MKKA1      = 0xF4,         /* Japan UNI-1 even + UNI-2 + MKKA1 */
	MKK4_FCCA       = 0xF5,         /* Japan UNI-1 even + UNI-2 + FCCA */
	MKK9_MKKA       = 0xF6,         /* Japan UNI-1 even + 4.9GHz */
	MKK10_MKKA      = 0xF7,         /* Japan UNI-1 even + UNI-2 + 4.9GHz */

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
	APL8		= 0x0850,	/* Malaysia */
	APL9		= 0x0950,	/* Korea (South) ROC 3 */

	ETSI1		= 0x0130,	/* Europe & others */
	ETSI2		= 0x0230,	/* Europe & others */
	ETSI3		= 0x0330,	/* Europe & others */
	ETSI4		= 0x0430,	/* Europe & others */
	ETSI5		= 0x0530,	/* Europe & others */
	ETSI6		= 0x0630,	/* Europe & others */
	ETSIA		= 0x0A30,	/* France */
	ETSIB		= 0x0B30,	/* Israel */
	ETSIC		= 0x0C30,	/* Latin America */

	FCC1		= 0x0110,	/* US & others */
	FCC2		= 0x0120,	/* Canada, Australia & New Zealand */
	FCC3		= 0x0160,	/* US w/new middle band & DFS */    
	FCC4          	= 0x0165,     	/* US Public Safety */
	FCCA		= 0x0A10,	 

	APLD		= 0x0D50,	/* South Korea */

	MKK1		= 0x0140,	/* Japan (UNI-1 odd)*/
	MKK2		= 0x0240,	/* Japan (4.9 GHz + UNI-1 odd) */
	MKK3		= 0x0340,	/* Japan (UNI-1 even) */
	MKK4		= 0x0440,	/* Japan (UNI-1 even + UNI-2) */
	MKK5		= 0x0540,	/* Japan (UNI-1 even + UNI-2 + mid-band) */
	MKK6		= 0x0640,	/* Japan (UNI-1 odd + UNI-1 even) */
	MKK7		= 0x0740,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 */
	MKK8		= 0x0840,	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 + mid-band) */
	MKK9            = 0x0940,       /* Japan (UNI-1 even + 4.9 GHZ) */
	MKK10           = 0x0B40,       /* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	MKKA		= 0x0A40,	/* Japan */
	MKKC		= 0x0A50,

	NULL1		= 0x0198,
	WORLD		= 0x0199,
	DEBUG_REG_DMN	= 0x01ff,
};
#define DEF_REGDMN		FCC1_FCCA

static struct {
	const char *name;
	HAL_REG_DOMAIN rd;
} domains[] = {
#define	D(_x)	{ #_x, _x }
	D(NO_ENUMRD),
	D(NULL1_WORLD),		/* For 11b-only countries (no 11a allowed) */
	D(NULL1_ETSIB),		/* Israel */
	D(NULL1_ETSIC),
	D(FCC1_FCCA),		/* USA */
	D(FCC1_WORLD),		/* Hong Kong */
	D(FCC4_FCCA),		/* USA - Public Safety */

	D(FCC2_FCCA),		/* Canada */
	D(FCC2_WORLD),		/* Australia & HK */
	D(FCC2_ETSIC),
	D(FRANCE_RES),		/* Legacy France for OEM */
	D(FCC3_FCCA),
	D(FCC3_WORLD),

	D(ETSI1_WORLD),
	D(ETSI3_ETSIA),		/* France (optional) */
	D(ETSI2_WORLD),		/* Hungary & others */
	D(ETSI3_WORLD),		/* France & others */
	D(ETSI4_WORLD),
	D(ETSI4_ETSIC),
	D(ETSI5_WORLD),
	D(ETSI6_WORLD),		/* Bulgaria */
	D(ETSI_RESERVED),		/* Reserved (Do not used) */

	D(MKK1_MKKA),		/* Japan (JP1) */
	D(MKK1_MKKB),		/* Japan (JP0) */
	D(APL4_WORLD),		/* Singapore */
	D(MKK2_MKKA),		/* Japan with 4.9G channels */
	D(APL_RESERVED),		/* Reserved (Do not used)  */
	D(APL2_WORLD),		/* Korea */
	D(APL2_APLC),
	D(APL3_WORLD),
	D(MKK1_FCCA),		/* Japan (JP1-1) */
	D(APL2_APLD),		/* Korea with 2.3G channels */
	D(MKK1_MKKA1),		/* Japan (JE1) */
	D(MKK1_MKKA2),		/* Japan (JE2) */
	D(MKK1_MKKC),

	D(APL3_FCCA),
	D(APL1_WORLD),		/* Latin America */
	D(APL1_FCCA),
	D(APL1_APLA),
	D(APL1_ETSIC),
	D(APL2_ETSIC),		/* Venezuela */
	D(APL5_WORLD),		/* Chile */
	D(APL6_WORLD),		/* Singapore */
	D(APL7_FCCA),     /* Taiwan 5.47 Band */
	D(APL8_WORLD),     /* Malaysia 5GHz */
	D(APL9_WORLD),     /* Korea 5GHz */

	D(WOR0_WORLD),		/* World0 (WO0 SKU) */
	D(WOR1_WORLD),		/* World1 (WO1 SKU) */
	D(WOR2_WORLD),		/* World2 (WO2 SKU) */
	D(WOR3_WORLD),		/* World3 (WO3 SKU) */
	D(WOR4_WORLD),		/* World4 (WO4 SKU) */	
	D(WOR5_ETSIC),		/* World5 (WO5 SKU) */    

	D(WOR01_WORLD),		/* World0-1 (WW0-1 SKU) */
	D(WOR02_WORLD),		/* World0-2 (WW0-2 SKU) */
	D(EU1_WORLD),

	D(WOR9_WORLD),		/* World9 (WO9 SKU) */	
	D(WORA_WORLD),		/* WorldA (WOA SKU) */	

	D(MKK3_MKKB),		/* Japan UNI-1 even + MKKB */
	D(MKK3_MKKA2),		/* Japan UNI-1 even + MKKA2 */
	D(MKK3_MKKC),		/* Japan UNI-1 even + MKKC */

	D(MKK4_MKKB),		/* Japan UNI-1 even + UNI-2 + MKKB */
	D(MKK4_MKKA2),		/* Japan UNI-1 even + UNI-2 + MKKA2 */
	D(MKK4_MKKC),		/* Japan UNI-1 even + UNI-2 + MKKC */

	D(MKK5_MKKB),		/* Japan UNI-1 even + UNI-2 + mid-band + MKKB */
	D(MKK5_MKKA2),		/* Japan UNI-1 even + UNI-2 + mid-band + MKKA2 */
	D(MKK5_MKKC),		/* Japan UNI-1 even + UNI-2 + mid-band + MKKC */

	D(MKK6_MKKB),		/* Japan UNI-1 even + UNI-1 odd MKKB */
	D(MKK6_MKKA2),		/* Japan UNI-1 even + UNI-1 odd + MKKA2 */
	D(MKK6_MKKC),		/* Japan UNI-1 even + UNI-1 odd + MKKC */

	D(MKK7_MKKB),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKB */
	D(MKK7_MKKA2),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKA2 */
	D(MKK7_MKKC),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + MKKC */

	D(MKK8_MKKB),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKB */
	D(MKK8_MKKA2),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKA2 */
	D(MKK8_MKKC),		/* Japan UNI-1 even + UNI-1 odd + UNI-2 + mid-band + MKKC */

	D(MKK3_MKKA),         /* Japan UNI-1 even + MKKA */
	D(MKK3_MKKA1),         /* Japan UNI-1 even + MKKA1 */
	D(MKK3_FCCA),         /* Japan UNI-1 even + FCCA */
	D(MKK4_MKKA),         /* Japan UNI-1 even + UNI-2 + MKKA */
	D(MKK4_MKKA1),         /* Japan UNI-1 even + UNI-2 + MKKA1 */
	D(MKK4_FCCA),         /* Japan UNI-1 even + UNI-2 + FCCA */
	D(MKK9_MKKA),         /* Japan UNI-1 even + 4.9GHz */
	D(MKK10_MKKA),         /* Japan UNI-1 even + UNI-2 + 4.9GHz */

	D(APL1),	/* LAT & Asia */
	D(APL2),	/* LAT & Asia */
	D(APL3),	/* Taiwan */
	D(APL4),	/* Jordan */
	D(APL5),	/* Chile */
	D(APL6),	/* Singapore */
	D(APL8),	/* Malaysia */
	D(APL9),	/* Korea (South) ROC 3 */

	D(ETSI1),	/* Europe & others */
	D(ETSI2),	/* Europe & others */
	D(ETSI3),	/* Europe & others */
	D(ETSI4),	/* Europe & others */
	D(ETSI5),	/* Europe & others */
	D(ETSI6),	/* Europe & others */
	D(ETSIA),	/* France */
	D(ETSIB),	/* Israel */
	D(ETSIC),	/* Latin America */

	D(FCC1),	/* US & others */
	D(FCC2),
	D(FCC3),	/* US w/new middle band & DFS */    
	D(FCC4),     	/* US Public Safety */
	D(FCCA),	 

	D(APLD),	/* South Korea */

	D(MKK1),	/* Japan (UNI-1 odd)*/
	D(MKK2),	/* Japan (4.9 GHz + UNI-1 odd) */
	D(MKK3),	/* Japan (UNI-1 even) */
	D(MKK4),	/* Japan (UNI-1 even + UNI-2) */
	D(MKK5),	/* Japan (UNI-1 even + UNI-2 + mid-band) */
	D(MKK6),	/* Japan (UNI-1 odd + UNI-1 even) */
	D(MKK7),	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 */
	D(MKK8),	/* Japan (UNI-1 odd + UNI-1 even + UNI-2 + mid-band) */
	D(MKK9),       /* Japan (UNI-1 even + 4.9 GHZ) */
	D(MKK10),       /* Japan (UNI-1 even + UNI-2 + 4.9 GHZ) */
	D(MKKA),	/* Japan */
	D(MKKC),

	D(NULL1),
	D(WORLD),
	D(DEBUG_REG_DMN),
#undef D
};

static HAL_BOOL
rdlookup(const char *name, HAL_REG_DOMAIN *rd)
{
	int i;

	for (i = 0; i < nitems(domains); i++)
		if (strcasecmp(domains[i].name, name) == 0) {
			*rd = domains[i].rd;
			return AH_TRUE;
		}
	return AH_FALSE;
}

static const char *
getrdname(HAL_REG_DOMAIN rd)
{
	int i;

	for (i = 0; i < nitems(domains); i++)
		if (domains[i].rd == rd)
			return domains[i].name;
	return NULL;
}

static void
rdlist()
{
	int i;

	printf("\nRegulatory domains:\n\n");
	for (i = 0; i < nitems(domains); i++)
		printf("%-15s%s", domains[i].name,
			((i+1)%5) == 0 ? "\n" : "");
	printf("\n");
}

typedef struct {
	HAL_CTRY_CODE	countryCode;	   
	HAL_REG_DOMAIN	regDmnEnum;
	const char*	isoName;
	const char*	name;
} COUNTRY_CODE_TO_ENUM_RD;
 
/*
 * Country Code Table to Enumerated RD
 */
static COUNTRY_CODE_TO_ENUM_RD allCountries[] = {
    {CTRY_DEBUG,       NO_ENUMRD,     "DB", "DEBUG" },
    {CTRY_DEFAULT,     DEF_REGDMN,    "NA", "NO_COUNTRY_SET" },
    {CTRY_ALBANIA,     NULL1_WORLD,   "AL", "ALBANIA" },
    {CTRY_ALGERIA,     NULL1_WORLD,   "DZ", "ALGERIA" },
    {CTRY_ARGENTINA,   APL3_WORLD,    "AR", "ARGENTINA" },
    {CTRY_ARMENIA,     ETSI4_WORLD,   "AM", "ARMENIA" },
    {CTRY_AUSTRALIA,   FCC2_WORLD,    "AU", "AUSTRALIA" },
    {CTRY_AUSTRIA,     ETSI1_WORLD,   "AT", "AUSTRIA" },
    {CTRY_AZERBAIJAN,  ETSI4_WORLD,   "AZ", "AZERBAIJAN" },
    {CTRY_BAHRAIN,     APL6_WORLD,   "BH", "BAHRAIN" },
    {CTRY_BELARUS,     NULL1_WORLD,   "BY", "BELARUS" },
    {CTRY_BELGIUM,     ETSI1_WORLD,   "BE", "BELGIUM" },
    {CTRY_BELIZE,      APL1_ETSIC,    "BZ", "BELIZE" },
    {CTRY_BOLIVIA,     APL1_ETSIC,    "BO", "BOLVIA" },
    {CTRY_BRAZIL,      FCC3_WORLD,    "BR", "BRAZIL" },
    {CTRY_BRUNEI_DARUSSALAM,APL1_WORLD,"BN", "BRUNEI DARUSSALAM" },
    {CTRY_BULGARIA,    ETSI6_WORLD,   "BG", "BULGARIA" },
    {CTRY_CANADA,      FCC2_FCCA,     "CA", "CANADA" },
    {CTRY_CHILE,       APL6_WORLD,    "CL", "CHILE" },
    {CTRY_CHINA,       APL1_WORLD,    "CN", "CHINA" },
    {CTRY_COLOMBIA,    FCC1_FCCA,     "CO", "COLOMBIA" },
    {CTRY_COSTA_RICA,  NULL1_WORLD,   "CR", "COSTA RICA" },
    {CTRY_CROATIA,     ETSI3_WORLD,   "HR", "CROATIA" },
    {CTRY_CYPRUS,      ETSI1_WORLD,   "CY", "CYPRUS" },
    {CTRY_CZECH,       ETSI3_WORLD,   "CZ", "CZECH REPUBLIC" },
    {CTRY_DENMARK,     ETSI1_WORLD,   "DK", "DENMARK" },
    {CTRY_DOMINICAN_REPUBLIC,FCC1_FCCA,"DO", "DOMINICAN REPUBLIC" },
    {CTRY_ECUADOR,     NULL1_WORLD,   "EC", "ECUADOR" },
    {CTRY_EGYPT,       ETSI3_WORLD,   "EG", "EGYPT" },
    {CTRY_EL_SALVADOR, NULL1_WORLD,   "SV", "EL SALVADOR" },    
    {CTRY_ESTONIA,     ETSI1_WORLD,   "EE", "ESTONIA" },
    {CTRY_FINLAND,     ETSI1_WORLD,   "FI", "FINLAND" },
    {CTRY_FRANCE,      ETSI3_WORLD,   "FR", "FRANCE" },
    {CTRY_FRANCE2,     ETSI3_WORLD,   "F2", "FRANCE_RES" },
    {CTRY_GEORGIA,     ETSI4_WORLD,   "GE", "GEORGIA" },
    {CTRY_GERMANY,     ETSI1_WORLD,   "DE", "GERMANY" },
    {CTRY_GREECE,      ETSI1_WORLD,   "GR", "GREECE" },
    {CTRY_GUATEMALA,   FCC1_FCCA,     "GT", "GUATEMALA" },
    {CTRY_HONDURAS,    NULL1_WORLD,   "HN", "HONDURAS" },
    {CTRY_HONG_KONG,   FCC2_WORLD,    "HK", "HONG KONG" },
    {CTRY_HUNGARY,     ETSI1_WORLD,   "HU", "HUNGARY" },
    {CTRY_ICELAND,     ETSI1_WORLD,   "IS", "ICELAND" },
    {CTRY_INDIA,       APL6_WORLD,    "IN", "INDIA" },
    {CTRY_INDONESIA,   APL1_WORLD,    "ID", "INDONESIA" },
    {CTRY_IRAN,        APL1_WORLD,    "IR", "IRAN" },
    {CTRY_IRELAND,     ETSI1_WORLD,   "IE", "IRELAND" },
    {CTRY_ISRAEL,      NULL1_WORLD,   "IL", "ISRAEL" },
    {CTRY_ITALY,       ETSI1_WORLD,   "IT", "ITALY" },
    {CTRY_JAPAN,       MKK1_MKKA,     "JP", "JAPAN" },
    {CTRY_JAPAN1,      MKK1_MKKB,     "JP", "JAPAN1" },
    {CTRY_JAPAN2,      MKK1_FCCA,     "JP", "JAPAN2" },    
    {CTRY_JAPAN3,      MKK2_MKKA,     "JP", "JAPAN3" },
    {CTRY_JAPAN4,      MKK1_MKKA1,    "JP", "JAPAN4" },
    {CTRY_JAPAN5,      MKK1_MKKA2,    "JP", "JAPAN5" },    
    {CTRY_JAPAN6,      MKK1_MKKC,     "JP", "JAPAN6" },    

    {CTRY_JAPAN7,      MKK3_MKKB,     "JP", "JAPAN7" },
    {CTRY_JAPAN8,      MKK3_MKKA2,    "JP", "JAPAN8" },    
    {CTRY_JAPAN9,      MKK3_MKKC,     "JP", "JAPAN9" },    

    {CTRY_JAPAN10,      MKK4_MKKB,     "JP", "JAPAN10" },
    {CTRY_JAPAN11,      MKK4_MKKA2,    "JP", "JAPAN11" },    
    {CTRY_JAPAN12,      MKK4_MKKC,     "JP", "JAPAN12" },    

    {CTRY_JAPAN13,      MKK5_MKKB,     "JP", "JAPAN13" },
    {CTRY_JAPAN14,      MKK5_MKKA2,    "JP", "JAPAN14" },    
    {CTRY_JAPAN15,      MKK5_MKKC,     "JP", "JAPAN15" },    

    {CTRY_JAPAN16,      MKK6_MKKB,     "JP", "JAPAN16" },
    {CTRY_JAPAN17,      MKK6_MKKA2,    "JP", "JAPAN17" },    
    {CTRY_JAPAN18,      MKK6_MKKC,     "JP", "JAPAN18" },    

    {CTRY_JAPAN19,      MKK7_MKKB,     "JP", "JAPAN19" },
    {CTRY_JAPAN20,      MKK7_MKKA2,    "JP", "JAPAN20" },    
    {CTRY_JAPAN21,      MKK7_MKKC,     "JP", "JAPAN21" },    

    {CTRY_JAPAN22,      MKK8_MKKB,     "JP", "JAPAN22" },
    {CTRY_JAPAN23,      MKK8_MKKA2,    "JP", "JAPAN23" },    
    {CTRY_JAPAN24,      MKK8_MKKC,     "JP", "JAPAN24" },    

    {CTRY_JORDAN,      APL4_WORLD,    "JO", "JORDAN" },
    {CTRY_KAZAKHSTAN,  NULL1_WORLD,   "KZ", "KAZAKHSTAN" },
    {CTRY_KOREA_NORTH, APL2_WORLD,    "KP", "NORTH KOREA" },
    {CTRY_KOREA_ROC,   APL2_WORLD,    "KR", "KOREA REPUBLIC" },
    {CTRY_KOREA_ROC2,  APL2_WORLD,    "K2", "KOREA REPUBLIC2" },
    {CTRY_KOREA_ROC3,  APL9_WORLD,    "K3", "KOREA REPUBLIC3" },
    {CTRY_KUWAIT,      NULL1_WORLD,   "KW", "KUWAIT" },
    {CTRY_LATVIA,      ETSI1_WORLD,   "LV", "LATVIA" },
    {CTRY_LEBANON,     NULL1_WORLD,   "LB", "LEBANON" },
    {CTRY_LIECHTENSTEIN,ETSI1_WORLD,  "LI", "LIECHTENSTEIN" },
    {CTRY_LITHUANIA,   ETSI1_WORLD,   "LT", "LITHUANIA" },
    {CTRY_LUXEMBOURG,  ETSI1_WORLD,   "LU", "LUXEMBOURG" },
    {CTRY_MACAU,       FCC2_WORLD,    "MO", "MACAU" },
    {CTRY_MACEDONIA,   NULL1_WORLD,   "MK", "MACEDONIA" },
    {CTRY_MALAYSIA,    APL8_WORLD,    "MY", "MALAYSIA" },
    {CTRY_MALTA,       ETSI1_WORLD,   "MT", "MALTA" },
    {CTRY_MEXICO,      FCC1_FCCA,     "MX", "MEXICO" },
    {CTRY_MONACO,      ETSI4_WORLD,   "MC", "MONACO" },
    {CTRY_MOROCCO,     NULL1_WORLD,   "MA", "MOROCCO" },
    {CTRY_NETHERLANDS, ETSI1_WORLD,   "NL", "NETHERLANDS" },
    {CTRY_NEW_ZEALAND, FCC2_ETSIC,    "NZ", "NEW ZEALAND" },
    {CTRY_NORWAY,      ETSI1_WORLD,   "NO", "NORWAY" },
    {CTRY_OMAN,        APL6_WORLD,    "OM", "OMAN" },
    {CTRY_PAKISTAN,    NULL1_WORLD,   "PK", "PAKISTAN" },
    {CTRY_PANAMA,      FCC1_FCCA,     "PA", "PANAMA" },
    {CTRY_PERU,        APL1_WORLD,    "PE", "PERU" },
    {CTRY_PHILIPPINES, APL1_WORLD,    "PH", "PHILIPPINES" },
    {CTRY_POLAND,      ETSI1_WORLD,   "PL", "POLAND" },
    {CTRY_PORTUGAL,    ETSI1_WORLD,   "PT", "PORTUGAL" },
    {CTRY_PUERTO_RICO, FCC1_FCCA,     "PR", "PUERTO RICO" },
    {CTRY_QATAR,       NULL1_WORLD,   "QA", "QATAR" },
    {CTRY_ROMANIA,     NULL1_WORLD,   "RO", "ROMANIA" },
    {CTRY_RUSSIA,      NULL1_WORLD,   "RU", "RUSSIA" },
    {CTRY_SAUDI_ARABIA,NULL1_WORLD,   "SA", "SAUDI ARABIA" },
    {CTRY_SINGAPORE,   APL6_WORLD,    "SG", "SINGAPORE" },
    {CTRY_SLOVAKIA,    ETSI1_WORLD,   "SK", "SLOVAK REPUBLIC" },
    {CTRY_SLOVENIA,    ETSI1_WORLD,   "SI", "SLOVENIA" },
    {CTRY_SOUTH_AFRICA,FCC3_WORLD,    "ZA", "SOUTH AFRICA" },
    {CTRY_SPAIN,       ETSI1_WORLD,   "ES", "SPAIN" },
    {CTRY_SWEDEN,      ETSI1_WORLD,   "SE", "SWEDEN" },
    {CTRY_SWITZERLAND, ETSI1_WORLD,   "CH", "SWITZERLAND" },
    {CTRY_SYRIA,       NULL1_WORLD,   "SY", "SYRIA" },
    {CTRY_TAIWAN,      APL3_FCCA,    "TW", "TAIWAN" },
    {CTRY_THAILAND,    NULL1_WORLD,   "TH", "THAILAND" },
    {CTRY_TRINIDAD_Y_TOBAGO,ETSI4_WORLD,"TT", "TRINIDAD & TOBAGO" },
    {CTRY_TUNISIA,     ETSI3_WORLD,   "TN", "TUNISIA" },
    {CTRY_TURKEY,      ETSI3_WORLD,   "TR", "TURKEY" },
    {CTRY_UKRAINE,     NULL1_WORLD,   "UA", "UKRAINE" },
    {CTRY_UAE,         NULL1_WORLD,   "AE", "UNITED ARAB EMIRATES" },
    {CTRY_UNITED_KINGDOM, ETSI1_WORLD,"GB", "UNITED KINGDOM" },
    {CTRY_UNITED_STATES, FCC1_FCCA,   "US", "UNITED STATES" },
    {CTRY_UNITED_STATES_FCC49, FCC4_FCCA,   "PS", "UNITED STATES (PUBLIC SAFETY)" },
    {CTRY_URUGUAY,     APL2_WORLD,    "UY", "URUGUAY" },
    {CTRY_UZBEKISTAN,  FCC3_FCCA,     "UZ", "UZBEKISTAN" },    
    {CTRY_VENEZUELA,   APL2_ETSIC,    "VE", "VENEZUELA" },
    {CTRY_VIET_NAM,    NULL1_WORLD,   "VN", "VIET NAM" },
    {CTRY_YEMEN,       NULL1_WORLD,   "YE", "YEMEN" },
    {CTRY_ZIMBABWE,    NULL1_WORLD,   "ZW", "ZIMBABWE" }    
};

static HAL_BOOL
cclookup(const char *name, HAL_REG_DOMAIN *rd, HAL_CTRY_CODE *cc)
{
	int i;

	for (i = 0; i < nitems(allCountries); i++)
		if (strcasecmp(allCountries[i].isoName, name) == 0 ||
		    strcasecmp(allCountries[i].name, name) == 0) {
			*rd = allCountries[i].regDmnEnum;
			*cc = allCountries[i].countryCode;
			return AH_TRUE;
		}
	return AH_FALSE;
}

static const char *
getccname(HAL_CTRY_CODE cc)
{
	int i;

	for (i = 0; i < nitems(allCountries); i++)
		if (allCountries[i].countryCode == cc)
			return allCountries[i].name;
	return NULL;
}

static const char *
getccisoname(HAL_CTRY_CODE cc)
{
	int i;

	for (i = 0; i < nitems(allCountries); i++)
		if (allCountries[i].countryCode == cc)
			return allCountries[i].isoName;
	return NULL;
}

static void
cclist()
{
	int i;

	printf("\nCountry codes:\n");
	for (i = 0; i < nitems(allCountries); i++)
		printf("%2s %-15.15s%s",
			allCountries[i].isoName,
			allCountries[i].name,
			((i+1)%4) == 0 ? "\n" : " ");
	printf("\n");
}

static HAL_BOOL
setRateTable(struct ath_hal *ah, const struct ieee80211_channel *chan, 
		   int16_t tpcScaleReduction, int16_t powerLimit,
                   int16_t *pMinPower, int16_t *pMaxPower);

static void
calctxpower(struct ath_hal *ah,
	int nchan, const struct ieee80211_channel *chans,
	int16_t tpcScaleReduction, int16_t powerLimit, int16_t *txpow)
{
	int16_t minpow;
	int i;

	for (i = 0; i < nchan; i++)
		if (!setRateTable(ah, &chans[i],
		    tpcScaleReduction, powerLimit, &minpow, &txpow[i])) {
			printf("unable to set rate table\n");
			exit(-1);
		}
}

int	n = 1;
const char *sep = "";
int	dopassive = 0;
int	showchannels = 0;
int	isdfs = 0;
int	is4ms = 0;

static int
anychan(const struct ieee80211_channel *chans, int nc, int flag)
{
	int i;

	for (i = 0; i < nc; i++)
		if ((chans[i].ic_flags & flag) != 0)
			return 1;
	return 0;
}

static __inline int
mapgsm(u_int freq, u_int flags)
{
	freq *= 10;
	if (flags & IEEE80211_CHAN_QUARTER)
		freq += 5;
	else if (flags & IEEE80211_CHAN_HALF)
		freq += 10;
	else
		freq += 20;
	return (freq - 24220) / 5;
}

static __inline int
mappsb(u_int freq, u_int flags)
{
	return ((freq * 10) + (((freq % 5) == 2) ? 5 : 0) - 49400) / 5;
}

/*
 * Convert GHz frequency to IEEE channel number.
 */
int
ath_hal_mhz2ieee(struct ath_hal *ah, u_int freq, u_int flags)
{
	if (flags & IEEE80211_CHAN_2GHZ) {	/* 2GHz band */
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return ((int)freq - 2407) / 5;
		else
			return 15 + ((freq - 2512) / 20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {/* 5Ghz band */
		if (IS_CHAN_IN_PUBLIC_SAFETY_BAND(freq))
			return mappsb(freq, flags);
		else if ((flags & IEEE80211_CHAN_A) && (freq <= 5000))
			return (freq - 4000) / 5;
		else
			return (freq - 5000) / 5;
	} else {			/* either, guess */
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return ((int)freq - 2407) / 5;
		if (freq < 5000) {
			if (IS_CHAN_IN_PUBLIC_SAFETY_BAND(freq))
				return mappsb(freq, flags);
			else if (freq > 4900)
				return (freq - 4000) / 5;
			else
				return 15 + ((freq - 2512) / 20);
		}
		return (freq - 5000) / 5;
	}
}

#define	IEEE80211_IS_CHAN_4MS(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_4MSXMIT) != 0)

static void
dumpchannels(struct ath_hal *ah, int nc,
	const struct ieee80211_channel *chans, int16_t *txpow)
{
	int i;

	for (i = 0; i < nc; i++) {
		const struct ieee80211_channel *c = &chans[i];
		int type;

		if (showchannels)
			printf("%s%3d", sep,
			    ath_hal_mhz2ieee(ah, c->ic_freq, c->ic_flags));
		else
			printf("%s%u", sep, c->ic_freq);
		if (IEEE80211_IS_CHAN_HALF(c))
			type = 'H';
		else if (IEEE80211_IS_CHAN_QUARTER(c))
			type = 'Q';
		else if (IEEE80211_IS_CHAN_TURBO(c))
			type = 'T';
		else if (IEEE80211_IS_CHAN_HT(c))
			type = 'N';
		else if (IEEE80211_IS_CHAN_A(c))
			type = 'A';
		else if (IEEE80211_IS_CHAN_108G(c))
			type = 'T';
		else if (IEEE80211_IS_CHAN_G(c))
			type = 'G';
		else
			type = 'B';
		if (dopassive && IEEE80211_IS_CHAN_PASSIVE(c))
			type = tolower(type);
		if (isdfs && is4ms)
			printf("%c%c%c %d.%d", type,
			    IEEE80211_IS_CHAN_DFS(c) ? '*' : ' ',
			    IEEE80211_IS_CHAN_4MS(c) ? '4' : ' ',
			    txpow[i]/2, (txpow[i]%2)*5);
		else if (isdfs)
			printf("%c%c %d.%d", type,
			    IEEE80211_IS_CHAN_DFS(c) ? '*' : ' ',
			    txpow[i]/2, (txpow[i]%2)*5);
		else if (is4ms)
			printf("%c%c %d.%d", type,
			    IEEE80211_IS_CHAN_4MS(c) ? '4' : ' ',
			    txpow[i]/2, (txpow[i]%2)*5);
		else
			printf("%c %d.%d", type, txpow[i]/2, (txpow[i]%2)*5);
		if ((n++ % (showchannels ? 7 : 6)) == 0)
			sep = "\n";
		else
			sep = " ";
	}
}

static void
intersect(struct ieee80211_channel *dst, int16_t *dtxpow, int *nd,
    const struct ieee80211_channel *src, int16_t *stxpow, int ns)
{
	int i = 0, j, k, l;
	while (i < *nd) {
		for (j = 0; j < ns && dst[i].ic_freq != src[j].ic_freq; j++)
			;
		if (j < ns && dtxpow[i] == stxpow[j]) {
			for (k = i+1, l = i; k < *nd; k++, l++)
				dst[l] = dst[k];
			(*nd)--;
		} else
			i++;
	}
}

static void
usage(const char *progname)
{
	printf("usage: %s [-acdefoilpr4ABGT] [-m opmode] [cc | rd]\n", progname);
	exit(-1);
}

static HAL_BOOL
getChipPowerLimits(struct ath_hal *ah, struct ieee80211_channel *chan)
{
}

static HAL_BOOL
eepromRead(struct ath_hal *ah, u_int off, u_int16_t *data)
{
	/* emulate enough stuff to handle japan channel shift */
	switch (off) {
	case AR_EEPROM_VERSION:
		*data = eeversion;
		return AH_TRUE;
	case AR_EEPROM_REG_CAPABILITIES_OFFSET:
		*data = AR_EEPROM_EEREGCAP_EN_KK_NEW_11A;
		return AH_TRUE;
	case AR_EEPROM_REG_CAPABILITIES_OFFSET_PRE4_0:
		*data = AR_EEPROM_EEREGCAP_EN_KK_NEW_11A_PRE4_0;
		return AH_TRUE;
	}
	return AH_FALSE;
}

HAL_STATUS
getCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t *result)
{
	const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;

	switch (type) {
	case HAL_CAP_REG_DMN:		/* regulatory domain */
		*result = AH_PRIVATE(ah)->ah_currentRD;
		return HAL_OK;
	default:
		return HAL_EINVAL;
	}
}

#define HAL_MODE_HT20 \
	(HAL_MODE_11NG_HT20 |  HAL_MODE_11NA_HT20)
#define	HAL_MODE_HT40 \
	(HAL_MODE_11NG_HT40PLUS | HAL_MODE_11NG_HT40MINUS | \
	 HAL_MODE_11NA_HT40PLUS | HAL_MODE_11NA_HT40MINUS)
#define	HAL_MODE_HT	(HAL_MODE_HT20 | HAL_MODE_HT40)
     
int
main(int argc, char *argv[])
{
	static const u_int16_t tpcScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };
	struct ath_hal_private ahp;
	struct ieee80211_channel achans[IEEE80211_CHAN_MAX];
	int16_t atxpow[IEEE80211_CHAN_MAX];
	struct ieee80211_channel bchans[IEEE80211_CHAN_MAX];
	int16_t btxpow[IEEE80211_CHAN_MAX];
	struct ieee80211_channel gchans[IEEE80211_CHAN_MAX];
	int16_t gtxpow[IEEE80211_CHAN_MAX];
	struct ieee80211_channel tchans[IEEE80211_CHAN_MAX];
	int16_t ttxpow[IEEE80211_CHAN_MAX];
	struct ieee80211_channel tgchans[IEEE80211_CHAN_MAX];
	int16_t tgtxpow[IEEE80211_CHAN_MAX];
	struct ieee80211_channel nchans[IEEE80211_CHAN_MAX];
	int16_t ntxpow[IEEE80211_CHAN_MAX];
	int i, na, nb, ng, nt, ntg, nn;
	HAL_BOOL showall = AH_FALSE;
	HAL_BOOL extendedChanMode = AH_TRUE;
	int modes = 0;
	int16_t tpcReduction, powerLimit;
	int showdfs = 0;
	int show4ms = 0;

	memset(&ahp, 0, sizeof(ahp));
	ahp.ah_getChannelEdges = getChannelEdges;
	ahp.ah_getWirelessModes = getWirelessModes;
	ahp.ah_eepromRead = eepromRead;
	ahp.ah_getChipPowerLimits = getChipPowerLimits;
	ahp.ah_caps.halWirelessModes = HAL_MODE_ALL;
	ahp.ah_caps.halLow5GhzChan = 4920;
	ahp.ah_caps.halHigh5GhzChan = 6100;
	ahp.ah_caps.halLow2GhzChan = 2312;
	ahp.ah_caps.halHigh2GhzChan = 2732;
	ahp.ah_caps.halChanHalfRate = AH_TRUE;
	ahp.ah_caps.halChanQuarterRate = AH_TRUE;
	ahp.h.ah_getCapability = getCapability;
	ahp.ah_opmode = HAL_M_STA;

	tpcReduction = tpcScaleReductionTable[0];
	powerLimit =  MAX_RATE_POWER;

	while ((i = getopt(argc, argv, "acdeflm:pr4ABGhHNT")) != -1)
		switch (i) {
		case 'a':
			showall = AH_TRUE;
			break;
		case 'c':
			showchannels = AH_TRUE;
			break;
		case 'd':
			ath_hal_debug = HAL_DEBUG_ANY;
			break;
		case 'e':
			extendedChanMode = AH_FALSE;
			break;
		case 'f':
			showchannels = AH_FALSE;
			break;
		case 'l':
			cclist();
			rdlist();
			exit(0);
		case 'm':
			if (strncasecmp(optarg, "sta", 2) == 0)
				ahp.ah_opmode = HAL_M_STA;
			else if (strncasecmp(optarg, "ibss", 2) == 0)
				ahp.ah_opmode = HAL_M_IBSS;
			else if (strncasecmp(optarg, "adhoc", 2) == 0)
				ahp.ah_opmode = HAL_M_IBSS;
			else if (strncasecmp(optarg, "ap", 2) == 0)
				ahp.ah_opmode = HAL_M_HOSTAP;
			else if (strncasecmp(optarg, "hostap", 2) == 0)
				ahp.ah_opmode = HAL_M_HOSTAP;
			else if (strncasecmp(optarg, "monitor", 2) == 0)
				ahp.ah_opmode = HAL_M_MONITOR;
			else
				usage(argv[0]);
			break;
		case 'p':
			dopassive = 1;
			break;
		case 'A':
			modes |= HAL_MODE_11A;
			break;
		case 'B':
			modes |= HAL_MODE_11B;
			break;
		case 'G':
			modes |= HAL_MODE_11G;
			break;
		case 'h':
			modes |= HAL_MODE_HT20;
			break;
		case 'H':
			modes |= HAL_MODE_HT40;
			break;
		case 'N':
			modes |= HAL_MODE_HT;
			break;
		case 'T':
			modes |= HAL_MODE_TURBO | HAL_MODE_108G;
			break;
		case 'r':
			showdfs = 1;
			break;
		case '4':
			show4ms = 1;
			break;
		default:
			usage(argv[0]);
		}
	switch (argc - optind)  {
	case 0:
		if (!cclookup("US", &rd, &cc)) {
			printf("%s: unknown country code\n", "US");
			exit(-1);
		}
		break;
	case 1:			/* cc/regdomain */
		if (!cclookup(argv[optind], &rd, &cc)) {
			if (!rdlookup(argv[optind], &rd)) {
				const char* rdname;

				rd = strtoul(argv[optind], NULL, 0);
				rdname = getrdname(rd);
				if (rdname == NULL) {
					printf("%s: unknown country/regulatory "
						"domain code\n", argv[optind]);
					exit(-1);
				}
			}
			cc = CTRY_DEFAULT;
		}
		break;
	default:		/* regdomain cc */
		if (!rdlookup(argv[optind], &rd)) {
			const char* rdname;

			rd = strtoul(argv[optind], NULL, 0);
			rdname = getrdname(rd);
			if (rdname == NULL) {
				printf("%s: unknown country/regulatory "
					"domain code\n", argv[optind]);
				exit(-1);
			}
		}
		if (!cclookup(argv[optind+1], &rd, &cc))
			cc = strtoul(argv[optind+1], NULL, 0);
		break;
	}
	if (cc != CTRY_DEFAULT)
		printf("\n%s (%s, 0x%x, %u) %s (0x%x, %u)\n",
			getccname(cc), getccisoname(cc), cc, cc,
			getrdname(rd), rd, rd);
	else
		printf("\n%s (0x%x, %u)\n",
			getrdname(rd), rd, rd);

	if (modes == 0) {
		/* NB: no HAL_MODE_HT */
		modes = HAL_MODE_11A | HAL_MODE_11B |
			HAL_MODE_11G | HAL_MODE_TURBO | HAL_MODE_108G;
	}
	na = nb = ng = nt = ntg = nn = 0;
	if (modes & HAL_MODE_11G) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, gchans, IEEE80211_CHAN_MAX, &ng,
		    HAL_MODE_11G, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, ng, gchans, tpcReduction, powerLimit, gtxpow);
			if (showdfs)
				isdfs |= anychan(gchans, ng, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(gchans, ng, IEEE80211_CHAN_4MSXMIT);
		}
	}
	if (modes & HAL_MODE_11B) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, bchans, IEEE80211_CHAN_MAX, &nb,
		    HAL_MODE_11B, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, nb, bchans, tpcReduction, powerLimit, btxpow);
			if (showdfs)
				isdfs |= anychan(bchans, nb, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(bchans, nb, IEEE80211_CHAN_4MSXMIT);
		}
	}
	if (modes & HAL_MODE_11A) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, achans, IEEE80211_CHAN_MAX, &na,
		    HAL_MODE_11A, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, na, achans, tpcReduction, powerLimit, atxpow);
			if (showdfs)
				isdfs |= anychan(achans, na, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(achans, na, IEEE80211_CHAN_4MSXMIT);
		}
	}
	if (modes & HAL_MODE_TURBO) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, tchans, IEEE80211_CHAN_MAX, &nt,
		    HAL_MODE_TURBO, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, nt, tchans, tpcReduction, powerLimit, ttxpow);
			if (showdfs)
				isdfs |= anychan(tchans, nt, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(tchans, nt, IEEE80211_CHAN_4MSXMIT);
		}
	}	
	if (modes & HAL_MODE_108G) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, tgchans, IEEE80211_CHAN_MAX, &ntg,
		    HAL_MODE_108G, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, ntg, tgchans, tpcReduction, powerLimit, tgtxpow);
			if (showdfs)
				isdfs |= anychan(tgchans, ntg, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(tgchans, ntg, IEEE80211_CHAN_4MSXMIT);
		}
	}
	if (modes & HAL_MODE_HT) {
		ahp.ah_currentRD = rd;
		if (ath_hal_getchannels(&ahp.h, nchans, IEEE80211_CHAN_MAX, &nn,
		    modes & HAL_MODE_HT, cc, rd, extendedChanMode) == HAL_OK) {
			calctxpower(&ahp.h, nn, nchans, tpcReduction, powerLimit, ntxpow);
			if (showdfs)
				isdfs |= anychan(nchans, nn, IEEE80211_CHAN_DFS);
			if (show4ms)
				is4ms |= anychan(nchans, nn, IEEE80211_CHAN_4MSXMIT);
		}
	}

	if (!showall) {
#define	CHECKMODES(_modes, _m)	((_modes & (_m)) == (_m))
		if (CHECKMODES(modes, HAL_MODE_11B|HAL_MODE_11G)) {
			/* b ^= g */
			intersect(bchans, btxpow, &nb, gchans, gtxpow, ng);
		}
		if (CHECKMODES(modes, HAL_MODE_11A|HAL_MODE_TURBO)) {
			/* t ^= a */
			intersect(tchans, ttxpow, &nt, achans, atxpow, na);
		}
		if (CHECKMODES(modes, HAL_MODE_11G|HAL_MODE_108G)) {
			/* tg ^= g */
			intersect(tgchans, tgtxpow, &ntg, gchans, gtxpow, ng);
		}
		if (CHECKMODES(modes, HAL_MODE_11G|HAL_MODE_HT)) {
			/* g ^= n */
			intersect(gchans, gtxpow, &ng, nchans, ntxpow, nn);
		}
		if (CHECKMODES(modes, HAL_MODE_11A|HAL_MODE_HT)) {
			/* a ^= n */
			intersect(achans, atxpow, &na, nchans, ntxpow, nn);
		}
#undef CHECKMODES
	}

	if (modes & HAL_MODE_11G)
		dumpchannels(&ahp.h, ng, gchans, gtxpow);
	if (modes & HAL_MODE_11B)
		dumpchannels(&ahp.h, nb, bchans, btxpow);
	if (modes & HAL_MODE_11A)
		dumpchannels(&ahp.h, na, achans, atxpow);
	if (modes & HAL_MODE_108G)
		dumpchannels(&ahp.h, ntg, tgchans, tgtxpow);
	if (modes & HAL_MODE_TURBO)
		dumpchannels(&ahp.h, nt, tchans, ttxpow);
	if (modes & HAL_MODE_HT)
		dumpchannels(&ahp.h, nn, nchans, ntxpow);
	printf("\n");
	return (0);
}

/*
 * Search a list for a specified value v that is within
 * EEP_DELTA of the search values.  Return the closest
 * values in the list above and below the desired value.
 * EEP_DELTA is a factional value; everything is scaled
 * so only integer arithmetic is used.
 *
 * NB: the input list is assumed to be sorted in ascending order
 */
static void
ar5212GetLowerUpperValues(u_int16_t v, u_int16_t *lp, u_int16_t listSize,
                          u_int16_t *vlo, u_int16_t *vhi)
{
	u_int32_t target = v * EEP_SCALE;
	u_int16_t *ep = lp+listSize;

	/*
	 * Check first and last elements for out-of-bounds conditions.
	 */
	if (target < (u_int32_t)(lp[0] * EEP_SCALE - EEP_DELTA)) {
		*vlo = *vhi = lp[0];
		return;
	}
	if (target > (u_int32_t)(ep[-1] * EEP_SCALE + EEP_DELTA)) {
		*vlo = *vhi = ep[-1];
		return;
	}

	/* look for value being near or between 2 values in list */
	for (; lp < ep; lp++) {
		/*
		 * If value is close to the current value of the list
		 * then target is not between values, it is one of the values
		 */
		if (abs(lp[0] * EEP_SCALE - target) < EEP_DELTA) {
			*vlo = *vhi = lp[0];
			return;
		}
		/*
		 * Look for value being between current value and next value
		 * if so return these 2 values
		 */
		if (target < (u_int32_t)(lp[1] * EEP_SCALE - EEP_DELTA)) {
			*vlo = lp[0];
			*vhi = lp[1];
			return;
		}
	}
}

/*
 * Find the maximum conformance test limit for the given channel and CTL info
 */
static u_int16_t
ar5212GetMaxEdgePower(u_int16_t channel, RD_EDGES_POWER *pRdEdgesPower)
{
	/* temp array for holding edge channels */
	u_int16_t tempChannelList[NUM_EDGES];
	u_int16_t clo, chi, twiceMaxEdgePower;
	int i, numEdges;

	/* Get the edge power */
	for (i = 0; i < NUM_EDGES; i++) {
		if (pRdEdgesPower[i].rdEdge == 0)
			break;
		tempChannelList[i] = pRdEdgesPower[i].rdEdge;
	}
	numEdges = i;

	ar5212GetLowerUpperValues(channel, tempChannelList,
		numEdges, &clo, &chi);
	/* Get the index for the lower channel */
	for (i = 0; i < numEdges && clo != tempChannelList[i]; i++)
		;
	/* Is lower channel ever outside the rdEdge? */
	HALASSERT(i != numEdges);

	if ((clo == chi && clo == channel) || (pRdEdgesPower[i].flag)) {
		/* 
		 * If there's an exact channel match or an inband flag set
		 * on the lower channel use the given rdEdgePower 
		 */
		twiceMaxEdgePower = pRdEdgesPower[i].twice_rdEdgePower;
		HALASSERT(twiceMaxEdgePower > 0);
	} else
		twiceMaxEdgePower = MAX_RATE_POWER;
	return twiceMaxEdgePower;
}

/*
 * Returns interpolated or the scaled up interpolated value
 */
static u_int16_t
interpolate(u_int16_t target, u_int16_t srcLeft, u_int16_t srcRight,
	u_int16_t targetLeft, u_int16_t targetRight)
{
	u_int16_t rv;
	int16_t lRatio;

	/* to get an accurate ratio, always scale, if want to scale, then don't scale back down */
	if ((targetLeft * targetRight) == 0)
		return 0;

	if (srcRight != srcLeft) {
		/*
		 * Note the ratio always need to be scaled,
		 * since it will be a fraction.
		 */
		lRatio = (target - srcLeft) * EEP_SCALE / (srcRight - srcLeft);
		if (lRatio < 0) {
		    /* Return as Left target if value would be negative */
		    rv = targetLeft;
		} else if (lRatio > EEP_SCALE) {
		    /* Return as Right target if Ratio is greater than 100% (SCALE) */
		    rv = targetRight;
		} else {
			rv = (lRatio * targetRight + (EEP_SCALE - lRatio) *
					targetLeft) / EEP_SCALE;
		}
	} else {
		rv = targetLeft;
	}
	return rv;
}

/*
 * Return the four rates of target power for the given target power table 
 * channel, and number of channels
 */
static void
ar5212GetTargetPowers(struct ath_hal *ah, const struct ieee80211_channel *chan,
	TRGT_POWER_INFO *powInfo,
	u_int16_t numChannels, TRGT_POWER_INFO *pNewPower)
{
	/* temp array for holding target power channels */
	u_int16_t tempChannelList[NUM_TEST_FREQUENCIES];
	u_int16_t clo, chi, ixlo, ixhi;
	int i;

	/* Copy the target powers into the temp channel list */
	for (i = 0; i < numChannels; i++)
		tempChannelList[i] = powInfo[i].testChannel;

	ar5212GetLowerUpperValues(chan->ic_freq, tempChannelList,
		numChannels, &clo, &chi);

	/* Get the indices for the channel */
	ixlo = ixhi = 0;
	for (i = 0; i < numChannels; i++) {
		if (clo == tempChannelList[i]) {
			ixlo = i;
		}
		if (chi == tempChannelList[i]) {
			ixhi = i;
			break;
		}
	}

	/*
	 * Get the lower and upper channels, target powers,
	 * and interpolate between them.
	 */
	pNewPower->twicePwr6_24 = interpolate(chan->ic_freq, clo, chi,
		powInfo[ixlo].twicePwr6_24, powInfo[ixhi].twicePwr6_24);
	pNewPower->twicePwr36 = interpolate(chan->ic_freq, clo, chi,
		powInfo[ixlo].twicePwr36, powInfo[ixhi].twicePwr36);
	pNewPower->twicePwr48 = interpolate(chan->ic_freq, clo, chi,
		powInfo[ixlo].twicePwr48, powInfo[ixhi].twicePwr48);
	pNewPower->twicePwr54 = interpolate(chan->ic_freq, clo, chi,
		powInfo[ixlo].twicePwr54, powInfo[ixhi].twicePwr54);
}

static RD_EDGES_POWER*
findEdgePower(struct ath_hal *ah, u_int ctl)
{
	int i;

	for (i = 0; i < _numCtls; i++)
		if (_ctl[i] == ctl)
			return &_rdEdgesPower[i * NUM_EDGES];
	return AH_NULL;
}

/*
 * Sets the transmit power in the baseband for the given
 * operating channel and mode.
 */
static HAL_BOOL
setRateTable(struct ath_hal *ah, const struct ieee80211_channel *chan, 
		   int16_t tpcScaleReduction, int16_t powerLimit,
                   int16_t *pMinPower, int16_t *pMaxPower)
{
	u_int16_t ratesArray[16];
	u_int16_t *rpow = ratesArray;
	u_int16_t twiceMaxRDPower, twiceMaxEdgePower, twiceMaxEdgePowerCck;
	int8_t twiceAntennaGain, twiceAntennaReduction;
	TRGT_POWER_INFO targetPowerOfdm, targetPowerCck;
	RD_EDGES_POWER *rep;
	int16_t scaledPower;
	u_int8_t cfgCtl;

	twiceMaxRDPower = chan->ic_maxregpower * 2;
	*pMaxPower = -MAX_RATE_POWER;
	*pMinPower = MAX_RATE_POWER;

	/* Get conformance test limit maximum for this channel */
	cfgCtl = ath_hal_getctl(ah, chan);
	rep = findEdgePower(ah, cfgCtl);
	if (rep != AH_NULL)
		twiceMaxEdgePower = ar5212GetMaxEdgePower(chan->ic_freq, rep);
	else
		twiceMaxEdgePower = MAX_RATE_POWER;

	if (IEEE80211_IS_CHAN_G(chan)) {
		/* Check for a CCK CTL for 11G CCK powers */
		cfgCtl = (cfgCtl & 0xFC) | 0x01;
		rep = findEdgePower(ah, cfgCtl);
		if (rep != AH_NULL)
			twiceMaxEdgePowerCck = ar5212GetMaxEdgePower(chan->ic_freq, rep);
		else
			twiceMaxEdgePowerCck = MAX_RATE_POWER;
	} else {
		/* Set the 11B cck edge power to the one found before */
		twiceMaxEdgePowerCck = twiceMaxEdgePower;
	}

	/* Get Antenna Gain reduction */
	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		twiceAntennaGain = antennaGainMax[0];
	} else {
		twiceAntennaGain = antennaGainMax[1];
	}
	twiceAntennaReduction =
		ath_hal_getantennareduction(ah, chan, twiceAntennaGain);

	if (IEEE80211_IS_CHAN_OFDM(chan)) {
		/* Get final OFDM target powers */
		if (IEEE80211_IS_CHAN_G(chan)) { 
			/* TODO - add Turbo 2.4 to this mode check */
			ar5212GetTargetPowers(ah, chan, trgtPwr_11g,
				numTargetPwr_11g, &targetPowerOfdm);
		} else {
			ar5212GetTargetPowers(ah, chan, trgtPwr_11a,
				numTargetPwr_11a, &targetPowerOfdm);
		}

		/* Get Maximum OFDM power */
		/* Minimum of target and edge powers */
		scaledPower = AH_MIN(twiceMaxEdgePower,
				twiceMaxRDPower - twiceAntennaReduction);

		/*
		 * If turbo is set, reduce power to keep power
		 * consumption under 2 Watts.  Note that we always do
		 * this unless specially configured.  Then we limit
		 * power only for non-AP operation.
		 */
		if (IEEE80211_IS_CHAN_TURBO(chan)
#ifdef AH_ENABLE_AP_SUPPORT
		    && AH_PRIVATE(ah)->ah_opmode != HAL_M_HOSTAP
#endif
		) {
			/*
			 * If turbo is set, reduce power to keep power
			 * consumption under 2 Watts
			 */
			if (eeversion >= AR_EEPROM_VER3_1)
				scaledPower = AH_MIN(scaledPower,
					turbo2WMaxPower5);
			/*
			 * EEPROM version 4.0 added an additional
			 * constraint on 2.4GHz channels.
			 */
			if (eeversion >= AR_EEPROM_VER4_0 &&
			    IEEE80211_IS_CHAN_2GHZ(chan))
				scaledPower = AH_MIN(scaledPower,
					turbo2WMaxPower2);
		}
		/* Reduce power by max regulatory domain allowed restrictions */
		scaledPower -= (tpcScaleReduction * 2);
		scaledPower = (scaledPower < 0) ? 0 : scaledPower;
		scaledPower = AH_MIN(scaledPower, powerLimit);

		scaledPower = AH_MIN(scaledPower, targetPowerOfdm.twicePwr6_24);

		/* Set OFDM rates 9, 12, 18, 24, 36, 48, 54, XR */
		rpow[0] = rpow[1] = rpow[2] = rpow[3] = rpow[4] = scaledPower;
		rpow[5] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr36);
		rpow[6] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr48);
		rpow[7] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr54);

#ifdef notyet
		if (eeversion >= AR_EEPROM_VER4_0) {
			/* Setup XR target power from EEPROM */
			rpow[15] = AH_MIN(scaledPower, IS_CHAN_2GHZ(chan) ?
				xrTargetPower2 : xrTargetPower5);
		} else {
			/* XR uses 6mb power */
			rpow[15] = rpow[0];
		}
#else
		rpow[15] = rpow[0];
#endif

		*pMinPower = rpow[7];
		*pMaxPower = rpow[0];

#if 0
		ahp->ah_ofdmTxPower = rpow[0];
#endif

		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: MaxRD: %d TurboMax: %d MaxCTL: %d "
		    "TPC_Reduction %d\n", __func__,
		    twiceMaxRDPower, turbo2WMaxPower5,
		    twiceMaxEdgePower, tpcScaleReduction * 2);
	}

	if (IEEE80211_IS_CHAN_CCK(chan)) {
		/* Get final CCK target powers */
		ar5212GetTargetPowers(ah, chan, trgtPwr_11b,
			numTargetPwr_11b, &targetPowerCck);

		/* Reduce power by max regulatory domain allowed restrictions */
		scaledPower = AH_MIN(twiceMaxEdgePowerCck,
			twiceMaxRDPower - twiceAntennaReduction);

		scaledPower -= (tpcScaleReduction * 2);
		scaledPower = (scaledPower < 0) ? 0 : scaledPower;
		scaledPower = AH_MIN(scaledPower, powerLimit);

		rpow[8] = (scaledPower < 1) ? 1 : scaledPower;

		/* Set CCK rates 2L, 2S, 5.5L, 5.5S, 11L, 11S */
		rpow[8]  = AH_MIN(scaledPower, targetPowerCck.twicePwr6_24);
		rpow[9]  = AH_MIN(scaledPower, targetPowerCck.twicePwr36);
		rpow[10] = rpow[9];
		rpow[11] = AH_MIN(scaledPower, targetPowerCck.twicePwr48);
		rpow[12] = rpow[11];
		rpow[13] = AH_MIN(scaledPower, targetPowerCck.twicePwr54);
		rpow[14] = rpow[13];

		/* Set min/max power based off OFDM values or initialization */
		if (rpow[13] < *pMinPower)
		    *pMinPower = rpow[13];
		if (rpow[9] > *pMaxPower)
		    *pMaxPower = rpow[9];

	}
#if 0
	ahp->ah_tx6PowerInHalfDbm = *pMaxPower;
#endif
	return AH_TRUE;
}

void*
ath_hal_malloc(size_t size)
{
	return calloc(1, size);
}

void
ath_hal_free(void* p)
{
	return free(p);
}

void
ath_hal_vprintf(struct ath_hal *ah, const char* fmt, va_list ap)
{
	vprintf(fmt, ap);
}

void
ath_hal_printf(struct ath_hal *ah, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ath_hal_vprintf(ah, fmt, ap);
	va_end(ap);
}

void
DO_HALDEBUG(struct ath_hal *ah, u_int mask, const char* fmt, ...)
{
	__va_list ap;
	va_start(ap, fmt);
	ath_hal_vprintf(ah, fmt, ap);
	va_end(ap);
}
