/*
 * Copyright (c) 2012, 2013 Adrian Chadd <adrian@FreeBSD.org>.
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
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"
#include "ah_desc.h"

#include "ar9300.h"
#include "ar9300reg.h"
#include "ar9300phy.h"
#include "ar9300desc.h"

#include "ar9300_freebsd.h"

#include "ar9300_stub.h"
#include "ar9300_stub_funcs.h"

#define FIX_NOISE_FLOOR     1
#define NEXT_TBTT_NOW      5 
static HAL_BOOL ar9300ClrMulticastFilterIndex(struct ath_hal *ah, uint32_t ix);
static HAL_BOOL ar9300SetMulticastFilterIndex(struct ath_hal *ah, uint32_t ix);

static void ar9300_beacon_set_beacon_timers(struct ath_hal *ah,
    const HAL_BEACON_TIMERS *bt);

static void
ar9300SetChainMasks(struct ath_hal *ah, uint32_t tx_chainmask,
    uint32_t rx_chainmask)
{

	AH9300(ah)->ah_tx_chainmask =
	    tx_chainmask & AH_PRIVATE(ah)->ah_caps.halTxChainMask;
	AH9300(ah)->ah_rx_chainmask =
	    rx_chainmask & AH_PRIVATE(ah)->ah_caps.halRxChainMask;
}

static u_int
ar9300GetSlotTime(struct ath_hal *ah)
{
	u_int clks = OS_REG_READ(ah, AR_D_GBL_IFS_SLOT) & 0xffff;
	return (ath_hal_mac_usec(ah, clks));	/* convert from system clocks */
}

static HAL_BOOL
ar9300_freebsd_set_tx_power_limit(struct ath_hal *ah, uint32_t limit)
{
	return (ar9300_set_tx_power_limit(ah, limit, 0, 0));
}

static uint64_t
ar9300_get_next_tbtt(struct ath_hal *ah)
{
	return (OS_REG_READ(ah, AR_NEXT_TBTT_TIMER));
}


/*
 * TODO: implement the antenna diversity control for AR9485 and
 * other LNA mixing based NICs.
 *
 * For now we'll just go with the HAL default and make these no-ops.
 */
static HAL_ANT_SETTING
ar9300_freebsd_get_antenna_switch(struct ath_hal *ah)
{

	return (HAL_ANT_VARIABLE);
}

static HAL_BOOL
ar9300_freebsd_set_antenna_switch(struct ath_hal *ah, HAL_ANT_SETTING setting)
{

	return (AH_TRUE);
}

static u_int
ar9300_freebsd_get_cts_timeout(struct ath_hal *ah)
{
    u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_CTS);
    return ath_hal_mac_usec(ah, clks);      /* convert from system clocks */
}

static void
ar9300_freebsd_set_tsf64(struct ath_hal *ah, uint64_t tsf64)
{

	/*
	 * XXX TODO: read ar5416SetTsf64() - we should wait before we do
	 * this.
	 */
	OS_REG_WRITE(ah, AR_TSF_L32, tsf64 & 0xffffffff);
	OS_REG_WRITE(ah, AR_TSF_U32, (tsf64 >> 32) & 0xffffffff);
}

/* Flags for pulse_bw_info */
#define	PRI_CH_RADAR_FOUND		0x01
#define	EXT_CH_RADAR_FOUND		0x02
#define	EXT_CH_RADAR_EARLY_FOUND	0x04

