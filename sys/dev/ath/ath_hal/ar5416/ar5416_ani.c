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
#include "opt_ah.h"

/*
 * XXX this is virtually the same code as for 5212; we reuse
 * storage in the 5212 state block; need to refactor.
 */
#include "ah.h"
#include "ah_internal.h"
#include "ah_desc.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/*
 * Anti noise immunity support.  We track phy errors and react
 * to excessive errors by adjusting the noise immunity parameters.
 */

#define HAL_EP_RND(x, mul) \
	((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define	BEACON_RSSI(ahp) \
	HAL_EP_RND(ahp->ah_stats.ast_nodestats.ns_avgbrssi, \
		HAL_RSSI_EP_MULTIPLIER)

/*
 * ANI processing tunes radio parameters according to PHY errors
 * and related information.  This is done for for noise and spur
 * immunity in all operating modes if the device indicates it's
 * capable at attach time.  In addition, when there is a reference
 * rssi value (e.g. beacon frames from an ap in station mode)
 * further tuning is done.
 *
 * ANI_ENA indicates whether any ANI processing should be done;
 * this is specified at attach time.
 *
 * ANI_ENA_RSSI indicates whether rssi-based processing should
 * done, this is enabled based on operating mode and is meaningful
 * only if ANI_ENA is true.
 *
 * ANI parameters are typically controlled only by the hal.  The
 * AniControl interface however permits manual tuning through the
 * diagnostic api.
 */
#define ANI_ENA(ah) \
	(AH5212(ah)->ah_procPhyErr & HAL_ANI_ENA)
#define ANI_ENA_RSSI(ah) \
	(AH5212(ah)->ah_procPhyErr & HAL_RSSI_ANI_ENA)

#define	ah_mibStats	ah_stats.ast_mibstats

static void
enableAniMIBCounters(struct ath_hal *ah, const struct ar5212AniParams *params)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALDEBUG(ah, HAL_DEBUG_ANI, "%s: Enable mib counters: "
	    "OfdmPhyErrBase 0x%x cckPhyErrBase 0x%x\n",
	    __func__, params->ofdmPhyErrBase, params->cckPhyErrBase);

	OS_REG_WRITE(ah, AR_FILTOFDM, 0);
	OS_REG_WRITE(ah, AR_FILTCCK, 0);

	OS_REG_WRITE(ah, AR_PHYCNT1, params->ofdmPhyErrBase);
	OS_REG_WRITE(ah, AR_PHYCNT2, params->cckPhyErrBase);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	ar5212UpdateMibCounters(ah, &ahp->ah_mibStats);	/* save+clear counters*/
	ar5212EnableMibCounters(ah);			/* enable everything */
}

static void 
disableAniMIBCounters(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALDEBUG(ah, HAL_DEBUG_ANI, "Disable MIB counters\n");

	ar5212UpdateMibCounters(ah, &ahp->ah_mibStats);	/* save stats */
	ar5212DisableMibCounters(ah);			/* disable everything */

	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, 0);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, 0);
}

static void
setPhyErrBase(struct ath_hal *ah, struct ar5212AniParams *params)
{
	if (params->ofdmTrigHigh >= AR_PHY_COUNTMAX) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "OFDM Trigger %d is too high for hw counters, using max\n",
		    params->ofdmTrigHigh);
		params->ofdmPhyErrBase = 0;
	} else
		params->ofdmPhyErrBase = AR_PHY_COUNTMAX - params->ofdmTrigHigh;
	if (params->cckTrigHigh >= AR_PHY_COUNTMAX) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "CCK Trigger %d is too high for hw counters, using max\n",
		    params->cckTrigHigh);
		params->cckPhyErrBase = 0;
	} else
		params->cckPhyErrBase = AR_PHY_COUNTMAX - params->cckTrigHigh;
}

/*
 * Setup ANI handling.  Sets all thresholds and reset the
 * channel statistics.  Note that ar5416AniReset should be
 * called by ar5416Reset before anything else happens and
 * that's where we force initial settings.
 */
