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

#include "ah_eeprom_v3.h"

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#define AH_5212_5112
#include "ar5212/ar5212.ini"

#define	N(a)	(sizeof(a)/sizeof(a[0]))

struct ar5112State {
	RF_HAL_FUNCS	base;		/* public state, must be first */
	uint16_t	pcdacTable[PWR_TABLE_SIZE];

	uint32_t	Bank1Data[N(ar5212Bank1_5112)];
	uint32_t	Bank2Data[N(ar5212Bank2_5112)];
	uint32_t	Bank3Data[N(ar5212Bank3_5112)];
	uint32_t	Bank6Data[N(ar5212Bank6_5112)];
	uint32_t	Bank7Data[N(ar5212Bank7_5112)];
};
#define	AR5112(ah)	((struct ar5112State *) AH5212(ah)->ah_rfHal)

static	void ar5212GetLowerUpperIndex(uint16_t v,
		uint16_t *lp, uint16_t listSize,
		uint32_t *vlo, uint32_t *vhi);
static HAL_BOOL getFullPwrTable(uint16_t numPcdacs, uint16_t *pcdacs,
		int16_t *power, int16_t maxPower, int16_t *retVals);
static int16_t getPminAndPcdacTableFromPowerTable(int16_t *pwrTableT4,
		uint16_t retVals[]);
static int16_t getPminAndPcdacTableFromTwoPowerTables(int16_t *pwrTableLXpdT4,
		int16_t *pwrTableHXpdT4, uint16_t retVals[], int16_t *pMid);
static int16_t interpolate_signed(uint16_t target,
		uint16_t srcLeft, uint16_t srcRight,
		int16_t targetLeft, int16_t targetRight);

extern	void ar5212ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32,
		uint32_t numBits, uint32_t firstBit, uint32_t column);

static void
ar5112WriteRegs(struct ath_hal *ah, u_int modesIndex, u_int freqIndex,
	int writes)
{
	HAL_INI_WRITE_ARRAY(ah, ar5212Modes_5112, modesIndex, writes);
	HAL_INI_WRITE_ARRAY(ah, ar5212Common_5112, 1, writes);
	HAL_INI_WRITE_ARRAY(ah, ar5212BB_RfGain_5112, freqIndex, writes);
}

