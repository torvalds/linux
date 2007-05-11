/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef WPA_H
#define WPA_H

#include <linux/skbuff.h>
#include <linux/types.h>
#include "ieee80211_i.h"

ieee80211_txrx_result
ieee80211_tx_h_michael_mic_add(struct ieee80211_txrx_data *tx);
ieee80211_txrx_result
ieee80211_rx_h_michael_mic_verify(struct ieee80211_txrx_data *rx);

ieee80211_txrx_result
ieee80211_tx_h_tkip_encrypt(struct ieee80211_txrx_data *tx);
ieee80211_txrx_result
ieee80211_rx_h_tkip_decrypt(struct ieee80211_txrx_data *rx);

ieee80211_txrx_result
ieee80211_tx_h_ccmp_encrypt(struct ieee80211_txrx_data *tx);
ieee80211_txrx_result
ieee80211_rx_h_ccmp_decrypt(struct ieee80211_txrx_data *rx);

#endif /* WPA_H */
