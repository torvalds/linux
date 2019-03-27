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
#include "ah_eeprom_v3.h"

static void
getPcdacInterceptsFromPcdacMinMax(HAL_EEPROM *ee,
	uint16_t pcdacMin, uint16_t pcdacMax, uint16_t *vp)
{
	static const uint16_t intercepts3[] =
		{ 0, 5, 10, 20, 30, 50, 70, 85, 90, 95, 100 };
	static const uint16_t intercepts3_2[] =
		{ 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
	const uint16_t *ip = ee->ee_version < AR_EEPROM_VER3_2 ?
		intercepts3 : intercepts3_2;
	int i;

	/* loop for the percentages in steps or 5 */
	for (i = 0; i < NUM_INTERCEPTS; i++ )
		*vp++ = (ip[i] * pcdacMax + (100 - ip[i]) * pcdacMin) / 100;
}

/*
 * Get channel value from binary representation held in eeprom
 */
static uint16_t
fbin2freq(HAL_EEPROM *ee, uint16_t fbin)
{
	if (fbin == CHANNEL_UNUSED)	/* reserved value, don't convert */
		return fbin;
	return ee->ee_version <= AR_EEPROM_VER3_2 ?
		(fbin > 62 ? 5100 + 10*62 + 5*(fbin-62) : 5100 + 10*fbin) :
		4800 + 5*fbin;
}

static uint16_t
fbin2freq_2p4(HAL_EEPROM *ee, uint16_t fbin)
{
	if (fbin == CHANNEL_UNUSED)	/* reserved value, don't convert */
		return fbin;
	return ee->ee_version <= AR_EEPROM_VER3_2 ?
		2400 + fbin :
		2300 + fbin;
}

/*
 * Now copy EEPROM frequency pier contents into the allocated space
 */
static HAL_BOOL
readEepromFreqPierInfo(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	uint16_t eeval, off;
	int i;

	if (ee->ee_version >= AR_EEPROM_VER4_0 &&
	    ee->ee_eepMap && !ee->ee_Amode) {
		/*
		 * V4.0 EEPROMs with map type 1 have frequency pier
		 * data only when 11a mode is supported.
		 */
		return AH_TRUE;
	}
	if (ee->ee_version >= AR_EEPROM_VER3_3) {
		off = GROUPS_OFFSET3_3 + GROUP1_OFFSET;
		for (i = 0; i < ee->ee_numChannels11a; i += 2) {
			EEREAD(off++);
			ee->ee_channels11a[i]   = (eeval >> 8) & FREQ_MASK_3_3;
			ee->ee_channels11a[i+1] = eeval & FREQ_MASK_3_3;
		} 
	} else {
		off = GROUPS_OFFSET3_2 + GROUP1_OFFSET;

		EEREAD(off++);
		ee->ee_channels11a[0] = (eeval >> 9) & FREQ_MASK;
		ee->ee_channels11a[1] = (eeval >> 2) & FREQ_MASK;
		ee->ee_channels11a[2] = (eeval << 5) & FREQ_MASK;

		EEREAD(off++);
		ee->ee_channels11a[2] |= (eeval >> 11) & 0x1f;
		ee->ee_channels11a[3]  = (eeval >>  4) & FREQ_MASK;
		ee->ee_channels11a[4]  = (eeval <<  3) & FREQ_MASK;

		EEREAD(off++);
		ee->ee_channels11a[4] |= (eeval >> 13) & 0x7;
		ee->ee_channels11a[5]  = (eeval >>  6) & FREQ_MASK;
		ee->ee_channels11a[6]  = (eeval <<  1) & FREQ_MASK;

		EEREAD(off++);
		ee->ee_channels11a[6] |= (eeval >> 15) & 0x1;
		ee->ee_channels11a[7]  = (eeval >>  8) & FREQ_MASK;
		ee->ee_channels11a[8]  = (eeval >>  1) & FREQ_MASK;
		ee->ee_channels11a[9]  = (eeval <<  6) & FREQ_MASK;

		EEREAD(off++);
		ee->ee_channels11a[9] |= (eeval >> 10) & 0x3f;
	}

	for (i = 0; i < ee->ee_numChannels11a; i++)
		ee->ee_channels11a[i] = fbin2freq(ee, ee->ee_channels11a[i]);

	return AH_TRUE;
#undef EEREAD
}

/*
 * Rev 4 Eeprom 5112 Power Extract Functions
 */

/*
 * Allocate the power information based on the number of channels
 * recorded by the calibration.  These values are then initialized.
 */
static HAL_BOOL
eepromAllocExpnPower5112(struct ath_hal *ah,
	const EEPROM_POWER_5112 *pCalDataset,
	EEPROM_POWER_EXPN_5112 *pPowerExpn)
{
	uint16_t numChannels = pCalDataset->numChannels;
	const uint16_t *pChanList = pCalDataset->pChannels;
	void *data;
	int i, j;

	/* Allocate the channel and Power Data arrays together */
	data = ath_hal_malloc(
		roundup(sizeof(uint16_t) * numChannels, sizeof(uint32_t)) +
		sizeof(EXPN_DATA_PER_CHANNEL_5112) * numChannels);
	if (data == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s unable to allocate raw data struct (gen3)\n", __func__);
		return AH_FALSE;
	}
	pPowerExpn->pChannels = data;
	pPowerExpn->pDataPerChannel = (void *)(((char *)data) +
		roundup(sizeof(uint16_t) * numChannels, sizeof(uint32_t)));

	pPowerExpn->numChannels = numChannels;
	for (i = 0; i < numChannels; i++) {
		pPowerExpn->pChannels[i] =
			pPowerExpn->pDataPerChannel[i].channelValue =
				pChanList[i];
		for (j = 0; j < NUM_XPD_PER_CHANNEL; j++) {
			pPowerExpn->pDataPerChannel[i].pDataPerXPD[j].xpd_gain = j;
			pPowerExpn->pDataPerChannel[i].pDataPerXPD[j].numPcdacs = 0;
		}
		pPowerExpn->pDataPerChannel[i].pDataPerXPD[0].numPcdacs = 4;
		pPowerExpn->pDataPerChannel[i].pDataPerXPD[3].numPcdacs = 3;
	}
	return AH_TRUE;
}

/*
 * Expand the dataSet from the calibration information into the
 * final power structure for 5112
 */
static HAL_BOOL
eepromExpandPower5112(struct ath_hal *ah,
	const EEPROM_POWER_5112 *pCalDataset,
	EEPROM_POWER_EXPN_5112 *pPowerExpn)
{
	int ii, jj, kk;
	int16_t maxPower_t4;
	EXPN_DATA_PER_XPD_5112 *pExpnXPD;
	/* ptr to array of info held per channel */
	const EEPROM_DATA_PER_CHANNEL_5112 *pCalCh;
	uint16_t xgainList[2], xpdMask;

	pPowerExpn->xpdMask = pCalDataset->xpdMask;

	xgainList[0] = 0xDEAD;
	xgainList[1] = 0xDEAD;

	kk = 0;
	xpdMask = pPowerExpn->xpdMask;
	for (jj = 0; jj < NUM_XPD_PER_CHANNEL; jj++) {
		if (((xpdMask >> jj) & 1) > 0) {
			if (kk > 1) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "%s: too many xpdGains in dataset: %u\n",
				    __func__, kk);
				return AH_FALSE;
			}
			xgainList[kk++] = jj;
		}
	}

	pPowerExpn->numChannels = pCalDataset->numChannels;
	if (pPowerExpn->numChannels == 0) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: no channels\n", __func__);
		return AH_FALSE;
	}

	for (ii = 0; ii < pPowerExpn->numChannels; ii++) {
		pCalCh = &pCalDataset->pDataPerChannel[ii];
		pPowerExpn->pDataPerChannel[ii].channelValue =
			pCalCh->channelValue;
		pPowerExpn->pDataPerChannel[ii].maxPower_t4 =
			pCalCh->maxPower_t4;
		maxPower_t4 = pPowerExpn->pDataPerChannel[ii].maxPower_t4;

		for (jj = 0; jj < NUM_XPD_PER_CHANNEL; jj++)
			pPowerExpn->pDataPerChannel[ii].pDataPerXPD[jj].numPcdacs = 0;
		if (xgainList[1] == 0xDEAD) {
			jj = xgainList[0];
			pExpnXPD = &pPowerExpn->pDataPerChannel[ii].pDataPerXPD[jj];
			pExpnXPD->numPcdacs = 4;
			pExpnXPD->pcdac[0] = pCalCh->pcd1_xg0;
			pExpnXPD->pcdac[1] = (uint16_t)
				(pExpnXPD->pcdac[0] + pCalCh->pcd2_delta_xg0);
			pExpnXPD->pcdac[2] = (uint16_t)
				(pExpnXPD->pcdac[1] + pCalCh->pcd3_delta_xg0);
			pExpnXPD->pcdac[3] = (uint16_t)
				(pExpnXPD->pcdac[2] + pCalCh->pcd4_delta_xg0);

			pExpnXPD->pwr_t4[0] = pCalCh->pwr1_xg0;
			pExpnXPD->pwr_t4[1] = pCalCh->pwr2_xg0;
			pExpnXPD->pwr_t4[2] = pCalCh->pwr3_xg0;
			pExpnXPD->pwr_t4[3] = pCalCh->pwr4_xg0;

		} else {
			pPowerExpn->pDataPerChannel[ii].pDataPerXPD[xgainList[0]].pcdac[0] = pCalCh->pcd1_xg0;
			pPowerExpn->pDataPerChannel[ii].pDataPerXPD[xgainList[1]].pcdac[0] = 20;
			pPowerExpn->pDataPerChannel[ii].pDataPerXPD[xgainList[1]].pcdac[1] = 35;
			pPowerExpn->pDataPerChannel[ii].pDataPerXPD[xgainList[1]].pcdac[2] = 63;

			jj = xgainList[0];
			pExpnXPD = &pPowerExpn->pDataPerChannel[ii].pDataPerXPD[jj];
			pExpnXPD->numPcdacs = 4;
			pExpnXPD->pcdac[1] = (uint16_t)
				(pExpnXPD->pcdac[0] + pCalCh->pcd2_delta_xg0);
			pExpnXPD->pcdac[2] = (uint16_t)
				(pExpnXPD->pcdac[1] + pCalCh->pcd3_delta_xg0);
			pExpnXPD->pcdac[3] = (uint16_t)
				(pExpnXPD->pcdac[2] + pCalCh->pcd4_delta_xg0);
			pExpnXPD->pwr_t4[0] = pCalCh->pwr1_xg0;
			pExpnXPD->pwr_t4[1] = pCalCh->pwr2_xg0;
			pExpnXPD->pwr_t4[2] = pCalCh->pwr3_xg0;
			pExpnXPD->pwr_t4[3] = pCalCh->pwr4_xg0;

			jj = xgainList[1];
			pExpnXPD = &pPowerExpn->pDataPerChannel[ii].pDataPerXPD[jj];
			pExpnXPD->numPcdacs = 3;

			pExpnXPD->pwr_t4[0] = pCalCh->pwr1_xg3;
			pExpnXPD->pwr_t4[1] = pCalCh->pwr2_xg3;
			pExpnXPD->pwr_t4[2] = pCalCh->pwr3_xg3;
		}
	}
	return AH_TRUE;
}