/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 */
static HAL_BOOL
ar5112SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
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
	} else if (((freq % 5) == 2) && (freq <= 5435)) {
		freq = freq - 2; /* Align to even 5MHz raster */
		channelSel = ath_hal_reverseBits(
			(uint32_t)(((freq - 4800)*10)/25 + 1), 8);
            	aModeRefSel = ath_hal_reverseBits(0, 2);
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
 * Return a reference to the requested RF Bank.
 */
static uint32_t *
ar5112GetRfBank(struct ath_hal *ah, int bank)
{
	struct ar5112State *priv = AR5112(ah);

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
 * Reads EEPROM header info from device structure and programs
 * all rf registers
 *
 * REQUIRES: Access to the analog rf device
 */
static HAL_BOOL
ar5112SetRfRegs(struct ath_hal *ah,
	const struct ieee80211_channel *chan,
	uint16_t modesIndex, uint16_t *rfXpdGain)
{
#define	RF_BANK_SETUP(_priv, _ix, _col) do {				    \
	int i;								    \
	for (i = 0; i < N(ar5212Bank##_ix##_5112); i++)			    \
		(_priv)->Bank##_ix##Data[i] = ar5212Bank##_ix##_5112[i][_col];\
} while (0)
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint16_t rfXpdSel, gainI;
	uint16_t ob5GHz = 0, db5GHz = 0;
	uint16_t ob2GHz = 0, db2GHz = 0;
	struct ar5112State *priv = AR5112(ah);
	GAIN_VALUES *gv = &ahp->ah_gainValues;
	int regWrites = 0;

	HALASSERT(priv);

	HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: chan %u/0x%x modesIndex %u\n",
	    __func__, chan->ic_freq, chan->ic_flags, modesIndex);

	/* Setup rf parameters */
	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		if (freq > 4000 && freq < 5260) {
			ob5GHz = ee->ee_ob1;
			db5GHz = ee->ee_db1;
		} else if (freq >= 5260 && freq < 5500) {
			ob5GHz = ee->ee_ob2;
			db5GHz = ee->ee_db2;
		} else if (freq >= 5500 && freq < 5725) {
			ob5GHz = ee->ee_ob3;
			db5GHz = ee->ee_db3;
		} else if (freq >= 5725) {
			ob5GHz = ee->ee_ob4;
			db5GHz = ee->ee_db4;
		} else {
			/* XXX else */
		}
		rfXpdSel = ee->ee_xpd[headerInfo11A];
		gainI = ee->ee_gainI[headerInfo11A];
		break;
	case IEEE80211_CHAN_B:
		ob2GHz = ee->ee_ob2GHz[0];
		db2GHz = ee->ee_db2GHz[0];
		rfXpdSel = ee->ee_xpd[headerInfo11B];
		gainI = ee->ee_gainI[headerInfo11B];
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_PUREG:	/* NB: really 108G */
		ob2GHz = ee->ee_ob2GHz[1];
		db2GHz = ee->ee_ob2GHz[1];
		rfXpdSel = ee->ee_xpd[headerInfo11G];
		gainI = ee->ee_gainI[headerInfo11G];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	/* Setup Bank 1 Write */
	RF_BANK_SETUP(priv, 1, 1);

	/* Setup Bank 2 Write */
	RF_BANK_SETUP(priv, 2, modesIndex);

	/* Setup Bank 3 Write */
	RF_BANK_SETUP(priv, 3, modesIndex);

	/* Setup Bank 6 Write */
	RF_BANK_SETUP(priv, 6, modesIndex);

	ar5212ModifyRfBuffer(priv->Bank6Data, rfXpdSel,     1, 302, 0);

	ar5212ModifyRfBuffer(priv->Bank6Data, rfXpdGain[0], 2, 270, 0);
	ar5212ModifyRfBuffer(priv->Bank6Data, rfXpdGain[1], 2, 257, 0);

	if (IEEE80211_IS_CHAN_OFDM(chan)) {
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_138], 1, 168, 3);
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_137], 1, 169, 3);
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_136], 1, 170, 3);
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_132], 1, 174, 3);
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_131], 1, 175, 3);
		ar5212ModifyRfBuffer(priv->Bank6Data,
			gv->currStep->paramVal[GP_PWD_130], 1, 176, 3);
	}

	/* Only the 5 or 2 GHz OB/DB need to be set for a mode */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		ar5212ModifyRfBuffer(priv->Bank6Data, ob2GHz, 3, 287, 0);
		ar5212ModifyRfBuffer(priv->Bank6Data, db2GHz, 3, 290, 0);
	} else {
		ar5212ModifyRfBuffer(priv->Bank6Data, ob5GHz, 3, 279, 0);
		ar5212ModifyRfBuffer(priv->Bank6Data, db5GHz, 3, 282, 0);
	}
	
	/* Lower synth voltage for X112 Rev 2.0 only */
	if (IS_RADX112_REV2(ah)) {
		/* Non-Reversed analyg registers - so values are pre-reversed */
		ar5212ModifyRfBuffer(priv->Bank6Data, 2, 2, 90, 2);
		ar5212ModifyRfBuffer(priv->Bank6Data, 2, 2, 92, 2);
		ar5212ModifyRfBuffer(priv->Bank6Data, 2, 2, 94, 2);
		ar5212ModifyRfBuffer(priv->Bank6Data, 2, 1, 254, 2);
	}

    /* Decrease Power Consumption for 5312/5213 and up */
    if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_2) {
        ar5212ModifyRfBuffer(priv->Bank6Data, 1, 1, 281, 1);
        ar5212ModifyRfBuffer(priv->Bank6Data, 1, 2, 1, 3);
        ar5212ModifyRfBuffer(priv->Bank6Data, 1, 2, 3, 3);
        ar5212ModifyRfBuffer(priv->Bank6Data, 1, 1, 139, 3);
        ar5212ModifyRfBuffer(priv->Bank6Data, 1, 1, 140, 3);
    }

	/* Setup Bank 7 Setup */
	RF_BANK_SETUP(priv, 7, modesIndex);
	if (IEEE80211_IS_CHAN_OFDM(chan))
		ar5212ModifyRfBuffer(priv->Bank7Data,
			gv->currStep->paramVal[GP_MIXGAIN_OVR], 2, 37, 0);

	ar5212ModifyRfBuffer(priv->Bank7Data, gainI, 6, 14, 0);

	/* Adjust params for Derby TX power control */
	if (IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan)) {
        	uint32_t	rfDelay, rfPeriod;

        	rfDelay = 0xf;
        	rfPeriod = (IEEE80211_IS_CHAN_HALF(chan)) ?  0x8 : 0xf;
        	ar5212ModifyRfBuffer(priv->Bank7Data, rfDelay, 4, 58, 0);
        	ar5212ModifyRfBuffer(priv->Bank7Data, rfPeriod, 4, 70, 0);
	}