static HAL_BOOL
ar9300_freebsd_proc_radar_event(struct ath_hal *ah, struct ath_rx_status *rxs,
    uint64_t fulltsf, const char *buf, HAL_DFS_EVENT *event)
{
	HAL_BOOL doDfsExtCh;
	HAL_BOOL doDfsEnhanced;
	HAL_BOOL doDfsCombinedRssi;

	uint8_t rssi = 0, ext_rssi = 0;
	uint8_t pulse_bw_info = 0, pulse_length_ext = 0, pulse_length_pri = 0;
	uint32_t dur = 0;
	int pri_found = 1, ext_found = 0;
	int early_ext = 0;
	int is_dc = 0;
	uint16_t datalen;		/* length from the RX status field */

	/* Check whether the given phy error is a radar event */
	if ((rxs->rs_phyerr != HAL_PHYERR_RADAR) &&
	    (rxs->rs_phyerr != HAL_PHYERR_FALSE_RADAR_EXT)) {
		return AH_FALSE;
	}

	/* Grab copies of the capabilities; just to make the code clearer */
	doDfsExtCh = AH_PRIVATE(ah)->ah_caps.halExtChanDfsSupport;
	doDfsEnhanced = AH_PRIVATE(ah)->ah_caps.halEnhancedDfsSupport;
	doDfsCombinedRssi = AH_PRIVATE(ah)->ah_caps.halUseCombinedRadarRssi;

	datalen = rxs->rs_datalen;

	/* If hardware supports it, use combined RSSI, else use chain 0 RSSI */
	if (doDfsCombinedRssi)
		rssi = (uint8_t) rxs->rs_rssi;
	else		
		rssi = (uint8_t) rxs->rs_rssi_ctl[0];

	/* Set this; but only use it if doDfsExtCh is set */
	ext_rssi = (uint8_t) rxs->rs_rssi_ext[0];

	/* Cap it at 0 if the RSSI is a negative number */
	if (rssi & 0x80)
		rssi = 0;

	if (ext_rssi & 0x80)
		ext_rssi = 0;

	/*
	 * Fetch the relevant data from the frame
	 */
	if (doDfsExtCh) {
		if (datalen < 3)
			return AH_FALSE;

		/* Last three bytes of the frame are of interest */
		pulse_length_pri = *(buf + datalen - 3);
		pulse_length_ext = *(buf + datalen - 2);
		pulse_bw_info = *(buf + datalen - 1);
		HALDEBUG(ah, HAL_DEBUG_DFS, "%s: rssi=%d, ext_rssi=%d, pulse_length_pri=%d,"
		    " pulse_length_ext=%d, pulse_bw_info=%x\n",
		    __func__, rssi, ext_rssi, pulse_length_pri, pulse_length_ext,
		    pulse_bw_info);
	} else {
		/* The pulse width is byte 0 of the data */
		if (datalen >= 1)
			dur = ((uint8_t) buf[0]) & 0xff;
		else
			dur = 0;

		if (dur == 0 && rssi == 0) {
			HALDEBUG(ah, HAL_DEBUG_DFS, "%s: dur and rssi are 0\n", __func__);
			return AH_FALSE;
		}

		HALDEBUG(ah, HAL_DEBUG_DFS, "%s: rssi=%d, dur=%d\n", __func__, rssi, dur);

		/* Single-channel only */
		pri_found = 1;
		ext_found = 0;
	}

	/*
	 * If doing extended channel data, pulse_bw_info must
	 * have one of the flags set.
	 */
	if (doDfsExtCh && pulse_bw_info == 0x0)
		return AH_FALSE;
		
	/*
	 * If the extended channel data is available, calculate
	 * which to pay attention to.
	 */
	if (doDfsExtCh) {
		/* If pulse is on DC, take the larger duration of the two */
		if ((pulse_bw_info & EXT_CH_RADAR_FOUND) &&
		    (pulse_bw_info & PRI_CH_RADAR_FOUND)) {
			is_dc = 1;
			if (pulse_length_ext > pulse_length_pri) {
				dur = pulse_length_ext;
				pri_found = 0;
				ext_found = 1;
			} else {
				dur = pulse_length_pri;
				pri_found = 1;
				ext_found = 0;
			}
		} else if (pulse_bw_info & EXT_CH_RADAR_EARLY_FOUND) {
			dur = pulse_length_ext;
			pri_found = 0;
			ext_found = 1;
			early_ext = 1;
		} else if (pulse_bw_info & PRI_CH_RADAR_FOUND) {
			dur = pulse_length_pri;
			pri_found = 1;
			ext_found = 0;
		} else if (pulse_bw_info & EXT_CH_RADAR_FOUND) {
			dur = pulse_length_ext;
			pri_found = 0;
			ext_found = 1;
		}
		
	}

	/*
	 * For enhanced DFS (Merlin and later), pulse_bw_info has
	 * implications for selecting the correct RSSI value.
	 */
	if (doDfsEnhanced) {
		switch (pulse_bw_info & 0x03) {
		case 0:
			/* No radar? */
			rssi = 0;
			break;
		case PRI_CH_RADAR_FOUND:
			/* Radar in primary channel */
			/* Cannot use ctrl channel RSSI if ext channel is stronger */
			if (ext_rssi >= (rssi + 3)) {
				rssi = 0;
			}
			break;
		case EXT_CH_RADAR_FOUND:
			/* Radar in extended channel */
			/* Cannot use ext channel RSSI if ctrl channel is stronger */
			if (rssi >= (ext_rssi + 12)) {
				rssi = 0;
			} else {
				rssi = ext_rssi;
			}
			break;
		case (PRI_CH_RADAR_FOUND | EXT_CH_RADAR_FOUND):
			/* When both are present, use stronger one */
			if (rssi < ext_rssi)
				rssi = ext_rssi;
			break;
		}
	}

	/*
	 * If not doing enhanced DFS, choose the ext channel if
	 * it is stronger than the main channel
	 */
	if (doDfsExtCh && !doDfsEnhanced) {
		if ((ext_rssi > rssi) && (ext_rssi < 128))
			rssi = ext_rssi;
	}

	/*
	 * XXX what happens if the above code decides the RSSI
	 * XXX wasn't valid, an sets it to 0?
	 */

	/*
	 * Fill out dfs_event structure.
	 */
	event->re_full_ts = fulltsf;
	event->re_ts = rxs->rs_tstamp;
	event->re_rssi = rssi;
	event->re_dur = dur;

	event->re_flags = 0;
	if (pri_found)
		event->re_flags |= HAL_DFS_EVENT_PRICH;
	if (ext_found)
		event->re_flags |= HAL_DFS_EVENT_EXTCH;
	if (early_ext)
		event->re_flags |= HAL_DFS_EVENT_EXTEARLY;
	if (is_dc)
		event->re_flags |= HAL_DFS_EVENT_ISDC;

	return AH_TRUE;
}

