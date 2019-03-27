/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
#ifndef _ATH_AR5212_H_
#define _ATH_AR5212_H_

#include "ah_eeprom.h"

#define	AR5212_MAGIC	0x19541014

/* DCU Transmit Filter macros */
#define CALC_MMR(dcu, idx) \
	( (4 * dcu) + (idx < 32 ? 0 : (idx < 64 ? 1 : (idx < 96 ? 2 : 3))) )
#define TXBLK_FROM_MMR(mmr) \
	(AR_D_TXBLK_BASE + ((mmr & 0x1f) << 6) + ((mmr & 0x20) >> 3))
#define CALC_TXBLK_ADDR(dcu, idx)	(TXBLK_FROM_MMR(CALC_MMR(dcu, idx)))
#define CALC_TXBLK_VALUE(idx)		(1 << (idx & 0x1f))

/* MAC register values */

#define INIT_INTERRUPT_MASK \
	( AR_IMR_TXERR  | AR_IMR_TXOK | AR_IMR_RXORN | \
	  AR_IMR_RXERR  | AR_IMR_RXOK | AR_IMR_TXURN | \
	  AR_IMR_HIUERR )
#define INIT_BEACON_CONTROL \
	((INIT_RESET_TSF << 24)  | (INIT_BEACON_EN << 23) | \
	  (INIT_TIM_OFFSET << 16) | INIT_BEACON_PERIOD)

#define INIT_CONFIG_STATUS	0x00000000
#define INIT_RSSI_THR		0x00000781	/* Missed beacon counter initialized to 0x7 (max is 0xff) */
#define INIT_IQCAL_LOG_COUNT_MAX	0xF
#define INIT_BCON_CNTRL_REG	0x00000000

#define INIT_USEC		40
#define HALF_RATE_USEC		19 /* ((40 / 2) - 1 ) */
#define QUARTER_RATE_USEC	9  /* ((40 / 4) - 1 ) */

#define RX_NON_FULL_RATE_LATENCY	63
#define TX_HALF_RATE_LATENCY		108
#define TX_QUARTER_RATE_LATENCY		216

#define IFS_SLOT_FULL_RATE	0x168 /* 9 us half, 40 MHz core clock (9*40) */
#define IFS_SLOT_HALF_RATE	0x104 /* 13 us half, 20 MHz core clock (13*20) */
#define IFS_SLOT_QUARTER_RATE	0xD2 /* 21 us quarter, 10 MHz core clock (21*10) */
#define IFS_EIFS_FULL_RATE	0xE60 /* (74 + (2 * 9)) * 40MHz core clock */
#define IFS_EIFS_HALF_RATE	0xDAC /* (149 + (2 * 13)) * 20MHz core clock */
#define IFS_EIFS_QUARTER_RATE	0xD48 /* (298 + (2 * 21)) * 10MHz core clock */

#define ACK_CTS_TIMEOUT_11A	0x3E8 /* ACK timeout in 11a core clocks */

/* Tx frame start to tx data start delay */
#define TX_FRAME_D_START_HALF_RATE 	0xc
#define TX_FRAME_D_START_QUARTER_RATE 	0xd

/*
 * Various fifo fill before Tx start, in 64-byte units
 * i.e. put the frame in the air while still DMAing
 */
#define MIN_TX_FIFO_THRESHOLD	0x1
#define MAX_TX_FIFO_THRESHOLD	((IEEE80211_MAX_LEN / 64) + 1)
#define INIT_TX_FIFO_THRESHOLD	MIN_TX_FIFO_THRESHOLD

#define	HAL_DECOMP_MASK_SIZE	128	/* 1 byte per key */

/*
 * Gain support.
 */
#define	NUM_CORNER_FIX_BITS		4
#define	NUM_CORNER_FIX_BITS_5112	7
#define	DYN_ADJ_UP_MARGIN		15
#define	DYN_ADJ_LO_MARGIN		20
#define	PHY_PROBE_CCK_CORRECTION	5
#define	CCK_OFDM_GAIN_DELTA		15

enum GAIN_PARAMS {
	GP_TXCLIP,
	GP_PD90,
	GP_PD84,
	GP_GSEL,
};

enum GAIN_PARAMS_5112 {
	GP_MIXGAIN_OVR,
	GP_PWD_138,
	GP_PWD_137,
	GP_PWD_136,
	GP_PWD_132,
	GP_PWD_131,
	GP_PWD_130,
};

