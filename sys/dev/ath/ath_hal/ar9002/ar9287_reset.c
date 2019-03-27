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
#include "ah_eeprom_9287.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#include "ar9002/ar9287phy.h"
#include "ar9002/ar9287an.h"

#include "ar9002/ar9287_olc.h"
#include "ar9002/ar9287_reset.h"

/*
 * Set the TX power calibration table per-chain.
 *
 * This only supports open-loop TX power control for the AR9287.
 */
static void
ar9287SetPowerCalTable(struct ath_hal *ah,
    const struct ieee80211_channel *chan, int16_t *pTxPowerIndexOffset)
{
	struct cal_data_op_loop_ar9287 *pRawDatasetOpenLoop;
	uint8_t *pCalBChans = NULL;
	uint16_t pdGainOverlap_t2;
	uint16_t numPiers = 0, i;
	uint16_t numXpdGain, xpdMask;
	uint16_t xpdGainValues[AR5416_NUM_PD_GAINS] = {0, 0, 0, 0};
	uint32_t regChainOffset;
	HAL_EEPROM_9287 *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ar9287_eeprom *pEepData = &ee->ee_base;

	xpdMask = pEepData->modalHeader.xpdGain;

	if ((pEepData->baseEepHeader.version & AR9287_EEP_VER_MINOR_MASK) >=
	    AR9287_EEP_MINOR_VER_2)
		pdGainOverlap_t2 = pEepData->modalHeader.pdGainOverlap;
	else
		pdGainOverlap_t2 = (uint16_t)(MS(OS_REG_READ(ah, AR_PHY_TPCRG5),
					    AR_PHY_TPCRG5_PD_GAIN_OVERLAP));

	/* Note: Kiwi should only be 2ghz.. */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		pCalBChans = pEepData->calFreqPier2G;
		numPiers = AR9287_NUM_2G_CAL_PIERS;
		pRawDatasetOpenLoop = (struct cal_data_op_loop_ar9287 *)pEepData->calPierData2G[0];
		AH5416(ah)->initPDADC = pRawDatasetOpenLoop->vpdPdg[0][0];
	}
	numXpdGain = 0;

	/* Calculate the value of xpdgains from the xpdGain Mask */
	for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR5416_NUM_PD_GAINS)
				break;
			xpdGainValues[numXpdGain] =
				(uint16_t)(AR5416_PD_GAINS_IN_MASK-i);
			numXpdGain++;
		}
	}

	OS_REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN,
		      (numXpdGain - 1) & 0x3);
	OS_REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_1,
		      xpdGainValues[0]);
	OS_REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_2,
		      xpdGainValues[1]);
	OS_REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3,
		      xpdGainValues[2]);

	for (i = 0; i < AR9287_MAX_CHAINS; i++) {
		regChainOffset = i * 0x1000;

		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			int8_t txPower;
			pRawDatasetOpenLoop =
			(struct cal_data_op_loop_ar9287 *)pEepData->calPierData2G[i];
				ar9287olcGetTxGainIndex(ah, chan,
				    pRawDatasetOpenLoop,
				    pCalBChans, numPiers,
				    &txPower);
				ar9287olcSetPDADCs(ah, txPower, i);
		}
	}

	*pTxPowerIndexOffset = 0;
}


/* XXX hard-coded values? */
#define REDUCE_SCALED_POWER_BY_TWO_CHAIN     6

/*
 * ar9287SetPowerPerRateTable
 *
 * Sets the transmit power in the baseband for the given
 * operating channel and mode.
 *
 * This is like the v14 EEPROM table except the 5GHz code.
 */
static HAL_BOOL
ar9287SetPowerPerRateTable(struct ath_hal *ah,
    struct ar9287_eeprom *pEepData,
    const struct ieee80211_channel *chan,
    int16_t *ratesArray, uint16_t cfgCtl,
    uint16_t AntennaReduction, 
    uint16_t twiceMaxRegulatoryPower,
    uint16_t powerLimit)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