void
ar9300_attach_freebsd_ops(struct ath_hal *ah)
{

	/* Global functions */
	ah->ah_detach		= ar9300_detach;
	ah->ah_getRateTable		= ar9300_get_rate_table;

	/* Reset functions */
	ah->ah_reset		= ar9300_reset_freebsd;
	ah->ah_phyDisable		= ar9300_phy_disable;
	ah->ah_disable		= ar9300_disable;
	ah->ah_configPCIE		= ar9300_config_pcie_freebsd;
//	ah->ah_disablePCIE		= ar9300_disable_pcie_phy;
	ah->ah_setPCUConfig		= ar9300_set_pcu_config;
	// perCalibration
	ah->ah_perCalibrationN	= ar9300_per_calibration_freebsd;
	ah->ah_resetCalValid	= ar9300_reset_cal_valid_freebsd;
	ah->ah_setTxPowerLimit	= ar9300_freebsd_set_tx_power_limit;
	ah->ah_getChanNoise		= ath_hal_getChanNoise;

	/* Transmit functions */
	ah->ah_setupTxQueue		= ar9300_setup_tx_queue;
	ah->ah_setTxQueueProps	= ar9300_set_tx_queue_props;
	ah->ah_getTxQueueProps	= ar9300_get_tx_queue_props;
	ah->ah_releaseTxQueue	= ar9300_release_tx_queue;
	ah->ah_resetTxQueue		= ar9300_reset_tx_queue;
	ah->ah_getTxDP		= ar9300_get_tx_dp;
	ah->ah_setTxDP		= ar9300_set_tx_dp;
	ah->ah_numTxPending		= ar9300_num_tx_pending;
	ah->ah_startTxDma		= ar9300_start_tx_dma;
	ah->ah_stopTxDma		= ar9300_stop_tx_dma_freebsd;
	ah->ah_setupTxDesc		= ar9300_freebsd_setup_tx_desc;
	ah->ah_setupXTxDesc		= ar9300_freebsd_setup_x_tx_desc;
	ah->ah_fillTxDesc		= ar9300_freebsd_fill_tx_desc;
	ah->ah_procTxDesc		= ar9300_freebsd_proc_tx_desc;
	ah->ah_getTxIntrQueue	= ar9300_get_tx_intr_queue;
	// reqTxIntrDesc
	ah->ah_getTxCompletionRates	= ar9300_freebsd_get_tx_completion_rates;
	ah->ah_setTxDescLink	= ar9300_set_desc_link;
	ah->ah_getTxDescLink	= ar9300_freebsd_get_desc_link;
	ah->ah_getTxDescLinkPtr	= ar9300_get_desc_link_ptr;
	ah->ah_setupTxStatusRing	= ar9300_setup_tx_status_ring;
	ah->ah_getTxRawTxDesc	 = ar9300_get_raw_tx_desc;
	ah->ah_updateTxTrigLevel	= ar9300_update_tx_trig_level;

	/* RX functions */
	ah->ah_getRxDP		= ar9300_get_rx_dp;
	ah->ah_setRxDP		= ar9300_set_rx_dp;
	ah->ah_enableReceive	= ar9300_enable_receive;
	ah->ah_stopDmaReceive	= ar9300_stop_dma_receive_freebsd;
	ah->ah_startPcuReceive	= ar9300_start_pcu_receive_freebsd;
	ah->ah_stopPcuReceive	= ar9300_stop_pcu_receive;
	ah->ah_setMulticastFilter	= ar9300_set_multicast_filter;
	ah->ah_setMulticastFilterIndex = ar9300SetMulticastFilterIndex;
	ah->ah_clrMulticastFilterIndex = ar9300ClrMulticastFilterIndex;
	ah->ah_getRxFilter		= ar9300_get_rx_filter;
	ah->ah_setRxFilter		= ar9300_set_rx_filter;
	/* setupRxDesc */
	ah->ah_procRxDesc		= ar9300_proc_rx_desc_freebsd;
	ah->ah_rxMonitor		= ar9300_ani_rxmonitor_freebsd;
	ah->ah_aniPoll		= ar9300_ani_poll_freebsd;
	ah->ah_procMibEvent		= ar9300_process_mib_intr;

	/* Misc functions */
	ah->ah_getCapability	= ar9300_get_capability;
	ah->ah_setCapability	= ar9300_set_capability;
	ah->ah_getDiagState		= ar9300_get_diag_state;
	ah->ah_getMacAddress	= ar9300_get_mac_address;
	ah->ah_setMacAddress	= ar9300_set_mac_address;
	ah->ah_getBssIdMask		= ar9300_get_bss_id_mask;
	ah->ah_setBssIdMask		= ar9300_set_bss_id_mask;
	ah->ah_setRegulatoryDomain	= ar9300_set_regulatory_domain;
	ah->ah_setLedState		= ar9300_set_led_state;
	ah->ah_writeAssocid		= ar9300_write_associd;
	ah->ah_gpioCfgInput		= ar9300_gpio_cfg_input;
	ah->ah_gpioCfgOutput	= ar9300_gpio_cfg_output;
	ah->ah_gpioGet		= ar9300_gpio_get;
	ah->ah_gpioSet		= ar9300_gpio_set;
	ah->ah_gpioSetIntr		= ar9300_gpio_set_intr;
	/* polarity */
	/* mask */
	ah->ah_getTsf32		= ar9300_get_tsf32;
	ah->ah_getTsf64		= ar9300_get_tsf64;
	ah->ah_resetTsf		= ar9300_reset_tsf;
	ah->ah_setTsf64		= ar9300_freebsd_set_tsf64;
	ah->ah_detectCardPresent	= ar9300_detect_card_present;
	// ah->ah_updateMibCounters	= ar9300_update_mib_counters;
	ah->ah_getRfGain		= ar9300_get_rfgain;
	ah->ah_getDefAntenna	= ar9300_get_def_antenna;
	ah->ah_setDefAntenna	= ar9300_set_def_antenna;
	ah->ah_getAntennaSwitch	= ar9300_freebsd_get_antenna_switch;
	ah->ah_setAntennaSwitch	= ar9300_freebsd_set_antenna_switch;
	// ah->ah_setSifsTime		= ar9300_set_sifs_time;
	// ah->ah_getSifsTime		= ar9300_get_sifs_time;
	ah->ah_setSlotTime		= ar9300_set_slot_time;
	ah->ah_getSlotTime		= ar9300GetSlotTime;
	ah->ah_getAckTimeout	= ar9300_get_ack_timeout;
	ah->ah_setAckTimeout	= ar9300_set_ack_timeout;
	// XXX ack/ctsrate
	// XXX CTS timeout
	ah->ah_getCTSTimeout = ar9300_freebsd_get_cts_timeout;
	// XXX decompmask
	// coverageclass
	ah->ah_setQuiet		= ar9300_set_quiet;
	ah->ah_getMibCycleCounts	= ar9300_freebsd_get_mib_cycle_counts;

	/* DFS functions */
	ah->ah_enableDfs		= ar9300_enable_dfs;
	ah->ah_getDfsThresh		= ar9300_get_dfs_thresh;
	ah->ah_getDfsDefaultThresh	= ar9300_get_default_dfs_thresh;
	ah->ah_procRadarEvent		= ar9300_freebsd_proc_radar_event;
	ah->ah_isFastClockEnabled	= ar9300_is_fast_clock_enabled;
	ah->ah_get11nExtBusy		= ar9300_get_11n_ext_busy;
	ah->ah_setDfsCacTxQuiet		= ar9300_cac_tx_quiet;

	/* Spectral Scan Functions */
	ah->ah_spectralConfigure	= ar9300_configure_spectral_scan;
	ah->ah_spectralGetConfig	= ar9300_get_spectral_params;
	ah->ah_spectralStart		= ar9300_start_spectral_scan;
	ah->ah_spectralStop		= ar9300_stop_spectral_scan;
	ah->ah_spectralIsEnabled	= ar9300_is_spectral_enabled;
	ah->ah_spectralIsActive		= ar9300_is_spectral_active;

	/* Key cache functions */
	ah->ah_getKeyCacheSize	= ar9300_get_key_cache_size;
	ah->ah_resetKeyCacheEntry	= ar9300_reset_key_cache_entry;
	ah->ah_isKeyCacheEntryValid	= ar9300_is_key_cache_entry_valid;
	ah->ah_setKeyCacheEntry	= ar9300_set_key_cache_entry;
	ah->ah_setKeyCacheEntryMac	= ar9300_set_key_cache_entry_mac;

	/* Power management functions */
	ah->ah_setPowerMode		= ar9300_set_power_mode;
	ah->ah_getPowerMode		= ar9300_get_power_mode;

	/* Beacon functions */
	/* ah_setBeaconTimers */
	ah->ah_beaconInit		= ar9300_freebsd_beacon_init;
	ah->ah_setBeaconTimers		= ar9300_beacon_set_beacon_timers;
	ah->ah_setStationBeaconTimers = ar9300_set_sta_beacon_timers;
	/* ah_resetStationBeaconTimers */
	ah->ah_getNextTBTT = ar9300_get_next_tbtt;

	/* Interrupt functions */
	ah->ah_isInterruptPending	= ar9300_is_interrupt_pending;
	ah->ah_getPendingInterrupts	= ar9300_get_pending_interrupts_freebsd;
	ah->ah_getInterrupts =	ar9300_get_interrupts;
	ah->ah_setInterrupts =	ar9300_set_interrupts_freebsd;

	/* Regulatory/internal functions */
	//    AH_PRIVATE(ah)->ah_getNfAdjust = ar9300_get_nf_adjust;
	AH_PRIVATE(ah)->ah_eepromRead = ar9300_eeprom_read_word;
	//    AH_PRIVATE(ah)->ah_getChipPowerLimits = ar9300_get_chip_power_limits;
	AH_PRIVATE(ah)->ah_getWirelessModes = ar9300_get_wireless_modes;
	AH_PRIVATE(ah)->ah_getChannelEdges = ar9300_get_channel_edges;

	AH_PRIVATE(ah)->ah_eepromRead = ar9300_eeprom_read_word;
	/* XXX ah_eeprom */
	/* XXX ah_eeversion */
	/* XXX ah_eepromDetach */
	/* XXX ah_eepromGet */
	AH_PRIVATE(ah)->ah_eepromGet = ar9300_eeprom_get_freebsd;
	/* XXX ah_eepromSet */
	/* XXX ah_getSpurChan */
	/* XXX ah_eepromDiag */

	/* 802.11n functions */
	ah->ah_chainTxDesc = ar9300_freebsd_chain_tx_desc;
	ah->ah_setupFirstTxDesc= ar9300_freebsd_setup_first_tx_desc;
	ah->ah_setupLastTxDesc = ar9300_freebsd_setup_last_tx_desc;
	ah->ah_set11nRateScenario = ar9300_freebsd_set_11n_rate_scenario;
	ah->ah_set11nTxDesc = ar9300_freebsd_setup_11n_desc;
	ah->ah_set11nAggrFirst = ar9300_set_11n_aggr_first;
	ah->ah_set11nAggrMiddle = ar9300_set_11n_aggr_middle;
	ah->ah_set11nAggrLast = ar9300_set_11n_aggr_last;
	ah->ah_clr11nAggr = ar9300_clr_11n_aggr;
	ah->ah_set11nBurstDuration = ar9300_set_11n_burst_duration;
	/* ah_get11nExtBusy */
	ah->ah_set11nMac2040 = ar9300_set_11n_mac2040;
	ah->ah_setChainMasks = ar9300SetChainMasks;
	/* ah_get11nRxClear */
	/* ah_set11nRxClear */

	/* bluetooth coexistence functions */
	ah->ah_btCoexSetInfo		= ar9300_set_bt_coex_info;
	ah->ah_btCoexSetConfig		= ar9300_bt_coex_config;
	ah->ah_btCoexSetQcuThresh	= ar9300_bt_coex_set_qcu_thresh;
	ah->ah_btCoexSetWeights		= ar9300_bt_coex_set_weights;
	ah->ah_btCoexSetBmissThresh	= ar9300_bt_coex_setup_bmiss_thresh;
	ah->ah_btCoexSetParameter	= ar9300_bt_coex_set_parameter;
	ah->ah_btCoexDisable		= ar9300_bt_coex_disable;
	ah->ah_btCoexEnable		= ar9300_bt_coex_enable;

	/* MCI bluetooth functions */
	if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
		/*
		 * Note: these are done in attach too for now, because
		 * at this point we haven't yet setup the mac/bb revision
		 * values, so this code is effectively NULL.
		 * However, I'm leaving this here so people digging
		 * into the code (a) see the MCI bits here, and (b)
		 * are now told they should look elsewhere for
		 * these methods.
		 */
		ah->ah_btCoexSetWeights = ar9300_mci_bt_coex_set_weights;
		ah->ah_btCoexDisable = ar9300_mci_bt_coex_disable;
		ah->ah_btCoexEnable = ar9300_mci_bt_coex_enable;
	}
	ah->ah_btMciSetup		= ar9300_mci_setup;
	ah->ah_btMciSendMessage		= ar9300_mci_send_message;
	ah->ah_btMciGetInterrupt	= ar9300_mci_get_interrupt;
	ah->ah_btMciState		= ar9300_mci_state;
	ah->ah_btMciDetach		= ar9300_mci_detach;

	/* LNA diversity functions */
	ah->ah_divLnaConfGet = ar9300_ant_div_comb_get_config;
	ah->ah_divLnaConfSet = ar9300_ant_div_comb_set_config;
}

