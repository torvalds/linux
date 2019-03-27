/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
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
#ifndef _ATH_AR5210_H_
#define _ATH_AR5210_H_

#define	AR5210_MAGIC	0x19980124

#if 0
/*
 * RTS_ENABLE includes LONG_PKT because they essentially
 * imply the same thing, and are set or not set together
 * for this chip
 */
#define AR5210_TXD_CTRL_A_HDR_LEN(_val)         (((_val)      ) & 0x0003f)
#define AR5210_TXD_CTRL_A_TX_RATE(_val)         (((_val) <<  6) & 0x003c0)
#define AR5210_TXD_CTRL_A_RTS_ENABLE            (                 0x00c00)
#define AR5210_TXD_CTRL_A_CLEAR_DEST_MASK(_val) (((_val) << 12) & 0x01000)
#define AR5210_TXD_CTRL_A_ANT_MODE(_val)        (((_val) << 13) & 0x02000)
#define AR5210_TXD_CTRL_A_PKT_TYPE(_val)        (((_val) << 14) & 0x1c000)
#define AR5210_TXD_CTRL_A_INT_REQ               (                 0x20000)
#define AR5210_TXD_CTRL_A_KEY_VALID             (                 0x40000)
#define AR5210_TXD_CTRL_B_KEY_ID(_val)          (((_val)      ) & 0x0003f)
#define AR5210_TXD_CTRL_B_RTS_DURATION(_val)    (((_val) <<  6) & 0x7ffc0)
#endif

#define INIT_CONFIG_STATUS              0x00000000
#define INIT_ACKTOPS                    0x00000008
#define INIT_BCON_CNTRL_REG             0x00000000
#define INIT_SLOT_TIME                  0x00000168
#define INIT_SLOT_TIME_TURBO            0x000001e0 /* More aggressive turbo slot timing = 6 us */
#define INIT_ACK_CTS_TIMEOUT            0x04000400
#define INIT_ACK_CTS_TIMEOUT_TURBO      0x08000800

#define INIT_USEC                       0x27
#define INIT_USEC_TURBO                 0x4f
#define INIT_USEC_32                    0x1f
#define INIT_TX_LATENCY                 0x36
#define INIT_RX_LATENCY                 0x1D
#define INIT_TRANSMIT_LATENCY \
	((INIT_RX_LATENCY << AR_USEC_RX_LATENCY_S) | \
	 (INIT_TX_LATENCY << AR_USEC_TX_LATENCY_S) | \
	 (INIT_USEC_32 << 7) | INIT_USEC )
#define INIT_TRANSMIT_LATENCY_TURBO  \
	((INIT_RX_LATENCY << AR_USEC_RX_LATENCY_S) | \
	 (INIT_TX_LATENCY << AR_USEC_TX_LATENCY_S) | \
	 (INIT_USEC_32 << 7) | INIT_USEC_TURBO)

#define INIT_SIFS                       0x230 /* = 16 us - 2 us */
#define INIT_SIFS_TURBO                 0x1E0 /* More aggressive turbo SIFS timing - 8 us - 2 us */

/*
 * Various fifo fill before Tx start, in 64-byte units
 * i.e. put the frame in the air while still DMAing
 */
#define MIN_TX_FIFO_THRESHOLD           0x1
#define MAX_TX_FIFO_THRESHOLD           ((IEEE80211_MAX_LEN / 64) + 1)

#define INIT_NEXT_CFP_START             0xffffffff

#define INIT_BEACON_PERIOD              0xffff
#define INIT_BEACON_EN                  0 /* this should be set by AP only when it's ready */
#define INIT_BEACON_CONTROL \
	((INIT_RESET_TSF << 24) | (INIT_BEACON_EN << 23) | \
	 (INIT_TIM_OFFSET<<16)  | INIT_BEACON_PERIOD)

#define INIT_RSSI_THR                   0x00000700 /* Missed beacon counter initialized to max value of 7 */
#define INIT_ProgIFS                    0x398      /* PIFS - 2us */
#define INIT_ProgIFS_TURBO              0x3C0
#define INIT_EIFS                       0xd70
#define INIT_EIFS_TURBO                 0x1ae0
#define INIT_CARR_SENSE_EN              1
#define INIT_PROTO_TIME_CNTRL           ( (INIT_CARR_SENSE_EN << 26) | (INIT_EIFS << 12) | \
                                          (INIT_ProgIFS) )
