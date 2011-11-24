/*
 * WPA Supplicant - Client mode MLME
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004, Instant802 Networks, Inc.
 * Copyright (c) 2005-2006, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef MLME_H
#define MLME_H

#ifdef CONFIG_CLIENT_MLME

int ieee80211_sta_init(struct wpa_supplicant *wpa_s);
void ieee80211_sta_deinit(struct wpa_supplicant *wpa_s);
int ieee80211_sta_req_scan(struct wpa_supplicant *wpa_s, const u8 *ssid,
			   size_t ssid_len);
int ieee80211_sta_deauthenticate(struct wpa_supplicant *wpa_s, u16 reason);
int ieee80211_sta_disassociate(struct wpa_supplicant *wpa_s, u16 reason);
int ieee80211_sta_associate(struct wpa_supplicant *wpa_s,
			    struct wpa_driver_associate_params *params);
int ieee80211_sta_get_ssid(struct wpa_supplicant *wpa_s, u8 *ssid,
			   size_t *len);
void ieee80211_sta_free_hw_features(struct wpa_hw_modes *hw_features,
				    size_t num_hw_features);
void ieee80211_sta_rx(struct wpa_supplicant *wpa_s, const u8 *buf, size_t len,
		      struct ieee80211_rx_status *rx_status);
int ieee80211_sta_get_scan_results(struct wpa_supplicant *wpa_s,
				   struct wpa_scan_result *results,
				   size_t max_size);

#else /* CONFIG_CLIENT_MLME */

static inline int ieee80211_sta_init(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline void ieee80211_sta_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline int ieee80211_sta_req_scan(struct wpa_supplicant *wpa_s,
					 const u8 *ssid, size_t ssid_len)
{
	return -1;
}

static inline int ieee80211_sta_deauthenticate(struct wpa_supplicant *wpa_s,
					       u16 reason)
{
	return -1;
}

static inline int ieee80211_sta_disassociate(struct wpa_supplicant *wpa_s,
					     u16 reason)
{
	return -1;
}

static inline int
ieee80211_sta_associate(struct wpa_supplicant *wpa_s,
			struct wpa_driver_associate_params *params)
{
	return -1;
}

static inline int ieee80211_sta_get_ssid(struct wpa_supplicant *wpa_s,
					 u8 *ssid, size_t *len)
{
	return -1;
}

static inline void
ieee80211_sta_free_hw_features(struct wpa_hw_modes *hw_features,
			       size_t num_hw_features)
{
}

static inline void
ieee80211_sta_rx(struct wpa_supplicant *wpa_s, const u8 *buf, size_t len,
		 struct ieee80211_rx_status *rx_status)
{
}

static inline int
ieee80211_sta_get_scan_results(struct wpa_supplicant *wpa_s,
			       struct wpa_scan_result *results,
			       size_t max_size)
{
	return -1;
}

#endif /* CONFIG_CLIENT_MLME */

#endif /* MLME_H */