void
ar5416AniAttach(struct ath_hal *ah, const struct ar5212AniParams *params24,
	const struct ar5212AniParams *params5, HAL_BOOL enable)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	if (params24 != AH_NULL) {
		OS_MEMCPY(&ahp->ah_aniParams24, params24, sizeof(*params24));
		setPhyErrBase(ah, &ahp->ah_aniParams24);
	}
	if (params5 != AH_NULL) {
		OS_MEMCPY(&ahp->ah_aniParams5, params5, sizeof(*params5));
		setPhyErrBase(ah, &ahp->ah_aniParams5);
	}

	OS_MEMZERO(ahp->ah_ani, sizeof(ahp->ah_ani));
	/* Enable MIB Counters */
	enableAniMIBCounters(ah, &ahp->ah_aniParams24 /*XXX*/);

	if (enable) {		/* Enable ani now */
		HALASSERT(params24 != AH_NULL && params5 != AH_NULL);
		ahp->ah_procPhyErr |= HAL_ANI_ENA;
	} else {
		ahp->ah_procPhyErr &= ~HAL_ANI_ENA;
	}
}

/*
 * Cleanup any ANI state setup.
 *
 * This doesn't restore registers to their default settings!
 */
void
ar5416AniDetach(struct ath_hal *ah)
{
	HALDEBUG(ah, HAL_DEBUG_ANI, "Detaching Ani\n");
	disableAniMIBCounters(ah);
}

/*
 * Control Adaptive Noise Immunity Parameters
 */
