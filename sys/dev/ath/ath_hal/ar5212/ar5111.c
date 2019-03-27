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

#define AH_5212_5111
#include "ar5212/ar5212.ini"

#define	N(a)	(sizeof(a)/sizeof(a[0]))

struct ar5111State {
	RF_HAL_FUNCS	base;		/* public state, must be first */
	uint16_t	pcdacTable[PWR_TABLE_SIZE];

	uint32_t	Bank0Data[N(ar5212Bank0_5111)];
	uint32_t	Bank1Data[N(ar5212Bank1_5111)];
	uint32_t	Bank2Data[N(ar5212Bank2_5111)];
	uint32_t	Bank3Data[N(ar5212Bank3_5111)];
	uint32_t	Bank6Data[N(ar5212Bank6_5111)];
	uint32_t	Bank7Data[N(ar5212Bank7_5111)];
};
#define	AR5111(ah)	((struct ar5111State *) AH5212(ah)->ah_rfHal)

static uint16_t ar5212GetScaledPower(uint16_t channel, uint16_t pcdacValue,
		const PCDACS_EEPROM *pSrcStruct);
static HAL_BOOL ar5212FindValueInList(uint16_t channel, uint16_t pcdacValue,
		const PCDACS_EEPROM *pSrcStruct, uint16_t *powerValue);
static void ar5212GetLowerUpperPcdacs(uint16_t pcdac, uint16_t channel,
		const PCDACS_EEPROM *pSrcStruct,
		uint16_t *pLowerPcdac, uint16_t *pUpperPcdac);

extern void ar5212GetLowerUpperValues(uint16_t value,
		const uint16_t *pList, uint16_t listSize,
		uint16_t *pLowerValue, uint16_t *pUpperValue);
extern	void ar5212ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32,
		uint32_t numBits, uint32_t firstBit, uint32_t column);

static void
ar5111WriteRegs(struct ath_hal *ah, u_int modesIndex, u_int freqIndex,
	int writes)
{
	HAL_INI_WRITE_ARRAY(ah, ar5212Modes_5111, modesIndex, writes);
	HAL_INI_WRITE_ARRAY(ah, ar5212Common_5111, 1, writes);
	HAL_INI_WRITE_ARRAY(ah, ar5212BB_RfGain_5111, freqIndex, writes);
}

/*
 * Take the MHz channel value and set the Channel value
 *
 * ASSUMES: Writes enabled to analog bus
 */
