/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2010-2011 Atheros Communications, Inc.
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
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"
#include "ah_desc.h"                    /* NB: for HAL_PHYERR* */

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#include "ah_eeprom_v14.h"	/* for owl_get_ntxchains() */

/*
 * These are default parameters for the AR5416 and
 * later 802.11n NICs.  They simply enable some
 * radar pulse event generation.
 *
 * These are very likely not valid for the AR5212 era
 * NICs.
 *
 * Since these define signal sizing and threshold
 * parameters, they may need changing based on the
 * specific antenna and receive amplifier
 * configuration.
 */
#define	AR5416_DFS_FIRPWR	-33
#define	AR5416_DFS_RRSSI	20
#define	AR5416_DFS_HEIGHT	10
#define	AR5416_DFS_PRSSI	15
#define	AR5416_DFS_INBAND	15
#define	AR5416_DFS_RELPWR	8
#define	AR5416_DFS_RELSTEP	12
#define	AR5416_DFS_MAXLEN	255

HAL_BOOL
ar5416GetDfsDefaultThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{

	/*
	 * These are general examples of the parameter values
	 * to use when configuring radar pulse detection for
	 * the AR5416, AR91xx, AR92xx NICs.  They are only
	 * for testing and do require tuning depending upon the
	 * hardware and deployment specifics.
	 */
	pe->pe_firpwr = AR5416_DFS_FIRPWR;
	pe->pe_rrssi = AR5416_DFS_RRSSI;
	pe->pe_height = AR5416_DFS_HEIGHT;
	pe->pe_prssi = AR5416_DFS_PRSSI;
	pe->pe_inband = AR5416_DFS_INBAND;
	pe->pe_relpwr = AR5416_DFS_RELPWR;
	pe->pe_relstep = AR5416_DFS_RELSTEP;
	pe->pe_maxlen = AR5416_DFS_MAXLEN;

	return (AH_TRUE);
}

/*
 * Get the radar parameter values and return them in the pe
 * structure
 */
void
ar5416GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
	uint32_t val, temp;

	val = OS_REG_READ(ah, AR_PHY_RADAR_0);

	temp = MS(val,AR_PHY_RADAR_0_FIRPWR);
	temp |= 0xFFFFFF80;
	pe->pe_firpwr = temp;
	pe->pe_rrssi = MS(val, AR_PHY_RADAR_0_RRSSI);
	pe->pe_height =  MS(val, AR_PHY_RADAR_0_HEIGHT);
	pe->pe_prssi = MS(val, AR_PHY_RADAR_0_PRSSI);
	pe->pe_inband = MS(val, AR_PHY_RADAR_0_INBAND);

	/* RADAR_1 values */
	val = OS_REG_READ(ah, AR_PHY_RADAR_1);
	pe->pe_relpwr = MS(val, AR_PHY_RADAR_1_RELPWR_THRESH);
	pe->pe_relstep = MS(val, AR_PHY_RADAR_1_RELSTEP_THRESH);
	pe->pe_maxlen = MS(val, AR_PHY_RADAR_1_MAXLEN);

	pe->pe_extchannel = !! (OS_REG_READ(ah, AR_PHY_RADAR_EXT) &
	    AR_PHY_RADAR_EXT_ENA);

	pe->pe_usefir128 = !! (OS_REG_READ(ah, AR_PHY_RADAR_1) &
	    AR_PHY_RADAR_1_USE_FIR128);
	pe->pe_blockradar = !! (OS_REG_READ(ah, AR_PHY_RADAR_1) &
	    AR_PHY_RADAR_1_BLOCK_CHECK);
	pe->pe_enmaxrssi = !! (OS_REG_READ(ah, AR_PHY_RADAR_1) &
	    AR_PHY_RADAR_1_MAX_RRSSI);
	pe->pe_enabled = !!
	    (OS_REG_READ(ah, AR_PHY_RADAR_0) & AR_PHY_RADAR_0_ENA);
	pe->pe_enrelpwr = !! (OS_REG_READ(ah, AR_PHY_RADAR_1) &
	    AR_PHY_RADAR_1_RELPWR_ENA);
	pe->pe_en_relstep_check = !! (OS_REG_READ(ah, AR_PHY_RADAR_1) &
	    AR_PHY_RADAR_1_RELSTEP_CHECK);
}

/*
 * Enable radar detection and set the radar parameters per the
 * values in pe
 */
