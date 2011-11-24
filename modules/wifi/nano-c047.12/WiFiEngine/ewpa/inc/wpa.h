/*
 * wpa_supplicant - WPA definitions
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
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

#ifndef WPA_H
#define WPA_H

#include "defs.h"
#include "wpa_common.h"

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#define WPA_CAPABILITY_PREAUTH BIT(0)
#define WPA_CAPABILITY_MGMT_FRAME_PROTECTION BIT(6)
#define WPA_CAPABILITY_PEERKEY_ENABLED BIT(9)

#define GENERIC_INFO_ELEM 0xdd
#define RSN_INFO_ELEM 0x30

#define PMKID_LEN 16


struct wpa_sm;
struct wpa_ssid;
struct eapol_sm;
struct wpa_config_blob;

struct wpa_sm_ctx {
	void *ctx; /* pointer to arbitrary upper level context */

	void (*set_state)(void *ctx, wpa_states state);
	wpa_states (*get_state)(void *ctx);
	void (*req_scan)(void *ctx, int sec, int usec);
	void (*deauthenticate)(void * ctx, int reason_code); 
	void (*disassociate)(void *ctx, int reason_code);
	int (*set_key)(void *ctx, wpa_alg alg,
		       const u8 *addr, int key_idx, int set_tx,
		       const u8 *seq, size_t seq_len,
		       const u8 *key, size_t key_len);
	void (*scan)(void *eloop_ctx, void *timeout_ctx);
	struct wpa_ssid * (*get_ssid)(void *ctx);
	int (*get_bssid)(void *ctx, u8 *bssid);
	int (*ether_send)(void *ctx, const u8 *dest, u16 proto, const u8 *buf,
			  size_t len);
	int (*get_beacon_ie)(void *ctx);
	void (*cancel_auth_timeout)(void *ctx);
	u8 * (*alloc_eapol)(void *ctx, u8 type, const void *data, u16 data_len,
			    size_t *msg_len, void **data_pos);
	int (*add_pmkid)(void *ctx, const u8 *bssid, const u8 *pmkid);
	int (*remove_pmkid)(void *ctx, const u8 *bssid, const u8 *pmkid);
	void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);
	const struct wpa_config_blob * (*get_config_blob)(void *ctx,
							  const char *name);
	int (*mlme_setprotection)(void *ctx, const u8 *addr,
				  int protection_type, int key_type);
};


enum wpa_sm_conf_params {
	RSNA_PMK_LIFETIME /* dot11RSNAConfigPMKLifetime */,
	RSNA_PMK_REAUTH_THRESHOLD /* dot11RSNAConfigPMKReauthThreshold */,
	RSNA_SA_TIMEOUT /* dot11RSNAConfigSATimeout */,
	WPA_PARAM_PROTO,
	WPA_PARAM_PAIRWISE,
	WPA_PARAM_GROUP,
	WPA_PARAM_KEY_MGMT,
	WPA_PARAM_MGMT_GROUP
};

struct wpa_ie_data {
	int proto;
	int pairwise_cipher;
	int group_cipher;
	int key_mgmt;
	int capabilities;
	int num_pmkid;
	const u8 *pmkid;
	int mgmt_group_cipher;
};

#ifndef CONFIG_NO_WPA

struct wpa_sm * wpa_sm_init(struct wpa_sm_ctx *ctx);
void wpa_sm_deinit(struct wpa_sm *sm);
void wpa_sm_notify_assoc(struct wpa_sm *sm, const u8 *bssid);
void wpa_sm_notify_disassoc(struct wpa_sm *sm);
void wpa_sm_set_pmk(struct wpa_sm *sm, const u8 *pmk, size_t pmk_len);
void wpa_sm_set_pmk_from_pmksa(struct wpa_sm *sm);
void wpa_sm_set_fast_reauth(struct wpa_sm *sm, int fast_reauth);
void wpa_sm_set_scard_ctx(struct wpa_sm *sm, void *scard_ctx);
void wpa_sm_set_config(struct wpa_sm *sm, struct wpa_ssid *config);
void wpa_sm_set_own_addr(struct wpa_sm *sm, const u8 *addr);
void wpa_sm_set_ifname(struct wpa_sm *sm, const char *ifname,
		       const char *bridge_ifname);
void wpa_sm_set_eapol(struct wpa_sm *sm, struct eapol_sm *eapol);
int wpa_sm_set_assoc_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_set_assoc_wpa_ie_default(struct wpa_sm *sm, u8 *wpa_ie,
				    size_t *wpa_ie_len);