static HAL_BOOL
ar5111SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define CI_2GHZ_INDEX_CORRECTION 19
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	uint32_t refClk, reg32, data2111;
	int16_t chan5111, chanIEEE;

	/*
	 * Structure to hold 11b tuning information for 5111/2111
	 * 16 MHz mode, divider ratio = 198 = NP+S. N=16, S=4 or 6, P=12
	 */
	typedef struct {
		uint32_t	refClkSel;	/* reference clock, 1 for 16 MHz */
		uint32_t	channelSelect;	/* P[7:4]S[3:0] bits */
		uint16_t	channel5111;	/* 11a channel for 5111 */
	} CHAN_INFO_2GHZ;

	static const CHAN_INFO_2GHZ chan2GHzData[] = {
		{ 1, 0x46, 96  },	/* 2312 -19 */
		{ 1, 0x46, 97  },	/* 2317 -18 */
		{ 1, 0x46, 98  },	/* 2322 -17 */
		{ 1, 0x46, 99  },	/* 2327 -16 */
		{ 1, 0x46, 100 },	/* 2332 -15 */
		{ 1, 0x46, 101 },	/* 2337 -14 */
		{ 1, 0x46, 102 },	/* 2342 -13 */
		{ 1, 0x46, 103 },	/* 2347 -12 */
		{ 1, 0x46, 104 },	/* 2352 -11 */
		{ 1, 0x46, 105 },	/* 2357 -10 */
		{ 1, 0x46, 106 },	/* 2362  -9 */
		{ 1, 0x46, 107 },	/* 2367  -8 */
		{ 1, 0x46, 108 },	/* 2372  -7 */
		/* index -6 to 0 are pad to make this a nolookup table */
		{ 1, 0x46, 116 },	/*       -6 */
		{ 1, 0x46, 116 },	/*       -5 */
		{ 1, 0x46, 116 },	/*       -4 */
		{ 1, 0x46, 116 },	/*       -3 */
		{ 1, 0x46, 116 },	/*       -2 */
		{ 1, 0x46, 116 },	/*       -1 */
		{ 1, 0x46, 116 },	/*        0 */
		{ 1, 0x46, 116 },	/* 2412   1 */
		{ 1, 0x46, 117 },	/* 2417   2 */
		{ 1, 0x46, 118 },	/* 2422   3 */
		{ 1, 0x46, 119 },	/* 2427   4 */
		{ 1, 0x46, 120 },	/* 2432   5 */
		{ 1, 0x46, 121 },	/* 2437   6 */
		{ 1, 0x46, 122 },	/* 2442   7 */
		{ 1, 0x46, 123 },	/* 2447   8 */
		{ 1, 0x46, 124 },	/* 2452   9 */
		{ 1, 0x46, 125 },	/* 2457  10 */
		{ 1, 0x46, 126 },	/* 2462  11 */
		{ 1, 0x46, 127 },	/* 2467  12 */
		{ 1, 0x46, 128 },	/* 2472  13 */
		{ 1, 0x44, 124 },	/* 2484  14 */
		{ 1, 0x46, 136 },	/* 2512  15 */
		{ 1, 0x46, 140 },	/* 2532  16 */
		{ 1, 0x46, 144 },	/* 2552  17 */
		{ 1, 0x46, 148 },	/* 2572  18 */
		{ 1, 0x46, 152 },	/* 2592  19 */
		{ 1, 0x46, 156 },	/* 2612  20 */
		{ 1, 0x46, 160 },	/* 2632  21 */
		{ 1, 0x46, 164 },	/* 2652  22 */
		{ 1, 0x46, 168 },	/* 2672  23 */
		{ 1, 0x46, 172 },	/* 2692  24 */
		{ 1, 0x46, 176 },	/* 2712  25 */
		{ 1, 0x46, 180 } 	/* 2732  26 */
	};

	OS_MARK(ah, AH_MARK_SETCHANNEL, freq);

	chanIEEE = chan->ic_ieee;
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		const CHAN_INFO_2GHZ* ci =
			&chan2GHzData[chanIEEE + CI_2GHZ_INDEX_CORRECTION];
		uint32_t txctl;

		data2111 = ((ath_hal_reverseBits(ci->channelSelect, 8) & 0xff)
				<< 5)
			 | (ci->refClkSel << 4);
		chan5111 = ci->channel5111;
		txctl = OS_REG_READ(ah, AR_PHY_CCK_TX_CTRL);
		if (freq == 2484) {
			/* Enable channel spreading for channel 14 */
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				txctl | AR_PHY_CCK_TX_CTRL_JAPAN);
		} else {
			OS_REG_WRITE(ah, AR_PHY_CCK_TX_CTRL,
				txctl &~ AR_PHY_CCK_TX_CTRL_JAPAN);
		}
	} else {
		chan5111 = chanIEEE;	/* no conversion needed */
		data2111 = 0;
	}

	/* Rest of the code is common for 5 GHz and 2.4 GHz. */
	if (chan5111 >= 145 || (chan5111 & 0x1)) {
		reg32  = ath_hal_reverseBits(chan5111 - 24, 8) & 0xff;
		refClk = 1;
	} else {
		reg32  = ath_hal_reverseBits(((chan5111 - 24)/2), 8) & 0xff;
		refClk = 0;
	}

	reg32 = (reg32 << 2) | (refClk << 1) | (1 << 10) | 0x1;
	OS_REG_WRITE(ah, AR_PHY(0x27), ((data2111 & 0xff) << 8) | (reg32 & 0xff));
	reg32 >>= 8;
	OS_REG_WRITE(ah, AR_PHY(0x34), (data2111 & 0xff00) | (reg32 & 0xff));

	AH_PRIVATE(ah)->ah_curchan = chan;
	return AH_TRUE;
