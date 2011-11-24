/*
 * wpa_supplicant - Internal WPA state machine definitions
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
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

#ifndef WPA_I_H
#define WPA_I_H

struct rsn_pmksa_candidate;

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

/**
 * struct wpa_ptk - WPA Pairwise Transient Key
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 */
struct wpa_ptk {
	u8 kck[16]; /* EAPOL-Key Key Confirmation Key (KCK) */
	u8 kek[16]; /* EAPOL-Key Key Encryption Key (KEK) */
	u8 tk1[16]; /* Temporal Key 1 (TK1) */
	union {
		u8 tk2[16]; /* Temporal Key 2 (TK2) */
		struct {
			u8 tx_mic_key[8];
			u8 rx_mic_key[8];
		} auth;
	} u;
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


#ifdef CONFIG_PEERKEY
#define PEERKEY_MAX_IE_LEN 80
struct wpa_peerkey {
	struct wpa_peerkey *next;
	int initiator; /* whether this end was initator for SMK handshake */
	u8 addr[ETH_ALEN]; /* other end MAC address */
	u8 inonce[WPA_NONCE_LEN]; /* Initiator Nonce */
	u8 pnonce[WPA_NONCE_LEN]; /* Peer Nonce */
	u8 rsnie_i[PEERKEY_MAX_IE_LEN]; /* Initiator RSN IE */
	size_t rsnie_i_len;
	u8 rsnie_p[PEERKEY_MAX_IE_LEN]; /* Peer RSN IE */
	size_t rsnie_p_len;
	u8 smk[PMK_LEN];
	int smk_complete;
	u8 smkid[PMKID_LEN];
	u32 lifetime;
	os_time_t expiration;
	int cipher; /* Selected cipher (WPA_CIPHER_*) */
	u8 replay_counter[WPA_REPLAY_COUNTER_LEN];
	int replay_counter_set;

	struct wpa_ptk stk, tstk;
	int stk_set, tstk_set;
};
#else /* CONFIG_PEERKEY */
struct wpa_peerkey;
#endif /* CONFIG_PEERKEY */


/**
 * struct wpa_sm - Internal WPA state machine data
 */
struct wpa_sm {
	u8 pmk[PMK_LEN];
	size_t pmk_len;
	struct wpa_ptk ptk, tptk;
	int ptk_set, tptk_set;
	u8 snonce[WPA_NONCE_LEN];
	u8 anonce[WPA_NONCE_LEN]; /* ANonce from the last 1/4 msg */
	int renew_snonce;
	u8 rx_replay_counter[WPA_REPLAY_COUNTER_LEN];
	int rx_replay_counter_set;
	u8 request_counter[WPA_REPLAY_COUNTER_LEN];

	struct eapol_sm *eapol; /* EAPOL state machine from upper level code */

	struct rsn_pmksa_cache *pmksa; /* PMKSA cache */
	struct rsn_pmksa_cache_entry *cur_pmksa; /* current PMKSA entry */
	struct rsn_pmksa_candidate *pmksa_candidates;

	struct l2_packet_data *l2_preauth;
	struct l2_packet_data *l2_preauth_br;
	u8 preauth_bssid[ETH_ALEN]; /* current RSN pre-auth peer or
				     * 00:00:00:00:00:00 if no pre-auth is
				     * in progress */
	struct eapol_sm *preauth_eapol;

	struct wpa_sm_ctx *ctx;

	void *scard_ctx; /* context for smartcard callbacks */
	int fast_reauth; /* whether EAP fast re-authentication is enabled */

	struct wpa_ssid *cur_ssid;

	u8 own_addr[ETH_ALEN];
	const char *ifname;
	const char *bridge_ifname;
	u8 bssid[ETH_ALEN];

	unsigned int dot11RSNAConfigPMKLifetime;
	unsigned int dot11RSNAConfigPMKReauthThreshold;
	unsigned int dot11RSNAConfigSATimeout;