typedef struct _gainOptStep {
	int16_t	paramVal[NUM_CORNER_FIX_BITS_5112];
	int32_t	stepGain;
	int8_t	stepName[16];
} GAIN_OPTIMIZATION_STEP;

typedef struct {
	uint32_t	numStepsInLadder;
	uint32_t	defaultStepNum;
	GAIN_OPTIMIZATION_STEP optStep[10];
} GAIN_OPTIMIZATION_LADDER;

typedef struct {
	uint32_t	currStepNum;
	uint32_t	currGain;
	uint32_t	targetGain;
	uint32_t	loTrig;
	uint32_t	hiTrig;
	uint32_t	active;
	const GAIN_OPTIMIZATION_STEP *currStep;
} GAIN_VALUES;

/* RF HAL structures */
typedef struct RfHalFuncs {
	void	  *priv;		/* private state */

	void	  (*rfDetach)(struct ath_hal *ah);
	void	  (*writeRegs)(struct ath_hal *,
		      u_int modeIndex, u_int freqIndex, int regWrites);
	uint32_t *(*getRfBank)(struct ath_hal *ah, int bank);
	HAL_BOOL  (*setChannel)(struct ath_hal *,
		      const struct ieee80211_channel *);
	HAL_BOOL  (*setRfRegs)(struct ath_hal *,
		      const struct ieee80211_channel *, uint16_t modesIndex,
		      uint16_t *rfXpdGain);
	HAL_BOOL  (*setPowerTable)(struct ath_hal *ah,
		      int16_t *minPower, int16_t *maxPower,
		      const struct ieee80211_channel *, uint16_t *rfXpdGain);
	HAL_BOOL  (*getChannelMaxMinPower)(struct ath_hal *ah,
		      const struct ieee80211_channel *,
		      int16_t *maxPow, int16_t *minPow);
	int16_t	  (*getNfAdjust)(struct ath_hal *, const HAL_CHANNEL_INTERNAL*);
} RF_HAL_FUNCS;

struct ar5212AniParams {
	int		maxNoiseImmunityLevel;	/* [0..4] */
	int		totalSizeDesired[5];
	int		coarseHigh[5];
	int		coarseLow[5];
	int		firpwr[5];

	int		maxSpurImmunityLevel;	/* [0..7] */
	int		cycPwrThr1[8];

	int		maxFirstepLevel;	/* [0..2] */
	int		firstep[3];

	uint32_t	ofdmTrigHigh;
	uint32_t	ofdmTrigLow;
	uint32_t	cckTrigHigh;
	uint32_t	cckTrigLow;
	int32_t		rssiThrLow;
	uint32_t	rssiThrHigh;

	int		period;			/* update listen period */

	/* NB: intentionally ordered so data exported to user space is first */
	uint32_t	ofdmPhyErrBase;	/* Base value for ofdm err counter */
	uint32_t	cckPhyErrBase;	/* Base value for cck err counters */
};

/*
 * Per-channel ANI state private to the driver.
 */
struct ar5212AniState {
	uint8_t		noiseImmunityLevel;
	uint8_t		spurImmunityLevel;
	uint8_t		firstepLevel;
	uint8_t		ofdmWeakSigDetectOff;
	uint8_t		cckWeakSigThreshold;
	uint32_t	listenTime;

	/* NB: intentionally ordered so data exported to user space is first */
	uint32_t	txFrameCount;	/* Last txFrameCount */
	uint32_t	rxFrameCount;	/* Last rx Frame count */
	uint32_t	cycleCount;	/* Last cycleCount
					   (to detect wrap-around) */
	uint32_t	ofdmPhyErrCount;/* OFDM err count since last reset */
	uint32_t	cckPhyErrCount;	/* CCK err count since last reset */

	const struct ar5212AniParams *params;
};

#define	HAL_ANI_ENA		0x00000001	/* ANI operation enabled */
#define	HAL_RSSI_ANI_ENA	0x00000002	/* rssi-based processing ena'd*/

