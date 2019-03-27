/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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

/*
 * This is almost the same as ar5416_reset.c but uses the v4k EEPROM and
 * supports only 2Ghz operation.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ah_eeprom_v14.h"
#include "ah_eeprom_v4k.h"

#include "ar9002/ar9285.h"
#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar9002/ar9002phy.h"
#include "ar9002/ar9285phy.h"
#include "ar9002/ar9285an.h"
#include "ar9002/ar9285_diversity.h"

/* Eeprom versioning macros. Returns true if the version is equal or newer than the ver specified */ 
#define	EEP_MINOR(_ah) \
	(AH_PRIVATE(_ah)->ah_eeversion & AR5416_EEP_VER_MINOR_MASK)
#define IS_EEP_MINOR_V2(_ah)	(EEP_MINOR(_ah) >= AR5416_EEP_MINOR_VER_2)
#define IS_EEP_MINOR_V3(_ah)	(EEP_MINOR(_ah) >= AR5416_EEP_MINOR_VER_3)

/* Additional Time delay to wait after activiting the Base band */
#define BASE_ACTIVATE_DELAY	100	/* 100 usec */
#define PLL_SETTLE_DELAY	300	/* 300 usec */
#define RTC_PLL_SETTLE_DELAY    1000    /* 1 ms     */

static HAL_BOOL ar9285SetPowerPerRateTable(struct ath_hal *ah,
	struct ar5416eeprom_4k *pEepData, 
	const struct ieee80211_channel *chan, int16_t *ratesArray,
	uint16_t cfgCtl, uint16_t AntennaReduction,
	uint16_t twiceMaxRegulatoryPower, 
	uint16_t powerLimit);
static HAL_BOOL ar9285SetPowerCalTable(struct ath_hal *ah,
	struct ar5416eeprom_4k *pEepData,
	const struct ieee80211_channel *chan,
	int16_t *pTxPowerIndexOffset);
static void ar9285GetGainBoundariesAndPdadcs(struct ath_hal *ah, 
	const struct ieee80211_channel *chan, CAL_DATA_PER_FREQ_4K *pRawDataSet,
	uint8_t * bChans, uint16_t availPiers,
	uint16_t tPdGainOverlap, int16_t *pMinCalPower,
	uint16_t * pPdGainBoundaries, uint8_t * pPDADCValues,
	uint16_t numXpdGains);

