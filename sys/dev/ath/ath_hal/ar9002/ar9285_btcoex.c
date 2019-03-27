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
#ifdef AH_DEBUG
#include "ah_desc.h"		    /* NB: for HAL_PHYERR* */
#endif

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar5416/ar5416desc.h" /* AR5416_CONTTXMODE */

#include "ar9002/ar9285phy.h"
#include "ar9002/ar9285.h"

/*
 * This is specific to Kite.
 *
 * Kiwi and others don't have antenna diversity like this.
 */
void
ar9285BTCoexAntennaDiversity(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u_int32_t regVal;
	u_int8_t ant_div_control1, ant_div_control2;

	HALDEBUG(ah, HAL_DEBUG_BT_COEX,
	    "%s: btCoexFlag: ALLOW=%d, ENABLE=%d\n",
	    __func__,
	    !! (ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_ANT_DIV_ALLOW),
	    !! (ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_ANT_DIV_ENABLE));

	if ((ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_ANT_DIV_ALLOW) ||
	    (AH5212(ah)->ah_diversity != HAL_ANT_VARIABLE)) {
	if ((ahp->ah_btCoexFlag & HAL_BT_COEX_FLAG_ANT_DIV_ENABLE) &&
	     (AH5212(ah)->ah_antControl == HAL_ANT_VARIABLE)) {
		/* Enable antenna diversity */
		ant_div_control1 = HAL_BT_COEX_ANTDIV_CONTROL1_ENABLE;
		ant_div_control2 = HAL_BT_COEX_ANTDIV_CONTROL2_ENABLE;

		/* Don't disable BT ant to allow BB to control SWCOM */
		ahp->ah_btCoexMode2 &= (~(AR_BT_DISABLE_BT_ANT));
		OS_REG_WRITE(ah, AR_BT_COEX_MODE2, ahp->ah_btCoexMode2);

		/* Program the correct SWCOM table */
		OS_REG_WRITE(ah, AR_PHY_SWITCH_COM,
		    HAL_BT_COEX_ANT_DIV_SWITCH_COM);
		OS_REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, 0, 0xf0000000);
	} else if (AH5212(ah)->ah_antControl == HAL_ANT_FIXED_B) {
		/* Disable antenna diversity. Use antenna B(LNA2) only. */
		ant_div_control1 = HAL_BT_COEX_ANTDIV_CONTROL1_FIXED_B;
		ant_div_control2 = HAL_BT_COEX_ANTDIV_CONTROL2_FIXED_B;

		/* Disable BT ant to allow concurrent BT and WLAN receive */
		ahp->ah_btCoexMode2 |= AR_BT_DISABLE_BT_ANT;
		OS_REG_WRITE(ah, AR_BT_COEX_MODE2, ahp->ah_btCoexMode2);

		/*
		 * Program SWCOM table to make sure RF switch always parks
		 * at WLAN side
		 */
		OS_REG_WRITE(ah, AR_PHY_SWITCH_COM,
		    HAL_BT_COEX_ANT_DIV_SWITCH_COM);
		OS_REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, 0x60000000, 0xf0000000);
	} else {
		/* Disable antenna diversity. Use antenna A(LNA1) only */
		ant_div_control1 = HAL_BT_COEX_ANTDIV_CONTROL1_FIXED_A;
		ant_div_control2 = HAL_BT_COEX_ANTDIV_CONTROL2_FIXED_A;

		/* Disable BT ant to allow concurrent BT and WLAN receive */
		ahp->ah_btCoexMode2 |= AR_BT_DISABLE_BT_ANT;
		OS_REG_WRITE(ah, AR_BT_COEX_MODE2, ahp->ah_btCoexMode2);

		/*
		 * Program SWCOM table to make sure RF switch always
		 * parks at BT side
		 */
		OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, 0);
		OS_REG_RMW(ah, AR_PHY_SWITCH_CHAIN_0, 0, 0xf0000000);
	}

	regVal = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regVal &= (~(AR_PHY_9285_ANT_DIV_CTL_ALL));
	/*
	 * Clear ant_fast_div_bias [14:9] since for Janus the main LNA is
	 * always LNA1.
	 */
	regVal &= (~(AR_PHY_9285_FAST_DIV_BIAS));

	regVal |= SM(ant_div_control1, AR_PHY_9285_ANT_DIV_CTL);
	regVal |= SM(ant_div_control2, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
	regVal |= SM((ant_div_control2 >> 2), AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
	regVal |= SM((ant_div_control1 >> 1), AR_PHY_9285_ANT_DIV_ALT_GAINTB);
	regVal |= SM((ant_div_control1 >> 2), AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
	OS_REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regVal);

	regVal = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
	regVal &= (~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	regVal |= SM((ant_div_control1 >> 3),
	    AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regVal);
    }
}

void
ar9285BTCoexSetParameter(struct ath_hal *ah, u_int32_t type, u_int32_t value)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	switch (type) {
	case HAL_BT_COEX_ANTENNA_DIVERSITY:
		if (AR_SREV_KITE(ah)) {
			ahp->ah_btCoexFlag |= HAL_BT_COEX_FLAG_ANT_DIV_ALLOW;
			if (value)
				ahp->ah_btCoexFlag |=
				    HAL_BT_COEX_FLAG_ANT_DIV_ENABLE;
			else
				ahp->ah_btCoexFlag &=
				    ~HAL_BT_COEX_FLAG_ANT_DIV_ENABLE;
			ar9285BTCoexAntennaDiversity(ah);
		}
		break;
	default:
		ar5416BTCoexSetParameter(ah, type, value);
		break;
	}
}