HAL_BOOL
ar9300_reset_freebsd(struct ath_hal *ah, HAL_OPMODE opmode,
    struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
    HAL_RESET_TYPE resetType,
    HAL_STATUS *status)
{
	HAL_BOOL r;
	HAL_HT_MACMODE macmode;
	struct ath_hal_private  *ap  = AH_PRIVATE(ah);

	macmode =
	    IEEE80211_IS_CHAN_HT40(chan) ?
	        HAL_HT_MACMODE_2040 : HAL_HT_MACMODE_20;

	r = ar9300_reset(ah, opmode, chan, macmode,
	    ap->ah_caps.halTxChainMask,
	    ap->ah_caps.halRxChainMask,
	    HAL_HT_EXTPROTSPACING_20, /* always 20Mhz channel spacing */
	    bChannelChange,
	    status,
	    AH_FALSE);       /* XXX should really extend ath_hal_reset() */

	return (r);
}

void
ar9300_config_pcie_freebsd(struct ath_hal *ah, HAL_BOOL restore,
    HAL_BOOL powerOff)
{

	ar9300_config_pci_power_save(ah, restore ? 1 : 0, powerOff ? 1 : 0);
}

/*
 * This is a copy from ar9300_eeprom_get(), purely because the FreeBSD
 * API is very silly and inconsistent.
 *
 * The AR93xx HAL doesn't call the eepromGetFlag() function, so this
 * only occurs for FreeBSD code.
 *
 * When I fix this particular API, I'll undo this.
 */
