/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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
#ifndef _ATH_AR5211_H_
#define _ATH_AR5211_H_

#include "ah_eeprom.h"

#define	AR5211_MAGIC	0x19570405

/* Classes for WME streams */
#define	AC_BK	0
#define	AC_BE	1
#define	AC_VI	2
#define	AC_VO	3

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
	( (INIT_RESET_TSF << 24)  | (INIT_BEACON_EN << 23) | \
	  (INIT_TIM_OFFSET << 16) | INIT_BEACON_PERIOD )

#define INIT_CONFIG_STATUS	0x00000000
#define INIT_RSSI_THR		0x00000700	/* Missed beacon counter initialized to 0x7 (max is 0xff) */
#define INIT_IQCAL_LOG_COUNT_MAX	0xF
#define INIT_BCON_CNTRL_REG	0x00000000

#define INIT_BEACON_PERIOD	0xffff
#define INIT_TIM_OFFSET		0
#define INIT_BEACON_EN		0		/* this should be set by AP only when it's ready */
#define INIT_RESET_TSF		0

/*
 * Various fifo fill before Tx start, in 64-byte units
 * i.e. put the frame in the air while still DMAing
 */
#define MIN_TX_FIFO_THRESHOLD	   0x1
#define MAX_TX_FIFO_THRESHOLD	   ((IEEE80211_MAX_LEN / 64) + 1)
#define INIT_TX_FIFO_THRESHOLD	  MIN_TX_FIFO_THRESHOLD

/*
 * Gain support.
 */
