/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2008-2009 Sam Leffler, Errno Consulting
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

#include "ah_eeprom_v14.h"		/* XXX for tx/rx gain */

#include "ar9002/ar9280.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#include "ar9002/ar9280v1.ini"
#include "ar9002/ar9280v2.ini"
#include "ar9002/ar9280_olc.h"

static const HAL_PERCAL_DATA ar9280_iq_cal = {		/* single sample */
	.calName = "IQ", .calType = IQ_MISMATCH_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MAX_LOG_COUNT,
	.calCollect	= ar5416IQCalCollect,
	.calPostProc	= ar5416IQCalibration
};
static const HAL_PERCAL_DATA ar9280_adc_gain_cal = {	/* single sample */
	.calName = "ADC Gain", .calType = ADC_GAIN_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MAX_LOG_COUNT,
	.calCollect	= ar5416AdcGainCalCollect,
	.calPostProc	= ar5416AdcGainCalibration
};
static const HAL_PERCAL_DATA ar9280_adc_dc_cal = {	/* single sample */
	.calName = "ADC DC", .calType = ADC_DC_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MAX_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};
static const HAL_PERCAL_DATA ar9280_adc_init_dc_cal = {
	.calName = "ADC Init DC", .calType = ADC_DC_INIT_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= INIT_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};

static void ar9280ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_off);
static void ar9280DisablePCIE(struct ath_hal *ah);
static HAL_BOOL ar9280FillCapabilityInfo(struct ath_hal *ah);
static void ar9280WriteIni(struct ath_hal *ah,
	const struct ieee80211_channel *chan);

static void
ar9280AniSetup(struct ath_hal *ah)
{
	/*
	 * These are the parameters from the AR5416 ANI code;
	 * they likely need quite a bit of adjustment for the
	 * AR9280.
	 */
        static const struct ar5212AniParams aniparams = {
                .maxNoiseImmunityLevel  = 4,    /* levels 0..4 */
                .totalSizeDesired       = { -55, -55, -55, -55, -62 },
                .coarseHigh             = { -14, -14, -14, -14, -12 },
                .coarseLow              = { -64, -64, -64, -64, -70 },
                .firpwr                 = { -78, -78, -78, -78, -80 },
                .maxSpurImmunityLevel   = 7,
                .cycPwrThr1             = { 2, 4, 6, 8, 10, 12, 14, 16 },
                .maxFirstepLevel        = 2,    /* levels 0..2 */
                .firstep                = { 0, 4, 8 },
                .ofdmTrigHigh           = 500,
                .ofdmTrigLow            = 200,
                .cckTrigHigh            = 200,
                .cckTrigLow             = 100,
                .rssiThrHigh            = 40,
                .rssiThrLow             = 7,
                .period                 = 100,
        };
	/* NB: disable ANI noise immmunity for reliable RIFS rx */
	AH5416(ah)->ah_ani_function &= ~(1 << HAL_ANI_NOISE_IMMUNITY_LEVEL);

        /* NB: ANI is not enabled yet */
        ar5416AniAttach(ah, &aniparams, &aniparams, AH_TRUE);
}

void
ar9280InitPLL(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t pll = SM(0x5, AR_RTC_SOWL_PLL_REFDIV);

	if (AR_SREV_MERLIN_20(ah) &&
	    chan != AH_NULL && IEEE80211_IS_CHAN_5GHZ(chan)) {
		/*
		 * PLL WAR for Merlin 2.0/2.1
		 * When doing fast clock, set PLL to 0x142c
		 * Else, set PLL to 0x2850 to prevent reset-to-reset variation 
		 */
		pll = IS_5GHZ_FAST_CLOCK_EN(ah, chan) ? 0x142c : 0x2850;
		if (IEEE80211_IS_CHAN_HALF(chan))
			pll |= SM(0x1, AR_RTC_SOWL_PLL_CLKSEL);
		else if (IEEE80211_IS_CHAN_QUARTER(chan))
			pll |= SM(0x2, AR_RTC_SOWL_PLL_CLKSEL);
	} else if (AR_SREV_MERLIN_10_OR_LATER(ah)) {
		pll = SM(0x5, AR_RTC_SOWL_PLL_REFDIV);
		if (chan != AH_NULL) {
			if (IEEE80211_IS_CHAN_HALF(chan))
				pll |= SM(0x1, AR_RTC_SOWL_PLL_CLKSEL);
			else if (IEEE80211_IS_CHAN_QUARTER(chan))
				pll |= SM(0x2, AR_RTC_SOWL_PLL_CLKSEL);
			if (IEEE80211_IS_CHAN_5GHZ(chan))
				pll |= SM(0x28, AR_RTC_SOWL_PLL_DIV);
			else
				pll |= SM(0x2c, AR_RTC_SOWL_PLL_DIV);
		} else
			pll |= SM(0x2c, AR_RTC_SOWL_PLL_DIV);
	}

	OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);
	OS_DELAY(RTC_PLL_SETTLE_DELAY);
	OS_REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_SLEEP_DERIVED_CLK);
}

