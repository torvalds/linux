/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2010 Atheros Communications Inc.
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

#include "ah_eeprom_v4k.h"

#include "ar9002/ar9285.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar9002/ar9002phy.h"
#include "ar9002/ar9285phy.h"
#include "ar9002/ar9285an.h"

#include "ar9002/ar9285_cal.h"

#define	AR9285_CLCAL_REDO_THRESH	1
#define	MAX_PACAL_SKIPCOUNT		8

#define	N(a)	(sizeof (a) / sizeof (a[0]))

static void
ar9285_hw_pa_cal(struct ath_hal *ah, HAL_BOOL is_reset)
{
	uint32_t regVal;
	int i, offset, offs_6_1, offs_0;
	uint32_t ccomp_org, reg_field;
	uint32_t regList[][2] = {
		{ 0x786c, 0 },
		{ 0x7854, 0 },
		{ 0x7820, 0 },
		{ 0x7824, 0 },
		{ 0x7868, 0 },
		{ 0x783c, 0 },
		{ 0x7838, 0 },
	};

	/* PA CAL is not needed for high power solution */
	if (ath_hal_eepromGet(ah, AR_EEP_TXGAIN_TYPE, AH_NULL) ==
	    AR5416_EEP_TXGAIN_HIGH_POWER)
		return;

	HALDEBUG(ah, HAL_DEBUG_PERCAL, "Running PA Calibration\n");

	for (i = 0; i < N(regList); i++)
		regList[i][1] = OS_REG_READ(ah, regList[i][0]);

	regVal = OS_REG_READ(ah, 0x7834);
	regVal &= (~(0x1));
	OS_REG_WRITE(ah, 0x7834, regVal);
	regVal = OS_REG_READ(ah, 0x9808);
	regVal |= (0x1 << 27);
	OS_REG_WRITE(ah, 0x9808, regVal);

	OS_REG_RMW_FIELD(ah, AR9285_AN_TOP3, AR9285_AN_TOP3_PWDDAC, 1);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDRXTXBB1, 1);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDV2I, 1);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RXTXBB1, AR9285_AN_RXTXBB1_PDDACIF, 1);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G2, AR9285_AN_RF2G2_OFFCAL, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PWDDB, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_ENPACAL, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV1, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPADRV2, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G1, AR9285_AN_RF2G1_PDPAOUT, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G8, AR9285_AN_RF2G8_PADRVGN2TAB0, 7);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G7, AR9285_AN_RF2G7_PADRVGN2TAB0, 0);
	ccomp_org = MS(OS_REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_CCOMP);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, 0xf);

	OS_REG_WRITE(ah, AR9285_AN_TOP2, 0xca0358a0);
	OS_DELAY(30);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, 0);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 0);

	for (i = 6; i > 0; i--) {
		regVal = OS_REG_READ(ah, 0x7834);
		regVal |= (1 << (19 + i));
		OS_REG_WRITE(ah, 0x7834, regVal);
		OS_DELAY(1);
		regVal = OS_REG_READ(ah, 0x7834);
		regVal &= (~(0x1 << (19 + i)));
		reg_field = MS(OS_REG_READ(ah, 0x7840), AR9285_AN_RXTXBB1_SPARE9);
		regVal |= (reg_field << (19 + i));
		OS_REG_WRITE(ah, 0x7834, regVal);
	}

	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, 1);
	OS_DELAY(1);
	reg_field = MS(OS_REG_READ(ah, AR9285_AN_RF2G9), AR9285_AN_RXTXBB1_SPARE9);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, reg_field);
	offs_6_1 = MS(OS_REG_READ(ah, AR9285_AN_RF2G6), AR9285_AN_RF2G6_OFFS);
	offs_0   = MS(OS_REG_READ(ah, AR9285_AN_RF2G3), AR9285_AN_RF2G3_PDVCCOMP);

	offset = (offs_6_1<<1) | offs_0;
	offset = offset - 0;
	offs_6_1 = offset>>1;
	offs_0 = offset & 1;

	if ((!is_reset) && (AH9285(ah)->pacal_info.prev_offset == offset)) {
		if (AH9285(ah)->pacal_info.max_skipcount < MAX_PACAL_SKIPCOUNT)
			AH9285(ah)->pacal_info.max_skipcount =
				2 * AH9285(ah)->pacal_info.max_skipcount;
		AH9285(ah)->pacal_info.skipcount = AH9285(ah)->pacal_info.max_skipcount;
	} else {
		AH9285(ah)->pacal_info.max_skipcount = 1;
		AH9285(ah)->pacal_info.skipcount = 0;
		AH9285(ah)->pacal_info.prev_offset = offset;
	}

	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_OFFS, offs_6_1);
	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_PDVCCOMP, offs_0);

	regVal = OS_REG_READ(ah, 0x7834);
	regVal |= 0x1;
	OS_REG_WRITE(ah, 0x7834, regVal);
	regVal = OS_REG_READ(ah, 0x9808);
	regVal &= (~(0x1 << 27));
	OS_REG_WRITE(ah, 0x9808, regVal);

	for (i = 0; i < N(regList); i++)
		OS_REG_WRITE(ah, regList[i][0], regList[i][1]);

	OS_REG_RMW_FIELD(ah, AR9285_AN_RF2G6, AR9285_AN_RF2G6_CCOMP, ccomp_org);
}