#ifdef notyet
	/* Analog registers are setup - EAR can modify */
	if (ar5212IsEarEngaged(pDev, chan))
		uint32_t modifier;
		ar5212EarModify(pDev, EAR_LC_RF_WRITE, chan, &modifier);
#endif
	/* Write Analog registers */
	HAL_INI_WRITE_BANK(ah, ar5212Bank1_5112, priv->Bank1Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank2_5112, priv->Bank2Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank3_5112, priv->Bank3Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank6_5112, priv->Bank6Data, regWrites);
	HAL_INI_WRITE_BANK(ah, ar5212Bank7_5112, priv->Bank7Data, regWrites);

	/* Now that we have reprogrammed rfgain value, clear the flag. */
	ahp->ah_rfgainState = HAL_RFGAIN_INACTIVE;
	return AH_TRUE;
#undef	RF_BANK_SETUP
}

/*
 * Read the transmit power levels from the structures taken from EEPROM
 * Interpolate read transmit power values for this channel
 * Organize the transmit power values into a table for writing into the hardware
 */
static HAL_BOOL
ar5112SetPowerTable(struct ath_hal *ah,
	int16_t *pPowerMin, int16_t *pPowerMax,
	const struct ieee80211_channel *chan,
	uint16_t *rfXpdGain)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint32_t numXpdGain = IS_RADX112_REV2(ah) ? 2 : 1;
	uint32_t    xpdGainMask = 0;
	int16_t     powerMid, *pPowerMid = &powerMid;

	const EXPN_DATA_PER_CHANNEL_5112 *pRawCh;
	const EEPROM_POWER_EXPN_5112     *pPowerExpn = AH_NULL;

	uint32_t    ii, jj, kk;
	int16_t     minPwr_t4, maxPwr_t4, Pmin, Pmid;

	uint32_t    chan_idx_L = 0, chan_idx_R = 0;
	uint16_t    chan_L, chan_R;

	int16_t     pwr_table0[64];
	int16_t     pwr_table1[64];
	uint16_t    pcdacs[10];
	int16_t     powers[10];
	uint16_t    numPcd;
	int16_t     powTableLXPD[2][64];
	int16_t     powTableHXPD[2][64];
	int16_t     tmpPowerTable[64];
	uint16_t    xgainList[2];
	uint16_t    xpdMask;

	switch (chan->ic_flags & IEEE80211_CHAN_ALLTURBOFULL) {
	case IEEE80211_CHAN_A:
	case IEEE80211_CHAN_ST:
		pPowerExpn = &ee->ee_modePowerArray5112[headerInfo11A];
		xpdGainMask = ee->ee_xgain[headerInfo11A];
		break;
	case IEEE80211_CHAN_B:
		pPowerExpn = &ee->ee_modePowerArray5112[headerInfo11B];
		xpdGainMask = ee->ee_xgain[headerInfo11B];
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_108G:
		pPowerExpn = &ee->ee_modePowerArray5112[headerInfo11G];
		xpdGainMask = ee->ee_xgain[headerInfo11G];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	if ((xpdGainMask & pPowerExpn->xpdMask) < 1) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: desired xpdGainMask 0x%x not supported by "
		    "calibrated xpdMask 0x%x\n", __func__,
		    xpdGainMask, pPowerExpn->xpdMask);
		return AH_FALSE;
	}

	maxPwr_t4 = (int16_t)(2*(*pPowerMax));	/* pwr_t2 -> pwr_t4 */
	minPwr_t4 = (int16_t)(2*(*pPowerMin));	/* pwr_t2 -> pwr_t4 */

	xgainList[0] = 0xDEAD;
	xgainList[1] = 0xDEAD;

	kk = 0;
	xpdMask = pPowerExpn->xpdMask;
	for (jj = 0; jj < NUM_XPD_PER_CHANNEL; jj++) {
		if (((xpdMask >> jj) & 1) > 0) {
			if (kk > 1) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "A maximum of 2 xpdGains supported"
				    "in pExpnPower data\n");
				return AH_FALSE;
			}
			xgainList[kk++] = (uint16_t)jj;
		}
	}

	ar5212GetLowerUpperIndex(freq, &pPowerExpn->pChannels[0],
		pPowerExpn->numChannels, &chan_idx_L, &chan_idx_R);

	kk = 0;
	for (ii = chan_idx_L; ii <= chan_idx_R; ii++) {
		pRawCh = &(pPowerExpn->pDataPerChannel[ii]);
		if (xgainList[1] == 0xDEAD) {
			jj = xgainList[0];
			numPcd = pRawCh->pDataPerXPD[jj].numPcdacs;
			OS_MEMCPY(&pcdacs[0], &pRawCh->pDataPerXPD[jj].pcdac[0],
				numPcd * sizeof(uint16_t));
			OS_MEMCPY(&powers[0], &pRawCh->pDataPerXPD[jj].pwr_t4[0],
				numPcd * sizeof(int16_t));
			if (!getFullPwrTable(numPcd, &pcdacs[0], &powers[0],
				pRawCh->maxPower_t4, &tmpPowerTable[0])) {
				return AH_FALSE;
			}
			OS_MEMCPY(&powTableLXPD[kk][0], &tmpPowerTable[0],
				64*sizeof(int16_t));
		} else {
			jj = xgainList[0];
			numPcd = pRawCh->pDataPerXPD[jj].numPcdacs;
			OS_MEMCPY(&pcdacs[0], &pRawCh->pDataPerXPD[jj].pcdac[0],
				numPcd*sizeof(uint16_t));
			OS_MEMCPY(&powers[0],
				&pRawCh->pDataPerXPD[jj].pwr_t4[0],
				numPcd*sizeof(int16_t));
			if (!getFullPwrTable(numPcd, &pcdacs[0], &powers[0],
				pRawCh->maxPower_t4, &tmpPowerTable[0])) {
				return AH_FALSE;
			}
			OS_MEMCPY(&powTableLXPD[kk][0], &tmpPowerTable[0],
				64 * sizeof(int16_t));

			jj = xgainList[1];
			numPcd = pRawCh->pDataPerXPD[jj].numPcdacs;
			OS_MEMCPY(&pcdacs[0], &pRawCh->pDataPerXPD[jj].pcdac[0],
				numPcd * sizeof(uint16_t));
			OS_MEMCPY(&powers[0],
				&pRawCh->pDataPerXPD[jj].pwr_t4[0],
				numPcd * sizeof(int16_t));
			if (!getFullPwrTable(numPcd, &pcdacs[0], &powers[0],
				pRawCh->maxPower_t4, &tmpPowerTable[0])) {
				return AH_FALSE;
			}
			OS_MEMCPY(&powTableHXPD[kk][0], &tmpPowerTable[0],
				64 * sizeof(int16_t));
		}
		kk++;
	}

	chan_L = pPowerExpn->pChannels[chan_idx_L];
	chan_R = pPowerExpn->pChannels[chan_idx_R];
	kk = chan_idx_R - chan_idx_L;

	if (xgainList[1] == 0xDEAD) {
		for (jj = 0; jj < 64; jj++) {
			pwr_table0[jj] = interpolate_signed(
				freq, chan_L, chan_R,
				powTableLXPD[0][jj], powTableLXPD[kk][jj]);
		}
		Pmin = getPminAndPcdacTableFromPowerTable(&pwr_table0[0],
				ahp->ah_pcdacTable);
		*pPowerMin = (int16_t) (Pmin / 2);
		*pPowerMid = (int16_t) (pwr_table0[63] / 2);
		*pPowerMax = (int16_t) (pwr_table0[63] / 2);
		rfXpdGain[0] = xgainList[0];
		rfXpdGain[1] = rfXpdGain[0];
	} else {
		for (jj = 0; jj < 64; jj++) {
			pwr_table0[jj] = interpolate_signed(
				freq, chan_L, chan_R,
				powTableLXPD[0][jj], powTableLXPD[kk][jj]);
			pwr_table1[jj] = interpolate_signed(
				freq, chan_L, chan_R,
				powTableHXPD[0][jj], powTableHXPD[kk][jj]);
		}
		if (numXpdGain == 2) {
			Pmin = getPminAndPcdacTableFromTwoPowerTables(
				&pwr_table0[0], &pwr_table1[0],
				ahp->ah_pcdacTable, &Pmid);
			*pPowerMin = (int16_t) (Pmin / 2);
			*pPowerMid = (int16_t) (Pmid / 2);
			*pPowerMax = (int16_t) (pwr_table0[63] / 2);
			rfXpdGain[0] = xgainList[0];
			rfXpdGain[1] = xgainList[1];
		} else if (minPwr_t4 <= pwr_table1[63] &&
			   maxPwr_t4 <= pwr_table1[63]) {
			Pmin = getPminAndPcdacTableFromPowerTable(
				&pwr_table1[0], ahp->ah_pcdacTable);
			rfXpdGain[0] = xgainList[1];
			rfXpdGain[1] = rfXpdGain[0];
			*pPowerMin = (int16_t) (Pmin / 2);
			*pPowerMid = (int16_t) (pwr_table1[63] / 2);
			*pPowerMax = (int16_t) (pwr_table1[63] / 2);
		} else {
			Pmin = getPminAndPcdacTableFromPowerTable(
				&pwr_table0[0], ahp->ah_pcdacTable);
			rfXpdGain[0] = xgainList[0];
			rfXpdGain[1] = rfXpdGain[0];
			*pPowerMin = (int16_t) (Pmin/2);
			*pPowerMid = (int16_t) (pwr_table0[63] / 2);
			*pPowerMax = (int16_t) (pwr_table0[63] / 2);
		}
	}

	/*
	 * Move 5112 rates to match power tables where the max
	 * power table entry corresponds with maxPower.
	 */
	HALASSERT(*pPowerMax <= PCDAC_STOP);
	ahp->ah_txPowerIndexOffset = PCDAC_STOP - *pPowerMax;

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
 * Return indices surrounding the value in sorted integer lists.
 *
 * NB: the input list is assumed to be sorted in ascending order
 */
