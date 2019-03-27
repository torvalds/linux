/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Adrian Chadd, Xenion Pty Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ah_eeprom_v14.h"

#include "ar9002/ar9280.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar9002/ar9002phy.h"

#include "ar9002/ar9280_olc.h"

void
ar9280olcInit(struct ath_hal *ah)
{
	uint32_t i;

	/* Only do OLC if it's enabled for this chipset */
	if (! ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL))
		return;

	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Setting up TX gain tables.\n", __func__);

	for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++)
		AH9280(ah)->originalGain[i] = MS(OS_REG_READ(ah,
		    AR_PHY_TX_GAIN_TBL1 + i * 4), AR_PHY_TX_GAIN);

	AH9280(ah)->PDADCdelta = 0;
}

void
ar9280olcGetTxGainIndex(struct ath_hal *ah,
    const struct ieee80211_channel *chan,
    struct calDataPerFreqOpLoop *rawDatasetOpLoop,
    uint8_t *calChans, uint16_t availPiers, uint8_t *pwr, uint8_t *pcdacIdx)
{
	uint8_t pcdac, i = 0;
	uint16_t idxL = 0, idxR = 0, numPiers;
	HAL_BOOL match;
	CHAN_CENTERS centers;

	ar5416GetChannelCenters(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++)
		if (calChans[numPiers] == AR5416_BCHAN_UNUSED)
			break;

	match = ath_ee_getLowerUpperIndex((uint8_t)FREQ2FBIN(centers.synth_center,
		    IEEE80211_IS_CHAN_2GHZ(chan)), calChans, numPiers,
		    &idxL, &idxR);
	if (match) {
		pcdac = rawDatasetOpLoop[idxL].pcdac[0][0];
		*pwr = rawDatasetOpLoop[idxL].pwrPdg[0][0];
	} else {
		pcdac = rawDatasetOpLoop[idxR].pcdac[0][0];
		*pwr = (rawDatasetOpLoop[idxL].pwrPdg[0][0] +
				rawDatasetOpLoop[idxR].pwrPdg[0][0])/2;
	}
	while (pcdac > AH9280(ah)->originalGain[i] &&
			i < (AR9280_TX_GAIN_TABLE_SIZE - 1))
		i++;

	*pcdacIdx = i;
}

/*
 * XXX txPower here is likely not the target txPower in the traditional
 * XXX sense, but is set by a call to ar9280olcGetTxGainIndex().
 * XXX Thus, be careful if you're trying to use this routine yourself.
 */
void
ar9280olcGetPDADCs(struct ath_hal *ah, uint32_t initTxGain, int txPower,
    uint8_t *pPDADCValues)
{
	uint32_t i;
	uint32_t offset;

	OS_REG_RMW_FIELD(ah, AR_PHY_TX_PWRCTRL6_0, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
	OS_REG_RMW_FIELD(ah, AR_PHY_TX_PWRCTRL6_1, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);

	OS_REG_RMW_FIELD(ah, AR_PHY_TX_PWRCTRL7, AR_PHY_TX_PWRCTRL_INIT_TX_GAIN, initTxGain);

	offset = txPower;
	for (i = 0; i < AR5416_NUM_PDADC_VALUES; i++)
		if (i < offset)
			pPDADCValues[i] = 0x0;
		else
			pPDADCValues[i] = 0xFF;
}

/*
 * Run temperature compensation calibration.
 *
 * The TX gain table is adjusted depending upon the difference
 * between the initial PDADC value and the currently read
 * average TX power sample value. This value is only valid if
 * frames have been transmitted, so currPDADC will be 0 if
 * no frames have yet been transmitted.
 */
void
ar9280olcTemperatureCompensation(struct ath_hal *ah)
{
	uint32_t rddata, i;
	int delta, currPDADC, regval;
	uint8_t hpwr_5g = 0;

	if (! ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL))
		return;

	rddata = OS_REG_READ(ah, AR_PHY_TX_PWRCTRL4);
	currPDADC = MS(rddata, AR_PHY_TX_PWRCTRL_PD_AVG_OUT);

	HALDEBUG(ah, HAL_DEBUG_PERCAL,
	    "%s: called: initPDADC=%d, currPDADC=%d\n",
	    __func__, AH5416(ah)->initPDADC, currPDADC);

	if (AH5416(ah)->initPDADC == 0 || currPDADC == 0)
		return;

	(void) (ath_hal_eepromGet(ah, AR_EEP_DAC_HPWR_5G, &hpwr_5g));

	if (hpwr_5g)
		delta = (currPDADC - AH5416(ah)->initPDADC + 4) / 8;
	else
		delta = (currPDADC - AH5416(ah)->initPDADC + 5) / 10;

	HALDEBUG(ah, HAL_DEBUG_PERCAL, "%s: delta=%d, PDADCdelta=%d\n",
	    __func__, delta, AH9280(ah)->PDADCdelta);

	if (delta != AH9280(ah)->PDADCdelta) {
		AH9280(ah)->PDADCdelta = delta;
		for (i = 1; i < AR9280_TX_GAIN_TABLE_SIZE; i++) {
			regval = AH9280(ah)->originalGain[i] - delta;
			if (regval < 0)
				regval = 0;

			OS_REG_RMW_FIELD(ah,
				      AR_PHY_TX_GAIN_TBL1 + i * 4,
				      AR_PHY_TX_GAIN, regval);
		}
	}
}


