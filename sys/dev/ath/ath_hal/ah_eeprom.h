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
#ifndef _ATH_AH_EEPROM_H_
#define _ATH_AH_EEPROM_H_

#define	AR_EEPROM_VER1		0x1000	/* Version 1.0; 5210 only */
/*
 * Version 3 EEPROMs are all 16K.
 * 3.1 adds turbo limit, antenna gain, 16 CTL's, 11g info,
 *	and 2.4Ghz ob/db for B & G
 * 3.2 has more accurate pcdac intercepts and analog chip
 *	calibration.
 * 3.3 adds ctl in-band limit, 32 ctl's, and frequency
 *	expansion
 * 3.4 adds xr power, gainI, and 2.4 turbo params
 */
#define	AR_EEPROM_VER3		0x3000	/* Version 3.0; start of 16k EEPROM */
#define	AR_EEPROM_VER3_1	0x3001	/* Version 3.1 */
#define	AR_EEPROM_VER3_2	0x3002	/* Version 3.2 */
#define	AR_EEPROM_VER3_3	0x3003	/* Version 3.3 */
#define	AR_EEPROM_VER3_4	0x3004	/* Version 3.4 */
#define	AR_EEPROM_VER4		0x4000	/* Version 4.x */
#define	AR_EEPROM_VER4_0	0x4000	/* Version 4.0 */
#define	AR_EEPROM_VER4_1	0x4001	/* Version 4.0 */
#define	AR_EEPROM_VER4_2	0x4002	/* Version 4.0 */
#define	AR_EEPROM_VER4_3	0x4003	/* Version 4.0 */
#define	AR_EEPROM_VER4_6	0x4006	/* Version 4.0 */
#define	AR_EEPROM_VER4_7	0x3007	/* Version 4.7 */
#define	AR_EEPROM_VER4_9	0x4009	/* EEPROM EAR futureproofing */
#define	AR_EEPROM_VER5		0x5000	/* Version 5.x */
#define	AR_EEPROM_VER5_0	0x5000	/* Adds new 2413 cal powers and added params */
#define	AR_EEPROM_VER5_1	0x5001	/* Adds capability values */
#define	AR_EEPROM_VER5_3	0x5003	/* Adds spur mitigation table */
#define	AR_EEPROM_VER5_4	0x5004
/*
 * Version 14 EEPROMs came in with AR5416.
 * 14.2 adds txFrameToPaOn, txFrameToDataStart, ht40PowerInc
 * 14.3 adds bswAtten, bswMargin, swSettle, and base OpFlags for HT20/40
 */
#define	AR_EEPROM_VER14		0xE000	/* Version 14.x */
#define	AR_EEPROM_VER14_1	0xE001	/* Adds 11n support */
#define	AR_EEPROM_VER14_2	0xE002
#define	AR_EEPROM_VER14_3	0xE003
#define	AR_EEPROM_VER14_7	0xE007
#define	AR_EEPROM_VER14_9	0xE009
#define	AR_EEPROM_VER14_16	0xE010
#define	AR_EEPROM_VER14_17	0xE011
#define	AR_EEPROM_VER14_19	0xE013