static HAL_BOOL
readEepromRawPowerCalInfo5112(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	const uint16_t dbmmask		 = 0xff;
	const uint16_t pcdac_delta_mask = 0x1f;
	const uint16_t pcdac_mask	 = 0x3f;
	const uint16_t freqmask	 = 0xff;

	int i, mode, numPiers;
	uint32_t off;
	uint16_t eeval;
	uint16_t freq[NUM_11A_EEPROM_CHANNELS];
        EEPROM_POWER_5112 eePower;

	HALASSERT(ee->ee_version >= AR_EEPROM_VER4_0);
	off = GROUPS_OFFSET3_3;
	for (mode = headerInfo11A; mode <= headerInfo11G; mode++) {
		numPiers = 0;
		switch (mode) {
		case headerInfo11A:
			if (!ee->ee_Amode)	/* no 11a calibration data */
				continue;
			while (numPiers < NUM_11A_EEPROM_CHANNELS) {
				EEREAD(off++);
				if ((eeval & freqmask) == 0)
					break;
				freq[numPiers++] = fbin2freq(ee,
					eeval & freqmask);

				if (((eeval >> 8) & freqmask) == 0)
					break;
				freq[numPiers++] = fbin2freq(ee,
					(eeval>>8) & freqmask);
			}
			break;
		case headerInfo11B:
			if (!ee->ee_Bmode)	/* no 11b calibration data */
				continue;
			for (i = 0; i < NUM_2_4_EEPROM_CHANNELS; i++)
				if (ee->ee_calPier11b[i] != CHANNEL_UNUSED)
					freq[numPiers++] = ee->ee_calPier11b[i];
			break;
		case headerInfo11G:
			if (!ee->ee_Gmode)	/* no 11g calibration data */
				continue;
			for (i = 0; i < NUM_2_4_EEPROM_CHANNELS; i++)
				if (ee->ee_calPier11g[i] != CHANNEL_UNUSED)
					freq[numPiers++] = ee->ee_calPier11g[i];
			break;
		default:
			HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid mode 0x%x\n",
			    __func__, mode);
			return AH_FALSE;
		}

		OS_MEMZERO(&eePower, sizeof(eePower));
		eePower.numChannels = numPiers;

		for (i = 0; i < numPiers; i++) {
			eePower.pChannels[i] = freq[i];
			eePower.pDataPerChannel[i].channelValue = freq[i];

			EEREAD(off++);
			eePower.pDataPerChannel[i].pwr1_xg0 = (int16_t)
				((eeval & dbmmask) - ((eeval >> 7) & 0x1)*256);
			eePower.pDataPerChannel[i].pwr2_xg0 = (int16_t)
				(((eeval >> 8) & dbmmask) - ((eeval >> 15) & 0x1)*256);

			EEREAD(off++);
			eePower.pDataPerChannel[i].pwr3_xg0 = (int16_t)
				((eeval & dbmmask) - ((eeval >> 7) & 0x1)*256);
			eePower.pDataPerChannel[i].pwr4_xg0 = (int16_t)
				(((eeval >> 8) & dbmmask) - ((eeval >> 15) & 0x1)*256);

			EEREAD(off++);
			eePower.pDataPerChannel[i].pcd2_delta_xg0 = (uint16_t)
				(eeval & pcdac_delta_mask);
			eePower.pDataPerChannel[i].pcd3_delta_xg0 = (uint16_t)
				((eeval >> 5) & pcdac_delta_mask);
			eePower.pDataPerChannel[i].pcd4_delta_xg0 = (uint16_t)
				((eeval >> 10) & pcdac_delta_mask);

			EEREAD(off++);
			eePower.pDataPerChannel[i].pwr1_xg3 = (int16_t)
				((eeval & dbmmask) - ((eeval >> 7) & 0x1)*256);
			eePower.pDataPerChannel[i].pwr2_xg3 = (int16_t)
				(((eeval >> 8) & dbmmask) - ((eeval >> 15) & 0x1)*256);

			EEREAD(off++);
			eePower.pDataPerChannel[i].pwr3_xg3 = (int16_t)
				((eeval & dbmmask) - ((eeval >> 7) & 0x1)*256);
			if (ee->ee_version >= AR_EEPROM_VER4_3) {
				eePower.pDataPerChannel[i].maxPower_t4 =
					eePower.pDataPerChannel[i].pwr4_xg0;     
				eePower.pDataPerChannel[i].pcd1_xg0 = (uint16_t)
					((eeval >> 8) & pcdac_mask);
			} else {
				eePower.pDataPerChannel[i].maxPower_t4 = (int16_t)
					(((eeval >> 8) & dbmmask) -
					 ((eeval >> 15) & 0x1)*256);
				eePower.pDataPerChannel[i].pcd1_xg0 = 1;
			}
		}
		eePower.xpdMask = ee->ee_xgain[mode];

		if (!eepromAllocExpnPower5112(ah, &eePower, &ee->ee_modePowerArray5112[mode])) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: did not allocate power struct\n", __func__);
			return AH_FALSE;
                }
                if (!eepromExpandPower5112(ah, &eePower, &ee->ee_modePowerArray5112[mode])) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: did not expand power struct\n", __func__);
			return AH_FALSE;
		}
	}
	return AH_TRUE;
#undef EEREAD
}

static void
freeEepromRawPowerCalInfo5112(struct ath_hal *ah, HAL_EEPROM *ee)
{
	int mode;
	void *data;

	for (mode = headerInfo11A; mode <= headerInfo11G; mode++) {
		EEPROM_POWER_EXPN_5112 *pPowerExpn =
			&ee->ee_modePowerArray5112[mode];
		data = pPowerExpn->pChannels;
		if (data != AH_NULL) {
			pPowerExpn->pChannels = AH_NULL;
			ath_hal_free(data);
		}
	}
}

static void
ar2413SetupEEPROMDataset(EEPROM_DATA_STRUCT_2413 *pEEPROMDataset2413,
	uint16_t myNumRawChannels, uint16_t *pMyRawChanList)
{
	uint16_t i, channelValue;
	uint32_t xpd_mask;
	uint16_t numPdGainsUsed;

	pEEPROMDataset2413->numChannels = myNumRawChannels;

	xpd_mask = pEEPROMDataset2413->xpd_mask;
	numPdGainsUsed = 0;
	if ((xpd_mask >> 0) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 1) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 2) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 3) & 0x1) numPdGainsUsed++;

	for (i = 0; i < myNumRawChannels; i++) {
		channelValue = pMyRawChanList[i];
		pEEPROMDataset2413->pChannels[i] = channelValue;
		pEEPROMDataset2413->pDataPerChannel[i].channelValue = channelValue;
		pEEPROMDataset2413->pDataPerChannel[i].numPdGains = numPdGainsUsed;
	}
}