static void
ar5212GetLowerUpperIndex(uint16_t v, uint16_t *lp, uint16_t listSize,
                          uint32_t *vlo, uint32_t *vhi)
{
	uint32_t target = v;
	uint16_t *ep = lp+listSize;
	uint16_t *tp;

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
			*vlo = *vhi = tp - lp;
			return;
		}
		/*
		 * Look for value being between current value and next value
		 * if so return these 2 values
		 */
		if (target < tp[1]) {
			*vlo = tp - lp;
			*vhi = *vlo + 1;
			return;
		}
	}
}

static HAL_BOOL
getFullPwrTable(uint16_t numPcdacs, uint16_t *pcdacs, int16_t *power, int16_t maxPower, int16_t *retVals)
{
	uint16_t    ii;
	uint16_t    idxL = 0;
	uint16_t    idxR = 1;

	if (numPcdacs < 2) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		     "%s: at least 2 pcdac values needed [%d]\n",
		     __func__, numPcdacs);
		return AH_FALSE;
	}
	for (ii = 0; ii < 64; ii++) {
		if (ii>pcdacs[idxR] && idxR < numPcdacs-1) {
			idxL++;
			idxR++;
		}
		retVals[ii] = interpolate_signed(ii,
			pcdacs[idxL], pcdacs[idxR], power[idxL], power[idxR]);
		if (retVals[ii] >= maxPower) {
			while (ii < 64)
				retVals[ii++] = maxPower;
		}
	}
	return AH_TRUE;
}