/* Local defines to distinguish between extension and control CTL's */
#define EXT_ADDITIVE (0x8000)
#define CTL_11A_EXT (CTL_11A | EXT_ADDITIVE)
#define CTL_11G_EXT (CTL_11G | EXT_ADDITIVE)
#define CTL_11B_EXT (CTL_11B | EXT_ADDITIVE)

	uint16_t twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	int i;
	int16_t  twiceLargestAntenna;
	struct cal_ctl_data_ar9287 *rep;
	CAL_TARGET_POWER_LEG targetPowerOfdm;
	CAL_TARGET_POWER_LEG targetPowerCck = {0, {0, 0, 0, 0}};
	CAL_TARGET_POWER_LEG targetPowerOfdmExt = {0, {0, 0, 0, 0}};
	CAL_TARGET_POWER_LEG targetPowerCckExt = {0, {0, 0, 0, 0}};
	CAL_TARGET_POWER_HT  targetPowerHt20;
	CAL_TARGET_POWER_HT  targetPowerHt40 = {0, {0, 0, 0, 0}};
	int16_t scaledPower, minCtlPower;

#define SUB_NUM_CTL_MODES_AT_2G_40 3   /* excluding HT40, EXT-OFDM, EXT-CCK */
	static const uint16_t ctlModesFor11g[] = {
	   CTL_11B, CTL_11G, CTL_2GHT20, CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40
	};
	const uint16_t *pCtlMode;
	uint16_t numCtlModes, ctlMode, freq;
	CHAN_CENTERS centers;

	ar5416GetChannelCenters(ah,  chan, &centers);

	/* Compute TxPower reduction due to Antenna Gain */

	twiceLargestAntenna = AH_MAX(
	    pEepData->modalHeader.antennaGainCh[0],
	    pEepData->modalHeader.antennaGainCh[1]);

	twiceLargestAntenna = (int16_t)AH_MIN((AntennaReduction) - twiceLargestAntenna, 0);

	/* XXX setup for 5212 use (really used?) */
	ath_hal_eepromSet(ah, AR_EEP_ANTGAINMAX_2, twiceLargestAntenna);

	/* 
	 * scaledPower is the minimum of the user input power level and
	 * the regulatory allowed power level
	 */
	scaledPower = AH_MIN(powerLimit, twiceMaxRegulatoryPower + twiceLargestAntenna);

	/* Reduce scaled Power by number of chains active to get to per chain tx power level */
	/* TODO: better value than these? */
	switch (owl_get_ntxchains(AH5416(ah)->ah_tx_chainmask)) {
	case 1:
		break;
	case 2:
		scaledPower -= REDUCE_SCALED_POWER_BY_TWO_CHAIN;
		break;
	default:
		return AH_FALSE; /* Unsupported number of chains */
	}

	scaledPower = AH_MAX(0, scaledPower);

	/* Get target powers from EEPROM - our baseline for TX Power */
	/* XXX assume channel is 2ghz */
	if (1) {
		/* Setup for CTL modes */
		numCtlModes = N(ctlModesFor11g) - SUB_NUM_CTL_MODES_AT_2G_40; /* CTL_11B, CTL_11G, CTL_2GHT20 */
		pCtlMode = ctlModesFor11g;

		ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPowerCck,
				AR9287_NUM_2G_CCK_TARGET_POWERS, &targetPowerCck, 4, AH_FALSE);
		ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPower2G,
				AR9287_NUM_2G_20_TARGET_POWERS, &targetPowerOfdm, 4, AH_FALSE);
		ar5416GetTargetPowers(ah,  chan, pEepData->calTargetPower2GHT20,
				AR9287_NUM_2G_20_TARGET_POWERS, &targetPowerHt20, 8, AH_FALSE);

		if (IEEE80211_IS_CHAN_HT40(chan)) {
			numCtlModes = N(ctlModesFor11g);    /* All 2G CTL's */

			ar5416GetTargetPowers(ah,  chan, pEepData->calTargetPower2GHT40,
				AR9287_NUM_2G_40_TARGET_POWERS, &targetPowerHt40, 8, AH_TRUE);
			/* Get target powers for extension channels */
			ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPowerCck,
				AR9287_NUM_2G_CCK_TARGET_POWERS, &targetPowerCckExt, 4, AH_TRUE);
			ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPower2G,
				AR9287_NUM_2G_20_TARGET_POWERS, &targetPowerOfdmExt, 4, AH_TRUE);
		}
	}

	/*
	 * For MIMO, need to apply regulatory caps individually across dynamically
	 * running modes: CCK, OFDM, HT20, HT40
	 *
	 * The outer loop walks through each possible applicable runtime mode.
	 * The inner loop walks through each ctlIndex entry in EEPROM.
	 * The ctl value is encoded as [7:4] == test group, [3:0] == test mode.
	 *
	 */
	for (ctlMode = 0; ctlMode < numCtlModes; ctlMode++) {
		HAL_BOOL isHt40CtlMode = (pCtlMode[ctlMode] == CTL_5GHT40) ||
		    (pCtlMode[ctlMode] == CTL_2GHT40);
		if (isHt40CtlMode) {
			freq = centers.ctl_center;
		} else if (pCtlMode[ctlMode] & EXT_ADDITIVE) {
			freq = centers.ext_center;
		} else {
			freq = centers.ctl_center;
		}

		/* walk through each CTL index stored in EEPROM */
		for (i = 0; (i < AR9287_NUM_CTLS) && pEepData->ctlIndex[i]; i++) {
			uint16_t twiceMinEdgePower;

			/* compare test group from regulatory channel list with test mode from pCtlMode list */
			if ((((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == pEepData->ctlIndex[i]) ||
				(((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == 
				 ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))) {
				rep = &(pEepData->ctlData[i]);
				twiceMinEdgePower = ar5416GetMaxEdgePower(freq,
							rep->ctlEdges[owl_get_ntxchains(AH5416(ah)->ah_tx_chainmask) - 1],
							IEEE80211_IS_CHAN_2GHZ(chan));
				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					/* Find the minimum of all CTL edge powers that apply to this channel */
					twiceMaxEdgePower = AH_MIN(twiceMaxEdgePower, twiceMinEdgePower);
				} else {
					/* specific */
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}
		minCtlPower = (uint8_t)AH_MIN(twiceMaxEdgePower, scaledPower);
		/* Apply ctl mode to correct target power set */
		switch(pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < N(targetPowerCck.tPow2x); i++) {
				targetPowerCck.tPow2x[i] = (uint8_t)AH_MIN(targetPowerCck.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0; i < N(targetPowerOfdm.tPow2x); i++) {
				targetPowerOfdm.tPow2x[i] = (uint8_t)AH_MIN(targetPowerOfdm.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0; i < N(targetPowerHt20.tPow2x); i++) {
				targetPowerHt20.tPow2x[i] = (uint8_t)AH_MIN(targetPowerHt20.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] = (uint8_t)AH_MIN(targetPowerCckExt.tPow2x[0], minCtlPower);
			break;
		case CTL_11A_EXT:
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] = (uint8_t)AH_MIN(targetPowerOfdmExt.tPow2x[0], minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0; i < N(targetPowerHt40.tPow2x); i++) {
				targetPowerHt40.tPow2x[i] = (uint8_t)AH_MIN(targetPowerHt40.tPow2x[i], minCtlPower);
			}
			break;
		default:
			return AH_FALSE;
			break;
		}
	} /* end ctl mode checking */

	/* Set rates Array from collected data */
	ar5416SetRatesArrayFromTargetPower(ah, chan, ratesArray,
	    &targetPowerCck,
	    &targetPowerCckExt,
	    &targetPowerOfdm,
	    &targetPowerOfdmExt,
	    &targetPowerHt20,
	    &targetPowerHt40);
	return AH_TRUE;
#undef EXT_ADDITIVE
#undef CTL_11A_EXT
#undef CTL_11G_EXT
#undef CTL_11B_EXT
#undef SUB_NUM_CTL_MODES_AT_5G_40
#undef SUB_NUM_CTL_MODES_AT_2G_40
#undef N
}

#undef REDUCE_SCALED_POWER_BY_TWO_CHAIN

/*
 * This is based off of the AR5416/AR9285 code and likely could
 * be unified in the future.
 */
HAL_BOOL
ar9287SetTransmitPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan, uint16_t *rfXpdGain)
{
#define	POW_SM(_r, _s)     (((_r) & 0x3f) << (_s))
#define	N(a)	    (sizeof (a) / sizeof (a[0]))

	const struct modal_eep_ar9287_header *pModal;
	struct ath_hal_5212 *ahp = AH5212(ah);
	int16_t	     txPowerIndexOffset = 0;
	int		 i;

	uint16_t	    cfgCtl;
	uint16_t	    powerLimit;
	uint16_t	    twiceAntennaReduction;
	uint16_t	    twiceMaxRegulatoryPower;
	int16_t	     maxPower;
	HAL_EEPROM_9287 *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ar9287_eeprom *pEepData = &ee->ee_base;

	AH5416(ah)->ah_ht40PowerIncForPdadc = 2;

	/* Setup info for the actual eeprom */
	OS_MEMZERO(AH5416(ah)->ah_ratesArray,
	  sizeof(AH5416(ah)->ah_ratesArray));
	cfgCtl = ath_hal_getctl(ah, chan);
	powerLimit = chan->ic_maxregpower * 2;
	twiceAntennaReduction = chan->ic_maxantgain;
	twiceMaxRegulatoryPower = AH_MIN(MAX_RATE_POWER,
	    AH_PRIVATE(ah)->ah_powerLimit);
	pModal = &pEepData->modalHeader;
	HALDEBUG(ah, HAL_DEBUG_RESET, "%s Channel=%u CfgCtl=%u\n",
	    __func__,chan->ic_freq, cfgCtl );

	/* XXX Assume Minor is v2 or later */
	AH5416(ah)->ah_ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;

	/* Fetch per-rate power table for the given channel */
	if (! ar9287SetPowerPerRateTable(ah, pEepData,  chan,
	    &AH5416(ah)->ah_ratesArray[0],
	    cfgCtl,
	    twiceAntennaReduction,
	    twiceMaxRegulatoryPower, powerLimit)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unable to set tx power per rate table\n", __func__);
		return AH_FALSE;
	}

	/* Set TX power control calibration curves for each TX chain */
	ar9287SetPowerCalTable(ah, chan, &txPowerIndexOffset);

	/* Calculate maximum power level */
	maxPower = AH_MAX(AH5416(ah)->ah_ratesArray[rate6mb],
	    AH5416(ah)->ah_ratesArray[rateHt20_0]);
	maxPower = AH_MAX(maxPower,
	    AH5416(ah)->ah_ratesArray[rate1l]);

	if (IEEE80211_IS_CHAN_HT40(chan))
		maxPower = AH_MAX(maxPower,
		    AH5416(ah)->ah_ratesArray[rateHt40_0]);

	ahp->ah_tx6PowerInHalfDbm = maxPower;
	AH_PRIVATE(ah)->ah_maxPowerLevel = maxPower;
	ahp->ah_txPowerIndexOffset = txPowerIndexOffset;

	/*
	 * txPowerIndexOffset is set by the SetPowerTable() call -
	 *  adjust the rate table (0 offset if rates EEPROM not loaded)
	 */
	/* XXX what about the pwrTableOffset? */
	for (i = 0; i < N(AH5416(ah)->ah_ratesArray); i++) {
		AH5416(ah)->ah_ratesArray[i] =
		    (int16_t)(txPowerIndexOffset +
		      AH5416(ah)->ah_ratesArray[i]);
		/* -5 dBm offset for Merlin and later; this includes Kiwi */
		AH5416(ah)->ah_ratesArray[i] -= AR5416_PWR_TABLE_OFFSET_DB * 2;
		if (AH5416(ah)->ah_ratesArray[i] > AR5416_MAX_RATE_POWER)
			AH5416(ah)->ah_ratesArray[i] = AR5416_MAX_RATE_POWER;
		if (AH5416(ah)->ah_ratesArray[i] < 0)
			AH5416(ah)->ah_ratesArray[i] = 0;
	}

#ifdef AH_EEPROM_DUMP
	ar5416PrintPowerPerRate(ah, AH5416(ah)->ah_ratesArray);
#endif

	/*
	 * Adjust the HT40 power to meet the correct target TX power
	 * for 40MHz mode, based on TX power curves that are established
	 * for 20MHz mode.
	 *
	 * XXX handle overflow/too high power level?
	 */
	if (IEEE80211_IS_CHAN_HT40(chan)) {
		AH5416(ah)->ah_ratesArray[rateHt40_0] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_1] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_2] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_3] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_4] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_5] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_6] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
		AH5416(ah)->ah_ratesArray[rateHt40_7] +=
		  AH5416(ah)->ah_ht40PowerIncForPdadc;
	}

	/* Write the TX power rate registers */
	ar5416WriteTxPowerRateRegisters(ah, chan, AH5416(ah)->ah_ratesArray);

	return AH_TRUE;
