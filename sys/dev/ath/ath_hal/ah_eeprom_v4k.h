/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2009 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Atheros Communications, Inc.
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
#ifndef _AH_EEPROM_V4K_H_
#define _AH_EEPROM_V4K_H_

#include "ah_eeprom.h"
#include "ah_eeprom_v14.h"

#if	_BYTE_ORDER == _BIG_ENDIAN
#define	__BIG_ENDIAN_BITFIELD
#endif

#define	AR9285_RDEXT_DEFAULT	0x1F

#define	AR5416_4K_EEP_PD_GAIN_BOUNDARY_DEFAULT	58

#undef owl_eep_start_loc
#ifdef __LINUX_ARM_ARCH__ /* AP71 */
#define owl_eep_start_loc		0
#else
#define owl_eep_start_loc		64
#endif

// 16-bit offset location start of calibration struct
#define AR5416_4K_EEP_START_LOC         64
#define AR5416_4K_NUM_2G_CAL_PIERS     	3
#define AR5416_4K_NUM_2G_CCK_TARGET_POWERS 3
#define AR5416_4K_NUM_2G_20_TARGET_POWERS  3
#define AR5416_4K_NUM_2G_40_TARGET_POWERS  3
#define AR5416_4K_NUM_CTLS              12
#define AR5416_4K_NUM_BAND_EDGES       	4
#define AR5416_4K_NUM_PD_GAINS         	2
#define AR5416_4K_MAX_CHAINS           	1

/*
 * NB: The format in EEPROM has words 0 and 2 swapped (i.e. version
 * and length are swapped).  We reverse their position after reading
 * the data into host memory so the version field is at the same
 * offset as in previous EEPROM layouts.  This makes utilities that
 * inspect the EEPROM contents work without looking at the PCI device
 * id which may or may not be reliable.
 */
typedef struct BaseEepHeader4k {
	uint16_t	version;	/* NB: length in EEPROM */
	uint16_t	checksum;
	uint16_t	length;		/* NB: version in EEPROM */
	uint8_t		opCapFlags;
	uint8_t		eepMisc;
	uint16_t	regDmn[2];
	uint8_t		macAddr[6];
	uint8_t		rxMask;
	uint8_t		txMask;
	uint16_t	rfSilent;
	uint16_t	blueToothOptions;
	uint16_t	deviceCap;
	uint32_t	binBuildNumber;
	uint8_t		deviceType;
	uint8_t		txGainType;	/* high power tx gain table support */
} __packed BASE_EEP4K_HEADER; // 32 B