	unsigned int dot11RSNA4WayHandshakeFailures;

	/* Selected configuration (based on Beacon/ProbeResp WPA IE) */
	unsigned int proto;
	unsigned int pairwise_cipher;
	unsigned int group_cipher;
	unsigned int key_mgmt;
	unsigned int mgmt_group_cipher;

	u8 *assoc_wpa_ie; /* Own WPA/RSN IE from (Re)AssocReq */
	size_t assoc_wpa_ie_len;
	u8 *ap_wpa_ie, *ap_rsn_ie;
	size_t ap_wpa_ie_len, ap_rsn_ie_len;

#ifdef CONFIG_PEERKEY
	struct wpa_peerkey *peerkey;
#endif /* CONFIG_PEERKEY */
};


static inline void wpa_sm_set_state(struct wpa_sm *sm, wpa_states state)
{
	sm->ctx->set_state(sm->ctx->ctx, state);
}

static inline wpa_states wpa_sm_get_state(struct wpa_sm *sm)
{
	return sm->ctx->get_state(sm->ctx->ctx);
}

static inline void wpa_sm_req_scan(struct wpa_sm *sm, int sec, int usec)
{
	sm->ctx->req_scan(sm->ctx->ctx, sec, usec);
}

static inline void wpa_sm_deauthenticate(struct wpa_sm *sm, int reason_code)
{
	sm->ctx->deauthenticate(sm->ctx->ctx, reason_code);
}

static inline void wpa_sm_disassociate(struct wpa_sm *sm, int reason_code)
{
	sm->ctx->disassociate(sm->ctx->ctx, reason_code);
}

static inline int wpa_sm_set_key(struct wpa_sm *sm, wpa_alg alg,
				 const u8 *addr, int key_idx, int set_tx,
				 const u8 *seq, size_t seq_len,
				 const u8 *key, size_t key_len)
{
	return sm->ctx->set_key(sm->ctx->ctx, alg, addr, key_idx, set_tx,
				seq, seq_len, key, key_len);
}

static inline struct wpa_ssid * wpa_sm_get_ssid(struct wpa_sm *sm)
{
	return sm->ctx->get_ssid(sm->ctx->ctx);
}

static inline int wpa_sm_get_bssid(struct wpa_sm *sm, u8 *bssid)
{
	return sm->ctx->get_bssid(sm->ctx->ctx, bssid);
}

static inline int wpa_sm_ether_send(struct wpa_sm *sm, const u8 *dest,
				    u16 proto, const u8 *buf, size_t len)
{
	return sm->ctx->ether_send(sm->ctx->ctx, dest, proto, buf, len);
}

static inline int wpa_sm_get_beacon_ie(struct wpa_sm *sm)
{
	return sm->ctx->get_beacon_ie(sm->ctx->ctx);
}

static inline void wpa_sm_cancel_auth_timeout(struct wpa_sm *sm)
{
	sm->ctx->cancel_auth_timeout(sm->ctx->ctx);
}

static inline u8 * wpa_sm_alloc_eapol(struct wpa_sm *sm, u8 type,
				      const void *data, u16 data_len,
				      size_t *msg_len, void **data_pos)
{
	return sm->ctx->alloc_eapol(sm->ctx->ctx, type, data, data_len,
				    msg_len, data_pos);
}

static inline int wpa_sm_add_pmkid(struct wpa_sm *sm, const u8 *bssid,
				   const u8 *pmkid)
{
	return sm->ctx->add_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wpa_sm_remove_pmkid(struct wpa_sm *sm, const u8 *bssid,
				      const u8 *pmkid)
{
	return sm->ctx->remove_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wpa_sm_mlme_setprotection(struct wpa_sm *sm, const u8 *addr,
					    int protect_type, int key_type)
{
	return sm->ctx->mlme_setprotection(sm->ctx->ctx, addr, protect_type,
					   key_type);
}

#endif /* WPA_I_H */
