/*
 * wpa_supplicant - Internal WAPI state machine definitions
 * Copyright (c) 2009 Nanoradio
 *
 */

#ifndef WAPI_I_H
#define WAPI_I_H

struct rsn_pmksa_candidate;

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

/**
 * struct wapi_ptk - WAPI Pairwise Transient Key
 * WAPI Spec - 8.1.4.10.2 Pairwise key derivation
 */
struct wapi_ptk {
	u8 uek[16]; /* Unicast Encryption Key (UEK) */
	u8 uck[16]; /* Unicast Integrity Check Key (UCK) */
	u8 mac[16]; /* Message Authentication Key (MAC) */
	u8 kek[16]; /* Key Encryption Key (KEK) */
	u8 challenge[32]; /* Challenge Seed */
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

typedef enum
	{
		AUTH_TYPE_NONE_WAPI = 0,        /*no WAPI       */
		AUTH_TYPE_WAPI,                 /*Certificate*/
		AUTH_TYPE_WAPI_PSK,             /*Pre-PSK*/
	}AUTH_TYPE;

typedef enum
	{
		KEY_TYPE_ASCII = 0,                     /*ascii         */
		KEY_TYPE_HEX,                           /*HEX*/
	}KEY_TYPE;

typedef struct
{
        AUTH_TYPE authType;             /*Authentication type*/
        union
        {
                struct
                {
                        KEY_TYPE kt;                            /*Key type*/
                        unsigned int  kl;                       /*key length*/
                        unsigned char kv[128];/*value*/
                };
                struct
                {
                        unsigned char as[1024]; /*ASU Certificate*/
                        unsigned char user[1024];/*User Certificate*/
                };
        }para;
}CNTAP_PARA;

typedef struct byte_data_{
	u8 length;
	u8 data[256];
} byte_data;

typedef struct _para_alg
{
        u8      para_flag;
        u16     para_len;
        u8      pad;
        u8      para_data[256];
}para_alg, *ppara_alg;

typedef struct _comm_data
{
        u16 length;
        u16 pad_value;
        u8 data[2048];
}comm_data, *pcomm_data,
	tkey, *ptkey,
	tsign;

struct cert_bin_t
{
        unsigned short length;
        unsigned short pad;
        unsigned char *data;
};

typedef struct _wai_fixdata_id
{
        u16     id_flag;
        u16     id_len;
        u8      id_data[1000];
} wai_fixdata_id;

/*signature algorithm*/
typedef struct _wai_fixdata_alg
{
        u16     alg_length;
        u8      sha256_flag;
        u8      sign_alg;
        para_alg        sign_para;
}wai_fixdata_alg;

typedef struct __cert_id
{
        u16 cert_flag;
        u16 length;
        u8 data[2048];
} cert_id;


/**
 * struct wapi_sm - Internal WAPI state machine data
 */
struct wapi_sm {
	u8 pmk[16];
	size_t pmk_len;
	u8 bkid[16];
	struct wapi_ptk ptk, tptk;
	int ptk_set, tptk_set;
	u8 snonce[WAPI_NONCE_LEN];
	u8 anonce[WAPI_NONCE_LEN]; /* ANonce from the last 1/4 msg */
	int renew_snonce;
	//u8 rx_replay_counter[WPA_REPLAY_COUNTER_LEN];
	//int rx_replay_counter_set;
	//u8 request_counter[WPA_REPLAY_COUNTER_LEN];

	struct eapol_sm *eapol; /* EAPOL state machine from upper level code */

	struct l2_packet_data *l2_preauth;
	struct l2_packet_data *l2_preauth_br;

	struct wapi_sm_ctx *ctx;

	int fast_reauth; /* whether EAP fast re-authentication is enabled */

	struct wpa_ssid *cur_ssid;

	u8 own_addr[ETH_ALEN];
	const char *ifname;
	const char *bridge_ifname;
	u8 bssid[ETH_ALEN];

	unsigned int dot11WAPIConfigSATimeout;
	unsigned int dot11WAPIConfigBKReauthThreshold;
	unsigned int dot11WAPIConfigBKLifetime;
	unsigned int dot11WAPIStatsWAIUnicastHandshakeFailures;

