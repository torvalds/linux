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

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ah_eeprom_v14.h"

#include "ar5212/ar5212.h"	/* for NF cal related declarations */

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/* Owl specific stuff */
#define NUM_NOISEFLOOR_READINGS 6       /* 3 chains * (ctl + ext) */

static void ar5416StartNFCal(struct ath_hal *ah);
static HAL_BOOL ar5416LoadNF(struct ath_hal *ah, const struct ieee80211_channel *);
static int16_t ar5416GetNf(struct ath_hal *, struct ieee80211_channel *);

static uint16_t ar5416GetDefaultNF(struct ath_hal *ah, const struct ieee80211_channel *chan);
static void ar5416SanitizeNF(struct ath_hal *ah, int16_t *nf);

/*
 * Determine if calibration is supported by device and channel flags
 */

/*
 * ADC GAIN/DC offset calibration is for calibrating two ADCs that
 * are acting as one by interleaving incoming symbols. This isn't
 * relevant for 2.4GHz 20MHz wide modes because, as far as I can tell,
 * the secondary ADC is never enabled. It is enabled however for
 * 5GHz modes.
 *
 * It hasn't been confirmed whether doing this calibration is needed
 * at all in the above modes and/or whether it's actually harmful.
 * So for now, let's leave it enabled and just remember to get
 * confirmation that it needs to be clarified.
 *
 * See US Patent No: US 7,541,952 B1:
 *  " Method and Apparatus for Offset and Gain Compensation for
 *    Analog-to-Digital Converters."
 */
static OS_INLINE HAL_BOOL
ar5416IsCalSupp(struct ath_hal *ah, const struct ieee80211_channel *chan,
	HAL_CAL_TYPE calType) 
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;

	switch (calType & cal->suppCals) {
	case IQ_MISMATCH_CAL:
		/* Run IQ Mismatch for non-CCK only */
		return !IEEE80211_IS_CHAN_B(chan);
	case ADC_GAIN_CAL:
	case ADC_DC_CAL:
		/*
		 * Run ADC Gain Cal for either 5ghz any or 2ghz HT40.
		 *
		 * Don't run ADC calibrations for 5ghz fast clock mode
		 * in HT20 - only one ADC is used.
		 */
		if (IEEE80211_IS_CHAN_HT20(chan) &&
		    (IS_5GHZ_FAST_CLOCK_EN(ah, chan)))
			return AH_FALSE;
		if (IEEE80211_IS_CHAN_5GHZ(chan))
			return AH_TRUE;
		if (IEEE80211_IS_CHAN_HT40(chan))
			return AH_TRUE;
		return AH_FALSE;
	}
	return AH_FALSE;
}

/*
 * Setup HW to collect samples used for current cal
 */
static void
ar5416SetupMeasurement(struct ath_hal *ah, HAL_CAL_LIST *currCal)
{
	/* Start calibration w/ 2^(INIT_IQCAL_LOG_COUNT_MAX+1) samples */
	OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
	    AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,
	    currCal->calData->calCountMax);

	/* Select calibration to run */
	switch (currCal->calData->calType) {
	case IQ_MISMATCH_CAL:
		OS_REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: start IQ Mismatch calibration\n", __func__);
		break;
	case ADC_GAIN_CAL:
		OS_REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_GAIN);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: start ADC Gain calibration\n", __func__);
		break;
	case ADC_DC_CAL:
		OS_REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_PER);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: start ADC DC calibration\n", __func__);
		break;
	case ADC_DC_INIT_CAL:
		OS_REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_INIT);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: start Init ADC DC calibration\n", __func__);
		break;
	}
	/* Kick-off cal */
	OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4, AR_PHY_TIMING_CTRL4_DO_CAL);
}

/*
 * Initialize shared data structures and prepare a cal to be run.
 */
static void
ar5416ResetMeasurement(struct ath_hal *ah, HAL_CAL_LIST *currCal)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;

	/* Reset data structures shared between different calibrations */
	OS_MEMZERO(cal->caldata, sizeof(cal->caldata));
	cal->calSamples = 0;

	/* Setup HW for new calibration */
	ar5416SetupMeasurement(ah, currCal);

	/* Change SW state to RUNNING for this calibration */
	currCal->calState = CAL_RUNNING;
}

#if 0
/*
 * Run non-periodic calibrations.
 */
