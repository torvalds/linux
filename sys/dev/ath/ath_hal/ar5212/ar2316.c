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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#include "ah_eeprom_v3.h"

#define AH_5212_2316
#include "ar5212/ar5212.ini"

#define	N(a)	(sizeof(a)/sizeof(a[0]))

typedef	RAW_DATA_STRUCT_2413 RAW_DATA_STRUCT_2316;
typedef RAW_DATA_PER_CHANNEL_2413 RAW_DATA_PER_CHANNEL_2316;
#define PWR_TABLE_SIZE_2316 PWR_TABLE_SIZE_2413

struct ar2316State {
	RF_HAL_FUNCS	base;		/* public state, must be first */
	uint16_t	pcdacTable[PWR_TABLE_SIZE_2316];

	uint32_t	Bank1Data[N(ar5212Bank1_2316)];
	uint32_t	Bank2Data[N(ar5212Bank2_2316)];
	uint32_t	Bank3Data[N(ar5212Bank3_2316)];
	uint32_t	Bank6Data[N(ar5212Bank6_2316)];
	uint32_t	Bank7Data[N(ar5212Bank7_2316)];

	/*
	 * Private state for reduced stack usage.
	 */
	/* filled out Vpd table for all pdGains (chanL) */
	uint16_t vpdTable_L[MAX_NUM_PDGAINS_PER_CHANNEL]
			    [MAX_PWR_RANGE_IN_HALF_DB];
	/* filled out Vpd table for all pdGains (chanR) */
	uint16_t vpdTable_R[MAX_NUM_PDGAINS_PER_CHANNEL]
			    [MAX_PWR_RANGE_IN_HALF_DB];
	/* filled out Vpd table for all pdGains (interpolated) */
	uint16_t vpdTable_I[MAX_NUM_PDGAINS_PER_CHANNEL]
			    [MAX_PWR_RANGE_IN_HALF_DB];
};
#define	AR2316(ah)	((struct ar2316State *) AH5212(ah)->ah_rfHal)

extern	void ar5212ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32,
		uint32_t numBits, uint32_t firstBit, uint32_t column);

static void
ar2316WriteRegs(struct ath_hal *ah, u_int modesIndex, u_int freqIndex,
	int regWrites)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HAL_INI_WRITE_ARRAY(ah, ar5212Modes_2316, modesIndex, regWrites);
	HAL_INI_WRITE_ARRAY(ah, ar5212Common_2316, 1, regWrites);
	HAL_INI_WRITE_ARRAY(ah, ar5212BB_RfGain_2316, freqIndex, regWrites);

	/* For AP51 */
        if (!ahp->ah_cwCalRequire) {
		OS_REG_WRITE(ah, 0xa358, (OS_REG_READ(ah, 0xa358) & ~0x2));
        } else {
		ahp->ah_cwCalRequire = AH_FALSE;
        }
}

/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 */
static HAL_BOOL
ar2316SetChannel(struct ath_hal *ah,  struct ieee80211_channel *chan)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	uint32_t channelSel  = 0;
	uint32_t bModeSynth  = 0;
	uint32_t aModeRefSel = 0;
	uint32_t reg32       = 0;

	OS_MARK(ah, AH_MARK_SETCHANNEL, freq);

	if (freq < 4800) {
		uint32_t txctl;

		if (((freq - 2192) % 5) == 0) {
			channelSel = ((freq - 672) * 2 - 3040)/10;
			bModeSynth = 0;
		} else if (((freq - 2224) % 5) == 0) {
			channelSel = ((freq - 704) * 2 - 3040) / 10;
			bModeSynth = 1;
		} else {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid channel %u MHz\n",
			    __func__, freq);
			return AH_FALSE;
		}

		channelSel = (channelSel << 2) & 0xff;
		channelSel = ath_hal_reverseBits(channelSel, 8);

		txctl = OS_REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {
			/* Enable channel spreading for channel 14 */
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
		}
	} else if ((freq % 20) == 0 && freq >= 5120) {
		channelSel = ath_hal_reverseBits(
			((freq - 4800) / 20 << 2), 8);
		aModeRefSel = ath_hal_reverseBits(3, 2);
	} else if ((freq % 10) == 0) {
		channelSel = ath_hal_reverseBits(
			((freq - 4800) / 10 << 1), 8);
		aModeRefSel = ath_hal_reverseBits(2, 2);
	} else if ((freq % 5) == 0) {
		channelSel = ath_hal_reverseBits(
			(freq - 4800) / 5, 8);
		aModeRefSel = ath_hal_reverseBits(1, 2);
	} else {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel %u MHz\n",
		    __func__, freq);
		return AH_FALSE;
	}

	reg32 = (channelSel << 4) | (aModeRefSel << 2) | (bModeSynth << 1) |
			(1 << 12) | 0x1;
	OS_REG_WRITE(ah, AR_PHY(0x27), reg32 & 0xff);

	reg32 >>= 8;
	OS_REG_WRITE(ah, AR_PHY(0x36), reg32 & 0x7f);

	AH_PRIVATE(ah)->ah_curchan = chan;
	return AH_TRUE;
}

