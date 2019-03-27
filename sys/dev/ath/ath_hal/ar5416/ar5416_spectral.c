/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2012 Qualcomm Atheros, All Rights Reserved.
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
#include "ah_desc.h"                    /* NB: for HAL_PHYERR* */

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/*
 * Default AR9280 spectral scan parameters
 */
#define	AR5416_SPECTRAL_SCAN_ENA		0
#define	AR5416_SPECTRAL_SCAN_ACTIVE		0
#define	AR5416_SPECTRAL_SCAN_FFT_PERIOD		8
#define	AR5416_SPECTRAL_SCAN_PERIOD		1
#define	AR5416_SPECTRAL_SCAN_COUNT		16 //used to be 128
#define	AR5416_SPECTRAL_SCAN_SHORT_REPEAT	1

/* constants */
#define	MAX_RADAR_RSSI_THRESH	0x3f
#define	MAX_RADAR_HEIGHT	0x3f
#define	ENABLE_ALL_PHYERR	0xffffffff

static void ar5416DisableRadar(struct ath_hal *ah);
static void ar5416PrepSpectralScan(struct ath_hal *ah);

static void
ar5416DisableRadar(struct ath_hal *ah)
{
	uint32_t val;

	// Enable radar FFT
	val = OS_REG_READ(ah, AR_PHY_RADAR_0);
	val |= AR_PHY_RADAR_0_FFT_ENA;

	// set radar detect thresholds to max to effectively disable radar
	val &= ~AR_PHY_RADAR_0_RRSSI;
	val |= SM(MAX_RADAR_RSSI_THRESH, AR_PHY_RADAR_0_RRSSI);

	val &= ~AR_PHY_RADAR_0_HEIGHT;
	val |= SM(MAX_RADAR_HEIGHT, AR_PHY_RADAR_0_HEIGHT);

	val &= ~(AR_PHY_RADAR_0_ENA);
	OS_REG_WRITE(ah, AR_PHY_RADAR_0, val);

	// disable extension radar detect
	val = OS_REG_READ(ah, AR_PHY_RADAR_EXT);
	OS_REG_WRITE(ah, AR_PHY_RADAR_EXT, val & ~AR_PHY_RADAR_EXT_ENA);

	val = OS_REG_READ(ah, AR_RX_FILTER);
	val |= (1<<13);
	OS_REG_WRITE(ah, AR_RX_FILTER, val);
}

static void
ar5416PrepSpectralScan(struct ath_hal *ah)
{

	ar5416DisableRadar(ah);
	OS_REG_WRITE(ah, AR_PHY_ERR, ENABLE_ALL_PHYERR);
}

void
ar5416ConfigureSpectralScan(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss)
{
	uint32_t val;

	ar5416PrepSpectralScan(ah);

	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

	if (ss->ss_fft_period != HAL_SPECTRAL_PARAM_NOVAL) {
		val &= ~AR_PHY_SPECTRAL_SCAN_FFT_PERIOD;
		val |= SM(ss->ss_fft_period, AR_PHY_SPECTRAL_SCAN_FFT_PERIOD);
	}

	if (ss->ss_period != HAL_SPECTRAL_PARAM_NOVAL) {
		val &= ~AR_PHY_SPECTRAL_SCAN_PERIOD;
		val |= SM(ss->ss_period, AR_PHY_SPECTRAL_SCAN_PERIOD);
	}

	if (ss->ss_period != HAL_SPECTRAL_PARAM_NOVAL) {
		val &= ~AR_PHY_SPECTRAL_SCAN_PERIOD;
		val |= SM(ss->ss_period, AR_PHY_SPECTRAL_SCAN_PERIOD);
	}

	/* This section is different for Kiwi and Merlin */
	if (AR_SREV_MERLIN(ah) ) {
		if (ss->ss_count != HAL_SPECTRAL_PARAM_NOVAL) {
			val &= ~AR_PHY_SPECTRAL_SCAN_COUNT;
			val |= SM(ss->ss_count, AR_PHY_SPECTRAL_SCAN_COUNT);
		}

		if (ss->ss_short_report == AH_TRUE) {
			val |= AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
		} else if (ss->ss_short_report != HAL_SPECTRAL_PARAM_NOVAL) {
			val &= ~AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
		}
	} else {
		if (ss->ss_count != HAL_SPECTRAL_PARAM_NOVAL) {
			/*
			 * In Merlin, for continuous scan, scan_count = 128.
			 * In case of Kiwi, this value should be 0
			 */
			if (ss->ss_count == 128)
				ss->ss_count = 0;
			val &= ~AR_PHY_SPECTRAL_SCAN_COUNT_KIWI;
			val |= SM(ss->ss_count, AR_PHY_SPECTRAL_SCAN_COUNT_KIWI);
		}

		if (ss->ss_short_report == AH_TRUE) {
			val |= AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT_KIWI;
		} else if (ss->ss_short_report != HAL_SPECTRAL_PARAM_NOVAL) {
			val &= ~AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT_KIWI;
		}

		//Select the mask to be same as before
		val |= AR_PHY_SPECTRAL_SCAN_PHYERR_MASK_SELECT_KIWI;
	}
	// Enable spectral scan
	OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val | AR_PHY_SPECTRAL_SCAN_ENA);

	ar5416GetSpectralParams(ah, ss);
}