static HAL_BOOL
ar5416RunInitCals(struct ath_hal *ah, int init_cal_count)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	HAL_CHANNEL_INTERNAL ichan;	/* XXX bogus */
	HAL_CAL_LIST *curCal = ahp->ah_cal_curr;
	HAL_BOOL isCalDone;
	int i;

	if (curCal == AH_NULL)
		return AH_FALSE;

	ichan.calValid = 0;
	for (i = 0; i < init_cal_count; i++) {
		/* Reset this Cal */
		ar5416ResetMeasurement(ah, curCal);
		/* Poll for offset calibration complete */
		if (!ath_hal_wait(ah, AR_PHY_TIMING_CTRL4, AR_PHY_TIMING_CTRL4_DO_CAL, 0)) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: Cal %d failed to finish in 100ms.\n",
			    __func__, curCal->calData->calType);
			/* Re-initialize list pointers for periodic cals */
			cal->cal_list = cal->cal_last = cal->cal_curr = AH_NULL;
			return AH_FALSE;
		}
		/* Run this cal */
		ar5416DoCalibration(ah, &ichan, ahp->ah_rxchainmask,
		    curCal, &isCalDone);
		if (!isCalDone)
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: init cal %d did not complete.\n",
			    __func__, curCal->calData->calType);
		if (curCal->calNext != AH_NULL)
			curCal = curCal->calNext;
	}

	/* Re-initialize list pointers for periodic cals */
	cal->cal_list = cal->cal_last = cal->cal_curr = AH_NULL;
	return AH_TRUE;
}
#endif


/*
 * AGC calibration for the AR5416, AR9130, AR9160, AR9280.
 */
HAL_BOOL
ar5416InitCalHardware(struct ath_hal *ah, const struct ieee80211_channel *chan)
{

	if (AR_SREV_MERLIN_10_OR_LATER(ah)) {
		/* Disable ADC */
		OS_REG_CLR_BIT(ah, AR_PHY_ADC_CTL,
		    AR_PHY_ADC_CTL_OFF_PWDADC);

		/* Enable Rx Filter Cal */
		OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	} 	

	/* Calibrate the AGC */
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);

	/* Poll for offset calibration complete */
	if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: offset calibration did not complete in 1ms; "
		    "noisy environment?\n", __func__);
		return AH_FALSE;
	}

	if (AR_SREV_MERLIN_10_OR_LATER(ah)) {
		/* Enable ADC */
		OS_REG_SET_BIT(ah, AR_PHY_ADC_CTL,
		    AR_PHY_ADC_CTL_OFF_PWDADC);

		/* Disable Rx Filter Cal */
		OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	}

	return AH_TRUE;
}

/*
 * Initialize Calibration infrastructure.
 */
#define	MAX_CAL_CHECK		32
HAL_BOOL
ar5416InitCal(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	HAL_CHANNEL_INTERNAL *ichan;

	ichan = ath_hal_checkchannel(ah, chan);
	HALASSERT(ichan != AH_NULL);

	/* Do initial chipset-specific calibration */
	if (! AH5416(ah)->ah_cal_initcal(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: initial chipset calibration did "
		    "not complete in time; noisy environment?\n", __func__);
		return AH_FALSE;
	}

	/* If there's PA Cal, do it */
	if (AH5416(ah)->ah_cal_pacal)
		AH5416(ah)->ah_cal_pacal(ah, AH_TRUE);

	/* 
	 * Do NF calibration after DC offset and other CALs.
	 * Per system engineers, noise floor value can sometimes be 20 dB
	 * higher than normal value if DC offset and noise floor cal are
	 * triggered at the same time.
	 */
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	/*
	 * This may take a while to run; make sure subsequent
	 * calibration routines check that this has completed
	 * before reading the value and triggering a subsequent
	 * calibration.
	 */

	/* Initialize list pointers */
	cal->cal_list = cal->cal_last = cal->cal_curr = AH_NULL;

	/*
	 * Enable IQ, ADC Gain, ADC DC Offset Cals
	 */
	if (AR_SREV_HOWL(ah) || AR_SREV_SOWL_10_OR_LATER(ah)) {
		/* Setup all non-periodic, init time only calibrations */
		/* XXX: Init DC Offset not working yet */
#if 0
		if (ar5416IsCalSupp(ah, chan, ADC_DC_INIT_CAL)) {
			INIT_CAL(&cal->adcDcCalInitData);
			INSERT_CAL(cal, &cal->adcDcCalInitData);
		}
		/* Initialize current pointer to first element in list */
		cal->cal_curr = cal->cal_list;

		if (cal->ah_cal_curr != AH_NULL && !ar5416RunInitCals(ah, 0))
			return AH_FALSE;
#endif
	}

	/* If Cals are supported, add them to list via INIT/INSERT_CAL */
	if (ar5416IsCalSupp(ah, chan, ADC_GAIN_CAL)) {
		INIT_CAL(&cal->adcGainCalData);
		INSERT_CAL(cal, &cal->adcGainCalData);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: enable ADC Gain Calibration.\n", __func__);
	}
	if (ar5416IsCalSupp(ah, chan, ADC_DC_CAL)) {
		INIT_CAL(&cal->adcDcCalData);
		INSERT_CAL(cal, &cal->adcDcCalData);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: enable ADC DC Calibration.\n", __func__);
	}
	if (ar5416IsCalSupp(ah, chan, IQ_MISMATCH_CAL)) {
		INIT_CAL(&cal->iqCalData);
		INSERT_CAL(cal, &cal->iqCalData);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: enable IQ Calibration.\n", __func__);
	}
	/* Initialize current pointer to first element in list */
	cal->cal_curr = cal->cal_list;

	/* Kick off measurements for the first cal */
	if (cal->cal_curr != AH_NULL)
		ar5416ResetMeasurement(ah, cal->cal_curr);

	/* Mark all calibrations on this channel as being invalid */
	ichan->calValid = 0;

	return AH_TRUE;
#undef	MAX_CAL_CHECK
}