/* XXX shouldn't be here! */
#define	EEP_MINOR(_ah) \
	(AH_PRIVATE(_ah)->ah_eeversion & AR5416_EEP_VER_MINOR_MASK)

/*
 * Attach for an AR9280 part.
 */
static struct ath_hal *
ar9280Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config,
	HAL_STATUS *status)
{
	struct ath_hal_9280 *ahp9280;
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;
	uint32_t val;
	HAL_STATUS ecode;
	HAL_BOOL rfStatus;
	int8_t pwr_table_offset;
	uint8_t pwr;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp9280 = ath_hal_malloc(sizeof (struct ath_hal_9280));
	if (ahp9280 == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ahp = AH5212(ahp9280);
	ah = &ahp->ah_priv.h;

	ar5416InitState(AH5416(ah), devid, sc, st, sh, status);

	/*
	 * Use the "local" EEPROM data given to us by the higher layers.
	 * This is a private copy out of system flash. The Linux ath9k
	 * commit for the initial AR9130 support mentions MMIO flash
	 * access is "unreliable." -adrian
	 */
	if (eepromdata != AH_NULL) {
		AH_PRIVATE((ah))->ah_eepromRead = ath_hal_EepromDataRead;
		AH_PRIVATE((ah))->ah_eepromWrite = NULL;
		ah->ah_eepromdata = eepromdata;
	}

	/* XXX override with 9280 specific state */
	/* override 5416 methods for our needs */
	AH5416(ah)->ah_initPLL = ar9280InitPLL;

	ah->ah_setAntennaSwitch		= ar9280SetAntennaSwitch;
	ah->ah_configPCIE		= ar9280ConfigPCIE;
	ah->ah_disablePCIE		= ar9280DisablePCIE;

	AH5416(ah)->ah_cal.iqCalData.calData = &ar9280_iq_cal;
	AH5416(ah)->ah_cal.adcGainCalData.calData = &ar9280_adc_gain_cal;
	AH5416(ah)->ah_cal.adcDcCalData.calData = &ar9280_adc_dc_cal;
	AH5416(ah)->ah_cal.adcDcCalInitData.calData = &ar9280_adc_init_dc_cal;
	AH5416(ah)->ah_cal.suppCals = ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;

	AH5416(ah)->ah_spurMitigate	= ar9280SpurMitigate;
	AH5416(ah)->ah_writeIni		= ar9280WriteIni;
	AH5416(ah)->ah_olcInit		= ar9280olcInit;
	AH5416(ah)->ah_olcTempCompensation = ar9280olcTemperatureCompensation;
	AH5416(ah)->ah_setPowerCalTable	= ar9280SetPowerCalTable;

	AH5416(ah)->ah_rx_chainmask	= AR9280_DEFAULT_RXCHAINMASK;
	AH5416(ah)->ah_tx_chainmask	= AR9280_DEFAULT_TXCHAINMASK;

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
	if (AR_SREV_MERLIN_20_OR_LATER(ah)) {
		HAL_INI_INIT(&ahp->ah_ini_modes, ar9280Modes_v2, 6);
		HAL_INI_INIT(&ahp->ah_ini_common, ar9280Common_v2, 2);
		HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes,
		    ar9280PciePhy_clkreq_always_on_L1_v2, 2);
		HAL_INI_INIT(&ahp9280->ah_ini_xmodes,
		    ar9280Modes_fast_clock_v2, 3);
	} else {
		HAL_INI_INIT(&ahp->ah_ini_modes, ar9280Modes_v1, 6);
		HAL_INI_INIT(&ahp->ah_ini_common, ar9280Common_v1, 2);
		HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes,
		    ar9280PciePhy_v1, 2);
	}
	ar5416AttachPCIE(ah);

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
	rfStatus = ar9280RfAttach(ah, &ecode);
	if (!rfStatus) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}

	/* Enable fixup for AR_AN_TOP2 if necessary */
	/*
	 * The v14 EEPROM layer returns HAL_EIO if PWDCLKIND isn't supported
	 * by the EEPROM version.
	 *
	 * ath9k checks the EEPROM minor version is >= 0x0a here, instead of
	 * the abstracted EEPROM access layer.
	 */
	ecode = ath_hal_eepromGet(ah, AR_EEP_PWDCLKIND, &pwr);
	if (AR_SREV_MERLIN_20_OR_LATER(ah) && ecode == HAL_OK && pwr == 0) {
		printf("[ath] enabling AN_TOP2_FIXUP\n");
		AH5416(ah)->ah_need_an_top2_fixup = 1;
	}

        /*
         * Check whether the power table offset isn't the default.
         * This can occur with eeprom minor V21 or greater on Merlin.
         */
	(void) ath_hal_eepromGet(ah, AR_EEP_PWR_TABLE_OFFSET, &pwr_table_offset);
	if (pwr_table_offset != AR5416_PWR_TABLE_OFFSET_DB)
		ath_hal_printf(ah, "[ath]: default pwr offset: %d dBm != EEPROM pwr offset: %d dBm; curves will be adjusted.\n",
		    AR5416_PWR_TABLE_OFFSET_DB, (int) pwr_table_offset);

	/* XXX check for >= minor ver 17 */
	if (AR_SREV_MERLIN_20(ah)) {
		/* setup rxgain table */
		switch (ath_hal_eepromGet(ah, AR_EEP_RXGAIN_TYPE, AH_NULL)) {
		case AR5416_EEP_RXGAIN_13dB_BACKOFF:
			HAL_INI_INIT(&ahp9280->ah_ini_rxgain,
			    ar9280Modes_backoff_13db_rxgain_v2, 6);
			break;
		case AR5416_EEP_RXGAIN_23dB_BACKOFF:
			HAL_INI_INIT(&ahp9280->ah_ini_rxgain,
			    ar9280Modes_backoff_23db_rxgain_v2, 6);
			break;
		case AR5416_EEP_RXGAIN_ORIG:
			HAL_INI_INIT(&ahp9280->ah_ini_rxgain,
			    ar9280Modes_original_rxgain_v2, 6);
			break;
		default:
			HALASSERT(AH_FALSE);
			goto bad;		/* XXX ? try to continue */
		}
	}

	/* XXX check for >= minor ver 19 */
	if (AR_SREV_MERLIN_20(ah)) {
		/* setp txgain table */
		switch (ath_hal_eepromGet(ah, AR_EEP_TXGAIN_TYPE, AH_NULL)) {
		case AR5416_EEP_TXGAIN_HIGH_POWER:
			HAL_INI_INIT(&ahp9280->ah_ini_txgain,
			    ar9280Modes_high_power_tx_gain_v2, 6);
			break;
		case AR5416_EEP_TXGAIN_ORIG:
			HAL_INI_INIT(&ahp9280->ah_ini_txgain,
			    ar9280Modes_original_tx_gain_v2, 6);
			break;
		default:
			HALASSERT(AH_FALSE);
			goto bad;		/* XXX ? try to continue */
		}
	}

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar9280FillCapabilityInfo(ah)) {
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

	ar9280AniSetup(ah);			/* Anti Noise Immunity */

	/* Setup noise floor min/max/nominal values */
	AH5416(ah)->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9280_2GHZ;
	AH5416(ah)->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9280_2GHZ;
	AH5416(ah)->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9280_2GHZ;
	AH5416(ah)->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_9280_5GHZ;
	AH5416(ah)->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9280_5GHZ;
	AH5416(ah)->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9280_5GHZ;

	ar5416InitNfHistBuff(AH5416(ah)->ah_cal.nfCalHist);

	HALDEBUG(ah, HAL_DEBUG_ATTACH, "%s: return\n", __func__);

	return ah;
bad:
	if (ah != AH_NULL)
		ah->ah_detach(ah);
	if (status)
		*status = ecode;
	return AH_NULL;
}

