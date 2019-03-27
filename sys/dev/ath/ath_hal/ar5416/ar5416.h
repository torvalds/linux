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
#ifndef _ATH_AR5416_H_
#define _ATH_AR5416_H_

#include "ar5212/ar5212.h"
#include "ar5416_cal.h"
#include "ah_eeprom_v14.h"	/* for CAL_TARGET_POWER_* */

#define	AR5416_MAGIC	0x20065416

typedef struct {
	uint16_t	synth_center;
	uint16_t	ctl_center;
	uint16_t	ext_center;
} CHAN_CENTERS;

typedef enum Ar5416_Rates {
        rate6mb,  rate9mb,  rate12mb, rate18mb,
        rate24mb, rate36mb, rate48mb, rate54mb,
        rate1l,   rate2l,   rate2s,   rate5_5l,
        rate5_5s, rate11l,  rate11s,  rateXr,
        rateHt20_0, rateHt20_1, rateHt20_2, rateHt20_3,
        rateHt20_4, rateHt20_5, rateHt20_6, rateHt20_7,
        rateHt40_0, rateHt40_1, rateHt40_2, rateHt40_3,
        rateHt40_4, rateHt40_5, rateHt40_6, rateHt40_7,
        rateDupCck, rateDupOfdm, rateExtCck, rateExtOfdm,
        Ar5416RateSize
} AR5416_RATES;

#define	AR5416_DEFAULT_RXCHAINMASK	7
#define	AR5416_DEFAULT_TXCHAINMASK	1
#define	AR5416_MAX_RATE_POWER		63
#define	AR5416_KEYTABLE_SIZE		128

#define	AR5416_CCA_MAX_GOOD_VALUE	-85
#define	AR5416_CCA_MAX_HIGH_VALUE	-62
#define	AR5416_CCA_MIN_BAD_VALUE	-140
#define	AR9285_CCA_MAX_GOOD_VALUE	-118

#define AR5416_SPUR_RSSI_THRESH		40

struct ar5416NfLimits {
	int16_t max;
	int16_t min;
	int16_t nominal;
};

struct ath_hal_5416 {
	struct ath_hal_5212 ah_5212;

	/* NB: RF data setup at attach */
	HAL_INI_ARRAY	ah_ini_bb_rfgain;
	HAL_INI_ARRAY	ah_ini_bank0;
	HAL_INI_ARRAY	ah_ini_bank1;
	HAL_INI_ARRAY	ah_ini_bank2;
	HAL_INI_ARRAY	ah_ini_bank3;
	HAL_INI_ARRAY	ah_ini_bank6;
	HAL_INI_ARRAY	ah_ini_bank7;
	HAL_INI_ARRAY	ah_ini_addac;
	HAL_INI_ARRAY	ah_ini_pcieserdes;

	void		(*ah_writeIni)(struct ath_hal *,
			    const struct ieee80211_channel *);
	void		(*ah_spurMitigate)(struct ath_hal *,
			    const struct ieee80211_channel *);

	/* calibration ops */
	HAL_BOOL	(*ah_cal_initcal)(struct ath_hal *,
			    const struct ieee80211_channel *);
	void		(*ah_cal_pacal)(struct ath_hal *,
			    HAL_BOOL is_reset);

	/* optional open-loop tx power control related methods */
	void		(*ah_olcInit)(struct ath_hal *);
	void		(*ah_olcTempCompensation)(struct ath_hal *);

	/* tx power control */
	HAL_BOOL	(*ah_setPowerCalTable) (struct ath_hal *ah,
			    struct ar5416eeprom *pEepData,
			    const struct ieee80211_channel *chan,
        		    int16_t *pTxPowerIndexOffset);

	/* baseband operations */
	void		(*ah_initPLL) (struct ath_hal *ah,
			    const struct ieee80211_channel *chan);

	/* bluetooth coexistence operations */
	void		(*ah_btCoexSetDiversity)(struct ath_hal *ah);

	u_int       	ah_globaltxtimeout;	/* global tx timeout */
	u_int		ah_gpioMask;
	int		ah_hangs;		/* h/w hangs state */
	uint8_t		ah_keytype[AR5416_KEYTABLE_SIZE];
	/*
	 * Primary/Extension Channel Tx, Rx, Rx Clear State
	 */
	uint32_t	ah_cycleCount;
	uint32_t	ah_ctlBusy;
	uint32_t	ah_extBusy;
	uint32_t	ah_rxBusy;
	uint32_t	ah_txBusy;
	uint32_t	ah_rx_chainmask;
	uint32_t	ah_tx_chainmask;