void
ar5416EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_PHY_RADAR_0);

	if (pe->pe_firpwr != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_FIRPWR;
		val |= SM(pe->pe_firpwr, AR_PHY_RADAR_0_FIRPWR);
	}
	if (pe->pe_rrssi != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_RRSSI;
		val |= SM(pe->pe_rrssi, AR_PHY_RADAR_0_RRSSI);
	}
	if (pe->pe_height != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_HEIGHT;
		val |= SM(pe->pe_height, AR_PHY_RADAR_0_HEIGHT);
	}
	if (pe->pe_prssi != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_PRSSI;
		val |= SM(pe->pe_prssi, AR_PHY_RADAR_0_PRSSI);
	}
	if (pe->pe_inband != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_INBAND;
		val |= SM(pe->pe_inband, AR_PHY_RADAR_0_INBAND);
	}

	/*Enable FFT data*/
	val |= AR_PHY_RADAR_0_FFT_ENA;
	OS_REG_WRITE(ah, AR_PHY_RADAR_0, val);

	/* Implicitly enable */
	if (pe->pe_enabled == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_0, AR_PHY_RADAR_0_ENA);
	else if (pe->pe_enabled == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_0, AR_PHY_RADAR_0_ENA);

	if (pe->pe_usefir128 == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_USE_FIR128);
	else if (pe->pe_usefir128 == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_USE_FIR128);

	if (pe->pe_enmaxrssi == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_MAX_RRSSI);
	else if (pe->pe_enmaxrssi == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_MAX_RRSSI);

	if (pe->pe_blockradar == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_BLOCK_CHECK);
	else if (pe->pe_blockradar == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_BLOCK_CHECK);

	if (pe->pe_relstep != HAL_PHYERR_PARAM_NOVAL) {
		val = OS_REG_READ(ah, AR_PHY_RADAR_1);
		val &= ~AR_PHY_RADAR_1_RELSTEP_THRESH;
		val |= SM(pe->pe_relstep, AR_PHY_RADAR_1_RELSTEP_THRESH);
		OS_REG_WRITE(ah, AR_PHY_RADAR_1, val);
	}
	if (pe->pe_relpwr != HAL_PHYERR_PARAM_NOVAL) {
		val = OS_REG_READ(ah, AR_PHY_RADAR_1);
		val &= ~AR_PHY_RADAR_1_RELPWR_THRESH;
		val |= SM(pe->pe_relpwr, AR_PHY_RADAR_1_RELPWR_THRESH);
		OS_REG_WRITE(ah, AR_PHY_RADAR_1, val);
	}

	if (pe->pe_en_relstep_check == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_1,
		    AR_PHY_RADAR_1_RELSTEP_CHECK);
	else if (pe->pe_en_relstep_check == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_1,
		    AR_PHY_RADAR_1_RELSTEP_CHECK);

	if (pe->pe_enrelpwr == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_1,
		    AR_PHY_RADAR_1_RELPWR_ENA);
	else if (pe->pe_enrelpwr == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_1,
		    AR_PHY_RADAR_1_RELPWR_ENA);

	if (pe->pe_maxlen != HAL_PHYERR_PARAM_NOVAL) {
		val = OS_REG_READ(ah, AR_PHY_RADAR_1);
		val &= ~AR_PHY_RADAR_1_MAXLEN;
		val |= SM(pe->pe_maxlen, AR_PHY_RADAR_1_MAXLEN);
		OS_REG_WRITE(ah, AR_PHY_RADAR_1, val);
	}

	/*
	 * Enable HT/40 if the upper layer asks;
	 * it should check the channel is HT/40 and HAL_CAP_EXT_CHAN_DFS
	 * is available.
	 */
	if (pe->pe_extchannel == 1)
		OS_REG_SET_BIT(ah, AR_PHY_RADAR_EXT, AR_PHY_RADAR_EXT_ENA);
	else if (pe->pe_extchannel == 0)
		OS_REG_CLR_BIT(ah, AR_PHY_RADAR_EXT, AR_PHY_RADAR_EXT_ENA);
}

/*
 * Extract the radar event information from the given phy error.
 *
 * Returns AH_TRUE if the phy error was actually a phy error,
 * AH_FALSE if the phy error wasn't a phy error.
 */

/* Flags for pulse_bw_info */
#define	PRI_CH_RADAR_FOUND		0x01
#define	EXT_CH_RADAR_FOUND		0x02
#define	EXT_CH_RADAR_EARLY_FOUND	0x04

HAL_BOOL
ar5416ProcessRadarEvent(struct ath_hal *ah, struct ath_rx_status *rxs,
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

/*
 * Return whether fast-clock is currently enabled for this
 * channel.
 */
HAL_BOOL
ar5416IsFastClockEnabled(struct ath_hal *ah)
{
	struct ath_hal_private *ahp = AH_PRIVATE(ah);

	return IS_5GHZ_FAST_CLOCK_EN(ah, ahp->ah_curchan);
}