#if 0
struct ar5212Stats {
	uint32_t	ast_ani_niup;	/* ANI increased noise immunity */
	uint32_t	ast_ani_nidown;	/* ANI decreased noise immunity */
	uint32_t	ast_ani_spurup;	/* ANI increased spur immunity */
	uint32_t	ast_ani_spurdown;/* ANI descreased spur immunity */
	uint32_t	ast_ani_ofdmon;	/* ANI OFDM weak signal detect on */
	uint32_t	ast_ani_ofdmoff;/* ANI OFDM weak signal detect off */
	uint32_t	ast_ani_cckhigh;/* ANI CCK weak signal threshold high */
	uint32_t	ast_ani_ccklow;	/* ANI CCK weak signal threshold low */
	uint32_t	ast_ani_stepup;	/* ANI increased first step level */
	uint32_t	ast_ani_stepdown;/* ANI decreased first step level */
	uint32_t	ast_ani_ofdmerrs;/* ANI cumulative ofdm phy err count */
	uint32_t	ast_ani_cckerrs;/* ANI cumulative cck phy err count */
	uint32_t	ast_ani_reset;	/* ANI parameters zero'd for non-STA */
	uint32_t	ast_ani_lzero;	/* ANI listen time forced to zero */
	uint32_t	ast_ani_lneg;	/* ANI listen time calculated < 0 */
	HAL_MIB_STATS	ast_mibstats;	/* MIB counter stats */
	HAL_NODE_STATS	ast_nodestats;	/* Latest rssi stats from driver */
};
#endif

/*
 * NF Cal history buffer
 */
#define	AR5212_CCA_MAX_GOOD_VALUE	-95
#define	AR5212_CCA_MAX_HIGH_VALUE	-62
#define	AR5212_CCA_MIN_BAD_VALUE	-125

#define	AR512_NF_CAL_HIST_MAX		5

struct ar5212NfCalHist {
	int16_t		nfCalBuffer[AR512_NF_CAL_HIST_MAX];
	int16_t		privNF;
	uint8_t		currIndex;
	uint8_t		first_run;
	uint8_t		invalidNFcount;
};

struct ath_hal_5212 {
	struct ath_hal_private	ah_priv;	/* base class */

	/*
	 * Per-chip common Initialization data.
	 * NB: RF backends have their own ini data.
	 */
	HAL_INI_ARRAY	ah_ini_modes;
	HAL_INI_ARRAY	ah_ini_common;

	GAIN_VALUES	ah_gainValues;

	uint8_t		ah_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		ah_bssid[IEEE80211_ADDR_LEN];
	uint8_t		ah_bssidmask[IEEE80211_ADDR_LEN];
	uint16_t	ah_assocId;

	/*
	 * Runtime state.
	 */
	uint32_t	ah_maskReg;		/* copy of AR_IMR */
	HAL_ANI_STATS	ah_stats;		/* various statistics */
	RF_HAL_FUNCS	*ah_rfHal;
	uint32_t	ah_txDescMask;		/* mask for TXDESC */
	uint32_t	ah_txOkInterruptMask;
	uint32_t	ah_txErrInterruptMask;
	uint32_t	ah_txDescInterruptMask;
	uint32_t	ah_txEolInterruptMask;
	uint32_t	ah_txUrnInterruptMask;
	HAL_TX_QUEUE_INFO ah_txq[HAL_NUM_TX_QUEUES];
	uint32_t	ah_intrTxqs;		/* tx q interrupt state */
						/* decomp mask array */
	uint8_t		ah_decompMask[HAL_DECOMP_MASK_SIZE];
	HAL_ANT_SETTING ah_antControl;		/* antenna setting */
	HAL_BOOL	ah_diversity;		/* fast diversity setting */
	enum {
		IQ_CAL_INACTIVE,
		IQ_CAL_RUNNING,
		IQ_CAL_DONE
	} ah_bIQCalibration;			/* IQ calibrate state */
	HAL_RFGAIN	ah_rfgainState;		/* RF gain calibrartion state */
	uint32_t	ah_tx6PowerInHalfDbm;	/* power output for 6Mb tx */
	uint32_t	ah_staId1Defaults;	/* STA_ID1 default settings */
	uint32_t	ah_miscMode;		/* MISC_MODE settings */
	uint32_t	ah_rssiThr;		/* RSSI_THR settings */
	HAL_BOOL	ah_cwCalRequire;	/* for ap51 */
	HAL_BOOL	ah_tpcEnabled;		/* per-packet tpc enabled */
	HAL_BOOL	ah_phyPowerOn;		/* PHY power state */
	HAL_BOOL	ah_isHb63;		/* cached HB63 check */
	uint32_t	ah_macTPC;		/* tpc register */
	uint32_t	ah_beaconInterval;	/* XXX */
	enum {
		AUTO_32KHZ,		/* use it if 32kHz crystal present */
		USE_32KHZ,		/* do it regardless */
		DONT_USE_32KHZ,		/* don't use it regardless */
	} ah_enable32kHzClock;			/* whether to sleep at 32kHz */
	uint32_t	ah_ofdmTxPower;
	int16_t		ah_txPowerIndexOffset;
	/*
	 * Noise floor cal histogram support.
	 */
	struct ar5212NfCalHist ah_nfCalHist;