HAL_BOOL
ar9285SetTransmitPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan, uint16_t *rfXpdGain)
{
#define POW_SM(_r, _s)     (((_r) & 0x3f) << (_s))
#define N(a)            (sizeof (a) / sizeof (a[0]))

    MODAL_EEP4K_HEADER	*pModal;
    struct ath_hal_5212 *ahp = AH5212(ah);
    int16_t		txPowerIndexOffset = 0;
    int			i;
    
    uint16_t		cfgCtl;
    uint16_t		powerLimit;
    uint16_t		twiceAntennaReduction;
    uint16_t		twiceMaxRegulatoryPower;
    int16_t		maxPower;
    HAL_EEPROM_v4k *ee = AH_PRIVATE(ah)->ah_eeprom;
    struct ar5416eeprom_4k *pEepData = &ee->ee_base;

    HALASSERT(AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER14_1);

    AH5416(ah)->ah_ht40PowerIncForPdadc = 2;

    /* Setup info for the actual eeprom */
    OS_MEMZERO(AH5416(ah)->ah_ratesArray, sizeof(AH5416(ah)->ah_ratesArray));
    cfgCtl = ath_hal_getctl(ah, chan);
    powerLimit = chan->ic_maxregpower * 2;
    twiceAntennaReduction = chan->ic_maxantgain;
    twiceMaxRegulatoryPower = AH_MIN(MAX_RATE_POWER, AH_PRIVATE(ah)->ah_powerLimit); 
    pModal = &pEepData->modalHeader;
    HALDEBUG(ah, HAL_DEBUG_RESET, "%s Channel=%u CfgCtl=%u\n",
	__func__,chan->ic_freq, cfgCtl );      
  
    if (IS_EEP_MINOR_V2(ah)) {
        AH5416(ah)->ah_ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;
    }
 
    if (!ar9285SetPowerPerRateTable(ah, pEepData,  chan,
                                    &AH5416(ah)->ah_ratesArray[0],cfgCtl,
                                    twiceAntennaReduction,
				    twiceMaxRegulatoryPower, powerLimit)) {
        HALDEBUG(ah, HAL_DEBUG_ANY,
	    "%s: unable to set tx power per rate table\n", __func__);
        return AH_FALSE;
    }

    if (!ar9285SetPowerCalTable(ah,  pEepData, chan, &txPowerIndexOffset)) {
        HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unable to set power table\n",
	    __func__);
        return AH_FALSE;
    }
  
    maxPower = AH_MAX(AH5416(ah)->ah_ratesArray[rate6mb],
      AH5416(ah)->ah_ratesArray[rateHt20_0]);
    maxPower = AH_MAX(maxPower, AH5416(ah)->ah_ratesArray[rate1l]);

    if (IEEE80211_IS_CHAN_HT40(chan)) {
        maxPower = AH_MAX(maxPower, AH5416(ah)->ah_ratesArray[rateHt40_0]);
    }

    ahp->ah_tx6PowerInHalfDbm = maxPower;   
    AH_PRIVATE(ah)->ah_maxPowerLevel = maxPower;
    ahp->ah_txPowerIndexOffset = txPowerIndexOffset;

    /*
     * txPowerIndexOffset is set by the SetPowerTable() call -
     *  adjust the rate table (0 offset if rates EEPROM not loaded)
     */
    for (i = 0; i < N(AH5416(ah)->ah_ratesArray); i++) {
        AH5416(ah)->ah_ratesArray[i] = (int16_t)(txPowerIndexOffset + AH5416(ah)->ah_ratesArray[i]);
	/* -5 dBm offset for Merlin and later; this includes Kite */
	AH5416(ah)->ah_ratesArray[i] -= AR5416_PWR_TABLE_OFFSET_DB * 2;
        if (AH5416(ah)->ah_ratesArray[i] > AR5416_MAX_RATE_POWER)
            AH5416(ah)->ah_ratesArray[i] = AR5416_MAX_RATE_POWER;
	if (AH5416(ah)->ah_ratesArray[i] < 0)
		AH5416(ah)->ah_ratesArray[i] = 0;
    }

#ifdef AH_EEPROM_DUMP
    ar5416PrintPowerPerRate(ah, AH5416(ah)->ah_ratesArray);
#endif

    /*
     * Adjust the HT40 power to meet the correct target TX power
     * for 40MHz mode, based on TX power curves that are established
     * for 20MHz mode.
     *
     * XXX handle overflow/too high power level?
     */
    if (IEEE80211_IS_CHAN_HT40(chan)) {
        AH5416(ah)->ah_ratesArray[rateHt40_0] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_1] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_2] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_3] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_4] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_5] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_6] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
        AH5416(ah)->ah_ratesArray[rateHt40_7] +=
          AH5416(ah)->ah_ht40PowerIncForPdadc;
    }

    /* Write the TX power rate registers */
    ar5416WriteTxPowerRateRegisters(ah, chan, AH5416(ah)->ah_ratesArray);

    return AH_TRUE;
#undef POW_SM
#undef N
}