HAL_BOOL
ar5416AniControl(struct ath_hal *ah, HAL_ANI_CMD cmd, int param)
{
	typedef int TABLE[];
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5212AniState *aniState = ahp->ah_curani;
	const struct ar5212AniParams *params = AH_NULL;

	/*
	 * This function may be called before there's a current
	 * channel (eg to disable ANI.)
	 */
	if (aniState != AH_NULL)
		params = aniState->params;

	OS_MARK(ah, AH_MARK_ANI_CONTROL, cmd);

	/* These commands can't be disabled */
	if (cmd == HAL_ANI_PRESENT)
		return AH_TRUE;

	if (cmd == HAL_ANI_MODE) {
		if (param == 0) {
			ahp->ah_procPhyErr &= ~HAL_ANI_ENA;
			/* Turn off HW counters if we have them */
			ar5416AniDetach(ah);
		} else {			/* normal/auto mode */
			/* don't mess with state if already enabled */
			if (! (ahp->ah_procPhyErr & HAL_ANI_ENA)) {
				/* Enable MIB Counters */
				/*
				 * XXX use 2.4ghz params if no channel is
				 * available
				 */
				enableAniMIBCounters(ah,
				    ahp->ah_curani != AH_NULL ?
				      ahp->ah_curani->params:
				      &ahp->ah_aniParams24);
				ahp->ah_procPhyErr |= HAL_ANI_ENA;
			}
		}
		return AH_TRUE;
	}

	/* Check whether the particular function is enabled */
	if (((1 << cmd) & AH5416(ah)->ah_ani_function) == 0) {
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: command %d disabled\n",
		    __func__, cmd);
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: cmd %d; mask %x\n", __func__, cmd, AH5416(ah)->ah_ani_function);
		return AH_FALSE;
	}


	switch (cmd) {
	case HAL_ANI_NOISE_IMMUNITY_LEVEL: {
		u_int level = param;

		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: HAL_ANI_NOISE_IMMUNITY_LEVEL: set level = %d\n", __func__, level);
		if (level > params->maxNoiseImmunityLevel) {
			HALDEBUG(ah, HAL_DEBUG_ANI,
			    "%s: immunity level out of range (%u > %u)\n",
			    __func__, level, params->maxNoiseImmunityLevel);
			return AH_FALSE;
		}

		OS_REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
		    AR_PHY_DESIRED_SZ_TOT_DES, params->totalSizeDesired[level]);
		OS_REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
		    AR_PHY_AGC_CTL1_COARSE_LOW, params->coarseLow[level]);
		OS_REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
		    AR_PHY_AGC_CTL1_COARSE_HIGH, params->coarseHigh[level]);
		OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
		    AR_PHY_FIND_SIG_FIRPWR, params->firpwr[level]);

		if (level > aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_niup++;
		else if (level < aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_nidown++;
		aniState->noiseImmunityLevel = level;
		break;
	}
	case HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION: {
		static const TABLE m1ThreshLow   = { 127,   50 };
		static const TABLE m2ThreshLow   = { 127,   40 };
		static const TABLE m1Thresh      = { 127, 0x4d };
		static const TABLE m2Thresh      = { 127, 0x40 };
		static const TABLE m2CountThr    = {  31,   16 };
		static const TABLE m2CountThrLow = {  63,   48 };
		u_int on = param ? 1 : 0;

		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION: %s\n", __func__, on ? "enabled" : "disabled");
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			AR_PHY_SFCORR_LOW_M1_THRESH_LOW, m1ThreshLow[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			AR_PHY_SFCORR_LOW_M2_THRESH_LOW, m2ThreshLow[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			AR_PHY_SFCORR_M1_THRESH, m1Thresh[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			AR_PHY_SFCORR_M2_THRESH, m2Thresh[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			AR_PHY_SFCORR_M2COUNT_THR, m2CountThr[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, m2CountThrLow[on]);

		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			AR_PHY_SFCORR_EXT_M1_THRESH_LOW, m1ThreshLow[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			AR_PHY_SFCORR_EXT_M2_THRESH_LOW, m2ThreshLow[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			AR_PHY_SFCORR_EXT_M1_THRESH, m1Thresh[on]);
		OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			AR_PHY_SFCORR_EXT_M2_THRESH, m2Thresh[on]);

		if (on) {
			OS_REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		} else {
			OS_REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		}
		if (on)
			ahp->ah_stats.ast_ani_ofdmon++;
		else
			ahp->ah_stats.ast_ani_ofdmoff++;
		aniState->ofdmWeakSigDetectOff = !on;
		break;
	}
	case HAL_ANI_CCK_WEAK_SIGNAL_THR: {
		static const TABLE weakSigThrCck = { 8, 6 };
		u_int high = param ? 1 : 0;

		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: HAL_ANI_CCK_WEAK_SIGNAL_THR: %s\n", __func__, high ? "high" : "low");
		OS_REG_RMW_FIELD(ah, AR_PHY_CCK_DETECT,
		    AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK, weakSigThrCck[high]);
		if (high)
			ahp->ah_stats.ast_ani_cckhigh++;
		else
			ahp->ah_stats.ast_ani_ccklow++;
		aniState->cckWeakSigThreshold = high;
		break;
	}
	case HAL_ANI_FIRSTEP_LEVEL: {
		u_int level = param;

		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: HAL_ANI_FIRSTEP_LEVEL: level = %d\n", __func__, level);
		if (level > params->maxFirstepLevel) {
			HALDEBUG(ah, HAL_DEBUG_ANI,
			    "%s: firstep level out of range (%u > %u)\n",
			    __func__, level, params->maxFirstepLevel);
			return AH_FALSE;
		}
		OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
		    AR_PHY_FIND_SIG_FIRSTEP, params->firstep[level]);
		if (level > aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepup++;
		else if (level < aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepdown++;
		aniState->firstepLevel = level;
		break;
	}
	case HAL_ANI_SPUR_IMMUNITY_LEVEL: {
		u_int level = param;

		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: HAL_ANI_SPUR_IMMUNITY_LEVEL: level = %d\n", __func__, level);
		if (level > params->maxSpurImmunityLevel) {
			HALDEBUG(ah, HAL_DEBUG_ANI,
			    "%s: spur immunity level out of range (%u > %u)\n",
			    __func__, level, params->maxSpurImmunityLevel);
			return AH_FALSE;
		}
		OS_REG_RMW_FIELD(ah, AR_PHY_TIMING5,
		    AR_PHY_TIMING5_CYCPWR_THR1, params->cycPwrThr1[level]);

		if (level > aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurup++;
		else if (level < aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurdown++;
		aniState->spurImmunityLevel = level;
		break;
	}
#ifdef AH_PRIVATE_DIAG
	case HAL_ANI_PHYERR_RESET:
		ahp->ah_stats.ast_ani_ofdmerrs = 0;
		ahp->ah_stats.ast_ani_cckerrs = 0;
		break;
#endif /* AH_PRIVATE_DIAG */
	default:
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: invalid cmd %u\n",
		    __func__, cmd);
		return AH_FALSE;
	}
	return AH_TRUE;
}

static void
ar5416AniOfdmErrTrigger(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	struct ar5212AniState *aniState;
	const struct ar5212AniParams *params;

	HALASSERT(chan != AH_NULL);

	if (!ANI_ENA(ah))
		return;

	aniState = ahp->ah_curani;
	params = aniState->params;
	/* First, raise noise immunity level, up to max */
	if (aniState->noiseImmunityLevel+1 < params->maxNoiseImmunityLevel) {
		if (ar5416AniControl(ah, HAL_ANI_NOISE_IMMUNITY_LEVEL, 
				 aniState->noiseImmunityLevel + 1))
			return;
	}
	/* then, raise spur immunity level, up to max */
	if (aniState->spurImmunityLevel+1 < params->maxSpurImmunityLevel) {
		if (ar5416AniControl(ah, HAL_ANI_SPUR_IMMUNITY_LEVEL,
				 aniState->spurImmunityLevel + 1))
			return;
	}

	/*
	 * In the case of AP mode operation, we cannot bucketize beacons
	 * according to RSSI.  Instead, raise Firstep level, up to max, and
	 * simply return.
	 */
	if (AH_PRIVATE(ah)->ah_opmode == HAL_M_HOSTAP) {
		if (aniState->firstepLevel < params->maxFirstepLevel) {
			if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
			    aniState->firstepLevel + 1))
				return;
		}
	}
	if (ANI_ENA_RSSI(ah)) {
		int32_t rssi = BEACON_RSSI(ahp);
		if (rssi > params->rssiThrHigh) {
			/*
			 * Beacon rssi is high, can turn off ofdm
			 * weak sig detect.
			 */
			if (!aniState->ofdmWeakSigDetectOff) {
				ar5416AniControl(ah,
				    HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				    AH_FALSE);
				ar5416AniControl(ah,
				    HAL_ANI_SPUR_IMMUNITY_LEVEL, 0);
				return;
			}
			/* 
			 * If weak sig detect is already off, as last resort,
			 * raise firstep level 
			 */
			if (aniState->firstepLevel < params->maxFirstepLevel) {
				if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel + 1))
					return;
			}
		} else if (rssi > params->rssiThrLow) {
			/* 
			 * Beacon rssi in mid range, need ofdm weak signal
			 * detect, but we can raise firststepLevel.
			 */
			if (aniState->ofdmWeakSigDetectOff)
				ar5416AniControl(ah,
				    HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				    AH_TRUE);
			if (aniState->firstepLevel < params->maxFirstepLevel)
				if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
				     aniState->firstepLevel + 1))
				return;
		} else {
			/* 
			 * Beacon rssi is low, if in 11b/g mode, turn off ofdm
			 * weak signal detection and zero firstepLevel to
			 * maximize CCK sensitivity 
			 */
			if (IEEE80211_IS_CHAN_CCK(chan)) {
				if (!aniState->ofdmWeakSigDetectOff)
					ar5416AniControl(ah,
					    HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					    AH_FALSE);
				if (aniState->firstepLevel > 0)
					if (ar5416AniControl(ah,
					     HAL_ANI_FIRSTEP_LEVEL, 0))
						return;
			}
		}
	}
}

