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

/* IQ Cal aliases */
#define	totalPowerMeasI(i)	caldata[0][i].u
#define	totalPowerMeasQ(i)	caldata[1][i].u
#define	totalIqCorrMeas(i)	caldata[2][i].s

/*
 * Collect data from HW to later perform IQ Mismatch Calibration
 */
void
ar5416IQCalCollect(struct ath_hal *ah)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	int i;

	/* 
	 * Accumulate IQ cal measures for active chains
	 */
	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		cal->totalPowerMeasI(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		cal->totalPowerMeasQ(i) +=
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		cal->totalIqCorrMeas(i) += (int32_t)
		    OS_REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%d: Chn %d pmi=0x%08x;pmq=0x%08x;iqcm=0x%08x;\n",
		    cal->calSamples, i, cal->totalPowerMeasI(i),
		    cal->totalPowerMeasQ(i), cal->totalIqCorrMeas(i));
	}
}

/*
 * Use HW data to do IQ Mismatch Calibration
 */
void
ar5416IQCalibration(struct ath_hal *ah, uint8_t numChains)
{
	struct ar5416PerCal *cal = &AH5416(ah)->ah_cal;
	int i;

	for (i = 0; i < numChains; i++) {
		uint32_t powerMeasI = cal->totalPowerMeasI(i);
		uint32_t powerMeasQ = cal->totalPowerMeasQ(i);
		uint32_t iqCorrMeas = cal->totalIqCorrMeas(i);
		uint32_t qCoffDenom, iCoffDenom;
		int iqCorrNeg;

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "Start IQ Cal and Correction for Chain %d\n", i);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "Orignal: iq_corr_meas = 0x%08x\n", iqCorrMeas);

		iqCorrNeg = 0;
		/* iqCorrMeas is always negative. */ 
		if (iqCorrMeas > 0x80000000)  {
			iqCorrMeas = (0xffffffff - iqCorrMeas) + 1;
			iqCorrNeg = 1;
		}

		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_i = 0x%08x\n",
		    powerMeasI);
		HALDEBUG(ah, HAL_DEBUG_PERCAL, " pwr_meas_q = 0x%08x\n",
		    powerMeasQ);
		HALDEBUG(ah, HAL_DEBUG_PERCAL, " iqCorrNeg is 0x%08x\n",
		    iqCorrNeg);

		iCoffDenom = (powerMeasI/2 + powerMeasQ/2)/ 128;
		qCoffDenom = powerMeasQ / 64;
		/* Protect against divide-by-0 */
		if (powerMeasQ != 0) {
			/* IQ corr_meas is already negated if iqcorr_neg == 1 */
			int32_t iCoff = iqCorrMeas/iCoffDenom;
			int32_t qCoff = powerMeasI/qCoffDenom - 64;

			HALDEBUG(ah, HAL_DEBUG_PERCAL, " iCoff = 0x%08x\n",
			    iCoff);
			HALDEBUG(ah, HAL_DEBUG_PERCAL, " qCoff = 0x%08x\n",
			    qCoff);
	 
			/* Negate iCoff if iqCorrNeg == 0 */
			iCoff = iCoff & 0x3f;
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    "New:  iCoff = 0x%08x\n", iCoff);

			if (iqCorrNeg == 0x0)
				iCoff = 0x40 - iCoff;
			if (qCoff > 15)
				qCoff = 15;
			else if (qCoff <= -16)
				qCoff = -16;
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    " : iCoff = 0x%x  qCoff = 0x%x\n", iCoff, qCoff);

			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4_CHAIN(i),
			    AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, iCoff);
			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4_CHAIN(i),
			    AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, qCoff);
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    "IQ Cal and Correction done for Chain %d\n", i);
		}
	}
	OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4,
	    AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);
}