/*
 * Takes a single calibration curve and creates a power table.
 * Adjusts the new power table so the max power is relative
 * to the maximum index in the power table.
 *
 * WARNING: rates must be adjusted for this relative power table
 */
static int16_t
getPminAndPcdacTableFromPowerTable(int16_t *pwrTableT4, uint16_t retVals[])
{
    int16_t ii, jj, jjMax;
    int16_t pMin, currPower, pMax;

    /* If the spread is > 31.5dB, keep the upper 31.5dB range */
    if ((pwrTableT4[63] - pwrTableT4[0]) > 126) {
        pMin = pwrTableT4[63] - 126;
    } else {
        pMin = pwrTableT4[0];
    }

    pMax = pwrTableT4[63];
    jjMax = 63;

    /* Search for highest pcdac 0.25dB below maxPower */
    while ((pwrTableT4[jjMax] > (pMax - 1) ) && (jjMax >= 0)) {
        jjMax--;
    }

    jj = jjMax;
    currPower = pMax;
    for (ii = 63; ii >= 0; ii--) {
        while ((jj < 64) && (jj > 0) && (pwrTableT4[jj] >= currPower)) {
            jj--;
        }
        if (jj == 0) {
            while (ii >= 0) {
                retVals[ii] = retVals[ii + 1];
                ii--;
            }
            break;
        }
        retVals[ii] = jj;
        currPower -= 2;  // corresponds to a 0.5dB step
    }
    return pMin;
}