	HAL_ANI_CMD	ah_ani_function;

	struct ar5416PerCal ah_cal;		/* periodic calibration state */

	struct ar5416NfLimits nf_2g;
	struct ar5416NfLimits nf_5g;

	/*
	 * TX power configuration related structures
	 */
	int		initPDADC;
	int		ah_ht40PowerIncForPdadc;
	int16_t		ah_ratesArray[Ar5416RateSize];

	int		ah_need_an_top2_fixup;	/* merlin or later chips that may need this workaround */

	/*
	 * Bluetooth coexistence static setup according to the registry
	 */
	HAL_BT_MODULE ah_btModule;            /* Bluetooth module identifier */
	uint8_t		ah_btCoexConfigType;  /* BT coex configuration */
	uint8_t		ah_btActiveGpioSelect;  /* GPIO pin for BT_ACTIVE */
	uint8_t		ah_btPriorityGpioSelect; /* GPIO pin for BT_PRIORITY */
	uint8_t		ah_wlanActiveGpioSelect; /* GPIO pin for WLAN_ACTIVE */
	uint8_t		ah_btActivePolarity;  /* Polarity of BT_ACTIVE */
	HAL_BOOL	ah_btCoexSingleAnt;   /* Single or dual antenna configuration */
	uint8_t		ah_btWlanIsolation;   /* Isolation between BT and WLAN in dB */

	/*
	 * Bluetooth coexistence runtime settings
	 */
	HAL_BOOL	ah_btCoexEnabled;     /* If Bluetooth coexistence is enabled */
	uint32_t	ah_btCoexMode;        /* Register setting for AR_BT_COEX_MODE */
	uint32_t	ah_btCoexBTWeight;    /* Register setting for AR_BT_COEX_WEIGHT */
	uint32_t	ah_btCoexWLANWeight;  /* Register setting for AR_BT_COEX_WEIGHT */
	uint32_t	ah_btCoexMode2;       /* Register setting for AR_BT_COEX_MODE2 */
	uint32_t	ah_btCoexFlag;        /* Special tuning flags for BT coex */
};
#define	AH5416(_ah)	((struct ath_hal_5416 *)(_ah))

#define IS_5416_PCI(ah) ((AH_PRIVATE(ah)->ah_macVersion) == AR_SREV_VERSION_OWL_PCI)
#define IS_5416_PCIE(ah) ((AH_PRIVATE(ah)->ah_macVersion) == AR_SREV_VERSION_OWL_PCIE)
#undef IS_PCIE
#define IS_PCIE(ah) (IS_5416_PCIE(ah))

extern	HAL_BOOL ar2133RfAttach(struct ath_hal *, HAL_STATUS *);

struct ath_hal;

extern	uint32_t ar5416GetRadioRev(struct ath_hal *ah);
extern	void ar5416InitState(struct ath_hal_5416 *, uint16_t devid,
		HAL_SOFTC sc, HAL_BUS_TAG st, HAL_BUS_HANDLE sh,
		HAL_STATUS *status);
extern	void ar5416Detach(struct ath_hal *ah);
extern	void ar5416AttachPCIE(struct ath_hal *ah);
extern	HAL_BOOL ar5416FillCapabilityInfo(struct ath_hal *ah);

extern	void ar5416AniAttach(struct ath_hal *, const struct ar5212AniParams *,
		const struct ar5212AniParams *, HAL_BOOL ena);
extern	void ar5416AniDetach(struct ath_hal *);
extern	HAL_BOOL ar5416AniControl(struct ath_hal *, HAL_ANI_CMD cmd, int param);
extern	HAL_BOOL ar5416AniSetParams(struct ath_hal *,
		const struct ar5212AniParams *, const struct ar5212AniParams *);
extern	void ar5416ProcessMibIntr(struct ath_hal *, const HAL_NODE_STATS *);
extern	void ar5416RxMonitor(struct ath_hal *, const HAL_NODE_STATS *,
			     const struct ieee80211_channel *);
extern	void ar5416AniPoll(struct ath_hal *, const struct ieee80211_channel *);
extern	void ar5416AniReset(struct ath_hal *, const struct ieee80211_channel *,
		HAL_OPMODE, int);

extern	void ar5416SetBeaconTimers(struct ath_hal *, const HAL_BEACON_TIMERS *);
extern	void ar5416BeaconInit(struct ath_hal *ah,
		uint32_t next_beacon, uint32_t beacon_period);
extern	void ar5416ResetStaBeaconTimers(struct ath_hal *ah);
extern	void ar5416SetStaBeaconTimers(struct ath_hal *ah,
		const HAL_BEACON_STATE *);