#undef CI_2GHZ_INDEX_CORRECTION
}

/*
 * Return a reference to the requested RF Bank.
 */
static uint32_t *
ar5111GetRfBank(struct ath_hal *ah, int bank)
{
	struct ar5111State *priv = AR5111(ah);

	HALASSERT(priv != AH_NULL);
	switch (bank) {
	case 0: return priv->Bank0Data;
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
ar5111SetRfRegs(struct ath_hal *ah, const struct ieee80211_channel *chan,
	uint16_t modesIndex, uint16_t *rfXpdGain)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint16_t rfXpdGainFixed, rfPloSel, rfPwdXpd, gainI;
	uint16_t tempOB, tempDB;
	uint32_t ob2GHz, db2GHz, rfReg[N(ar5212Bank6_5111)];
	int i, regWrites = 0;

	HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: chan %u/0x%x modesIndex %u\n",
	    __func__, chan->ic_freq, chan->ic_flags, modesIndex);

	/* Setup rf parameters */
	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		if (4000 < freq && freq < 5260) {
			tempOB = ee->ee_ob1;
			tempDB = ee->ee_db1;
		} else if (5260 <= freq && freq < 5500) {
			tempOB = ee->ee_ob2;
			tempDB = ee->ee_db2;
		} else if (5500 <= freq && freq < 5725) {
			tempOB = ee->ee_ob3;
			tempDB = ee->ee_db3;
		} else if (freq >= 5725) {
			tempOB = ee->ee_ob4;
			tempDB = ee->ee_db4;
		} else {
			/* XXX when does this happen??? */
			tempOB = tempDB = 0;
		}
		ob2GHz = db2GHz = 0;

		rfXpdGainFixed = ee->ee_xgain[headerInfo11A];
		rfPloSel = ee->ee_xpd[headerInfo11A];
		rfPwdXpd = !ee->ee_xpd[headerInfo11A];
		gainI = ee->ee_gainI[headerInfo11A];
		break;
	case IEEE80211_CHAN_B:
		tempOB = ee->ee_obFor24;
		tempDB = ee->ee_dbFor24;
		ob2GHz = ee->ee_ob2GHz[0];
		db2GHz = ee->ee_db2GHz[0];

		rfXpdGainFixed = ee->ee_xgain[headerInfo11B];
		rfPloSel = ee->ee_xpd[headerInfo11B];
		rfPwdXpd = !ee->ee_xpd[headerInfo11B];
		gainI = ee->ee_gainI[headerInfo11B];
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_PUREG:	/* NB: really 108G */
		tempOB = ee->ee_obFor24g;
		tempDB = ee->ee_dbFor24g;
		ob2GHz = ee->ee_ob2GHz[1];
		db2GHz = ee->ee_db2GHz[1];

		rfXpdGainFixed = ee->ee_xgain[headerInfo11G];
		rfPloSel = ee->ee_xpd[headerInfo11G];
		rfPwdXpd = !ee->ee_xpd[headerInfo11G];
		gainI = ee->ee_gainI[headerInfo11G];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	HALASSERT(1 <= tempOB && tempOB <= 5);
	HALASSERT(1 <= tempDB && tempDB <= 5);

	/* Bank 0 Write */
	for (i = 0; i < N(ar5212Bank0_5111); i++)
		rfReg[i] = ar5212Bank0_5111[i][modesIndex];
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		ar5212ModifyRfBuffer(rfReg, ob2GHz, 3, 119, 0);
		ar5212ModifyRfBuffer(rfReg, db2GHz, 3, 122, 0);
	}
	HAL_INI_WRITE_BANK(ah, ar5212Bank0_5111, rfReg, regWrites);

	/* Bank 1 Write */
	HAL_INI_WRITE_ARRAY(ah, ar5212Bank1_5111, 1, regWrites);

	/* Bank 2 Write */
	HAL_INI_WRITE_ARRAY(ah, ar5212Bank2_5111, modesIndex, regWrites);

	/* Bank 3 Write */
	HAL_INI_WRITE_ARRAY(ah, ar5212Bank3_5111, modesIndex, regWrites);

	/* Bank 6 Write */
	for (i = 0; i < N(ar5212Bank6_5111); i++)
		rfReg[i] = ar5212Bank6_5111[i][modesIndex];
	if (IEEE80211_IS_CHAN_A(chan)) {	/* NB: CHANNEL_A | CHANNEL_T */
		ar5212ModifyRfBuffer(rfReg, ee->ee_cornerCal.pd84, 1, 51, 3);
		ar5212ModifyRfBuffer(rfReg, ee->ee_cornerCal.pd90, 1, 45, 3);
	}
	ar5212ModifyRfBuffer(rfReg, rfPwdXpd, 1, 95, 0);
	ar5212ModifyRfBuffer(rfReg, rfXpdGainFixed, 4, 96, 0);
	/* Set 5212 OB & DB */
	ar5212ModifyRfBuffer(rfReg, tempOB, 3, 104, 0);
	ar5212ModifyRfBuffer(rfReg, tempDB, 3, 107, 0);
	HAL_INI_WRITE_BANK(ah, ar5212Bank6_5111, rfReg, regWrites);

	/* Bank 7 Write */
	for (i = 0; i < N(ar5212Bank7_5111); i++)
		rfReg[i] = ar5212Bank7_5111[i][modesIndex];
	ar5212ModifyRfBuffer(rfReg, gainI, 6, 29, 0);   
	ar5212ModifyRfBuffer(rfReg, rfPloSel, 1, 4, 0);   

	if (IEEE80211_IS_CHAN_QUARTER(chan) || IEEE80211_IS_CHAN_HALF(chan)) {
        	uint32_t	rfWaitI, rfWaitS, rfMaxTime;

        	rfWaitS = 0x1f;
        	rfWaitI = (IEEE80211_IS_CHAN_HALF(chan)) ?  0x10 : 0x1f;
        	rfMaxTime = 3;
        	ar5212ModifyRfBuffer(rfReg, rfWaitS, 5, 19, 0);
        	ar5212ModifyRfBuffer(rfReg, rfWaitI, 5, 24, 0);
        	ar5212ModifyRfBuffer(rfReg, rfMaxTime, 2, 49, 0);

	}

	HAL_INI_WRITE_BANK(ah, ar5212Bank7_5111, rfReg, regWrites);

	/* Now that we have reprogrammed rfgain value, clear the flag. */
	ahp->ah_rfgainState = HAL_RFGAIN_INACTIVE;

	return AH_TRUE;
}