#define INIT_PROTO_TIME_CNTRL_TURBO     ( (INIT_CARR_SENSE_EN << 26) | (INIT_EIFS_TURBO << 12) | \
                                          (INIT_ProgIFS_TURBO) )

#define	AR5210_MAX_RATE_POWER	60

#undef HAL_NUM_TX_QUEUES	/* from ah.h */
#define	HAL_NUM_TX_QUEUES	3

struct ath_hal_5210 {
	struct ath_hal_private ah_priv;	/* base definitions */

	uint8_t		ah_macaddr[IEEE80211_ADDR_LEN];
	/*
	 * Runtime state.
	 */
	uint32_t	ah_maskReg;		/* shadow of IMR+IER regs */
	uint32_t	ah_txOkInterruptMask;
	uint32_t	ah_txErrInterruptMask;
	uint32_t	ah_txDescInterruptMask;
	uint32_t	ah_txEolInterruptMask;
	uint32_t	ah_txUrnInterruptMask;
	uint8_t		ah_bssid[IEEE80211_ADDR_LEN];
	HAL_TX_QUEUE_INFO ah_txq[HAL_NUM_TX_QUEUES]; /* beacon+cab+data */
	/*
	 * Station mode support.
	 */
	uint32_t	ah_staId1Defaults;	/* STA_ID1 default settings */
	uint32_t	ah_rssiThr;		/* RSSI_THR settings */

	u_int		ah_sifstime;		/* user-specified sifs time */
	u_int		ah_slottime;		/* user-specified slot time */
	u_int		ah_acktimeout;		/* user-specified ack timeout */
	u_int		ah_ctstimeout;		/* user-specified cts timeout */

	uint16_t	ah_associd;		/* association id */
};
#define	AH5210(ah)	((struct ath_hal_5210 *)(ah))

struct ath_hal;

extern	void ar5210Detach(struct ath_hal *ah);
extern	HAL_BOOL ar5210Reset(struct ath_hal *, HAL_OPMODE,
		struct ieee80211_channel *, HAL_BOOL bChannelChange,
		HAL_RESET_TYPE, HAL_STATUS *);
extern	void ar5210SetPCUConfig(struct ath_hal *);
extern	HAL_BOOL ar5210PhyDisable(struct ath_hal *);
extern	HAL_BOOL ar5210Disable(struct ath_hal *);
extern	HAL_BOOL ar5210ChipReset(struct ath_hal *, struct ieee80211_channel *);
extern	HAL_BOOL ar5210PerCalibration(struct ath_hal *, struct ieee80211_channel *, HAL_BOOL *);
extern	HAL_BOOL ar5210PerCalibrationN(struct ath_hal *ah, struct ieee80211_channel *chan,
		u_int chainMask, HAL_BOOL longCal, HAL_BOOL *isCalDone);
extern	HAL_BOOL ar5210ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *);
extern	int16_t ar5210GetNoiseFloor(struct ath_hal *);
extern	int16_t ar5210GetNfAdjust(struct ath_hal *,
		const HAL_CHANNEL_INTERNAL *);