/*
 * Entry point for upper layers to restart current cal.
 * Reset the calibration valid bit in channel.
 */
HAL_BOOL
ar5416ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	HAL_CAL_LIST *currCal = cal->cal_curr;

	if (!AR_SREV_SOWL_10_OR_LATER(ah))
		return AH_FALSE;
	if (currCal == AH_NULL)
		return AH_FALSE;
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}
	/*
	 * Expected that this calibration has run before, post-reset.
	 * Current state should be done
	 */
	if (currCal->calState != CAL_DONE) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: Calibration state incorrect, %d\n",
		    __func__, currCal->calState);
		return AH_FALSE;
	}

	/* Verify Cal is supported on this channel */
	if (!ar5416IsCalSupp(ah, chan, currCal->calData->calType))
		return AH_FALSE;

	HALDEBUG(ah, HAL_DEBUG_PERCAL,
	    "%s: Resetting Cal %d state for channel %u/0x%x\n",
	    __func__, currCal->calData->calType, chan->ic_freq,
	    chan->ic_flags);

	/* Disable cal validity in channel */
	ichan->calValid &= ~currCal->calData->calType;
	currCal->calState = CAL_WAITING;

	return AH_TRUE;
}

/*
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
static void
ar5416DoCalibration(struct ath_hal *ah,  HAL_CHANNEL_INTERNAL *ichan,
	uint8_t rxchainmask, HAL_CAL_LIST *currCal, HAL_BOOL *isCalDone)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;

	/* Cal is assumed not done until explicitly set below */
	*isCalDone = AH_FALSE;

	HALDEBUG(ah, HAL_DEBUG_PERCAL,
	    "%s: %s Calibration, state %d, calValid 0x%x\n",
	    __func__, currCal->calData->calName, currCal->calState,
	    ichan->calValid);

	/* Calibration in progress. */
	if (currCal->calState == CAL_RUNNING) {
		/* Check to see if it has finished. */
		if (!(OS_REG_READ(ah, AR_PHY_TIMING_CTRL4) & AR_PHY_TIMING_CTRL4_DO_CAL)) {
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    "%s: sample %d of %d finished\n",
			    __func__, cal->calSamples,
			    currCal->calData->calNumSamples);
			/* 
			 * Collect measurements for active chains.
			 */
			currCal->calData->calCollect(ah);
			if (++cal->calSamples >= currCal->calData->calNumSamples) {
				int i, numChains = 0;
				for (i = 0; i < AR5416_MAX_CHAINS; i++) {
					if (rxchainmask & (1 << i))
						numChains++;
				}
				/* 
				 * Process accumulated data
				 */
				currCal->calData->calPostProc(ah, numChains);

				/* Calibration has finished. */
				ichan->calValid |= currCal->calData->calType;
				currCal->calState = CAL_DONE;
				*isCalDone = AH_TRUE;
			} else {
				/*
				 * Set-up to collect of another sub-sample.
				 */
				ar5416SetupMeasurement(ah, currCal);
			}
		}
	} else if (!(ichan->calValid & currCal->calData->calType)) {
		/* If current cal is marked invalid in channel, kick it off */
		ar5416ResetMeasurement(ah, currCal);
	}
}