static void
ar9285SetBoardGain(struct ath_hal *ah, const MODAL_EEP4K_HEADER *pModal,
    const struct ar5416eeprom_4k *eep, uint8_t txRxAttenLocal)
{
	OS_REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0,
		  pModal->antCtrlChain[0]);

	OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4_CHAIN(0),
		  (OS_REG_READ(ah, AR_PHY_TIMING_CTRL4_CHAIN(0)) &
		   ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
		     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
		  SM(pModal->iqCalICh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
		  SM(pModal->iqCalQCh[0], AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_3) {
		txRxAttenLocal = pModal->txRxAttenCh[0];

		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
		    AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN, pModal->bswMargin[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
		    AR_PHY_GAIN_2GHZ_XATTEN1_DB, pModal->bswAtten[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
		    AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN, pModal->xatten2Margin[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
		    AR_PHY_GAIN_2GHZ_XATTEN2_DB, pModal->xatten2Db[0]);

		/* Set the block 1 value to block 0 value */
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
		      AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
		      pModal->bswMargin[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
		      AR_PHY_GAIN_2GHZ_XATTEN1_DB, pModal->bswAtten[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
		      AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
		      pModal->xatten2Margin[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ + 0x1000,
		      AR_PHY_GAIN_2GHZ_XATTEN2_DB, pModal->xatten2Db[0]);
	}

	OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN,
		      AR9280_PHY_RXGAIN_TXRX_ATTEN, txRxAttenLocal);
	OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN,
		      AR9280_PHY_RXGAIN_TXRX_MARGIN, pModal->rxTxMarginCh[0]);

	OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000,
		      AR9280_PHY_RXGAIN_TXRX_ATTEN, txRxAttenLocal);
	OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN + 0x1000,
		      AR9280_PHY_RXGAIN_TXRX_MARGIN, pModal->rxTxMarginCh[0]);
}

/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
HAL_BOOL
ar9285SetBoardValues(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	const HAL_EEPROM_v4k *ee = AH_PRIVATE(ah)->ah_eeprom;
	const struct ar5416eeprom_4k *eep = &ee->ee_base;
	const MODAL_EEP4K_HEADER *pModal;
	uint8_t txRxAttenLocal;
	uint8_t ob[5], db1[5], db2[5];

	pModal = &eep->modalHeader;
	txRxAttenLocal = 23;

	OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, pModal->antCtrlCommon);

	/* Single chain for 4K EEPROM*/
	ar9285SetBoardGain(ah, pModal, eep, txRxAttenLocal);

	/* Initialize Ant Diversity settings if supported */
	(void) ar9285SetAntennaSwitch(ah, AH5212(ah)->ah_antControl);

	/* Configure TX power calibration */
	if (pModal->version >= 2) {
		ob[0] = pModal->ob_0;
		ob[1] = pModal->ob_1;
		ob[2] = pModal->ob_2;
		ob[3] = pModal->ob_3;
		ob[4] = pModal->ob_4;

		db1[0] = pModal->db1_0;
		db1[1] = pModal->db1_1;
		db1[2] = pModal->db1_2;
		db1[3] = pModal->db1_3;
		db1[4] = pModal->db1_4;

		db2[0] = pModal->db2_0;
		db2[1] = pModal->db2_1;
		db2[2] = pModal->db2_2;
		db2[3] = pModal->db2_3;
		db2[4] = pModal->db2_4;
	} else if (pModal->version == 1) {
		ob[0] = pModal->ob_0;
		ob[1] = ob[2] = ob[3] = ob[4] = pModal->ob_1;
		db1[0] = pModal->db1_0;
		db1[1] = db1[2] = db1[3] = db1[4] = pModal->db1_1;
		db2[0] = pModal->db2_0;
		db2[1] = db2[2] = db2[3] = db2[4] = pModal->db2_1;
	} else {
		int i;

		for (i = 0; i < 5; i++) {
			ob[i] = pModal->ob_0;
			db1[i] = pModal->db1_0;
			db2[i] = pModal->db1_0;
		}
	}

	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_0, ob[0]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_1, ob[1]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_2, ob[2]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_3, ob[3]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_OB_4, ob[4]);

	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_0, db1[0]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_1, db1[1]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G3, AR9285_AN_RF2G3_DB1_2, db1[2]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB1_3, db1[3]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB1_4, db1[4]);

	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_0, db2[0]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_1, db2[1]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_2, db2[2]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_3, db2[3]);
	OS_A_REG_RMW_FIELD(ah, AR9285_AN_RF2G4, AR9285_AN_RF2G4_DB2_4, db2[4]);

	OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH,
		      pModal->switchSettling);
	OS_REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC,
		      pModal->adcDesiredSize);

	OS_REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF) |
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAB_OFF) |
		  SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAA_ON)  |
		  SM(pModal->txFrameToXpaOn, AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
		      pModal->txEndToRxOn);

	OS_REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62,
		      pModal->thresh62);
	OS_REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0, AR_PHY_EXT_CCA0_THRESH62,
		      pModal->thresh62);

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_FRAME_TO_DATA_START,
		    pModal->txFrameToDataStart);
		OS_REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_FRAME_TO_PA_ON,
		    pModal->txFrameToPaOn);
	}

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_3) {
		if (IEEE80211_IS_CHAN_HT40(chan))
			OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING,
			    AR_PHY_SETTLING_SWITCH, pModal->swSettleHt40);
	}

	/*
	 * Program the CCK TX gain factor appropriately if needed.
	 * The AR9285/AR9271 has a non-constant PA tx gain behaviour
	 * for CCK versus OFDM rates; other chips deal with this
	 * differently.
	 *
	 * The mask/shift/multiply hackery is done so place the same
	 * value (bb_desired_scale) into multiple 5-bit fields.
	 * For example, AR_PHY_TX_PWRCTRL9 has bb_desired_scale written
	 * to three fields: (0..4), (5..9) and (10..14).
	 */
	if (AR_SREV_9271(ah) || AR_SREV_KITE(ah)) {
		uint8_t bb_desired_scale = (pModal->bb_scale_smrt_antenna & EEP_4K_BB_DESIRED_SCALE_MASK);
		if ((eep->baseEepHeader.txGainType == 0) && (bb_desired_scale != 0)) {
			ath_hal_printf(ah, "[ath]: adjusting cck tx gain factor\n");
			uint32_t pwrctrl, mask, clr;

			mask = (1<<0) | (1<<5) | (1<<10) | (1<<15) | (1<<20) | (1<<25);
			pwrctrl = mask * bb_desired_scale;
			clr = mask * 0x1f;
			OS_REG_RMW(ah, AR_PHY_TX_PWRCTRL8, pwrctrl, clr);
			OS_REG_RMW(ah, AR_PHY_TX_PWRCTRL10, pwrctrl, clr);
			OS_REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL12, pwrctrl, clr);

			mask = (1<<0) | (1<<5) | (1<<15);
			pwrctrl = mask * bb_desired_scale;
			clr = mask * 0x1f;
			OS_REG_RMW(ah, AR_PHY_TX_PWRCTRL9, pwrctrl, clr);

			mask = (1<<0) | (1<<5);
			pwrctrl = mask * bb_desired_scale;
			clr = mask * 0x1f;
			OS_REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL11, pwrctrl, clr);
			OS_REG_RMW(ah, AR_PHY_CH0_TX_PWRCTRL13, pwrctrl, clr);
		}
	}

	return AH_TRUE;
}