static void
ar5416AniCckErrTrigger(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	struct ar5212AniState *aniState;
	const struct ar5212AniParams *params;

	HALASSERT(chan != AH_NULL);

	if (!ANI_ENA(ah))
		return;

	/* first, raise noise immunity level, up to max */
	aniState = ahp->ah_curani;
	params = aniState->params;
	if ((AH5416(ah)->ah_ani_function & (1 << HAL_ANI_NOISE_IMMUNITY_LEVEL) &&
	    aniState->noiseImmunityLevel+1 < params->maxNoiseImmunityLevel)) {
		ar5416AniControl(ah, HAL_ANI_NOISE_IMMUNITY_LEVEL,
				 aniState->noiseImmunityLevel + 1);
		return;
	}

	if (ANI_ENA_RSSI(ah)) {
		int32_t rssi = BEACON_RSSI(ahp);
		if (rssi >  params->rssiThrLow) {
			/*
			 * Beacon signal in mid and high range,
			 * raise firstep level.
			 */
			if (aniState->firstepLevel < params->maxFirstepLevel)
				ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel + 1);
		} else {
			/*
			 * Beacon rssi is low, zero firstep level to maximize
			 * CCK sensitivity in 11b/g mode.
			 */
			if (IEEE80211_IS_CHAN_CCK(chan)) {
				if (aniState->firstepLevel > 0)
					ar5416AniControl(ah,
					    HAL_ANI_FIRSTEP_LEVEL, 0);
			}
		}
	}
}

static void
ar5416AniRestart(struct ath_hal *ah, struct ar5212AniState *aniState)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const struct ar5212AniParams *params = aniState->params;

	aniState->listenTime = 0;
	/*
	 * NB: these are written on reset based on the
	 *     ini so we must re-write them!
	 */
	HALDEBUG(ah, HAL_DEBUG_ANI,
	    "%s: Writing ofdmbase=%u   cckbase=%u\n", __func__,
	    params->ofdmPhyErrBase, params->cckPhyErrBase);
	OS_REG_WRITE(ah, AR_PHY_ERR_1, params->ofdmPhyErrBase);
	OS_REG_WRITE(ah, AR_PHY_ERR_2, params->cckPhyErrBase);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	/* Clear the mib counters and save them in the stats */
	ar5212UpdateMibCounters(ah, &ahp->ah_mibStats);
	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