typedef struct _gainOptStep {
	int16_t	paramVal[4];
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

enum {
	RFGAIN_INACTIVE,
	RFGAIN_READ_REQUESTED,
	RFGAIN_NEED_CHANGE
};

/*
 * Header Info - general parameters and
 * values set for each chipset board solution
 * that are programmed every reset
 */
struct ath_hal_5211 {
	struct ath_hal_private	ah_priv;	/* base class */

	GAIN_VALUES	ah_gainValues;

	uint8_t		ah_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		ah_bssid[IEEE80211_ADDR_LEN];

	/*
	 * Runtime state.
	 */
	uint32_t	ah_maskReg;		/* copy of AR_IMR */
	uint32_t	ah_txOkInterruptMask;
	uint32_t	ah_txErrInterruptMask;
	uint32_t	ah_txDescInterruptMask;
	uint32_t	ah_txEolInterruptMask;
	uint32_t	ah_txUrnInterruptMask;
	HAL_TX_QUEUE_INFO ah_txq[HAL_NUM_TX_QUEUES];
	HAL_ANT_SETTING ah_diversityControl;	/* antenna setting */
	uint32_t	ah_calibrationTime;
	HAL_BOOL	ah_bIQCalibration;
	int		ah_rfgainState;
	uint32_t	ah_tx6PowerInHalfDbm;	/* power output for 6Mb tx */
	uint32_t	ah_staId1Defaults;	/* STA_ID1 default settings */
	uint32_t	ah_beaconInterval;
	uint32_t	ah_rssiThr;		/* RSSI_THR settings */

	u_int		ah_sifstime;		/* user-specified sifs time */
	u_int		ah_slottime;		/* user-specified slot time */
	u_int		ah_acktimeout;		/* user-specified ack timeout */
	u_int		ah_ctstimeout;		/* user-specified cts timeout */
	/*
	 * RF Silent handling.
	 */
	uint32_t	ah_gpioSelect;		/* GPIO pin to use */
	uint32_t	ah_polarity;		/* polarity to disable RF */
	uint32_t	ah_gpioBit;		/* after init, prev value */
};
#define	AH5211(ah)	((struct ath_hal_5211 *)(ah))

struct ath_hal;

extern	void ar5211Detach(struct ath_hal *);

extern	HAL_BOOL ar5211Reset(struct ath_hal *, HAL_OPMODE,
		struct ieee80211_channel *, HAL_BOOL bChannelChange,
		HAL_RESET_TYPE,
		HAL_STATUS *);
extern	HAL_BOOL ar5211PhyDisable(struct ath_hal *);
extern	HAL_BOOL ar5211Disable(struct ath_hal *);
extern	HAL_BOOL ar5211ChipReset(struct ath_hal *,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar5211PerCalibration(struct ath_hal *, struct ieee80211_channel *, HAL_BOOL *);
extern	HAL_BOOL ar5211PerCalibrationN(struct ath_hal *ah, struct ieee80211_channel *chan,
		u_int chainMask, HAL_BOOL longCal, HAL_BOOL *isCalDone);
extern	HAL_BOOL ar5211ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *);
extern	HAL_BOOL ar5211SetTxPowerLimit(struct ath_hal *, uint32_t limit);
extern	HAL_BOOL ar5211CalNoiseFloor(struct ath_hal *,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar5211SetAntennaSwitchInternal(struct ath_hal *,
		HAL_ANT_SETTING, const struct ieee80211_channel *);
extern	int16_t ar5211GetNfAdjust(struct ath_hal *,
		const HAL_CHANNEL_INTERNAL *);
extern	HAL_BOOL ar5211ResetDma(struct ath_hal *, HAL_OPMODE);
extern	void ar5211InitializeGainValues(struct ath_hal *);
extern	HAL_RFGAIN ar5211GetRfgain(struct ath_hal *);
extern	void ar5211SetPCUConfig(struct ath_hal *);

extern  HAL_BOOL ar5211SetTxQueueProps(struct ath_hal *ah, int q,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5211GetTxQueueProps(struct ath_hal *ah, int q,
		HAL_TXQ_INFO *qInfo);
extern	int ar5211SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5211ReleaseTxQueue(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5211ResetTxQueue(struct ath_hal *ah, u_int q);
extern	uint32_t ar5211GetTxDP(struct ath_hal *, u_int);
extern	HAL_BOOL ar5211SetTxDP(struct ath_hal *, u_int, uint32_t txdp);
extern	HAL_BOOL ar5211UpdateTxTrigLevel(struct ath_hal *, HAL_BOOL);
extern	HAL_BOOL ar5211StartTxDma(struct ath_hal *, u_int);
extern	HAL_BOOL ar5211StopTxDma(struct ath_hal *, u_int);
extern	uint32_t ar5211NumTxPending(struct ath_hal *, u_int qnum);
extern	HAL_BOOL ar5211IsTxQueueStopped(struct ath_hal *, u_int);
extern	HAL_BOOL ar5211GetTransmitFilterIndex(struct ath_hal *, uint32_t);
extern	HAL_BOOL ar5211SetupTxDesc(struct ath_hal *, struct ath_desc *,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txTries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
                u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar5211SetupXTxDesc(struct ath_hal *, struct ath_desc *,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3);
extern	HAL_BOOL ar5211FillTxDesc(struct ath_hal *, struct ath_desc *,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0);
extern	HAL_STATUS ar5211ProcTxDesc(struct ath_hal *,
		struct ath_desc *, struct ath_tx_status *);
extern  void ar5211GetTxIntrQueue(struct ath_hal *ah, uint32_t *);
extern  void ar5211IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *);
extern	HAL_BOOL ar5211GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *ds0, int *rates, int *tries);
extern	void ar5211SetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t link);
extern	void ar5211GetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t *link);
extern	void ar5211GetTxDescLinkPtr(struct ath_hal *ah, void *ds,
		uint32_t **linkptr);

extern	uint32_t ar5211GetRxDP(struct ath_hal *, HAL_RX_QUEUE);
extern	void ar5211SetRxDP(struct ath_hal *, uint32_t rxdp, HAL_RX_QUEUE);
extern	void ar5211EnableReceive(struct ath_hal *);
extern	HAL_BOOL ar5211StopDmaReceive(struct ath_hal *);
extern	void ar5211StartPcuReceive(struct ath_hal *);
extern	void ar5211StopPcuReceive(struct ath_hal *);
extern	void ar5211SetMulticastFilter(struct ath_hal *,
		uint32_t filter0, uint32_t filter1);
extern	HAL_BOOL ar5211ClrMulticastFilterIndex(struct ath_hal *, uint32_t);
extern	HAL_BOOL ar5211SetMulticastFilterIndex(struct ath_hal *, uint32_t);
extern	uint32_t ar5211GetRxFilter(struct ath_hal *);
extern	void ar5211SetRxFilter(struct ath_hal *, uint32_t);
extern	HAL_BOOL ar5211SetupRxDesc(struct ath_hal *, struct ath_desc *,
		uint32_t, u_int flags);
extern	HAL_STATUS ar5211ProcRxDesc(struct ath_hal *, struct ath_desc *,
		uint32_t, struct ath_desc *, uint64_t,
		struct ath_rx_status *);

extern	void ar5211GetMacAddress(struct ath_hal *, uint8_t *);
extern	HAL_BOOL ar5211SetMacAddress(struct ath_hal *ah, const uint8_t *);
extern	void ar5211GetBssIdMask(struct ath_hal *, uint8_t *);
extern	HAL_BOOL ar5211SetBssIdMask(struct ath_hal *, const uint8_t *);
extern	HAL_BOOL ar5211EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar5211EepromWrite(struct ath_hal *, u_int off, uint16_t data);
extern	HAL_BOOL ar5211SetRegulatoryDomain(struct ath_hal *,
		uint16_t, HAL_STATUS *);
extern	u_int ar5211GetWirelessModes(struct ath_hal *);
extern	void ar5211EnableRfKill(struct ath_hal *);
extern	uint32_t ar5211GpioGet(struct ath_hal *, uint32_t gpio);
extern	void ar5211GpioSetIntr(struct ath_hal *, u_int, uint32_t ilevel);
extern	HAL_BOOL ar5211GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar5211GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5211GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	void ar5211SetLedState(struct ath_hal *, HAL_LED_STATE);
extern	u_int ar5211AntennaGet(struct ath_hal *);
extern	void ar5211WriteAssocid(struct ath_hal *,
		const uint8_t *bssid, uint16_t assocId);
extern	uint64_t ar5211GetTsf64(struct ath_hal *);
extern	uint32_t ar5211GetTsf32(struct ath_hal *);
extern	void ar5211ResetTsf(struct ath_hal *);
extern	uint32_t ar5211GetMaxTurboRate(struct ath_hal *);
extern	uint32_t ar5211GetRandomSeed(struct ath_hal *);
extern	HAL_BOOL ar5211DetectCardPresent(struct ath_hal *);
extern	void ar5211UpdateMibCounters(struct ath_hal *, HAL_MIB_STATS *);
extern	void ar5211EnableHwEncryption(struct ath_hal *);
extern	void ar5211DisableHwEncryption(struct ath_hal *);
extern	HAL_BOOL ar5211SetSlotTime(struct ath_hal *, u_int);
extern	u_int ar5211GetSlotTime(struct ath_hal *);
extern	HAL_BOOL ar5211SetAckTimeout(struct ath_hal *, u_int);
extern	u_int ar5211GetAckTimeout(struct ath_hal *);
extern	HAL_BOOL ar5211SetAckCTSRate(struct ath_hal *, u_int);
extern	u_int ar5211GetAckCTSRate(struct ath_hal *);
extern	HAL_BOOL ar5211SetCTSTimeout(struct ath_hal *, u_int);
extern	u_int ar5211GetCTSTimeout(struct ath_hal *);
extern	HAL_BOOL ar5211SetSifsTime(struct ath_hal *, u_int);
extern	u_int ar5211GetSifsTime(struct ath_hal *);
extern  HAL_BOOL ar5211SetDecompMask(struct ath_hal *, uint16_t, int);
extern	void ar5211SetCoverageClass(struct ath_hal *, uint8_t, int);
extern	HAL_STATUS ar5211SetQuiet(struct ath_hal *, uint32_t, uint32_t,
		uint32_t, HAL_QUIET_FLAG);
extern	uint32_t ar5211GetCurRssi(struct ath_hal *);
extern	u_int ar5211GetDefAntenna(struct ath_hal *);
extern	void ar5211SetDefAntenna(struct ath_hal *ah, u_int antenna);
extern	HAL_ANT_SETTING ar5211GetAntennaSwitch(struct ath_hal *);
extern	HAL_BOOL ar5211SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);
extern	HAL_STATUS ar5211GetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t *);
extern	HAL_BOOL ar5211SetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t, HAL_STATUS *);
extern	HAL_BOOL ar5211GetDiagState(struct ath_hal *ah, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize);
extern	uint32_t ar5211Get11nExtBusy(struct ath_hal *);
extern	HAL_BOOL ar5211GetMibCycleCounts(struct ath_hal *,
		HAL_SURVEY_SAMPLE *);