void
ar9002_hw_pa_cal(struct ath_hal *ah, HAL_BOOL is_reset)
{
	if (AR_SREV_KITE_11_OR_LATER(ah)) {
		if (is_reset || !AH9285(ah)->pacal_info.skipcount)
			ar9285_hw_pa_cal(ah, is_reset);
		else
			AH9285(ah)->pacal_info.skipcount--;
	}
}

/* Carrier leakage Calibration fix */
static HAL_BOOL
ar9285_hw_cl_cal(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	OS_REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
	if (IEEE80211_IS_CHAN_HT20(chan)) {
		OS_REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_PARALLEL_CAL_ENABLE);
		OS_REG_SET_BIT(ah, AR_PHY_TURBO, AR_PHY_FC_DYN2040_EN);
		OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
			    AR_PHY_AGC_CONTROL_FLTR_CAL);
		OS_REG_CLR_BIT(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_CAL_ENABLE);
		OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
		if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL,
				  AR_PHY_AGC_CONTROL_CAL, 0)) {
			HALDEBUG(ah, HAL_DEBUG_PERCAL,
				"offset calibration failed to complete in 1ms; noisy environment?\n");
			return AH_FALSE;
		}
		OS_REG_CLR_BIT(ah, AR_PHY_TURBO, AR_PHY_FC_DYN2040_EN);
		OS_REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_PARALLEL_CAL_ENABLE);
		OS_REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
	}
	OS_REG_CLR_BIT(ah, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_FLTR_CAL);
	OS_REG_SET_BIT(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_CAL_ENABLE);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
	if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL,
			  0)) {
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
			"offset calibration failed to complete in 1ms; noisy environment?\n");
		return AH_FALSE;
	}

	OS_REG_SET_BIT(ah, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
	OS_REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
	OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_FLTR_CAL);

	return AH_TRUE;
}

static HAL_BOOL
ar9285_hw_clc(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	int i;
	uint32_t txgain_max;
	uint32_t clc_gain, gain_mask = 0, clc_num = 0;
	uint32_t reg_clc_I0, reg_clc_Q0;
	uint32_t i0_num = 0;
	uint32_t q0_num = 0;
	uint32_t total_num = 0;
	uint32_t reg_rf2g5_org;
	HAL_BOOL retv = AH_TRUE;

	if (!(ar9285_hw_cl_cal(ah, chan)))
		return AH_FALSE;

	txgain_max = MS(OS_REG_READ(ah, AR_PHY_TX_PWRCTRL7),
			AR_PHY_TX_PWRCTRL_TX_GAIN_TAB_MAX);

	for (i = 0; i < (txgain_max+1); i++) {
		clc_gain = (OS_REG_READ(ah, (AR_PHY_TX_GAIN_TBL1+(i<<2))) &
			   AR_PHY_TX_GAIN_CLC) >> AR_PHY_TX_GAIN_CLC_S;
		if (!(gain_mask & (1 << clc_gain))) {
			gain_mask |= (1 << clc_gain);
			clc_num++;
		}
	}

	for (i = 0; i < clc_num; i++) {
		reg_clc_I0 = (OS_REG_READ(ah, (AR_PHY_CLC_TBL1 + (i << 2)))
			      & AR_PHY_CLC_I0) >> AR_PHY_CLC_I0_S;
		reg_clc_Q0 = (OS_REG_READ(ah, (AR_PHY_CLC_TBL1 + (i << 2)))
			      & AR_PHY_CLC_Q0) >> AR_PHY_CLC_Q0_S;
		if (reg_clc_I0 == 0)
			i0_num++;

		if (reg_clc_Q0 == 0)
			q0_num++;
	}
	total_num = i0_num + q0_num;
	if (total_num > AR9285_CLCAL_REDO_THRESH) {
		reg_rf2g5_org = OS_REG_READ(ah, AR9285_RF2G5);
		if (AR_SREV_9285E_20(ah)) {
			OS_REG_WRITE(ah, AR9285_RF2G5,
				  (reg_rf2g5_org & AR9285_RF2G5_IC50TX) |
				  AR9285_RF2G5_IC50TX_XE_SET);
		} else {
			OS_REG_WRITE(ah, AR9285_RF2G5,
				  (reg_rf2g5_org & AR9285_RF2G5_IC50TX) |
				  AR9285_RF2G5_IC50TX_SET);
		}
		retv = ar9285_hw_cl_cal(ah, chan);
		OS_REG_WRITE(ah, AR9285_RF2G5, reg_rf2g5_org);
	}
	return retv;
}

HAL_BOOL
ar9285InitCalHardware(struct ath_hal *ah,
    const struct ieee80211_channel *chan)
{
	if (AR_SREV_KITE(ah) && AR_SREV_KITE_10_OR_LATER(ah) &&
	    (! ar9285_hw_clc(ah, chan)))
		return AH_FALSE;

	return AH_TRUE;
}