	u_int		ah_slottime;		/* user-specified slot time */
	u_int		ah_acktimeout;		/* user-specified ack timeout */
	u_int		ah_ctstimeout;		/* user-specified cts timeout */
	u_int		ah_sifstime;		/* user-specified sifs time */
	/*
	 * RF Silent handling; setup according to the EEPROM.
	 */
	uint32_t	ah_gpioSelect;		/* GPIO pin to use */
	uint32_t	ah_polarity;		/* polarity to disable RF */
	uint32_t	ah_gpioBit;		/* after init, prev value */
	/*
	 * ANI support.
	 */
	uint32_t	ah_procPhyErr;		/* Process Phy errs */
	HAL_BOOL	ah_hasHwPhyCounters;	/* Hardware has phy counters */
	struct ar5212AniParams ah_aniParams24;	/* 2.4GHz parameters */
	struct ar5212AniParams ah_aniParams5;	/* 5GHz parameters */
	struct ar5212AniState	*ah_curani;	/* cached last reference */
	struct ar5212AniState	ah_ani[AH_MAXCHAN]; /* per-channel state */

	/* AR5416 uses some of the AR5212 ANI code; these are the ANI methods */
	HAL_BOOL	(*ah_aniControl) (struct ath_hal *, HAL_ANI_CMD cmd, int param);

	/*
	 * Transmit power state.  Note these are maintained
	 * here so they can be retrieved by diagnostic tools.
	 */
	uint16_t	*ah_pcdacTable;
	u_int		ah_pcdacTableSize;
	uint16_t	ah_ratesArray[37];

	uint8_t		ah_txTrigLev;		/* current Tx trigger level */
	uint8_t		ah_maxTxTrigLev;	/* max tx trigger level */

	/*
	 * Channel Tx, Rx, Rx Clear State
	 */
	uint32_t	ah_cycleCount;
	uint32_t	ah_ctlBusy;
	uint32_t	ah_rxBusy;
	uint32_t	ah_txBusy;
	uint32_t	ah_rx_chainmask;
	uint32_t	ah_tx_chainmask;

	/* Used to return ANI statistics to the diagnostic API */
	HAL_ANI_STATS	ext_ani_stats;
};
#define	AH5212(_ah)	((struct ath_hal_5212 *)(_ah))

/*
 * IS_XXXX macros test the MAC version
 * IS_RADXXX macros test the radio/RF version (matching both 2G-only and 2/5G)
 *
 * Some single chip radios have equivalent radio/RF (e.g. 5112)
 * for those use IS_RADXXX_ANY macros.
 */
#define IS_2317(ah) \
	((AH_PRIVATE(ah)->ah_devid == AR5212_AR2317_REV1) || \
	 (AH_PRIVATE(ah)->ah_devid == AR5212_AR2317_REV2))
#define	IS_2316(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_2415)
#define	IS_2413(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_2413 || IS_2316(ah))
#define IS_5424(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_5424 || \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_5413 && \
	  AH_PRIVATE(ah)->ah_macRev <= AR_SREV_D2PLUS_MS))
#define IS_5413(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_5413 || IS_5424(ah))
#define IS_2425(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_2425)
#define IS_2417(ah) \
	((AH_PRIVATE(ah)->ah_macVersion) == AR_SREV_2417)
#define IS_HB63(ah)		(AH5212(ah)->ah_isHb63 == AH_TRUE)

#define	AH_RADIO_MAJOR(ah) \
	(AH_PRIVATE(ah)->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR)
#define	AH_RADIO_MINOR(ah) \
	(AH_PRIVATE(ah)->ah_analog5GhzRev & AR_RADIO_SREV_MINOR)