/*
 * Restore/reset the ANI parameters and reset the statistics.
 * This routine must be called for every channel change.
 *
 * NOTE: This is where ah_curani is set; other ani code assumes
 *       it is setup to reflect the current channel.
 */
void
ar5416AniReset(struct ath_hal *ah, const struct ieee80211_channel *chan,
	HAL_OPMODE opmode, int restore)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	/* XXX bounds check ic_devdata */
	struct ar5212AniState *aniState = &ahp->ah_ani[chan->ic_devdata];
	uint32_t rxfilter;

	if ((ichan->privFlags & CHANNEL_ANI_INIT) == 0) {
		OS_MEMZERO(aniState, sizeof(*aniState));
		if (IEEE80211_IS_CHAN_2GHZ(chan))
			aniState->params = &ahp->ah_aniParams24;
		else
			aniState->params = &ahp->ah_aniParams5;
		ichan->privFlags |= CHANNEL_ANI_INIT;
		HALASSERT((ichan->privFlags & CHANNEL_ANI_SETUP) == 0);
	}
	ahp->ah_curani = aniState;
#if 0
	ath_hal_printf(ah,"%s: chan %u/0x%x restore %d opmode %u%s\n",
	    __func__, chan->ic_freq, chan->ic_flags, restore, opmode,
	    ichan->privFlags & CHANNEL_ANI_SETUP ? " setup" : "");
#else
	HALDEBUG(ah, HAL_DEBUG_ANI, "%s: chan %u/0x%x restore %d opmode %u%s\n",
	    __func__, chan->ic_freq, chan->ic_flags, restore, opmode,
	    ichan->privFlags & CHANNEL_ANI_SETUP ? " setup" : "");
#endif
	OS_MARK(ah, AH_MARK_ANI_RESET, opmode);

	/*
	 * Turn off PHY error frame delivery while we futz with settings.
	 */
	rxfilter = ah->ah_getRxFilter(ah);
	ah->ah_setRxFilter(ah, rxfilter &~ HAL_RX_FILTER_PHYERR);

	/*
	 * If ANI is disabled at this point, don't set the default
	 * ANI parameter settings - leave the HAL settings there.
	 * This is (currently) needed for reliable radar detection.
	 */
	if (! ANI_ENA(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: ANI disabled\n",
		    __func__);
		goto finish;
	}

	/*
	 * Use a restrictive set of ANI parameters for hostap mode.
	 */
	if (opmode == HAL_M_HOSTAP) {
		if (IEEE80211_IS_CHAN_2GHZ(chan))
			AH5416(ah)->ah_ani_function =
			    HAL_ANI_SPUR_IMMUNITY_LEVEL | HAL_ANI_FIRSTEP_LEVEL;
		else
			AH5416(ah)->ah_ani_function = 0;
	}

	/*
	 * Automatic processing is done only in station mode right now.
	 */
	if (opmode == HAL_M_STA)
		ahp->ah_procPhyErr |= HAL_RSSI_ANI_ENA;
	else
		ahp->ah_procPhyErr &= ~HAL_RSSI_ANI_ENA;
	/*
	 * Set all ani parameters.  We either set them to initial
	 * values or restore the previous ones for the channel.
	 * XXX if ANI follows hardware, we don't care what mode we're
	 * XXX in, we should keep the ani parameters
	 */
	if (restore && (ichan->privFlags & CHANNEL_ANI_SETUP)) {
		ar5416AniControl(ah, HAL_ANI_NOISE_IMMUNITY_LEVEL,
				 aniState->noiseImmunityLevel);
		ar5416AniControl(ah, HAL_ANI_SPUR_IMMUNITY_LEVEL,
				 aniState->spurImmunityLevel);
		ar5416AniControl(ah, HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				 !aniState->ofdmWeakSigDetectOff);
		ar5416AniControl(ah, HAL_ANI_CCK_WEAK_SIGNAL_THR,
				 aniState->cckWeakSigThreshold);
		ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
				 aniState->firstepLevel);
	} else {
		ar5416AniControl(ah, HAL_ANI_NOISE_IMMUNITY_LEVEL, 0);
		ar5416AniControl(ah, HAL_ANI_SPUR_IMMUNITY_LEVEL, 0);
		ar5416AniControl(ah, HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
			AH_FALSE);
		ar5416AniControl(ah, HAL_ANI_CCK_WEAK_SIGNAL_THR, AH_FALSE);
		ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL, 0);
		ichan->privFlags |= CHANNEL_ANI_SETUP;
	}

	/*
	 * In case the counters haven't yet been setup; set them up.
	 */
	enableAniMIBCounters(ah, aniState->params);
	ar5416AniRestart(ah, aniState);