/*
 * Internal interface to schedule periodic calibration work.
 */
HAL_BOOL
ar5416PerCalibrationN(struct ath_hal *ah, struct ieee80211_channel *chan,
	u_int rxchainmask, HAL_BOOL longcal, HAL_BOOL *isCalDone)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	HAL_CAL_LIST *currCal = cal->cal_curr;
	HAL_CHANNEL_INTERNAL *ichan;
	int r;

	OS_MARK(ah, AH_MARK_PERCAL, chan->ic_freq);

	*isCalDone = AH_TRUE;

	/*
	 * Since ath_hal calls the PerCal method with rxchainmask=0x1;
	 * override it with the current chainmask. The upper levels currently
	 * doesn't know about the chainmask.
	 */
	rxchainmask = AH5416(ah)->ah_rx_chainmask;

	/* Invalid channel check */
	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}

	/*
	 * For given calibration:
	 * 1. Call generic cal routine
	 * 2. When this cal is done (isCalDone) if we have more cals waiting
	 *    (eg after reset), mask this to upper layers by not propagating
	 *    isCalDone if it is set to TRUE.
	 *    Instead, change isCalDone to FALSE and setup the waiting cal(s)
	 *    to be run.
	 */
	if (currCal != AH_NULL &&
	    (currCal->calState == CAL_RUNNING ||
	     currCal->calState == CAL_WAITING)) {
		ar5416DoCalibration(ah, ichan, rxchainmask, currCal, isCalDone);
		if (*isCalDone == AH_TRUE) {
			cal->cal_curr = currCal = currCal->calNext;
			if (currCal->calState == CAL_WAITING) {
				*isCalDone = AH_FALSE;
				ar5416ResetMeasurement(ah, currCal);
			}
		}
	}

	/* Do NF cal only at longer intervals */
	if (longcal) {
		/* Do PA calibration if the chipset supports */
		if (AH5416(ah)->ah_cal_pacal)
			AH5416(ah)->ah_cal_pacal(ah, AH_FALSE);

		/* Do open-loop temperature compensation if the chipset needs it */
		if (ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL))
			AH5416(ah)->ah_olcTempCompensation(ah);

		/*
		 * Get the value from the previous NF cal
		 * and update the history buffer.
		 */
		r = ar5416GetNf(ah, chan);
		if (r == 0 || r == -1) {
			/* NF calibration result isn't valid */
			HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s: NF calibration"
			    " didn't finish; delaying CCA\n", __func__);
		} else {
			int ret;
			/* 
			 * NF calibration result is valid.
			 *
			 * Load the NF from history buffer of the current channel.
			 * NF is slow time-variant, so it is OK to use a
			 * historical value.
			 */
			ret = ar5416LoadNF(ah, AH_PRIVATE(ah)->ah_curchan);

			/* start NF calibration, without updating BB NF register*/
			ar5416StartNFCal(ah);

			/*
			 * If we failed calibration then tell the driver
			 * we failed and it should do a full chip reset
			 */
			if (! ret)
				return AH_FALSE;
		}
	}
	return AH_TRUE;
}