static HAL_BOOL
ar2413ReadCalDataset(struct ath_hal *ah, HAL_EEPROM *ee,
	EEPROM_DATA_STRUCT_2413 *pCalDataset,
	uint32_t start_offset, uint32_t maxPiers, uint8_t mode)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	const uint16_t dbm_I_mask = 0x1F;	/* 5-bits. 1dB step. */
	const uint16_t dbm_delta_mask = 0xF;	/* 4-bits. 0.5dB step. */
	const uint16_t Vpd_I_mask = 0x7F;	/* 7-bits. 0-128 */
	const uint16_t Vpd_delta_mask = 0x3F;	/* 6-bits. 0-63 */
	const uint16_t freqmask = 0xff;

	uint16_t ii, eeval;
	uint16_t idx, numPiers;
	uint16_t freq[NUM_11A_EEPROM_CHANNELS];

	idx = start_offset;
    for (numPiers = 0; numPiers < maxPiers;) {
        EEREAD(idx++);
        if ((eeval & freqmask) == 0)
            break;
        if (mode == headerInfo11A)
            freq[numPiers++] = fbin2freq(ee, (eeval & freqmask));
        else
            freq[numPiers++] = fbin2freq_2p4(ee, (eeval & freqmask));
                                                                                          
        if (((eeval >> 8) & freqmask) == 0)
            break;
        if (mode == headerInfo11A)
            freq[numPiers++] = fbin2freq(ee, (eeval >> 8) & freqmask);
        else
            freq[numPiers++] = fbin2freq_2p4(ee, (eeval >> 8) & freqmask);
    }
	ar2413SetupEEPROMDataset(pCalDataset, numPiers, &freq[0]);

	idx = start_offset + (maxPiers / 2);
	for (ii = 0; ii < pCalDataset->numChannels; ii++) {
		EEPROM_DATA_PER_CHANNEL_2413 *currCh =
			&(pCalDataset->pDataPerChannel[ii]);

		if (currCh->numPdGains > 0) {
			/*
			 * Read the first NUM_POINTS_OTHER_PDGAINS pwr
			 * and Vpd values for pdgain_0
			 */
			EEREAD(idx++);
			currCh->pwr_I[0] = eeval & dbm_I_mask;
			currCh->Vpd_I[0] = (eeval >> 5) & Vpd_I_mask;
			currCh->pwr_delta_t2[0][0] =
				(eeval >> 12) & dbm_delta_mask;
			
			EEREAD(idx++);
			currCh->Vpd_delta[0][0] = eeval & Vpd_delta_mask;
			currCh->pwr_delta_t2[1][0] =
				(eeval >> 6) & dbm_delta_mask;
			currCh->Vpd_delta[1][0] =
				(eeval >> 10) & Vpd_delta_mask;
			
			EEREAD(idx++);
			currCh->pwr_delta_t2[2][0] = eeval & dbm_delta_mask;
			currCh->Vpd_delta[2][0] = (eeval >> 4) & Vpd_delta_mask;
		}
		
		if (currCh->numPdGains > 1) {
			/*
			 * Read the first NUM_POINTS_OTHER_PDGAINS pwr
			 * and Vpd values for pdgain_1
			 */
			currCh->pwr_I[1] = (eeval >> 10) & dbm_I_mask;
			currCh->Vpd_I[1] = (eeval >> 15) & 0x1;
			
			EEREAD(idx++);
			/* upper 6 bits */
			currCh->Vpd_I[1] |= (eeval & 0x3F) << 1;
			currCh->pwr_delta_t2[0][1] =
				(eeval >> 6) & dbm_delta_mask;
			currCh->Vpd_delta[0][1] =
				(eeval >> 10) & Vpd_delta_mask;
			
			EEREAD(idx++);
			currCh->pwr_delta_t2[1][1] = eeval & dbm_delta_mask;
			currCh->Vpd_delta[1][1] = (eeval >> 4) & Vpd_delta_mask;
			currCh->pwr_delta_t2[2][1] =
				(eeval >> 10) & dbm_delta_mask;
			currCh->Vpd_delta[2][1] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 4 bits */
			currCh->Vpd_delta[2][1] |= (eeval & 0xF) << 2;
		} else if (currCh->numPdGains == 1) {
			/*
			 * Read the last pwr and Vpd values for pdgain_0
			 */
			currCh->pwr_delta_t2[3][0] =
				(eeval >> 10) & dbm_delta_mask;
			currCh->Vpd_delta[3][0] = (eeval >> 14) & 0x3;

			EEREAD(idx++);
			/* upper 4 bits */
			currCh->Vpd_delta[3][0] |= (eeval & 0xF) << 2;

			/* 4 words if numPdGains == 1 */
		}

		if (currCh->numPdGains > 2) {
			/*
			 * Read the first NUM_POINTS_OTHER_PDGAINS pwr
			 * and Vpd values for pdgain_2
			 */
			currCh->pwr_I[2] = (eeval >> 4) & dbm_I_mask;
			currCh->Vpd_I[2] = (eeval >> 9) & Vpd_I_mask;
			
			EEREAD(idx++);
			currCh->pwr_delta_t2[0][2] =
				(eeval >> 0) & dbm_delta_mask;
			currCh->Vpd_delta[0][2] = (eeval >> 4) & Vpd_delta_mask;
			currCh->pwr_delta_t2[1][2] =
				(eeval >> 10) & dbm_delta_mask;
			currCh->Vpd_delta[1][2] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 4 bits */
			currCh->Vpd_delta[1][2] |= (eeval & 0xF) << 2;
			currCh->pwr_delta_t2[2][2] =
				(eeval >> 4) & dbm_delta_mask;
			currCh->Vpd_delta[2][2] = (eeval >> 8) & Vpd_delta_mask;
		} else if (currCh->numPdGains == 2) {
			/*
			 * Read the last pwr and Vpd values for pdgain_1
			 */
			currCh->pwr_delta_t2[3][1] =
				(eeval >> 4) & dbm_delta_mask;
			currCh->Vpd_delta[3][1] = (eeval >> 8) & Vpd_delta_mask;

			/* 6 words if numPdGains == 2 */
		}

		if (currCh->numPdGains > 3) {
			/*
			 * Read the first NUM_POINTS_OTHER_PDGAINS pwr
			 * and Vpd values for pdgain_3
			 */
			currCh->pwr_I[3] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 3 bits */
			currCh->pwr_I[3] |= ((eeval >> 0) & 0x7) << 2;
			currCh->Vpd_I[3] = (eeval >> 3) & Vpd_I_mask;
			currCh->pwr_delta_t2[0][3] =
				(eeval >> 10) & dbm_delta_mask;
			currCh->Vpd_delta[0][3] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 4 bits */
			currCh->Vpd_delta[0][3] |= (eeval & 0xF) << 2;
			currCh->pwr_delta_t2[1][3] =
				(eeval >> 4) & dbm_delta_mask;
			currCh->Vpd_delta[1][3] = (eeval >> 8) & Vpd_delta_mask;
			currCh->pwr_delta_t2[2][3] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 2 bits */
			currCh->pwr_delta_t2[2][3] |= ((eeval >> 0) & 0x3) << 2;
			currCh->Vpd_delta[2][3] = (eeval >> 2) & Vpd_delta_mask;
			currCh->pwr_delta_t2[3][3] =
				(eeval >> 8) & dbm_delta_mask;
			currCh->Vpd_delta[3][3] = (eeval >> 12) & 0xF;
			
			EEREAD(idx++);
			/* upper 2 bits */
			currCh->Vpd_delta[3][3] |= ((eeval >> 0) & 0x3) << 4;

			/* 12 words if numPdGains == 4 */
		} else if (currCh->numPdGains == 3) {
			/* read the last pwr and Vpd values for pdgain_2 */
			currCh->pwr_delta_t2[3][2] = (eeval >> 14) & 0x3;
			
			EEREAD(idx++);
			/* upper 2 bits */
			currCh->pwr_delta_t2[3][2] |= ((eeval >> 0) & 0x3) << 2;
			currCh->Vpd_delta[3][2] = (eeval >> 2) & Vpd_delta_mask;

			/* 9 words if numPdGains == 3 */
		}
	}
	return AH_TRUE;
#undef EEREAD
}

static void
ar2413SetupRawDataset(RAW_DATA_STRUCT_2413 *pRaw, EEPROM_DATA_STRUCT_2413 *pCal)
{
	uint16_t i, j, kk, channelValue;
	uint16_t xpd_mask;
	uint16_t numPdGainsUsed;

	pRaw->numChannels = pCal->numChannels;

	xpd_mask = pRaw->xpd_mask;
	numPdGainsUsed = 0;
	if ((xpd_mask >> 0) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 1) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 2) & 0x1) numPdGainsUsed++;
	if ((xpd_mask >> 3) & 0x1) numPdGainsUsed++;

	for (i = 0; i < pCal->numChannels; i++) {
		channelValue = pCal->pChannels[i];

		pRaw->pChannels[i] = channelValue;

		pRaw->pDataPerChannel[i].channelValue = channelValue;
		pRaw->pDataPerChannel[i].numPdGains = numPdGainsUsed;

		kk = 0;
		for (j = 0; j < MAX_NUM_PDGAINS_PER_CHANNEL; j++) {
			pRaw->pDataPerChannel[i].pDataPerPDGain[j].pd_gain = j;
			if ((xpd_mask >> j) & 0x1) {
				pRaw->pDataPerChannel[i].pDataPerPDGain[j].numVpd = NUM_POINTS_OTHER_PDGAINS;
				kk++;
				if (kk == 1) {
					/* 
					 * lowest pd_gain corresponds
					 *  to highest power and thus,
					 *  has one more point
					 */
					pRaw->pDataPerChannel[i].pDataPerPDGain[j].numVpd = NUM_POINTS_LAST_PDGAIN;
				}
			} else {
				pRaw->pDataPerChannel[i].pDataPerPDGain[j].numVpd = 0;
			}
		}
	}
}

static HAL_BOOL
ar2413EepromToRawDataset(struct ath_hal *ah,
	EEPROM_DATA_STRUCT_2413 *pCal, RAW_DATA_STRUCT_2413 *pRaw)
{
	uint16_t ii, jj, kk, ss;
	RAW_DATA_PER_PDGAIN_2413 *pRawXPD;
	/* ptr to array of info held per channel */
	EEPROM_DATA_PER_CHANNEL_2413 *pCalCh;
	uint16_t xgain_list[MAX_NUM_PDGAINS_PER_CHANNEL];
	uint16_t xpd_mask;
	uint32_t numPdGainsUsed;

	HALASSERT(pRaw->xpd_mask == pCal->xpd_mask);

	xgain_list[0] = 0xDEAD;
	xgain_list[1] = 0xDEAD;
	xgain_list[2] = 0xDEAD;
	xgain_list[3] = 0xDEAD;

	numPdGainsUsed = 0;
	xpd_mask = pRaw->xpd_mask;
	for (jj = 0; jj < MAX_NUM_PDGAINS_PER_CHANNEL; jj++) {
		if ((xpd_mask >> (MAX_NUM_PDGAINS_PER_CHANNEL-jj-1)) & 1)
			xgain_list[numPdGainsUsed++] = MAX_NUM_PDGAINS_PER_CHANNEL-jj-1;
	}

	pRaw->numChannels = pCal->numChannels;
	for (ii = 0; ii < pRaw->numChannels; ii++) {
		pCalCh = &(pCal->pDataPerChannel[ii]);
		pRaw->pDataPerChannel[ii].channelValue = pCalCh->channelValue;

		/* numVpd has already been setup appropriately for the relevant pdGains */
		for (jj = 0; jj < numPdGainsUsed; jj++) {
			/* use jj for calDataset and ss for rawDataset */
			ss = xgain_list[jj];
			pRawXPD = &(pRaw->pDataPerChannel[ii].pDataPerPDGain[ss]);
			HALASSERT(pRawXPD->numVpd >= 1);

			pRawXPD->pwr_t4[0] = (uint16_t)(4*pCalCh->pwr_I[jj]);
			pRawXPD->Vpd[0]    = pCalCh->Vpd_I[jj];

			for (kk = 1; kk < pRawXPD->numVpd; kk++) {
				pRawXPD->pwr_t4[kk] = (int16_t)(pRawXPD->pwr_t4[kk-1] + 2*pCalCh->pwr_delta_t2[kk-1][jj]);
				pRawXPD->Vpd[kk] = (uint16_t)(pRawXPD->Vpd[kk-1] + pCalCh->Vpd_delta[kk-1][jj]);
			}
			/* loop over Vpds */
		}
		/* loop over pd_gains */
	}
	/* loop over channels */
	return AH_TRUE;
}