extern	HAL_BOOL ar5210SetTxPowerLimit(struct ath_hal *, uint32_t limit);
extern	HAL_BOOL ar5210SetTransmitPower(struct ath_hal *,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar5210CalNoiseFloor(struct ath_hal *, HAL_CHANNEL_INTERNAL *);
extern	HAL_BOOL ar5210ResetDma(struct ath_hal *, HAL_OPMODE);

extern  HAL_BOOL ar5210SetTxQueueProps(struct ath_hal *ah, int q,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5210GetTxQueueProps(struct ath_hal *ah, int q,
		HAL_TXQ_INFO *qInfo);
extern	int ar5210SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar5210ReleaseTxQueue(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar5210ResetTxQueue(struct ath_hal *ah, u_int q);
extern	uint32_t ar5210GetTxDP(struct ath_hal *, u_int);
extern	HAL_BOOL ar5210SetTxDP(struct ath_hal *, u_int, uint32_t txdp);
extern	HAL_BOOL ar5210UpdateTxTrigLevel(struct ath_hal *, HAL_BOOL);
extern	uint32_t ar5210NumTxPending(struct ath_hal *, u_int);
extern	HAL_BOOL ar5210StartTxDma(struct ath_hal *, u_int);
extern	HAL_BOOL ar5210StopTxDma(struct ath_hal *, u_int);
extern	HAL_BOOL ar5210SetupTxDesc(struct ath_hal *, struct ath_desc *,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txRetries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
                u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar5210SetupXTxDesc(struct ath_hal *, struct ath_desc *,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3);
extern	HAL_BOOL ar5210FillTxDesc(struct ath_hal *, struct ath_desc *,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0);
extern	HAL_STATUS ar5210ProcTxDesc(struct ath_hal *,
		struct ath_desc *, struct ath_tx_status *);
extern  void ar5210GetTxIntrQueue(struct ath_hal *ah, uint32_t *);
extern  void ar5210IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *);
extern	HAL_BOOL ar5210GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *, int *rates, int *tries);
extern	void ar5210SetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t link);
extern	void ar5210GetTxDescLink(struct ath_hal *ah, void *ds,
		uint32_t *link);
extern	void ar5210GetTxDescLinkPtr(struct ath_hal *ah, void *ds,
		uint32_t **linkptr);

extern	uint32_t ar5210GetRxDP(struct ath_hal *, HAL_RX_QUEUE);
extern	void ar5210SetRxDP(struct ath_hal *, uint32_t rxdp, HAL_RX_QUEUE);
extern	void ar5210EnableReceive(struct ath_hal *);
extern	HAL_BOOL ar5210StopDmaReceive(struct ath_hal *);
extern	void ar5210StartPcuReceive(struct ath_hal *);
extern	void ar5210StopPcuReceive(struct ath_hal *);
extern	void ar5210SetMulticastFilter(struct ath_hal *,
		uint32_t filter0, uint32_t filter1);
extern	HAL_BOOL ar5210ClrMulticastFilterIndex(struct ath_hal *, uint32_t);
extern	HAL_BOOL ar5210SetMulticastFilterIndex(struct ath_hal *, uint32_t);
extern	uint32_t ar5210GetRxFilter(struct ath_hal *);
extern	void ar5210SetRxFilter(struct ath_hal *, uint32_t);
extern	HAL_BOOL ar5210SetupRxDesc(struct ath_hal *, struct ath_desc *,
		uint32_t, u_int flags);
extern	HAL_STATUS ar5210ProcRxDesc(struct ath_hal *, struct ath_desc *,
		uint32_t, struct ath_desc *, uint64_t,
		struct ath_rx_status *);

extern	void ar5210GetMacAddress(struct ath_hal *, uint8_t *);
extern	HAL_BOOL ar5210SetMacAddress(struct ath_hal *ah, const uint8_t *);
extern	void ar5210GetBssIdMask(struct ath_hal *, uint8_t *);
extern	HAL_BOOL ar5210SetBssIdMask(struct ath_hal *, const uint8_t *);
extern	HAL_BOOL ar5210EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar5210EepromWrite(struct ath_hal *, u_int off, uint16_t data);
extern	HAL_BOOL ar5210SetRegulatoryDomain(struct ath_hal *,
		uint16_t, HAL_STATUS *);
extern	u_int ar5210GetWirelessModes(struct ath_hal *ah);
extern	void ar5210EnableRfKill(struct ath_hal *);
extern	HAL_BOOL ar5210GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5210GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	uint32_t ar5210GpioGet(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5210GpioSet(struct ath_hal *, uint32_t gpio, uint32_t);
extern	void ar5210Gpio0SetIntr(struct ath_hal *, u_int, uint32_t ilevel);
extern	void ar5210SetLedState(struct ath_hal *, HAL_LED_STATE);
extern	u_int ar5210GetDefAntenna(struct ath_hal *);
extern	void ar5210SetDefAntenna(struct ath_hal *, u_int);
extern	HAL_ANT_SETTING ar5210GetAntennaSwitch(struct ath_hal *);
extern	HAL_BOOL ar5210SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);
extern	void ar5210WriteAssocid(struct ath_hal *,
		const uint8_t *bssid, uint16_t assocId);
extern	uint32_t ar5210GetTsf32(struct ath_hal *);
extern	uint64_t ar5210GetTsf64(struct ath_hal *);
extern	void ar5210ResetTsf(struct ath_hal *);
extern	uint32_t ar5210GetRandomSeed(struct ath_hal *);
extern	HAL_BOOL ar5210DetectCardPresent(struct ath_hal *);
extern	void ar5210UpdateMibCounters(struct ath_hal *, HAL_MIB_STATS *);
extern	void ar5210EnableHwEncryption(struct ath_hal *);
extern	void ar5210DisableHwEncryption(struct ath_hal *);
extern	HAL_RFGAIN ar5210GetRfgain(struct ath_hal *);
extern	HAL_BOOL ar5210SetSifsTime(struct ath_hal *, u_int);
extern	u_int ar5210GetSifsTime(struct ath_hal *);
extern	HAL_BOOL ar5210SetSlotTime(struct ath_hal *, u_int);
extern	u_int ar5210GetSlotTime(struct ath_hal *);
extern	HAL_BOOL ar5210SetAckTimeout(struct ath_hal *, u_int);
extern	u_int ar5210GetAckTimeout(struct ath_hal *);
extern	HAL_BOOL ar5210SetAckCTSRate(struct ath_hal *, u_int);
extern	u_int ar5210GetAckCTSRate(struct ath_hal *);
extern	HAL_BOOL ar5210SetCTSTimeout(struct ath_hal *, u_int);
extern	u_int ar5210GetCTSTimeout(struct ath_hal *);
extern  HAL_BOOL ar5210SetDecompMask(struct ath_hal *, uint16_t, int);
void 	ar5210SetCoverageClass(struct ath_hal *, uint8_t, int);
extern	HAL_STATUS ar5210SetQuiet(struct ath_hal *, uint32_t, uint32_t,
		uint32_t, HAL_QUIET_FLAG);
extern	HAL_STATUS ar5210GetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t *);
extern	HAL_BOOL ar5210SetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t, HAL_STATUS *);
extern	HAL_BOOL ar5210GetDiagState(struct ath_hal *ah, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize);
extern	uint32_t ar5210Get11nExtBusy(struct ath_hal *);
extern	HAL_BOOL ar5210GetMibCycleCounts(struct ath_hal *,
		HAL_SURVEY_SAMPLE *);