/*
 * Reads EEPROM header info from device structure and programs
 * all rf registers
 *
 * REQUIRES: Access to the analog rf device
 */
static HAL_BOOL
ar2316SetRfRegs(struct ath_hal *ah, const struct ieee80211_channel *chan,
	uint16_t modesIndex, uint16_t *rfXpdGain)
{
#define	RF_BANK_SETUP(_priv, _ix, _col) do {				    \
	int i;								    \
	for (i = 0; i < N(ar5212Bank##_ix##_2316); i++)			    \
		(_priv)->Bank##_ix##Data[i] = ar5212Bank##_ix##_2316[i][_col];\
} while (0)
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint16_t ob2GHz = 0, db2GHz = 0;
	struct ar2316State *priv = AR2316(ah);
	int regWrites = 0;

	HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: chan %u/0x%x modesIndex %u\n",
	    __func__, chan->ic_freq, chan->ic_flags, modesIndex);

	HALASSERT(priv != AH_NULL);

	/* Setup rf parameters */
	if (IEEE80211_IS_CHAN_B(chan)) {
		ob2GHz = ee->ee_obFor24;
		db2GHz = ee->ee_dbFor24;
	} else {
		ob2GHz = ee->ee_obFor24g;
		db2GHz = ee->ee_dbFor24g;
	}

	/* Bank 1 Write */
	RF_BANK_SETUP(priv, 1, 1);

	/* Bank 2 Write */
	RF_BANK_SETUP(priv, 2, modesIndex);

	/* Bank 3 Write */
	RF_BANK_SETUP(priv, 3, modesIndex);

	/* Bank 6 Write */
	RF_BANK_SETUP(priv, 6, modesIndex);

	ar5212ModifyRfBuffer(priv->Bank6Data, ob2GHz,   3, 178, 0);
	ar5212ModifyRfBuffer(priv->Bank6Data, db2GHz,   3, 175, 0);

	/* Bank 7 Setup */
	RF_BANK_SETUP(priv, 7, modesIndex);

	/* Write Analog registers */
	HAL_INI_WRITE_BANK(ah, ar5212Bank1_2316, priv->Bank1Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank2_2316, priv->Bank2Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank3_2316, priv->Bank3Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank6_2316, priv->Bank6Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank7_2316, priv->Bank7Data, regWrites);

	/* Now that we have reprogrammed rfgain value, clear the flag. */
	ahp->ah_rfgainState = HAL_RFGAIN_INACTIVE;

	return AH_TRUE;
#undef	RF_BANK_SETUP
}

/*
 * Return a reference to the requested RF Bank.
 */
static uint32_t *
ar2316GetRfBank(struct ath_hal *ah, int bank)
{
	struct ar2316State *priv = AR2316(ah);

	HALASSERT(priv != AH_NULL);
	switch (bank) {
	case 1: return priv->Bank1Data;
	case 2: return priv->Bank2Data;
	case 3: return priv->Bank3Data;
	case 6: return priv->Bank6Data;
	case 7: return priv->Bank7Data;
	}
	HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown RF Bank %d requested\n",
	    __func__, bank);
	return AH_NULL;
}

/*
 * Return indices surrounding the value in sorted integer lists.
 *
 * NB: the input list is assumed to be sorted in ascending order
 */