HAL_STATUS
ar9300_eeprom_get_freebsd(struct ath_hal *ah, int param, void *val)
{

	switch (param) {
	case AR_EEP_FSTCLK_5G:
		return HAL_OK;
	default:
		ath_hal_printf(ah, "%s: called, param=%d\n",
		    __func__, param);
		return HAL_EIO;
	}
}

HAL_BOOL
ar9300_stop_tx_dma_freebsd(struct ath_hal *ah, u_int q)
{

	return ar9300_stop_tx_dma(ah, q, 1000);
}

void
ar9300_ani_poll_freebsd(struct ath_hal *ah,
    const struct ieee80211_channel *chan)
{

	HAL_NODE_STATS stats;
	HAL_ANISTATS anistats;
	HAL_SURVEY_SAMPLE survey;

	OS_MEMZERO(&stats, sizeof(stats));
	OS_MEMZERO(&anistats, sizeof(anistats));
	OS_MEMZERO(&survey, sizeof(survey));

	ar9300_ani_ar_poll(ah, &stats, chan, &anistats);

	/*
	 * If ANI stats are valid, use them to update the
	 * channel survey.
	 */
	if (anistats.valid) {
		survey.cycle_count = anistats.cyclecnt_diff;
		survey.chan_busy = anistats.rxclr_cnt;
		survey.ext_chan_busy = anistats.extrxclr_cnt;
		survey.tx_busy = anistats.txframecnt_diff;
		survey.rx_busy = anistats.rxframecnt_diff;
		ath_hal_survey_add_sample(ah, &survey);
	}
}