int wpa_sm_set_ap_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_set_ap_rsn_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_get_mib(struct wpa_sm *sm, char *buf, size_t buflen);

int wpa_sm_set_param(struct wpa_sm *sm, enum wpa_sm_conf_params param,
		     unsigned int value);
unsigned int wpa_sm_get_param(struct wpa_sm *sm,
			      enum wpa_sm_conf_params param);

int wpa_sm_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
		      int verbose);

void wpa_sm_key_request(struct wpa_sm *sm, int error, int pairwise);

int wpa_sm_stkstart(struct wpa_sm *sm, const u8 *peer);

int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
		     struct wpa_ie_data *data);

void wpa_sm_aborted_cached(struct wpa_sm *sm);
int wpa_sm_rx_eapol(struct wpa_sm *sm, const u8 *src_addr,
		    const u8 *buf, size_t len);
int wpa_sm_parse_own_wpa_ie(struct wpa_sm *sm, struct wpa_ie_data *data);

#else /* CONFIG_NO_WPA */

static inline struct wpa_sm * wpa_sm_init(struct wpa_sm_ctx *ctx)
{
	return (struct wpa_sm *) 1;
}

static inline void wpa_sm_deinit(struct wpa_sm *sm)
{
}

static inline void wpa_sm_notify_assoc(struct wpa_sm *sm, const u8 *bssid)
{
}

static inline void wpa_sm_notify_disassoc(struct wpa_sm *sm)
{
}

static inline void wpa_sm_set_pmk(struct wpa_sm *sm, const u8 *pmk,
				  size_t pmk_len)
{
}

static inline void wpa_sm_set_pmk_from_pmksa(struct wpa_sm *sm)
{
}

static inline void wpa_sm_set_fast_reauth(struct wpa_sm *sm, int fast_reauth)
{
}

static inline void wpa_sm_set_scard_ctx(struct wpa_sm *sm, void *scard_ctx)
{
}

static inline void wpa_sm_set_config(struct wpa_sm *sm,
				     struct wpa_ssid *config)
{
}

static inline void wpa_sm_set_own_addr(struct wpa_sm *sm, const u8 *addr)
{
}

static inline void wpa_sm_set_ifname(struct wpa_sm *sm, const char *ifname,
				     const char *bridge_ifname)
{
}

static inline void wpa_sm_set_eapol(struct wpa_sm *sm, struct eapol_sm *eapol)
{
}

static inline int wpa_sm_set_assoc_wpa_ie(struct wpa_sm *sm, const u8 *ie,
					  size_t len)
{
	return -1;
}

static inline int wpa_sm_set_assoc_wpa_ie_default(struct wpa_sm *sm,
						  u8 *wpa_ie,
						  size_t *wpa_ie_len)
{
	return -1;
}

static inline int wpa_sm_set_ap_wpa_ie(struct wpa_sm *sm, const u8 *ie,
				       size_t len)
{
	return -1;
}

static inline int wpa_sm_set_ap_rsn_ie(struct wpa_sm *sm, const u8 *ie,
				       size_t len)
{
	return -1;
}

static inline int wpa_sm_get_mib(struct wpa_sm *sm, char *buf, size_t buflen)
{
	return 0;
}

static inline int wpa_sm_set_param(struct wpa_sm *sm,
				   enum wpa_sm_conf_params param,
				   unsigned int value)
{
	return -1;
}

static inline unsigned int wpa_sm_get_param(struct wpa_sm *sm,
					    enum wpa_sm_conf_params param)
{
	return 0;
}

static inline int wpa_sm_get_status(struct wpa_sm *sm, char *buf,
				    size_t buflen, int verbose)
{
	return 0;
}

static inline void wpa_sm_key_request(struct wpa_sm *sm, int error,
				      int pairwise)
{
}

static inline int wpa_sm_stkstart(struct wpa_sm *sm, const u8 *peer)
{
	return -1;
}

static inline int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
				   struct wpa_ie_data *data)
{
	return -1;
}

static inline void wpa_sm_aborted_cached(struct wpa_sm *sm)
{
}

static inline int wpa_sm_rx_eapol(struct wpa_sm *sm, const u8 *src_addr,
				  const u8 *buf, size_t len)
{
	return -1;
}

static inline int wpa_sm_parse_own_wpa_ie(struct wpa_sm *sm,
					  struct wpa_ie_data *data)
{
	return -1;
}

#endif /* CONFIG_NO_WPA */

#endif /* WPA_H */