static void
GetLowerUpperIndex(int16_t v, const uint16_t *lp, uint16_t listSize,
                          uint32_t *vlo, uint32_t *vhi)
{
	int16_t target = v;
	const int16_t *ep = lp+listSize;
	const int16_t *tp;

	/*
	 * Check first and last elements for out-of-bounds conditions.
	 */
	if (target < lp[0]) {
		*vlo = *vhi = 0;
		return;
	}
	if (target >= ep[-1]) {
		*vlo = *vhi = listSize - 1;
		return;
	}

	/* look for value being near or between 2 values in list */
	for (tp = lp; tp < ep; tp++) {
		/*
		 * If value is close to the current value of the list
		 * then target is not between values, it is one of the values
		 */
		if (*tp == target) {
			*vlo = *vhi = tp - (const int16_t *) lp;
			return;
		}
		/*
		 * Look for value being between current value and next value
		 * if so return these 2 values
		 */
		if (target < tp[1]) {
			*vlo = tp - (const int16_t *) lp;
			*vhi = *vlo + 1;
			return;
		}
	}
}

/*
 * Fill the Vpdlist for indices Pmax-Pmin
 */
static HAL_BOOL
ar2316FillVpdTable(uint32_t pdGainIdx, int16_t Pmin, int16_t  Pmax,
		   const int16_t *pwrList, const int16_t *VpdList,
		   uint16_t numIntercepts, uint16_t retVpdList[][64])
{
	uint16_t ii, jj, kk;
	int16_t currPwr = (int16_t)(2*Pmin);
	/* since Pmin is pwr*2 and pwrList is 4*pwr */
	uint32_t  idxL, idxR;

	ii = 0;
	jj = 0;

	if (numIntercepts < 2)
		return AH_FALSE;

	while (ii <= (uint16_t)(Pmax - Pmin)) {
		GetLowerUpperIndex(currPwr, pwrList, numIntercepts, 
					 &(idxL), &(idxR));
		if (idxR < 1)
			idxR = 1;			/* extrapolate below */
		if (idxL == (uint32_t)(numIntercepts - 1))
			idxL = numIntercepts - 2;	/* extrapolate above */
		if (pwrList[idxL] == pwrList[idxR])
			kk = VpdList[idxL];
		else
			kk = (uint16_t)
				(((currPwr - pwrList[idxL])*VpdList[idxR]+ 
				  (pwrList[idxR] - currPwr)*VpdList[idxL])/
				 (pwrList[idxR] - pwrList[idxL]));
		retVpdList[pdGainIdx][ii] = kk;
		ii++;
		currPwr += 2;				/* half dB steps */
	}

	return AH_TRUE;
}

/*
 * Returns interpolated or the scaled up interpolated value
 */
static int16_t
interpolate_signed(uint16_t target, uint16_t srcLeft, uint16_t srcRight,
	int16_t targetLeft, int16_t targetRight)
{
	int16_t rv;

	if (srcRight != srcLeft) {
		rv = ((target - srcLeft)*targetRight +
		      (srcRight - target)*targetLeft) / (srcRight - srcLeft);
	} else {
		rv = targetLeft;
	}
	return rv;
}

/*
 * Uses the data points read from EEPROM to reconstruct the pdadc power table
 * Called by ar2316SetPowerTable()
 */
