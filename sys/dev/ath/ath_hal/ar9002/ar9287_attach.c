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
#include "ah_eeprom_9287.h"

#include "ar9002/ar9280.h"
#include "ar9002/ar9287.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#include "ar9002/ar9287_cal.h"
#include "ar9002/ar9287_reset.h"
#include "ar9002/ar9287_olc.h"

#include "ar9002/ar9287.ini"

static const HAL_PERCAL_DATA ar9287_iq_cal = {		/* single sample */
	.calName = "IQ", .calType = IQ_MISMATCH_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MAX_LOG_COUNT,
	.calCollect	= ar5416IQCalCollect,
	.calPostProc	= ar5416IQCalibration
};
static const HAL_PERCAL_DATA ar9287_adc_gain_cal = {	/* single sample */
	.calName = "ADC Gain", .calType = ADC_GAIN_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcGainCalCollect,
	.calPostProc	= ar5416AdcGainCalibration
};
static const HAL_PERCAL_DATA ar9287_adc_dc_cal = {	/* single sample */
	.calName = "ADC DC", .calType = ADC_DC_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= PER_MIN_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};
static const HAL_PERCAL_DATA ar9287_adc_init_dc_cal = {
	.calName = "ADC Init DC", .calType = ADC_DC_INIT_CAL,
	.calNumSamples	= MIN_CAL_SAMPLES,
	.calCountMax	= INIT_LOG_COUNT,
	.calCollect	= ar5416AdcDcCalCollect,
	.calPostProc	= ar5416AdcDcCalibration
};

static void ar9287ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore,
		HAL_BOOL power_off);
static void ar9287DisablePCIE(struct ath_hal *ah);
static HAL_BOOL ar9287FillCapabilityInfo(struct ath_hal *ah);
static void ar9287WriteIni(struct ath_hal *ah,
	const struct ieee80211_channel *chan);

static void
ar9287AniSetup(struct ath_hal *ah)
{
	/*
	 * These are the parameters from the AR5416 ANI code;
	 * they likely need quite a bit of adjustment for the
	 * AR9287.
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
	AH5416(ah)->ah_ani_function &= ~ HAL_ANI_NOISE_IMMUNITY_LEVEL;

        /* NB: ANI is not enabled yet */
        ar5416AniAttach(ah, &aniparams, &aniparams, AH_TRUE);
}

/*
 * Attach for an AR9287 part.
 */