extern	uint64_t ar5416GetNextTBTT(struct ath_hal *);

/* ar5416_btcoex.c */
extern	void ar5416SetBTCoexInfo(struct ath_hal *ah,
		HAL_BT_COEX_INFO *btinfo);
extern	void ar5416BTCoexConfig(struct ath_hal *ah,
		HAL_BT_COEX_CONFIG *btconf);
extern	void ar5416BTCoexAntennaDiversity(struct ath_hal *ah);
extern	void ar5416BTCoexSetQcuThresh(struct ath_hal *ah, int qnum);
extern	void ar5416BTCoexSetWeights(struct ath_hal *ah, uint32_t stompType);
extern	void ar5416BTCoexSetupBmissThresh(struct ath_hal *ah,
		uint32_t thresh);
extern	void ar5416BTCoexSetParameter(struct ath_hal *ah, uint32_t type,
		uint32_t value);
extern	void ar5416BTCoexDisable(struct ath_hal *ah);
extern	int ar5416BTCoexEnable(struct ath_hal *ah);
extern	void ar5416InitBTCoex(struct ath_hal *ah);

extern	HAL_BOOL ar5416EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar5416EepromWrite(struct ath_hal *, u_int off, uint16_t data);

extern	HAL_BOOL ar5416IsInterruptPending(struct ath_hal *ah);
extern	HAL_BOOL ar5416GetPendingInterrupts(struct ath_hal *, HAL_INT *masked);
extern	HAL_INT ar5416SetInterrupts(struct ath_hal *ah, HAL_INT ints);

extern	HAL_BOOL ar5416GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar5416GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5416GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	uint32_t ar5416GpioGet(struct ath_hal *ah, uint32_t gpio);
extern	void ar5416GpioSetIntr(struct ath_hal *ah, u_int, uint32_t ilevel);

extern	u_int ar5416GetWirelessModes(struct ath_hal *ah);
extern	void ar5416SetLedState(struct ath_hal *ah, HAL_LED_STATE state);
extern	uint64_t ar5416GetTsf64(struct ath_hal *ah);
extern	void ar5416SetTsf64(struct ath_hal *ah, uint64_t tsf64);
extern	void ar5416ResetTsf(struct ath_hal *ah);
extern	uint32_t ar5416GetCurRssi(struct ath_hal *ah);
extern	HAL_BOOL ar5416SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);
extern	HAL_BOOL ar5416SetDecompMask(struct ath_hal *, uint16_t, int);
extern	void ar5416SetCoverageClass(struct ath_hal *, uint8_t, int);
extern	HAL_BOOL ar5416GetMibCycleCounts(struct ath_hal *ah,
	    HAL_SURVEY_SAMPLE *hsample);
extern	void ar5416SetChainMasks(struct ath_hal *ah, uint32_t, uint32_t);
extern	uint32_t ar5416Get11nExtBusy(struct ath_hal *ah);
extern	void ar5416Set11nMac2040(struct ath_hal *ah, HAL_HT_MACMODE mode);
extern	HAL_HT_RXCLEAR ar5416Get11nRxClear(struct ath_hal *ah);
extern	void ar5416Set11nRxClear(struct ath_hal *ah, HAL_HT_RXCLEAR rxclear);
extern	HAL_STATUS ar5416SetQuiet(struct ath_hal *ah, uint32_t period,
	    uint32_t duration, uint32_t nextStart, HAL_QUIET_FLAG flag);
extern	HAL_STATUS ar5416GetCapability(struct ath_hal *ah,
	    HAL_CAPABILITY_TYPE type, uint32_t capability, uint32_t *result);
extern	HAL_BOOL ar5416SetCapability(struct ath_hal *ah,
	    HAL_CAPABILITY_TYPE type, uint32_t capability, uint32_t val,
	    HAL_STATUS *status);
extern	HAL_BOOL ar5416GetDiagState(struct ath_hal *ah, int request,
	    const void *args, uint32_t argsize,
	    void **result, uint32_t *resultsize);
extern	HAL_BOOL ar5416SetRifsDelay(struct ath_hal *ah,
	    const struct ieee80211_channel *chan, HAL_BOOL enable);

extern	void ar5416EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	HAL_BOOL ar5416GetDfsDefaultThresh(struct ath_hal *ah,
	    HAL_PHYERR_PARAM *pe);
extern	void ar5416GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	HAL_BOOL ar5416ProcessRadarEvent(struct ath_hal *ah,
	    struct ath_rx_status *rxs, uint64_t fulltsf, const char *buf,
	    HAL_DFS_EVENT *event);