static HAL_BOOL
readEepromRawPowerCalInfo2413(struct ath_hal *ah, HAL_EEPROM *ee)
{
	/* NB: index is 1 less than numPdgains */
	static const uint16_t wordsForPdgains[] = { 4, 6, 9, 12 };
	EEPROM_DATA_STRUCT_2413 *pCal = AH_NULL;
	RAW_DATA_STRUCT_2413 *pRaw;
	int numEEPROMWordsPerChannel;
	uint32_t off;
	HAL_BOOL ret = AH_FALSE;
	
	HALASSERT(ee->ee_version >= AR_EEPROM_VER5_0);
	HALASSERT(ee->ee_eepMap == 2);
	
	pCal = ath_hal_malloc(sizeof(EEPROM_DATA_STRUCT_2413));
	if (pCal == AH_NULL)
		goto exit;
	
	off = ee->ee_eepMap2PowerCalStart;
	if (ee->ee_Amode) {
		OS_MEMZERO(pCal, sizeof(EEPROM_DATA_STRUCT_2413));
		pCal->xpd_mask = ee->ee_xgain[headerInfo11A];
		if (!ar2413ReadCalDataset(ah, ee, pCal, off,
			NUM_11A_EEPROM_CHANNELS_2413, headerInfo11A)) {
			goto exit;
		}
		pRaw = &ee->ee_rawDataset2413[headerInfo11A];
		pRaw->xpd_mask = ee->ee_xgain[headerInfo11A];
		ar2413SetupRawDataset(pRaw, pCal);
		if (!ar2413EepromToRawDataset(ah, pCal, pRaw)) {
			goto exit;
		}
		/* setup offsets for mode_11a next */
		numEEPROMWordsPerChannel = wordsForPdgains[
			pCal->pDataPerChannel[0].numPdGains - 1];
		off += pCal->numChannels * numEEPROMWordsPerChannel + 5;
	}
	if (ee->ee_Bmode) {
		OS_MEMZERO(pCal, sizeof(EEPROM_DATA_STRUCT_2413));
		pCal->xpd_mask = ee->ee_xgain[headerInfo11B];
		if (!ar2413ReadCalDataset(ah, ee, pCal, off,
			NUM_2_4_EEPROM_CHANNELS_2413 , headerInfo11B)) {
			goto exit;
		}
		pRaw = &ee->ee_rawDataset2413[headerInfo11B];
		pRaw->xpd_mask = ee->ee_xgain[headerInfo11B];
		ar2413SetupRawDataset(pRaw, pCal);
		if (!ar2413EepromToRawDataset(ah, pCal, pRaw)) {
			goto exit;
		}
		/* setup offsets for mode_11g next */
		numEEPROMWordsPerChannel = wordsForPdgains[
			pCal->pDataPerChannel[0].numPdGains - 1];
		off += pCal->numChannels * numEEPROMWordsPerChannel + 2;
	}
	if (ee->ee_Gmode) {
		OS_MEMZERO(pCal, sizeof(EEPROM_DATA_STRUCT_2413));
		pCal->xpd_mask = ee->ee_xgain[headerInfo11G];
		if (!ar2413ReadCalDataset(ah, ee, pCal, off,
			NUM_2_4_EEPROM_CHANNELS_2413, headerInfo11G)) {
			goto exit;
		}
		pRaw = &ee->ee_rawDataset2413[headerInfo11G];
		pRaw->xpd_mask = ee->ee_xgain[headerInfo11G];
		ar2413SetupRawDataset(pRaw, pCal);
		if (!ar2413EepromToRawDataset(ah, pCal, pRaw)) {
			goto exit;
		}
	}
	ret = AH_TRUE;
 exit:
	if (pCal != AH_NULL)
		ath_hal_free(pCal);
	return ret;
}

/*
 * Now copy EEPROM Raw Power Calibration per frequency contents 
 * into the allocated space
 */
static HAL_BOOL
readEepromRawPowerCalInfo(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	uint16_t eeval, nchan;
	uint32_t off;
	int i, j, mode;

        if (ee->ee_version >= AR_EEPROM_VER4_0 && ee->ee_eepMap == 1)
		return readEepromRawPowerCalInfo5112(ah, ee);
	if (ee->ee_version >= AR_EEPROM_VER5_0 && ee->ee_eepMap == 2)
		return readEepromRawPowerCalInfo2413(ah, ee);

	/*
	 * Group 2:  read raw power data for all frequency piers
	 *
	 * NOTE: Group 2 contains the raw power calibration
	 *	 information for each of the channels that
	 *	 we recorded above.
	 */
	for (mode = headerInfo11A; mode <= headerInfo11G; mode++) {
		uint16_t *pChannels = AH_NULL;
		DATA_PER_CHANNEL *pChannelData = AH_NULL;

		off = ee->ee_version >= AR_EEPROM_VER3_3 ? 
			GROUPS_OFFSET3_3 : GROUPS_OFFSET3_2;
		switch (mode) {
		case headerInfo11A:
			off      	+= GROUP2_OFFSET;
			nchan		= ee->ee_numChannels11a;
			pChannelData	= ee->ee_dataPerChannel11a;
			pChannels	= ee->ee_channels11a;
			break;
		case headerInfo11B:
			if (!ee->ee_Bmode)
				continue;
			off		+= GROUP3_OFFSET;
			nchan		= ee->ee_numChannels2_4;
			pChannelData	= ee->ee_dataPerChannel11b;
			pChannels	= ee->ee_channels11b;
			break;
		case headerInfo11G:
			if (!ee->ee_Gmode)
				continue;
			off		+= GROUP4_OFFSET;
			nchan		= ee->ee_numChannels2_4;
			pChannelData	= ee->ee_dataPerChannel11g;
			pChannels	= ee->ee_channels11g;
			break;
		default:
			HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid mode 0x%x\n",
			    __func__, mode);
			return AH_FALSE;
		}
		for (i = 0; i < nchan; i++) {
			pChannelData->channelValue = pChannels[i];

			EEREAD(off++);
			pChannelData->pcdacMax     = (uint16_t)((eeval >> 10) & PCDAC_MASK);
			pChannelData->pcdacMin     = (uint16_t)((eeval >> 4) & PCDAC_MASK);
			pChannelData->PwrValues[0] = (uint16_t)((eeval << 2) & POWER_MASK);

			EEREAD(off++);
			pChannelData->PwrValues[0] |= (uint16_t)((eeval >> 14) & 0x3);
			pChannelData->PwrValues[1] = (uint16_t)((eeval >> 8) & POWER_MASK);
			pChannelData->PwrValues[2] = (uint16_t)((eeval >> 2) & POWER_MASK);
			pChannelData->PwrValues[3] = (uint16_t)((eeval << 4) & POWER_MASK);

			EEREAD(off++);
			pChannelData->PwrValues[3] |= (uint16_t)((eeval >> 12) & 0xf);
			pChannelData->PwrValues[4] = (uint16_t)((eeval >> 6) & POWER_MASK);
			pChannelData->PwrValues[5] = (uint16_t)(eeval  & POWER_MASK);

			EEREAD(off++);
			pChannelData->PwrValues[6] = (uint16_t)((eeval >> 10) & POWER_MASK);
			pChannelData->PwrValues[7] = (uint16_t)((eeval >> 4) & POWER_MASK);
			pChannelData->PwrValues[8] = (uint16_t)((eeval << 2) & POWER_MASK);

			EEREAD(off++);
			pChannelData->PwrValues[8] |= (uint16_t)((eeval >> 14) & 0x3);
			pChannelData->PwrValues[9] = (uint16_t)((eeval >> 8) & POWER_MASK);
			pChannelData->PwrValues[10] = (uint16_t)((eeval >> 2) & POWER_MASK);

			getPcdacInterceptsFromPcdacMinMax(ee,
				pChannelData->pcdacMin, pChannelData->pcdacMax,
				pChannelData->PcdacValues) ;

			for (j = 0; j < pChannelData->numPcdacValues; j++) {
				pChannelData->PwrValues[j] = (uint16_t)(
					PWR_STEP * pChannelData->PwrValues[j]);
				/* Note these values are scaled up. */
			}
			pChannelData++;
		}
	}
	return AH_TRUE;
#undef EEREAD
}

/*
 * Copy EEPROM Target Power Calbration per rate contents 
 * into the allocated space
 */
