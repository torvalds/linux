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

/* Adc Gain Cal aliases */
#define	totalAdcIOddPhase(i)	caldata[0][i].u
#define	totalAdcIEvenPhase(i)	caldata[1][i].u
#define	totalAdcQOddPhase(i)	caldata[2][i].u
#define	totalAdcQEvenPhase(i)	caldata[3][i].u

/*
 * Collect data from HW to later perform ADC Gain Calibration
 */
void
ar5416AdcGainCalCollect(struct ath_hal *ah)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	int i;

	/*
	* Accumulate ADC Gain cal measures for active chains
	*/
	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		cal->totalAdcIOddPhase(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		cal->totalAdcIEvenPhase(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		cal->totalAdcQOddPhase(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		cal->totalAdcQEvenPhase(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%d: Chn %d oddi=0x%08x; eveni=0x%08x; oddq=0x%08x; evenq=0x%08x;\n",
		    cal->calSamples, i, cal->totalAdcIOddPhase(i),
		    cal->totalAdcIEvenPhase(i), cal->totalAdcQOddPhase(i),
		    cal->totalAdcQEvenPhase(i));
	}
}

/*
 * Use HW data to do ADC Gain Calibration
 */
void
ar5416AdcGainCalibration(struct ath_hal *ah, uint8_t numChains)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	uint32_t i;

	for (i = 0; i < numChains; i++) {
		uint32_t iOddMeasOffset  = cal->totalAdcIOddPhase(i);
		uint32_t iEvenMeasOffset = cal->totalAdcIEvenPhase(i);
		uint32_t qOddMeasOffset  = cal->totalAdcQOddPhase(i);
		uint32_t qEvenMeasOffset = cal->totalAdcQEvenPhase(i);

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "Start ADC Gain Cal for Chain %d\n", i);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "  pwr_meas_odd_i = 0x%08x\n", iOddMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "  pwr_meas_even_i = 0x%08x\n", iEvenMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "  pwr_meas_odd_q = 0x%08x\n", qOddMeasOffset);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "  pwr_meas_even_q = 0x%08x\n", qEvenMeasOffset);

		if (iOddMeasOffset != 0 && qEvenMeasOffset != 0) {
			uint32_t iGainMismatch =
			    ((iEvenMeasOffset*32)/iOddMeasOffset) & 0x3f;
			uint32_t qGainMismatch =
			    ((qOddMeasOffset*32)/qEvenMeasOffset) & 0x3f;
			uint32_t val;

			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    " gain_mismatch_i = 0x%08x\n",
			    iGainMismatch);
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    " gain_mismatch_q = 0x%08x\n",
			    qGainMismatch);

			val = OS_REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
			val &= 0xfffff000;
			val |= (qGainMismatch) | (iGainMismatch << 6);
			OS_REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val); 

			HALDEBUG(ah,  HAL_DEBUG_PERCAL,
			    "ADC Gain Cal done for Chain %d\n", i);
		}
	}
	OS_REG_SET_BIT(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
	    AR_PHY_NEW_ADC_GAIN_CORR_ENABLE);
}