static int 
ar2316getGainBoundariesAndPdadcsForPowers(struct ath_hal *ah, uint16_t channel,
		const RAW_DATA_STRUCT_2316 *pRawDataset,
		uint16_t pdGainOverlap_t2, 
		int16_t  *pMinCalPower, uint16_t pPdGainBoundaries[], 
		uint16_t pPdGainValues[], uint16_t pPDADCValues[]) 
{
	struct ar2316State *priv = AR2316(ah);
#define	VpdTable_L	priv->vpdTable_L
#define	VpdTable_R	priv->vpdTable_R
#define	VpdTable_I	priv->vpdTable_I
	uint32_t ii, jj, kk;
	int32_t ss;/* potentially -ve index for taking care of pdGainOverlap */
	uint32_t idxL, idxR;
	uint32_t numPdGainsUsed = 0;
	/* 
	 * If desired to support -ve power levels in future, just
	 * change pwr_I_0 to signed 5-bits.
	 */
	int16_t Pmin_t2[MAX_NUM_PDGAINS_PER_CHANNEL];
	/* to accommodate -ve power levels later on. */
	int16_t Pmax_t2[MAX_NUM_PDGAINS_PER_CHANNEL];
	/* to accommodate -ve power levels later on */
	uint16_t numVpd = 0;
	uint16_t Vpd_step;
	int16_t tmpVal ; 
	uint32_t sizeCurrVpdTable, maxIndex, tgtIndex;

	/* Get upper lower index */
	GetLowerUpperIndex(channel, pRawDataset->pChannels,
				 pRawDataset->numChannels, &(idxL), &(idxR));

	for (ii = 0; ii < MAX_NUM_PDGAINS_PER_CHANNEL; ii++) {
		jj = MAX_NUM_PDGAINS_PER_CHANNEL - ii - 1;
		/* work backwards 'cause highest pdGain for lowest power */
		numVpd = pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].numVpd;
		if (numVpd > 0) {
			pPdGainValues[numPdGainsUsed] = pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].pd_gain;
			Pmin_t2[numPdGainsUsed] = pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].pwr_t4[0];
			if (Pmin_t2[numPdGainsUsed] >pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].pwr_t4[0]) {
				Pmin_t2[numPdGainsUsed] = pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].pwr_t4[0];
			}
			Pmin_t2[numPdGainsUsed] = (int16_t)
				(Pmin_t2[numPdGainsUsed] / 2);
			Pmax_t2[numPdGainsUsed] = pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].pwr_t4[numVpd-1];
			if (Pmax_t2[numPdGainsUsed] > pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].pwr_t4[numVpd-1])
				Pmax_t2[numPdGainsUsed] = 
					pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].pwr_t4[numVpd-1];
			Pmax_t2[numPdGainsUsed] = (int16_t)(Pmax_t2[numPdGainsUsed] / 2);
			ar2316FillVpdTable(
					   numPdGainsUsed, Pmin_t2[numPdGainsUsed], Pmax_t2[numPdGainsUsed], 
					   &(pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].pwr_t4[0]), 
					   &(pRawDataset->pDataPerChannel[idxL].pDataPerPDGain[jj].Vpd[0]), numVpd, VpdTable_L
					   );
			ar2316FillVpdTable(
					   numPdGainsUsed, Pmin_t2[numPdGainsUsed], Pmax_t2[numPdGainsUsed], 
					   &(pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].pwr_t4[0]),
					   &(pRawDataset->pDataPerChannel[idxR].pDataPerPDGain[jj].Vpd[0]), numVpd, VpdTable_R
					   );
			for (kk = 0; kk < (uint16_t)(Pmax_t2[numPdGainsUsed] - Pmin_t2[numPdGainsUsed]); kk++) {
				VpdTable_I[numPdGainsUsed][kk] = 
					interpolate_signed(
							   channel, pRawDataset->pChannels[idxL], pRawDataset->pChannels[idxR],
							   (int16_t)VpdTable_L[numPdGainsUsed][kk], (int16_t)VpdTable_R[numPdGainsUsed][kk]);
			}
			/* fill VpdTable_I for this pdGain */
			numPdGainsUsed++;
		}
		/* if this pdGain is used */
	}

	*pMinCalPower = Pmin_t2[0];
	kk = 0; /* index for the final table */
	for (ii = 0; ii < numPdGainsUsed; ii++) {
		if (ii == (numPdGainsUsed - 1))
			pPdGainBoundaries[ii] = Pmax_t2[ii] +
				PD_GAIN_BOUNDARY_STRETCH_IN_HALF_DB;
		else 
			pPdGainBoundaries[ii] = (uint16_t)
				((Pmax_t2[ii] + Pmin_t2[ii+1]) / 2 );
		if (pPdGainBoundaries[ii] > 63) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: clamp pPdGainBoundaries[%d] %d\n",
			    __func__, ii, pPdGainBoundaries[ii]);/*XXX*/
			pPdGainBoundaries[ii] = 63;
		}

		/* Find starting index for this pdGain */
		if (ii == 0) 
			ss = 0; /* for the first pdGain, start from index 0 */
		else 
			ss = (pPdGainBoundaries[ii-1] - Pmin_t2[ii]) - 
				pdGainOverlap_t2;
		Vpd_step = (uint16_t)(VpdTable_I[ii][1] - VpdTable_I[ii][0]);
		Vpd_step = (uint16_t)((Vpd_step < 1) ? 1 : Vpd_step);
		/*
		 *-ve ss indicates need to extrapolate data below for this pdGain
		 */
		while (ss < 0) {
			tmpVal = (int16_t)(VpdTable_I[ii][0] + ss*Vpd_step);
			pPDADCValues[kk++] = (uint16_t)((tmpVal < 0) ? 0 : tmpVal);
			ss++;
		}

		sizeCurrVpdTable = Pmax_t2[ii] - Pmin_t2[ii];
		tgtIndex = pPdGainBoundaries[ii] + pdGainOverlap_t2 - Pmin_t2[ii];
		maxIndex = (tgtIndex < sizeCurrVpdTable) ? tgtIndex : sizeCurrVpdTable;

		while (ss < (int16_t)maxIndex)
			pPDADCValues[kk++] = VpdTable_I[ii][ss++];

		Vpd_step = (uint16_t)(VpdTable_I[ii][sizeCurrVpdTable-1] -
				       VpdTable_I[ii][sizeCurrVpdTable-2]);
		Vpd_step = (uint16_t)((Vpd_step < 1) ? 1 : Vpd_step);           
		/*
		 * for last gain, pdGainBoundary == Pmax_t2, so will 
		 * have to extrapolate
		 */
		if (tgtIndex > maxIndex) {	/* need to extrapolate above */
			while(ss < (int16_t)tgtIndex) {
				tmpVal = (uint16_t)
					(VpdTable_I[ii][sizeCurrVpdTable-1] + 
					 (ss-maxIndex)*Vpd_step);
				pPDADCValues[kk++] = (tmpVal > 127) ? 
					127 : tmpVal;
				ss++;
			}
		}				/* extrapolated above */
	}					/* for all pdGainUsed */

	while (ii < MAX_NUM_PDGAINS_PER_CHANNEL) {
		pPdGainBoundaries[ii] = pPdGainBoundaries[ii-1];
		ii++;
	}
	while (kk < 128) {
		pPDADCValues[kk] = pPDADCValues[kk-1];
		kk++;
	}

	return numPdGainsUsed;