extern	void ar5211SetChainMasks(struct ath_hal *ah, uint32_t, uint32_t);

extern	void ar5211EnableDfs(struct ath_hal *, HAL_PHYERR_PARAM *);
extern	void ar5211GetDfsThresh(struct ath_hal *, HAL_PHYERR_PARAM *);

extern	u_int ar5211GetKeyCacheSize(struct ath_hal *);
extern	HAL_BOOL ar5211IsKeyCacheEntryValid(struct ath_hal *, uint16_t);
extern	HAL_BOOL ar5211ResetKeyCacheEntry(struct ath_hal *, uint16_t entry);
extern	HAL_BOOL ar5211SetKeyCacheEntry(struct ath_hal *, uint16_t entry,
                       const HAL_KEYVAL *, const uint8_t *mac,
                       int xorKey);
extern	HAL_BOOL ar5211SetKeyCacheEntryMac(struct ath_hal *,
			uint16_t, const uint8_t *);

extern	HAL_BOOL ar5211SetPowerMode(struct ath_hal *, uint32_t powerRequest,
		int setChip);
extern	HAL_POWER_MODE ar5211GetPowerMode(struct ath_hal *);

extern	void ar5211SetBeaconTimers(struct ath_hal *,
		const HAL_BEACON_TIMERS *);
