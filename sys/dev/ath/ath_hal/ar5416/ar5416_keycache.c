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

static const int keyType[] = {
	1,	/* HAL_CIPHER_WEP */
	0,	/* HAL_CIPHER_AES_OCB */
	2,	/* HAL_CIPHER_AES_CCM */
	0,	/* HAL_CIPHER_CKIP */
	3,	/* HAL_CIPHER_TKIP */
	0,	/* HAL_CIPHER_CLR */
};

/*
 * Clear the specified key cache entry and any associated MIC entry.
 */
HAL_BOOL
ar5416ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (ar5212ResetKeyCacheEntry(ah, entry)) {
		ahp->ah_keytype[entry] = keyType[HAL_CIPHER_CLR];
		return AH_TRUE;
	} else
		return AH_FALSE;
}

/*
 * Sets the contents of the specified key cache entry
 * and any associated MIC entry.
 */
HAL_BOOL
ar5416SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
                       const HAL_KEYVAL *k, const uint8_t *mac,
                       int xorKey)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (ar5212SetKeyCacheEntry(ah, entry, k, mac, xorKey)) {
		ahp->ah_keytype[entry] = keyType[k->kv_type];
		return AH_TRUE;
	} else
		return AH_FALSE;
}