/*
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
HAL_BOOL
ar5416PerCalibration(struct ath_hal *ah, struct ieee80211_channel *chan,
	HAL_BOOL *isIQdone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	HAL_CAL_LIST *curCal = cal->cal_curr;

	if (curCal != AH_NULL && curCal->calData->calType == IQ_MISMATCH_CAL) {
		return ar5416PerCalibrationN(ah, chan, ahp->ah_rx_chainmask,
		    AH_TRUE, isIQdone);
	} else {
		HAL_BOOL isCalDone;

		*isIQdone = AH_FALSE;
		return ar5416PerCalibrationN(ah, chan, ahp->ah_rx_chainmask,
		    AH_TRUE, &isCalDone);
	}
}

static HAL_BOOL
ar5416GetEepromNoiseFloorThresh(struct ath_hal *ah,
	const struct ieee80211_channel *chan, int16_t *nft)
{
	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		ath_hal_eepromGet(ah, AR_EEP_NFTHRESH_5, nft);
		return AH_TRUE;
	}
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		ath_hal_eepromGet(ah, AR_EEP_NFTHRESH_2, nft);
		return AH_TRUE;
	}
	HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
	    __func__, chan->ic_flags);
	return AH_FALSE;
}

static void
ar5416StartNFCal(struct ath_hal *ah)
{
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
}

static HAL_BOOL
ar5416LoadNF(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	static const uint32_t ar5416_cca_regs[] = {
		AR_PHY_CCA,
		AR_PHY_CH1_CCA,
		AR_PHY_CH2_CCA,
		AR_PHY_EXT_CCA,
		AR_PHY_CH1_EXT_CCA,
		AR_PHY_CH2_EXT_CCA
	};
	struct ar5212NfCalHist *h;
	int i;
	int32_t val;
	uint8_t chainmask;
	int16_t default_nf = ar5416GetDefaultNF(ah, chan);

	/*
	 * Force NF calibration for all chains.
	 */
	if (AR_SREV_KITE(ah)) {
		/* Kite has only one chain */
		chainmask = 0x9;
	} else if (AR_SREV_MERLIN(ah) || AR_SREV_KIWI(ah)) {
		/* Merlin/Kiwi has only two chains */
		chainmask = 0x1B;
	} else {
		chainmask = 0x3F;
	}

	/*
	 * Write filtered NF values into maxCCApwr register parameter
	 * so we can load below.
	 */
	h = AH5416(ah)->ah_cal.nfCalHist;
	HALDEBUG(ah, HAL_DEBUG_NFCAL, "CCA: ");
	for (i = 0; i < AR5416_NUM_NF_READINGS; i ++) {

		/* Don't write to EXT radio CCA registers unless in HT/40 mode */
		/* XXX this check should really be cleaner! */
		if (i > 2 && !IEEE80211_IS_CHAN_HT40(chan))
			continue;

		if (chainmask & (1 << i)) { 
			int16_t nf_val;

			if (h)
				nf_val = h[i].privNF;
			else
				nf_val = default_nf;

			val = OS_REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((uint32_t) nf_val << 1) & 0x1ff);
			HALDEBUG(ah, HAL_DEBUG_NFCAL, "[%d: %d]", i, nf_val);
			OS_REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}
	HALDEBUG(ah, HAL_DEBUG_NFCAL, "\n");

	/* Load software filtered NF value into baseband internal minCCApwr variable. */
	OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	/* Wait for load to complete, should be fast, a few 10s of us. */
	if (! ar5212WaitNFCalComplete(ah, 1000)) {
		/*
		 * We timed out waiting for the noisefloor to load, probably due to an
		 * in-progress rx. Simply return here and allow the load plenty of time
		 * to complete before the next calibration interval.  We need to avoid
		 * trying to load -50 (which happens below) while the previous load is
		 * still in progress as this can cause rx deafness. Instead by returning
		 * here, the baseband nf cal will just be capped by our present
		 * noisefloor until the next calibration timer.
		 */
		HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "Timeout while waiting for "
		    "nf to load: AR_PHY_AGC_CONTROL=0x%x\n",
		    OS_REG_READ(ah, AR_PHY_AGC_CONTROL));
		return AH_FALSE;
	}

	/*
	 * Restore maxCCAPower register parameter again so that we're not capped
	 * by the median we just loaded.  This will be initial (and max) value
	 * of next noise floor calibration the baseband does.  
	 */
	for (i = 0; i < AR5416_NUM_NF_READINGS; i ++) {

		/* Don't write to EXT radio CCA registers unless in HT/40 mode */
		/* XXX this check should really be cleaner! */
		if (i > 2 && !IEEE80211_IS_CHAN_HT40(chan))
			continue;

		if (chainmask & (1 << i)) {	
			val = OS_REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((uint32_t)(-50) << 1) & 0x1ff);
			OS_REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}
	return AH_TRUE;
}

/*
 * This just initialises the "good" values for AR5416 which
 * may not be right; it'lll be overridden by ar5416SanitizeNF()
 * to nominal values.
 */
void
ar5416InitNfHistBuff(struct ar5212NfCalHist *h)
{
	int i, j;

	for (i = 0; i < AR5416_NUM_NF_READINGS; i ++) {
		h[i].currIndex = 0;
		h[i].privNF = AR5416_CCA_MAX_GOOD_VALUE;
		h[i].invalidNFcount = AR512_NF_CAL_HIST_MAX;
		for (j = 0; j < AR512_NF_CAL_HIST_MAX; j ++)
			h[i].nfCalBuffer[j] = AR5416_CCA_MAX_GOOD_VALUE;
	}
}