/*
 * Helper functions common for AP/CB/XB
 */

static HAL_BOOL
ar9285SetPowerPerRateTable(struct ath_hal *ah, struct ar5416eeprom_4k *pEepData,
                           const struct ieee80211_channel *chan,
                           int16_t *ratesArray, uint16_t cfgCtl,
                           uint16_t AntennaReduction, 
                           uint16_t twiceMaxRegulatoryPower,
                           uint16_t powerLimit)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
/* Local defines to distinguish between extension and control CTL's */
#define EXT_ADDITIVE (0x8000)
#define CTL_11G_EXT (CTL_11G | EXT_ADDITIVE)
#define CTL_11B_EXT (CTL_11B | EXT_ADDITIVE)

	uint16_t twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	int i;
	int16_t  twiceLargestAntenna;
	CAL_CTL_DATA_4K *rep;
	CAL_TARGET_POWER_LEG targetPowerOfdm, targetPowerCck = {0, {0, 0, 0, 0}};
	CAL_TARGET_POWER_LEG targetPowerOfdmExt = {0, {0, 0, 0, 0}}, targetPowerCckExt = {0, {0, 0, 0, 0}};
	CAL_TARGET_POWER_HT  targetPowerHt20, targetPowerHt40 = {0, {0, 0, 0, 0}};
	int16_t scaledPower, minCtlPower;