/*
 * Combines the XPD curves from two calibration sets into a single
 * power table and adjusts the power table so the max power is relative
 * to the maximum index in the power table
 *
 * WARNING: rates must be adjusted for this relative power table
 */
static int16_t
getPminAndPcdacTableFromTwoPowerTables(int16_t *pwrTableLXpdT4,
	int16_t *pwrTableHXpdT4, uint16_t retVals[], int16_t *pMid)
{
    int16_t     ii, jj, jjMax;
    int16_t     pMin, pMax, currPower;
    int16_t     *pwrTableT4;
    uint16_t    msbFlag = 0x40;  // turns on the 7th bit of the pcdac

    /* If the spread is > 31.5dB, keep the upper 31.5dB range */
    if ((pwrTableLXpdT4[63] - pwrTableHXpdT4[0]) > 126) {
        pMin = pwrTableLXpdT4[63] - 126;
    } else {
        pMin = pwrTableHXpdT4[0];
    }

    pMax = pwrTableLXpdT4[63];
    jjMax = 63;
    /* Search for highest pcdac 0.25dB below maxPower */
    while ((pwrTableLXpdT4[jjMax] > (pMax - 1) ) && (jjMax >= 0)){
        jjMax--;
    }

    *pMid = pwrTableHXpdT4[63];
    jj = jjMax;
    ii = 63;
    currPower = pMax;
    pwrTableT4 = &(pwrTableLXpdT4[0]);
    while (ii >= 0) {
        if ((currPower <= *pMid) || ( (jj == 0) && (msbFlag == 0x40))){
            msbFlag = 0x00;
            pwrTableT4 = &(pwrTableHXpdT4[0]);
            jj = 63;
        }
        while ((jj > 0) && (pwrTableT4[jj] >= currPower)) {
            jj--;
        }
        if ((jj == 0) && (msbFlag == 0x00)) {
            while (ii >= 0) {
                retVals[ii] = retVals[ii+1];
                ii--;
            }
            break;
        }
        retVals[ii] = jj | msbFlag;
        currPower -= 2;  // corresponds to a 0.5dB step
        ii--;
    }
    return pMin;
}

static int16_t
ar5112GetMinPower(struct ath_hal *ah, const EXPN_DATA_PER_CHANNEL_5112 *data)
{
	int i, minIndex;
	int16_t minGain,minPwr,minPcdac,retVal;

	/* Assume NUM_POINTS_XPD0 > 0 */
	minGain = data->pDataPerXPD[0].xpd_gain;
	for (minIndex=0,i=1; i<NUM_XPD_PER_CHANNEL; i++) {
		if (data->pDataPerXPD[i].xpd_gain < minGain) {
			minIndex = i;
			minGain = data->pDataPerXPD[i].xpd_gain;
		}
	}
	minPwr = data->pDataPerXPD[minIndex].pwr_t4[0];
	minPcdac = data->pDataPerXPD[minIndex].pcdac[0];
	for (i=1; i<NUM_POINTS_XPD0; i++) {
		if (data->pDataPerXPD[minIndex].pwr_t4[i] < minPwr) {
			minPwr = data->pDataPerXPD[minIndex].pwr_t4[i];
			minPcdac = data->pDataPerXPD[minIndex].pcdac[i];
		}
	}
	retVal = minPwr - (minPcdac*2);
	return(retVal);
}
	
