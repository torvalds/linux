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
#ifndef _ATH_AH_EEPROM_V3_H_
#define _ATH_AH_EEPROM_V3_H_

#include "ah_eeprom.h"

/* EEPROM defines for Version 2 & 3 AR5211 chips */
#define	AR_EEPROM_RFSILENT	0x0f	/* RF Silent/Clock Run Enable */
#define	AR_EEPROM_MAC(i)	(0x1d+(i)) /* MAC address word */
#define	AR_EEPROM_MAGIC		0x3d	/* magic number */
#define	AR_EEPROM_PROTECT	0x3f	/* EEPROM protect bits */
#define	AR_EEPROM_PROTECT_PCIE	0x01	/* EEPROM protect bits for Condor/Swan*/
#define	AR_EEPROM_REG_DOMAIN	0xbf	/* current regulatory domain */
#define	AR_EEPROM_ATHEROS_BASE	0xc0	/* Base of Atheros-specific data */
#define	AR_EEPROM_ATHEROS(i)	(AR_EEPROM_ATHEROS_BASE+(i))
#define	AR_EEPROM_ATHEROS_MAX	(0x400-AR_EEPROM_ATHEROS_BASE)
#define	AR_EEPROM_VERSION	AR_EEPROM_ATHEROS(1)

/* FLASH(EEPROM) Defines for AR531X chips */
#define AR_EEPROM_SIZE_LOWER    0x1b    /* size info -- lower */
#define AR_EEPROM_SIZE_UPPER    0x1c    /* size info -- upper */
#define AR_EEPROM_SIZE_UPPER_MASK 0xfff0
#define AR_EEPROM_SIZE_UPPER_SHIFT 4
#define	AR_EEPROM_SIZE_ENDLOC_SHIFT 12
#define AR_EEPROM_ATHEROS_MAX_LOC 0x400
#define AR_EEPROM_ATHEROS_MAX_OFF (AR_EEPROM_ATHEROS_MAX_LOC-AR_EEPROM_ATHEROS_BASE)

/* regulatory capabilities offsets */
#define AR_EEPROM_REG_CAPABILITIES_OFFSET		0xCA
#define AR_EEPROM_REG_CAPABILITIES_OFFSET_PRE4_0	0xCF /* prior to 4.0 */

/* regulatory capabilities */
#define AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND	0x0040
#define AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN	0x0080
#define AR_EEPROM_EEREGCAP_EN_KK_U2		0x0100
#define AR_EEPROM_EEREGCAP_EN_KK_MIDBAND	0x0200
#define AR_EEPROM_EEREGCAP_EN_KK_U1_ODD		0x0400
#define AR_EEPROM_EEREGCAP_EN_KK_NEW_11A	0x0800

/* regulatory capabilities prior to eeprom version 4.0 */
#define AR_EEPROM_EEREGCAP_EN_KK_U1_ODD_PRE4_0	0x4000
#define AR_EEPROM_EEREGCAP_EN_KK_NEW_11A_PRE4_0	0x8000

/*
 * AR2413 (includes AR5413)
 */
#define AR_EEPROM_SERIAL_NUM_OFFSET     0xB0    /* EEPROM serial number */
#define AR_EEPROM_SERIAL_NUM_SIZE       12      /* EEPROM serial number size */
#define AR_EEPROM_CAPABILITIES_OFFSET   0xC9    /* EEPROM Location of capabilities */

#define AR_EEPROM_EEPCAP_COMPRESS_DIS	0x0001
#define AR_EEPROM_EEPCAP_AES_DIS	0x0002
#define AR_EEPROM_EEPCAP_FASTFRAME_DIS	0x0004
#define AR_EEPROM_EEPCAP_BURST_DIS	0x0008
#define AR_EEPROM_EEPCAP_MAXQCU		0x01F0
#define AR_EEPROM_EEPCAP_MAXQCU_S	4
#define AR_EEPROM_EEPCAP_HEAVY_CLIP_EN	0x0200
#define AR_EEPROM_EEPCAP_KC_ENTRIES	0xF000
#define AR_EEPROM_EEPCAP_KC_ENTRIES_S	12

/* XXX used to index various EEPROM-derived data structures */
enum {
	headerInfo11A	= 0,
	headerInfo11B	= 1,
	headerInfo11G	= 2,
};

