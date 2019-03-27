/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2005 Atheros Communications, Inc.
 * Copyright (c) 2008-2010, Atheros Communications Inc.
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
#ifdef	AH_DEBUG
#include "ah_desc.h"                    /* NB: for HAL_PHYERR* */
#endif

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar5416/ar5416desc.h" /* AR5416_CONTTXMODE */
#include "ar5416/ar5416_btcoex.h"

void
ar5416SetBTCoexInfo(struct ath_hal *ah, HAL_BT_COEX_INFO *btinfo)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_btModule = btinfo->bt_module;
	ahp->ah_btCoexConfigType = btinfo->bt_coex_config;
	ahp->ah_btActiveGpioSelect = btinfo->bt_gpio_bt_active;
	ahp->ah_btPriorityGpioSelect = btinfo->bt_gpio_bt_priority;
	ahp->ah_wlanActiveGpioSelect = btinfo->bt_gpio_wlan_active;
	ahp->ah_btActivePolarity = btinfo->bt_active_polarity;
	ahp->ah_btCoexSingleAnt = btinfo->bt_single_ant;
	ahp->ah_btWlanIsolation = btinfo->bt_isolation;
}

void
ar5416BTCoexConfig(struct ath_hal *ah, HAL_BT_COEX_CONFIG *btconf)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	HAL_BOOL rxClearPolarity = btconf->bt_rxclear_polarity;

	/*
	 * For Kiwi and Osprey, the polarity of rx_clear is active high.
	 * The bt_rxclear_polarity flag from ath(4) needs to be inverted.
	 */
	if (AR_SREV_KIWI(ah)) {
		rxClearPolarity = !btconf->bt_rxclear_polarity;
	}

	ahp->ah_btCoexMode = (ahp->ah_btCoexMode & AR_BT_QCU_THRESH) |
	    SM(btconf->bt_time_extend, AR_BT_TIME_EXTEND) |
	    SM(btconf->bt_txstate_extend, AR_BT_TXSTATE_EXTEND) |
	    SM(btconf->bt_txframe_extend, AR_BT_TX_FRAME_EXTEND) |
	    SM(btconf->bt_mode, AR_BT_MODE) |
	    SM(btconf->bt_quiet_collision, AR_BT_QUIET) |
	    SM(rxClearPolarity, AR_BT_RX_CLEAR_POLARITY) |
	    SM(btconf->bt_priority_time, AR_BT_PRIORITY_TIME) |
	    SM(btconf->bt_first_slot_time, AR_BT_FIRST_SLOT_TIME);

	ahp->ah_btCoexMode2 |= SM(btconf->bt_hold_rxclear,
	    AR_BT_HOLD_RX_CLEAR);

	if (ahp->ah_btCoexSingleAnt == AH_FALSE) {
		/* Enable ACK to go out even though BT has higher priority. */
		ahp->ah_btCoexMode2 |= AR_BT_DISABLE_BT_ANT;
	}
}

void
ar5416BTCoexSetQcuThresh(struct ath_hal *ah, int qnum)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_btCoexMode |= SM(qnum, AR_BT_QCU_THRESH);
}

void
ar5416BTCoexSetWeights(struct ath_hal *ah, u_int32_t stompType)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (AR_SREV_KIWI_10_OR_LATER(ah)) {
		/* TODO: TX RX separate is not enabled. */
		switch (stompType) {
		case HAL_BT_COEX_STOMP_AUDIO:
			/* XXX TODO */
		case HAL_BT_COEX_STOMP_ALL:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_ALL_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_LOW:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_LOW_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_ALL_FORCE:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight =
			    AR5416_STOMP_ALL_FORCE_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_LOW_FORCE:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight =
			    AR5416_STOMP_LOW_FORCE_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_NONE:
		case HAL_BT_COEX_NO_STOMP:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_NONE_WLAN_WGHT;
			break;
		default:
			/* There is a forceWeight from registry */
			ahp->ah_btCoexBTWeight = stompType & 0xffff;
			ahp->ah_btCoexWLANWeight = stompType >> 16;
			break;
		}
	} else {
		switch (stompType) {
		case HAL_BT_COEX_STOMP_AUDIO:
			/* XXX TODO */
		case HAL_BT_COEX_STOMP_ALL:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_ALL_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_LOW:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_LOW_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_ALL_FORCE:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight =
			    AR5416_STOMP_ALL_FORCE_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_LOW_FORCE:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight =
			    AR5416_STOMP_LOW_FORCE_WLAN_WGHT;
			break;
		case HAL_BT_COEX_STOMP_NONE:
		case HAL_BT_COEX_NO_STOMP:
			ahp->ah_btCoexBTWeight = AR5416_BT_WGHT;
			ahp->ah_btCoexWLANWeight = AR5416_STOMP_NONE_WLAN_WGHT;
			break;
		default:
			/* There is a forceWeight from registry */
			ahp->ah_btCoexBTWeight = stompType & 0xffff;
			ahp->ah_btCoexWLANWeight = stompType >> 16;
			break;
		}
	}
}

