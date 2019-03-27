#ifndef	__AR9300_STUB_FUNCS_H__
#define	__AR9300_STUB_FUNCS_H__

extern	uint32_t ar9300_Stub_GetRadioRev(struct ath_hal *ah);

#if 0
extern	void ar9300_Stub_InitState(struct ath_hal_5212 *, uint16_t devid, HAL_SOFTC,
		HAL_BUS_TAG st, HAL_BUS_HANDLE sh, HAL_STATUS *status);
#endif
extern	void ar9300_Stub_Detach(struct ath_hal *ah);
extern  HAL_BOOL ar9300_Stub_ChipTest(struct ath_hal *ah);
extern  HAL_BOOL ar9300_Stub_GetChannelEdges(struct ath_hal *ah,
                uint16_t flags, uint16_t *low, uint16_t *high);
extern	HAL_BOOL ar9300_Stub_FillCapabilityInfo(struct ath_hal *ah);

extern	void ar9300_Stub_SetBeaconTimers(struct ath_hal *ah,
		const HAL_BEACON_TIMERS *);
extern	void ar9300_Stub_BeaconInit(struct ath_hal *ah,
		uint32_t next_beacon, uint32_t beacon_period);
extern	void ar9300_Stub_ResetStaBeaconTimers(struct ath_hal *ah);
extern	void ar9300_Stub_SetStaBeaconTimers(struct ath_hal *ah,
		const HAL_BEACON_STATE *);
extern	uint64_t ar9300_Stub_GetNextTBTT(struct ath_hal *);

extern	HAL_BOOL ar9300_Stub_IsInterruptPending(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_GetPendingInterrupts(struct ath_hal *ah, HAL_INT *);
extern	HAL_INT ar9300_Stub_GetInterrupts(struct ath_hal *ah);
extern	HAL_INT ar9300_Stub_SetInterrupts(struct ath_hal *ah, HAL_INT ints);

extern	uint32_t ar9300_Stub_GetKeyCacheSize(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_IsKeyCacheEntryValid(struct ath_hal *, uint16_t entry);
extern	HAL_BOOL ar9300_Stub_ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry);
extern	HAL_BOOL ar9300_Stub_SetKeyCacheEntryMac(struct ath_hal *,
			uint16_t entry, const uint8_t *mac);
extern	HAL_BOOL ar9300_Stub_SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
                       const HAL_KEYVAL *k, const uint8_t *mac, int xorKey);

extern	void ar9300_Stub_GetMacAddress(struct ath_hal *ah, uint8_t *mac);
extern	HAL_BOOL ar9300_Stub_SetMacAddress(struct ath_hal *ah, const uint8_t *);
extern	void ar9300_Stub_GetBssIdMask(struct ath_hal *ah, uint8_t *mac);
extern	HAL_BOOL ar9300_Stub_SetBssIdMask(struct ath_hal *, const uint8_t *);
extern	HAL_BOOL ar9300_Stub_EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar9300_Stub_EepromWrite(struct ath_hal *, u_int off, uint16_t data);
extern	HAL_BOOL ar9300_Stub_SetRegulatoryDomain(struct ath_hal *ah,
		uint16_t regDomain, HAL_STATUS *stats);