static void
ar9280ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{
	uint32_t val;

	if (AH_PRIVATE(ah)->ah_ispcie && !restore) {
		ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_pcieserdes, 1, 0);
		OS_DELAY(1000);
	}


	/*
	 * Set PCIe workaround bits
	 *
	 * NOTE:
	 *
	 * In Merlin and Kite, bit 14 in WA register (disable L1) should only
	 * be set when device enters D3 and be cleared when device comes back
	 * to D0.
	 */
	if (power_off) {		/* Power-off */
		OS_REG_CLR_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);

		val = OS_REG_READ(ah, AR_WA);

		/*
		 * Disable bit 6 and 7 before entering D3 to prevent
		 * system hang.
		 */
		val &= ~(AR_WA_BIT6 | AR_WA_BIT7);

		/*
		 * XXX Not sure, is specified in the reference HAL.
		 */
		val |= AR_WA_BIT22;

		/*
		 * See above: set AR_WA_D3_L1_DISABLE when entering D3 state.
		 *
		 * XXX The reference HAL does it this way - it only sets
		 * AR_WA_D3_L1_DISABLE if it's set in AR9280_WA_DEFAULT,
		 * which it (currently) isn't.  So the following statement
		 * is currently a NOP.
		 */
		if (AR9280_WA_DEFAULT & AR_WA_D3_L1_DISABLE)
			val |= AR_WA_D3_L1_DISABLE;

		OS_REG_WRITE(ah, AR_WA, val);
	} else {			/* Power-on */
		val = AR9280_WA_DEFAULT;

		/*
		 * See note above: make sure L1_DISABLE is not set.
		 */
		val &= (~AR_WA_D3_L1_DISABLE);
		OS_REG_WRITE(ah, AR_WA, val);

		/* set bit 19 to allow forcing of pcie core into L1 state */
		OS_REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
	}
}

