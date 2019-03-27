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

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/* Adc DC Offset Cal aliases */
#define	totalAdcDcOffsetIOddPhase(i)	caldata[0][i].s
#define	totalAdcDcOffsetIEvenPhase(i)	caldata[1][i].s
#define	totalAdcDcOffsetQOddPhase(i)	caldata[2][i].s
#define	totalAdcDcOffsetQEvenPhase(i)	caldata[3][i].s

void
ar5416AdcDcCalCollect(struct ath_hal *ah)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		cal->totalAdcDcOffsetIOddPhase(i) += (int32_t)
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		cal->totalAdcDcOffsetIEvenPhase(i) += (int32_t)
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		cal->totalAdcDcOffsetQOddPhase(i) += (int32_t)
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		cal->totalAdcDcOffsetQEvenPhase(i) += (int32_t)
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%d: Chn %d oddi=0x%08x; eveni=0x%08x; oddq=0x%08x; evenq=0x%08x;\n",
		   cal->calSamples, i,
		   cal->totalAdcDcOffsetIOddPhase(i),
		   cal->totalAdcDcOffsetIEvenPhase(i),
		   cal->totalAdcDcOffsetQOddPhase(i),
		   cal->totalAdcDcOffsetQEvenPhase(i));
	}
}

void
ar5416AdcDcCalibration(struct ath_hal *ah, uint8_t numChains)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	const HAL_PERCAL_DATA *calData = cal->cal_curr->calData;
	uint32_t numSamples;
	int i;

	numSamples = (1 << (calData->calCountMax + 5)) * calData->calNumSamples;
	for (i = 0; i < numChains; i++) {
		uint32_t iOddMeasOffset = cal->totalAdcDcOffsetIOddPhase(i);
		uint32_t iEvenMeasOffset = cal->totalAdcDcOffsetIEvenPhase(i);
		int32_t qOddMeasOffset = cal->totalAdcDcOffsetQOddPhase(i);
		int32_t qEvenMeasOffset = cal->totalAdcDcOffsetQEvenPhase(i);
		int32_t qDcMismatch, iDcMismatch;
		uint32_t val;

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "Starting ADC DC Offset Cal for Chain %d\n", i);

		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_odd_i = %d\n",
		    iOddMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_even_i = %d\n",
		    iEvenMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_odd_q = %d\n",
		    qOddMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_even_q = %d\n",
		    qEvenMeasOffset);

		HALASSERT(numSamples);

		iDcMismatch = (((iEvenMeasOffset - iOddMeasOffset) * 2) /
		    numSamples) & 0x1ff;
		qDcMismatch = (((qOddMeasOffset - qEvenMeasOffset) * 2) /
		    numSamples) & 0x1ff;
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    " dc_offset_mismatch_i = 0x%08x\n", iDcMismatch);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    " dc_offset_mismatch_q = 0x%08x\n", qDcMismatch);

		val = OS_REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
		val &= 0xc0000fff;
		val |= (qDcMismatch << 12) | (iDcMismatch << 21);
		OS_REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val); 

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "ADC DC Offset Cal done for Chain %d\n", i);
	}
	OS_REG_SET_BIT(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
	    AR_PHY_NEW_ADC_DC_OFFSET_CORR_ENABLE);
}
