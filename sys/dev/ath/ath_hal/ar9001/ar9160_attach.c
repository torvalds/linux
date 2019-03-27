/*-
 * SPDX-License-Identifier: ISC
 *
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

#include "ar9001/ar9160.ini"

static const HAL_PERCAL_DATA ar9160_iq_cal = {		/* multi sample */
	.calName = "IQ", .calType = IQ_MISMATCH_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416IQCalCollect,
	.calPostProc	= ar5416IQCalibration
};
static const HAL_PERCAL_DATA ar9160_adc_gain_cal = {	/* multi sample */
	.calName = "ADC Gain", .calType = ADC_GAIN_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcGainCalCollect,
	.calPostProc	= ar5416AdcGainCalibration
};
static const HAL_PERCAL_DATA ar9160_adc_dc_cal = {	/* multi sample */
	.calName = "ADC DC", .calType = ADC_DC_CAL,
	.calNumSamples	= MAX_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};
static const HAL_PERCAL_DATA ar9160_adc_init_dc_cal = {
	.calName = "ADC Init DC", .calType = ADC_DC_INIT_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= INIT_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};

static HAL_BOOL ar9160FillCapabilityInfo(struct ath_hal *ah);

static void
ar9160AniSetup(struct ath_hal *ah)
{
	static const struct ar5212AniParams aniparams = {
		.maxNoiseImmunityLevel	= 4,	/* levels 0..4 */
		.totalSizeDesired	= { -55, -55, -55, -55, -62 },
		.coarseHigh		= { -14, -14, -14, -14, -12 },
		.coarseLow		= { -64, -64, -64, -64, -70 },
		.firpwr			= { -78, -78, -78, -78, -80 },
		.maxSpurImmunityLevel	= 7,
		.cycPwrThr1		= { 2, 4, 6, 8, 10, 12, 14, 16 },
		.maxFirstepLevel	= 2,	/* levels 0..2 */
		.firstep		= { 0, 4, 8 },
		.ofdmTrigHigh		= 500,
		.ofdmTrigLow		= 200,
		.cckTrigHigh		= 200,
		.cckTrigLow		= 100,
		.rssiThrHigh		= 40,
		.rssiThrLow		= 7,
		.period			= 100,
	};

	/* NB: disable ANI noise immmunity for reliable RIFS rx */
	AH5416(ah)->ah_ani_function &= ~(1 << HAL_ANI_NOISE_IMMUNITY_LEVEL);
	ar5416AniAttach(ah, &aniparams, &aniparams, AH_TRUE);
}

static void 
ar9160InitPLL(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t pll = SM(0x5, AR_RTC_SOWL_PLL_REFDIV);
	if (chan != AH_NULL) {
		if (IEEE80211_IS_CHAN_HALF(chan))
			pll |= SM(0x1, AR_RTC_SOWL_PLL_CLKSEL);
		else if (IEEE80211_IS_CHAN_QUARTER(chan))
			pll |= SM(0x2, AR_RTC_SOWL_PLL_CLKSEL);

		if (IEEE80211_IS_CHAN_5GHZ(chan))
			pll |= SM(0x50, AR_RTC_SOWL_PLL_DIV);
		else
			pll |= SM(0x58, AR_RTC_SOWL_PLL_DIV);
	} else
		pll |= SM(0x58, AR_RTC_SOWL_PLL_DIV);

	OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);
	OS_DELAY(RTC_PLL_SETTLE_DELAY);
	OS_REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_SLEEP_DERIVED_CLK);
}

/*
 * Attach for an AR9160 part.
 */
