/*-
 * SPDX-License-Identifier: ISC
 *
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
#ifndef _AH_EEPROM_V14_H_
#define _AH_EEPROM_V14_H_

#include "ah_eeprom.h"

/* reg_off = 4 * (eep_off) */
#define AR5416_EEPROM_S			2
#define AR5416_EEPROM_OFFSET		0x2000
#define AR5416_EEPROM_START_ADDR	0x503f1200
#define AR5416_EEPROM_MAX		0xae0 /* Ignore for the moment used only on the flash implementations */
#define AR5416_EEPROM_MAGIC		0xa55a
#define AR5416_EEPROM_MAGIC_OFFSET	0x0

#define owl_get_ntxchains(_txchainmask) \
    (((_txchainmask >> 2) & 1) + ((_txchainmask >> 1) & 1) + (_txchainmask & 1))

#ifdef __LINUX_ARM_ARCH__ /* AP71 */
#define owl_eep_start_loc		0
#else
#define owl_eep_start_loc		256
#endif

/* End temp defines */

#define AR5416_EEP_NO_BACK_VER       	0x1
#define AR5416_EEP_VER               	0xE
#define AR5416_EEP_VER_MINOR_MASK	0xFFF
// Adds modal params txFrameToPaOn, txFrametoDataStart, ht40PowerInc
#define AR5416_EEP_MINOR_VER_2		0x2
// Adds modal params bswAtten, bswMargin, swSettle and base OpFlags for HT20/40 Disable
#define AR5416_EEP_MINOR_VER_3		0x3
#define AR5416_EEP_MINOR_VER_7		0x7
#define AR5416_EEP_MINOR_VER_9		0x9
#define AR5416_EEP_MINOR_VER_10		0xa
#define AR5416_EEP_MINOR_VER_16		0x10
#define AR5416_EEP_MINOR_VER_17		0x11
#define AR5416_EEP_MINOR_VER_19		0x13
#define AR5416_EEP_MINOR_VER_20		0x14
#define AR5416_EEP_MINOR_VER_21		0x15
#define	AR5416_EEP_MINOR_VER_22		0x16

// 16-bit offset location start of calibration struct
#define AR5416_EEP_START_LOC         	256
#define AR5416_NUM_5G_CAL_PIERS      	8
#define AR5416_NUM_2G_CAL_PIERS      	4
#define AR5416_NUM_5G_20_TARGET_POWERS  8
#define AR5416_NUM_5G_40_TARGET_POWERS  8
#define AR5416_NUM_2G_CCK_TARGET_POWERS 3
#define AR5416_NUM_2G_20_TARGET_POWERS  4
#define AR5416_NUM_2G_40_TARGET_POWERS  4
#define AR5416_NUM_CTLS              	24
#define AR5416_NUM_BAND_EDGES        	8
#define AR5416_NUM_PD_GAINS          	4
#define AR5416_PD_GAINS_IN_MASK      	4
#define AR5416_PD_GAIN_ICEPTS        	5
#define AR5416_EEPROM_MODAL_SPURS    	5
#define AR5416_MAX_RATE_POWER        	63
#define AR5416_NUM_PDADC_VALUES      	128
#define AR5416_NUM_RATES             	16
#define AR5416_BCHAN_UNUSED          	0xFF
#define AR5416_MAX_PWR_RANGE_IN_HALF_DB 64
#define AR5416_EEPMISC_BIG_ENDIAN    	0x01
#define FREQ2FBIN(x,y) 			((y) ? ((x) - 2300) : (((x) - 4800) / 5))
#define AR5416_MAX_CHAINS            	3
#define	AR5416_PWR_TABLE_OFFSET_DB	-5
#define AR5416_ANT_16S               	25

#define AR5416_NUM_ANT_CHAIN_FIELDS     7
#define AR5416_NUM_ANT_COMMON_FIELDS    4
#define AR5416_SIZE_ANT_CHAIN_FIELD     3
#define AR5416_SIZE_ANT_COMMON_FIELD    4
#define AR5416_ANT_CHAIN_MASK           0x7
#define AR5416_ANT_COMMON_MASK          0xf
#define AR5416_CHAIN_0_IDX              0
#define AR5416_CHAIN_1_IDX              1
#define AR5416_CHAIN_2_IDX              2

#define	AR5416_OPFLAGS_11A		0x01
#define	AR5416_OPFLAGS_11G		0x02
#define	AR5416_OPFLAGS_N_5G_HT40	0x04	/* If set, disable 5G HT40 */
#define	AR5416_OPFLAGS_N_2G_HT40	0x08
#define	AR5416_OPFLAGS_N_5G_HT20	0x10
#define	AR5416_OPFLAGS_N_2G_HT20	0x20

/* RF silent fields in EEPROM */
#define	EEP_RFSILENT_ENABLED		0x0001	/* enabled/disabled */
#define	EEP_RFSILENT_ENABLED_S		0
#define	EEP_RFSILENT_POLARITY		0x0002	/* polarity */
#define	EEP_RFSILENT_POLARITY_S		1
#define	EEP_RFSILENT_GPIO_SEL		0x001c	/* gpio PIN */
#define	EEP_RFSILENT_GPIO_SEL_S		2

