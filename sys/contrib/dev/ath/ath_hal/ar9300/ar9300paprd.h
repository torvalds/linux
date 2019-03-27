/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __AR9300PAPRD_H__
#define __AR9300PAPRD_H__

#include <ah.h>
#include "ar9300.h"
#include "ar9300phy.h"

#define AH_PAPRD_AM_PM_MASK              0x1ffffff
#define AH_PAPRD_IDEAL_AGC2_PWR_RANGE    0xe0

extern int ar9300_paprd_init_table(struct ath_hal *ah, struct ieee80211_channel *chan);
extern HAL_STATUS ar9300_paprd_setup_gain_table(struct ath_hal *ah, int chain_num);
extern HAL_STATUS ar9300_paprd_create_curve(struct ath_hal *ah, struct ieee80211_channel *chan, int chain_num);
extern int ar9300_paprd_is_done(struct ath_hal *ah);
extern void ar9300_enable_paprd(struct ath_hal *ah, HAL_BOOL enable_flag, struct ieee80211_channel * chan);
extern void ar9300_swizzle_paprd_entries(struct ath_hal *ah, unsigned int txchain);
extern void ar9300_populate_paprd_single_table(struct ath_hal *ah, struct ieee80211_channel *chan, int chain_num);
extern void ar9300_paprd_dec_tx_pwr(struct ath_hal *ah);
extern int ar9300_paprd_thermal_send(struct ath_hal *ah);

#endif