static HAL_BOOL
readEepromTargetPowerCalInfo(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	uint16_t eeval, enable24;
	uint32_t off;
	int i, mode, nchan;

	enable24 = ee->ee_Bmode || ee->ee_Gmode;
	for (mode = headerInfo11A; mode <= headerInfo11G; mode++) {
		TRGT_POWER_INFO *pPowerInfo;
		uint16_t *pNumTrgtChannels;

		off = ee->ee_version >= AR_EEPROM_VER4_0 ?
				ee->ee_targetPowersStart - GROUP5_OFFSET :
		      ee->ee_version >= AR_EEPROM_VER3_3 ?
				GROUPS_OFFSET3_3 : GROUPS_OFFSET3_2;
		switch (mode) {
		case headerInfo11A:
			off += GROUP5_OFFSET;
			nchan = NUM_TEST_FREQUENCIES;
			pPowerInfo = ee->ee_trgtPwr_11a;
			pNumTrgtChannels = &ee->ee_numTargetPwr_11a;
			break;
		case headerInfo11B:
			if (!enable24)
				continue;
			off += GROUP6_OFFSET;
			nchan = 2;
			pPowerInfo = ee->ee_trgtPwr_11b;
			pNumTrgtChannels = &ee->ee_numTargetPwr_11b;
			break;
		case headerInfo11G:
			if (!enable24)
				continue;
			off += GROUP7_OFFSET;
			nchan = 3;
			pPowerInfo = ee->ee_trgtPwr_11g;
			pNumTrgtChannels = &ee->ee_numTargetPwr_11g;
			break;
		default:
			HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid mode 0x%x\n",
			    __func__, mode);
			return AH_FALSE;
		}
		*pNumTrgtChannels = 0;
		for (i = 0; i < nchan; i++) {
			EEREAD(off++);
			if (ee->ee_version >= AR_EEPROM_VER3_3) {
				pPowerInfo->testChannel = (eeval >> 8) & 0xff;
			} else {
				pPowerInfo->testChannel = (eeval >> 9) & 0x7f;
			}

			if (pPowerInfo->testChannel != 0) {
				/* get the channel value and read rest of info */
				if (mode == headerInfo11A) {
					pPowerInfo->testChannel = fbin2freq(ee, pPowerInfo->testChannel);
				} else {
					pPowerInfo->testChannel = fbin2freq_2p4(ee, pPowerInfo->testChannel);
				}

				if (ee->ee_version >= AR_EEPROM_VER3_3) {
					pPowerInfo->twicePwr6_24 = (eeval >> 2) & POWER_MASK;
					pPowerInfo->twicePwr36   = (eeval << 4) & POWER_MASK;
				} else {
					pPowerInfo->twicePwr6_24 = (eeval >> 3) & POWER_MASK;
					pPowerInfo->twicePwr36   = (eeval << 3) & POWER_MASK;
				}

				EEREAD(off++);
				if (ee->ee_version >= AR_EEPROM_VER3_3) {
					pPowerInfo->twicePwr36 |= (eeval >> 12) & 0xf;
					pPowerInfo->twicePwr48 = (eeval >> 6) & POWER_MASK;
					pPowerInfo->twicePwr54 =  eeval & POWER_MASK;
				} else {
					pPowerInfo->twicePwr36 |= (eeval >> 13) & 0x7;
					pPowerInfo->twicePwr48 = (eeval >> 7) & POWER_MASK;
					pPowerInfo->twicePwr54 = (eeval >> 1) & POWER_MASK;
				}
				(*pNumTrgtChannels)++;
			}
			pPowerInfo++;
		}
	}
	return AH_TRUE;
#undef EEREAD
}

/*
 * Now copy EEPROM Coformance Testing Limits contents 
 * into the allocated space
 */
static HAL_BOOL
readEepromCTLInfo(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	RD_EDGES_POWER *rep;
	uint16_t eeval;
	uint32_t off;
	int i, j;

	rep = ee->ee_rdEdgesPower;

	off = GROUP8_OFFSET +
		(ee->ee_version >= AR_EEPROM_VER4_0 ?
			ee->ee_targetPowersStart - GROUP5_OFFSET :
	         ee->ee_version >= AR_EEPROM_VER3_3 ?
			GROUPS_OFFSET3_3 : GROUPS_OFFSET3_2);
	for (i = 0; i < ee->ee_numCtls; i++) {
		if (ee->ee_ctl[i] == 0) {
			/* Move offset and edges */
			off += (ee->ee_version >= AR_EEPROM_VER3_3 ? 8 : 7);
			rep += NUM_EDGES;
			continue;
		}
		if (ee->ee_version >= AR_EEPROM_VER3_3) {
			for (j = 0; j < NUM_EDGES; j += 2) {
				EEREAD(off++);
				rep[j].rdEdge = (eeval >> 8) & FREQ_MASK_3_3;
				rep[j+1].rdEdge = eeval & FREQ_MASK_3_3;
			}
			for (j = 0; j < NUM_EDGES; j += 2) {
				EEREAD(off++);
				rep[j].twice_rdEdgePower = 
					(eeval >> 8) & POWER_MASK;
				rep[j].flag = (eeval >> 14) & 1;
				rep[j+1].twice_rdEdgePower = eeval & POWER_MASK;
				rep[j+1].flag = (eeval >> 6) & 1;
			}
		} else { 
			EEREAD(off++);
			rep[0].rdEdge = (eeval >> 9) & FREQ_MASK;
			rep[1].rdEdge = (eeval >> 2) & FREQ_MASK;
			rep[2].rdEdge = (eeval << 5) & FREQ_MASK;

			EEREAD(off++);
			rep[2].rdEdge |= (eeval >> 11) & 0x1f;
			rep[3].rdEdge = (eeval >> 4) & FREQ_MASK;
			rep[4].rdEdge = (eeval << 3) & FREQ_MASK;

			EEREAD(off++);
			rep[4].rdEdge |= (eeval >> 13) & 0x7;
			rep[5].rdEdge = (eeval >> 6) & FREQ_MASK;
			rep[6].rdEdge = (eeval << 1) & FREQ_MASK;

			EEREAD(off++);
			rep[6].rdEdge |= (eeval >> 15) & 0x1;
			rep[7].rdEdge = (eeval >> 8) & FREQ_MASK;

			rep[0].twice_rdEdgePower = (eeval >> 2) & POWER_MASK;
			rep[1].twice_rdEdgePower = (eeval << 4) & POWER_MASK;

			EEREAD(off++);
			rep[1].twice_rdEdgePower |= (eeval >> 12) & 0xf;
			rep[2].twice_rdEdgePower = (eeval >> 6) & POWER_MASK;
			rep[3].twice_rdEdgePower = eeval & POWER_MASK;

			EEREAD(off++);
			rep[4].twice_rdEdgePower = (eeval >> 10) & POWER_MASK;
			rep[5].twice_rdEdgePower = (eeval >> 4) & POWER_MASK;
			rep[6].twice_rdEdgePower = (eeval << 2) & POWER_MASK;

			EEREAD(off++);
			rep[6].twice_rdEdgePower |= (eeval >> 14) & 0x3;
			rep[7].twice_rdEdgePower = (eeval >> 8) & POWER_MASK;
		}

		for (j = 0; j < NUM_EDGES; j++ ) {
			if (rep[j].rdEdge != 0 || rep[j].twice_rdEdgePower != 0) {
				if ((ee->ee_ctl[i] & CTL_MODE_M) == CTL_11A ||
				    (ee->ee_ctl[i] & CTL_MODE_M) == CTL_TURBO) {
					rep[j].rdEdge = fbin2freq(ee, rep[j].rdEdge);
				} else {
					rep[j].rdEdge = fbin2freq_2p4(ee, rep[j].rdEdge);
				}
			}
		}
		rep += NUM_EDGES;
	}
	return AH_TRUE;
#undef EEREAD
}

/*
 * Read the individual header fields for a Rev 3 EEPROM
 */