#define GROUPS_OFFSET3_2	0x100	/* groups offset for ver3.2 and earlier */
#define GROUPS_OFFSET3_3	0x150	/* groups offset for ver3.3 */
/* relative offset of GROUPi to GROUPS_OFFSET */
#define GROUP1_OFFSET		0x0
#define GROUP2_OFFSET		0x5
#define GROUP3_OFFSET		0x37
#define GROUP4_OFFSET		0x46
#define GROUP5_OFFSET		0x55
#define GROUP6_OFFSET		0x65
#define GROUP7_OFFSET		0x69
#define GROUP8_OFFSET		0x6f

/* RF silent fields in EEPROM */
#define AR_EEPROM_RFSILENT_GPIO_SEL	0x001c
#define AR_EEPROM_RFSILENT_GPIO_SEL_S	2
#define AR_EEPROM_RFSILENT_POLARITY	0x0002
#define AR_EEPROM_RFSILENT_POLARITY_S	1

/* Protect Bits RP is read protect, WP is write protect */
#define	AR_EEPROM_PROTECT_RP_0_31	0x0001
#define	AR_EEPROM_PROTECT_WP_0_31	0x0002
#define	AR_EEPROM_PROTECT_RP_32_63	0x0004
#define	AR_EEPROM_PROTECT_WP_32_63	0x0008
#define	AR_EEPROM_PROTECT_RP_64_127	0x0010
#define	AR_EEPROM_PROTECT_WP_64_127	0x0020
#define	AR_EEPROM_PROTECT_RP_128_191	0x0040
#define	AR_EEPROM_PROTECT_WP_128_191	0x0080
#define	AR_EEPROM_PROTECT_RP_192_207	0x0100
#define	AR_EEPROM_PROTECT_WP_192_207	0x0200
#define	AR_EEPROM_PROTECT_RP_208_223	0x0400
#define	AR_EEPROM_PROTECT_WP_208_223	0x0800
#define	AR_EEPROM_PROTECT_RP_224_239	0x1000
#define	AR_EEPROM_PROTECT_WP_224_239	0x2000
#define	AR_EEPROM_PROTECT_RP_240_255	0x4000
#define	AR_EEPROM_PROTECT_WP_240_255	0x8000

#define	AR_EEPROM_MODAL_SPURS		5
#define	AR_SPUR_5413_1			1640	/* Freq 2464 */
#define	AR_SPUR_5413_2			1200	/* Freq 2420 */

/*
 * EEPROM fixed point conversion scale factors.
 * NB: if you change one be sure to keep the other in sync.
 */
#define EEP_SCALE	100		/* conversion scale to avoid fp arith */
#define EEP_DELTA	10		/* SCALE/10, to avoid arith divide */

#define PWR_MIN		0
#define PWR_MAX		3150		/* 31.5 * SCALE */
#define PWR_STEP	50		/* 0.5 * SCALE */
/* Keep 2 above defines together */

#define NUM_11A_EEPROM_CHANNELS	10
#define NUM_2_4_EEPROM_CHANNELS	3
#define NUM_PCDAC_VALUES	11
#define NUM_TEST_FREQUENCIES	8
#define NUM_EDGES	 	8
#define NUM_INTERCEPTS	 	11
#define FREQ_MASK		0x7f
#define FREQ_MASK_3_3		0xff	/* expanded in version 3.3 */
#define PCDAC_MASK		0x3f
#define POWER_MASK		0x3f
#define	NON_EDGE_FLAG_MASK	0x40
#define CHANNEL_POWER_INFO	8
#define OBDB_UNSET		0xffff
#define	CHANNEL_UNUSED		0xff
#define	SCALE_OC_DELTA(_x)	(((_x) * 2) / 10)

/* Used during pcdac table construction */
#define PCDAC_START	1
#define PCDAC_STOP	63
#define PCDAC_STEP	1
#define PWR_TABLE_SIZE	64
#define	MAX_RATE_POWER	63

/* Used during power/rate table construction */
#define NUM_CTLS	16
#define	NUM_CTLS_3_3	32		/* expanded in version 3.3 */
#define	NUM_CTLS_MAX	NUM_CTLS_3_3

typedef struct fullPcdacStruct {
	uint16_t	channelValue;
	uint16_t	pcdacMin;
	uint16_t	pcdacMax;
	uint16_t	numPcdacValues;
	uint16_t	PcdacValues[64];
	/* power is 32bit since in dest it is scaled */
	int16_t		PwrValues[64];
} FULL_PCDAC_STRUCT;

typedef struct dataPerChannel {
	uint16_t	channelValue;
	uint16_t	pcdacMin;
	uint16_t	pcdacMax;
	uint16_t	numPcdacValues;
	uint16_t	PcdacValues[NUM_PCDAC_VALUES];
	/* NB: power is 32bit since in dest it is scaled */
	int16_t		PwrValues[NUM_PCDAC_VALUES];
} DATA_PER_CHANNEL;