#define	IS_RAD5111(ah) \
	(AH_RADIO_MAJOR(ah) == AR_RAD5111_SREV_MAJOR || \
	 AH_RADIO_MAJOR(ah) == AR_RAD2111_SREV_MAJOR)
#define	IS_RAD5112(ah) \
	(AH_RADIO_MAJOR(ah) == AR_RAD5112_SREV_MAJOR || \
	 AH_RADIO_MAJOR(ah) == AR_RAD2112_SREV_MAJOR)
/* NB: does not include 5413 as Atheros' IS_5112 macro does */
#define	IS_RAD5112_ANY(ah) \
	(AR_RAD5112_SREV_MAJOR <= AH_RADIO_MAJOR(ah) && \
	 AH_RADIO_MAJOR(ah) <= AR_RAD2413_SREV_MAJOR)
#define	IS_RAD5112_REV1(ah) \
	(IS_RAD5112(ah) && \
	 AH_RADIO_MINOR(ah) < (AR_RAD5112_SREV_2_0 & AR_RADIO_SREV_MINOR))
#define IS_RADX112_REV2(ah) \
	(AH_PRIVATE(ah)->ah_analog5GhzRev == AR_RAD5112_SREV_2_0 || \
	 AH_PRIVATE(ah)->ah_analog5GhzRev == AR_RAD2112_SREV_2_0 || \
	 AH_PRIVATE(ah)->ah_analog5GhzRev == AR_RAD2112_SREV_2_1 || \
	 AH_PRIVATE(ah)->ah_analog5GhzRev == AR_RAD5112_SREV_2_1)

#define	ar5212RfDetach(ah) do {				\
	if (AH5212(ah)->ah_rfHal != AH_NULL)		\
		AH5212(ah)->ah_rfHal->rfDetach(ah);	\
} while (0)
#define	ar5212GetRfBank(ah, b) \
	AH5212(ah)->ah_rfHal->getRfBank(ah, b)

/*
 * Hack macros for Nala/San: 11b is handled
 * using 11g; flip the channel flags to accomplish this.
 */
#define SAVE_CCK(_ah, _chan, _flag) do {			\
	if ((IS_2425(_ah) || IS_2417(_ah)) &&			\
	    (((_chan)->ic_flags) & IEEE80211_CHAN_CCK)) {	\
		(_chan)->ic_flags &= ~IEEE80211_CHAN_CCK;	\
		(_chan)->ic_flags |= IEEE80211_CHAN_DYN;	\
		(_flag) = AH_TRUE;				\
	} else							\
		(_flag) = AH_FALSE;				\
} while (0)
#define RESTORE_CCK(_ah, _chan, _flag) do {                     \
	if ((_flag) && (IS_2425(_ah) || IS_2417(_ah))) {	\
		(_chan)->ic_flags &= ~IEEE80211_CHAN_DYN;	\
		(_chan)->ic_flags |= IEEE80211_CHAN_CCK;	\
	}							\
} while (0)

struct ath_hal;

extern	uint32_t ar5212GetRadioRev(struct ath_hal *ah);
extern	void ar5212InitState(struct ath_hal_5212 *, uint16_t devid, HAL_SOFTC,
		HAL_BUS_TAG st, HAL_BUS_HANDLE sh, HAL_STATUS *status);
extern	void ar5212Detach(struct ath_hal *ah);
extern  HAL_BOOL ar5212ChipTest(struct ath_hal *ah);
extern  HAL_BOOL ar5212GetChannelEdges(struct ath_hal *ah,
                uint16_t flags, uint16_t *low, uint16_t *high);
extern	HAL_BOOL ar5212FillCapabilityInfo(struct ath_hal *ah);

extern	void ar5212SetBeaconTimers(struct ath_hal *ah,
		const HAL_BEACON_TIMERS *);
extern	void ar5212BeaconInit(struct ath_hal *ah,
		uint32_t next_beacon, uint32_t beacon_period);
extern	void ar5212ResetStaBeaconTimers(struct ath_hal *ah);
extern	void ar5212SetStaBeaconTimers(struct ath_hal *ah,
		const HAL_BEACON_STATE *);
extern	uint64_t ar5212GetNextTBTT(struct ath_hal *);

extern	HAL_BOOL ar5212IsInterruptPending(struct ath_hal *ah);
extern	HAL_BOOL ar5212GetPendingInterrupts(struct ath_hal *ah, HAL_INT *);
extern	HAL_INT ar5212GetInterrupts(struct ath_hal *ah);
extern	HAL_INT ar5212SetInterrupts(struct ath_hal *ah, HAL_INT ints);