static HAL_BOOL
readHeaderInfo(struct ath_hal *ah, HAL_EEPROM *ee)
{
#define	EEREAD(_off) do {				\
	if (!ath_hal_eepromRead(ah, _off, &eeval))	\
		return AH_FALSE;			\
} while (0)
	static const uint32_t headerOffset3_0[] = {
		0x00C2, /* 0 - Mode bits, device type, max turbo power */
		0x00C4, /* 1 - 2.4 and 5 antenna gain */
		0x00C5, /* 2 - Begin 11A modal section */
		0x00D0, /* 3 - Begin 11B modal section */
		0x00DA, /* 4 - Begin 11G modal section */
		0x00E4  /* 5 - Begin CTL section */
	};
	static const uint32_t headerOffset3_3[] = {
		0x00C2, /* 0 - Mode bits, device type, max turbo power */
		0x00C3, /* 1 - 2.4 and 5 antenna gain */
		0x00D4, /* 2 - Begin 11A modal section */
		0x00F2, /* 3 - Begin 11B modal section */
		0x010D, /* 4 - Begin 11G modal section */
		0x0128  /* 5 - Begin CTL section */
	};

	static const uint32_t regCapOffsetPre4_0 = 0x00CF;
	static const uint32_t regCapOffsetPost4_0 = 0x00CA; 

	const uint32_t *header;
	uint32_t off;
	uint16_t eeval;
	int i;

	/* initialize cckOfdmGainDelta for < 4.2 eeprom */
	ee->ee_cckOfdmGainDelta = CCK_OFDM_GAIN_DELTA;
	ee->ee_scaledCh14FilterCckDelta = TENX_CH14_FILTER_CCK_DELTA_INIT;

	if (ee->ee_version >= AR_EEPROM_VER3_3) {
		header = headerOffset3_3;
		ee->ee_numCtls = NUM_CTLS_3_3;
	} else {
		header = headerOffset3_0;
		ee->ee_numCtls = NUM_CTLS;
	}
	HALASSERT(ee->ee_numCtls <= NUM_CTLS_MAX);

	EEREAD(header[0]);
	ee->ee_turbo5Disable	= (eeval >> 15) & 0x01;
	ee->ee_rfKill		= (eeval >> 14) & 0x01;
	ee->ee_deviceType	= (eeval >> 11) & 0x07;
	ee->ee_turbo2WMaxPower5	= (eeval >> 4) & 0x7F;
	if (ee->ee_version >= AR_EEPROM_VER4_0)
		ee->ee_turbo2Disable	= (eeval >> 3) & 0x01;
	else
		ee->ee_turbo2Disable	= 1;
	ee->ee_Gmode		= (eeval >> 2) & 0x01;
	ee->ee_Bmode		= (eeval >> 1) & 0x01;
	ee->ee_Amode		= (eeval & 0x01);

	off = header[1];
	EEREAD(off++);
	ee->ee_antennaGainMax[0] = (int8_t)((eeval >> 8) & 0xFF);
	ee->ee_antennaGainMax[1] = (int8_t)(eeval & 0xFF);
	if (ee->ee_version >= AR_EEPROM_VER4_0) {
		EEREAD(off++);
		ee->ee_eepMap		 = (eeval>>14) & 0x3;
		ee->ee_disableXr5	 = (eeval>>13) & 0x1;
		ee->ee_disableXr2	 = (eeval>>12) & 0x1;
		ee->ee_earStart		 = eeval & 0xfff;

		EEREAD(off++);
		ee->ee_targetPowersStart = eeval & 0xfff;
		ee->ee_exist32kHzCrystal = (eeval>>14) & 0x1;

		if (ee->ee_version >= AR_EEPROM_VER5_0) {
			off += 2;
			EEREAD(off);
			ee->ee_eepMap2PowerCalStart = (eeval >> 4) & 0xfff;
			/* Properly cal'ed 5.0 devices should be non-zero */
		}
	}

	/* Read the moded sections of the EEPROM header in the order A, B, G */
	for (i = headerInfo11A; i <= headerInfo11G; i++) {
		/* Set the offset via the index */
		off = header[2 + i];

		EEREAD(off++);
		ee->ee_switchSettling[i] = (eeval >> 8) & 0x7f;
		ee->ee_txrxAtten[i] = (eeval >> 2) & 0x3f;
		ee->ee_antennaControl[0][i] = (eeval << 4) & 0x3f;

		EEREAD(off++);
		ee->ee_antennaControl[0][i] |= (eeval >> 12) & 0x0f;
		ee->ee_antennaControl[1][i] = (eeval >> 6) & 0x3f;
		ee->ee_antennaControl[2][i] = eeval & 0x3f;

		EEREAD(off++);
		ee->ee_antennaControl[3][i] = (eeval >> 10)  & 0x3f;
		ee->ee_antennaControl[4][i] = (eeval >> 4)  & 0x3f;
		ee->ee_antennaControl[5][i] = (eeval << 2)  & 0x3f;

		EEREAD(off++);
		ee->ee_antennaControl[5][i] |= (eeval >> 14)  & 0x03;
		ee->ee_antennaControl[6][i] = (eeval >> 8)  & 0x3f;
		ee->ee_antennaControl[7][i] = (eeval >> 2)  & 0x3f;
		ee->ee_antennaControl[8][i] = (eeval << 4)  & 0x3f;

		EEREAD(off++);
		ee->ee_antennaControl[8][i] |= (eeval >> 12)  & 0x0f;
		ee->ee_antennaControl[9][i] = (eeval >> 6)  & 0x3f;
		ee->ee_antennaControl[10][i] = eeval & 0x3f;

		EEREAD(off++);
		ee->ee_adcDesiredSize[i] = (int8_t)((eeval >> 8)  & 0xff);
		switch (i) {
		case headerInfo11A:
			ee->ee_ob4 = (eeval >> 5)  & 0x07;
			ee->ee_db4 = (eeval >> 2)  & 0x07;
			ee->ee_ob3 = (eeval << 1)  & 0x07;
			break;
		case headerInfo11B:
			ee->ee_obFor24 = (eeval >> 4)  & 0x07;
			ee->ee_dbFor24 = eeval & 0x07;
			break;
		case headerInfo11G:
			ee->ee_obFor24g = (eeval >> 4)  & 0x07;
			ee->ee_dbFor24g = eeval & 0x07;
			break;
		}

		if (i == headerInfo11A) {
			EEREAD(off++);
			ee->ee_ob3 |= (eeval >> 15)  & 0x01;
			ee->ee_db3 = (eeval >> 12)  & 0x07;
			ee->ee_ob2 = (eeval >> 9)  & 0x07;
			ee->ee_db2 = (eeval >> 6)  & 0x07;
			ee->ee_ob1 = (eeval >> 3)  & 0x07;
			ee->ee_db1 = eeval & 0x07;
		}

		EEREAD(off++);
		ee->ee_txEndToXLNAOn[i] = (eeval >> 8)  & 0xff;
		ee->ee_thresh62[i] = eeval & 0xff;

		EEREAD(off++);
		ee->ee_txEndToXPAOff[i] = (eeval >> 8)  & 0xff;
		ee->ee_txFrameToXPAOn[i] = eeval  & 0xff;

		EEREAD(off++);
		ee->ee_pgaDesiredSize[i] = (int8_t)((eeval >> 8)  & 0xff);
		ee->ee_noiseFloorThresh[i] = eeval  & 0xff;
		if (ee->ee_noiseFloorThresh[i] & 0x80) {
			ee->ee_noiseFloorThresh[i] = 0 -
				((ee->ee_noiseFloorThresh[i] ^ 0xff) + 1);
		}

		EEREAD(off++);
		ee->ee_xlnaGain[i] = (eeval >> 5)  & 0xff;
		ee->ee_xgain[i] = (eeval >> 1)  & 0x0f;
		ee->ee_xpd[i] = eeval  & 0x01;
		if (ee->ee_version >= AR_EEPROM_VER4_0) {
			switch (i) {
			case headerInfo11A:
				ee->ee_fixedBias5 = (eeval >> 13) & 0x1;
				break;
			case headerInfo11G:
				ee->ee_fixedBias2 = (eeval >> 13) & 0x1;
				break;
			}
		}

		if (ee->ee_version >= AR_EEPROM_VER3_3) {
			EEREAD(off++);
			ee->ee_falseDetectBackoff[i] = (eeval >> 6) & 0x7F;
			switch (i) {
			case headerInfo11B:
				ee->ee_ob2GHz[0] = eeval & 0x7;
				ee->ee_db2GHz[0] = (eeval >> 3) & 0x7;
				break;
			case headerInfo11G:
				ee->ee_ob2GHz[1] = eeval & 0x7;
				ee->ee_db2GHz[1] = (eeval >> 3) & 0x7;
				break;
			case headerInfo11A:
				ee->ee_xrTargetPower5 = eeval & 0x3f;
				break;
			}
		}
		if (ee->ee_version >= AR_EEPROM_VER3_4) {
			ee->ee_gainI[i] = (eeval >> 13) & 0x07;

			EEREAD(off++);
			ee->ee_gainI[i] |= (eeval << 3) & 0x38;
			if (i == headerInfo11G) {
				ee->ee_cckOfdmPwrDelta = (eeval >> 3) & 0xFF;
				if (ee->ee_version >= AR_EEPROM_VER4_6)
					ee->ee_scaledCh14FilterCckDelta =
						(eeval >> 11) & 0x1f;
			}
			if (i == headerInfo11A &&
			    ee->ee_version >= AR_EEPROM_VER4_0) {
				ee->ee_iqCalI[0] = (eeval >> 8 ) & 0x3f;
				ee->ee_iqCalQ[0] = (eeval >> 3 ) & 0x1f;
			}
		} else {
			ee->ee_gainI[i] = 10;
			ee->ee_cckOfdmPwrDelta = TENX_OFDM_CCK_DELTA_INIT;
		}
		if (ee->ee_version >= AR_EEPROM_VER4_0) {
			switch (i) {
			case headerInfo11B:
				EEREAD(off++);
				ee->ee_calPier11b[0] =
					fbin2freq_2p4(ee, eeval&0xff);
				ee->ee_calPier11b[1] =
					fbin2freq_2p4(ee, (eeval >> 8)&0xff);
				EEREAD(off++);
				ee->ee_calPier11b[2] =
					fbin2freq_2p4(ee, eeval&0xff);
				if (ee->ee_version >= AR_EEPROM_VER4_1)
					ee->ee_rxtxMargin[headerInfo11B] =
						(eeval >> 8) & 0x3f;
				break;
			case headerInfo11G:
				EEREAD(off++);
				ee->ee_calPier11g[0] =
					fbin2freq_2p4(ee, eeval & 0xff);
				ee->ee_calPier11g[1] =
					fbin2freq_2p4(ee, (eeval >> 8) & 0xff);

				EEREAD(off++);
				ee->ee_turbo2WMaxPower2 = eeval & 0x7F;
				ee->ee_xrTargetPower2 = (eeval >> 7) & 0x3f;

				EEREAD(off++);
				ee->ee_calPier11g[2] =
					fbin2freq_2p4(ee, eeval & 0xff);
				if (ee->ee_version >= AR_EEPROM_VER4_1)
					 ee->ee_rxtxMargin[headerInfo11G] =
						(eeval >> 8) & 0x3f;

				EEREAD(off++);
				ee->ee_iqCalI[1] = (eeval >> 5) & 0x3F;
				ee->ee_iqCalQ[1] = eeval & 0x1F;

				if (ee->ee_version >= AR_EEPROM_VER4_2) {
					EEREAD(off++);
					ee->ee_cckOfdmGainDelta =
						(uint8_t)(eeval & 0xFF);
					if (ee->ee_version >= AR_EEPROM_VER5_0) {
						ee->ee_switchSettlingTurbo[1] =
							(eeval >> 8) & 0x7f;
						ee->ee_txrxAttenTurbo[1] =
							(eeval >> 15) & 0x1;
						EEREAD(off++);
						ee->ee_txrxAttenTurbo[1] |=
							(eeval & 0x1F) << 1;
						ee->ee_rxtxMarginTurbo[1] =
							(eeval >> 5) & 0x3F;
						ee->ee_adcDesiredSizeTurbo[1] =
							(eeval >> 11) & 0x1F;
						EEREAD(off++);
						ee->ee_adcDesiredSizeTurbo[1] |=
							(eeval & 0x7) << 5;
						ee->ee_pgaDesiredSizeTurbo[1] =
							(eeval >> 3) & 0xFF;
					}
				}
				break;
			case headerInfo11A:
				if (ee->ee_version >= AR_EEPROM_VER4_1) {
					EEREAD(off++);
					ee->ee_rxtxMargin[headerInfo11A] =
						eeval & 0x3f;
					if (ee->ee_version >= AR_EEPROM_VER5_0) {
						ee->ee_switchSettlingTurbo[0] =
							(eeval >> 6) & 0x7f;
						ee->ee_txrxAttenTurbo[0] =
							(eeval >> 13) & 0x7;
						EEREAD(off++);
						ee->ee_txrxAttenTurbo[0] |=
							(eeval & 0x7) << 3;
						ee->ee_rxtxMarginTurbo[0] =
							(eeval >> 3) & 0x3F;
						ee->ee_adcDesiredSizeTurbo[0] =
							(eeval >> 9) & 0x7F;
						EEREAD(off++);
						ee->ee_adcDesiredSizeTurbo[0] |=
							(eeval & 0x1) << 7;
						ee->ee_pgaDesiredSizeTurbo[0] =
							(eeval >> 1) & 0xFF;
					}
				}
				break;
			}
		}
	}
	if (ee->ee_version < AR_EEPROM_VER3_3) {
		/* Version 3.1+ specific parameters */
		EEREAD(0xec);
		ee->ee_ob2GHz[0] = eeval & 0x7;
		ee->ee_db2GHz[0] = (eeval >> 3) & 0x7;

		EEREAD(0xed);
		ee->ee_ob2GHz[1] = eeval & 0x7;
		ee->ee_db2GHz[1] = (eeval >> 3) & 0x7;
	}

	/* Initialize corner cal (thermal tx gain adjust parameters) */
	ee->ee_cornerCal.clip = 4;
	ee->ee_cornerCal.pd90 = 1;
	ee->ee_cornerCal.pd84 = 1;
	ee->ee_cornerCal.gSel = 0;

	/*
	* Read the conformance test limit identifiers
	* These are used to match regulatory domain testing needs with
	* the RD-specific tests that have been calibrated in the EEPROM.
	*/
	off = header[5];
	for (i = 0; i < ee->ee_numCtls; i += 2) {
		EEREAD(off++);
		ee->ee_ctl[i] = (eeval >> 8) & 0xff;
		ee->ee_ctl[i+1] = eeval & 0xff;
	}

	if (ee->ee_version < AR_EEPROM_VER5_3) {
		/* XXX only for 5413? */
		ee->ee_spurChans[0][1] = AR_SPUR_5413_1;
		ee->ee_spurChans[1][1] = AR_SPUR_5413_2;
		ee->ee_spurChans[2][1] = AR_NO_SPUR;
		ee->ee_spurChans[0][0] = AR_NO_SPUR;
	} else {
		/* Read spur mitigation data */
		for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
			EEREAD(off);
			ee->ee_spurChans[i][0] = eeval;
			EEREAD(off+AR_EEPROM_MODAL_SPURS);
			ee->ee_spurChans[i][1] = eeval;
			off++;
		}
	}

	/* for recent changes to NF scale */
	if (ee->ee_version <= AR_EEPROM_VER3_2) {
		ee->ee_noiseFloorThresh[headerInfo11A] = -54;
		ee->ee_noiseFloorThresh[headerInfo11B] = -1;
		ee->ee_noiseFloorThresh[headerInfo11G] = -1;
	}
	/* to override thresh62 for better 2.4 and 5 operation */
	if (ee->ee_version <= AR_EEPROM_VER3_2) {
		ee->ee_thresh62[headerInfo11A] = 15;	/* 11A */
		ee->ee_thresh62[headerInfo11B] = 28;	/* 11B */
		ee->ee_thresh62[headerInfo11G] = 28;	/* 11G */
	}

	/* Check for regulatory capabilities */
	if (ee->ee_version >= AR_EEPROM_VER4_0) {
		EEREAD(regCapOffsetPost4_0);
	} else {
		EEREAD(regCapOffsetPre4_0);
	}

	ee->ee_regCap = eeval;

	if (ee->ee_Amode == 0) {
		/* Check for valid Amode in upgraded h/w */
		if (ee->ee_version >= AR_EEPROM_VER4_0) {
			ee->ee_Amode = (ee->ee_regCap & AR_EEPROM_EEREGCAP_EN_KK_NEW_11A)?1:0;
		} else {
			ee->ee_Amode = (ee->ee_regCap & AR_EEPROM_EEREGCAP_EN_KK_NEW_11A_PRE4_0)?1:0;
		}
	}

	if (ee->ee_version >= AR_EEPROM_VER5_1)
		EEREAD(AR_EEPROM_CAPABILITIES_OFFSET);
	else
		eeval = 0;
	ee->ee_opCap = eeval;

	EEREAD(AR_EEPROM_REG_DOMAIN);
	ee->ee_regdomain = eeval;

	return AH_TRUE;
