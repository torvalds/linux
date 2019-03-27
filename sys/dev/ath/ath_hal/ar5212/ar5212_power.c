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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212desc.h"

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
ar5212SetPowerModeAwake(struct ath_hal *ah, int setChip)
{
#define	AR_SCR_MASK \
    (AR_SCR_SLDUR|AR_SCR_SLE|AR_SCR_SLDTP|AR_SCR_SLDWP|\
     AR_SCR_SLEPOL|AR_SCR_MIBIE|AR_SCR_UNKNOWN)
#define	POWER_UP_TIME	2000
	uint32_t scr, val;
	int i;

	if (setChip) {
		/*
		 * Be careful setting the AWAKE mode.  When we are called
		 * with the chip powered down the read returns 0xffffffff
		 * which when blindly written back with OS_REG_RMW_FIELD 
		 * enables the MIB interrupt for the sleep performance
		 * counters.  This can result in an interrupt storm when
		 * ANI is in operation as no one knows to turn off the MIB
		 * interrupt cause.
		 */
		scr = OS_REG_READ(ah, AR_SCR);
		if (scr & ~AR_SCR_MASK) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: bogus SCR 0x%x, PCICFG 0x%x\n",
			    __func__, scr, OS_REG_READ(ah, AR_PCICFG));
			scr = 0;
		}
		scr = (scr &~ AR_SCR_SLE) | AR_SCR_SLE_WAKE;
		OS_REG_WRITE(ah, AR_SCR, scr);
		OS_DELAY(10);	/* Give chip the chance to awake */

		for (i = POWER_UP_TIME / 50; i != 0; i--) {
			val = OS_REG_READ(ah, AR_PCICFG);
			if ((val & AR_PCICFG_SPWR_DN) == 0)
				break;
			OS_DELAY(50);
			OS_REG_WRITE(ah, AR_SCR, scr);
		}
		if (i == 0) {
#ifdef AH_DEBUG
			ath_hal_printf(ah, "%s: Failed to wakeup in %ums\n",
				__func__, POWER_UP_TIME/50);
#endif
			return AH_FALSE;
		}
	} 

	OS_REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	return AH_TRUE;
#undef POWER_UP_TIME
#undef AR_SCR_MASK
}

/*
 * Notify Power Mgt is disabled in self-generated frames.
 * If requested, force chip to sleep.
 */
static void
ar5212SetPowerModeSleep(struct ath_hal *ah, int setChip)
{
	OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip)
		OS_REG_RMW_FIELD(ah, AR_SCR, AR_SCR_SLE, AR_SCR_SLE_SLP);
}

/*
 * Notify Power Management is enabled in self-generating
 * fames.  If request, set power mode of chip to
 * auto/normal.  Duration in units of 128us (1/8 TU).
 */
static void
ar5212SetPowerModeNetworkSleep(struct ath_hal *ah, int setChip)
{
	OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip)
		OS_REG_RMW_FIELD(ah, AR_SCR, AR_SCR_SLE, AR_SCR_SLE_NORM);
}

/*
 * Set power mgt to the requested mode, and conditionally set
 * the chip as well
 */
HAL_BOOL
ar5212SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode, int setChip)
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

	HALDEBUG(ah, HAL_DEBUG_POWER, "%s: %s -> %s (%s)\n", __func__,
		modes[ah->ah_powerMode], modes[mode],
		setChip ? "set chip " : "");
	switch (mode) {
	case HAL_PM_AWAKE:
		if (setChip)
			ah->ah_powerMode = mode;
		status = ar5212SetPowerModeAwake(ah, setChip);
		break;
	case HAL_PM_FULL_SLEEP:
		ar5212SetPowerModeSleep(ah, setChip);
		if (setChip)
			ah->ah_powerMode = mode;
		break;
	case HAL_PM_NETWORK_SLEEP:
		ar5212SetPowerModeNetworkSleep(ah, setChip);
		if (setChip)
			ah->ah_powerMode = mode;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unknown power mode %u\n",
		    __func__, mode);
		return AH_FALSE;
	}
	return status;
}

/*
 * Return the current sleep mode of the chip
 */
HAL_POWER_MODE
ar5212GetPowerMode(struct ath_hal *ah)
{
	/* Just so happens the h/w maps directly to the abstracted value */
	return MS(OS_REG_READ(ah, AR_SCR), AR_SCR_SLE);
}

#if 0
/*
 * Return the current sleep state of the chip
 * TRUE = sleeping
 */
HAL_BOOL
ar5212GetPowerStatus(struct ath_hal *ah)
{
	return (OS_REG_READ(ah, AR_PCICFG) & AR_PCICFG_SPWR_DN) != 0;
}
#endif