extern	uint32_t ar5212GetKeyCacheSize(struct ath_hal *);
extern	HAL_BOOL ar5212IsKeyCacheEntryValid(struct ath_hal *, uint16_t entry);
extern	HAL_BOOL ar5212ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry);
extern	HAL_BOOL ar5212SetKeyCacheEntryMac(struct ath_hal *,
			uint16_t entry, const uint8_t *mac);
extern	HAL_BOOL ar5212SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
                       const HAL_KEYVAL *k, const uint8_t *mac, int xorKey);

extern	void ar5212GetMacAddress(struct ath_hal *ah, uint8_t *mac);
extern	HAL_BOOL ar5212SetMacAddress(struct ath_hal *ah, const uint8_t *);
extern	void ar5212GetBssIdMask(struct ath_hal *ah, uint8_t *mac);
extern	HAL_BOOL ar5212SetBssIdMask(struct ath_hal *, const uint8_t *);
extern	HAL_BOOL ar5212EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar5212EepromWrite(struct ath_hal *, u_int off, uint16_t data);
extern	HAL_BOOL ar5212SetRegulatoryDomain(struct ath_hal *ah,
		uint16_t regDomain, HAL_STATUS *stats);
extern	u_int ar5212GetWirelessModes(struct ath_hal *ah);
extern	void ar5212EnableRfKill(struct ath_hal *);
extern	HAL_BOOL ar5212GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar5212GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5212GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	uint32_t ar5212GpioGet(struct ath_hal *ah, uint32_t gpio);
extern	void ar5212GpioSetIntr(struct ath_hal *ah, u_int, uint32_t ilevel);
extern	void ar5212SetLedState(struct ath_hal *ah, HAL_LED_STATE state);
extern	void ar5212WriteAssocid(struct ath_hal *ah, const uint8_t *bssid,
		uint16_t assocId);
extern	uint32_t ar5212GetTsf32(struct ath_hal *ah);
extern	uint64_t ar5212GetTsf64(struct ath_hal *ah);
extern	void ar5212SetTsf64(struct ath_hal *ah, uint64_t tsf64);
extern	void ar5212ResetTsf(struct ath_hal *ah);
extern	void ar5212SetBasicRate(struct ath_hal *ah, HAL_RATE_SET *pSet);
extern	uint32_t ar5212GetRandomSeed(struct ath_hal *ah);
extern	HAL_BOOL ar5212DetectCardPresent(struct ath_hal *ah);
extern	void ar5212EnableMibCounters(struct ath_hal *);
extern	void ar5212DisableMibCounters(struct ath_hal *);
extern	void ar5212UpdateMibCounters(struct ath_hal *ah, HAL_MIB_STATS* stats);
extern	HAL_BOOL ar5212IsJapanChannelSpreadSupported(struct ath_hal *ah);
extern	uint32_t ar5212GetCurRssi(struct ath_hal *ah);
extern	u_int ar5212GetDefAntenna(struct ath_hal *ah);
extern	void ar5212SetDefAntenna(struct ath_hal *ah, u_int antenna);
extern	HAL_ANT_SETTING ar5212GetAntennaSwitch(struct ath_hal *);
extern	HAL_BOOL ar5212SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);
extern	HAL_BOOL ar5212IsSleepAfterBeaconBroken(struct ath_hal *ah);
extern	HAL_BOOL ar5212SetSifsTime(struct ath_hal *, u_int);
extern	u_int ar5212GetSifsTime(struct ath_hal *);
extern	HAL_BOOL ar5212SetSlotTime(struct ath_hal *, u_int);
extern	u_int ar5212GetSlotTime(struct ath_hal *);
extern	HAL_BOOL ar5212SetAckTimeout(struct ath_hal *, u_int);
extern	u_int ar5212GetAckTimeout(struct ath_hal *);
extern	HAL_BOOL ar5212SetAckCTSRate(struct ath_hal *, u_int);
extern	u_int ar5212GetAckCTSRate(struct ath_hal *);
extern	HAL_BOOL ar5212SetCTSTimeout(struct ath_hal *, u_int);
extern	u_int ar5212GetCTSTimeout(struct ath_hal *);
extern  HAL_BOOL ar5212SetDecompMask(struct ath_hal *, uint16_t, int);
void 	ar5212SetCoverageClass(struct ath_hal *, uint8_t, int);
extern	void ar5212SetPCUConfig(struct ath_hal *);
extern	HAL_BOOL ar5212Use32KHzclock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	void ar5212SetupClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	void ar5212RestoreClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	int16_t ar5212GetNfAdjust(struct ath_hal *,
		const HAL_CHANNEL_INTERNAL *);