static HAL_BOOL
ar5112GetChannelMaxMinPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan,
	int16_t *maxPow, int16_t *minPow)
{
	uint16_t freq = chan->ic_freq;		/* NB: never mapped */
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	int numChannels=0,i,last;
	int totalD, totalF,totalMin;
	const EXPN_DATA_PER_CHANNEL_5112 *data=AH_NULL;
	const EEPROM_POWER_EXPN_5112 *powerArray=AH_NULL;

	*maxPow = 0;
	if (IEEE80211_IS_CHAN_A(chan)) {
		powerArray = ee->ee_modePowerArray5112;
		data = powerArray[headerInfo11A].pDataPerChannel;
		numChannels = powerArray[headerInfo11A].numChannels;
	} else if (IEEE80211_IS_CHAN_G(chan) || IEEE80211_IS_CHAN_108G(chan)) {
		/* XXX - is this correct? Should we also use the same power for turbo G? */
		powerArray = ee->ee_modePowerArray5112;
		data = powerArray[headerInfo11G].pDataPerChannel;
		numChannels = powerArray[headerInfo11G].numChannels;
	} else if (IEEE80211_IS_CHAN_B(chan)) {
		powerArray = ee->ee_modePowerArray5112;
		data = powerArray[headerInfo11B].pDataPerChannel;
		numChannels = powerArray[headerInfo11B].numChannels;
	} else {
		return (AH_TRUE);
	}
	/* Make sure the channel is in the range of the TP values 
	 *  (freq piers)
	 */
	if (numChannels < 1)
		return(AH_FALSE);

	if ((freq < data[0].channelValue) ||
	    (freq > data[numChannels-1].channelValue)) {
		if (freq < data[0].channelValue) {
			*maxPow = data[0].maxPower_t4;
			*minPow = ar5112GetMinPower(ah, &data[0]);
			return(AH_TRUE);
		} else {
			*maxPow = data[numChannels - 1].maxPower_t4;
			*minPow = ar5112GetMinPower(ah, &data[numChannels - 1]);
			return(AH_TRUE);
		}
	}

	/* Linearly interpolate the power value now */
	for (last=0,i=0;
	     (i<numChannels) && (freq > data[i].channelValue);
	     last=i++);
	totalD = data[i].channelValue - data[last].channelValue;
	if (totalD > 0) {
		totalF = data[i].maxPower_t4 - data[last].maxPower_t4;
		*maxPow = (int8_t) ((totalF*(freq-data[last].channelValue) + data[last].maxPower_t4*totalD)/totalD);

		totalMin = ar5112GetMinPower(ah,&data[i]) - ar5112GetMinPower(ah, &data[last]);
		*minPow = (int8_t) ((totalMin*(freq-data[last].channelValue) + ar5112GetMinPower(ah, &data[last])*totalD)/totalD);
		return (AH_TRUE);
	} else {
		if (freq == data[i].channelValue) {
			*maxPow = data[i].maxPower_t4;
			*minPow = ar5112GetMinPower(ah, &data[i]);
			return(AH_TRUE);
		} else
			return(AH_FALSE);
	}
}

/*
 * Free memory for analog bank scratch buffers
 */
static void
ar5112RfDetach(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALASSERT(ahp->ah_rfHal != AH_NULL);
	ath_hal_free(ahp->ah_rfHal);
	ahp->ah_rfHal = AH_NULL;
}

/*
 * Allocate memory for analog bank scratch buffers
 * Scratch Buffer will be reinitialized every reset so no need to zero now
 */
static HAL_BOOL
ar5112RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5112State *priv;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	HALASSERT(ahp->ah_rfHal == AH_NULL);
	priv = ath_hal_malloc(sizeof(struct ar5112State));
	if (priv == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot allocate private state\n", __func__);
		*status = HAL_ENOMEM;		/* XXX */
		return AH_FALSE;
	}
	priv->base.rfDetach		= ar5112RfDetach;
	priv->base.writeRegs		= ar5112WriteRegs;
	priv->base.getRfBank		= ar5112GetRfBank;
	priv->base.setChannel		= ar5112SetChannel;
	priv->base.setRfRegs		= ar5112SetRfRegs;
	priv->base.setPowerTable	= ar5112SetPowerTable;
	priv->base.getChannelMaxMinPower = ar5112GetChannelMaxMinPower;
	priv->base.getNfAdjust		= ar5212GetNfAdjust;

	ahp->ah_pcdacTable = priv->pcdacTable;
	ahp->ah_pcdacTableSize = sizeof(priv->pcdacTable);
	ahp->ah_rfHal = &priv->base;

	return AH_TRUE;
}

static HAL_BOOL
ar5112Probe(struct ath_hal *ah)
{
	return IS_RAD5112(ah);
}
AH_RF(RF5112, ar5112Probe, ar5112RfAttach);