/* Rx gain type values */
#define	AR5416_EEP_RXGAIN_23dB_BACKOFF	0
#define	AR5416_EEP_RXGAIN_13dB_BACKOFF	1
#define	AR5416_EEP_RXGAIN_ORIG		2

/* Tx gain type values */
#define	AR5416_EEP_TXGAIN_ORIG		0
#define	AR5416_EEP_TXGAIN_HIGH_POWER	1

typedef struct spurChanStruct {
	uint16_t	spurChan;
	uint8_t		spurRangeLow;
	uint8_t		spurRangeHigh;
} __packed SPUR_CHAN;

typedef struct CalTargetPowerLegacy {
	uint8_t		bChannel;
	uint8_t		tPow2x[4];
} __packed CAL_TARGET_POWER_LEG;

typedef struct CalTargetPowerHt {
	uint8_t		bChannel;
	uint8_t		tPow2x[8];
} __packed CAL_TARGET_POWER_HT;

typedef struct CalCtlEdges {
	uint8_t		bChannel;
	uint8_t		tPowerFlag;	/* [0..5] tPower [6..7] flag */
#define	CAL_CTL_EDGES_POWER	0x3f
#define	CAL_CTL_EDGES_POWER_S	0
#define	CAL_CTL_EDGES_FLAG	0xc0
#define	CAL_CTL_EDGES_FLAG_S	6
} __packed CAL_CTL_EDGES;

/*
 * These are the secondary regulatory domain flags
 * for regDmn[1].
 */
#define	AR5416_REGDMN_EN_FCC_MID	0x01	/* 5.47 - 5.7GHz operation */
#define	AR5416_REGDMN_EN_JAP_MID	0x02	/* 5.47 - 5.7GHz operation */
#define	AR5416_REGDMN_EN_FCC_DFS_HT40	0x04	/* FCC HT40 + DFS operation */
#define	AR5416_REGDMN_EN_JAP_HT40	0x08	/* JP HT40 operation */
#define	AR5416_REGDMN_EN_JAP_DFS_HT40	0x10	/* JP HT40 + DFS operation */

/*
 * NB: The format in EEPROM has words 0 and 2 swapped (i.e. version
 * and length are swapped).  We reverse their position after reading
 * the data into host memory so the version field is at the same
 * offset as in previous EEPROM layouts.  This makes utilities that
 * inspect the EEPROM contents work without looking at the PCI device
 * id which may or may not be reliable.
 */
typedef struct BaseEepHeader {
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
	uint8_t		pwdclkind;
	uint8_t		fastClk5g;
	uint8_t		divChain;  
	uint8_t		rxGainType;
	uint8_t		dacHiPwrMode_5G;/* use the DAC high power mode (MB91) */
	uint8_t		openLoopPwrCntl;/* 1: use open loop power control,
					   0: use closed loop power control */
	uint8_t		dacLpMode;
	uint8_t		txGainType;	/* high power tx gain table support */
	uint8_t		rcChainMask;	/* "1" if the card is an HB93 1x2 */
	uint8_t		desiredScaleCCK;
	uint8_t		pwr_table_offset;
	uint8_t		frac_n_5g;	/*
					 * bit 0: indicates that fracN synth
					 * mode applies to all 5G channels
					 */
	uint8_t		futureBase[21];
} __packed BASE_EEP_HEADER; // 64 B