#define SUB_NUM_CTL_MODES_AT_2G_40 3   /* excluding HT40, EXT-OFDM, EXT-CCK */
	static const uint16_t ctlModesFor11g[] = {
	   CTL_11B, CTL_11G, CTL_2GHT20, CTL_11B_EXT, CTL_11G_EXT, CTL_2GHT40
	};
	const uint16_t *pCtlMode;
	uint16_t numCtlModes, ctlMode, freq;
	CHAN_CENTERS centers;

	ar5416GetChannelCenters(ah,  chan, &centers);

	/* Compute TxPower reduction due to Antenna Gain */

	twiceLargestAntenna = pEepData->modalHeader.antennaGainCh[0];
	twiceLargestAntenna = (int16_t)AH_MIN((AntennaReduction) - twiceLargestAntenna, 0);

	/* XXX setup for 5212 use (really used?) */
	ath_hal_eepromSet(ah, AR_EEP_ANTGAINMAX_2, twiceLargestAntenna);

	/* 
	 * scaledPower is the minimum of the user input power level and
	 * the regulatory allowed power level
	 */
	scaledPower = AH_MIN(powerLimit, twiceMaxRegulatoryPower + twiceLargestAntenna);

	/* Get target powers from EEPROM - our baseline for TX Power */
	/* Setup for CTL modes */
	numCtlModes = N(ctlModesFor11g) - SUB_NUM_CTL_MODES_AT_2G_40; /* CTL_11B, CTL_11G, CTL_2GHT20 */
	pCtlMode = ctlModesFor11g;

	ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPowerCck,
			AR5416_4K_NUM_2G_CCK_TARGET_POWERS, &targetPowerCck, 4, AH_FALSE);
	ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPower2G,
			AR5416_4K_NUM_2G_20_TARGET_POWERS, &targetPowerOfdm, 4, AH_FALSE);
	ar5416GetTargetPowers(ah,  chan, pEepData->calTargetPower2GHT20,
			AR5416_4K_NUM_2G_20_TARGET_POWERS, &targetPowerHt20, 8, AH_FALSE);

	if (IEEE80211_IS_CHAN_HT40(chan)) {
		numCtlModes = N(ctlModesFor11g);    /* All 2G CTL's */

		ar5416GetTargetPowers(ah,  chan, pEepData->calTargetPower2GHT40,
			AR5416_4K_NUM_2G_40_TARGET_POWERS, &targetPowerHt40, 8, AH_TRUE);
		/* Get target powers for extension channels */
		ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPowerCck,
			AR5416_4K_NUM_2G_CCK_TARGET_POWERS, &targetPowerCckExt, 4, AH_TRUE);
		ar5416GetTargetPowersLeg(ah,  chan, pEepData->calTargetPower2G,
			AR5416_4K_NUM_2G_20_TARGET_POWERS, &targetPowerOfdmExt, 4, AH_TRUE);
	}

	/*
	 * For MIMO, need to apply regulatory caps individually across dynamically
	 * running modes: CCK, OFDM, HT20, HT40
	 *
	 * The outer loop walks through each possible applicable runtime mode.
	 * The inner loop walks through each ctlIndex entry in EEPROM.
	 * The ctl value is encoded as [7:4] == test group, [3:0] == test mode.
	 *
	 */
	for (ctlMode = 0; ctlMode < numCtlModes; ctlMode++) {
		HAL_BOOL isHt40CtlMode = (pCtlMode[ctlMode] == CTL_5GHT40) ||
		    (pCtlMode[ctlMode] == CTL_2GHT40);
		if (isHt40CtlMode) {
			freq = centers.ctl_center;
		} else if (pCtlMode[ctlMode] & EXT_ADDITIVE) {
			freq = centers.ext_center;
		} else {
			freq = centers.ctl_center;
		}

		/* walk through each CTL index stored in EEPROM */
		for (i = 0; (i < AR5416_4K_NUM_CTLS) && pEepData->ctlIndex[i]; i++) {
			uint16_t twiceMinEdgePower;

			/* compare test group from regulatory channel list with test mode from pCtlMode list */
			if ((((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == pEepData->ctlIndex[i]) ||
				(((cfgCtl & ~CTL_MODE_M) | (pCtlMode[ctlMode] & CTL_MODE_M)) == 
				 ((pEepData->ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))) {
				rep = &(pEepData->ctlData[i]);
				twiceMinEdgePower = ar5416GetMaxEdgePower(freq,
							rep->ctlEdges[
							  owl_get_ntxchains(AH5416(ah)->ah_tx_chainmask) - 1], AH_TRUE);
				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					/* Find the minimum of all CTL edge powers that apply to this channel */
					twiceMaxEdgePower = AH_MIN(twiceMaxEdgePower, twiceMinEdgePower);
				} else {
					/* specific */
					twiceMaxEdgePower = twiceMinEdgePower;
					break;
				}
			}
		}
		minCtlPower = (uint8_t)AH_MIN(twiceMaxEdgePower, scaledPower);
		/* Apply ctl mode to correct target power set */
		switch(pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < N(targetPowerCck.tPow2x); i++) {
				targetPowerCck.tPow2x[i] = (uint8_t)AH_MIN(targetPowerCck.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0; i < N(targetPowerOfdm.tPow2x); i++) {
				targetPowerOfdm.tPow2x[i] = (uint8_t)AH_MIN(targetPowerOfdm.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0; i < N(targetPowerHt20.tPow2x); i++) {
				targetPowerHt20.tPow2x[i] = (uint8_t)AH_MIN(targetPowerHt20.tPow2x[i], minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] = (uint8_t)AH_MIN(targetPowerCckExt.tPow2x[0], minCtlPower);
			break;
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] = (uint8_t)AH_MIN(targetPowerOfdmExt.tPow2x[0], minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0; i < N(targetPowerHt40.tPow2x); i++) {
				targetPowerHt40.tPow2x[i] = (uint8_t)AH_MIN(targetPowerHt40.tPow2x[i], minCtlPower);
			}
			break;
		default:
			return AH_FALSE;
			break;
		}
	} /* end ctl mode checking */

        /* Set rates Array from collected data */
	ar5416SetRatesArrayFromTargetPower(ah, chan, ratesArray,
	    &targetPowerCck,
	    &targetPowerCckExt,
	    &targetPowerOfdm,
	    &targetPowerOfdmExt,
	    &targetPowerHt20,
	    &targetPowerHt40);

	return AH_TRUE;
#undef EXT_ADDITIVE
#undef CTL_11G_EXT
#undef CTL_11B_EXT
#undef SUB_NUM_CTL_MODES_AT_2G_40
#undef N
}

static HAL_BOOL
ar9285SetPowerCalTable(struct ath_hal *ah, struct ar5416eeprom_4k *pEepData,
	const struct ieee80211_channel *chan, int16_t *pTxPowerIndexOffset)
{
    CAL_DATA_PER_FREQ_4K *pRawDataset;
    uint8_t  *pCalBChans = AH_NULL;
    uint16_t pdGainOverlap_t2;
    static uint8_t  pdadcValues[AR5416_NUM_PDADC_VALUES];
    uint16_t gainBoundaries[AR5416_PD_GAINS_IN_MASK];
    uint16_t numPiers, i;
    int16_t  tMinCalPower;
    uint16_t numXpdGain, xpdMask;
    uint16_t xpdGainValues[4];	/* v4k eeprom has 2; the other two stay 0 */
    uint32_t regChainOffset;

    OS_MEMZERO(xpdGainValues, sizeof(xpdGainValues));
    
    xpdMask = pEepData->modalHeader.xpdGain;

    if (IS_EEP_MINOR_V2(ah)) {
        pdGainOverlap_t2 = pEepData->modalHeader.pdGainOverlap;
    } else { 
    	pdGainOverlap_t2 = (uint16_t)(MS(OS_REG_READ(ah, AR_PHY_TPCRG5), AR_PHY_TPCRG5_PD_GAIN_OVERLAP));
    }

    pCalBChans = pEepData->calFreqPier2G;
    numPiers = AR5416_4K_NUM_2G_CAL_PIERS;
    numXpdGain = 0;

    /* Calculate the value of xpdgains from the xpdGain Mask */
    for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
        if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
            if (numXpdGain >= AR5416_4K_NUM_PD_GAINS) {
                HALASSERT(0);
                break;
            }
            xpdGainValues[numXpdGain] = (uint16_t)(AR5416_PD_GAINS_IN_MASK - i);
            numXpdGain++;
        }
    }
    
    /* Write the detector gain biases and their number */
    ar5416WriteDetectorGainBiases(ah, numXpdGain, xpdGainValues);

    for (i = 0; i < AR5416_MAX_CHAINS; i++) {
	regChainOffset = ar5416GetRegChainOffset(ah, i);
        if (pEepData->baseEepHeader.txMask & (1 << i)) {
            pRawDataset = pEepData->calPierData2G[i];

            ar9285GetGainBoundariesAndPdadcs(ah,  chan, pRawDataset,
                                             pCalBChans, numPiers,
                                             pdGainOverlap_t2,
                                             &tMinCalPower, gainBoundaries,
                                             pdadcValues, numXpdGain);

            if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {
                /*
                 * Note the pdadc table may not start at 0 dBm power, could be
                 * negative or greater than 0.  Need to offset the power
                 * values by the amount of minPower for griffin
                 */
		ar5416SetGainBoundariesClosedLoop(ah, i, pdGainOverlap_t2, gainBoundaries); 
            }

            /* Write the power values into the baseband power table */
	    ar5416WritePdadcValues(ah, i, pdadcValues);
        }
    }
    *pTxPowerIndexOffset = 0;

    return AH_TRUE;
}

