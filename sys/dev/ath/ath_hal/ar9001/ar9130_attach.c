/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2011 Adrian Chadd, Xenion Pty Ltd
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Atheros Communications, Inc.
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

#include "ar9001/ar9130reg.h"
#include "ar9001/ar9130_phy.h"
#include "ar9001/ar9130_eeprom.h"

#include "ar9001/ar9130.ini"

static const HAL_PERCAL_DATA ar9130_iq_cal = {		/* multi sample */
	.calName = "IQ", .calType = IQ_MISMATCH_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416IQCalCollect,
	.calPostProc	= ar5416IQCalibration
};
static const HAL_PERCAL_DATA ar9130_adc_gain_cal = {	/* multi sample */
	.calName = "ADC Gain", .calType = ADC_GAIN_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcGainCalCollect,
	.calPostProc	= ar5416AdcGainCalibration
};
static const HAL_PERCAL_DATA ar9130_adc_dc_cal = {	/* multi sample */
	.calName = "ADC DC", .calType = ADC_DC_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};
static const HAL_PERCAL_DATA ar9130_adc_init_dc_cal = {
	.calName = "ADC Init DC", .calType = ADC_DC_INIT_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= INIT_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};

static HAL_BOOL ar9130FillCapabilityInfo(struct ath_hal *ah);

/*
 * Attach for an AR9130 part.
 */
static struct ath_hal *
ar9130Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config,
	HAL_STATUS *status)
{
	struct ath_hal_5416 *ahp5416;
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;
	uint32_t val;
	HAL_STATUS ecode;
	HAL_BOOL rfStatus;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp5416 = ath_hal_malloc(sizeof (struct ath_hal_5416));
	if (ahp5416 == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ar5416InitState(ahp5416, devid, sc, st, sh, status);
	ahp = &ahp5416->ah_5212;
	ah = &ahp->ah_priv.h;

	/* XXX override with 9100 specific state */
	AH5416(ah)->ah_initPLL = ar9130InitPLL;
	/* XXX should force chainmasks to 0x7, as per ath9k calibration bugs */

	/* override 5416 methods for our needs */

	AH5416(ah)->ah_cal.iqCalData.calData = &ar9130_iq_cal;
	AH5416(ah)->ah_cal.adcGainCalData.calData = &ar9130_adc_gain_cal;
	AH5416(ah)->ah_cal.adcDcCalData.calData = &ar9130_adc_dc_cal;
	AH5416(ah)->ah_cal.adcDcCalInitData.calData = &ar9130_adc_init_dc_cal;
	AH5416(ah)->ah_cal.suppCals = ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;

	/*
	 * This was hard-set because the initial ath9k port of this
	 * code kept their runtime conditional register #defines.
	 * AR_SREV and the RTC registers have shifted for Howl;
	 * they detected this and changed the values at runtime.
	 * The current port doesn't yet do this; it may do at a
	 * later stage, so this is set early so any routines which
	 * manipulate the registers have ah_macVersion set to base
	 * the above decision upon.
	 */
	AH_PRIVATE((ah))->ah_macVersion = AR_XSREV_VERSION_HOWL;

	/*
	 * Use the "local" EEPROM data given to us by the higher layers.
	 * This is a private copy out of system flash.
	 * By this stage the SoC SPI flash may have disabled the memory-
	 * mapping and rely purely on port-based SPI IO.
	 */
	AH_PRIVATE((ah))->ah_eepromRead = ath_hal_EepromDataRead;
	AH_PRIVATE((ah))->ah_eepromWrite = NULL;
	ah->ah_eepromdata = eepromdata;

	if (!ar5416SetResetReg(ah, HAL_RESET_POWER_ON)) {
		/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: couldn't reset chip\n",
		    __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	if (!ar5416SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: couldn't wakeup chip\n",
		    __func__);
		ecode = HAL_EIO;
		goto bad;
	}
	/* Read Revisions from Chips before taking out of reset */
	val = OS_REG_READ(ah, AR_SREV_CHIP_HOWL) & AR_SREV_CHIP_HOWL_ID;

	/* XXX are these values even valid for the mac/radio revision? -adrian */
	HALDEBUG(ah, HAL_DEBUG_ATTACH,
	    "%s: ID 0x%x VERSION 0x%x TYPE 0x%x REVISION 0x%x\n",
	    __func__, MS(val, AR_XSREV_ID), MS(val, AR_XSREV_VERSION),
	    MS(val, AR_XSREV_TYPE), MS(val, AR_XSREV_REVISION));
	AH_PRIVATE(ah)->ah_macRev = MS(val, AR_XSREV_REVISION);
	AH_PRIVATE(ah)->ah_ispcie = 0;

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar5416Modes_9100, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar5416Common_9100, 2);

	HAL_INI_INIT(&AH5416(ah)->ah_ini_bb_rfgain, ar5416BB_RfGain_9100, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank0, ar5416Bank0_9100, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank1, ar5416Bank1_9100, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank2, ar5416Bank2_9100, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank3, ar5416Bank3_9100, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank6, ar5416Bank6TPC_9100, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank7, ar5416Bank7_9100, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_addac, ar5416Addac_9100, 2);

	ecode = ath_hal_v14EepromAttach(ah);
	if (ecode != HAL_OK)
		goto bad;

	if (!ar5416ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}

	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);

	if (!ar5212ChipTest(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: hardware self-test failed\n",
		    __func__);
		ecode = HAL_ESELFTEST;
		goto bad;
	}

	/*
	 * Set correct Baseband to analog shift
	 * setting to access analog chips.
	 */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	/* Read Radio Chip Rev Extract */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ar5416GetRadioRev(ah);
	switch (AH_PRIVATE(ah)->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR) {
        case AR_RAD2133_SREV_MAJOR:	/* Sowl: 2G/3x3 */
	case AR_RAD5133_SREV_MAJOR:	/* Sowl: 2+5G/3x3 */
		break;
	default:
		if (AH_PRIVATE(ah)->ah_analog5GhzRev == 0) {
			AH_PRIVATE(ah)->ah_analog5GhzRev =
				AR_RAD5133_SREV_MAJOR;
			break;
		}
#ifdef AH_DEBUG
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: 5G Radio Chip Rev 0x%02X is not supported by "
		    "this driver\n", __func__,
		    AH_PRIVATE(ah)->ah_analog5GhzRev);
		ecode = HAL_ENOTSUPP;
		goto bad;