void
ar5416BTCoexSetupBmissThresh(struct ath_hal *ah, u_int32_t thresh)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_btCoexMode2 |= SM(thresh, AR_BT_BCN_MISS_THRESH);
}

/*
 * There is no antenna diversity for Owl, Kiwi, etc.
 *
 * Kite will override this particular method.
 */
void
ar5416BTCoexAntennaDiversity(struct ath_hal *ah)
{
}

void
ar5416BTCoexSetParameter(struct ath_hal *ah, u_int32_t type, u_int32_t value)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	switch (type) {
	case HAL_BT_COEX_SET_ACK_PWR:
		if (value) {
			ahp->ah_btCoexFlag |= HAL_BT_COEX_FLAG_LOW_ACK_PWR;
			OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_LOW_ACK_POWER);
		} else {
			ahp->ah_btCoexFlag &= ~HAL_BT_COEX_FLAG_LOW_ACK_PWR;
			OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_HIGH_ACK_POWER);
		}
		break;
	case HAL_BT_COEX_ANTENNA_DIVERSITY:
		/* This is overridden for Kite */
		break;
#if 0
        case HAL_BT_COEX_LOWER_TX_PWR:
            if (value) {
                if ((ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_LOWER_TX_PWR) == 0) {
                    ahp->ah_btCoexFlag |= HAL_BT_COEX_FLAG_LOWER_TX_PWR;
		    AH_PRIVATE(ah)->ah_config.ath_hal_desc_tpc = 1;
                    ar5416SetTxPowerLimit(ah, AH_PRIVATE(ah)->ah_power_limit, AH_PRIVATE(ah)->ah_extra_txpow, 0);
                }
            }
            else {
                if (ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_LOWER_TX_PWR) {
                    ahp->ah_btCoexFlag &= ~HAL_BT_COEX_FLAG_LOWER_TX_PWR;
		    AH_PRIVATE(ah)->ah_config.ath_hal_desc_tpc = 0;
                    ar5416SetTxPowerLimit(ah, AH_PRIVATE(ah)->ah_power_limit, AH_PRIVATE(ah)->ah_extra_txpow, 0);
                }
            }
            break;
#endif
	default:
			break;
	}
}

void
ar5416BTCoexDisable(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	/* Always drive rx_clear_external output as 0 */
	ar5416GpioSet(ah, ahp->ah_wlanActiveGpioSelect, 0);
	ar5416GpioCfgOutput(ah, ahp->ah_wlanActiveGpioSelect,
	    HAL_GPIO_OUTPUT_MUX_AS_OUTPUT);

	if (AR_SREV_9271(ah)) {
		/*
		 * Set wlanActiveGpio to input when disabling BT-COEX to
		 * reduce power consumption
		 */
		ar5416GpioCfgInput(ah, ahp->ah_wlanActiveGpioSelect);
	}

	if (ahp->ah_btCoexSingleAnt == AH_TRUE) {
		OS_REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE,
		    1);
		OS_REG_RMW_FIELD(ah, AR_MISC_MODE, AR_PCU_BT_ANT_PREVENT_RX,
		    0);
	}

	OS_REG_WRITE(ah, AR_BT_COEX_MODE, AR_BT_QUIET | AR_BT_MODE);
	OS_REG_WRITE(ah, AR_BT_COEX_WEIGHT, 0);
	if (AR_SREV_KIWI_10_OR_LATER(ah))
		OS_REG_WRITE(ah, AR_BT_COEX_WEIGHT2, 0);
	OS_REG_WRITE(ah, AR_BT_COEX_MODE2, 0);

	ahp->ah_btCoexEnabled = AH_FALSE;
}