#undef EEREAD
}

/*
 * Now verify and copy EEPROM contents into the allocated space
 */
static HAL_BOOL
legacyEepromReadContents(struct ath_hal *ah, HAL_EEPROM *ee)
{
	/* Read the header information here */
	if (!readHeaderInfo(ah, ee))
		return AH_FALSE;
#if 0
	/* Require 5112 devices to have EEPROM 4.0 EEP_MAP set */
	if (IS_5112(ah) && !ee->ee_eepMap) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5112 devices must have EEPROM 4.0 with the "
		    "EEP_MAP set\n", __func__);
		return AH_FALSE;
	}
#endif
	/*
	 * Group 1: frequency pier locations readback
	 * check that the structure has been populated
	 * with enough space to hold the channels
	 *
	 * NOTE: Group 1 contains the 5 GHz channel numbers
	 *	 that have dBm->pcdac calibrated information.
	 */
	if (!readEepromFreqPierInfo(ah, ee))
		return AH_FALSE;

	/*
	 * Group 2:  readback data for all frequency piers
	 *
	 * NOTE: Group 2 contains the raw power calibration
	 *	 information for each of the channels that we
	 *	 recorded above.
	 */
	if (!readEepromRawPowerCalInfo(ah, ee))
		return AH_FALSE;

	/*
	 * Group 5: target power values per rate
	 *
	 * NOTE: Group 5 contains the recorded maximum power
	 *	 in dB that can be attained for the given rate.
	 */
	/* Read the power per rate info for test channels */
	if (!readEepromTargetPowerCalInfo(ah, ee))
		return AH_FALSE;

	/*
	 * Group 8: Conformance Test Limits information
	 *
	 * NOTE: Group 8 contains the values to limit the
	 *	 maximum transmit power value based on any
	 *	 band edge violations.
	 */
	/* Read the RD edge power limits */
	return readEepromCTLInfo(ah, ee);
}

static HAL_STATUS
legacyEepromGet(struct ath_hal *ah, int param, void *val)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint8_t *macaddr;
	uint16_t eeval;
	uint32_t sum;
	int i;

	switch (param) {
	case AR_EEP_OPCAP:
		*(uint16_t *) val = ee->ee_opCap;
		return HAL_OK;
	case AR_EEP_REGDMN_0:
		*(uint16_t *) val = ee->ee_regdomain;
		return HAL_OK;
	case AR_EEP_RFSILENT:
		if (!ath_hal_eepromRead(ah, AR_EEPROM_RFSILENT, &eeval))
			return HAL_EEREAD;
		*(uint16_t *) val = eeval;
		return HAL_OK;
	case AR_EEP_MACADDR:
		sum = 0;
		macaddr = val;
		for (i = 0; i < 3; i++) {
			if (!ath_hal_eepromRead(ah, AR_EEPROM_MAC(2-i), &eeval)) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "%s: cannot read EEPROM location %u\n",
				    __func__, i);
				return HAL_EEREAD;
			}
			sum += eeval;
			macaddr[2*i] = eeval >> 8;
			macaddr[2*i + 1] = eeval & 0xff;
		}
		if (sum == 0 || sum == 0xffff*3) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: mac address read failed: %s\n", __func__,
			    ath_hal_ether_sprintf(macaddr));
			return HAL_EEBADMAC;
		}
		return HAL_OK;
	case AR_EEP_RFKILL:
		HALASSERT(val == AH_NULL);
		return ee->ee_rfKill ? HAL_OK : HAL_EIO;
	case AR_EEP_AMODE:
		HALASSERT(val == AH_NULL);
		return ee->ee_Amode ? HAL_OK : HAL_EIO;
	case AR_EEP_BMODE:
		HALASSERT(val == AH_NULL);
		return ee->ee_Bmode ? HAL_OK : HAL_EIO;
	case AR_EEP_GMODE:
		HALASSERT(val == AH_NULL);
		return ee->ee_Gmode ? HAL_OK : HAL_EIO;
	case AR_EEP_TURBO5DISABLE:
		HALASSERT(val == AH_NULL);
		return ee->ee_turbo5Disable ? HAL_OK : HAL_EIO;
	case AR_EEP_TURBO2DISABLE:
		HALASSERT(val == AH_NULL);
		return ee->ee_turbo2Disable ? HAL_OK : HAL_EIO;
	case AR_EEP_ISTALON:		/* Talon detect */
		HALASSERT(val == AH_NULL);
		return (ee->ee_version >= AR_EEPROM_VER5_4 &&
		    ath_hal_eepromRead(ah, 0x0b, &eeval) && eeval == 1) ?
			HAL_OK : HAL_EIO;
	case AR_EEP_32KHZCRYSTAL:
		HALASSERT(val == AH_NULL);
		return ee->ee_exist32kHzCrystal ? HAL_OK : HAL_EIO;
	case AR_EEP_COMPRESS:
		HALASSERT(val == AH_NULL);
		return (ee->ee_opCap & AR_EEPROM_EEPCAP_COMPRESS_DIS) == 0 ?
		    HAL_OK : HAL_EIO;
	case AR_EEP_FASTFRAME:
		HALASSERT(val == AH_NULL);
		return (ee->ee_opCap & AR_EEPROM_EEPCAP_FASTFRAME_DIS) == 0 ?
		    HAL_OK : HAL_EIO;
	case AR_EEP_AES:
		HALASSERT(val == AH_NULL);
		return (ee->ee_opCap & AR_EEPROM_EEPCAP_AES_DIS) == 0 ?
		    HAL_OK : HAL_EIO;
	case AR_EEP_BURST:
		HALASSERT(val == AH_NULL);
		return (ee->ee_opCap & AR_EEPROM_EEPCAP_BURST_DIS) == 0 ?
		    HAL_OK : HAL_EIO;
	case AR_EEP_MAXQCU:
		if (ee->ee_opCap & AR_EEPROM_EEPCAP_MAXQCU) {
			*(uint16_t *) val =
			    MS(ee->ee_opCap, AR_EEPROM_EEPCAP_MAXQCU);
			return HAL_OK;
		} else
			return HAL_EIO;
	case AR_EEP_KCENTRIES:
		if (ee->ee_opCap & AR_EEPROM_EEPCAP_KC_ENTRIES) {
			*(uint16_t *) val =
			    1 << MS(ee->ee_opCap, AR_EEPROM_EEPCAP_KC_ENTRIES);
			return HAL_OK;
		} else
			return HAL_EIO;
	case AR_EEP_ANTGAINMAX_5:
		*(int8_t *) val = ee->ee_antennaGainMax[0];
		return HAL_OK;
	case AR_EEP_ANTGAINMAX_2:
		*(int8_t *) val = ee->ee_antennaGainMax[1];
		return HAL_OK;
	case AR_EEP_WRITEPROTECT:
		HALASSERT(val == AH_NULL);
		return (ee->ee_protect & AR_EEPROM_PROTECT_WP_128_191) ?
		    HAL_OK : HAL_EIO;
	}
	return HAL_EINVAL;
}