extern	void ar5212SetCompRegs(struct ath_hal *ah);
extern	HAL_STATUS ar5212GetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t *);
extern	HAL_BOOL ar5212SetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t, HAL_STATUS *);
extern	HAL_BOOL ar5212GetDiagState(struct ath_hal *ah, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize);
extern	HAL_STATUS ar5212SetQuiet(struct ath_hal *ah, uint32_t period,
		uint32_t duration, uint32_t nextStart, HAL_QUIET_FLAG flag);
extern	HAL_BOOL ar5212GetMibCycleCounts(struct ath_hal *,
		HAL_SURVEY_SAMPLE *);
extern	void ar5212SetChainMasks(struct ath_hal *, uint32_t, uint32_t);

extern	HAL_BOOL ar5212SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode,
		int setChip);
extern	HAL_POWER_MODE ar5212GetPowerMode(struct ath_hal *ah);
extern	HAL_BOOL ar5212GetPowerStatus(struct ath_hal *ah);

extern	uint32_t ar5212GetRxDP(struct ath_hal *ath, HAL_RX_QUEUE);
extern	void ar5212SetRxDP(struct ath_hal *ah, uint32_t rxdp, HAL_RX_QUEUE);
extern	void ar5212EnableReceive(struct ath_hal *ah);
extern	HAL_BOOL ar5212StopDmaReceive(struct ath_hal *ah);
extern	void ar5212StartPcuReceive(struct ath_hal *ah);
extern	void ar5212StopPcuReceive(struct ath_hal *ah);
extern	void ar5212SetMulticastFilter(struct ath_hal *ah,
		uint32_t filter0, uint32_t filter1);
extern	HAL_BOOL ar5212ClrMulticastFilterIndex(struct ath_hal *, uint32_t ix);
extern	HAL_BOOL ar5212SetMulticastFilterIndex(struct ath_hal *, uint32_t ix);
extern	uint32_t ar5212GetRxFilter(struct ath_hal *ah);
extern	void ar5212SetRxFilter(struct ath_hal *ah, uint32_t bits);
extern	HAL_BOOL ar5212SetupRxDesc(struct ath_hal *,
		struct ath_desc *, uint32_t size, u_int flags);
extern	HAL_STATUS ar5212ProcRxDesc(struct ath_hal *ah, struct ath_desc *,
		uint32_t, struct ath_desc *, uint64_t,
		struct ath_rx_status *);

extern	HAL_BOOL ar5212Reset(struct ath_hal *ah, HAL_OPMODE opmode,
		struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
		HAL_RESET_TYPE, HAL_STATUS *status);
extern	HAL_BOOL ar5212SetChannel(struct ath_hal *,
		const struct ieee80211_channel *);
extern	void ar5212SetOperatingMode(struct ath_hal *ah, int opmode);
extern	HAL_BOOL ar5212PhyDisable(struct ath_hal *ah);
extern	HAL_BOOL ar5212Disable(struct ath_hal *ah);
extern	HAL_BOOL ar5212ChipReset(struct ath_hal *ah,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar5212PerCalibration(struct ath_hal *ah,
		struct ieee80211_channel *chan, HAL_BOOL *isIQdone);
extern	HAL_BOOL ar5212PerCalibrationN(struct ath_hal *ah,
		struct ieee80211_channel *chan, u_int chainMask,
		HAL_BOOL longCal, HAL_BOOL *isCalDone);
extern	HAL_BOOL ar5212ResetCalValid(struct ath_hal *ah,
		const struct ieee80211_channel *);
extern	int16_t ar5212GetNoiseFloor(struct ath_hal *ah);
extern	void ar5212InitNfCalHistBuffer(struct ath_hal *);
extern	int16_t ar5212GetNfHistMid(const int16_t calData[]);
extern	void ar5212SetSpurMitigation(struct ath_hal *,
		 const struct ieee80211_channel *);
extern	HAL_BOOL ar5212SetAntennaSwitchInternal(struct ath_hal *ah,
		HAL_ANT_SETTING settings, const struct ieee80211_channel *);
extern	HAL_BOOL ar5212SetTxPowerLimit(struct ath_hal *ah, uint32_t limit);
extern	HAL_BOOL ar5212GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan);
extern	void ar5212InitializeGainValues(struct ath_hal *);
extern	HAL_RFGAIN ar5212GetRfgain(struct ath_hal *ah);
extern	void ar5212RequestRfgain(struct ath_hal *);

