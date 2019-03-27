#ifndef	__AR9300_FREEBSD_H__
#define	__AR9300_FREEBSD_H__

extern	void ar9300_attach_freebsd_ops(struct ath_hal *ah);
extern	HAL_BOOL ar9300_reset_freebsd(struct ath_hal *ah, HAL_OPMODE opmode,
	    struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
	    HAL_RESET_TYPE resetType, HAL_STATUS *status);
extern	void ar9300_config_pcie_freebsd(struct ath_hal *, HAL_BOOL, HAL_BOOL);
extern	HAL_STATUS ar9300_eeprom_get_freebsd(struct ath_hal *, int param,
	    void *val);
extern	HAL_BOOL ar9300_stop_tx_dma_freebsd(struct ath_hal *ah, u_int q);
extern	void ar9300_ani_poll_freebsd(struct ath_hal *ah,
	    const struct ieee80211_channel *chan);
extern	void ar9300_config_defaults_freebsd(struct ath_hal *ah,
	    HAL_OPS_CONFIG *ah_config);
extern	HAL_BOOL ar9300_stop_dma_receive_freebsd(struct ath_hal *ah);
extern	HAL_BOOL ar9300_get_pending_interrupts_freebsd(struct ath_hal *ah,
	    HAL_INT *masked);
extern	HAL_INT ar9300_set_interrupts_freebsd(struct ath_hal *ah,
	    HAL_INT mask);
extern	HAL_BOOL ar9300_per_calibration_freebsd(struct ath_hal *ah,
	    struct ieee80211_channel *chan, u_int rxchainmask,
	    HAL_BOOL longCal, HAL_BOOL *isCalDone);
extern	HAL_BOOL ar9300_reset_cal_valid_freebsd(struct ath_hal *ah,
	    const struct ieee80211_channel *chan);
extern	void ar9300_start_pcu_receive_freebsd(struct ath_hal *ah);
extern	HAL_STATUS ar9300_proc_rx_desc_freebsd(struct ath_hal *ah,
	    struct ath_desc *ds, uint32_t pa, struct ath_desc *ds_next,
	    uint64_t tsf, struct ath_rx_status *rxs);
extern	void ar9300_ani_rxmonitor_freebsd(struct ath_hal *ah,
	    const HAL_NODE_STATS *stats, const struct ieee80211_channel *chan);
extern	void ar9300_freebsd_get_desc_link(struct ath_hal *, void *ds,
	    uint32_t *);

extern	HAL_BOOL ar9300_freebsd_setup_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type,
	    u_int txPower, u_int txRate0, u_int txTries0, u_int keyIx,
	    u_int antMode, u_int flags, u_int rtsctsRate, u_int rtsCtsDuration,
	    u_int compicvLen, u_int compivLen, u_int comp);
extern	HAL_BOOL ar9300_freebsd_setup_x_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, u_int txRate1, u_int txTries1,
	    u_int txRate2, u_int txTries2, u_int txRate3, u_int txTries3);
extern	HAL_BOOL ar9300_freebsd_fill_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, HAL_DMA_ADDR *bufAddrList,
	    uint32_t *segLenList, u_int descId, u_int qId, HAL_BOOL firstSeg,
	    HAL_BOOL lastSeg, const struct ath_desc *ds0);
extern	HAL_BOOL ar9300_freebsd_get_tx_completion_rates(struct ath_hal *ah,
	    const struct ath_desc *ds0, int *rates, int *tries);
extern	void ar9300_freebsd_set_11n_rate_scenario(struct ath_hal *,
	    struct ath_desc *, u_int, u_int, HAL_11N_RATE_SERIES series[],
	    u_int, u_int);

extern	HAL_BOOL ar9300_freebsd_chain_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds,
	    HAL_DMA_ADDR *bufAddrList,
	    uint32_t *segLenList,
	    u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type,
	    u_int keyIx, HAL_CIPHER cipher, uint8_t numDelims,
	    HAL_BOOL firstSeg, HAL_BOOL lastSeg, HAL_BOOL lastAggr);
extern	HAL_BOOL ar9300_freebsd_setup_first_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, u_int aggrLen, u_int flags, u_int txPower,
	    u_int txRate0, u_int txTries0, u_int antMode, u_int rtsctsRate,
	    u_int rtsctsDuration);
extern	HAL_BOOL ar9300_freebsd_setup_last_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, const struct ath_desc *ds0);

extern	void ar9300_freebsd_setup_11n_desc(struct ath_hal *ah,
	    void *ds, u_int pktLen, HAL_PKT_TYPE type, u_int txPower,
	    u_int keyIx, u_int flags);

extern	HAL_STATUS ar9300_freebsd_proc_tx_desc(struct ath_hal *ah,
	    struct ath_desc *ds, struct ath_tx_status *ts);

extern	void ar9300_freebsd_beacon_init(struct ath_hal *ah,
	    uint32_t next_beacon, uint32_t beacon_period);

extern	HAL_BOOL ar9300_freebsd_get_mib_cycle_counts(struct ath_hal *ah,
	    HAL_SURVEY_SAMPLE *);

extern	HAL_BOOL ar9300_freebsd_get_dfs_default_thresh(struct ath_hal *ah,
	    HAL_PHYERR_PARAM *pe);

#endif	/* __AR9300_FREEBSD_H__ */