/*
 * Get the spectral parameter values and return them in the pe
 * structure
 */
void
ar5416GetSpectralParams(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

	ss->ss_fft_period = MS(val, AR_PHY_SPECTRAL_SCAN_FFT_PERIOD);
	ss->ss_period = MS(val, AR_PHY_SPECTRAL_SCAN_PERIOD);
	if (AR_SREV_MERLIN(ah) ) {
		ss->ss_count = MS(val, AR_PHY_SPECTRAL_SCAN_COUNT);
		ss->ss_short_report = MS(val, AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT);
	} else {
		ss->ss_count = MS(val, AR_PHY_SPECTRAL_SCAN_COUNT_KIWI);
		ss->ss_short_report = MS(val, AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT_KIWI);
	}
	val = OS_REG_READ(ah, AR_PHY_RADAR_1);
	ss->radar_bin_thresh_sel = MS(val, AR_PHY_RADAR_1_BIN_THRESH_SELECT);
}

HAL_BOOL
ar5416IsSpectralActive(struct ath_hal *ah)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	return MS(val, AR_PHY_SPECTRAL_SCAN_ACTIVE);
}

HAL_BOOL
ar5416IsSpectralEnabled(struct ath_hal *ah)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	return MS(val,AR_PHY_SPECTRAL_SCAN_ENA);
}

void
ar5416StartSpectralScan(struct ath_hal *ah)
{
	uint32_t val;

	ar5416PrepSpectralScan(ah);

	// Activate spectral scan
	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	val |= AR_PHY_SPECTRAL_SCAN_ENA;
	val |= AR_PHY_SPECTRAL_SCAN_ACTIVE;
	OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	val = OS_REG_READ(ah, AR_PHY_ERR_MASK_REG);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_REG, val | AR_PHY_ERR_RADAR);
}

void
ar5416StopSpectralScan(struct ath_hal *ah)
{
	uint32_t val;
	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

	// Deactivate spectral scan
	val &= ~AR_PHY_SPECTRAL_SCAN_ENA;
	val &= ~AR_PHY_SPECTRAL_SCAN_ACTIVE;
	OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	val = OS_REG_READ(ah, AR_PHY_ERR_MASK_REG) & (~AR_PHY_ERR_RADAR);
	OS_REG_WRITE(ah, AR_PHY_ERR_MASK_REG, val);
}

uint32_t
ar5416GetSpectralConfig(struct ath_hal *ah)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
	return val;
}

void
ar5416RestoreSpectralConfig(struct ath_hal *ah, uint32_t restoreval)
{
	uint32_t curval;

	ar5416PrepSpectralScan(ah);

	curval = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

	if (restoreval != curval) {
		restoreval |= AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
		OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, restoreval);
	}
	return;
}