static HAL_STATUS
legacyEepromSet(struct ath_hal *ah, int param, int v)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

	switch (param) {
	case AR_EEP_AMODE:
		ee->ee_Amode = v;
		return HAL_OK;
	case AR_EEP_BMODE:
		ee->ee_Bmode = v;
		return HAL_OK;
	case AR_EEP_GMODE:
		ee->ee_Gmode = v;
		return HAL_OK;
	case AR_EEP_TURBO5DISABLE:
		ee->ee_turbo5Disable = v;
		return HAL_OK;
	case AR_EEP_TURBO2DISABLE:
		ee->ee_turbo2Disable = v;
		return HAL_OK;
	case AR_EEP_COMPRESS:
		if (v)
			ee->ee_opCap &= ~AR_EEPROM_EEPCAP_COMPRESS_DIS;
		else
			ee->ee_opCap |= AR_EEPROM_EEPCAP_COMPRESS_DIS;
		return HAL_OK;
	case AR_EEP_FASTFRAME:
		if (v)
			ee->ee_opCap &= ~AR_EEPROM_EEPCAP_FASTFRAME_DIS;
		else
			ee->ee_opCap |= AR_EEPROM_EEPCAP_FASTFRAME_DIS;
		return HAL_OK;
	case AR_EEP_AES:
		if (v)
			ee->ee_opCap &= ~AR_EEPROM_EEPCAP_AES_DIS;
		else
			ee->ee_opCap |= AR_EEPROM_EEPCAP_AES_DIS;
		return HAL_OK;
	case AR_EEP_BURST:
		if (v)
			ee->ee_opCap &= ~AR_EEPROM_EEPCAP_BURST_DIS;
		else
			ee->ee_opCap |= AR_EEPROM_EEPCAP_BURST_DIS;
		return HAL_OK;
	}
	return HAL_EINVAL;
}

static HAL_BOOL
legacyEepromDiag(struct ath_hal *ah, int request,
     const void *args, uint32_t argsize, void **result, uint32_t *resultsize)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	const EEPROM_POWER_EXPN_5112 *pe;

	switch (request) {
	case HAL_DIAG_EEPROM:
		*result = ee;
		*resultsize = sizeof(*ee);
		return AH_TRUE;
	case HAL_DIAG_EEPROM_EXP_11A:
	case HAL_DIAG_EEPROM_EXP_11B:
	case HAL_DIAG_EEPROM_EXP_11G:
		pe = &ee->ee_modePowerArray5112[
		    request - HAL_DIAG_EEPROM_EXP_11A];
		*result = pe->pChannels;
		*resultsize = (*result == AH_NULL) ? 0 :
			roundup(sizeof(uint16_t) * pe->numChannels,
				sizeof(uint32_t)) +
			sizeof(EXPN_DATA_PER_CHANNEL_5112) * pe->numChannels;
		return AH_TRUE;
	}
	return AH_FALSE;
}

static uint16_t
legacyEepromGetSpurChan(struct ath_hal *ah, int ix, HAL_BOOL is2GHz)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

	HALASSERT(0 <= ix && ix < AR_EEPROM_MODAL_SPURS);
	return ee->ee_spurChans[ix][is2GHz];
}

/*
 * Reclaim any EEPROM-related storage.
 */
static void
legacyEepromDetach(struct ath_hal *ah)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

        if (ee->ee_version >= AR_EEPROM_VER4_0 && ee->ee_eepMap == 1)
		freeEepromRawPowerCalInfo5112(ah, ee);
	ath_hal_free(ee);
	AH_PRIVATE(ah)->ah_eeprom = AH_NULL;
}

/*
 * These are not valid 2.4 channels, either we change 'em
 * or we need to change the coding to accept them.
 */
static const uint16_t channels11b[] = { 2412, 2447, 2484 };
static const uint16_t channels11g[] = { 2312, 2412, 2484 };

HAL_STATUS
ath_hal_legacyEepromAttach(struct ath_hal *ah)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint32_t sum, eepMax;
	uint16_t eeversion, eeprotect, eeval;
	u_int i;

	HALASSERT(ee == AH_NULL);

	if (!ath_hal_eepromRead(ah, AR_EEPROM_VERSION, &eeversion)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unable to read EEPROM version\n", __func__);
		return HAL_EEREAD;
	}
	if (eeversion < AR_EEPROM_VER3) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unsupported EEPROM version "
		    "%u (0x%x) found\n", __func__, eeversion, eeversion);
		return HAL_EEVERSION;
	}

	if (!ath_hal_eepromRead(ah, AR_EEPROM_PROTECT, &eeprotect)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: cannot read EEPROM protection "
		    "bits; read locked?\n", __func__);
		return HAL_EEREAD;
	}
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "EEPROM protect 0x%x\n", eeprotect);
	/* XXX check proper access before continuing */

	/*
	 * Read the Atheros EEPROM entries and calculate the checksum.
	 */
	if (!ath_hal_eepromRead(ah, AR_EEPROM_SIZE_UPPER, &eeval)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read EEPROM upper size\n" , __func__);
		return HAL_EEREAD;
	}
	if (eeval != 0)	{
		eepMax = (eeval & AR_EEPROM_SIZE_UPPER_MASK) <<
			AR_EEPROM_SIZE_ENDLOC_SHIFT;
		if (!ath_hal_eepromRead(ah, AR_EEPROM_SIZE_LOWER, &eeval)) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: cannot read EEPROM lower size\n" , __func__);
			return HAL_EEREAD;
		}
		eepMax = (eepMax | eeval) - AR_EEPROM_ATHEROS_BASE;
	} else
		eepMax = AR_EEPROM_ATHEROS_MAX;
	sum = 0;
	for (i = 0; i < eepMax; i++) {
		if (!ath_hal_eepromRead(ah, AR_EEPROM_ATHEROS(i), &eeval)) {
			return HAL_EEREAD;
		}
		sum ^= eeval;
	}
	if (sum != 0xffff) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad EEPROM checksum 0x%x\n",
		    __func__, sum);
		return HAL_EEBADSUM;
	}

	ee = ath_hal_malloc(sizeof(HAL_EEPROM));
	if (ee == AH_NULL) {
		/* XXX message */
		return HAL_ENOMEM;
	}

	ee->ee_protect = eeprotect;
	ee->ee_version = eeversion;

	ee->ee_numChannels11a = NUM_11A_EEPROM_CHANNELS;
	ee->ee_numChannels2_4 = NUM_2_4_EEPROM_CHANNELS;

	for (i = 0; i < NUM_11A_EEPROM_CHANNELS; i ++)
		ee->ee_dataPerChannel11a[i].numPcdacValues = NUM_PCDAC_VALUES;

	/* the channel list for 2.4 is fixed, fill this in here */
	for (i = 0; i < NUM_2_4_EEPROM_CHANNELS; i++) {
		ee->ee_channels11b[i] = channels11b[i];
		/* XXX 5211 requires a hack though we don't support 11g */
		if (ah->ah_magic == 0x19570405)
			ee->ee_channels11g[i] = channels11b[i];
		else
			ee->ee_channels11g[i] = channels11g[i];
		ee->ee_dataPerChannel11b[i].numPcdacValues = NUM_PCDAC_VALUES;
		ee->ee_dataPerChannel11g[i].numPcdacValues = NUM_PCDAC_VALUES;
	}

	if (!legacyEepromReadContents(ah, ee)) {
		/* XXX message */
		ath_hal_free(ee);
		return HAL_EEREAD;	/* XXX */
	}

	AH_PRIVATE(ah)->ah_eeprom = ee;
	AH_PRIVATE(ah)->ah_eeversion = eeversion;
	AH_PRIVATE(ah)->ah_eepromDetach = legacyEepromDetach;
	AH_PRIVATE(ah)->ah_eepromGet = legacyEepromGet;
	AH_PRIVATE(ah)->ah_eepromSet = legacyEepromSet;
	AH_PRIVATE(ah)->ah_getSpurChan = legacyEepromGetSpurChan;
	AH_PRIVATE(ah)->ah_eepromDiag = legacyEepromDiag;
	return HAL_OK;
}