/*
 * Returns interpolated or the scaled up interpolated value
 */
static uint16_t
interpolate(uint16_t target, uint16_t srcLeft, uint16_t srcRight,
	uint16_t targetLeft, uint16_t targetRight)
{
	uint16_t rv;
	int16_t lRatio;

	/* to get an accurate ratio, always scale, if want to scale, then don't scale back down */
	if ((targetLeft * targetRight) == 0)
		return 0;

	if (srcRight != srcLeft) {
		/*
		 * Note the ratio always need to be scaled,
		 * since it will be a fraction.
		 */
		lRatio = (target - srcLeft) * EEP_SCALE / (srcRight - srcLeft);
		if (lRatio < 0) {
		    /* Return as Left target if value would be negative */
		    rv = targetLeft;
		} else if (lRatio > EEP_SCALE) {
		    /* Return as Right target if Ratio is greater than 100% (SCALE) */
		    rv = targetRight;
		} else {
			rv = (lRatio * targetRight + (EEP_SCALE - lRatio) *
					targetLeft) / EEP_SCALE;
		}
	} else {
		rv = targetLeft;
	}
	return rv;
}

/*
 * Read the transmit power levels from the structures taken from EEPROM
 * Interpolate read transmit power values for this channel
 * Organize the transmit power values into a table for writing into the hardware
 */
