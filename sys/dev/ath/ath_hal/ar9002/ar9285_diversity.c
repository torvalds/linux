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
#include "ah_desc.h"
#include "ah_internal.h"
#include "ah_eeprom_v4k.h"

#include "ar9002/ar9280.h"
#include "ar9002/ar9285.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar9002/ar9285phy.h"
#include "ar9002/ar9285_phy.h"

#include "ar9002/ar9285_diversity.h"

/*
 * Set the antenna switch to control RX antenna diversity.
 *
 * If a fixed configuration is used, the LNA and div bias
 * settings are fixed and the antenna diversity scanning routine
 * is disabled.
 *
 * If a variable configuration is used, a default is programmed
 * in and sampling commences per RXed packet.
 *
 * Since this is called from ar9285SetBoardValues() to setup
 * diversity, it means that after a reset or scan, any current
 * software diversity combining settings will be lost and won't
 * re-appear until after the first successful sample run.
 * Please keep this in mind if you're seeing weird performance
 * that happens to relate to scan/diversity timing.
 */
HAL_BOOL
ar9285SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING settings)
{
	int regVal;
	const HAL_EEPROM_v4k *ee = AH_PRIVATE(ah)->ah_eeprom;
	const MODAL_EEP4K_HEADER *pModal = &ee->ee_base.modalHeader;
	uint8_t ant_div_control1, ant_div_control2;

	if (pModal->version < 3) {
		HALDEBUG(ah, HAL_DEBUG_DIVERSITY, "%s: not supported\n",
	    __func__);
		return AH_FALSE;	/* Can't do diversity */
	}

	/* Store settings */
	AH5212(ah)->ah_antControl = settings;
	AH5212(ah)->ah_diversity = (settings == HAL_ANT_VARIABLE);
	
	/* XXX don't fiddle if the PHY is in sleep mode or ! chan */

	/* Begin setting the relevant registers */

	ant_div_control1 = pModal->antdiv_ctl1;
	ant_div_control2 = pModal->antdiv_ctl2;

	regVal = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regVal &= (~(AR_PHY_9285_ANT_DIV_CTL_ALL));

	/* enable antenna diversity only if diversityControl == HAL_ANT_VARIABLE */
	if (settings == HAL_ANT_VARIABLE)
	    regVal |= SM(ant_div_control1, AR_PHY_9285_ANT_DIV_CTL);

	if (settings == HAL_ANT_VARIABLE) {
	    HALDEBUG(ah, HAL_DEBUG_DIVERSITY, "%s: HAL_ANT_VARIABLE\n",
	      __func__);
	    regVal |= SM(ant_div_control2, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
	    regVal |= SM((ant_div_control2 >> 2), AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
	    regVal |= SM((ant_div_control1 >> 1), AR_PHY_9285_ANT_DIV_ALT_GAINTB);
	    regVal |= SM((ant_div_control1 >> 2), AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
	} else {
	    if (settings == HAL_ANT_FIXED_A) {
		/* Diversity disabled, RX = LNA1 */
		HALDEBUG(ah, HAL_DEBUG_DIVERSITY, "%s: HAL_ANT_FIXED_A\n",
		    __func__);
		regVal |= SM(HAL_ANT_DIV_COMB_LNA2, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
		regVal |= SM(HAL_ANT_DIV_COMB_LNA1, AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
		regVal |= SM(AR_PHY_9285_ANT_DIV_GAINTB_0, AR_PHY_9285_ANT_DIV_ALT_GAINTB);
		regVal |= SM(AR_PHY_9285_ANT_DIV_GAINTB_1, AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
	    }
	    else if (settings == HAL_ANT_FIXED_B) {
		/* Diversity disabled, RX = LNA2 */
		HALDEBUG(ah, HAL_DEBUG_DIVERSITY, "%s: HAL_ANT_FIXED_B\n",
		    __func__);
		regVal |= SM(HAL_ANT_DIV_COMB_LNA1, AR_PHY_9285_ANT_DIV_ALT_LNACONF);
		regVal |= SM(HAL_ANT_DIV_COMB_LNA2, AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
		regVal |= SM(AR_PHY_9285_ANT_DIV_GAINTB_1, AR_PHY_9285_ANT_DIV_ALT_GAINTB);
		regVal |= SM(AR_PHY_9285_ANT_DIV_GAINTB_0, AR_PHY_9285_ANT_DIV_MAIN_GAINTB);
	    }
	}

	OS_REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regVal);
	regVal = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regVal = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
	regVal &= (~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	if (settings == HAL_ANT_VARIABLE)
	    regVal |= SM((ant_div_control1 >> 3), AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);

	OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regVal);
	regVal = OS_REG_READ(ah, AR_PHY_CCK_DETECT);

	/*
	 * If Diversity combining is available and the diversity setting
	 * is to allow variable diversity, enable it by default.
	 *
	 * This will be eventually overridden by the software antenna
	 * diversity logic.
	 *
	 * Note that yes, this following section overrides the above
	 * settings for the LNA configuration and fast-bias.
	 */
	if (ar9285_check_div_comb(ah) && AH5212(ah)->ah_diversity == AH_TRUE) {
		// If support DivComb, set MAIN to LNA1 and ALT to LNA2 at the first beginning
		HALDEBUG(ah, HAL_DEBUG_DIVERSITY,
		    "%s: Enable initial settings for combined diversity\n",
		    __func__);
		regVal = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
		regVal &= (~(AR_PHY_9285_ANT_DIV_MAIN_LNACONF | AR_PHY_9285_ANT_DIV_ALT_LNACONF));
		regVal |= (HAL_ANT_DIV_COMB_LNA1 << AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S);
		regVal |= (HAL_ANT_DIV_COMB_LNA2 << AR_PHY_9285_ANT_DIV_ALT_LNACONF_S);
		regVal &= (~(AR_PHY_9285_FAST_DIV_BIAS));
		regVal |= (0 << AR_PHY_9285_FAST_DIV_BIAS_S);
		OS_REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regVal);
	}

	return AH_TRUE;
}