#undef VpdTable_L
#undef VpdTable_R
#undef VpdTable_I
}

static HAL_BOOL
ar2316SetPowerTable(struct ath_hal *ah,
	int16_t *minPower, int16_t *maxPower,
	const struct ieee80211_channel *chan, 
	uint16_t *rfXpdGain)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	const RAW_DATA_STRUCT_2316 *pRawDataset = AH_NULL;
	uint16_t pdGainOverlap_t2;
	int16_t minCalPower2316_t2;
	uint16_t *pdadcValues = ahp->ah_pcdacTable;
	uint16_t gainBoundaries[4];
	uint32_t reg32, regoffset;
	int i, numPdGainsUsed;
#ifndef AH_USE_INIPDGAIN
	uint32_t tpcrg1;
#endif

	HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: chan 0x%x flag 0x%x\n",
	    __func__, chan->ic_freq, chan->ic_flags);

	if (IEEE80211_IS_CHAN_G(chan) || IEEE80211_IS_CHAN_108G(chan))
		pRawDataset = &ee->ee_rawDataset2413[headerInfo11G];
	else if (IEEE80211_IS_CHAN_B(chan))
		pRawDataset = &ee->ee_rawDataset2413[headerInfo11B];
	else {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: illegal mode\n", __func__);
		return AH_FALSE;
	}

	pdGainOverlap_t2 = (uint16_t) SM(OS_REG_READ(ah, AR_PHY_TPCRG5),
					  AR_PHY_TPCRG5_PD_GAIN_OVERLAP);
    
	numPdGainsUsed = ar2316getGainBoundariesAndPdadcsForPowers(ah,
		chan->channel, pRawDataset, pdGainOverlap_t2,
		&minCalPower2316_t2,gainBoundaries, rfXpdGain, pdadcValues);
	HALASSERT(1 <= numPdGainsUsed && numPdGainsUsed <= 3);

#ifdef AH_USE_INIPDGAIN
	/*
	 * Use pd_gains curve from eeprom; Atheros always uses
	 * the default curve from the ini file but some vendors
	 * (e.g. Zcomax) want to override this curve and not
	 * honoring their settings results in tx power 5dBm low.
	 */
	OS_REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN, 
			 (pRawDataset->pDataPerChannel[0].numPdGains - 1));