extern	void ar5210SetChainMasks(struct ath_hal *, uint32_t, uint32_t);
extern	void ar5210EnableDfs(struct ath_hal *, HAL_PHYERR_PARAM *);
extern	void ar5210GetDfsThresh(struct ath_hal *, HAL_PHYERR_PARAM *);
extern	void ar5210UpdateDiagReg(struct ath_hal *ah, uint32_t val);

extern	u_int ar5210GetKeyCacheSize(struct ath_hal *);
extern	HAL_BOOL ar5210IsKeyCacheEntryValid(struct ath_hal *, uint16_t);
extern	HAL_BOOL ar5210ResetKeyCacheEntry(struct ath_hal *, uint16_t entry);
extern	HAL_BOOL ar5210SetKeyCacheEntry(struct ath_hal *, uint16_t entry,
                       const HAL_KEYVAL *, const uint8_t *mac, int xorKey);
extern	HAL_BOOL ar5210SetKeyCacheEntryMac(struct ath_hal *,
			uint16_t, const uint8_t *);

extern	HAL_BOOL ar5210SetPowerMode(struct ath_hal *, uint32_t powerRequest,
		int setChip);
extern	HAL_POWER_MODE ar5210GetPowerMode(struct ath_hal *);

extern	void ar5210SetBeaconTimers(struct ath_hal *,
		const HAL_BEACON_TIMERS *);
extern	void ar5210BeaconInit(struct ath_hal *, uint32_t, uint32_t);
extern	void ar5210SetStaBeaconTimers(struct ath_hal *,
		const HAL_BEACON_STATE *);
extern	void ar5210ResetStaBeaconTimers(struct ath_hal *);
extern	uint64_t ar5210GetNextTBTT(struct ath_hal *);

extern	HAL_BOOL ar5210IsInterruptPending(struct ath_hal *);
extern	HAL_BOOL ar5210GetPendingInterrupts(struct ath_hal *, HAL_INT *);
extern	HAL_INT ar5210GetInterrupts(struct ath_hal *);
extern	HAL_INT ar5210SetInterrupts(struct ath_hal *, HAL_INT ints);

extern	const HAL_RATE_TABLE *ar5210GetRateTable(struct ath_hal *, u_int mode);

extern	HAL_BOOL ar5210AniControl(struct ath_hal *, HAL_ANI_CMD, int );
extern	void ar5210AniPoll(struct ath_hal *, const struct ieee80211_channel *);
extern	void ar5210RxMonitor(struct ath_hal *, const HAL_NODE_STATS *,
		const struct ieee80211_channel *);
extern	void ar5210MibEvent(struct ath_hal *, const HAL_NODE_STATS *);
#endif /* _ATH_AR5210_H_ */