typedef struct ModalEepHeader {
	uint32_t	antCtrlChain[AR5416_MAX_CHAINS];	// 12
	uint32_t	antCtrlCommon;				// 4
	int8_t		antennaGainCh[AR5416_MAX_CHAINS];	// 3
	uint8_t		switchSettling;				// 1
	uint8_t		txRxAttenCh[AR5416_MAX_CHAINS];		// 3
	uint8_t		rxTxMarginCh[AR5416_MAX_CHAINS];	// 3
	uint8_t		adcDesiredSize;				// 1
	int8_t		pgaDesiredSize;				// 1
	uint8_t		xlnaGainCh[AR5416_MAX_CHAINS];		// 3
	uint8_t		txEndToXpaOff;				// 1
	uint8_t		txEndToRxOn;				// 1
	uint8_t		txFrameToXpaOn;				// 1
	uint8_t		thresh62;				// 1
	uint8_t		noiseFloorThreshCh[AR5416_MAX_CHAINS];	// 3
	uint8_t		xpdGain;				// 1
	uint8_t		xpd;					// 1
	int8_t		iqCalICh[AR5416_MAX_CHAINS];		// 1
	int8_t		iqCalQCh[AR5416_MAX_CHAINS];		// 1
	uint8_t		pdGainOverlap;				// 1
	uint8_t		ob;					// 1
	uint8_t		db;					// 1
	uint8_t		xpaBiasLvl;				// 1
	uint8_t		pwrDecreaseFor2Chain;			// 1
	uint8_t		pwrDecreaseFor3Chain;			// 1 -> 48 B
	uint8_t		txFrameToDataStart;			// 1
	uint8_t		txFrameToPaOn;				// 1
	uint8_t		ht40PowerIncForPdadc;			// 1
	uint8_t		bswAtten[AR5416_MAX_CHAINS];		// 3
	uint8_t		bswMargin[AR5416_MAX_CHAINS];		// 3
	uint8_t		swSettleHt40;				// 1	
	uint8_t		xatten2Db[AR5416_MAX_CHAINS];    	// 3 -> New for AR9280 (0xa20c/b20c 11:6)
	uint8_t		xatten2Margin[AR5416_MAX_CHAINS];	// 3 -> New for AR9280 (0xa20c/b20c 21:17)
	uint8_t		ob_ch1;				// 1 -> ob and db become chain specific from AR9280
	uint8_t		db_ch1;				// 1
	uint8_t		flagBits;			// 1
#define	AR5416_EEP_FLAG_USEANT1		0x80	/* +1 configured antenna */
#define	AR5416_EEP_FLAG_FORCEXPAON	0x40	/* force XPA bit for 5G */
#define	AR5416_EEP_FLAG_LOCALBIAS	0x20	/* enable local bias */
#define	AR5416_EEP_FLAG_FEMBANDSELECT	0x10	/* FEM band select used */
#define	AR5416_EEP_FLAG_XLNABUFIN	0x08
#define	AR5416_EEP_FLAG_XLNAISEL1	0x04
#define	AR5416_EEP_FLAG_XLNAISEL2	0x02
#define	AR5416_EEP_FLAG_XLNABUFMODE	0x01
	uint8_t		miscBits;			// [0..1]: bb_tx_dac_scale_cck
	uint16_t	xpaBiasLvlFreq[3];		// 3
	uint8_t		futureModal[6];			// 6

	SPUR_CHAN spurChans[AR5416_EEPROM_MODAL_SPURS];	// 20 B
} __packed MODAL_EEP_HEADER;				// == 100 B    

typedef struct calDataPerFreqOpLoop {
	uint8_t		pwrPdg[2][5]; /* power measurement */
	uint8_t		vpdPdg[2][5]; /* pdadc voltage at power measurement */
	uint8_t		pcdac[2][5];  /* pcdac used for power measurement */
	uint8_t		empty[2][5];  /* future use */
} __packed CAL_DATA_PER_FREQ_OP_LOOP;

typedef struct CalCtlData {
	CAL_CTL_EDGES		ctlEdges[AR5416_MAX_CHAINS][AR5416_NUM_BAND_EDGES];
} __packed CAL_CTL_DATA;

typedef struct calDataPerFreq {
	uint8_t		pwrPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
	uint8_t		vpdPdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
} __packed CAL_DATA_PER_FREQ;

struct ar5416eeprom {
	BASE_EEP_HEADER		baseEepHeader;         // 64 B
	uint8_t			custData[64];          // 64 B
	MODAL_EEP_HEADER	modalHeader[2];        // 200 B
	uint8_t			calFreqPier5G[AR5416_NUM_5G_CAL_PIERS];
	uint8_t			calFreqPier2G[AR5416_NUM_2G_CAL_PIERS];
	CAL_DATA_PER_FREQ	calPierData5G[AR5416_MAX_CHAINS][AR5416_NUM_5G_CAL_PIERS];
	CAL_DATA_PER_FREQ	calPierData2G[AR5416_MAX_CHAINS][AR5416_NUM_2G_CAL_PIERS];
	CAL_TARGET_POWER_LEG	calTargetPower5G[AR5416_NUM_5G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower5GHT20[AR5416_NUM_5G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower5GHT40[AR5416_NUM_5G_40_TARGET_POWERS];
	CAL_TARGET_POWER_LEG	calTargetPowerCck[AR5416_NUM_2G_CCK_TARGET_POWERS];
	CAL_TARGET_POWER_LEG	calTargetPower2G[AR5416_NUM_2G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower2GHT20[AR5416_NUM_2G_20_TARGET_POWERS];
	CAL_TARGET_POWER_HT	calTargetPower2GHT40[AR5416_NUM_2G_40_TARGET_POWERS];
	uint8_t			ctlIndex[AR5416_NUM_CTLS];
	CAL_CTL_DATA		ctlData[AR5416_NUM_CTLS];
	uint8_t			padding;			
} __packed;

typedef struct {
	struct ar5416eeprom ee_base;
#define NUM_EDGES	 8
	uint16_t	ee_numCtls;
	RD_EDGES_POWER	ee_rdEdgesPower[NUM_EDGES*AR5416_NUM_CTLS];
	/* XXX these are dynamically calculated for use by shared code */
	int8_t		ee_antennaGainMax[2];
} HAL_EEPROM_v14;
#endif /* _AH_EEPROM_V14_H_ */