extern	HAL_BOOL ar5212UpdateTxTrigLevel(struct ath_hal *,
		HAL_BOOL IncTrigLevel);
extern  HAL_BOOL ar5212SetTxQueueProps(struct ath_hal *ah, int q,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5212GetTxQueueProps(struct ath_hal *ah, int q,
		HAL_TXQ_INFO *qInfo);
extern	int ar5212SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5212ReleaseTxQueue(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5212ResetTxQueue(struct ath_hal *ah, u_int q);
extern	uint32_t ar5212GetTxDP(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5212SetTxDP(struct ath_hal *ah, u_int q, uint32_t txdp);
extern	HAL_BOOL ar5212StartTxDma(struct ath_hal *ah, u_int q);
extern	uint32_t ar5212NumTxPending(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5212StopTxDma(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5212SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txTries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
		u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar5212SetupXTxDesc(struct ath_hal *, struct ath_desc *,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3);
extern	HAL_BOOL ar5212FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0);
extern	HAL_STATUS ar5212ProcTxDesc(struct ath_hal *ah,
		struct ath_desc *, struct ath_tx_status *);
extern  void ar5212GetTxIntrQueue(struct ath_hal *ah, uint32_t *);
extern  void ar5212IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *);
extern	HAL_BOOL ar5212GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *ds0, int *rates, int *tries);
extern	void ar5212SetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t link);
extern	void ar5212GetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t *link);
extern	void ar5212GetTxDescLinkPtr(struct ath_hal *ah, void *ds,
		uint32_t **linkptr);

extern	const HAL_RATE_TABLE *ar5212GetRateTable(struct ath_hal *, u_int mode);

extern	void ar5212AniAttach(struct ath_hal *, const struct ar5212AniParams *,
		const struct ar5212AniParams *, HAL_BOOL ena);
extern	void ar5212AniDetach(struct ath_hal *);
extern	struct ar5212AniState *ar5212AniGetCurrentState(struct ath_hal *);
extern	HAL_ANI_STATS *ar5212AniGetCurrentStats(struct ath_hal *);
extern	HAL_BOOL ar5212AniControl(struct ath_hal *, HAL_ANI_CMD cmd, int param);
extern	HAL_BOOL ar5212AniSetParams(struct ath_hal *,
		const struct ar5212AniParams *, const struct ar5212AniParams *);
struct ath_rx_status;
extern	void ar5212AniPhyErrReport(struct ath_hal *ah,
		const struct ath_rx_status *rs);
extern	void ar5212ProcessMibIntr(struct ath_hal *, const HAL_NODE_STATS *);
extern	void ar5212RxMonitor(struct ath_hal *, const HAL_NODE_STATS *,
			     const struct ieee80211_channel *);
extern	void ar5212AniPoll(struct ath_hal *, const struct ieee80211_channel *);
extern	void ar5212AniReset(struct ath_hal *, const struct ieee80211_channel *,
		HAL_OPMODE, int);

extern	HAL_BOOL ar5212IsNFCalInProgress(struct ath_hal *ah);
extern	HAL_BOOL ar5212WaitNFCalComplete(struct ath_hal *ah, int i);
extern	void ar5212EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	HAL_BOOL ar5212GetDfsDefaultThresh(struct ath_hal *ah,
	    HAL_PHYERR_PARAM *pe);
extern	void ar5212GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	HAL_BOOL ar5212ProcessRadarEvent(struct ath_hal *ah,
	    struct ath_rx_status *rxs, uint64_t fulltsf, const char *buf,
	    HAL_DFS_EVENT *event);
extern	HAL_BOOL ar5212IsFastClockEnabled(struct ath_hal *ah);
extern	uint32_t ar5212Get11nExtBusy(struct ath_hal *ah);

#endif	/* _ATH_AR5212_H_ */