#else
	tpcrg1 = OS_REG_READ(ah, AR_PHY_TPCRG1);
	tpcrg1 = (tpcrg1 &~ AR_PHY_TPCRG1_NUM_PD_GAIN)
		  | SM(numPdGainsUsed-1, AR_PHY_TPCRG1_NUM_PD_GAIN);
	switch (numPdGainsUsed) {
	case 3:
		tpcrg1 &= ~AR_PHY_TPCRG1_PDGAIN_SETTING3;
		tpcrg1 |= SM(rfXpdGain[2], AR_PHY_TPCRG1_PDGAIN_SETTING3);
		/* fall thru... */
	case 2:
		tpcrg1 &= ~AR_PHY_TPCRG1_PDGAIN_SETTING2;
		tpcrg1 |= SM(rfXpdGain[1], AR_PHY_TPCRG1_PDGAIN_SETTING2);
		/* fall thru... */
	case 1:
		tpcrg1 &= ~AR_PHY_TPCRG1_PDGAIN_SETTING1;
		tpcrg1 |= SM(rfXpdGain[0], AR_PHY_TPCRG1_PDGAIN_SETTING1);
		break;
	}
#ifdef AH_DEBUG
	if (tpcrg1 != OS_REG_READ(ah, AR_PHY_TPCRG1))
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: using non-default "
		    "pd_gains (default 0x%x, calculated 0x%x)\n",
		    __func__, OS_REG_READ(ah, AR_PHY_TPCRG1), tpcrg1);
#endif
	OS_REG_WRITE(ah, AR_PHY_TPCRG1, tpcrg1);
#endif

	/*
	 * Note the pdadc table may not start at 0 dBm power, could be
	 * negative or greater than 0.  Need to offset the power
	 * values by the amount of minPower for griffin
	 */
	if (minCalPower2316_t2 != 0)
		ahp->ah_txPowerIndexOffset = (int16_t)(0 - minCalPower2316_t2);
	else
		ahp->ah_txPowerIndexOffset = 0;

	/* Finally, write the power values into the baseband power table */
	regoffset = 0x9800 + (672 <<2); /* beginning of pdadc table in griffin */
	for (i = 0; i < 32; i++) {
		reg32 = ((pdadcValues[4*i + 0] & 0xFF) << 0)  | 
			((pdadcValues[4*i + 1] & 0xFF) << 8)  |
			((pdadcValues[4*i + 2] & 0xFF) << 16) |
			((pdadcValues[4*i + 3] & 0xFF) << 24) ;        
		OS_REG_WRITE(ah, regoffset, reg32);
		regoffset += 4;
	}

	OS_REG_WRITE(ah, AR_PHY_TPCRG5, 
		     SM(pdGainOverlap_t2, AR_PHY_TPCRG5_PD_GAIN_OVERLAP) | 
		     SM(gainBoundaries[0], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1) |
		     SM(gainBoundaries[1], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2) |
		     SM(gainBoundaries[2], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3) |
		     SM(gainBoundaries[3], AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));

	return AH_TRUE;
}

static int16_t
ar2316GetMinPower(struct ath_hal *ah, const RAW_DATA_PER_CHANNEL_2316 *data)
{
	uint32_t ii,jj;
	uint16_t Pmin=0,numVpd;

	for (ii = 0; ii < MAX_NUM_PDGAINS_PER_CHANNEL; ii++) {
		jj = MAX_NUM_PDGAINS_PER_CHANNEL - ii - 1;
		/* work backwards 'cause highest pdGain for lowest power */
		numVpd = data->pDataPerPDGain[jj].numVpd;
		if (numVpd > 0) {
			Pmin = data->pDataPerPDGain[jj].pwr_t4[0];
			return(Pmin);
		}
	}
	return(Pmin);
}

static int16_t
ar2316GetMaxPower(struct ath_hal *ah, const RAW_DATA_PER_CHANNEL_2316 *data)
{
	uint32_t ii;
	uint16_t Pmax=0,numVpd;
	
	for (ii=0; ii< MAX_NUM_PDGAINS_PER_CHANNEL; ii++) {
		/* work forwards cuase lowest pdGain for highest power */
		numVpd = data->pDataPerPDGain[ii].numVpd;
		if (numVpd > 0) {
			Pmax = data->pDataPerPDGain[ii].pwr_t4[numVpd-1];
			return(Pmax);
		}
	}
	return(Pmax);
}