typedef struct ModalEepHeader4k {
	uint32_t	antCtrlChain[AR5416_4K_MAX_CHAINS];	// 4
	uint32_t	antCtrlCommon;				// 4
	int8_t		antennaGainCh[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		switchSettling;				// 1
	uint8_t		txRxAttenCh[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		rxTxMarginCh[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		adcDesiredSize;				// 1
	int8_t		pgaDesiredSize;				// 1
	uint8_t		xlnaGainCh[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		txEndToXpaOff;				// 1
	uint8_t		txEndToRxOn;				// 1
	uint8_t		txFrameToXpaOn;				// 1
	uint8_t		thresh62;				// 1
	uint8_t		noiseFloorThreshCh[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		xpdGain;				// 1
	uint8_t		xpd;					// 1
	int8_t		iqCalICh[AR5416_4K_MAX_CHAINS];		// 1
	int8_t		iqCalQCh[AR5416_4K_MAX_CHAINS];		// 1

	uint8_t		pdGainOverlap;				// 1

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t		ob_1:4, ob_0:4;				// 1
	uint8_t		db1_1:4, db1_0:4;			// 1
#else
	uint8_t		ob_0:4, ob_1:4;
	uint8_t		db1_0:4, db1_1:4;
#endif

	uint8_t		xpaBiasLvl;				// 1
	uint8_t		txFrameToDataStart;			// 1
	uint8_t		txFrameToPaOn;				// 1
	uint8_t		ht40PowerIncForPdadc;			// 1
	uint8_t		bswAtten[AR5416_4K_MAX_CHAINS];		// 1
	uint8_t		bswMargin[AR5416_4K_MAX_CHAINS];	// 1
	uint8_t		swSettleHt40;				// 1	
	uint8_t		xatten2Db[AR5416_4K_MAX_CHAINS];    	// 1
	uint8_t		xatten2Margin[AR5416_4K_MAX_CHAINS];	// 1

#ifdef __BIG_ENDIAN_BITFIELD
        uint8_t		db2_1:4, db2_0:4;			// 1
#else
	uint8_t		db2_0:4, db2_1:4;			// 1
#endif

	uint8_t		version;				// 1

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t		ob_3:4, ob_2:4;				// 1
	uint8_t		antdiv_ctl1:4, ob_4:4;			// 1
	uint8_t		db1_3:4, db1_2:4;			// 1
	uint8_t		antdiv_ctl2:4, db1_4:4;			// 1
	uint8_t		db2_2:4, db2_3:4;			// 1
	uint8_t		reserved:4, db2_4:4;			// 1
#else
	uint8_t		ob_2:4, ob_3:4;
	uint8_t		ob_4:4, antdiv_ctl1:4;
	uint8_t		db1_2:4, db1_3:4;
	uint8_t		db1_4:4, antdiv_ctl2:4;
	uint8_t		db2_2:4, db2_3:4;
	uint8_t		db2_4:4, reserved:4;
#endif
	uint8_t		tx_diversity;
	uint8_t		flc_pwr_thresh;
	uint8_t		bb_scale_smrt_antenna;
#define	EEP_4K_BB_DESIRED_SCALE_MASK	0x1f
	uint8_t		futureModal[1];

	SPUR_CHAN spurChans[AR5416_EEPROM_MODAL_SPURS];	// 20 B
} __packed MODAL_EEP4K_HEADER;				// == 68 B

typedef struct CalCtlData4k {
	CAL_CTL_EDGES		ctlEdges[AR5416_4K_MAX_CHAINS][AR5416_4K_NUM_BAND_EDGES];
} __packed CAL_CTL_DATA_4K;

typedef struct calDataPerFreq4k {
	uint8_t		pwrPdg[AR5416_4K_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
	uint8_t		vpdPdg[AR5416_4K_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
} __packed CAL_DATA_PER_FREQ_4K;

struct ar5416eeprom_4k {
	BASE_EEP4K_HEADER	baseEepHeader;         // 32 B
	uint8_t			custData[20];          // 20 B
	MODAL_EEP4K_HEADER	modalHeader;           // 68 B
	uint8_t			calFreqPier2G[AR5416_4K_NUM_2G_CAL_PIERS];
	CAL_DATA_PER_FREQ_4K	calPierData2G[AR5416_4K_MAX_CHAINS][AR5416_4K_NUM_2G_CAL_PIERS];
	CAL_TARGET_POWER_LEG	calTargetPowerCck[AR5416_4K_NUM_2G_CCK_TARGET_POWERS];
	CAL_TARGET_POWER_LEG	calTargetPower2G[AR5416_4K_NUM_2G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower2GHT20[AR5416_4K_NUM_2G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower2GHT40[AR5416_4K_NUM_2G_40_TARGET_POWERS];
	uint8_t			ctlIndex[AR5416_4K_NUM_CTLS];
	CAL_CTL_DATA_4K		ctlData[AR5416_4K_NUM_CTLS];
	uint8_t			padding;			
} __packed;

typedef struct {
	struct ar5416eeprom_4k ee_base;
#define NUM_EDGES	 8
	uint16_t	ee_numCtls;
	RD_EDGES_POWER	ee_rdEdgesPower[NUM_EDGES*AR5416_4K_NUM_CTLS];
	/* XXX these are dynamically calculated for use by shared code */
	int8_t		ee_antennaGainMax;
} HAL_EEPROM_v4k;
#endif /* _AH_EEPROM_V4K_H_ */