extern	HAL_BOOL ar5416IsFastClockEnabled(struct ath_hal *ah);

/* ar9280_spectral.c */
extern	void ar5416ConfigureSpectralScan(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss);
extern	void ar5416GetSpectralParams(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss);
extern	HAL_BOOL ar5416IsSpectralActive(struct ath_hal *ah);
extern	HAL_BOOL ar5416IsSpectralEnabled(struct ath_hal *ah);
extern	void ar5416StartSpectralScan(struct ath_hal *ah);
extern	void ar5416StopSpectralScan(struct ath_hal *ah);
extern	uint32_t ar5416GetSpectralConfig(struct ath_hal *ah);
extern	void ar5416RestoreSpectralConfig(struct ath_hal *ah, uint32_t restoreval);

extern	HAL_BOOL ar5416SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode,
		int setChip);
extern	HAL_POWER_MODE ar5416GetPowerMode(struct ath_hal *ah);
extern	HAL_BOOL ar5416GetPowerStatus(struct ath_hal *ah);

extern	HAL_BOOL ar5416ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry);
extern	HAL_BOOL ar5416SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
	       const HAL_KEYVAL *k, const uint8_t *mac, int xorKey);

extern	uint32_t ar5416GetRxFilter(struct ath_hal *ah);
extern	void ar5416SetRxFilter(struct ath_hal *ah, uint32_t bits);
extern	HAL_BOOL ar5416StopDmaReceive(struct ath_hal *ah);
extern	void ar5416StartPcuReceive(struct ath_hal *ah);
extern	void ar5416StopPcuReceive(struct ath_hal *ah);
extern	HAL_BOOL ar5416SetupRxDesc(struct ath_hal *,
		struct ath_desc *, uint32_t size, u_int flags);
extern	HAL_STATUS ar5416ProcRxDesc(struct ath_hal *ah, struct ath_desc *,
		uint32_t, struct ath_desc *, uint64_t,
		struct ath_rx_status *);

extern	HAL_BOOL ar5416Reset(struct ath_hal *ah, HAL_OPMODE opmode,
		struct ieee80211_channel *chan,
		HAL_BOOL bChannelChange,
		HAL_RESET_TYPE,
		HAL_STATUS *status);
extern	HAL_BOOL ar5416PhyDisable(struct ath_hal *ah);
extern	HAL_RFGAIN ar5416GetRfgain(struct ath_hal *ah);
extern	HAL_BOOL ar5416Disable(struct ath_hal *ah);
extern	HAL_BOOL ar5416ChipReset(struct ath_hal *ah,
		const struct ieee80211_channel *);
extern	int ar5416GetRegChainOffset(struct ath_hal *ah, int i);
extern	HAL_BOOL ar5416SetBoardValues(struct ath_hal *,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar5416SetResetReg(struct ath_hal *, uint32_t type);
extern	HAL_BOOL ar5416SetTxPowerLimit(struct ath_hal *ah, uint32_t limit);
extern	HAL_BOOL ar5416SetTransmitPower(struct ath_hal *,
    		const struct ieee80211_channel *, uint16_t *);
extern	HAL_BOOL ar5416GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan);
extern	void ar5416GetChannelCenters(struct ath_hal *,
		const struct ieee80211_channel *chan, CHAN_CENTERS *centers);
extern	void ar5416SetRatesArrayFromTargetPower(struct ath_hal *ah,
		const struct ieee80211_channel *chan,
		int16_t *ratesArray,
		const CAL_TARGET_POWER_LEG *targetPowerCck,
		const CAL_TARGET_POWER_LEG *targetPowerCckExt,
		const CAL_TARGET_POWER_LEG *targetPowerOfdm,
		const CAL_TARGET_POWER_LEG *targetPowerOfdmExt,
		const CAL_TARGET_POWER_HT *targetPowerHt20,
		const CAL_TARGET_POWER_HT *targetPowerHt40);
extern	void ar5416GetTargetPowers(struct ath_hal *ah, 
		const struct ieee80211_channel *chan,
		CAL_TARGET_POWER_HT *powInfo,
		uint16_t numChannels, CAL_TARGET_POWER_HT *pNewPower,
		uint16_t numRates, HAL_BOOL isHt40Target);
extern	void ar5416GetTargetPowersLeg(struct ath_hal *ah, 
		const struct ieee80211_channel *chan,
		CAL_TARGET_POWER_LEG *powInfo,
		uint16_t numChannels, CAL_TARGET_POWER_LEG *pNewPower,
		uint16_t numRates, HAL_BOOL isExtTarget);