static struct ath_hal *
ar9160Attach(uint16_t devid, HAL_SOFTC sc,
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

	/* XXX override with 9160 specific state */
	/* override 5416 methods for our needs */
	AH5416(ah)->ah_initPLL = ar9160InitPLL;

	AH5416(ah)->ah_cal.iqCalData.calData = &ar9160_iq_cal;
	AH5416(ah)->ah_cal.adcGainCalData.calData = &ar9160_adc_gain_cal;
	AH5416(ah)->ah_cal.adcDcCalData.calData = &ar9160_adc_dc_cal;
	AH5416(ah)->ah_cal.adcDcCalInitData.calData = &ar9160_adc_init_dc_cal;
	AH5416(ah)->ah_cal.suppCals = ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;

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
	val = OS_REG_READ(ah, AR_SREV);
	HALDEBUG(ah, HAL_DEBUG_ATTACH,
	    "%s: ID 0x%x VERSION 0x%x TYPE 0x%x REVISION 0x%x\n",
	    __func__, MS(val, AR_XSREV_ID), MS(val, AR_XSREV_VERSION),
	    MS(val, AR_XSREV_TYPE), MS(val, AR_XSREV_REVISION));
	/* NB: include chip type to differentiate from pre-Sowl versions */
	AH_PRIVATE(ah)->ah_macVersion =
	    (val & AR_XSREV_VERSION) >> AR_XSREV_TYPE_S;
	AH_PRIVATE(ah)->ah_macRev = MS(val, AR_XSREV_REVISION);
	AH_PRIVATE(ah)->ah_ispcie = (val & AR_XSREV_TYPE_HOST_MODE) == 0;

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar9160Modes, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar9160Common, 2);

	HAL_INI_INIT(&AH5416(ah)->ah_ini_bb_rfgain, ar9160BB_RfGain, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank0, ar9160Bank0, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank1, ar9160Bank1, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank2, ar9160Bank2, 2);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank3, ar9160Bank3, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank6, ar9160Bank6TPC, 3);
	HAL_INI_INIT(&AH5416(ah)->ah_ini_bank7, ar9160Bank7, 2);
	if (AR_SREV_SOWL_11(ah))
		HAL_INI_INIT(&AH5416(ah)->ah_ini_addac, ar9160Addac_1_1, 2);
	else
		HAL_INI_INIT(&AH5416(ah)->ah_ini_addac, ar9160Addac, 2);

	ecode = ath_hal_v14EepromAttach(ah);
	if (ecode != HAL_OK)
		goto bad;

	HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes, ar9160PciePhy, 2);
	ar5416AttachPCIE(ah);

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
	if (!ar9160FillCapabilityInfo(ah)) {
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
		OS_REG_WRITE(ah, AR_MISC_MODE, OS_REG_READ(ah, AR_MISC_MODE) | ahp->ah_miscMode);

	ar9160AniSetup(ah);			/* Anti Noise Immunity */

	/* This just uses the AR5416 NF values */
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
ar9160FillCapabilityInfo(struct ath_hal *ah)
{
	HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;

	if (!ar5416FillCapabilityInfo(ah))
		return AH_FALSE;
	pCap->halCSTSupport = AH_TRUE;
	pCap->halRifsRxSupport = AH_TRUE;
	pCap->halRifsTxSupport = AH_TRUE;
	pCap->halRtsAggrLimit = 64*1024;	/* 802.11n max */
	pCap->halExtChanDfsSupport = AH_TRUE;
	pCap->halUseCombinedRadarRssi = AH_TRUE;
	pCap->halAutoSleepSupport = AH_FALSE;	/* XXX? */
	pCap->halMbssidAggrSupport = AH_TRUE;
	pCap->hal4AddrAggrSupport = AH_TRUE;
	/* BB Read WAR */
	pCap->halHasBBReadWar = AH_TRUE;

	/* AR9160 is a 2x2 stream device */
	pCap->halTxStreams = 2;
	pCap->halRxStreams = 2;

	return AH_TRUE;
}

static const char*
ar9160Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID && devid == AR9160_DEVID_PCI)
		return "Atheros 9160";
	return AH_NULL;
}
AH_CHIP(AR9160, ar9160Probe, ar9160Attach);