static struct ath_hal *
ar9287Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config,
	HAL_STATUS *status)
{
	struct ath_hal_9287 *ahp9287;
	struct ath_hal_5212 *ahp;
	struct ath_hal *ah;
	uint32_t val;
	HAL_STATUS ecode;
	HAL_BOOL rfStatus;
	int8_t pwr_table_offset;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
	    __func__, sc, (void*) st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp9287 = ath_hal_malloc(sizeof (struct ath_hal_9287));
	if (ahp9287 == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ahp = AH5212(ahp9287);
	ah = &ahp->ah_priv.h;

	ar5416InitState(AH5416(ah), devid, sc, st, sh, status);

	if (eepromdata != AH_NULL) {
		AH_PRIVATE(ah)->ah_eepromRead = ath_hal_EepromDataRead;
		AH_PRIVATE(ah)->ah_eepromWrite = NULL;
		ah->ah_eepromdata = eepromdata;
	}


	/* XXX override with 9280 specific state */
	/* override 5416 methods for our needs */
	AH5416(ah)->ah_initPLL = ar9280InitPLL;

	ah->ah_setAntennaSwitch		= ar9287SetAntennaSwitch;
	ah->ah_configPCIE		= ar9287ConfigPCIE;
	ah->ah_disablePCIE		= ar9287DisablePCIE;

	AH5416(ah)->ah_cal.iqCalData.calData = &ar9287_iq_cal;
	AH5416(ah)->ah_cal.adcGainCalData.calData = &ar9287_adc_gain_cal;
	AH5416(ah)->ah_cal.adcDcCalData.calData = &ar9287_adc_dc_cal;
	AH5416(ah)->ah_cal.adcDcCalInitData.calData = &ar9287_adc_init_dc_cal;
	/* Better performance without ADC Gain Calibration */
	AH5416(ah)->ah_cal.suppCals = ADC_DC_CAL | IQ_MISMATCH_CAL;

	AH5416(ah)->ah_spurMitigate	= ar9280SpurMitigate;
	AH5416(ah)->ah_writeIni		= ar9287WriteIni;

	ah->ah_setTxPower		= ar9287SetTransmitPower;
	ah->ah_setBoardValues		= ar9287SetBoardValues;

	AH5416(ah)->ah_olcInit		= ar9287olcInit;
	AH5416(ah)->ah_olcTempCompensation = ar9287olcTemperatureCompensation;
	//AH5416(ah)->ah_setPowerCalTable	= ar9287SetPowerCalTable;
	AH5416(ah)->ah_cal_initcal	= ar9287InitCalHardware;
	AH5416(ah)->ah_cal_pacal	= ar9287PACal;

	/* XXX NF calibration */
	/* XXX Ini override? (IFS vars - since the kiwi mac clock is faster?) */
	/* XXX what else is kiwi-specific in the radio/calibration pathway? */

	AH5416(ah)->ah_rx_chainmask	= AR9287_DEFAULT_RXCHAINMASK;
	AH5416(ah)->ah_tx_chainmask	= AR9287_DEFAULT_TXCHAINMASK;

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

	/* Don't support Kiwi < 1.2; those are pre-release chips */
	if (! AR_SREV_KIWI_12_OR_LATER(ah)) {
		ath_hal_printf(ah, "[ath]: Kiwi < 1.2 is not supported\n");
		ecode = HAL_EIO;
		goto bad;
	}

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar9287Modes_9287_1_1, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar9287Common_9287_1_1, 2);

	/* If pcie_clock_req */
	HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes,
	    ar9287PciePhy_clkreq_always_on_L1_9287_1_1, 2);

	/* XXX WoW ini values */

	/* Else */
#if 0
	HAL_INI_INIT(&AH5416(ah)->ah_ini_pcieserdes,
	    ar9287PciePhy_clkreq_off_L1_9287_1_1, 2);