/*
 * Setup the configuration parameters in the style the AR9300 HAL
 * wants.
 */
void
ar9300_config_defaults_freebsd(struct ath_hal *ah, HAL_OPS_CONFIG *ah_config)
{

	/* Until FreeBSD's HAL does this by default - just copy */
	OS_MEMCPY(&ah->ah_config, ah_config, sizeof(HAL_OPS_CONFIG));
	ah->ah_config.ath_hal_enable_ani = AH_TRUE;
}

HAL_BOOL
ar9300_stop_dma_receive_freebsd(struct ath_hal *ah)
{

	return ar9300_stop_dma_receive(ah, 1000);
}

HAL_BOOL
ar9300_get_pending_interrupts_freebsd(struct ath_hal *ah, HAL_INT *masked)
{

	/* Non-MSI, so no MSI vector; and 'nortc' = 0 */
	return ar9300_get_pending_interrupts(ah, masked, HAL_INT_LINE, 0, 0);
}

HAL_INT
ar9300_set_interrupts_freebsd(struct ath_hal *ah, HAL_INT ints)
{

	/* nortc = 0 */
	return ar9300_set_interrupts(ah, ints, 0);
}

HAL_BOOL
ar9300_per_calibration_freebsd(struct ath_hal *ah,
    struct ieee80211_channel *chan, u_int rxchainmask, HAL_BOOL long_cal,
    HAL_BOOL *isCalDone)
{
	/* XXX fake scheduled calibrations for now */
	u_int32_t sched_cals = 0xfffffff;

	return ar9300_calibration(ah, chan,
	    AH_PRIVATE(ah)->ah_caps.halRxChainMask,
	    long_cal,
	    isCalDone,
	    0,			/* is_scan */
	    &sched_cals);
}

HAL_BOOL
ar9300_reset_cal_valid_freebsd(struct ath_hal *ah,
    const struct ieee80211_channel *chan)
{

	HAL_BOOL is_cal_done = AH_TRUE;
	
	ar9300_reset_cal_valid(ah, chan, &is_cal_done, 0xffffffff);
	return (is_cal_done);
}


void
ar9300_start_pcu_receive_freebsd(struct ath_hal *ah)
{

	/* is_scanning flag == NULL */
	ar9300_start_pcu_receive(ah, AH_FALSE);
}

/*
 * FreeBSD will just pass in the descriptor value as 'pa'.
 * The Atheros HAL treats 'pa' as the physical address of the RX
 * descriptor and 'bufaddr' as the physical address of the RX buffer.
 * I'm not sure why they didn't collapse them - the AR9300 RX descriptor
 * routine doesn't check 'pa'.
 */
HAL_STATUS
ar9300_proc_rx_desc_freebsd(struct ath_hal *ah, struct ath_desc *ds,
    uint32_t pa, struct ath_desc *ds_next, uint64_t tsf,
    struct ath_rx_status *rxs)
{

	return (ar9300_proc_rx_desc_fast(ah, ds, 0, ds_next, rxs,
	    (void *) ds));
}

/*
 * This is the primary way the ANI code gets the node statistics per packet.
 */