static void
ar9280DisablePCIE(struct ath_hal *ah)
{
}

static void
ar9280WriteIni(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	u_int modesIndex, freqIndex;
	int regWrites = 0;
	int i;
	const HAL_INI_ARRAY *ia;

	/* Setup the indices for the next set of register array writes */
	/* XXX Ignore 11n dynamic mode on the AR5416 for the moment */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		freqIndex = 2;
		if (IEEE80211_IS_CHAN_HT40(chan))
			modesIndex = 3;
		else if (IEEE80211_IS_CHAN_108G(chan))
			modesIndex = 5;
		else
			modesIndex = 4;
	} else {
		freqIndex = 1;
		if (IEEE80211_IS_CHAN_HT40(chan) ||
		    IEEE80211_IS_CHAN_TURBO(chan))
			modesIndex = 2;
		else
			modesIndex = 1;
	}

	/* Set correct Baseband to analog shift setting to access analog chips. */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);
	OS_REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	/*
	 * This is unwound because at the moment, there's a requirement
	 * for Merlin (and later, perhaps) to have a specific bit fixed
	 * in the AR_AN_TOP2 register before writing it.
	 */
	ia = &AH5212(ah)->ah_ini_modes;
#if 0
	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_modes,
	    modesIndex, regWrites);