static HAL_BOOL
ar5111SetPowerTable(struct ath_hal *ah,
	int16_t *pMinPower, int16_t *pMaxPower,
	const struct ieee80211_channel *chan,
	uint16_t *rfXpdGain)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	FULL_PCDAC_STRUCT pcdacStruct;
	int i, j;

	uint16_t     *pPcdacValues;
	int16_t      *pScaledUpDbm;
	int16_t      minScaledPwr;
	int16_t      maxScaledPwr;
	int16_t      pwr;
	uint16_t     pcdacMin = 0;
	uint16_t     pcdacMax = PCDAC_STOP;
	uint16_t     pcdacTableIndex;
	uint16_t     scaledPcdac;
	PCDACS_EEPROM *pSrcStruct;
	PCDACS_EEPROM eepromPcdacs;

	/* setup the pcdac struct to point to the correct info, based on mode */
	switch (chan->ic_flags & IEEE80211_CHAN_ALLTURBOFULL) {
	case IEEE80211_CHAN_A:
	case IEEE80211_CHAN_ST:
		eepromPcdacs.numChannels     = ee->ee_numChannels11a;
		eepromPcdacs.pChannelList    = ee->ee_channels11a;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11a;
		break;
	case IEEE80211_CHAN_B:
		eepromPcdacs.numChannels     = ee->ee_numChannels2_4;
		eepromPcdacs.pChannelList    = ee->ee_channels11b;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11b;
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_108G:
		eepromPcdacs.numChannels     = ee->ee_numChannels2_4;
		eepromPcdacs.pChannelList    = ee->ee_channels11g;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11g;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	pSrcStruct = &eepromPcdacs;

	OS_MEMZERO(&pcdacStruct, sizeof(pcdacStruct));
	pPcdacValues = pcdacStruct.PcdacValues;
	pScaledUpDbm = pcdacStruct.PwrValues;

	/* Initialize the pcdacs to dBM structs pcdacs to be 1 to 63 */
	for (i = PCDAC_START, j = 0; i <= PCDAC_STOP; i+= PCDAC_STEP, j++)
		pPcdacValues[j] = i;

	pcdacStruct.numPcdacValues = j;
	pcdacStruct.pcdacMin = PCDAC_START;
	pcdacStruct.pcdacMax = PCDAC_STOP;

	/* Fill out the power values for this channel */
	for (j = 0; j < pcdacStruct.numPcdacValues; j++ )
		pScaledUpDbm[j] = ar5212GetScaledPower(freq,
			pPcdacValues[j], pSrcStruct);

	/* Now scale the pcdac values to fit in the 64 entry power table */
	minScaledPwr = pScaledUpDbm[0];
	maxScaledPwr = pScaledUpDbm[pcdacStruct.numPcdacValues - 1];

	/* find minimum and make monotonic */
	for (j = 0; j < pcdacStruct.numPcdacValues; j++) {
		if (minScaledPwr >= pScaledUpDbm[j]) {
			minScaledPwr = pScaledUpDbm[j];
			pcdacMin = j;
		}
		/*
		 * Make the full_hsh monotonically increasing otherwise
		 * interpolation algorithm will get fooled gotta start
		 * working from the top, hence i = 63 - j.
		 */
		i = (uint16_t)(pcdacStruct.numPcdacValues - 1 - j);
		if (i == 0)
			break;
		if (pScaledUpDbm[i-1] > pScaledUpDbm[i]) {
			/*
			 * It could be a glitch, so make the power for
			 * this pcdac the same as the power from the
			 * next highest pcdac.
			 */
			pScaledUpDbm[i - 1] = pScaledUpDbm[i];
		}
	}

	for (j = 0; j < pcdacStruct.numPcdacValues; j++)
		if (maxScaledPwr < pScaledUpDbm[j]) {
			maxScaledPwr = pScaledUpDbm[j];
			pcdacMax = j;
		}

	/* Find the first power level with a pcdac */
	pwr = (uint16_t)(PWR_STEP *
		((minScaledPwr - PWR_MIN + PWR_STEP / 2) / PWR_STEP) + PWR_MIN);

	/* Write all the first pcdac entries based off the pcdacMin */
	pcdacTableIndex = 0;
	for (i = 0; i < (2 * (pwr - PWR_MIN) / EEP_SCALE + 1); i++) {
		HALASSERT(pcdacTableIndex < PWR_TABLE_SIZE);
		ahp->ah_pcdacTable[pcdacTableIndex++] = pcdacMin;
	}

	i = 0;
	while (pwr < pScaledUpDbm[pcdacStruct.numPcdacValues - 1] &&
	    pcdacTableIndex < PWR_TABLE_SIZE) {
		pwr += PWR_STEP;
		/* stop if dbM > max_power_possible */
		while (pwr < pScaledUpDbm[pcdacStruct.numPcdacValues - 1] &&
		       (pwr - pScaledUpDbm[i])*(pwr - pScaledUpDbm[i+1]) > 0)
			i++;
		/* scale by 2 and add 1 to enable round up or down as needed */
		scaledPcdac = (uint16_t)(interpolate(pwr,
			pScaledUpDbm[i], pScaledUpDbm[i + 1],
			(uint16_t)(pPcdacValues[i] * 2),
			(uint16_t)(pPcdacValues[i + 1] * 2)) + 1);

		HALASSERT(pcdacTableIndex < PWR_TABLE_SIZE);
		ahp->ah_pcdacTable[pcdacTableIndex] = scaledPcdac / 2;
		if (ahp->ah_pcdacTable[pcdacTableIndex] > pcdacMax)
			ahp->ah_pcdacTable[pcdacTableIndex] = pcdacMax;
		pcdacTableIndex++;
	}

	/* Write all the last pcdac entries based off the last valid pcdac */
	while (pcdacTableIndex < PWR_TABLE_SIZE) {
		ahp->ah_pcdacTable[pcdacTableIndex] =
			ahp->ah_pcdacTable[pcdacTableIndex - 1];
		pcdacTableIndex++;
	}

	/* No power table adjustment for 5111 */
	ahp->ah_txPowerIndexOffset = 0;

	return AH_TRUE;
}