finish:
	/* restore RX filter mask */
	ah->ah_setRxFilter(ah, rxfilter);
}

/*
 * Process a MIB interrupt.  We may potentially be invoked because
 * any of the MIB counters overflow/trigger so don't assume we're
 * here because a PHY error counter triggered.
 */
void
ar5416ProcessMibIntr(struct ath_hal *ah, const HAL_NODE_STATS *stats)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t phyCnt1, phyCnt2;

	HALDEBUG(ah, HAL_DEBUG_ANI, "%s: mibc 0x%x phyCnt1 0x%x phyCnt2 0x%x "
	    "filtofdm 0x%x filtcck 0x%x\n",
	    __func__, OS_REG_READ(ah, AR_MIBC),
	    OS_REG_READ(ah, AR_PHYCNT1), OS_REG_READ(ah, AR_PHYCNT2),
	    OS_REG_READ(ah, AR_FILTOFDM), OS_REG_READ(ah, AR_FILTCCK));

	/*
	 * First order of business is to clear whatever caused
	 * the interrupt so we don't keep getting interrupted.
	 * We have the usual mib counters that are reset-on-read
	 * and the additional counters that appeared starting in
	 * Hainan.  We collect the mib counters and explicitly
	 * zero additional counters we are not using.  Anything
	 * else is reset only if it caused the interrupt.
	 */
	/* NB: these are not reset-on-read */
	phyCnt1 = OS_REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = OS_REG_READ(ah, AR_PHY_ERR_2);
	/* not used, always reset them in case they are the cause */
	OS_REG_WRITE(ah, AR_FILTOFDM, 0);
	OS_REG_WRITE(ah, AR_FILTCCK, 0);
	if ((OS_REG_READ(ah, AR_SLP_MIB_CTRL) & AR_SLP_MIB_PENDING) == 0)
		OS_REG_WRITE(ah, AR_SLP_MIB_CTRL, AR_SLP_MIB_CLEAR);

	/* Clear the mib counters and save them in the stats */
	ar5212UpdateMibCounters(ah, &ahp->ah_mibStats);
	ahp->ah_stats.ast_nodestats = *stats;

	/*
	 * Check for an ani stat hitting the trigger threshold.
	 * When this happens we get a MIB interrupt and the top
	 * 2 bits of the counter register will be 0b11, hence
	 * the mask check of phyCnt?.
	 */
	if (((phyCnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) || 
	    ((phyCnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK)) {
		struct ar5212AniState *aniState = ahp->ah_curani;
		const struct ar5212AniParams *params = aniState->params;
		uint32_t ofdmPhyErrCnt, cckPhyErrCnt;

		ofdmPhyErrCnt = phyCnt1 - params->ofdmPhyErrBase;
		ahp->ah_stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - params->cckPhyErrBase;
		ahp->ah_stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;

		/*
		 * NB: figure out which counter triggered.  If both
		 * trigger we'll only deal with one as the processing
		 * clobbers the error counter so the trigger threshold
		 * check will never be true.
		 */
		if (aniState->ofdmPhyErrCount > params->ofdmTrigHigh)
			ar5416AniOfdmErrTrigger(ah);
		if (aniState->cckPhyErrCount > params->cckTrigHigh)
			ar5416AniCckErrTrigger(ah);
		/* NB: always restart to insure the h/w counters are reset */
		ar5416AniRestart(ah, aniState);
	}
}

static void
ar5416AniLowerImmunity(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5212AniState *aniState;
	const struct ar5212AniParams *params;
	
	HALASSERT(ANI_ENA(ah));

	aniState = ahp->ah_curani;
	params = aniState->params;

	/*
	 * In the case of AP mode operation, we cannot bucketize beacons
	 * according to RSSI.  Instead, lower Firstep level, down to min, and
	 * simply return.
	 */
	if (AH_PRIVATE(ah)->ah_opmode == HAL_M_HOSTAP) {
		if (aniState->firstepLevel > 0) {
			if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
			    aniState->firstepLevel - 1))
				return;
		}
	}
	if (ANI_ENA_RSSI(ah)) {
		int32_t rssi = BEACON_RSSI(ahp);
		if (rssi > params->rssiThrHigh) {
			/* 
			 * Beacon signal is high, leave ofdm weak signal
			 * detection off or it may oscillate.  Let it fall
			 * through.
			 */
		} else if (rssi > params->rssiThrLow) {
			/*
			 * Beacon rssi in mid range, turn on ofdm weak signal
			 * detection or lower firstep level.
			 */
			if (aniState->ofdmWeakSigDetectOff) {
				if (ar5416AniControl(ah,
				    HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				    AH_TRUE))
					return;
			}
			if (aniState->firstepLevel > 0) {
				if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel - 1))
					return;
			}
		} else {
			/*
			 * Beacon rssi is low, reduce firstep level.
			 */
			if (aniState->firstepLevel > 0) {
				if (ar5416AniControl(ah, HAL_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel - 1))
					return;
			}
		}
	}
	/* then lower spur immunity level, down to zero */
	if (aniState->spurImmunityLevel > 0) {
		if (ar5416AniControl(ah, HAL_ANI_SPUR_IMMUNITY_LEVEL,
				 aniState->spurImmunityLevel - 1))
			return;
	}
	/* 
	 * if all else fails, lower noise immunity level down to a min value
	 * zero for now
	 */
	if (aniState->noiseImmunityLevel > 0) {
		if (ar5416AniControl(ah, HAL_ANI_NOISE_IMMUNITY_LEVEL,
				 aniState->noiseImmunityLevel - 1))
			return;
	}
}