static HAL_BOOL
ar2316GetChannelMaxMinPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan,
	int16_t *maxPow, int16_t *minPow)
{
	uint16_t freq = chan->ic_freq;		/* NB: never mapped */
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	const RAW_DATA_STRUCT_2316 *pRawDataset = AH_NULL;
	const RAW_DATA_PER_CHANNEL_2316 *data=AH_NULL;
	uint16_t numChannels;
	int totalD,totalF, totalMin,last, i;

	*maxPow = 0;

	if (IEEE80211_IS_CHAN_G(chan) || IEEE80211_IS_CHAN_108G(chan))
		pRawDataset = &ee->ee_rawDataset2413[headerInfo11G];
	else if (IEEE80211_IS_CHAN_B(chan))
		pRawDataset = &ee->ee_rawDataset2413[headerInfo11B];
	else
		return(AH_FALSE);

	numChannels = pRawDataset->numChannels;
	data = pRawDataset->pDataPerChannel;
	
	/* Make sure the channel is in the range of the TP values 
	 *  (freq piers)
	 */
	if (numChannels < 1)
		return(AH_FALSE);

	if ((freq < data[0].channelValue) ||
	    (freq > data[numChannels-1].channelValue)) {
		if (freq < data[0].channelValue) {
			*maxPow = ar2316GetMaxPower(ah, &data[0]);
			*minPow = ar2316GetMinPower(ah, &data[0]);
			return(AH_TRUE);
		} else {
			*maxPow = ar2316GetMaxPower(ah, &data[numChannels - 1]);
			*minPow = ar2316GetMinPower(ah, &data[numChannels - 1]);
			return(AH_TRUE);
		}
	}

	/* Linearly interpolate the power value now */
	for (last=0,i=0; (i<numChannels) && (freq > data[i].channelValue);
	     last = i++);
	totalD = data[i].channelValue - data[last].channelValue;
	if (totalD > 0) {
		totalF = ar2316GetMaxPower(ah, &data[i]) - ar2316GetMaxPower(ah, &data[last]);
		*maxPow = (int8_t) ((totalF*(freq-data[last].channelValue) + 
				     ar2316GetMaxPower(ah, &data[last])*totalD)/totalD);
		totalMin = ar2316GetMinPower(ah, &data[i]) - ar2316GetMinPower(ah, &data[last]);
		*minPow = (int8_t) ((totalMin*(freq-data[last].channelValue) +
				     ar2316GetMinPower(ah, &data[last])*totalD)/totalD);
		return(AH_TRUE);
	} else {
		if (freq == data[i].channelValue) {
			*maxPow = ar2316GetMaxPower(ah, &data[i]);
			*minPow = ar2316GetMinPower(ah, &data[i]);
			return(AH_TRUE);
		} else
			return(AH_FALSE);
	}
}

/*
 * Free memory for analog bank scratch buffers
 */
static void
ar2316RfDetach(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALASSERT(ahp->ah_rfHal != AH_NULL);
	ath_hal_free(ahp->ah_rfHal);
	ahp->ah_rfHal = AH_NULL;
}

/*
 * Allocate memory for private state.
 * Scratch Buffer will be reinitialized every reset so no need to zero now
 */
static HAL_BOOL
ar2316RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar2316State *priv;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	HALASSERT(ahp->ah_rfHal == AH_NULL);
	priv = ath_hal_malloc(sizeof(struct ar2316State));
	if (priv == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot allocate private state\n", __func__);
		*status = HAL_ENOMEM;		/* XXX */
		return AH_FALSE;
	}
	priv->base.rfDetach		= ar2316RfDetach;
	priv->base.writeRegs		= ar2316WriteRegs;
	priv->base.getRfBank		= ar2316GetRfBank;
	priv->base.setChannel		= ar2316SetChannel;
	priv->base.setRfRegs		= ar2316SetRfRegs;
	priv->base.setPowerTable	= ar2316SetPowerTable;
	priv->base.getChannelMaxMinPower = ar2316GetChannelMaxMinPower;
	priv->base.getNfAdjust		= ar5212GetNfAdjust;

	ahp->ah_pcdacTable = priv->pcdacTable;
	ahp->ah_pcdacTableSize = sizeof(priv->pcdacTable);
	ahp->ah_rfHal = &priv->base;

	ahp->ah_cwCalRequire = AH_TRUE;		/* force initial cal */

	return AH_TRUE;
}

static HAL_BOOL
ar2316Probe(struct ath_hal *ah)
{
	return IS_2316(ah);
}
AH_RF(RF2316, ar2316Probe, ar2316RfAttach);