static int16_t
ar9280ChangeGainBoundarySettings(struct ath_hal *ah, uint16_t *gb,
    uint16_t numXpdGain, uint16_t pdGainOverlap_t2, int8_t pwr_table_offset,
    int16_t *diff)
{
	uint16_t k;

	/* Prior to writing the boundaries or the pdadc vs. power table
	 * into the chip registers the default starting point on the pdadc
	 * vs. power table needs to be checked and the curve boundaries
	 * adjusted accordingly
	 */
	if (AR_SREV_MERLIN_20_OR_LATER(ah)) {
		uint16_t gb_limit;

		if (AR5416_PWR_TABLE_OFFSET_DB != pwr_table_offset) {
			/* get the difference in dB */
			*diff = (uint16_t)(pwr_table_offset - AR5416_PWR_TABLE_OFFSET_DB);
			/* get the number of half dB steps */
			*diff *= 2;
			/* change the original gain boundary settings
			 * by the number of half dB steps
			 */
			for (k = 0; k < numXpdGain; k++)
				gb[k] = (uint16_t)(gb[k] - *diff);
		}
		/* Because of a hardware limitation, ensure the gain boundary
		 * is not larger than (63 - overlap)
		 */
		gb_limit = (uint16_t)(AR5416_MAX_RATE_POWER - pdGainOverlap_t2);

		for (k = 0; k < numXpdGain; k++)
			gb[k] = (uint16_t)min(gb_limit, gb[k]);
	}

	return *diff;
}

static void
ar9280AdjustPDADCValues(struct ath_hal *ah, int8_t pwr_table_offset,
    int16_t diff, uint8_t *pdadcValues)
{
#define NUM_PDADC(diff) (AR5416_NUM_PDADC_VALUES - diff)
	uint16_t k;

	/* If this is a board that has a pwrTableOffset that differs from
	 * the default AR5416_PWR_TABLE_OFFSET_DB then the start of the
	 * pdadc vs pwr table needs to be adjusted prior to writing to the
	 * chip.
	 */
	if (AR_SREV_MERLIN_20_OR_LATER(ah)) {
		if (AR5416_PWR_TABLE_OFFSET_DB != pwr_table_offset) {
			/* shift the table to start at the new offset */
			for (k = 0; k < (uint16_t)NUM_PDADC(diff); k++ ) {
				pdadcValues[k] = pdadcValues[k + diff];
			}

			/* fill the back of the table */
			for (k = (uint16_t)NUM_PDADC(diff); k < NUM_PDADC(0); k++) {
				pdadcValues[k] = pdadcValues[NUM_PDADC(diff)];
			}
		}
	}
#undef NUM_PDADC
}
/*
 * This effectively disables the gain boundaries leaving it
 * to the open-loop TX power control.
 */
static void
ar9280SetGainBoundariesOpenLoop(struct ath_hal *ah, int i,
    uint16_t pdGainOverlap_t2, uint16_t gainBoundaries[])
{
	int regChainOffset;

	regChainOffset = ar5416GetRegChainOffset(ah, i);

	/* These are unused for OLC */
	(void) pdGainOverlap_t2;
	(void) gainBoundaries;

	HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: chain %d: writing closed loop values\n",
	    __func__, i);

	OS_REG_WRITE(ah, AR_PHY_TPCRG5 + regChainOffset,
	    SM(0x6, AR_PHY_TPCRG5_PD_GAIN_OVERLAP) |
	    SM(0x38, AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)  |
	    SM(0x38, AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)  |
	    SM(0x38, AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)  |
	    SM(0x38, AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));
}

/* Eeprom versioning macros. Returns true if the version is equal or newer than the ver specified */
/* XXX shouldn't be here! */
#define EEP_MINOR(_ah) \
        (AH_PRIVATE(_ah)->ah_eeversion & AR5416_EEP_VER_MINOR_MASK)
#define IS_EEP_MINOR_V2(_ah)    (EEP_MINOR(_ah) >= AR5416_EEP_MINOR_VER_2)
#define IS_EEP_MINOR_V3(_ah)    (EEP_MINOR(_ah) >= AR5416_EEP_MINOR_VER_3)

/**************************************************************
 * ar9280SetPowerCalTable
 *
 * Pull the PDADC piers from cal data and interpolate them across the given
 * points as well as from the nearest pier(s) to get a power detector
 * linear voltage to power level table.
 *
 * Handle OLC for Merlin where required.
 */