extern	u_int ar9300_Stub_GetWirelessModes(struct ath_hal *ah);
extern	void ar9300_Stub_EnableRfKill(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar9300_Stub_GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar9300_Stub_GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	uint32_t ar9300_Stub_GpioGet(struct ath_hal *ah, uint32_t gpio);
extern	void ar9300_Stub_GpioSetIntr(struct ath_hal *ah, u_int, uint32_t ilevel);
extern	void ar9300_Stub_SetLedState(struct ath_hal *ah, HAL_LED_STATE state);
extern	void ar9300_Stub_WriteAssocid(struct ath_hal *ah, const uint8_t *bssid,
		uint16_t assocId);
extern	uint32_t ar9300_Stub_GetTsf32(struct ath_hal *ah);
extern	uint64_t ar9300_Stub_GetTsf64(struct ath_hal *ah);
extern	void ar9300_Stub_SetTsf64(struct ath_hal *ah, uint64_t tsf64);
extern	void ar9300_Stub_ResetTsf(struct ath_hal *ah);
extern	void ar9300_Stub_SetBasicRate(struct ath_hal *ah, HAL_RATE_SET *pSet);
extern	uint32_t ar9300_Stub_GetRandomSeed(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_DetectCardPresent(struct ath_hal *ah);
extern	void ar9300_Stub_EnableMibCounters(struct ath_hal *);
extern	void ar9300_Stub_DisableMibCounters(struct ath_hal *);
extern	void ar9300_Stub_UpdateMibCounters(struct ath_hal *ah, HAL_MIB_STATS* stats);
extern	HAL_BOOL ar9300_Stub_IsJapanChannelSpreadSupported(struct ath_hal *ah);
extern	uint32_t ar9300_Stub_GetCurRssi(struct ath_hal *ah);
extern	u_int ar9300_Stub_GetDefAntenna(struct ath_hal *ah);
extern	void ar9300_Stub_SetDefAntenna(struct ath_hal *ah, u_int antenna);
extern	HAL_ANT_SETTING ar9300_Stub_GetAntennaSwitch(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);
extern	HAL_BOOL ar9300_Stub_IsSleepAfterBeaconBroken(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_SetSifsTime(struct ath_hal *, u_int);
extern	u_int ar9300_Stub_GetSifsTime(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_SetSlotTime(struct ath_hal *, u_int);
extern	u_int ar9300_Stub_GetSlotTime(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_SetAckTimeout(struct ath_hal *, u_int);
extern	u_int ar9300_Stub_GetAckTimeout(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_SetAckCTSRate(struct ath_hal *, u_int);
extern	u_int ar9300_Stub_GetAckCTSRate(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_SetCTSTimeout(struct ath_hal *, u_int);
extern	u_int ar9300_Stub_GetCTSTimeout(struct ath_hal *);
extern  HAL_BOOL ar9300_Stub_SetDecompMask(struct ath_hal *, uint16_t, int);
void 	ar9300_Stub_SetCoverageClass(struct ath_hal *, uint8_t, int);
extern	void ar9300_Stub_SetPCUConfig(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_Use32KHzclock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	void ar9300_Stub_SetupClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	void ar9300_Stub_RestoreClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern	int16_t ar9300_Stub_GetNfAdjust(struct ath_hal *,
		const HAL_CHANNEL_INTERNAL *);
extern	void ar9300_Stub_SetCompRegs(struct ath_hal *ah);
extern	HAL_STATUS ar9300_Stub_GetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t *);
extern	HAL_BOOL ar9300_Stub_SetCapability(struct ath_hal *, HAL_CAPABILITY_TYPE,
		uint32_t, uint32_t, HAL_STATUS *);
extern	HAL_BOOL ar9300_Stub_GetDiagState(struct ath_hal *ah, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize);
extern	HAL_STATUS ar9300_Stub_SetQuiet(struct ath_hal *ah, uint32_t period,
		uint32_t duration, uint32_t nextStart, HAL_QUIET_FLAG flag);
extern	HAL_BOOL ar9300_Stub_GetMibCycleCounts(struct ath_hal *,
		HAL_SURVEY_SAMPLE *);

extern	HAL_BOOL ar9300_Stub_SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode,
		int setChip);
extern	HAL_POWER_MODE ar9300_Stub_GetPowerMode(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_GetPowerStatus(struct ath_hal *ah);

extern	uint32_t ar9300_Stub_GetRxDP(struct ath_hal *ath, HAL_RX_QUEUE);
extern	void ar9300_Stub_SetRxDP(struct ath_hal *ah, uint32_t rxdp,
	    HAL_RX_QUEUE);
extern	void ar9300_Stub_EnableReceive(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_StopDmaReceive(struct ath_hal *ah);
extern	void ar9300_Stub_StartPcuReceive(struct ath_hal *ah);
extern	void ar9300_Stub_StopPcuReceive(struct ath_hal *ah);
extern	void ar9300_Stub_SetMulticastFilter(struct ath_hal *ah,
		uint32_t filter0, uint32_t filter1);
extern	HAL_BOOL ar9300_Stub_ClrMulticastFilterIndex(struct ath_hal *, uint32_t ix);
extern	HAL_BOOL ar9300_Stub_SetMulticastFilterIndex(struct ath_hal *, uint32_t ix);
extern	uint32_t ar9300_Stub_GetRxFilter(struct ath_hal *ah);
extern	void ar9300_Stub_SetRxFilter(struct ath_hal *ah, uint32_t bits);
extern	HAL_BOOL ar9300_Stub_SetupRxDesc(struct ath_hal *,
		struct ath_desc *, uint32_t size, u_int flags);
extern	HAL_STATUS ar9300_Stub_ProcRxDesc(struct ath_hal *ah, struct ath_desc *,
		uint32_t, struct ath_desc *, uint64_t,
		struct ath_rx_status *);

extern	HAL_BOOL ar9300_Stub_Reset(struct ath_hal *ah, HAL_OPMODE opmode,
		struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
		HAL_RESET_TYPE resetType,
		HAL_STATUS *status);
extern	HAL_BOOL ar9300_Stub_SetChannel(struct ath_hal *,
		const struct ieee80211_channel *);
extern	void ar9300_Stub_SetOperatingMode(struct ath_hal *ah, int opmode);
extern	HAL_BOOL ar9300_Stub_PhyDisable(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_Disable(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_ChipReset(struct ath_hal *ah,
		const struct ieee80211_channel *);
extern	HAL_BOOL ar9300_Stub_PerCalibration(struct ath_hal *ah,
		struct ieee80211_channel *chan, HAL_BOOL *isIQdone);
extern	HAL_BOOL ar9300_Stub_PerCalibrationN(struct ath_hal *ah,
		struct ieee80211_channel *chan, u_int chainMask,
		HAL_BOOL longCal, HAL_BOOL *isCalDone);
extern	HAL_BOOL ar9300_Stub_ResetCalValid(struct ath_hal *ah,
		const struct ieee80211_channel *);
extern	int16_t ar9300_Stub_GetNoiseFloor(struct ath_hal *ah);
extern	void ar9300_Stub_InitNfCalHistBuffer(struct ath_hal *);
extern	int16_t ar9300_Stub_GetNfHistMid(const int16_t calData[]);
extern	void ar9300_Stub_SetSpurMitigation(struct ath_hal *,
		 const struct ieee80211_channel *);
extern	HAL_BOOL ar9300_Stub_SetAntennaSwitchInternal(struct ath_hal *ah,
		HAL_ANT_SETTING settings, const struct ieee80211_channel *);
extern	HAL_BOOL ar9300_Stub_SetTxPowerLimit(struct ath_hal *ah, uint32_t limit);
extern	HAL_BOOL ar9300_Stub_GetChipPowerLimits(struct ath_hal *ah,
		struct ieee80211_channel *chan);
extern	void ar9300_Stub_InitializeGainValues(struct ath_hal *);
extern	HAL_RFGAIN ar9300_Stub_GetRfgain(struct ath_hal *ah);
extern	void ar9300_Stub_RequestRfgain(struct ath_hal *);

extern	HAL_BOOL ar9300_Stub_UpdateTxTrigLevel(struct ath_hal *,
		HAL_BOOL IncTrigLevel);
extern  HAL_BOOL ar9300_Stub_SetTxQueueProps(struct ath_hal *ah, int q,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar9300_Stub_GetTxQueueProps(struct ath_hal *ah, int q,
		HAL_TXQ_INFO *qInfo);
extern	int ar9300_Stub_SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
		const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ar9300_Stub_ReleaseTxQueue(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar9300_Stub_ResetTxQueue(struct ath_hal *ah, u_int q);
extern	uint32_t ar9300_Stub_GetTxDP(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar9300_Stub_SetTxDP(struct ath_hal *ah, u_int q, uint32_t txdp);
extern	HAL_BOOL ar9300_Stub_StartTxDma(struct ath_hal *ah, u_int q);
extern	uint32_t ar9300_Stub_NumTxPending(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar9300_Stub_StopTxDma(struct ath_hal *ah, u_int q);
extern	HAL_BOOL ar9300_Stub_SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
		u_int txRate0, u_int txTries0,
		u_int keyIx, u_int antMode, u_int flags,
		u_int rtsctsRate, u_int rtsctsDuration,
		u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar9300_Stub_SetupXTxDesc(struct ath_hal *, struct ath_desc *,
		u_int txRate1, u_int txRetries1,
		u_int txRate2, u_int txRetries2,
		u_int txRate3, u_int txRetries3);
extern	HAL_BOOL ar9300_Stub_FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
		u_int descId, u_int qcuId,
		HAL_BOOL firstSeg, HAL_BOOL lastSeg,
		const struct ath_desc *ds0);
extern	HAL_STATUS ar9300_Stub_ProcTxDesc(struct ath_hal *ah,
		struct ath_desc *, struct ath_tx_status *);
extern  void ar9300_Stub_GetTxIntrQueue(struct ath_hal *ah, uint32_t *);
extern  void ar9300_Stub_IntrReqTxDesc(struct ath_hal *ah, struct ath_desc *);
extern	HAL_BOOL ar9300_Stub_GetTxCompletionRates(struct ath_hal *ah,
		const struct ath_desc *ds0, int *rates, int *tries);

extern	const HAL_RATE_TABLE * ar9300_Stub_GetRateTable(struct ath_hal *, u_int mode);

#if 0
extern	void ar9300_Stub_AniAttach(struct ath_hal *, const struct ar9300_Stub_AniParams *,
		const struct ar9300_Stub_AniParams *, HAL_BOOL ena);
#endif
extern	void ar9300_Stub_AniDetach(struct ath_hal *);
extern	struct ar9300_Stub_AniState *ar5212AniGetCurrentState(struct ath_hal *);
extern	struct ar9300_Stub_Stats *ar5212AniGetCurrentStats(struct ath_hal *);
extern	HAL_BOOL ar9300_Stub_AniControl(struct ath_hal *, HAL_ANI_CMD cmd, int param);
#if 0
extern	HAL_BOOL ar9300_Stub_AniSetParams(struct ath_hal *,
		const struct ar9300_Stub_AniParams *, const struct ar9300_Stub_AniParams *);
#endif
struct ath_rx_status;
extern	void ar9300_Stub_AniPhyErrReport(struct ath_hal *ah,
		const struct ath_rx_status *rs);
extern	void ar9300_Stub_ProcessMibIntr(struct ath_hal *, const HAL_NODE_STATS *);
extern	void ar9300_Stub_RxMonitor(struct ath_hal *, const HAL_NODE_STATS *,
			     const struct ieee80211_channel *);
extern	void ar9300_Stub_AniPoll(struct ath_hal *, const struct ieee80211_channel *);
extern	void ar9300_Stub_AniReset(struct ath_hal *, const struct ieee80211_channel *,
		HAL_OPMODE, int);

extern	HAL_BOOL ar9300_Stub_IsNFCalInProgress(struct ath_hal *ah);
extern	HAL_BOOL ar9300_Stub_WaitNFCalComplete(struct ath_hal *ah, int i);
extern	void ar9300_Stub_EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	void ar9300_Stub_GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern	HAL_BOOL ar9300_Stub_ProcessRadarEvent(struct ath_hal *ah,
	    struct ath_rx_status *rxs, uint64_t fulltsf, const char *buf,
	    HAL_DFS_EVENT *event);
extern	HAL_BOOL ar9300_Stub_IsFastClockEnabled(struct ath_hal *ah);
extern	uint32_t ar9300_Stub_Get11nExtBusy(struct ath_hal *ah);

extern	void ar9300_Stub_ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
	HAL_BOOL powerOff);
extern	void ar9300_Stub_DisablePCIE(struct ath_hal *ah);



#endif	/* __AR9300_STUB_FUNCS_H__ */