/* points to the appropriate pcdac structs in the above struct based on mode */
typedef struct pcdacsEeprom {
	const uint16_t	*pChannelList;
	uint16_t	numChannels;
	const DATA_PER_CHANNEL *pDataPerChannel;
} PCDACS_EEPROM;

typedef struct trgtPowerInfo {
	uint16_t	twicePwr54;
	uint16_t	twicePwr48;
	uint16_t	twicePwr36;
	uint16_t	twicePwr6_24;
	uint16_t	testChannel;
} TRGT_POWER_INFO;

typedef struct trgtPowerAllModes {
	uint16_t	numTargetPwr_11a;
	TRGT_POWER_INFO trgtPwr_11a[NUM_TEST_FREQUENCIES];
	uint16_t	numTargetPwr_11g;
	TRGT_POWER_INFO trgtPwr_11g[3];
	uint16_t	numTargetPwr_11b;
	TRGT_POWER_INFO trgtPwr_11b[2];
} TRGT_POWER_ALL_MODES;

typedef struct cornerCalInfo {
	uint16_t	gSel;
	uint16_t	pd84;
	uint16_t	pd90;
	uint16_t	clip;
} CORNER_CAL_INFO;

/*
 * EEPROM version 4 definitions
 */
#define NUM_XPD_PER_CHANNEL      4
#define NUM_POINTS_XPD0          4
#define NUM_POINTS_XPD3          3
#define IDEAL_10dB_INTERCEPT_2G  35
#define IDEAL_10dB_INTERCEPT_5G  55

#define	TENX_OFDM_CCK_DELTA_INIT	15		/* power 1.5 dbm */
#define	TENX_CH14_FILTER_CCK_DELTA_INIT	15		/* power 1.5 dbm */
#define	CCK_OFDM_GAIN_DELTA		15

#define NUM_TARGET_POWER_LOCATIONS_11B  4
#define NUM_TARGET_POWER_LOCATIONS_11G  6


typedef struct {
	uint16_t	xpd_gain;
	uint16_t	numPcdacs;
	uint16_t	pcdac[NUM_POINTS_XPD0];
	int16_t		pwr_t4[NUM_POINTS_XPD0];	/* or gainF */
} EXPN_DATA_PER_XPD_5112;

typedef struct {
	uint16_t	channelValue;
	int16_t		maxPower_t4;                
	EXPN_DATA_PER_XPD_5112	pDataPerXPD[NUM_XPD_PER_CHANNEL];
} EXPN_DATA_PER_CHANNEL_5112;

typedef struct {
	uint16_t	*pChannels;
	uint16_t	numChannels;
	uint16_t 	xpdMask;	/* mask of permitted xpd_gains */
	EXPN_DATA_PER_CHANNEL_5112 *pDataPerChannel;
} EEPROM_POWER_EXPN_5112;

typedef struct {
	uint16_t	channelValue;
	uint16_t	pcd1_xg0;
	int16_t		pwr1_xg0;
	uint16_t	pcd2_delta_xg0;
	int16_t		pwr2_xg0;
	uint16_t	pcd3_delta_xg0;
	int16_t		pwr3_xg0;
	uint16_t	pcd4_delta_xg0;
	int16_t		pwr4_xg0;
	int16_t		maxPower_t4;
	int16_t		pwr1_xg3;	/* pcdac = 20 */
	int16_t		pwr2_xg3;	/* pcdac = 35 */
	int16_t		pwr3_xg3;	/* pcdac = 63 */
	/* XXX - Should be pwr1_xg2, etc to agree with documentation */
} EEPROM_DATA_PER_CHANNEL_5112;

typedef struct {
	uint16_t	pChannels[NUM_11A_EEPROM_CHANNELS];
	uint16_t	numChannels;
	uint16_t	xpdMask;	/* mask of permitted xpd_gains */
	EEPROM_DATA_PER_CHANNEL_5112 pDataPerChannel[NUM_11A_EEPROM_CHANNELS];
} EEPROM_POWER_5112;

/*
 * EEPROM version 5 definitions (Griffin, et. al.).
 */
#define NUM_2_4_EEPROM_CHANNELS_2413	4
#define NUM_11A_EEPROM_CHANNELS_2413    10
#define PWR_TABLE_SIZE_2413		128

