/*
 * WPA Supplicant - driver_wext exported functions
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
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

#ifndef DRIVER_WEXT_H
#define DRIVER_WEXT_H

struct wpa_driver_wext_data;

int wpa_driver_wext_get_ifflags(struct wpa_driver_wext_data *drv, int *flags);
int wpa_driver_wext_set_ifflags(struct wpa_driver_wext_data *drv, int flags);
int wpa_driver_wext_get_bssid(void *priv, u8 *bssid);
int wpa_driver_wext_set_bssid(void *priv, const u8 *bssid);
int wpa_driver_wext_get_ssid(void *priv, u8 *ssid);
int wpa_driver_wext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len);
int wpa_driver_wext_set_freq(void *priv, int freq);
int wpa_driver_wext_set_mode(void *priv, int mode);
int wpa_driver_wext_set_key(void *priv, wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len);
int wpa_driver_wext_scan(void *priv, const u8 *ssid, size_t ssid_len);
int wpa_driver_wext_get_scan_results(void *priv,
				     struct wpa_scan_result *results,
				     size_t max_size);

void wpa_driver_wext_scan_timeout(void *eloop_ctx, void *timeout_ctx);

int wpa_driver_wext_alternative_ifindex(struct wpa_driver_wext_data *drv,
					const char *ifname);

void * wpa_driver_wext_init(void *ctx, const char *ifname);
void wpa_driver_wext_deinit(void *priv);

int wpa_driver_wext_set_operstate(void *priv, int state);
int wpa_driver_wext_get_version(struct wpa_driver_wext_data *drv);

#endif /* DRIVER_WEXT_H */