#endif
	HALASSERT(modesIndex < ia->cols);
	for (i = 0; i < ia->rows; i++) {
		uint32_t reg = HAL_INI_VAL(ia, i, 0);
		uint32_t val = HAL_INI_VAL(ia, i, modesIndex);

		if (reg == AR_AN_TOP2 && AH5416(ah)->ah_need_an_top2_fixup)
			val &= ~AR_AN_TOP2_PWDCLKIND;

		OS_REG_WRITE(ah, reg, val);

		/* Analog shift register delay seems needed for Merlin - PR kern/154220 */
		if (reg >= 0x7800 && reg < 0x7900)
			OS_DELAY(100);

		DMA_YIELD(regWrites);
	}

	if (AR_SREV_MERLIN_20_OR_LATER(ah)) {
		regWrites = ath_hal_ini_write(ah, &AH9280(ah)->ah_ini_rxgain,
		    modesIndex, regWrites);
		regWrites = ath_hal_ini_write(ah, &AH9280(ah)->ah_ini_txgain,
		    modesIndex, regWrites);
	}
	/* XXX Merlin 100us delay for shift registers */
	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_common,
	    1, regWrites);

	if (AR_SREV_MERLIN_20(ah) && IS_5GHZ_FAST_CLOCK_EN(ah, chan)) {
		/* 5GHz channels w/ Fast Clock use different modal values */
		regWrites = ath_hal_ini_write(ah, &AH9280(ah)->ah_ini_xmodes,
		    modesIndex, regWrites);
	}
}

#define	AR_BASE_FREQ_2GHZ	2300
#define	AR_BASE_FREQ_5GHZ	4900
#define	AR_SPUR_FEEQ_BOUND_HT40	19
#define	AR_SPUR_FEEQ_BOUND_HT20	10