#define CLOCK_RATE 44000	/* XXX use mac_usec or similar */
/* convert HW counter values to ms using 11g clock rate, goo9d enough
   for 11a and Turbo */

/* 
 * Return an approximation of the time spent ``listening'' by
 * deducting the cycles spent tx'ing and rx'ing from the total
 * cycle count since our last call.  A return value <0 indicates
 * an invalid/inconsistent time.
 *
 * This may be called with ANI disabled; in which case simply keep
 * the statistics and don't write to the aniState pointer.
 *
 * XXX TODO: Make this cleaner!
 */
static int32_t
ar5416AniGetListenTime(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5212AniState *aniState = NULL;
	int32_t listenTime = 0;
	int good;
	HAL_SURVEY_SAMPLE hs;

	/*
	 * We shouldn't see ah_curchan be NULL, but just in case..
	 */
	if (AH_PRIVATE(ah)->ah_curchan == AH_NULL) {
		ath_hal_printf(ah, "%s: ah_curchan = NULL?\n", __func__);
		return (0);
	}

	/*
	 * Fetch the current statistics, squirrel away the current
	 * sample.
	 */
	OS_MEMZERO(&hs, sizeof(hs));
	good = ar5416GetMibCycleCounts(ah, &hs);
	ath_hal_survey_add_sample(ah, &hs);

	if (ANI_ENA(ah))
		aniState = ahp->ah_curani;

	if (good == AH_FALSE) {
		/*
		 * Cycle counter wrap (or initial call); it's not possible
		 * to accurately calculate a value because the registers
		 * right shift rather than wrap--so punt and return 0.
		 */
		listenTime = 0;
		ahp->ah_stats.ast_ani_lzero++;
	} else if (ANI_ENA(ah)) {
		/*
		 * Only calculate and update the cycle count if we have
		 * an ANI state.
		 */
		int32_t ccdelta =
		    AH5416(ah)->ah_cycleCount - aniState->cycleCount;
		int32_t rfdelta =
		    AH5416(ah)->ah_rxBusy - aniState->rxFrameCount;
		int32_t tfdelta =
		    AH5416(ah)->ah_txBusy - aniState->txFrameCount;
		listenTime = (ccdelta - rfdelta - tfdelta) / CLOCK_RATE;
	}

	/*
	 * Again, only update ANI state if we have it.
	 */
	if (ANI_ENA(ah)) {
		aniState->cycleCount = AH5416(ah)->ah_cycleCount;
		aniState->rxFrameCount = AH5416(ah)->ah_rxBusy;
		aniState->txFrameCount = AH5416(ah)->ah_txBusy;
	}

	return listenTime;
}

/*
 * Update ani stats in preparation for listen time processing.
 */