#endif

	/* Initialise Japan arrays */
	HAL_INI_INIT(&ahp9287->ah_ini_cckFirNormal,
	    ar9287Common_normal_cck_fir_coeff_9287_1_1, 2);
	HAL_INI_INIT(&ahp9287->ah_ini_cckFirJapan2484,
	    ar9287Common_japan_2484_cck_fir_coeff_9287_1_1, 2);

	ar5416AttachPCIE(ah);

	ecode = ath_hal_9287EepromAttach(ah);
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
	rfStatus = ar9287RfAttach(ah, &ecode);
	if (!rfStatus) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}

	/*
	 * We only implement open-loop TX power control
	 * for the AR9287 in this codebase.
	 */
	if (! ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL)) {
		ath_hal_printf(ah, "[ath] AR9287 w/ closed-loop TX power control"
		    " isn't supported.\n");
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

        /*
         * Check whether the power table offset isn't the default.
         * This can occur with eeprom minor V21 or greater on Merlin.
         */
	(void) ath_hal_eepromGet(ah, AR_EEP_PWR_TABLE_OFFSET, &pwr_table_offset);
	if (pwr_table_offset != AR5416_PWR_TABLE_OFFSET_DB)
		ath_hal_printf(ah, "[ath]: default pwr offset: %d dBm != EEPROM pwr offset: %d dBm; curves will be adjusted.\n",
		    AR5416_PWR_TABLE_OFFSET_DB, (int) pwr_table_offset);

	/* setup rxgain table */
	HAL_INI_INIT(&ahp9287->ah_ini_rxgain, ar9287Modes_rx_gain_9287_1_1, 6);

	/* setup txgain table */
	HAL_INI_INIT(&ahp9287->ah_ini_txgain, ar9287Modes_tx_gain_9287_1_1, 6);

	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar9287FillCapabilityInfo(ah)) {
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
	AH_PRIVATE(ah)->ah_currentRDext = AR9287_RDEXT_DEFAULT;

	/*
	 * ah_miscMode is populated by ar5416FillCapabilityInfo()
	 * starting from griffin. Set here to make sure that
	 * AR_MISC_MODE_MIC_NEW_LOC_ENABLE is set before a GTK is
	 * placed into hardware.
	 */
	if (ahp->ah_miscMode != 0)
		OS_REG_WRITE(ah, AR_MISC_MODE, OS_REG_READ(ah, AR_MISC_MODE) | ahp->ah_miscMode);

	ar9287AniSetup(ah);			/* Anti Noise Immunity */

	/* Setup noise floor min/max/nominal values */
	AH5416(ah)->nf_2g.max = AR_PHY_CCA_MAX_GOOD_VAL_9287_2GHZ;
	AH5416(ah)->nf_2g.min = AR_PHY_CCA_MIN_GOOD_VAL_9287_2GHZ;
	AH5416(ah)->nf_2g.nominal = AR_PHY_CCA_NOM_VAL_9287_2GHZ;
	AH5416(ah)->nf_5g.max = AR_PHY_CCA_MAX_GOOD_VAL_9287_5GHZ;
	AH5416(ah)->nf_5g.min = AR_PHY_CCA_MIN_GOOD_VAL_9287_5GHZ;
	AH5416(ah)->nf_5g.nominal = AR_PHY_CCA_NOM_VAL_9287_5GHZ;

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
ar9287ConfigPCIE(struct ath_hal *ah, HAL_BOOL restore, HAL_BOOL power_off)
{
	if (AH_PRIVATE(ah)->ah_ispcie && !restore) {
		ath_hal_ini_write(ah, &AH5416(ah)->ah_ini_pcieserdes, 1, 0);
		OS_DELAY(1000);
		OS_REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
		/* Yes, Kiwi uses the Kite PCIe PHY WA */
		OS_REG_WRITE(ah, AR_WA, AR9285_WA_DEFAULT);
	}
}

static void
ar9287DisablePCIE(struct ath_hal *ah)
{
	/* XXX TODO */
}

static void
ar9287WriteIni(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	u_int modesIndex, freqIndex;
	int regWrites = 0;

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

	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_modes, modesIndex, regWrites);
	regWrites = ath_hal_ini_write(ah, &AH9287(ah)->ah_ini_rxgain, modesIndex, regWrites);
	regWrites = ath_hal_ini_write(ah, &AH9287(ah)->ah_ini_txgain, modesIndex, regWrites);
	regWrites = ath_hal_ini_write(ah, &AH5212(ah)->ah_ini_common, 1, regWrites);
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
static HAL_BOOL
ar9287FillCapabilityInfo(struct ath_hal *ah)
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
	pCap->halPSPollBroken = AH_FALSE;
	pCap->halSpectralScanSupport = AH_TRUE;

	/* Hardware supports (at least) single-stream STBC TX/RX */
	pCap->halRxStbcSupport = 1;
	pCap->halTxStbcSupport = 1;

	/* Hardware supports short-GI w/ 20MHz */
	pCap->halHTSGI20Support = 1;

	pCap->halEnhancedDfsSupport = AH_TRUE;

	return AH_TRUE;
}

/*
 * This has been disabled - having the HAL flip chainmasks on/off
 * when attempting to implement 11n disrupts things. For now, just
 * leave this flipped off and worry about implementing TX diversity
 * for legacy and MCS0-15 when 11n is fully functioning.
 */
HAL_BOOL
ar9287SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING settings)
{
	return AH_TRUE;
}

static const char*
ar9287Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID) {
		if (devid == AR9287_DEVID_PCI)
			return "Atheros 9227";
		if (devid == AR9287_DEVID_PCIE)
			return "Atheros 9287";
	}
	return AH_NULL;
}
AH_CHIP(AR9287, ar9287Probe, ar9287Attach);
