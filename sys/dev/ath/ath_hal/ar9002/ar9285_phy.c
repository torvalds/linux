/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2010 Atheros Communications Inc.
 * Copyright (c) 2010-2011 Adrian Chadd, Xenion Pty Ltd.
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
#include "ah_devid.h"
#include "ah_eeprom_v4k.h"

#include "ar9002/ar9280.h"
#include "ar9002/ar9285.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar9002/ar9285phy.h"
#include "ar9002/ar9285_phy.h"

void
ar9285_antdiv_comb_conf_get(struct ath_hal *ah, HAL_ANT_COMB_CONFIG *antconf)
{
	uint32_t regval;

	regval = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	antconf->main_lna_conf = (regval & AR_PHY_9285_ANT_DIV_MAIN_LNACONF) >>
				  AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S;
	antconf->alt_lna_conf = (regval & AR_PHY_9285_ANT_DIV_ALT_LNACONF) >>
				 AR_PHY_9285_ANT_DIV_ALT_LNACONF_S;
	antconf->fast_div_bias = (regval & AR_PHY_9285_FAST_DIV_BIAS) >>
				  AR_PHY_9285_FAST_DIV_BIAS_S;
	antconf->antdiv_configgroup = DEFAULT_ANTDIV_CONFIG_GROUP;
}

void
ar9285_antdiv_comb_conf_set(struct ath_hal *ah, HAL_ANT_COMB_CONFIG *antconf)
{
	uint32_t regval;

	regval = OS_REG_READ(ah, AR_PHY_MULTICHAIN_GAIN_CTL);
	regval &= ~(AR_PHY_9285_ANT_DIV_MAIN_LNACONF |
		    AR_PHY_9285_ANT_DIV_ALT_LNACONF |
		    AR_PHY_9285_FAST_DIV_BIAS);
	regval |= ((antconf->main_lna_conf << AR_PHY_9285_ANT_DIV_MAIN_LNACONF_S)
		   & AR_PHY_9285_ANT_DIV_MAIN_LNACONF);
	regval |= ((antconf->alt_lna_conf << AR_PHY_9285_ANT_DIV_ALT_LNACONF_S)
		   & AR_PHY_9285_ANT_DIV_ALT_LNACONF);
	regval |= ((antconf->fast_div_bias << AR_PHY_9285_FAST_DIV_BIAS_S)
		   & AR_PHY_9285_FAST_DIV_BIAS);

	OS_REG_WRITE(ah, AR_PHY_MULTICHAIN_GAIN_CTL, regval);
}

/*
 * Check whether combined + fast antenna diversity should be enabled.
 *
 * This enables software-driven RX antenna diversity based on RX
 * RSSI + antenna config packet sampling.
 */
HAL_BOOL
ar9285_check_div_comb(struct ath_hal *ah)
{
	uint8_t ant_div_ctl1;
	HAL_EEPROM_v4k *ee = AH_PRIVATE(ah)->ah_eeprom;
        const MODAL_EEP4K_HEADER *pModal = &ee->ee_base.modalHeader;

#if 0
	/* For now, simply disable this until it's better debugged. -adrian */
	return AH_FALSE;
#endif

	if (! AR_SREV_KITE(ah))
		return AH_FALSE;

	if (pModal->version < 3)
		return AH_FALSE;

	ant_div_ctl1 = pModal->antdiv_ctl1;
	if ((ant_div_ctl1 & 0x1) && ((ant_div_ctl1 >> 3) & 0x1))
		return AH_TRUE;

	return AH_FALSE;
}