	/* Selected configuration (based on Beacon/ProbeResp WAPI IE) */
	unsigned int proto;
	unsigned int pairwise_cipher;
	unsigned int group_cipher;
	unsigned int key_mgmt;
	unsigned int mgmt_group_cipher;

	/* WAPI Cert */
	u8 ae_auth_flag[32];
	wai_fixdata_id ae_asu_id;
	wai_fixdata_id ae_id;
	wai_fixdata_alg sign_alg;
	cert_id ae_cert;
	para_alg ecdh;
	byte_data asue_eck; /* ASUE temp private key */
	byte_data  asue_key_data;
	byte_data  ae_key_data;
	u8 Nasue[32];
        u8 Nae[32];
	u16 txseq;
	u8 flag;
	
	u8 rekey_flag;

	u8 *assoc_wapi_ie; /* Own WAPI IE from (Re)AssocReq */
	size_t assoc_wapi_ie_len;
	u8 *ap_wapi_ie;
	size_t ap_wapi_ie_len;
};


static inline void wapi_sm_set_state(struct wapi_sm *sm, wpa_states state)
{
	sm->ctx->set_state(sm->ctx->ctx, state);
}

static inline wpa_states wapi_sm_get_state(struct wapi_sm *sm)
{
	return sm->ctx->get_state(sm->ctx->ctx);
}

static inline void wapi_sm_deauthenticate(struct wapi_sm *sm, int reason_code)
{
	sm->ctx->deauthenticate(sm->ctx->ctx, reason_code);
}

static inline void wapi_sm_disassociate(struct wapi_sm *sm, int reason_code)
{
	sm->ctx->disassociate(sm->ctx->ctx, reason_code);
}

static inline int wapi_sm_set_key(struct wapi_sm *sm, wpa_alg alg,
				 const u8 *addr, int key_idx, int set_tx,
				 const u8 *seq, size_t seq_len,
				 const u8 *key, size_t key_len)
{
	return sm->ctx->set_key(sm->ctx->ctx, alg, addr, key_idx, set_tx,
				seq, seq_len, key, key_len);
}

static inline struct wpa_ssid * wapi_sm_get_ssid(struct wapi_sm *sm)
{
	return sm->ctx->get_ssid(sm->ctx->ctx);
}

static inline int wapi_sm_get_bssid(struct wapi_sm *sm, u8 *bssid)
{
	return sm->ctx->get_bssid(sm->ctx->ctx, bssid);
}

static inline int wapi_sm_ether_send(struct wapi_sm *sm, const u8 *dest,
				    u16 proto, const u8 *buf, size_t len)
{
	return sm->ctx->ether_send(sm->ctx->ctx, dest, proto, buf, len);
}

static inline int wapi_sm_get_beacon_ie(struct wapi_sm *sm)
{
	return sm->ctx->get_beacon_ie(sm->ctx->ctx);
}

static inline void wapi_sm_cancel_auth_timeout(struct wapi_sm *sm)
{
	sm->ctx->cancel_auth_timeout(sm->ctx->ctx);
}

static inline u8 * wapi_sm_alloc_eapol(struct wapi_sm *sm, u8 subtype,
				      const void *data, u16 data_len,
				      size_t *msg_len, void **data_pos)
{
	return sm->ctx->alloc_eapol(sm->ctx->ctx, subtype, data, data_len,
				    msg_len, data_pos);
}

static inline int wapi_sm_add_pmkid(struct wapi_sm *sm, const u8 *bssid,
				   const u8 *pmkid)
{
	return sm->ctx->add_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wapi_sm_remove_pmkid(struct wapi_sm *sm, const u8 *bssid,
				      const u8 *pmkid)
{
	return sm->ctx->remove_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wapi_sm_mlme_setprotection(struct wapi_sm *sm, const u8 *addr,
					    int protect_type, int key_type)
{
	return sm->ctx->mlme_setprotection(sm->ctx->ctx, addr, protect_type,
					   key_type);
}
int wapi_fixdata_id_by_ident(void *cert_st, wai_fixdata_id *fixdata_id, u16 index);

#endif /* WAPI_I_H */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