HAL_BOOL
ar9280SetPowerCalTable(struct ath_hal *ah, struct ar5416eeprom *pEepData,
	const struct ieee80211_channel *chan, int16_t *pTxPowerIndexOffset)
{
	CAL_DATA_PER_FREQ *pRawDataset;
	uint8_t  *pCalBChans = AH_NULL;
	uint16_t pdGainOverlap_t2;
	static uint8_t  pdadcValues[AR5416_NUM_PDADC_VALUES];
	uint16_t gainBoundaries[AR5416_PD_GAINS_IN_MASK];
	uint16_t numPiers, i;
	int16_t  tMinCalPower;
	uint16_t numXpdGain, xpdMask;
	uint16_t xpdGainValues[AR5416_NUM_PD_GAINS];
	uint32_t regChainOffset;
	int8_t pwr_table_offset;

	OS_MEMZERO(xpdGainValues, sizeof(xpdGainValues));
	    
	xpdMask = pEepData->modalHeader[IEEE80211_IS_CHAN_2GHZ(chan)].xpdGain;

	(void) ath_hal_eepromGet(ah, AR_EEP_PWR_TABLE_OFFSET, &pwr_table_offset);


	if (IS_EEP_MINOR_V2(ah)) {
		pdGainOverlap_t2 = pEepData->modalHeader[IEEE80211_IS_CHAN_2GHZ(chan)].pdGainOverlap;
	} else { 
		pdGainOverlap_t2 = (uint16_t)(MS(OS_REG_READ(ah, AR_PHY_TPCRG5), AR_PHY_TPCRG5_PD_GAIN_OVERLAP));
	}

	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		pCalBChans = pEepData->calFreqPier2G;
		numPiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pCalBChans = pEepData->calFreqPier5G;
		numPiers = AR5416_NUM_5G_CAL_PIERS;
	}

	/* If OLC is being done, set the init PDADC value appropriately */
	if (IEEE80211_IS_CHAN_2GHZ(chan) && AR_SREV_MERLIN_20_OR_LATER(ah) &&
	    ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL)) {
		struct calDataPerFreq *pRawDataset = pEepData->calPierData2G[0];
		AH5416(ah)->initPDADC = ((struct calDataPerFreqOpLoop *) pRawDataset)->vpdPdg[0][0];
	} else {
		/*
		 * XXX ath9k doesn't clear this for 5ghz mode if
		 * it were set in 2ghz mode before!
		 * The Merlin OLC temperature compensation code
		 * uses this to calculate the PDADC delta during
		 * calibration ; 0 here effectively stops the
		 * temperature compensation calibration from
		 * occurring.
		 */
		AH5416(ah)->initPDADC = 0;
	}

	/* Calculate the value of xpdgains from the xpdGain Mask */
	numXpdGain = ar5416GetXpdGainValues(ah, xpdMask, xpdGainValues);
	    
	/* Write the detector gain biases and their number */
	ar5416WriteDetectorGainBiases(ah, numXpdGain, xpdGainValues);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		regChainOffset = ar5416GetRegChainOffset(ah, i);
		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			uint16_t diff;

			if (IEEE80211_IS_CHAN_2GHZ(chan)) {
				pRawDataset = pEepData->calPierData2G[i];
			} else {
				pRawDataset = pEepData->calPierData5G[i];
			}

			/* Fetch the gain boundaries and the PDADC values */
			if (AR_SREV_MERLIN_20_OR_LATER(ah) &&
			    ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL)) {
				uint8_t pcdacIdx;
				uint8_t txPower;

				ar9280olcGetTxGainIndex(ah, chan,
				    (struct calDataPerFreqOpLoop *) pRawDataset,
				    pCalBChans, numPiers, &txPower, &pcdacIdx);
				ar9280olcGetPDADCs(ah, pcdacIdx, txPower / 2, pdadcValues);
			} else {
				ar5416GetGainBoundariesAndPdadcs(ah,  chan,
				    pRawDataset, pCalBChans, numPiers,
				    pdGainOverlap_t2, &tMinCalPower,
				    gainBoundaries, pdadcValues, numXpdGain);
			}

			/*
			 * Prior to writing the boundaries or the pdadc vs. power table
			 * into the chip registers the default starting point on the pdadc
			 * vs. power table needs to be checked and the curve boundaries
			 * adjusted accordingly
			 */
			diff = ar9280ChangeGainBoundarySettings(ah,
			    gainBoundaries, numXpdGain, pdGainOverlap_t2,
			    pwr_table_offset, &diff);

			if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {
				/* Set gain boundaries for either open- or closed-loop TPC */
				if (AR_SREV_MERLIN_20_OR_LATER(ah) &&
				    ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL))
					ar9280SetGainBoundariesOpenLoop(ah,
					    i, pdGainOverlap_t2,
					    gainBoundaries);
				else
					ar5416SetGainBoundariesClosedLoop(ah,
					    i, pdGainOverlap_t2,
					    gainBoundaries);
			}

			/*
			 * If this is a board that has a pwrTableOffset that differs from
			 * the default AR5416_PWR_TABLE_OFFSET_DB then the start of the
			 * pdadc vs pwr table needs to be adjusted prior to writing to the
			 * chip.
			 */
			ar9280AdjustPDADCValues(ah, pwr_table_offset, diff, pdadcValues);

			/* Write the power values into the baseband power table */
			ar5416WritePdadcValues(ah, i, pdadcValues);
		}
	}
	*pTxPowerIndexOffset = 0;

	return AH_TRUE;
}