void
ar9300_ani_rxmonitor_freebsd(struct ath_hal *ah, const HAL_NODE_STATS *stats,
    const struct ieee80211_channel *chan)
{
	struct ath_hal_9300 *ahp = AH9300(ah);

	ahp->ah_stats.ast_nodestats.ns_avgbrssi = stats->ns_avgbrssi;
}

void
ar9300_freebsd_get_desc_link(struct ath_hal *ah, void *ds, uint32_t *link)
{
	struct ar9300_txc *ads = AR9300TXC(ds);

	(*link) = ads->ds_link;
}

/*
 * TX descriptor field setting wrappers - eek.
 */


HAL_BOOL
ar9300_freebsd_setup_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int txPower,
    u_int txRate0, u_int txTries0, u_int keyIx, u_int antMode, u_int flags,
    u_int rtsctsRate, u_int rtsCtsDuration, u_int compicvLen,
    u_int compivLen, u_int comp)
{
	struct ath_hal_9300 *ahp = AH9300(ah);

	HAL_KEY_TYPE keyType = 0;	/* XXX No padding */

	if (keyIx != HAL_TXKEYIX_INVALID)
		keyType = ahp->ah_keytype[keyIx];

	/* XXX bounds check keyix */
	ar9300_set_11n_tx_desc(ah, ds, pktLen, type, txPower, keyIx,
	    keyType, flags);

	return AH_TRUE;
}

HAL_BOOL
ar9300_freebsd_setup_x_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    u_int txRate1, u_int txTries1,
    u_int txRate2, u_int txTries2,
    u_int txRate3, u_int txTries3)
{

#if 0
	ath_hal_printf(ah, "%s: called, 0x%x/%d, 0x%x/%d, 0x%x/%d\n",
	    __func__,
	    txRate1, txTries1,
	    txRate2, txTries2,
	    txRate3, txTries3);
#endif

	/* XXX should only be called during probe */
	return (AH_TRUE);
}

HAL_BOOL
ar9300_freebsd_fill_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    HAL_DMA_ADDR *bufListPtr, uint32_t *segLenPtr, u_int descId, u_int qid,
    HAL_BOOL firstSeg, HAL_BOOL lastSeg,
    const struct ath_desc *ds0)
{
	HAL_KEY_TYPE keyType = 0;
	const struct ar9300_txc *ads = AR9300TXC_CONST(ds0);

	/*
	 * FreeBSD's HAL doesn't pass the keytype to fill_tx_desc();
	 * it's copied as part of the descriptor chaining.
	 *
	 * So, extract it from ds0.
	 */
	keyType = MS(ads->ds_ctl17, AR_encr_type);

	return ar9300_fill_tx_desc(ah, ds, bufListPtr, segLenPtr, descId,
	    qid, keyType, firstSeg, lastSeg, ds0);
}

HAL_BOOL
ar9300_freebsd_get_tx_completion_rates(struct ath_hal *ah,
    const struct ath_desc *ds0, int *rates, int *tries)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return AH_FALSE;	/* XXX for now */
}


/*
 * 802.11n TX descriptor wrappers
 */
void
ar9300_freebsd_set_11n_rate_scenario(struct ath_hal *ah, struct ath_desc *ds,
    u_int durUpdateEn, u_int rtsctsRate, HAL_11N_RATE_SERIES series[],
    u_int nseries, u_int flags)
{

	/* lastds=NULL, rtscts_duration is 0, smart antenna is 0 */
	ar9300_set_11n_rate_scenario(ah, (void *) ds, (void *)ds, durUpdateEn,
	    rtsctsRate, 0, series, nseries, flags, 0);
}

/* chaintxdesc */
HAL_BOOL
ar9300_freebsd_chain_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    HAL_DMA_ADDR *bufLenList, uint32_t *segLenList,
    u_int pktLen, u_int hdrLen, HAL_PKT_TYPE type, u_int keyIx,
    HAL_CIPHER cipher, uint8_t numDelims,
    HAL_BOOL firstSeg, HAL_BOOL lastSeg, HAL_BOOL lastAggr)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return AH_FALSE;
}

/* setupfirsttxdesc */
HAL_BOOL
ar9300_freebsd_setup_first_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    u_int aggrLen, u_int flags, u_int txPower, u_int txRate0,
    u_int txTries0, u_int antMode, u_int rtsctsRate, u_int rtsctsDuration)
{

	ath_hal_printf(ah, "%s: called\n", __func__);
	return AH_FALSE;
}

/* setuplasttxdesc */
/*
 * This gets called but for now let's not log anything;
 * it's only used to update the rate control information.
 */
HAL_BOOL
ar9300_freebsd_setup_last_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    const struct ath_desc *ds0)
{

//	ath_hal_printf(ah, "%s: called\n", __func__);
	return AH_FALSE;
}