/* Used during pdadc construction */
#define	MAX_NUM_PDGAINS_PER_CHANNEL	4
#define	NUM_PDGAINS_PER_CHANNEL		2
#define	NUM_POINTS_LAST_PDGAIN		5
#define	NUM_POINTS_OTHER_PDGAINS	4
#define	XPD_GAIN1_GEN5			3
#define	XPD_GAIN2_GEN5			1
#define	MAX_PWR_RANGE_IN_HALF_DB	64
#define	PD_GAIN_BOUNDARY_STRETCH_IN_HALF_DB	4

typedef struct {
	uint16_t	pd_gain;
	uint16_t	numVpd;
	uint16_t	Vpd[NUM_POINTS_LAST_PDGAIN];
	int16_t		pwr_t4[NUM_POINTS_LAST_PDGAIN];	/* or gainF */
} RAW_DATA_PER_PDGAIN_2413;

typedef struct {
	uint16_t	channelValue;
	int16_t		maxPower_t4;    
	uint16_t	numPdGains;	/* # Pd Gains per channel */
	RAW_DATA_PER_PDGAIN_2413 pDataPerPDGain[MAX_NUM_PDGAINS_PER_CHANNEL];
} RAW_DATA_PER_CHANNEL_2413;

/* XXX: assumes NUM_11A_EEPROM_CHANNELS_2413 >= NUM_2_4_EEPROM_CHANNELS_2413 ??? */
typedef struct {
	uint16_t	pChannels[NUM_11A_EEPROM_CHANNELS_2413];
	uint16_t	numChannels;
	uint16_t	xpd_mask;	/* mask of permitted xpd_gains */
	RAW_DATA_PER_CHANNEL_2413 pDataPerChannel[NUM_11A_EEPROM_CHANNELS_2413];
} RAW_DATA_STRUCT_2413;

typedef struct {
	uint16_t	channelValue;
	uint16_t	numPdGains;
	uint16_t	Vpd_I[MAX_NUM_PDGAINS_PER_CHANNEL];
	int16_t		pwr_I[MAX_NUM_PDGAINS_PER_CHANNEL];
	uint16_t	Vpd_delta[NUM_POINTS_LAST_PDGAIN]
				[MAX_NUM_PDGAINS_PER_CHANNEL];
	int16_t		pwr_delta_t2[NUM_POINTS_LAST_PDGAIN]
				[MAX_NUM_PDGAINS_PER_CHANNEL];
	int16_t		maxPower_t4;
} EEPROM_DATA_PER_CHANNEL_2413;

typedef struct {
	uint16_t	pChannels[NUM_11A_EEPROM_CHANNELS_2413];
	uint16_t	numChannels;
	uint16_t	xpd_mask;	/* mask of permitted xpd_gains */
	EEPROM_DATA_PER_CHANNEL_2413 pDataPerChannel[NUM_11A_EEPROM_CHANNELS_2413];
} EEPROM_DATA_STRUCT_2413;

/*
 * Information retrieved from EEPROM.
 */