#undef POW_SM
#undef N
}

/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
HAL_BOOL
ar9287SetBoardValues(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	const HAL_EEPROM_9287 *ee = AH_PRIVATE(ah)->ah_eeprom;
	const struct ar9287_eeprom *eep = &ee->ee_base;
	const struct modal_eep_ar9287_header *pModal = &eep->modalHeader;
	uint16_t antWrites[AR9287_ANT_16S];
	uint32_t regChainOffset, regval;
	uint8_t txRxAttenLocal;
	int i, j, offset_num;

	pModal = &eep->modalHeader;

	antWrites[0] = (uint16_t)((pModal->antCtrlCommon >> 28) & 0xF);
	antWrites[1] = (uint16_t)((pModal->antCtrlCommon >> 24) & 0xF);
	antWrites[2] = (uint16_t)((pModal->antCtrlCommon >> 20) & 0xF);
	antWrites[3] = (uint16_t)((pModal->antCtrlCommon >> 16) & 0xF);
	antWrites[4] = (uint16_t)((pModal->antCtrlCommon >> 12) & 0xF);
	antWrites[5] = (uint16_t)((pModal->antCtrlCommon >> 8) & 0xF);
	antWrites[6] = (uint16_t)((pModal->antCtrlCommon >> 4)  & 0xF);
	antWrites[7] = (uint16_t)(pModal->antCtrlCommon & 0xF);

	offset_num = 8;

	for (i = 0, j = offset_num; i < AR9287_MAX_CHAINS; i++) {
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 28) & 0xf);
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 10) & 0x3);
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 8) & 0x3);
		antWrites[j++] = 0;
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 6) & 0x3);
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 4) & 0x3);
		antWrites[j++] = (uint16_t)((pModal->antCtrlChain[i] >> 2) & 0x3);
		antWrites[j++] = (uint16_t)(pModal->antCtrlChain[i] & 0x3);
	}

	OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, pModal->antCtrlCommon);

	for (i = 0; i < AR9287_MAX_CHAINS; i++)	{
		regChainOffset = i * 0x1000;

		OS_REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0 + regChainOffset,
			  pModal->antCtrlChain[i]);

		OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4_CHAIN(0) + regChainOffset,
			  (OS_REG_READ(ah, AR_PHY_TIMING_CTRL4_CHAIN(0)
			      + regChainOffset)
			   & ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
			       AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
			  SM(pModal->iqCalICh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
			  SM(pModal->iqCalQCh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

		txRxAttenLocal = pModal->txRxAttenCh[i];

		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + regChainOffset,
			      AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
			      pModal->bswMargin[i]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + regChainOffset,
			      AR_PHY_GAIN_2GHZ_XATTEN1_DB,
			      pModal->bswAtten[i]);
		OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN + regChainOffset,
			      AR9280_PHY_RXGAIN_TXRX_ATTEN,
			      txRxAttenLocal);
		OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN + regChainOffset,
			      AR9280_PHY_RXGAIN_TXRX_MARGIN,
			      pModal->rxTxMarginCh[i]);
	}

	if (IEEE80211_IS_CHAN_HT40(chan))
		OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING,
			      AR_PHY_SETTLING_SWITCH, pModal->swSettleHt40);
	else
		OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING,
			      AR_PHY_SETTLING_SWITCH, pModal->switchSettling);

	OS_REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
		      AR_PHY_DESIRED_SZ_ADC, pModal->adcDesiredSize);

	OS_REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF)
		  | SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAB_OFF)
		  | SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAA_ON)
		  | SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL3,
		      AR_PHY_TX_END_TO_A2_RX_ON, pModal->txEndToRxOn);

	OS_REG_RMW_FIELD(ah, AR_PHY_CCA,
		      AR9280_PHY_CCA_THRESH62, pModal->thresh62);
	OS_REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0,
		      AR_PHY_EXT_CCA0_THRESH62, pModal->thresh62);

	regval = OS_REG_READ(ah, AR9287_AN_RF2G3_CH0);
	regval &= ~(AR9287_AN_RF2G3_DB1 |
		    AR9287_AN_RF2G3_DB2 |
		    AR9287_AN_RF2G3_OB_CCK |
		    AR9287_AN_RF2G3_OB_PSK |
		    AR9287_AN_RF2G3_OB_QAM |
		    AR9287_AN_RF2G3_OB_PAL_OFF);
	regval |= (SM(pModal->db1, AR9287_AN_RF2G3_DB1) |
		   SM(pModal->db2, AR9287_AN_RF2G3_DB2) |
		   SM(pModal->ob_cck, AR9287_AN_RF2G3_OB_CCK) |
		   SM(pModal->ob_psk, AR9287_AN_RF2G3_OB_PSK) |
		   SM(pModal->ob_qam, AR9287_AN_RF2G3_OB_QAM) |
		   SM(pModal->ob_pal_off, AR9287_AN_RF2G3_OB_PAL_OFF));

	/* Analog write - requires a 100usec delay */
	OS_A_REG_WRITE(ah, AR9287_AN_RF2G3_CH0, regval);

	regval = OS_REG_READ(ah, AR9287_AN_RF2G3_CH1);
	regval &= ~(AR9287_AN_RF2G3_DB1 |
		    AR9287_AN_RF2G3_DB2 |
		    AR9287_AN_RF2G3_OB_CCK |
		    AR9287_AN_RF2G3_OB_PSK |
		    AR9287_AN_RF2G3_OB_QAM |
		    AR9287_AN_RF2G3_OB_PAL_OFF);
	regval |= (SM(pModal->db1, AR9287_AN_RF2G3_DB1) |
		   SM(pModal->db2, AR9287_AN_RF2G3_DB2) |
		   SM(pModal->ob_cck, AR9287_AN_RF2G3_OB_CCK) |
		   SM(pModal->ob_psk, AR9287_AN_RF2G3_OB_PSK) |
		   SM(pModal->ob_qam, AR9287_AN_RF2G3_OB_QAM) |
		   SM(pModal->ob_pal_off, AR9287_AN_RF2G3_OB_PAL_OFF));

	OS_A_REG_WRITE(ah, AR9287_AN_RF2G3_CH1, regval);

	OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
	    AR_PHY_TX_FRAME_TO_DATA_START, pModal->txFrameToDataStart);
	OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
	    AR_PHY_TX_FRAME_TO_PA_ON, pModal->txFrameToPaOn);

	OS_A_REG_RMW_FIELD(ah, AR9287_AN_TOP2,
	    AR9287_AN_TOP2_XPABIAS_LVL, pModal->xpaBiasLvl);

	return AH_TRUE;
}