static void
ar9285GetGainBoundariesAndPdadcs(struct ath_hal *ah, 
                                 const struct ieee80211_channel *chan,
				 CAL_DATA_PER_FREQ_4K *pRawDataSet,
                                 uint8_t * bChans,  uint16_t availPiers,
                                 uint16_t tPdGainOverlap, int16_t *pMinCalPower, uint16_t * pPdGainBoundaries,
                                 uint8_t * pPDADCValues, uint16_t numXpdGains)
{

    int       i, j, k;
    int16_t   ss;         /* potentially -ve index for taking care of pdGainOverlap */
    uint16_t  idxL, idxR, numPiers; /* Pier indexes */

    /* filled out Vpd table for all pdGains (chanL) */
    static uint8_t   vpdTableL[AR5416_4K_NUM_PD_GAINS][AR5416_MAX_PWR_RANGE_IN_HALF_DB];

    /* filled out Vpd table for all pdGains (chanR) */
    static uint8_t   vpdTableR[AR5416_4K_NUM_PD_GAINS][AR5416_MAX_PWR_RANGE_IN_HALF_DB];

    /* filled out Vpd table for all pdGains (interpolated) */
    static uint8_t   vpdTableI[AR5416_4K_NUM_PD_GAINS][AR5416_MAX_PWR_RANGE_IN_HALF_DB];

    uint8_t   *pVpdL, *pVpdR, *pPwrL, *pPwrR;
    uint8_t   minPwrT4[AR5416_4K_NUM_PD_GAINS];
    uint8_t   maxPwrT4[AR5416_4K_NUM_PD_GAINS];
    int16_t   vpdStep;
    int16_t   tmpVal;
    uint16_t  sizeCurrVpdTable, maxIndex, tgtIndex;
    HAL_BOOL    match;
    int16_t  minDelta = 0;
    CHAN_CENTERS centers;

    ar5416GetChannelCenters(ah, chan, &centers);

    /* Trim numPiers for the number of populated channel Piers */
    for (numPiers = 0; numPiers < availPiers; numPiers++) {
        if (bChans[numPiers] == AR5416_BCHAN_UNUSED) {
            break;
        }
    }

    /* Find pier indexes around the current channel */
    match = ath_ee_getLowerUpperIndex((uint8_t)FREQ2FBIN(centers.synth_center,
      IEEE80211_IS_CHAN_2GHZ(chan)), bChans, numPiers, &idxL, &idxR);

    if (match) {
        /* Directly fill both vpd tables from the matching index */
        for (i = 0; i < numXpdGains; i++) {
            minPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][0];
            maxPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][4];
            ath_ee_FillVpdTable(minPwrT4[i], maxPwrT4[i],
			       pRawDataSet[idxL].pwrPdg[i],
                               pRawDataSet[idxL].vpdPdg[i],
			       AR5416_PD_GAIN_ICEPTS, vpdTableI[i]);
        }
    } else {
        for (i = 0; i < numXpdGains; i++) {
            pVpdL = pRawDataSet[idxL].vpdPdg[i];
            pPwrL = pRawDataSet[idxL].pwrPdg[i];
            pVpdR = pRawDataSet[idxR].vpdPdg[i];
            pPwrR = pRawDataSet[idxR].pwrPdg[i];

            /* Start Vpd interpolation from the max of the minimum powers */
            minPwrT4[i] = AH_MAX(pPwrL[0], pPwrR[0]);

            /* End Vpd interpolation from the min of the max powers */
            maxPwrT4[i] = AH_MIN(pPwrL[AR5416_PD_GAIN_ICEPTS - 1], pPwrR[AR5416_PD_GAIN_ICEPTS - 1]);
            HALASSERT(maxPwrT4[i] > minPwrT4[i]);

            /* Fill pier Vpds */
            ath_ee_FillVpdTable(minPwrT4[i], maxPwrT4[i], pPwrL, pVpdL,
			       AR5416_PD_GAIN_ICEPTS, vpdTableL[i]);
            ath_ee_FillVpdTable(minPwrT4[i], maxPwrT4[i], pPwrR, pVpdR,
			       AR5416_PD_GAIN_ICEPTS, vpdTableR[i]);

            /* Interpolate the final vpd */
            for (j = 0; j <= (maxPwrT4[i] - minPwrT4[i]) / 2; j++) {
                vpdTableI[i][j] = (uint8_t)(ath_ee_interpolate((uint16_t)FREQ2FBIN(centers.synth_center,
                    IEEE80211_IS_CHAN_2GHZ(chan)),
                    bChans[idxL], bChans[idxR], vpdTableL[i][j], vpdTableR[i][j]));
            }
        }
    }
    *pMinCalPower = (int16_t)(minPwrT4[0] / 2);

    k = 0; /* index for the final table */
    for (i = 0; i < numXpdGains; i++) {
        if (i == (numXpdGains - 1)) {
            pPdGainBoundaries[i] = (uint16_t)(maxPwrT4[i] / 2);
        } else {
            pPdGainBoundaries[i] = (uint16_t)((maxPwrT4[i] + minPwrT4[i+1]) / 4);
        }

        pPdGainBoundaries[i] = (uint16_t)AH_MIN(AR5416_MAX_RATE_POWER, pPdGainBoundaries[i]);

	/* NB: only applies to owl 1.0 */
        if ((i == 0) && !AR_SREV_5416_V20_OR_LATER(ah) ) {
	    /*
             * fix the gain delta, but get a delta that can be applied to min to
             * keep the upper power values accurate, don't think max needs to
             * be adjusted because should not be at that area of the table?
	     */
            minDelta = pPdGainBoundaries[0] - 23;
            pPdGainBoundaries[0] = 23;
        }
        else {
            minDelta = 0;
        }

        /* Find starting index for this pdGain */
        if (i == 0) {
            if (AR_SREV_MERLIN_20_OR_LATER(ah))
                ss = (int16_t)(0 - (minPwrT4[i] / 2));
            else
                ss = 0; /* for the first pdGain, start from index 0 */
        } else {
	    /* need overlap entries extrapolated below. */
            ss = (int16_t)((pPdGainBoundaries[i-1] - (minPwrT4[i] / 2)) - tPdGainOverlap + 1 + minDelta);
        }
        vpdStep = (int16_t)(vpdTableI[i][1] - vpdTableI[i][0]);
        vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);
        /*
         *-ve ss indicates need to extrapolate data below for this pdGain
         */
        while ((ss < 0) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
            tmpVal = (int16_t)(vpdTableI[i][0] + ss * vpdStep);
            pPDADCValues[k++] = (uint8_t)((tmpVal < 0) ? 0 : tmpVal);
            ss++;
        }

        sizeCurrVpdTable = (uint8_t)((maxPwrT4[i] - minPwrT4[i]) / 2 +1);
        tgtIndex = (uint8_t)(pPdGainBoundaries[i] + tPdGainOverlap - (minPwrT4[i] / 2));
        maxIndex = (tgtIndex < sizeCurrVpdTable) ? tgtIndex : sizeCurrVpdTable;

        while ((ss < maxIndex) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
            pPDADCValues[k++] = vpdTableI[i][ss++];
        }

        vpdStep = (int16_t)(vpdTableI[i][sizeCurrVpdTable - 1] - vpdTableI[i][sizeCurrVpdTable - 2]);
        vpdStep = (int16_t)((vpdStep < 1) ? 1 : vpdStep);
        /*
         * for last gain, pdGainBoundary == Pmax_t2, so will
         * have to extrapolate
         */
        if (tgtIndex >= maxIndex) {  /* need to extrapolate above */
            while ((ss <= tgtIndex) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
                tmpVal = (int16_t)((vpdTableI[i][sizeCurrVpdTable - 1] +
                          (ss - maxIndex +1) * vpdStep));
                pPDADCValues[k++] = (uint8_t)((tmpVal > 255) ? 255 : tmpVal);
                ss++;
            }
        }               /* extrapolated above */
    }                   /* for all pdGainUsed */

    /* Fill out pdGainBoundaries - only up to 2 allowed here, but hardware allows up to 4 */
    while (i < AR5416_PD_GAINS_IN_MASK) {
        pPdGainBoundaries[i] = AR5416_4K_EEP_PD_GAIN_BOUNDARY_DEFAULT;
        i++;
    }

    while (k < AR5416_NUM_PDADC_VALUES) {
        pPDADCValues[k] = pPDADCValues[k-1];
        k++;
    }
    return;
}
