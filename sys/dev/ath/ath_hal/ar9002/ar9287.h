/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2010 Atheros Communications, Inc.
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

#ifndef _ATH_AR9287_H_
#define _ATH_AR9287_H_

#include "ar5416/ar5416.h"

/*
 * This is a chip thing, but it's used here as part of the
 * ath_hal_9287 struct; so it's convienent to locate the
 * define here.
 */
#define AR9287_TX_GAIN_TABLE_SIZE		22

struct ath_hal_9287 {
	struct ath_hal_5416 ah_5416;

	HAL_INI_ARRAY	ah_ini_xmodes;
	HAL_INI_ARRAY	ah_ini_rxgain;
	HAL_INI_ARRAY	ah_ini_txgain;

	HAL_INI_ARRAY	ah_ini_cckFirNormal;
	HAL_INI_ARRAY	ah_ini_cckFirJapan2484;

	int PDADCdelta;

	uint32_t	originalGain[AR9287_TX_GAIN_TABLE_SIZE];
};
#define	AH9287(_ah)	((struct ath_hal_9287 *)(_ah))

#define	AR9287_DEFAULT_RXCHAINMASK	3
#define	AR9287_DEFAULT_TXCHAINMASK	3

#define	AR_PHY_CCA_NOM_VAL_9287_2GHZ		-112
#define	AR_PHY_CCA_NOM_VAL_9287_5GHZ		-112
#define	AR_PHY_CCA_MIN_GOOD_VAL_9287_2GHZ	-127
#define	AR_PHY_CCA_MIN_GOOD_VAL_9287_5GHZ	-122
#define	AR_PHY_CCA_MAX_GOOD_VAL_9287_2GHZ	-97
#define	AR_PHY_CCA_MAX_GOOD_VAL_9287_5GHZ	-102

extern	HAL_BOOL ar9287RfAttach(struct ath_hal *, HAL_STATUS *);
extern	HAL_BOOL ar9287SetAntennaSwitch(struct ath_hal *, HAL_ANT_SETTING);

#endif	/* _ATH_AR9287_H_ */