int
ar5416BTCoexEnable(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	/* Program coex mode and weight registers to actually enable coex */
	OS_REG_WRITE(ah, AR_BT_COEX_MODE, ahp->ah_btCoexMode);
	OS_REG_WRITE(ah, AR_BT_COEX_WEIGHT,
	    SM(ahp->ah_btCoexWLANWeight & 0xFFFF, AR_BT_WL_WGHT) |
	    SM(ahp->ah_btCoexBTWeight & 0xFFFF, AR_BT_BT_WGHT));
	if (AR_SREV_KIWI_10_OR_LATER(ah)) {
	OS_REG_WRITE(ah, AR_BT_COEX_WEIGHT2,
	    SM(ahp->ah_btCoexWLANWeight >> 16, AR_BT_WL_WGHT));
	}
	OS_REG_WRITE(ah, AR_BT_COEX_MODE2, ahp->ah_btCoexMode2);

	/* Added Select GPIO5~8 instaed SPI */
	if (AR_SREV_9271(ah)) {
		uint32_t val;

		val = OS_REG_READ(ah, AR9271_CLOCK_CONTROL);
		val &= 0xFFFFFEFF;
		OS_REG_WRITE(ah, AR9271_CLOCK_CONTROL, val);
	}

	if (ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_LOW_ACK_PWR)
		OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_LOW_ACK_POWER);
	else
		OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_HIGH_ACK_POWER);

	if (ahp->ah_btCoexSingleAnt == AH_TRUE) {
		OS_REG_RMW_FIELD(ah, AR_QUIET1,
		    AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
		/* XXX should update miscMode? */
		OS_REG_RMW_FIELD(ah, AR_MISC_MODE,
		    AR_PCU_BT_ANT_PREVENT_RX, 1);
	} else {
		OS_REG_RMW_FIELD(ah, AR_QUIET1,
		    AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
		/* XXX should update miscMode? */
		OS_REG_RMW_FIELD(ah, AR_MISC_MODE,
		    AR_PCU_BT_ANT_PREVENT_RX, 0);
	}

	if (ahp->ah_btCoexConfigType == HAL_BT_COEX_CFG_3WIRE) {
		/* For 3-wire, configure the desired GPIO port for rx_clear */
		ar5416GpioCfgOutput(ah, ahp->ah_wlanActiveGpioSelect,
		    HAL_GPIO_OUTPUT_MUX_AS_WLAN_ACTIVE);
	} else {
		/*
		 * For 2-wire, configure the desired GPIO port
		 * for TX_FRAME output
		 */
		ar5416GpioCfgOutput(ah, ahp->ah_wlanActiveGpioSelect,
		    HAL_GPIO_OUTPUT_MUX_AS_TX_FRAME);
	}

	/*
	 * Enable a weak pull down on BT_ACTIVE.
	 * When BT device is disabled, BT_ACTIVE might be floating.
	 */
	OS_REG_RMW(ah, AR_GPIO_PDPU,
	    (0x2 << (ahp->ah_btActiveGpioSelect * 2)),
	    (0x3 << (ahp->ah_btActiveGpioSelect * 2)));

	ahp->ah_btCoexEnabled = AH_TRUE;

	return (0);
}

void
ar5416InitBTCoex(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	HALDEBUG(ah, HAL_DEBUG_BT_COEX,
	    "%s: called; configType=%d\n",
	    __func__,
	    ahp->ah_btCoexConfigType);

	if (ahp->ah_btCoexConfigType == HAL_BT_COEX_CFG_3WIRE) {
		OS_REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    (AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB |
		    AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB));

		/*
		 * Set input mux for bt_prority_async and
		 * bt_active_async to GPIO pins
		 */
		OS_REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
		    AR_GPIO_INPUT_MUX1_BT_ACTIVE,
		    ahp->ah_btActiveGpioSelect);
		OS_REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
		    AR_GPIO_INPUT_MUX1_BT_PRIORITY,
		    ahp->ah_btPriorityGpioSelect);

		/*
		 * Configure the desired GPIO ports for input
		 */
		ar5416GpioCfgInput(ah, ahp->ah_btActiveGpioSelect);
		ar5416GpioCfgInput(ah, ahp->ah_btPriorityGpioSelect);

		/*
		 * Configure the antenna diversity setup.
		 * It's a no-op for AR9287; AR9285 overrides this
		 * as required.
		 */
		AH5416(ah)->ah_btCoexSetDiversity(ah);

		if (ahp->ah_btCoexEnabled)
			ar5416BTCoexEnable(ah);
		else
			ar5416BTCoexDisable(ah);
	} else if (ahp->ah_btCoexConfigType != HAL_BT_COEX_CFG_NONE) {
		/* 2-wire */
		if (ahp->ah_btCoexEnabled) {
			/* Connect bt_active_async to baseband */
			OS_REG_CLR_BIT(ah, AR_GPIO_INPUT_EN_VAL,
			    (AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF |
			     AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF));
			OS_REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
			    AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB);

			/*
			 * Set input mux for bt_prority_async and
			 * bt_active_async to GPIO pins
			 */
			OS_REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
			    AR_GPIO_INPUT_MUX1_BT_ACTIVE,
                            ahp->ah_btActiveGpioSelect);

			/* Configure the desired GPIO ports for input */
			ar5416GpioCfgInput(ah, ahp->ah_btActiveGpioSelect);

			/* Enable coexistence on initialization */
			ar5416BTCoexEnable(ah);
		}
	}
}