/*
 * Update the noise floor buffer as a ring buffer
 */
static void
ar5416UpdateNFHistBuff(struct ath_hal *ah, struct ar5212NfCalHist *h,
    int16_t *nfarray)
{
	int i;

	/* XXX TODO: don't record nfarray[] entries for inactive chains */
	for (i = 0; i < AR5416_NUM_NF_READINGS; i ++) {
		h[i].nfCalBuffer[h[i].currIndex] = nfarray[i];

		if (++h[i].currIndex >= AR512_NF_CAL_HIST_MAX)
			h[i].currIndex = 0;
		if (h[i].invalidNFcount > 0) {
			if (nfarray[i] < AR5416_CCA_MIN_BAD_VALUE ||
			    nfarray[i] > AR5416_CCA_MAX_HIGH_VALUE) {
				h[i].invalidNFcount = AR512_NF_CAL_HIST_MAX;
			} else {
				h[i].invalidNFcount--;
				h[i].privNF = nfarray[i];
			}
		} else {
			h[i].privNF = ar5212GetNfHistMid(h[i].nfCalBuffer);
		}
	}
}   

static uint16_t
ar5416GetDefaultNF(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
        struct ar5416NfLimits *limit;

        if (!chan || IEEE80211_IS_CHAN_2GHZ(chan))
                limit = &AH5416(ah)->nf_2g;
        else
                limit = &AH5416(ah)->nf_5g;

        return limit->nominal;
}

static void
ar5416SanitizeNF(struct ath_hal *ah, int16_t *nf)
{

        struct ar5416NfLimits *limit;
        int i;

        if (IEEE80211_IS_CHAN_2GHZ(AH_PRIVATE(ah)->ah_curchan))
                limit = &AH5416(ah)->nf_2g;
        else
                limit = &AH5416(ah)->nf_5g;

        for (i = 0; i < AR5416_NUM_NF_READINGS; i++) {
                if (!nf[i])
                        continue;

                if (nf[i] > limit->max) {
                        HALDEBUG(ah, HAL_DEBUG_NFCAL,
                                  "NF[%d] (%d) > MAX (%d), correcting to MAX\n",
                                  i, nf[i], limit->max);
                        nf[i] = limit->max;
                } else if (nf[i] < limit->min) {
                        HALDEBUG(ah, HAL_DEBUG_NFCAL,
                                  "NF[%d] (%d) < MIN (%d), correcting to NOM\n",
                                  i, nf[i], limit->min);
                        nf[i] = limit->nominal;
                }
        }
}


/*
 * Read the NF and check it against the noise floor threshold
 *
 * Return 0 if the NF calibration hadn't finished, 0 if it was
 * invalid, or > 0 for a valid NF reading.
 */
static int16_t
ar5416GetNf(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	int16_t nf, nfThresh;
	int i;
	int retval = 0;

	if (ar5212IsNFCalInProgress(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: NF didn't complete in calibration window\n", __func__);
		nf = 0;
		retval = -1;	/* NF didn't finish */
	} else {
		/* Finished NF cal, check against threshold */
		int16_t nfarray[NUM_NOISEFLOOR_READINGS] = { 0 };
		HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
			
		/* TODO - enhance for multiple chains and ext ch */
		ath_hal_getNoiseFloor(ah, nfarray);
		nf = nfarray[0];
		ar5416SanitizeNF(ah, nfarray);
		if (ar5416GetEepromNoiseFloorThresh(ah, chan, &nfThresh)) {
			if (nf > nfThresh) {
				HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
				    "%s: noise floor failed detected; "
				    "detected %d, threshold %d\n", __func__,
				    nf, nfThresh);
				/*
				 * NB: Don't discriminate 2.4 vs 5Ghz, if this
				 *     happens it indicates a problem regardless
				 *     of the band.
				 */
				chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
				nf = 0;
				retval = 0;
			}
		} else {
			nf = 0;
			retval = 0;
		}
		/* Update MIMO channel statistics, regardless of validity or not (for now) */
		for (i = 0; i < 3; i++) {
			ichan->noiseFloorCtl[i] = nfarray[i];
			ichan->noiseFloorExt[i] = nfarray[i + 3];
		}
		ichan->privFlags |= CHANNEL_MIMO_NF_VALID;

		ar5416UpdateNFHistBuff(ah, AH5416(ah)->ah_cal.nfCalHist, nfarray);
		ichan->rawNoiseFloor = nf;
		retval = nf;
	}
	return retval;
}