/*
 * Get or interpolate the pcdac value from the calibrated data.
 */
static uint16_t
ar5212GetScaledPower(uint16_t channel, uint16_t pcdacValue,
	const PCDACS_EEPROM *pSrcStruct)
{
	uint16_t powerValue;
	uint16_t lFreq, rFreq;		/* left and right frequency values */
	uint16_t llPcdac, ulPcdac;	/* lower and upper left pcdac values */
	uint16_t lrPcdac, urPcdac;	/* lower and upper right pcdac values */
	uint16_t lPwr, uPwr;		/* lower and upper temp pwr values */
	uint16_t lScaledPwr, rScaledPwr; /* left and right scaled power */

	if (ar5212FindValueInList(channel, pcdacValue, pSrcStruct, &powerValue)) {
		/* value was copied from srcStruct */
		return powerValue;
	}

	ar5212GetLowerUpperValues(channel,
		pSrcStruct->pChannelList, pSrcStruct->numChannels,
		&lFreq, &rFreq);
	ar5212GetLowerUpperPcdacs(pcdacValue,
		lFreq, pSrcStruct, &llPcdac, &ulPcdac);
	ar5212GetLowerUpperPcdacs(pcdacValue,
		rFreq, pSrcStruct, &lrPcdac, &urPcdac);

	/* get the power index for the pcdac value */
	ar5212FindValueInList(lFreq, llPcdac, pSrcStruct, &lPwr);
	ar5212FindValueInList(lFreq, ulPcdac, pSrcStruct, &uPwr);
	lScaledPwr = interpolate(pcdacValue, llPcdac, ulPcdac, lPwr, uPwr);

	ar5212FindValueInList(rFreq, lrPcdac, pSrcStruct, &lPwr);
	ar5212FindValueInList(rFreq, urPcdac, pSrcStruct, &uPwr);
	rScaledPwr = interpolate(pcdacValue, lrPcdac, urPcdac, lPwr, uPwr);

	return interpolate(channel, lFreq, rFreq, lScaledPwr, rScaledPwr);
}

