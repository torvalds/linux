/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"

/*
 * Notify Power Mgt is enabled in self-generated frames.
 * If requested, force chip awake.
 *
 * Returns A_OK if chip is awake or successfully forced awake.
 *
 * WARNING WARNING WARNING
 * There is a problem with the chip where sometimes it will not wake up.
 */
static HAL_BOOL
ar5416SetPowerModeAwake(struct ath_hal *ah, int setChip)
{
#define	POWER_UP_TIME	200000
	uint32_t val;
	int i = 0;

	if (setChip) {
		/*
		 * Do a Power-On-Reset if OWL is shutdown
		 * the NetBSD driver  power-cycles the Cardbus slot
		 * as part of the reset procedure.
		 */
		if ((OS_REG_READ(ah, AR_RTC_STATUS) 
			& AR_RTC_PM_STATUS_M) == AR_RTC_STATUS_SHUTDOWN) {
			if (!ar5416SetResetReg(ah, HAL_RESET_POWER_ON))
				goto bad;			
			AH5416(ah)->ah_initPLL(ah, AH_NULL);
		}

		if (AR_SREV_HOWL(ah))
			OS_REG_SET_BIT(ah, AR_RTC_RESET, AR_RTC_RESET_EN);

		OS_REG_SET_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
		if (AR_SREV_HOWL(ah))
			OS_DELAY(10000);
		else
			OS_DELAY(50);   /* Give chip the chance to awake */

		for (i = POWER_UP_TIME / 50; i != 0; i--) {
			val = OS_REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;
			if (val == AR_RTC_STATUS_ON)
				break;
			OS_DELAY(50);
			OS_REG_SET_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
		}		
	bad:
		if (i == 0) {
#ifdef AH_DEBUG
			ath_hal_printf(ah, "%s: Failed to wakeup in %ums\n",
				__func__, POWER_UP_TIME/1000);
#endif
			return AH_FALSE;
		}
	} 

	OS_REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	return AH_TRUE;
#undef POWER_UP_TIME
}

/*
 * Notify Power Mgt is disabled in self-generated frames.
 * If requested, force chip to sleep.
 */
static void
ar5416SetPowerModeSleep(struct ath_hal *ah, int setChip)
{
	OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		/* Clear the RTC force wake bit to allow the mac to sleep */
		OS_REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
		if (! AR_SREV_HOWL(ah))
			OS_REG_WRITE(ah, AR_RC, AR_RC_AHB|AR_RC_HOSTIF);
		/* Shutdown chip. Active low */
		if (! AR_SREV_OWL(ah))
			OS_REG_CLR_BIT(ah, AR_RTC_RESET, AR_RTC_RESET_EN);
	}
}

/*
 * Notify Power Management is enabled in self-generating
 * fames.  If request, set power mode of chip to
 * auto/normal.  Duration in units of 128us (1/8 TU).
 */
static void
ar5416SetPowerModeNetworkSleep(struct ath_hal *ah, int setChip)
{
	OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	
	if (setChip)
		OS_REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
}

/*
 * Set power mgt to the requested mode, and conditionally set
 * the chip as well
 */
HAL_BOOL
ar5416SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode, int setChip)
{
#ifdef AH_DEBUG
	static const char* modes[] = {
		"AWAKE",
		"FULL-SLEEP",
		"NETWORK SLEEP",
		"UNDEFINED"
	};
#endif
	int status = AH_TRUE;

#if 0
	if (!setChip)
		return AH_TRUE;
#endif

	HALDEBUG(ah, HAL_DEBUG_POWER, "%s: %s -> %s (%s)\n", __func__,
	    modes[ah->ah_powerMode], modes[mode], setChip ? "set chip " : "");
	switch (mode) {
	case HAL_PM_AWAKE:
		if (setChip)
			ah->ah_powerMode = mode;
		status = ar5416SetPowerModeAwake(ah, setChip);
		break;
	case HAL_PM_FULL_SLEEP:
		ar5416SetPowerModeSleep(ah, setChip);
		if (setChip)
			ah->ah_powerMode = mode;
		break;
	case HAL_PM_NETWORK_SLEEP:
		ar5416SetPowerModeNetworkSleep(ah, setChip);
		if (setChip)
			ah->ah_powerMode = mode;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown power mode 0x%x\n",
		    __func__, mode);
		return AH_FALSE;
	}
	return status;
}

/*
 * Return the current sleep mode of the chip
 */
HAL_POWER_MODE
ar5416GetPowerMode(struct ath_hal *ah)
{
	int mode = OS_REG_READ(ah, AR_RTC_STATUS);
	switch (mode & AR_RTC_PM_STATUS_M) {
	case AR_RTC_STATUS_ON:
	case AR_RTC_STATUS_WAKEUP:
		return HAL_PM_AWAKE;
	case AR_RTC_STATUS_SLEEP:
		return HAL_PM_NETWORK_SLEEP;
	case AR_RTC_STATUS_SHUTDOWN:
		return HAL_PM_FULL_SLEEP;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unknown power mode, RTC_STATUS 0x%x\n",
		    __func__, mode);
		return HAL_PM_UNDEFINED;	
	}
}
