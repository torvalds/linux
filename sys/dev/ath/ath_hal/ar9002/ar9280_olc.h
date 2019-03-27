/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Adrian Chadd, Xenion Pty Ltd.
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

#ifndef	__ATH_AR9280_OLC_H__
#define	__ATH_AR9280_OLC_H__

extern void ar9280olcInit(struct ath_hal *ah);

/* OLC TX power control */
extern void ar9280olcGetPDADCs(struct ath_hal *ah, uint32_t initTxGain,
    int txPower, uint8_t *pPDADCValues);
extern void ar9280olcGetTxGainIndex(struct ath_hal *ah,
    const struct ieee80211_channel *chan,
    struct calDataPerFreqOpLoop *rawDatasetOpLoop,
    uint8_t *calChans, uint16_t availPiers, uint8_t *pwr, uint8_t *pcdacIdx);
extern void ar9280GetGainBoundariesAndPdadcs(struct ath_hal *ah,
        const struct ieee80211_channel *chan, CAL_DATA_PER_FREQ *pRawDataSet,
        uint8_t * bChans, uint16_t availPiers,
        uint16_t tPdGainOverlap, int16_t *pMinCalPower,
        uint16_t * pPdGainBoundaries, uint8_t * pPDADCValues,
        uint16_t numXpdGains);
extern HAL_BOOL ar9280SetPowerCalTable(struct ath_hal *ah,
	struct ar5416eeprom *pEepData, const struct ieee80211_channel *chan,
	int16_t *pTxPowerIndexOffset);

/* OLC calibration */
extern void ar9280olcTemperatureCompensation(struct ath_hal *ah);

#endif
