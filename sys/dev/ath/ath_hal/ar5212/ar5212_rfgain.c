/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#include "ah_eeprom_v3.h"

static const GAIN_OPTIMIZATION_LADDER gainLadder = {
	9,					/* numStepsInLadder */
	4,					/* defaultStepNum */
	{ { {4, 1, 1, 1},  6, "FG8"},
	  { {4, 0, 1, 1},  4, "FG7"},
	  { {3, 1, 1, 1},  3, "FG6"},
	  { {4, 0, 0, 1},  1, "FG5"},
	  { {4, 1, 1, 0},  0, "FG4"},	/* noJack */
	  { {4, 0, 1, 0}, -2, "FG3"},	/* halfJack */
	  { {3, 1, 1, 0}, -3, "FG2"},	/* clip3 */
	  { {4, 0, 0, 0}, -4, "FG1"},	/* noJack */
	  { {2, 1, 1, 0}, -6, "FG0"} 	/* clip2 */
	}
};

static const GAIN_OPTIMIZATION_LADDER gainLadder5112 = {
	8,					/* numStepsInLadder */
	1,					/* defaultStepNum */
	{ { {3, 0,0,0, 0,0,0},   6, "FG7"},	/* most fixed gain */
	  { {2, 0,0,0, 0,0,0},   0, "FG6"},
	  { {1, 0,0,0, 0,0,0},  -3, "FG5"},
	  { {0, 0,0,0, 0,0,0},  -6, "FG4"},
	  { {0, 1,1,0, 0,0,0},  -8, "FG3"},
	  { {0, 1,1,0, 1,1,0}, -10, "FG2"},
	  { {0, 1,0,1, 1,1,0}, -13, "FG1"},
	  { {0, 1,0,1, 1,0,1}, -16, "FG0"},	/* least fixed gain */
	}
};

/*
 * Initialize the gain structure to good values
 */
void
ar5212InitializeGainValues(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	GAIN_VALUES *gv = &ahp->ah_gainValues;

	/* initialize gain optimization values */
	if (IS_RAD5112_ANY(ah)) {
		gv->currStepNum = gainLadder5112.defaultStepNum;
		gv->currStep =
			&gainLadder5112.optStep[gainLadder5112.defaultStepNum];
		gv->active = AH_TRUE;
		gv->loTrig = 20;
		gv->hiTrig = 85;
	} else {
		gv->currStepNum = gainLadder.defaultStepNum;
		gv->currStep = &gainLadder.optStep[gainLadder.defaultStepNum];
		gv->active = AH_TRUE;
		gv->loTrig = 20;
		gv->hiTrig = 35;
	}
}

#define	MAX_ANALOG_START	319		/* XXX */

/*
 * Find analog bits of given parameter data and return a reversed value
 */
static uint32_t
ar5212GetRfField(uint32_t *rfBuf, uint32_t numBits, uint32_t firstBit, uint32_t column)
{
	uint32_t reg32 = 0, mask, arrayEntry, lastBit;
	uint32_t bitPosition, bitsShifted;
	int32_t bitsLeft;

	HALASSERT(column <= 3);
	HALASSERT(numBits <= 32);
	HALASSERT(firstBit + numBits <= MAX_ANALOG_START);

	arrayEntry = (firstBit - 1) / 8;
	bitPosition = (firstBit - 1) % 8;
	bitsLeft = numBits;
	bitsShifted = 0;
	while (bitsLeft > 0) {
		lastBit = (bitPosition + bitsLeft > 8) ?
			(8) : (bitPosition + bitsLeft);
		mask = (((1 << lastBit) - 1) ^ ((1 << bitPosition) - 1)) <<
			(column * 8);
		reg32 |= (((rfBuf[arrayEntry] & mask) >> (column * 8)) >>
			bitPosition) << bitsShifted;
		bitsShifted += lastBit - bitPosition;
		bitsLeft -= (8 - bitPosition);
		bitPosition = 0;
		arrayEntry++;
	}
	reg32 = ath_hal_reverseBits(reg32, numBits);
	return reg32;
}