extern	void ar5416InitChainMasks(struct ath_hal *ah);
extern	void ar5416RestoreChainMask(struct ath_hal *ah);
extern	void ar5416EepromSetAddac(struct ath_hal *ah,
		const struct ieee80211_channel *chan);
extern	uint16_t ar5416GetMaxEdgePower(uint16_t freq,
		CAL_CTL_EDGES *pRdEdgesPower, HAL_BOOL is2GHz);
extern	void ar5416InitPLL(struct ath_hal *ah,
		const struct ieee80211_channel *chan);

/* TX power setup related routines in ar5416_reset.c */
extern	void ar5416GetGainBoundariesAndPdadcs(struct ath_hal *ah,
	const struct ieee80211_channel *chan, CAL_DATA_PER_FREQ *pRawDataSet,
	uint8_t * bChans, uint16_t availPiers,
	uint16_t tPdGainOverlap, int16_t *pMinCalPower,
	uint16_t * pPdGainBoundaries, uint8_t * pPDADCValues,
	uint16_t numXpdGains);
extern	void ar5416SetGainBoundariesClosedLoop(struct ath_hal *ah,
	int i, uint16_t pdGainOverlap_t2,
	uint16_t gainBoundaries[]);
extern	uint16_t ar5416GetXpdGainValues(struct ath_hal *ah, uint16_t xpdMask,
	uint16_t xpdGainValues[]);
extern	void ar5416WriteDetectorGainBiases(struct ath_hal *ah,
	uint16_t numXpdGain, uint16_t xpdGainValues[]);
extern	void ar5416WritePdadcValues(struct ath_hal *ah, int i,
	uint8_t pdadcValues[]);
extern	HAL_BOOL ar5416SetPowerCalTable(struct ath_hal *ah,
	struct ar5416eeprom *pEepData, const struct ieee80211_channel *chan,
	int16_t *pTxPowerIndexOffset);
extern	void ar5416WriteTxPowerRateRegisters(struct ath_hal *ah,
	const struct ieee80211_channel *chan, const int16_t ratesArray[]);

extern	HAL_BOOL ar5416StopTxDma(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5416SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txTries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
		u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar5416SetupXTxDesc(struct ath_hal *, struct ath_desc *,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3);
extern	HAL_BOOL ar5416FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0);
extern	HAL_STATUS ar5416ProcTxDesc(struct ath_hal *ah,
		struct ath_desc *, struct ath_tx_status *);
extern	HAL_BOOL ar5416GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *ds0, int *rates, int *tries);

extern	HAL_BOOL ar5416ResetTxQueue(struct ath_hal *ah, u_int q);
extern	int ar5416SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
	        const HAL_TXQ_INFO *qInfo);

extern	HAL_BOOL ar5416ChainTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int keyIx,
		HAL_CIPHER cipher, uint8_t delims,
		HAL_BOOL firstSeg, HAL_BOOL lastSeg, HAL_BOOL lastAggr);
extern	HAL_BOOL ar5416SetupFirstTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		u_int aggrLen, u_int flags, u_int txPower, u_int txRate0, u_int txTries0,
		u_int antMode, u_int rtsctsRate, u_int rtsctsDuration);
extern	HAL_BOOL ar5416SetupLastTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		const struct ath_desc *ds0);
extern	HAL_BOOL ar5416SetGlobalTxTimeout(struct ath_hal *ah, u_int tu);
extern	u_int ar5416GetGlobalTxTimeout(struct ath_hal *ah);
extern	void ar5416Set11nRateScenario(struct ath_hal *ah, struct ath_desc *ds,
		u_int durUpdateEn, u_int rtsctsRate, HAL_11N_RATE_SERIES series[],
		u_int nseries, u_int flags);

extern void ar5416Set11nAggrFirst(struct ath_hal *ah, struct ath_desc *ds,
		u_int aggrLen, u_int numDelims);
extern	void ar5416Set11nAggrMiddle(struct ath_hal *ah, struct ath_desc *ds, u_int numDelims);
extern void ar5416Set11nAggrLast(struct ath_hal *ah, struct ath_desc *ds);
extern	void ar5416Clr11nAggr(struct ath_hal *ah, struct ath_desc *ds);
extern	void ar5416Set11nVirtualMoreFrag(struct ath_hal *ah,
		struct ath_desc *ds, u_int vmf);

extern	void ar5416Set11nBurstDuration(struct ath_hal *ah, struct ath_desc *ds, u_int burstDuration);

extern	const HAL_RATE_TABLE *ar5416GetRateTable(struct ath_hal *, u_int mode);
#endif	/* _ATH_AR5416_H_ */