void
ar9280SpurMitigate(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
    static const int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
                AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60 };
    static const int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
                AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60 };
    static int inc[4] = { 0, 100, 0, 0 };

    int bb_spur = AR_NO_SPUR;
    int freq;
    int bin, cur_bin;
    int bb_spur_off, spur_subchannel_sd;
    int spur_freq_sd;
    int spur_delta_phase;
    int denominator;
    int upper, lower, cur_vit_mask;
    int tmp, newVal;
    int i;
    CHAN_CENTERS centers;

    int8_t mask_m[123];
    int8_t mask_p[123];
    int8_t mask_amt;
    int tmp_mask;
    int cur_bb_spur;
    HAL_BOOL is2GHz = IEEE80211_IS_CHAN_2GHZ(chan);

    OS_MEMZERO(&mask_m, sizeof(int8_t) * 123);
    OS_MEMZERO(&mask_p, sizeof(int8_t) * 123);

    ar5416GetChannelCenters(ah, chan, &centers);
    freq = centers.synth_center;

    /*
     * Need to verify range +/- 9.38 for static ht20 and +/- 18.75 for ht40,
     * otherwise spur is out-of-band and can be ignored.
     */
    for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
        cur_bb_spur = ath_hal_getSpurChan(ah, i, is2GHz);
        /* Get actual spur freq in MHz from EEPROM read value */ 
        if (is2GHz) {
            cur_bb_spur =  (cur_bb_spur / 10) + AR_BASE_FREQ_2GHZ;
        } else {
            cur_bb_spur =  (cur_bb_spur / 10) + AR_BASE_FREQ_5GHZ;
        }

        if (AR_NO_SPUR == cur_bb_spur)
            break;
        cur_bb_spur = cur_bb_spur - freq;

        if (IEEE80211_IS_CHAN_HT40(chan)) {
            if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT40) && 
                (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT40)) {
                bb_spur = cur_bb_spur;
                break;
            }
        } else if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT20) &&
                   (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT20)) {
            bb_spur = cur_bb_spur;
            break;
        }
    }

    if (AR_NO_SPUR == bb_spur) {
#if 1
        /*
         * MRC CCK can interfere with beacon detection and cause deaf/mute.
         * Disable MRC CCK for now.
         */
        OS_REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
#else
        /* Enable MRC CCK if no spur is found in this channel. */
        OS_REG_SET_BIT(ah, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
#endif
        return;
    } else {
        /* 
         * For Merlin, spur can break CCK MRC algorithm. Disable CCK MRC if spur
         * is found in this channel.
         */
        OS_REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
    }

    bin = bb_spur * 320;

    tmp = OS_REG_READ(ah, AR_PHY_TIMING_CTRL4_CHAIN(0));

    newVal = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
        AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
        AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
        AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
    OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4_CHAIN(0), newVal);

    newVal = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
        AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
        AR_PHY_SPUR_REG_MASK_RATE_SELECT |
        AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
        SM(AR5416_SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
    OS_REG_WRITE(ah, AR_PHY_SPUR_REG, newVal);

    /* Pick control or extn channel to cancel the spur */
    if (IEEE80211_IS_CHAN_HT40(chan)) {
        if (bb_spur < 0) {
            spur_subchannel_sd = 1;
            bb_spur_off = bb_spur + 10;
        } else {
            spur_subchannel_sd = 0;
            bb_spur_off = bb_spur - 10;
        }
    } else {
        spur_subchannel_sd = 0;
        bb_spur_off = bb_spur;
    }

    /*
     * spur_delta_phase = bb_spur/40 * 2**21 for static ht20,
     * /80 for dyn2040.
     */
    if (IEEE80211_IS_CHAN_HT40(chan))
        spur_delta_phase = ((bb_spur * 262144) / 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;    
    else
        spur_delta_phase = ((bb_spur * 524288) / 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;

    /*
     * in 11A mode the denominator of spur_freq_sd should be 40 and
     * it should be 44 in 11G
     */
    denominator = IEEE80211_IS_CHAN_2GHZ(chan) ? 44 : 40;
    spur_freq_sd = ((bb_spur_off * 2048) / denominator) & 0x3ff;

    newVal = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
        SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
        SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
    OS_REG_WRITE(ah, AR_PHY_TIMING11, newVal);

    /* Choose to cancel between control and extension channels */
    newVal = spur_subchannel_sd << AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S;
    OS_REG_WRITE(ah, AR_PHY_SFCORR_EXT, newVal);

    /*
     * ============================================
     * Set Pilot and Channel Masks
     *
     * pilot mask 1 [31:0] = +6..-26, no 0 bin
     * pilot mask 2 [19:0] = +26..+7
     *
     * channel mask 1 [31:0] = +6..-26, no 0 bin
     * channel mask 2 [19:0] = +26..+7
     */
    cur_bin = -6000;
    upper = bin + 100;
    lower = bin - 100;

    for (i = 0; i < 4; i++) {
        int pilot_mask = 0;
        int chan_mask  = 0;
        int bp         = 0;
        for (bp = 0; bp < 30; bp++) {
            if ((cur_bin > lower) && (cur_bin < upper)) {
                pilot_mask = pilot_mask | 0x1 << bp;
                chan_mask  = chan_mask | 0x1 << bp;
            }
            cur_bin += 100;
        }
        cur_bin += inc[i];
        OS_REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
        OS_REG_WRITE(ah, chan_mask_reg[i], chan_mask);
    }

    /* =================================================
     * viterbi mask 1 based on channel magnitude
     * four levels 0-3
     *  - mask (-27 to 27) (reg 64,0x9900 to 67,0x990c)
     *      [1 2 2 1] for -9.6 or [1 2 1] for +16
     *  - enable_mask_ppm, all bins move with freq
     *
     *  - mask_select,    8 bits for rates (reg 67,0x990c)
     *  - mask_rate_cntl, 8 bits for rates (reg 67,0x990c)
     *      choose which mask to use mask or mask2
     */

    /*
     * viterbi mask 2  2nd set for per data rate puncturing
     * four levels 0-3
     *  - mask_select, 8 bits for rates (reg 67)
     *  - mask (-27 to 27) (reg 98,0x9988 to 101,0x9994)
     *      [1 2 2 1] for -9.6 or [1 2 1] for +16
     */
    cur_vit_mask = 6100;
    upper        = bin + 120;
    lower        = bin - 120;

    for (i = 0; i < 123; i++) {
        if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {
            if ((abs(cur_vit_mask - bin)) < 75) {
                mask_amt = 1;
            } else {
                mask_amt = 0;
            }
            if (cur_vit_mask < 0) {
                mask_m[abs(cur_vit_mask / 100)] = mask_amt;
            } else {
                mask_p[cur_vit_mask / 100] = mask_amt;
            }
        }
        cur_vit_mask -= 100;
    }

    tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
          | (mask_m[48] << 26) | (mask_m[49] << 24)
          | (mask_m[50] << 22) | (mask_m[51] << 20)
          | (mask_m[52] << 18) | (mask_m[53] << 16)
          | (mask_m[54] << 14) | (mask_m[55] << 12)
          | (mask_m[56] << 10) | (mask_m[57] <<  8)
          | (mask_m[58] <<  6) | (mask_m[59] <<  4)
          | (mask_m[60] <<  2) | (mask_m[61] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

    tmp_mask =             (mask_m[31] << 28)
          | (mask_m[32] << 26) | (mask_m[33] << 24)
          | (mask_m[34] << 22) | (mask_m[35] << 20)
          | (mask_m[36] << 18) | (mask_m[37] << 16)
          | (mask_m[48] << 14) | (mask_m[39] << 12)
          | (mask_m[40] << 10) | (mask_m[41] <<  8)
          | (mask_m[42] <<  6) | (mask_m[43] <<  4)
          | (mask_m[44] <<  2) | (mask_m[45] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

    tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
          | (mask_m[18] << 26) | (mask_m[18] << 24)
          | (mask_m[20] << 22) | (mask_m[20] << 20)
          | (mask_m[22] << 18) | (mask_m[22] << 16)
          | (mask_m[24] << 14) | (mask_m[24] << 12)
          | (mask_m[25] << 10) | (mask_m[26] <<  8)
          | (mask_m[27] <<  6) | (mask_m[28] <<  4)
          | (mask_m[29] <<  2) | (mask_m[30] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

    tmp_mask = (mask_m[ 0] << 30) | (mask_m[ 1] << 28)
          | (mask_m[ 2] << 26) | (mask_m[ 3] << 24)
          | (mask_m[ 4] << 22) | (mask_m[ 5] << 20)
          | (mask_m[ 6] << 18) | (mask_m[ 7] << 16)
          | (mask_m[ 8] << 14) | (mask_m[ 9] << 12)
          | (mask_m[10] << 10) | (mask_m[11] <<  8)
          | (mask_m[12] <<  6) | (mask_m[13] <<  4)
          | (mask_m[14] <<  2) | (mask_m[15] <<  0);
    OS_REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

    tmp_mask =             (mask_p[15] << 28)
          | (mask_p[14] << 26) | (mask_p[13] << 24)
          | (mask_p[12] << 22) | (mask_p[11] << 20)
          | (mask_p[10] << 18) | (mask_p[ 9] << 16)
          | (mask_p[ 8] << 14) | (mask_p[ 7] << 12)
          | (mask_p[ 6] << 10) | (mask_p[ 5] <<  8)
          | (mask_p[ 4] <<  6) | (mask_p[ 3] <<  4)
          | (mask_p[ 2] <<  2) | (mask_p[ 1] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

    tmp_mask =             (mask_p[30] << 28)
          | (mask_p[29] << 26) | (mask_p[28] << 24)
          | (mask_p[27] << 22) | (mask_p[26] << 20)
          | (mask_p[25] << 18) | (mask_p[24] << 16)
          | (mask_p[23] << 14) | (mask_p[22] << 12)
          | (mask_p[21] << 10) | (mask_p[20] <<  8)
          | (mask_p[19] <<  6) | (mask_p[18] <<  4)
          | (mask_p[17] <<  2) | (mask_p[16] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

    tmp_mask =             (mask_p[45] << 28)
          | (mask_p[44] << 26) | (mask_p[43] << 24)
          | (mask_p[42] << 22) | (mask_p[41] << 20)
          | (mask_p[40] << 18) | (mask_p[39] << 16)
          | (mask_p[38] << 14) | (mask_p[37] << 12)
          | (mask_p[36] << 10) | (mask_p[35] <<  8)
          | (mask_p[34] <<  6) | (mask_p[33] <<  4)
          | (mask_p[32] <<  2) | (mask_p[31] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

    tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
          | (mask_p[59] << 26) | (mask_p[58] << 24)
          | (mask_p[57] << 22) | (mask_p[56] << 20)
          | (mask_p[55] << 18) | (mask_p[54] << 16)
          | (mask_p[53] << 14) | (mask_p[52] << 12)
          | (mask_p[51] << 10) | (mask_p[50] <<  8)
          | (mask_p[49] <<  6) | (mask_p[48] <<  4)
          | (mask_p[47] <<  2) | (mask_p[46] <<  0);
    OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
    OS_REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
static HAL_BOOL
ar9280FillCapabilityInfo(struct ath_hal *ah)
{
	HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;

	if (!ar5416FillCapabilityInfo(ah))
		return AH_FALSE;
	pCap->halNumGpioPins = 10;
	pCap->halWowSupport = AH_TRUE;
	pCap->halWowMatchPatternExact = AH_TRUE;
#if 0
	pCap->halWowMatchPatternDword = AH_TRUE;
#endif
	pCap->halCSTSupport = AH_TRUE;
	pCap->halRifsRxSupport = AH_TRUE;
	pCap->halRifsTxSupport = AH_TRUE;
	pCap->halRtsAggrLimit = 64*1024;	/* 802.11n max */
	pCap->halExtChanDfsSupport = AH_TRUE;
	pCap->halUseCombinedRadarRssi = AH_TRUE;
#if 0
	/* XXX bluetooth */
	pCap->halBtCoexSupport = AH_TRUE;
#endif
	pCap->halAutoSleepSupport = AH_FALSE;	/* XXX? */
	pCap->hal4kbSplitTransSupport = AH_FALSE;
	/* Disable this so Block-ACK works correctly */
	pCap->halHasRxSelfLinkedTail = AH_FALSE;
	pCap->halMbssidAggrSupport = AH_TRUE;
	pCap->hal4AddrAggrSupport = AH_TRUE;
	pCap->halSpectralScanSupport = AH_TRUE;

	if (AR_SREV_MERLIN_20(ah)) {
		pCap->halPSPollBroken = AH_FALSE;
		/*
		 * This just enables the support; it doesn't
		 * state 5ghz fast clock will always be used.
		 */
		pCap->halSupportsFastClock5GHz = AH_TRUE;
	}
	pCap->halRxStbcSupport = 1;
	pCap->halTxStbcSupport = 1;
	pCap->halEnhancedDfsSupport = AH_TRUE;

	return AH_TRUE;
}

/*
 * This has been disabled - having the HAL flip chainmasks on/off
 * when attempting to implement 11n disrupts things. For now, just
 * leave this flipped off and worry about implementing TX diversity
 * for legacy and MCS0-7 when 11n is fully functioning.
 */
HAL_BOOL
ar9280SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING settings)
{
#define ANTENNA0_CHAINMASK    0x1
#define ANTENNA1_CHAINMASK    0x2
#if 0
	struct ath_hal_5416 *ahp = AH5416(ah);

	/* Antenna selection is done by setting the tx/rx chainmasks approp. */
	switch (settings) {
	case HAL_ANT_FIXED_A:
		/* Enable first antenna only */
		ahp->ah_tx_chainmask = ANTENNA0_CHAINMASK;
		ahp->ah_rx_chainmask = ANTENNA0_CHAINMASK;
		break;
	case HAL_ANT_FIXED_B:
		/* Enable second antenna only, after checking capability */
		if (AH_PRIVATE(ah)->ah_caps.halTxChainMask > ANTENNA1_CHAINMASK)
			ahp->ah_tx_chainmask = ANTENNA1_CHAINMASK;
		ahp->ah_rx_chainmask = ANTENNA1_CHAINMASK;
		break;
	case HAL_ANT_VARIABLE:
		/* Restore original chainmask settings */
		/* XXX */
		ahp->ah_tx_chainmask = AR9280_DEFAULT_TXCHAINMASK;
		ahp->ah_rx_chainmask = AR9280_DEFAULT_RXCHAINMASK;
		break;
	}

	HALDEBUG(ah, HAL_DEBUG_ANY, "%s: settings=%d, tx/rx chainmask=%d/%d\n",
	    __func__, settings, ahp->ah_tx_chainmask, ahp->ah_rx_chainmask);

#endif
	return AH_TRUE;
#undef ANTENNA0_CHAINMASK
#undef ANTENNA1_CHAINMASK
}

static const char*
ar9280Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID) {
		if (devid == AR9280_DEVID_PCI)
			return "Atheros 9220";
		if (devid == AR9280_DEVID_PCIE)
			return "Atheros 9280";
	}
	return AH_NULL;
}
AH_CHIP(AR9280, ar9280Probe, ar9280Attach);