static HAL_BOOL
ar5212InvalidGainReadback(struct ath_hal *ah, GAIN_VALUES *gv)
{
	uint32_t gStep, g, mixOvr;
	uint32_t L1, L2, L3, L4;

	if (IS_RAD5112_ANY(ah)) {
		mixOvr = ar5212GetRfField(ar5212GetRfBank(ah, 7), 1, 36, 0);
		L1 = 0;
		L2 = 107;
		L3 = 0;
		L4 = 107;
		if (mixOvr == 1) {
			L2 = 83;
			L4 = 83;
			gv->hiTrig = 55;
		}
	} else {
		gStep = ar5212GetRfField(ar5212GetRfBank(ah, 7), 6, 37, 0);

		L1 = 0;
		L2 = (gStep == 0x3f) ? 50 : gStep + 4;
		L3 = (gStep != 0x3f) ? 0x40 : L1;
		L4 = L3 + 50;

		gv->loTrig = L1 + (gStep == 0x3f ? DYN_ADJ_LO_MARGIN : 0);
		/* never adjust if != 0x3f */
		gv->hiTrig = L4 - (gStep == 0x3f ? DYN_ADJ_UP_MARGIN : -5);
	}
	g = gv->currGain;

	return !((g >= L1 && g<= L2) || (g >= L3 && g <= L4));
}

/*
 * Enable the probe gain check on the next packet
 */
void
ar5212RequestRfgain(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t probePowerIndex;

	/* Enable the gain readback probe */
	probePowerIndex = ahp->ah_ofdmTxPower + ahp->ah_txPowerIndexOffset;
	OS_REG_WRITE(ah, AR_PHY_PAPD_PROBE,
		  SM(probePowerIndex, AR_PHY_PAPD_PROBE_POWERTX)
		| AR_PHY_PAPD_PROBE_NEXT_TX);

	ahp->ah_rfgainState = HAL_RFGAIN_READ_REQUESTED;
}

/*
 * Check to see if our readback gain level sits within the linear
 * region of our current variable attenuation window
 */
static HAL_BOOL
ar5212IsGainAdjustNeeded(struct ath_hal *ah, const GAIN_VALUES *gv)
{
	return (gv->currGain <= gv->loTrig || gv->currGain >= gv->hiTrig);
}

/*
 * Move the rabbit ears in the correct direction.
 */
static int32_t 
ar5212AdjustGain(struct ath_hal *ah, GAIN_VALUES *gv)
{
	const GAIN_OPTIMIZATION_LADDER *gl;

	if (IS_RAD5112_ANY(ah))
		gl = &gainLadder5112;
	else
		gl = &gainLadder;
	gv->currStep = &gl->optStep[gv->currStepNum];
	if (gv->currGain >= gv->hiTrig) {
		if (gv->currStepNum == 0) {
			HALDEBUG(ah, HAL_DEBUG_ANY, "%s: Max gain limit.\n",
			    __func__);
			return -1;
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Adding gain: currG=%d [%s] --> ",
		    __func__, gv->currGain, gv->currStep->stepName);
		gv->targetGain = gv->currGain;
		while (gv->targetGain >= gv->hiTrig && gv->currStepNum > 0) {
			gv->targetGain -= 2 * (gl->optStep[--(gv->currStepNum)].stepGain -
				gv->currStep->stepGain);
			gv->currStep = &gl->optStep[gv->currStepNum];
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "targG=%d [%s]\n",
		    gv->targetGain, gv->currStep->stepName);
		return 1;
	}
	if (gv->currGain <= gv->loTrig) {
		if (gv->currStepNum == gl->numStepsInLadder-1) {
			HALDEBUG(ah, HAL_DEBUG_RFPARAM,
			    "%s: Min gain limit.\n", __func__);
			return -2;
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Deducting gain: currG=%d [%s] --> ",
		    __func__, gv->currGain, gv->currStep->stepName);
		gv->targetGain = gv->currGain;
		while (gv->targetGain <= gv->loTrig &&
		      gv->currStepNum < (gl->numStepsInLadder - 1)) {
			gv->targetGain -= 2 *
				(gl->optStep[++(gv->currStepNum)].stepGain - gv->currStep->stepGain);
			gv->currStep = &gl->optStep[gv->currStepNum];
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "targG=%d [%s]\n",
		    gv->targetGain, gv->currStep->stepName);
		return 2;
	}
	return 0;		/* caller didn't call needAdjGain first */
}

