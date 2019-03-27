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
#ifndef _ATH_AR5312_H_
#define _ATH_AR5312_H_

#include "ah_soc.h"
#include "ar5212/ar5212.h"

#define AR5312_UNIT(_ah) \
	(((const struct ar531x_config *)((_ah)->ah_st))->unit)
#define AR5312_BOARDCONFIG(_ah) \
	(((const struct ar531x_config *)((_ah)->ah_st))->board)
#define AR5312_RADIOCONFIG(_ah) \
	(((const struct ar531x_config *)((_ah)->ah_st))->radio)

#define	IS_5312_2_X(ah) \
	(AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_VENICE && \
	 (AH_PRIVATE(ah)->ah_macRev == 2 || AH_PRIVATE(ah)->ah_macRev == 7))
#define IS_5315(ah) \
	(AH_PRIVATE(ah)->ah_devid == AR5212_AR2315_REV6 || \
	 AH_PRIVATE(ah)->ah_devid == AR5212_AR2315_REV7 || \
	 AH_PRIVATE(ah)->ah_devid == AR5212_AR2317_REV1 || \
	 AH_PRIVATE(ah)->ah_devid == AR5212_AR2317_REV2)

extern  HAL_BOOL ar5312IsInterruptPending(struct ath_hal *ah);

/* AR5312 */
extern	HAL_BOOL ar5312GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar5312GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5312GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	uint32_t ar5312GpioGet(struct ath_hal *ah, uint32_t gpio);
extern	void ar5312GpioSetIntr(struct ath_hal *ah, u_int, uint32_t ilevel);

/* AR2315+ */
extern	HAL_BOOL ar5315GpioCfgOutput(struct ath_hal *, uint32_t gpio,
		HAL_GPIO_MUX_TYPE);
extern	HAL_BOOL ar5315GpioCfgInput(struct ath_hal *, uint32_t gpio);
extern	HAL_BOOL ar5315GpioSet(struct ath_hal *, uint32_t gpio, uint32_t val);
extern	uint32_t ar5315GpioGet(struct ath_hal *ah, uint32_t gpio);
extern	void ar5315GpioSetIntr(struct ath_hal *ah, u_int, uint32_t ilevel);

extern  void ar5312SetLedState(struct ath_hal *ah, HAL_LED_STATE state);
extern  HAL_BOOL ar5312DetectCardPresent(struct ath_hal *ah);
extern  void ar5312SetupClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern  void ar5312RestoreClock(struct ath_hal *ah, HAL_OPMODE opmode);
extern  void ar5312DumpState(struct ath_hal *ah);
extern  HAL_BOOL ar5312Reset(struct ath_hal *ah, HAL_OPMODE opmode,
	    struct ieee80211_channel *chan,
	      HAL_BOOL bChannelChange,
	      HAL_RESET_TYPE resetType,
	      HAL_STATUS *status);
extern  HAL_BOOL ar5312ChipReset(struct ath_hal *ah,
	      struct ieee80211_channel *chan);
extern  HAL_BOOL ar5312SetPowerMode(struct ath_hal *ah, HAL_POWER_MODE mode,
                                    int setChip);
extern  HAL_BOOL ar5312PhyDisable(struct ath_hal *ah);
extern  HAL_BOOL ar5312Disable(struct ath_hal *ah);
extern  HAL_BOOL ar5312MacReset(struct ath_hal *ah, unsigned int RCMask);
extern  uint32_t ar5312GetPowerMode(struct ath_hal *ah);
extern  HAL_BOOL ar5312GetPowerStatus(struct ath_hal *ah);

/* BSP functions */
extern	HAL_BOOL ar5312EepromRead(struct ath_hal *, u_int off, uint16_t *data);
extern	HAL_BOOL ar5312EepromWrite(struct ath_hal *, u_int off, uint16_t data);

#endif	/* _ATH_AR3212_H_ */