typedef struct {
	uint16_t	ee_version;		/* Version field */
	uint16_t	ee_protect;		/* EEPROM protect field */
	uint16_t	ee_regdomain;		/* Regulatory domain */

	/* General Device Parameters */
	uint16_t	ee_turbo5Disable;
	uint16_t	ee_turbo2Disable;
	uint16_t	ee_rfKill;
	uint16_t	ee_deviceType;
	uint16_t	ee_turbo2WMaxPower5;
	uint16_t	ee_turbo2WMaxPower2;
	uint16_t	ee_xrTargetPower5;
	uint16_t	ee_xrTargetPower2;
	uint16_t	ee_Amode;
	uint16_t	ee_regCap;
	uint16_t	ee_Bmode;
	uint16_t	ee_Gmode;
	int8_t		ee_antennaGainMax[2];
	uint16_t	ee_xtnd5GSupport;
	uint8_t		ee_cckOfdmPwrDelta;
	uint8_t		ee_exist32kHzCrystal;
	uint16_t	ee_targetPowersStart;
	uint16_t	ee_fixedBias5;
	uint16_t	ee_fixedBias2;
	uint16_t	ee_cckOfdmGainDelta;
	uint16_t	ee_scaledCh14FilterCckDelta;
	uint16_t	ee_eepMap;
	uint16_t	ee_earStart;

	/* 5 GHz / 2.4 GHz CKK / 2.4 GHz OFDM common parameters */
	uint16_t	ee_switchSettling[3];
	uint16_t	ee_txrxAtten[3];
	uint16_t	ee_txEndToXLNAOn[3];
	uint16_t	ee_thresh62[3];
	uint16_t	ee_txEndToXPAOff[3];
	uint16_t	ee_txFrameToXPAOn[3];
	int8_t		ee_adcDesiredSize[3];	 /* 8-bit signed value */
	int8_t		ee_pgaDesiredSize[3];	 /* 8-bit signed value */
	int16_t		ee_noiseFloorThresh[3];
	uint16_t	ee_xlnaGain[3];
	uint16_t	ee_xgain[3];
	uint16_t	ee_xpd[3];
	uint16_t	ee_antennaControl[11][3];
	uint16_t	ee_falseDetectBackoff[3];
	uint16_t	ee_gainI[3];
	uint16_t	ee_rxtxMargin[3];

	/* new parameters added for the AR2413 */
	HAL_BOOL	ee_disableXr5;
	HAL_BOOL	ee_disableXr2;
	uint16_t	ee_eepMap2PowerCalStart;
	uint16_t	ee_capField;

	uint16_t	ee_switchSettlingTurbo[2];
	uint16_t	ee_txrxAttenTurbo[2];
	int8_t		ee_adcDesiredSizeTurbo[2];
	int8_t		ee_pgaDesiredSizeTurbo[2];
	uint16_t	ee_rxtxMarginTurbo[2];

	/* 5 GHz parameters */
	uint16_t	ee_ob1;
	uint16_t	ee_db1;
	uint16_t	ee_ob2;
	uint16_t	ee_db2;
	uint16_t	ee_ob3;
	uint16_t	ee_db3;
	uint16_t	ee_ob4;
	uint16_t	ee_db4;

	/* 2.4 GHz parameters */
	uint16_t	ee_obFor24;
	uint16_t	ee_dbFor24;
	uint16_t	ee_obFor24g;
	uint16_t	ee_dbFor24g;
	uint16_t	ee_ob2GHz[2];
	uint16_t	ee_db2GHz[2];
	uint16_t	ee_numCtls;
	uint16_t	ee_ctl[NUM_CTLS_MAX];
	uint16_t	ee_iqCalI[2];
	uint16_t	ee_iqCalQ[2];
	uint16_t	ee_calPier11g[NUM_2_4_EEPROM_CHANNELS];
	uint16_t	ee_calPier11b[NUM_2_4_EEPROM_CHANNELS];

	/* corner calibration information */
	CORNER_CAL_INFO	ee_cornerCal;

	uint16_t	ee_opCap;

	/* 11a info */
	uint16_t	ee_channels11a[NUM_11A_EEPROM_CHANNELS];
	uint16_t	ee_numChannels11a;
	DATA_PER_CHANNEL ee_dataPerChannel11a[NUM_11A_EEPROM_CHANNELS];

	uint16_t	ee_numChannels2_4;
	uint16_t	ee_channels11g[NUM_2_4_EEPROM_CHANNELS];
	uint16_t	ee_channels11b[NUM_2_4_EEPROM_CHANNELS];
	uint16_t	ee_spurChans[AR_EEPROM_MODAL_SPURS][2];

	/* 11g info */
	DATA_PER_CHANNEL ee_dataPerChannel11g[NUM_2_4_EEPROM_CHANNELS];

	/* 11b info */
	DATA_PER_CHANNEL ee_dataPerChannel11b[NUM_2_4_EEPROM_CHANNELS];

	TRGT_POWER_ALL_MODES ee_tpow;

	RD_EDGES_POWER	ee_rdEdgesPower[NUM_EDGES*NUM_CTLS_MAX];

	union {
		EEPROM_POWER_EXPN_5112  eu_modePowerArray5112[3];
		RAW_DATA_STRUCT_2413	eu_rawDataset2413[3];
	} ee_u;
} HAL_EEPROM;

/* write-around defines */
#define	ee_numTargetPwr_11a	ee_tpow.numTargetPwr_11a
#define	ee_trgtPwr_11a		ee_tpow.trgtPwr_11a
#define	ee_numTargetPwr_11g	ee_tpow.numTargetPwr_11g
#define	ee_trgtPwr_11g		ee_tpow.trgtPwr_11g
#define	ee_numTargetPwr_11b	ee_tpow.numTargetPwr_11b
#define	ee_trgtPwr_11b		ee_tpow.trgtPwr_11b
#define	ee_modePowerArray5112	ee_u.eu_modePowerArray5112
#define	ee_rawDataset2413	ee_u.eu_rawDataset2413
#endif /* _ATH_AH_EEPROM_V3_H_ */