enum {
	AR_EEP_RFKILL,		/* use ath_hal_eepromGetFlag */
	AR_EEP_AMODE,		/* use ath_hal_eepromGetFlag */
	AR_EEP_BMODE,		/* use ath_hal_eepromGetFlag */
	AR_EEP_GMODE,		/* use ath_hal_eepromGetFlag */
	AR_EEP_TURBO5DISABLE,	/* use ath_hal_eepromGetFlag */
	AR_EEP_TURBO2DISABLE,	/* use ath_hal_eepromGetFlag */
	AR_EEP_ISTALON,		/* use ath_hal_eepromGetFlag */
	AR_EEP_32KHZCRYSTAL,	/* use ath_hal_eepromGetFlag */
	AR_EEP_MACADDR,		/* uint8_t* */
	AR_EEP_COMPRESS,	/* use ath_hal_eepromGetFlag */
	AR_EEP_FASTFRAME,	/* use ath_hal_eepromGetFlag */
	AR_EEP_AES,		/* use ath_hal_eepromGetFlag */
	AR_EEP_BURST,		/* use ath_hal_eepromGetFlag */
	AR_EEP_MAXQCU,		/* uint16_t* */
	AR_EEP_KCENTRIES,	/* uint16_t* */
	AR_EEP_NFTHRESH_5,	/* int16_t* */
	AR_EEP_NFTHRESH_2,	/* int16_t* */
	AR_EEP_REGDMN_0,	/* uint16_t* */
	AR_EEP_REGDMN_1,	/* uint16_t* */
	AR_EEP_OPCAP,		/* uint16_t* */
	AR_EEP_OPMODE,		/* uint16_t* */
	AR_EEP_RFSILENT,	/* uint16_t* */
	AR_EEP_OB_5,		/* uint8_t* */
	AR_EEP_DB_5,		/* uint8_t* */
	AR_EEP_OB_2,		/* uint8_t* */
	AR_EEP_DB_2,		/* uint8_t* */
	AR_EEP_TXMASK,		/* uint8_t* */
	AR_EEP_RXMASK,		/* uint8_t* */
	AR_EEP_RXGAIN_TYPE,	/* uint8_t* */
	AR_EEP_TXGAIN_TYPE,	/* uint8_t* */
	AR_EEP_DAC_HPWR_5G,	/* uint8_t* */
	AR_EEP_OL_PWRCTRL,	/* use ath_hal_eepromGetFlag */
	AR_EEP_FSTCLK_5G,	/* use ath_hal_eepromGetFlag */
	AR_EEP_ANTGAINMAX_5,	/* int8_t* */
	AR_EEP_ANTGAINMAX_2,	/* int8_t* */
	AR_EEP_WRITEPROTECT,	/* use ath_hal_eepromGetFlag */
	AR_EEP_PWR_TABLE_OFFSET,/* int8_t* */
	AR_EEP_PWDCLKIND,	/* uint8_t* */
	AR_EEP_TEMPSENSE_SLOPE,	/* int8_t* */
	AR_EEP_TEMPSENSE_SLOPE_PAL_ON,	/* int8_t* */
	AR_EEP_FRAC_N_5G,	/* uint8_t* */

	/* New fields for AR9300 and later */
	AR_EEP_DRIVE_STRENGTH,
	AR_EEP_PAPRD_ENABLED,
};

typedef struct {
	uint16_t	rdEdge;
	uint16_t	twice_rdEdgePower;
	HAL_BOOL	flag;
} RD_EDGES_POWER;

/* XXX should probably be version-dependent */
#define	SD_NO_CTL		0xf0
#define	NO_CTL			0xff
#define	CTL_MODE_M		0x0f
#define	CTL_11A			0
#define	CTL_11B			1
#define	CTL_11G			2
#define	CTL_TURBO		3
#define	CTL_108G		4
#define	CTL_2GHT20		5
#define	CTL_5GHT20		6
#define	CTL_2GHT40		7
#define	CTL_5GHT40		8

/* XXX must match what FCC/MKK/ETSI are defined as in ah_regdomain.h */
#define	HAL_REG_DMN_MASK	0xf0
#define	HAL_REGDMN_FCC		0x10
#define	HAL_REGDMN_MKK		0x40
#define	HAL_REGDMN_ETSI		0x30

#define	is_reg_dmn_fcc(reg_dmn)	\
	   (((reg_dmn & HAL_REG_DMN_MASK) == HAL_REGDMN_FCC) ? 1 : 0)
#define	is_reg_dmn_etsi(reg_dmn)	\
	    (((reg_dmn & HAL_REG_DMN_MASK) == HAL_REGDMN_ETSI) ? 1 : 0)
#define	is_reg_dmn_mkk(reg_dmn)	\
	    (((reg_dmn & HAL_REG_DMN_MASK) == HAL_REGDMN_MKK) ? 1 : 0)

#define	AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND	0x0040
#define	AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN	0x0080
#define	AR_EEPROM_EEREGCAP_EN_KK_U2		0x0100
#define	AR_EEPROM_EEREGCAP_EN_KK_MIDBAND	0x0200
#define	AR_EEPROM_EEREGCAP_EN_KK_U1_ODD		0x0400
#define	AR_EEPROM_EEREGCAP_EN_KK_NEW_11A	0x0800

/* regulatory capabilities prior to eeprom version 4.0 */
#define	AR_EEPROM_EEREGCAP_EN_KK_U1_ODD_PRE4_0  0x4000
#define	AR_EEPROM_EEREGCAP_EN_KK_NEW_11A_PRE4_0 0x8000

#define	AR_NO_SPUR		0x8000

/* XXX exposed to chip code */
#define	MAX_RATE_POWER	63

HAL_STATUS	ath_hal_v1EepromAttach(struct ath_hal *ah);
HAL_STATUS	ath_hal_legacyEepromAttach(struct ath_hal *ah);
HAL_STATUS	ath_hal_v14EepromAttach(struct ath_hal *ah);
HAL_STATUS	ath_hal_v4kEepromAttach(struct ath_hal *ah);
HAL_STATUS	ath_hal_9287EepromAttach(struct ath_hal *ah);
#endif /* _ATH_AH_EEPROM_H_ */
