/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2008-2009 Sam Leffler, Errno Consulting
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
#ifndef _ATH_AR9285_H_
#define _ATH_AR9285_H_

#include "ar5416/ar5416.h"

struct ath_hal_9285 {
	struct ath_hal_5416 ah_5416;

	HAL_INI_ARRAY	ah_ini_txgain;
	HAL_INI_ARRAY	ah_ini_rxgain;

	struct {
		int32_t prev_offset;	/* Previous value of PA offset value */
		int8_t max_skipcount;	/* Max No. of times PACAL can be skipped */
		int8_t skipcount;	/* No. of times the PACAL to be skipped */
	} pacal_info;
};
#define	AH9285(_ah)	((struct ath_hal_9285 *)(_ah))

#define	AR9285_DEFAULT_RXCHAINMASK	1
#define	AR9285_DEFAULT_TXCHAINMASK	1

#define	AR_PHY_CCA_NOM_VAL_9285_2GHZ		-118
#define	AR_PHY_CCA_MIN_GOOD_VAL_9285_2GHZ	-127
#define	AR_PHY_CCA_MAX_GOOD_VAL_9285_2GHZ	-108

HAL_BOOL ar9285RfAttach(struct ath_hal *, HAL_STATUS *);

extern	HAL_BOOL ar9285SetTransmitPower(struct ath_hal *,
		const struct ieee80211_channel *, uint16_t *);
extern HAL_BOOL ar9285SetBoardValues(struct ath_hal *,
		const struct ieee80211_channel *);

/* ar9285_btcoex.h */
extern	void ar9285BTCoexAntennaDiversity(struct ath_hal *ah);
extern	void ar9285BTCoexSetParameter(struct ath_hal *ah,
		u_int32_t value, u_int32_t type);

#endif	/* _ATH_AR9285_H_ */