/*
 * Find the value from the calibrated source data struct
 */
static HAL_BOOL
ar5212FindValueInList(uint16_t channel, uint16_t pcdacValue,
	const PCDACS_EEPROM *pSrcStruct, uint16_t *powerValue)
{
	const DATA_PER_CHANNEL *pChannelData = pSrcStruct->pDataPerChannel;
	int i;

	for (i = 0; i < pSrcStruct->numChannels; i++ ) {
		if (pChannelData->channelValue == channel) {
			const uint16_t* pPcdac = pChannelData->PcdacValues;
			int j;

			for (j = 0; j < pChannelData->numPcdacValues; j++ ) {
				if (*pPcdac == pcdacValue) {
					*powerValue = pChannelData->PwrValues[j];
					return AH_TRUE;
				}
				pPcdac++;
			}
		}
		pChannelData++;
	}
	return AH_FALSE;
}

/*
 * Get the upper and lower pcdac given the channel and the pcdac
 * used in the search
 */
static void
ar5212GetLowerUpperPcdacs(uint16_t pcdac, uint16_t channel,
	const PCDACS_EEPROM *pSrcStruct,
	uint16_t *pLowerPcdac, uint16_t *pUpperPcdac)
{
	const DATA_PER_CHANNEL *pChannelData = pSrcStruct->pDataPerChannel;
	int i;

	/* Find the channel information */
	for (i = 0; i < pSrcStruct->numChannels; i++) {
		if (pChannelData->channelValue == channel)
			break;
		pChannelData++;
	}
	ar5212GetLowerUpperValues(pcdac, pChannelData->PcdacValues,
		      pChannelData->numPcdacValues,
		      pLowerPcdac, pUpperPcdac);
}

static HAL_BOOL
ar5111GetChannelMaxMinPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan,
	int16_t *maxPow, int16_t *minPow)
{
	/* XXX - Get 5111 power limits! */
	/* NB: caller will cope */
	return AH_FALSE;
}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 */
static int16_t
ar5111GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	static const struct {
		uint16_t freqLow;
		int16_t	  adjust;
	} adjust5111[] = {
		{ 5790,	6 },	/* NB: ordered high -> low */
		{ 5730, 4 },
		{ 5690, 3 },
		{ 5660, 2 },
		{ 5610, 1 },
		{ 5530, 0 },
		{ 5450, 0 },
		{ 5379, 1 },
		{ 5209, 3 },
		{ 3000, 5 },
		{    0, 0 },
	};
	int i;

	for (i = 0; c->channel <= adjust5111[i].freqLow; i++)
		;
	return adjust5111[i].adjust;
}

/*
 * Free memory for analog bank scratch buffers
 */
static void
ar5111RfDetach(struct ath_hal *ah)
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
ar5111RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5111State *priv;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	HALASSERT(ahp->ah_rfHal == AH_NULL);
	priv = ath_hal_malloc(sizeof(struct ar5111State));
	if (priv == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot allocate private state\n", __func__);
		*status = HAL_ENOMEM;		/* XXX */
		return AH_FALSE;
	}
	priv->base.rfDetach		= ar5111RfDetach;
	priv->base.writeRegs		= ar5111WriteRegs;
	priv->base.getRfBank		= ar5111GetRfBank;
	priv->base.setChannel		= ar5111SetChannel;
	priv->base.setRfRegs		= ar5111SetRfRegs;
	priv->base.setPowerTable	= ar5111SetPowerTable;
	priv->base.getChannelMaxMinPower = ar5111GetChannelMaxMinPower;
	priv->base.getNfAdjust		= ar5111GetNfAdjust;

	ahp->ah_pcdacTable = priv->pcdacTable;
	ahp->ah_pcdacTableSize = sizeof(priv->pcdacTable);
	ahp->ah_rfHal = &priv->base;

	return AH_TRUE;
}

static HAL_BOOL
ar5111Probe(struct ath_hal *ah)
{
	return IS_RAD5111(ah);
}
AH_RF(RF5111, ar5111Probe, ar5111RfAttach);