extern	void ar5211BeaconInit(struct ath_hal *, uint32_t, uint32_t);
extern	void ar5211SetStaBeaconTimers(struct ath_hal *,
		const HAL_BEACON_STATE *);
extern	void ar5211ResetStaBeaconTimers(struct ath_hal *);
extern	uint64_t ar5211GetNextTBTT(struct ath_hal *);

extern	HAL_BOOL ar5211IsInterruptPending(struct ath_hal *);
extern	HAL_BOOL ar5211GetPendingInterrupts(struct ath_hal *, HAL_INT *);
extern	HAL_INT ar5211GetInterrupts(struct ath_hal *);
extern	HAL_INT ar5211SetInterrupts(struct ath_hal *, HAL_INT ints);

extern	const HAL_RATE_TABLE *ar5211GetRateTable(struct ath_hal *, u_int mode);

extern	HAL_BOOL ar5211AniControl(struct ath_hal *, HAL_ANI_CMD, int );
extern	void ar5211RxMonitor(struct ath_hal *, const HAL_NODE_STATS *,
		const struct ieee80211_channel *);
extern	void ar5211AniPoll(struct ath_hal *, const struct ieee80211_channel *);
extern	void ar5211MibEvent(struct ath_hal *, const HAL_NODE_STATS *);
#endif /* _ATH_AR5211_H_ */