/*
 * Read rf register to determine if gainF needs correction
 */
static uint32_t
ar5212GetGainFCorrection(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t correction;

	HALASSERT(IS_RADX112_REV2(ah));

	correction = 0;
	if (ar5212GetRfField(ar5212GetRfBank(ah, 7), 1, 36, 0) == 1) {
		const GAIN_VALUES *gv = &ahp->ah_gainValues;
		uint32_t mixGain = gv->currStep->paramVal[0];
		uint32_t gainStep =
			ar5212GetRfField(ar5212GetRfBank(ah, 7), 4, 32, 0);
		switch (mixGain) {
		case 0 :
			correction = 0;
			break;
		case 1 :
			correction = gainStep;
			break;
		case 2 :
			correction = 2 * gainStep - 5;
			break;
		case 3 :
			correction = 2 * gainStep;
			break;
		}
	}
	return correction;
}

/*
 * Exported call to check for a recent gain reading and return
 * the current state of the thermal calibration gain engine.
 */
HAL_RFGAIN
ar5212GetRfgain(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	GAIN_VALUES *gv = &ahp->ah_gainValues;
	uint32_t rddata, probeType;

	/* NB: beware of touching the BB when PHY is powered down */
	if (!gv->active || !ahp->ah_phyPowerOn)
		return HAL_RFGAIN_INACTIVE;

	if (ahp->ah_rfgainState == HAL_RFGAIN_READ_REQUESTED) {
		/* Caller had asked to setup a new reading. Check it. */
		rddata = OS_REG_READ(ah, AR_PHY_PAPD_PROBE);

		if ((rddata & AR_PHY_PAPD_PROBE_NEXT_TX) == 0) {
			/* bit got cleared, we have a new reading. */
			gv->currGain = rddata >> AR_PHY_PAPD_PROBE_GAINF_S;
			probeType = MS(rddata, AR_PHY_PAPD_PROBE_TYPE);
			if (probeType == AR_PHY_PAPD_PROBE_TYPE_CCK) {
				const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

				HALASSERT(IS_RAD5112_ANY(ah));
				HALASSERT(ah->ah_magic == AR5212_MAGIC);
				if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_2)
					gv->currGain += ee->ee_cckOfdmGainDelta;
				else
					gv->currGain += PHY_PROBE_CCK_CORRECTION;
			}
			if (IS_RADX112_REV2(ah)) {
				uint32_t correct = ar5212GetGainFCorrection(ah);
				if (gv->currGain >= correct)
					gv->currGain -= correct;
				else
					gv->currGain = 0;
			}
			/* inactive by default */
			ahp->ah_rfgainState = HAL_RFGAIN_INACTIVE;

			if (!ar5212InvalidGainReadback(ah, gv) &&
			    ar5212IsGainAdjustNeeded(ah, gv) &&
			    ar5212AdjustGain(ah, gv) > 0) {
				/*
				 * Change needed. Copy ladder info
				 * into eeprom info.
				 */
				ahp->ah_rfgainState = HAL_RFGAIN_NEED_CHANGE;
				/* for ap51 */
				ahp->ah_cwCalRequire = AH_TRUE;
				/* Request IQ recalibration for temperature chang */
				ahp->ah_bIQCalibration = IQ_CAL_INACTIVE;
			}
		}
	}
	return ahp->ah_rfgainState;
}