static void
updateMIBStats(struct ath_hal *ah, struct ar5212AniState *aniState)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const struct ar5212AniParams *params = aniState->params;
	uint32_t phyCnt1, phyCnt2;
	int32_t ofdmPhyErrCnt, cckPhyErrCnt;

	/* Clear the mib counters and save them in the stats */
	ar5212UpdateMibCounters(ah, &ahp->ah_mibStats);

	/* NB: these are not reset-on-read */
	phyCnt1 = OS_REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = OS_REG_READ(ah, AR_PHY_ERR_2);

	/* NB: these are spec'd to never roll-over */
	ofdmPhyErrCnt = phyCnt1 - params->ofdmPhyErrBase;
	if (ofdmPhyErrCnt < 0) {
		HALDEBUG(ah, HAL_DEBUG_ANI, "OFDM phyErrCnt %d phyCnt1 0x%x\n",
		    ofdmPhyErrCnt, phyCnt1);
		ofdmPhyErrCnt = AR_PHY_COUNTMAX;
	}
	ahp->ah_stats.ast_ani_ofdmerrs +=
	     ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
	aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

	cckPhyErrCnt = phyCnt2 - params->cckPhyErrBase;
	if (cckPhyErrCnt < 0) {
		HALDEBUG(ah, HAL_DEBUG_ANI, "CCK phyErrCnt %d phyCnt2 0x%x\n",
		    cckPhyErrCnt, phyCnt2);
		cckPhyErrCnt = AR_PHY_COUNTMAX;
	}
	ahp->ah_stats.ast_ani_cckerrs +=
		cckPhyErrCnt - aniState->cckPhyErrCount;
	aniState->cckPhyErrCount = cckPhyErrCnt;
}

void
ar5416RxMonitor(struct ath_hal *ah, const HAL_NODE_STATS *stats,
		const struct ieee80211_channel *chan)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	ahp->ah_stats.ast_nodestats.ns_avgbrssi = stats->ns_avgbrssi;
}

/*
 * Do periodic processing.  This routine is called from the
 * driver's rx interrupt handler after processing frames.
 */
void
ar5416AniPoll(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5212AniState *aniState = ahp->ah_curani;
	const struct ar5212AniParams *params;
	int32_t listenTime;

	/* Always update from the MIB, for statistics gathering */
	listenTime = ar5416AniGetListenTime(ah);

	/* XXX can aniState be null? */
	if (aniState == AH_NULL)
		return;

	if (!ANI_ENA(ah))
		return;

	if (listenTime < 0) {
		ahp->ah_stats.ast_ani_lneg++;
		/* restart ANI period if listenTime is invalid */
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: invalid listenTime\n",
		    __func__);
		ar5416AniRestart(ah, aniState);

		/* Don't do any further processing */
		return;
	}
	/* XXX beware of overflow? */
	aniState->listenTime += listenTime;

	OS_MARK(ah, AH_MARK_ANI_POLL, aniState->listenTime);

	params = aniState->params;
	if (aniState->listenTime > 5*params->period) {
		/* 
		 * Check to see if need to lower immunity if
		 * 5 aniPeriods have passed
		 */
		updateMIBStats(ah, aniState);
		if (aniState->ofdmPhyErrCount <= aniState->listenTime *
		    params->ofdmTrigLow/1000 &&
		    aniState->cckPhyErrCount <= aniState->listenTime *
		    params->cckTrigLow/1000)
			ar5416AniLowerImmunity(ah);
		HALDEBUG(ah, HAL_DEBUG_ANI, "%s: lower immunity\n",
		    __func__);
		ar5416AniRestart(ah, aniState);
	} else if (aniState->listenTime > params->period) {
		updateMIBStats(ah, aniState);
		/* check to see if need to raise immunity */
		if (aniState->ofdmPhyErrCount > aniState->listenTime *
		    params->ofdmTrigHigh / 1000) {
                        HALDEBUG(ah, HAL_DEBUG_ANI,
                            "%s: OFDM err %u listenTime %u\n", __func__,
                            aniState->ofdmPhyErrCount, aniState->listenTime);
			ar5416AniOfdmErrTrigger(ah);
			ar5416AniRestart(ah, aniState);
		} else if (aniState->cckPhyErrCount > aniState->listenTime *
			   params->cckTrigHigh / 1000) {
                        HALDEBUG(ah, HAL_DEBUG_ANI,
                            "%s: CCK err %u listenTime %u\n", __func__,
                            aniState->cckPhyErrCount, aniState->listenTime);
			ar5416AniCckErrTrigger(ah);
			ar5416AniRestart(ah, aniState);
		}
	}
}