void
ar9300_freebsd_setup_11n_desc(struct ath_hal *ah, void *ds, u_int pktLen,
    HAL_PKT_TYPE type, u_int txPower, u_int keyIx, u_int flags)
{
	ath_hal_printf(ah, "%s: called\n", __func__);
#if 0
	struct ath_hal_9300 *ahp = AH9300(ah);

	HAL_KEY_TYPE keyType = 0;	/* XXX No padding */

	if (keyIx != HAL_TXKEYIX_INVALID)
		keyType = ahp->ah_keytype[keyIx];

	/* XXX bounds check keyix */
	ar9300_set_11n_tx_desc(ah, ds, pktLen, type, txPower, keyIx,
	    keyType, flags);
#endif
}

HAL_STATUS
ar9300_freebsd_proc_tx_desc(struct ath_hal *ah, struct ath_desc *ds,
    struct ath_tx_status *ts)
{

	return ar9300_proc_tx_desc(ah, ts);
}

void
ar9300_freebsd_beacon_init(struct ath_hal *ah, uint32_t next_beacon,
    uint32_t beacon_period)
{

	ar9300_beacon_init(ah, next_beacon, beacon_period, 0,
	    AH_PRIVATE(ah)->ah_opmode);
}

HAL_BOOL
ar9300_freebsd_get_mib_cycle_counts(struct ath_hal *ah,
    HAL_SURVEY_SAMPLE *hs)

{

	return (AH_FALSE);
}

/*
 * Clear multicast filter by index - from FreeBSD ar5212_recv.c
 */
static HAL_BOOL
ar9300ClrMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{
	uint32_t val;

	if (ix >= 64)
		return (AH_FALSE);
	if (ix >= 32) {
		val = OS_REG_READ(ah, AR_MCAST_FIL1);
		OS_REG_WRITE(ah, AR_MCAST_FIL1, (val &~ (1<<(ix-32))));
	} else {
		val = OS_REG_READ(ah, AR_MCAST_FIL0);
		OS_REG_WRITE(ah, AR_MCAST_FIL0, (val &~ (1<<ix)));
	}
	return AH_TRUE;
}

/*
 * Set multicast filter by index - from FreeBSD ar5212_recv.c
 */
static HAL_BOOL
ar9300SetMulticastFilterIndex(struct ath_hal *ah, uint32_t ix)
{
	uint32_t val;

	if (ix >= 64)
		return (AH_FALSE);
	if (ix >= 32) {
		val = OS_REG_READ(ah, AR_MCAST_FIL1);
		OS_REG_WRITE(ah, AR_MCAST_FIL1, (val | (1<<(ix-32))));
	} else {
		val = OS_REG_READ(ah, AR_MCAST_FIL0);
		OS_REG_WRITE(ah, AR_MCAST_FIL0, (val | (1<<ix)));
	}
	return (AH_TRUE);
}

#define	TU_TO_USEC(_tu) ((_tu) << 10)
#define	ONE_EIGHTH_TU_TO_USEC(_tu8) ((_tu8) << 7)

/*
 * Initializes all of the hardware registers used to
 * send beacons.  Note that for station operation the
 * driver calls ar9300_set_sta_beacon_timers instead.
 */
static void
ar9300_beacon_set_beacon_timers(struct ath_hal *ah,
    const HAL_BEACON_TIMERS *bt)
{
	uint32_t bperiod;

#if 0
    HALASSERT(opmode == HAL_M_IBSS || opmode == HAL_M_HOSTAP);
    if (opmode == HAL_M_IBSS) {
        OS_REG_SET_BIT(ah, AR_TXCFG, AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
    }
#endif

	/* XXX TODO: should migrate the HAL code to always use ONE_EIGHTH_TU */
	OS_REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(bt->bt_nexttbtt));
	OS_REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, ONE_EIGHTH_TU_TO_USEC(bt->bt_nextdba));
	OS_REG_WRITE(ah, AR_NEXT_SWBA, ONE_EIGHTH_TU_TO_USEC(bt->bt_nextswba));
	OS_REG_WRITE(ah, AR_NEXT_NDP_TIMER, TU_TO_USEC(bt->bt_nextatim));

	bperiod = TU_TO_USEC(bt->bt_intval & HAL_BEACON_PERIOD);
	AH9300(ah)->ah_beaconInterval = bt->bt_intval & HAL_BEACON_PERIOD;
	OS_REG_WRITE(ah, AR_BEACON_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_DMA_BEACON_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_SWBA_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_NDP_PERIOD, bperiod);

	/*
	 * Reset TSF if required.
	 */
	if (bt->bt_intval & HAL_BEACON_RESET_TSF)
		ar9300_reset_tsf(ah);

	/* enable timers */
	/* NB: flags == 0 handled specially for backwards compatibility */
	OS_REG_SET_BIT(ah, AR_TIMER_MODE,
	    bt->bt_flags != 0 ? bt->bt_flags :
	    AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN);
}


/*
 * RF attach stubs
 */

static HAL_BOOL
rf9330_attach(struct ath_hal *ah, HAL_STATUS *status)
{

	(*status) = HAL_EINVAL;
	return (AH_FALSE);
}

static HAL_BOOL
rf9330_probe(struct ath_hal *ah)
{
	return (AH_FALSE);
}

AH_RF(RF9330, rf9330_probe, rf9330_attach);