#endif
	}
	rfStatus = ar2133RfAttach(ah, &ecode);
	if (!rfStatus) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar9130FillCapabilityInfo(ah)) {
		ecode = HAL_EEREAD;
		goto bad;
	}

	ecode = ath_hal_eepromGet(ah, AR_EEP_MACADDR, ahp->ah_macaddr);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error getting mac address from EEPROM\n", __func__);
		goto bad;
        }
	/* XXX How about the serial number ? */
	/* Read Reg Domain */
	AH_PRIVATE(ah)->ah_currentRD =
	    ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, AH_NULL);
	AH_PRIVATE(ah)->ah_currentRDext =
	    ath_hal_eepromGet(ah, AR_EEP_REGDMN_1, AH_NULL);


	/*
	 * ah_miscMode is populated by ar5416FillCapabilityInfo()
	 * starting from griffin. Set here to make sure that
	 * AR_MISC_MODE_MIC_NEW_LOC_ENABLE is set before a GTK is
	 * placed into hardware.
	 */
	if (ahp->ah_miscMode != 0)
		OS_REG_WRITE(ah, AR_MISC_MODE, ahp->ah_miscMode);

	/* XXX no ANI for AR9130 */
	AH5416(ah)->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_5416_2GHZ;
	AH5416(ah)->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_5416_2GHZ;
	AH5416(ah)->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_5416_2GHZ;
	AH5416(ah)->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_5416_5GHZ;
	AH5416(ah)->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_5416_5GHZ;
	AH5416(ah)->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_5416_5GHZ;

	ar5416InitNfHistBuff(AH5416(ah)->ah_cal.nfCalHist);

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;
bad:
	if (ahp)
		ar5416Detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
static HAL_BOOL
ar9130FillCapabilityInfo(struct ath_hal *ah)
{
	HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: begin\n", __func__);
	if (!ar5416FillCapabilityInfo(ah))
		return AH_FALSE;
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: fill'ed; now setting\n", __func__);
	pCap->halCSTSupport = AH_TRUE;
	pCap->halRifsRxSupport = AH_TRUE;
	pCap->halRifsTxSupport = AH_TRUE;
	pCap->halRtsAggrLimit = 64*1024;	/* 802.11n max */
	pCap->halExtChanDfsSupport = AH_TRUE;
	pCap->halUseCombinedRadarRssi = AH_TRUE;
	pCap->halAutoSleepSupport = AH_FALSE;	/* XXX? */
	/*
	 * MBSSID aggregation is broken in Howl v1.1, v1.2, v1.3
	 * and works fine in v1.4.
	 * XXX todo, enable it for v1.4.
	 */
	pCap->halMbssidAggrSupport = AH_FALSE;
	pCap->hal4AddrAggrSupport = AH_TRUE;
	/* BB Read WAR */
	pCap->halHasBBReadWar = AH_TRUE;

	/*
	 * Implement the PLL/config changes needed for half/quarter
	 * rates before re-enabling them here.
	 */
	pCap->halChanHalfRate = AH_FALSE;
	pCap->halChanQuarterRate = AH_FALSE;

	return AH_TRUE;
}

static const char*
ar9130Probe(uint16_t vendorid, uint16_t devid)
{
        if (vendorid == ATHEROS_VENDOR_ID && devid == AR5416_AR9130_DEVID)
                return "Atheros 9130";
	return AH_NULL;
}
AH_CHIP(AR9130, ar9130Probe, ar9130Attach);
