/*
 *
 * WPA/WAPI Supplicant - WAPI state machine and WAPI-Key processing
 * Copyright (c) 2009 Nanoradio
 *
 */

#include "includes.h"
#include "common.h"
#include "sha1.h"
#include "sha256.h"
#include "sms4.h"
#include "eloop.h"
#include "config.h"
#include "l2_packet.h"
#include "eapol_sm.h"
#include "wapi.h"
#include "wapi_i.h"
#include "cert.h"
#include "ecc.h"
#include "hmac.h"

#if (DE_BUILTIN_WAPI == CFG_INCLUDED)

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct wapi_ie_hdr {
        u8 elem_id;
        u8 len;
        u8 version[2];
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

static const int WAPI_SELECTOR_LEN = 4;
static const u16 WAPI_VERSION_IE = 1;
static const u8 WAPI_CIPHER_SUITE_SMS4[] = { 0x00, 0x14, 0x72, 1 };
static const u8 WAPI_AUTH_KEY_MGMT_SUITE_CERT[] = {0x00, 0x14, 0x72, 1};
static const u8 WAPI_AUTH_KEY_MGMT_SUITE_PSK[] = {0x00, 0x14, 0x72, 2};

/* TODO: This should be moved to eloop structs*/
struct wapi_cert_data wapi_cert;

struct wapi_pairwise_key_req {
	u8 flag; /* bit 4 is used for rekeying */
	u8 bkid[16]; /* key (shared bk) */
	u8 uskid; /* bit 0: key index */
	u8 addid[12]; /* authentication MAC || supplicant MAC */
	u8 auth_challenge[32]; /* authenticator challenge */
} STRUCT_PACKED;

struct wapi_pairwise_key_reply {
	u8 flag; /* bit 4 is used for rekeying */
	u8 bkid[16]; /* key (shared bk) */
	u8 uskid; /* bit 0: key index */
	u8 addid[12]; /* authentication MAC || supplicant MAC */
	u8 supp_challenge[32]; /* supplicant challenge */
	u8 auth_challenge[32]; /* authenticator challenge */
	u8 supp_wapi_ie[24]; /* FIXME: add variable size */
	u8 key_mac[20]; /* Message Authentication Code*/
} STRUCT_PACKED;

struct wapi_pairwise_key_confirm {
	u8 flag; /* bit 4 is used for rekeying */
	u8 bkid[16]; /* key (shared bk) */
	u8 uskid; /* bit 0: key index */
	u8 addid[12]; /* authentication MAC || supplicant MAC */
	u8 supp_challenge[32]; /* supplicant challenge */
	u8 ap_ie[22]; /* authenticator IE */
} STRUCT_PACKED;

struct wapi_multicast_key_announce {
	u8 flag;
	u8 mskid;
	u8 uskid;
	u8 addid[12];
	u8 data_seq_num[16];
	u8 key_id[16];
	/* following key_data + mic */
} STRUCT_PACKED;

struct wapi_multicast_key_resp {
	u8 flag;
	u8 mskid;
	u8 uskid;
	u8 addid[12];
	u8 key_id[16];
	u8 key_mac[20];
} STRUCT_PACKED;

void *get_buffer(int len)
{
        char *buffer=NULL;
        buffer = (char *)os_malloc(len);
        if(buffer)
                os_memset(buffer, 0, len);
        else
                buffer = NULL;
        return buffer;
}

void *free_buffer(void *buffer, int len)
{
        char *tmpbuf = (char *)buffer;

        if(tmpbuf != NULL)
		{
			os_memset(tmpbuf, 0, len);
			os_free(tmpbuf);
			return NULL;
		}
        else
                return NULL;
}

static void * ex_memcpy(void *dbuf, const void *srcbuf, int len)
{
        os_memcpy(dbuf, srcbuf, len);
        return (char *)dbuf+len;
}

static u8 * wapi_build_hdr(u8 *pos, u16 txseq, u8 stype)
{
        struct wapi_hdr *hdr = (struct wapi_hdr *)pos;

        hdr->version = host_to_be16(WAPI_VERSION);
        hdr->type = WAPI_TYPE;
        hdr->subtype= stype;
        hdr->reserved = 0;
        hdr->length = 0x0000;
        hdr->seq_num = host_to_be16(txseq);
        hdr->frag_seq_num = 0;
        hdr->flag = 0;
        return (u8 *)(hdr+1);
}

static int cmp_var_struct(const void* remote,  const void* local, int len)
{
	u16 remote_val_type = 0;
	u16 remote_val_len = 0;
	const u8 *p = NULL;
	
	wai_fixdata_id * id = (wai_fixdata_id *)local;

	p = remote;

	/*  identifier */
	GETSHORT(p, remote_val_type); p += sizeof(u16);

	/* data length */
	GETSHORT(p, remote_val_len); p += sizeof(u16);

	if ((remote_val_type != id->id_flag)
	    || (remote_val_len != id->id_len)
	    || os_memcmp(p, id->id_data, len))
		{
			return -1;
		}
	
	return 0;
}

static void wpa_set_length(u8 *pos, u16 length)
{
        SETSHORT((pos+6), length);
}

static int wapi_selector_to_bitfield(const u8 *s)
{
        if (os_memcmp(s, WAPI_CIPHER_SUITE_SMS4, WAPI_SELECTOR_LEN) == 0)
                return WAPI_CIPHER_SMS4;
        return 0;
}

static int wapi_key_mgmt_to_bitfield(const u8 *s)
{
        if (os_memcmp(s, WAPI_AUTH_KEY_MGMT_SUITE_CERT, WAPI_SELECTOR_LEN) ==
            0)
                return WAPI_KEY_MGMT_CERT;
        if (os_memcmp(s, WAPI_AUTH_KEY_MGMT_SUITE_PSK, WAPI_SELECTOR_LEN)
            == 0)
                return WAPI_KEY_MGMT_PSK;
        return 0;
}


/**
 * wapi_cipher_txt - Convert cipher suite to a text string
 * @cipher: Cipher suite (WAPI_CIPHER_* enum)
 * Returns: Pointer to a text string of the cipher suite name
 */
static const char * wapi_cipher_txt(int cipher)
{
        switch (cipher) {
        case WAPI_CIPHER_SMS4:
                return "SMS4";
        default:
                return "UNKNOWN";
        }
}

/**
 * wapi_key_mgmt_txt - Convert key management suite to a text string
 * @key_mgmt: Key management suite (WAPI_KEY_MGMT_* enum)
 * @proto: WAPI protocol
 * Returns: Pointer to a text string of the key management suite name
 */
static const char * wapi_key_mgmt_txt(int key_mgmt, int proto)
{
        switch (key_mgmt) {
        case WAPI_KEY_MGMT_CERT:
                return "WAPI Certificate";
        case WAPI_KEY_MGMT_PSK:
                return "WAPI PSK";
	default:
                return "UNKNOWN";
        }
}

/**
 * wapi_sm_init - Initialize WAPI state machine
 * @ctx: Context pointer for callbacks; this needs to be an allocated buffer
 * Returns: Pointer to the allocated WAPI state machine data
 *
 * This function is used to allocate a new WAPI state machine and the returned
 * value is passed to all WAPI state machine calls.
 */
struct wapi_sm * wapi_sm_init(struct wapi_sm_ctx *ctx)
{
        struct wapi_sm *sm;

        sm = os_zalloc(sizeof(*sm));
        if (sm == NULL)
                return NULL;
        sm->renew_snonce = 1;
        sm->ctx = ctx;


        sm->dot11WAPIConfigBKLifetime = 43200;
        sm->dot11WAPIConfigBKReauthThreshold = 70;
        sm->dot11WAPIConfigSATimeout = 60;

#if 0
        sm->pmksa = pmksa_cache_init(wpa_sm_pmksa_free_cb, sm, sm);
        if (sm->pmksa == NULL) {
                wpa_printf(MSG_ERROR, "RSN: PMKSA cache initialization "
                           "failed");
                os_free(sm);
                return NULL;
        
	}
#endif
        return sm;
}

/**
 * wapi_sm_deinit - Deinitialize WAPI state machine
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 */
void wapi_sm_deinit(struct wapi_sm *sm)
{
        if (sm == NULL)
                return;

	/* FIXME: this should be only used for wapi certificate */
	/*X509_exit();*/

        //pmksa_cache_deinit(sm->pmksa);
        //eloop_cancel_timeout(wpa_sm_start_preauth, sm, NULL);
        os_free(sm->assoc_wapi_ie);
        os_free(sm->ap_wapi_ie);
	os_free(sm->ctx);
        os_free(sm);
}

/**
 * wapi_sm_set_own_addr - Set own MAC address
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @addr: Own MAC address
 */
void wapi_sm_set_own_addr(struct wapi_sm *sm, const u8 *addr)
{
        if (sm)
                os_memcpy(sm->own_addr, addr, ETH_ALEN);
}


/**
 * wapi_sm_set_ifname - Set network interface name
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @ifname: Interface name
 * @bridge_ifname: Optional bridge interface name (for pre-auth)
 */
void wapi_sm_set_ifname(struct wapi_sm *sm, const char *ifname,
                       const char *bridge_ifname)
{
        if (sm) {
                sm->ifname = ifname;
                sm->bridge_ifname = bridge_ifname;
        }
}

/**
 * wapi_sm_set_eapol - Set EAPOL (WAPI) state machine pointer
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @eapol: Pointer to EAPOL state machine allocated with eapol_sm_init()
 */
void wapi_sm_set_eapol(struct wapi_sm *sm, struct eapol_sm *eapol)
{
        if (sm)
                sm->eapol = eapol;
}

/**
 * wapi_sm_notify_assoc - Notify WAPI state machine about association
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @bssid: The BSSID of the new association
 *
 * This function is called to let WAPI state machine know that the connection
 * was established.
 */
void wapi_sm_notify_assoc(struct wapi_sm *sm, const u8 *bssid)
{
        if (sm == NULL)
                return;

        wpa_printf(MSG_DEBUG, "WAPI: Association event - clear replay counter");
        os_memcpy(sm->bssid, bssid, ETH_ALEN);
        /* FIXME */
	//os_memset(sm->rx_replay_counter, 0, WPA_REPLAY_COUNTER_LEN);
        //sm->rx_replay_counter_set = 0;
        //sm->renew_snonce = 1;
        //if (os_memcmp(sm->preauth_bssid, bssid, ETH_ALEN) == 0)
	//       rsn_preauth_deinit(sm);
}

/**
 * wapi_sm_notify_disassoc - Notify WAPI state machine about disassociation
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 *
 * This function is called to let WAPI state machine know that the connection
 * was lost. This will abort any existing pre-authentication session.
 */
void wapi_sm_notify_disassoc(struct wapi_sm *sm)
{
	if (wapi_sm_get_state(sm) == WPA_4WAY_HANDSHAKE)
                sm->dot11WAPIStatsWAIUnicastHandshakeFailures++;
}

/**
 * wapi_sm_get_status - Get WAPI state machine
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query WAPI state machine for status information. This function fills in
 * a text area with current status information. If the buffer (buf) is not
 * large enough, status information will be truncated to fit the buffer.
 */
int wapi_sm_get_status(struct wapi_sm *sm, char *buf, size_t buflen,
								int verbose)
{
	char *pos = buf, *end = buf + buflen;
	int ret;
	
	ret = os_snprintf(pos, end - pos,
                          "pairwise_cipher=%s\n"
                          "group_cipher=%s\n"
			  "key_mgmt=%s\n",
			  wapi_cipher_txt(sm->pairwise_cipher),
			  wapi_cipher_txt(sm->group_cipher),
			  wapi_key_mgmt_txt(sm->key_mgmt, sm->proto));
	if (ret < 0 || ret >= end - pos)
		return pos - buf;
	pos += ret;
	return pos - buf;
}

/**
 * wapi_sm_set_param - Set WAPI state machine parameters
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @param: Parameter field
 * @value: Parameter value
 * Returns: 0 on success, -1 on failure
 */
int wapi_sm_set_param(struct wapi_sm *sm, enum wapi_sm_conf_params param,
                     unsigned int value)
{
        int ret = 0;

        if (sm == NULL)
                return -1;

        switch (param) {
        case WAPI_PARAM_PROTO:
                sm->proto = value;
                break;
        case WAPI_PARAM_PAIRWISE:
                sm->pairwise_cipher = value;
                break;
        case WAPI_PARAM_GROUP:
                sm->group_cipher = value;
                break;
	case WAPI_PARAM_KEY_MGMT:
                sm->key_mgmt = value;
                break;
        default:
                break;
        }

        return ret;
}

/**
 * wapi_sm_set_config - Notification of current configuration change
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @config: Pointer to current network configuration
 *
 * Notify WAPI state machine that configuration has changed. config will be
 * stored as a backpointer to network configuration. This can be %NULL to clear
 * the stored pointed.
 */
void wapi_sm_set_config(struct wapi_sm *sm, struct wpa_ssid *config)
{
        if (sm) {
                sm->cur_ssid = config;
		/* FIXME */
                //pmksa_cache_notify_reconfig(sm->pmksa);
        }
}

/**
 * wapi_sm_set_pmk - Set PMK (BK)
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @pmk: The new PMK (BK)
 * @pmk_len: The length of the new PMK (BK) in bytes
 *
 * Configure the PMK (BK) for WAPI state machine.
 */
void wapi_sm_set_pmk(struct wapi_sm *sm, const u8 *pmk, size_t pmk_len)
{
        if (sm == NULL)
                return;

        sm->pmk_len = pmk_len;
        os_memcpy(sm->pmk, pmk, pmk_len);
}

/**
 * wapi_gen_ie - Generate WAPI IE based on current security policy
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @wapi_ie: Pointer to memory area for the generated WAPI IE
 * @wapi_ie_len: Maximum length of the generated WAPI IE
 * Returns: Length of the generated WAPI IE or -1 on failure
 */
static int wapi_gen_ie(struct wapi_sm *sm, u8 *wapi_ie, size_t wapi_ie_len)
{
	u8 *pos;
        struct wapi_ie_hdr *hdr;
        u16 capab;

        if (wapi_ie_len < sizeof(*hdr) + WAPI_SELECTOR_LEN +
            2 + WAPI_SELECTOR_LEN + 2 + WAPI_SELECTOR_LEN + 2) {
		/*+(sm->cur_pmksa ? 2 + PMKID_LEN : 0))*/
                return -1;
	}

        hdr = (struct wapi_ie_hdr *) wapi_ie;
        hdr->elem_id = WAPI_INFO_ELEM;
        WPA_PUT_LE16(hdr->version, WAPI_VERSION_IE);
        pos = (u8 *) (hdr + 1);


        *pos++ = 1;
        *pos++ = 0;
        if (sm->key_mgmt == WAPI_KEY_MGMT_CERT) {
                os_memcpy(pos, WAPI_AUTH_KEY_MGMT_SUITE_CERT,
                          WAPI_SELECTOR_LEN);
        } else if (sm->key_mgmt == WAPI_KEY_MGMT_PSK) {
                os_memcpy(pos, WAPI_AUTH_KEY_MGMT_SUITE_PSK,
                          WAPI_SELECTOR_LEN);
        } else {
                wpa_printf(MSG_WARNING, "Invalid key management type (%d).",
                           sm->key_mgmt);
                return -1;
        }
        pos += WAPI_SELECTOR_LEN;

        *pos++ = 1;
        *pos++ = 0;
        if (sm->pairwise_cipher == WAPI_CIPHER_SMS4) {
                os_memcpy(pos, WAPI_CIPHER_SUITE_SMS4, WAPI_SELECTOR_LEN);
        } else {
                wpa_printf(MSG_WARNING, "Invalid pairwise cipher (%d).",
                           sm->pairwise_cipher);
                return -1;
        }
        pos += WAPI_SELECTOR_LEN;

        if (sm->group_cipher == WAPI_CIPHER_SMS4) {
                os_memcpy(pos, WAPI_CIPHER_SUITE_SMS4, WAPI_SELECTOR_LEN);
        } else {
                wpa_printf(MSG_WARNING, "Invalid group cipher (%d).",
                           sm->group_cipher);
                return -1;
        }
        pos += WAPI_SELECTOR_LEN;

        /* WAPI Capabilities */
        capab = 0;
        WPA_PUT_LE16(pos, capab);
        pos += 2;

#if 0 /* FIXME add pmksa support*/
        if (sm->cur_pmksa) {
                /* PMKID Count (2 octets, little endian) */
                *pos++ = 1;
                *pos++ = 0;
                /* PMKID */
                os_memcpy(pos, sm->cur_pmksa->pmkid, PMKID_LEN);
                pos += PMKID_LEN;
        }
#endif

        hdr->len = (pos - wapi_ie) - 2;

        WPA_ASSERT((size_t) (pos - wapi_ie) <= wapi_ie_len);

        return pos - wapi_ie;
}


/**
 * wapi_sm_set_assoc_ie_default - Generate own WAPI IE from configuration
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @wapi_ie: Pointer to buffer for WAPI IE
 * @wapi_ie_len: Pointer to the length of the wapi_ie buffer
 * Returns: 0 on success, -1 on failure
 */
int wapi_sm_set_assoc_ie_default(struct wapi_sm *sm, u8 *wapi_ie,
                                    size_t *wapi_ie_len)
{
        int res;

        if (sm == NULL)
                return -1;

        res = wapi_gen_ie(sm, wapi_ie, *wapi_ie_len);
        if (res < 0)
                return -1;
        *wapi_ie_len = res;

        wpa_hexdump(MSG_DEBUG, "WAPI: Set own WAPI IE default",
                    wapi_ie, *wapi_ie_len);

        if (sm->assoc_wapi_ie == NULL) {
                /*
                 * Make a copy of the WAPI IE so that 4-Way Handshake gets
                 * the correct version of the IE even if PMKSA caching is
                 * aborted (which would remove PMKID from IE generation).
                 */
                sm->assoc_wapi_ie = os_malloc(*wapi_ie_len);
                if (sm->assoc_wapi_ie == NULL)
                        return -1;

                os_memcpy(sm->assoc_wapi_ie, wapi_ie, *wapi_ie_len);
                sm->assoc_wapi_ie_len = *wapi_ie_len;
        }

        return 0;
}

/**
 * wapi_sm_set_assoc_ie - Set own WAPI IE from (Re)AssocReq
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @ie: Pointer to IE data (starting from id)
 * @len: IE length
 * Returns: 0 on success, -1 on failure
 *
 * Inform WAPI state machine about the WAPI IE used in (Re)Association
 * Request frame. The IE will be used to override the default value generated
 * with wapi_sm_set_assoc_ie_default().
 */
int wapi_sm_set_assoc_ie(struct wapi_sm *sm, const u8 *ie, size_t len)
{
	if (sm == NULL)
		return -1;
	
	os_free(sm->assoc_wapi_ie);
	if (ie == NULL || len == 0) {
		wpa_printf(MSG_DEBUG, "WAPI: clearing own WAPI IE");
		sm->assoc_wapi_ie = NULL;
		sm->assoc_wapi_ie_len = 0;
	} else {
		wpa_hexdump(MSG_DEBUG, "WAPI: set own WAPI IE", ie, len);
		sm->assoc_wapi_ie = os_malloc(len);
		if (sm->assoc_wapi_ie == NULL)
			return -1;
		
		os_memcpy(sm->assoc_wapi_ie, ie, len);
		sm->assoc_wapi_ie_len = len;
	}
	
	return 0;
}

/**
 * wapi_sm_set_ap_ie - Set AP WAPI IE from Beacon/ProbeResp
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @ie: Pointer to IE data (starting from id)
 * @len: IE length
 * Returns: 0 on success, -1 on failure
 *
 * Inform WAPI state machine about the WAPI IE used in Beacon / Probe Response
 * frame.
 */
int wapi_sm_set_ap_ie(struct wapi_sm *sm, const u8 *ie, size_t len)
{
        if (sm == NULL)
                return -1;

        os_free(sm->ap_wapi_ie);
        if (ie == NULL || len == 0) {
                wpa_printf(MSG_DEBUG, "WAPI: clearing AP WAPI IE");
                sm->ap_wapi_ie = NULL;
                sm->ap_wapi_ie_len = 0;
        } else {
                wpa_hexdump(MSG_DEBUG, "WAPI: set AP WAPI IE", ie, len);
                sm->ap_wapi_ie = os_malloc(len);
                if (sm->ap_wapi_ie == NULL)
                        return -1;

                os_memcpy(sm->ap_wapi_ie, ie, len);
                sm->ap_wapi_ie_len = len;
        }

        return 0;
}


/**
 * wapi_parse_ie - Parse WAPI IE
 * @wapi_ie: Pointer to WAPI IE
 * @wapi_ie_len: Length of the WAPI IE
 * @data: Pointer to data area for parsing results
 * Returns: 0 on success, -1 on failure
 *
 * Parse the contents of WAPI IE and write the parsed data into data.
 */
int wapi_parse_ie(const u8 *wapi_ie, size_t wapi_ie_len,
		  struct wapi_ie_data *data)
{
        const struct wapi_ie_hdr *hdr;
        const u8 *pos;
        int left;
        int i, count;

        data->proto = WAPI_PROTO;
        data->pairwise_cipher = WAPI_CIPHER_SMS4;
        data->group_cipher = WAPI_CIPHER_SMS4;
        data->key_mgmt = WAPI_KEY_MGMT_CERT;
        data->capabilities = 0;
        data->pmkid = NULL;
        data->num_pmkid = 0;
        data->mgmt_group_cipher = 0;
	
	if (wapi_ie_len == 0) {
                /* No WAPI IE - fail silently */
                return -1;
        }

	printf("wapi_ie_len:%d\n", wapi_ie_len);
        if (wapi_ie_len < sizeof(struct wapi_ie_hdr)) {
                wpa_printf(MSG_DEBUG, "%s: ie len too short %lu",
                           __func__, (unsigned long) wapi_ie_len);
                return -1;
        }

        hdr = (const struct wapi_ie_hdr *) wapi_ie;

        if (hdr->elem_id != WAPI_INFO_ELEM ||
            hdr->len != wapi_ie_len - 2 ||
            WPA_GET_LE16(hdr->version) != WAPI_VERSION_IE) {
                wpa_printf(MSG_DEBUG, "%s: malformed ie or unknown version",
                           __func__);
                return -1;
        }

        pos = (const u8 *) (hdr + 1);
        left = wapi_ie_len - sizeof(*hdr);

        if (left >= 2) {
                data->key_mgmt = 0;
                count = WPA_GET_LE16(pos);
                pos += 2;
                left -= 2;
                if (count == 0 || left < count * WAPI_SELECTOR_LEN) {
                        wpa_printf(MSG_DEBUG, "%s: ie count botch (key mgmt), "
                                   "count %u left %u", __func__, count, left);
                        return -1;
                }
                for (i = 0; i < count; i++) {
                        data->key_mgmt |= wapi_key_mgmt_to_bitfield(pos);
                        pos += WAPI_SELECTOR_LEN;
                        left -= WAPI_SELECTOR_LEN;
                }
        } else if (left == 1) {
                wpa_printf(MSG_DEBUG, "%s: ie too short (for pairwise)",
                           __func__);
                return -1;
        }

        if (left >= 2) {
                data->pairwise_cipher = 0;
                count = WPA_GET_LE16(pos);
                pos += 2;
                left -= 2;
                if (count == 0 || left < count * WAPI_SELECTOR_LEN) {
                        wpa_printf(MSG_DEBUG, "%s: ie count botch (pairwise), "
                                   "count %u left %u", __func__, count, left);
                        return -1;
                }
                for (i = 0; i < count; i++) {
                        data->pairwise_cipher |= wapi_selector_to_bitfield(pos);
                        pos += WAPI_SELECTOR_LEN;
                        left -= WAPI_SELECTOR_LEN;
                }
	} else if (left == 1) {
                wpa_printf(MSG_DEBUG, "%s: ie too short (for group cipher)",
                           __func__);
                return -1;
        }

        if (left >= WAPI_SELECTOR_LEN) {
                data->group_cipher = wapi_selector_to_bitfield(pos);
		pos += WAPI_SELECTOR_LEN;
                left -= WAPI_SELECTOR_LEN;
        } else if (left > 0) {
                wpa_printf(MSG_DEBUG, "%s: ie length mismatch, %u too much",
                           __func__, left);
                return -1;
        }

        if (left >= 2) {
                data->capabilities = WPA_GET_LE16(pos);
                pos += 2;
                left -= 2;
        }

	/* FIXME -> add wapi BKID Cound and BKID list */
#if 0
	if (left >= 2) {
                data->num_pmkid = WPA_GET_LE16(pos);
                pos += 2;
                left -= 2;
                if (left < data->num_pmkid * PMKID_LEN) {
                        wpa_printf(MSG_DEBUG, "%s: PMKID underflow "
                                   "(num_pmkid=%d left=%d)",
                                   __func__, data->num_pmkid, left);
                        data->num_pmkid = 0;
                } else {
                        data->pmkid = pos;
                        pos += data->num_pmkid * PMKID_LEN;
                        left -= data->num_pmkid * PMKID_LEN;
                }
        }
#endif
	if (left > 0) {
                wpa_printf(MSG_DEBUG, "%s: ie has %u trailing bytes - ignored",
                           __func__, left);
        }

        return 0;
}

/**
 * wapi_sm_parse_own_ie - Parse own WAPI IE
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @data: Pointer to data area for parsing results
 * Returns: 0 on success, -1 if IE is not known, or -2 on parsing failure
 *
 * Parse the contents of the own WAPI IE from (Re)AssocReq and write the
 * parsed data into data.
 */
int wapi_sm_parse_own_ie(struct wapi_sm *sm, struct wapi_ie_data *data)
{
        if (sm == NULL || sm->assoc_wapi_ie == NULL) {
                wpa_printf(MSG_DEBUG, "WAPI: No WAPI IE available from "
                           "association info");
                return -1;
        }
        if (wapi_parse_ie(sm->assoc_wapi_ie, sm->assoc_wapi_ie_len, data))
                return -2;
        return 0;
}

static int wapi_install_ptk(struct wapi_sm *sm,
			    const struct wapi_pairwise_key_req *key)
{
        int keylen, rsclen;
        wpa_alg alg;
        u8 null_rsc[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        const u8 *key_rsc = null_rsc;
	u8 usk[32]; /* unicast key */

        wpa_printf(MSG_DEBUG, "WAPI: Installing PTK to the driver.");

        switch (sm->pairwise_cipher) {
        case WAPI_CIPHER_SMS4:
                alg = WAPI_ALG_SMS4;
                keylen = 32;
                rsclen = 6;
                break;
	default:
                wpa_printf(MSG_WARNING, "WAPI: Unsupported pairwise cipher %d",
                           sm->pairwise_cipher);
                return -1;
        }

        if (sm->proto == WAPI_PROTO) {
                null_rsc[0] = 0x5c;
		null_rsc[1] = 0x36;
		key_rsc = null_rsc;		
        } /*else {
                key_rsc = key->key_rsc;
                wpa_hexdump(MSG_DEBUG, "WPA: RSC", key_rsc, rsclen);
		}*/

	/* Copy the encryption key and the integrity check key to the 32 byte unicast key that will be installed in driver */
	os_memcpy(usk, sm->ptk.uek, 16);
	os_memcpy(usk+16, sm->ptk.uck, 16);

        if (wapi_sm_set_key(sm, alg, sm->bssid, key->uskid, 1, key_rsc, rsclen,
                           (u8 *) usk, keylen) < 0) {
                wpa_printf(MSG_WARNING, "WAPI: Failed to set PTK to the "
                           "driver.");
                return -1;
        }
        return 0;
}

/**
 * wapi_key_mic - Calculate WAPI-Key MIC
 * @key: WAPI-Key Key Confirmation Key (KCK)
 * @buf: Pointer to the beginning of the WAPI header (version field)
 * @len: Length of the WAPI frame (from WAPI header to the end of the frame)
 * @mic: Pointer to the buffer to which the WAPI-Key MIC is written
 *
 * Calculate WAPI-Key MIC for an WAPI-Key packet. The WAPI-Key MIC field has
 * to be cleared (all zeroes) when calling this function.
 *
 */
static void wapi_key_mic(const u8 *key,
			 const u8 *buf, size_t len, u8 *mic)
{
	u8 hash[SHA256_MAC_LEN];
		
	hmac_sha256(key, 16, buf+12, len-12-20, hash);
	//hmac_sha256(key, 16, buf, len, hash);
	os_memcpy(mic, hash, 20);
	wpa_hexdump(MSG_DEBUG, "@@@ MIC: ", mic, 20);
}

static void wapi_key_send(struct wapi_sm *sm, u8 *kck,
			  const u8 *dest, u16 proto,
			  u8 *msg, size_t msg_len, u8 *key_mic)
{
        if (os_memcmp(dest, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0 &&
            os_memcmp(sm->bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0) {
                /*
                 * Association event was not yet received; try to fetch
                 * BSSID from the driver.
                 */
                if (wapi_sm_get_bssid(sm, sm->bssid) < 0) {
                        wpa_printf(MSG_DEBUG, "WAPI: Failed to read BSSID for "
                                   "WAPI-Key destination address");
                } else {
                        dest = sm->bssid;
                        wpa_printf(MSG_DEBUG, "WAPI: Use BSSID (" MACSTR
                                   ") as the destination for WAPI-Key",
                                   MAC2STR(dest));
                }
        }

	if (key_mic) {
                wapi_key_mic(kck, msg, msg_len, key_mic);
        }

        wpa_hexdump(MSG_MSGDUMP, "WAPI: TX EAPOL-Key", msg, msg_len);
        wapi_sm_ether_send(sm, dest, proto, msg, msg_len);
        eapol_sm_notify_tx_eapol_key(sm->eapol);
        os_free(msg);
}

static int wapi_send_2_of_3(struct wapi_sm *sm,
                                      const struct wapi_pairwise_key_req *key,
                                      const u8 *nonce,
                                      const u8 *wapi_ie, size_t wapi_ie_len,
                                      struct wapi_ptk *ptk)
{
	size_t rlen;
        struct wapi_pairwise_key_reply *reply;
        u8 *rbuf;
	u8 dst[6];

	if (wapi_ie == NULL) {
                wpa_printf(MSG_WARNING, "WAPI: No wapi_ie set - cannot "
                           "generate msg 2/3");
                return -1;
        }

        wpa_hexdump(MSG_DEBUG, "WAPI: WAPI IE for msg 2/3", wapi_ie, wapi_ie_len);

	rbuf = wapi_sm_alloc_eapol(sm, WAPI_SUBTYPE_UNICAST_KEY_RESPONSE,
				   NULL, sizeof(*reply) /*+ wapi_ie_len*/,
                                  &rlen, (void *) &reply);
        if (rbuf == NULL)
                return -1;

	reply->flag = 0;
	os_memcpy(reply->bkid, key->bkid, 16);
	reply->uskid = key->uskid;
	os_memcpy(reply->addid, key->addid, 12);
	os_memcpy(reply->supp_challenge, sm->snonce, 32);
	os_memcpy(reply->auth_challenge, key->auth_challenge, 32);
	os_memcpy(reply->supp_wapi_ie, sm->assoc_wapi_ie, 24);
	
	os_memcpy(dst, key->addid, 6);

	wpa_printf(MSG_DEBUG, "WAPI: Sending WAPI-Key 2/3");
        wapi_key_send(sm, ptk->mac, dst, ETH_P_WAPI,
		      rbuf, rlen, reply->key_mac);

	return 0;
}

static int wapi_send_2_of_2(struct wapi_sm *sm,
			    const struct wapi_multicast_key_announce *key)
{
        size_t rlen;
        struct wapi_multicast_key_resp *reply;
        u8 *rbuf;
        u8 dst[6];

        rbuf = wapi_sm_alloc_eapol(sm, WAPI_SYBTYPE_MULTICAST_KEY_RESPONSE,
                                   NULL, sizeof(*reply),
				   &rlen, (void *) &reply);
        if (rbuf == NULL)
                return -1;

	reply->flag = 0;
	reply->mskid = key->mskid;
	reply->uskid = key->uskid;
	os_memcpy(reply->addid, key->addid, 12);
	os_memcpy(reply->key_id, key->key_id, 16);

	os_memcpy(dst, key->addid, 6);

	wpa_printf(MSG_DEBUG, "WAPI: Sending WAPI-Key 2/2");
        wapi_key_send(sm, sm->ptk.mac, dst, ETH_P_WAPI,
		      rbuf, rlen, reply->key_mac);

	return 0;
}

/**
 * wapi_pmk_to_ptk - Calculate PTK from PMK, addresses, and nonces
 * @pmk: Pairwise master key
 * @pmk_len: Length of PMK
 * @label: Label to use in derivation
 * @addid: Authenticator and supplicant mac address. 
 * @nonce1: ANonce or SNonce
 * @nonce2: SNonce or ANonce
 * @ptk: Buffer for pairwise transient key
 * @ptk_len: Length of PTK
 *
 * KD_HMAC_SHA256(BK, ADDID || N1 || N2 || 
 * "pairwise key expansion for unicast and additional keys and nonce")
 *
 * N1: authenticator challenge
 * N2: supplicant challenge
 *
 */

static void wapi_pmk_to_ptk(const u8 *pmk, size_t pmk_len,
                           const char *label,
                           const u8 *addid,
                           const u8 *nonce1, const u8 *nonce2,
                           u8 *ptk, size_t ptk_len)
{
        u8 data[12+ 32+ 32+ 64]; /* addid + N1 + N2 + label */

#if 0
	u8 temp_addid[12] = {0x0e, 0x0b, 0xc0, 0x02, 0x2f, 0x55,  0x00, 0x21, 0x19, 0xc2, 0x46, 0xe6};

	/* Auth Challenge */
	u8 temp_nonce1[32] = {0x31 ,0xd6 ,0x34 ,0x9b ,0xc8 ,0xca ,0xcc ,0xaf ,0xae ,0x6e ,0x9d ,0x51 ,0x23 ,0x99 ,0x73 ,0xdc ,0x8a,
			      0x9c ,0x60 ,0xbd ,0x34 ,0xcb ,0xf7 ,0xc4 ,0xb7 ,0x33 ,0x3f ,0xb2 ,0xe8 ,0x31 ,0x5e ,0x08};

	/* Station Challenge */
	u8 temp_nonce2[32] = {0x0a ,0xb7 ,0x83 ,0x2b ,0xfe ,0x49 ,0xf9 ,0x87 ,0xbf ,0x9d ,0xa1 ,0xcb ,0xe3 ,0x99 ,0x74 ,0x6f ,0x33,
			      0xb6 ,0x39 ,0x69 ,0x99 ,0x99 ,0x12 ,0x57 ,0x8c ,0xb2 ,0x9b ,0x0c ,0x6a ,0x40 ,0x3a ,0xfe };
#endif 

#if 1   
	os_memcpy(data, addid, 12);
	os_memcpy(data+12, nonce1, 32);
	os_memcpy(data+12+32, nonce2, 32);
	os_memcpy(data+12+32+32, label, 64);
#else
	os_memcpy(data, temp_addid, 12);
	os_memcpy(data+12, temp_nonce1, 32);
	os_memcpy(data+12+32, temp_nonce2, 32);
	os_memcpy(data+12+32+32, label, 64);
#endif
	ptk_len = 96;

	kd_hmac_sha256(pmk, pmk_len, data, sizeof(data), ptk, ptk_len);

        wpa_hexdump(MSG_DEBUG, "WAPI: PMK (BK)", pmk, pmk_len);
        wpa_hexdump(MSG_DEBUG, "WAPI: PTK", ptk, ptk_len);
}

int wapi_fixdata_id_by_ident(void *cert_st, wai_fixdata_id *fixdata_id, u16 index)
{
	u8 *temp ;
	byte_data *subject_name = NULL;
	byte_data *issure_name = NULL;
	byte_data *serial_no = NULL;
	const struct cert_obj_st_t *cert_obj = NULL;

	if(fixdata_id == NULL || cert_st == NULL) 
		return -1;

	temp= fixdata_id->id_data;
	fixdata_id->id_flag = index;

	cert_obj = get_cert_obj(index);

	if((cert_obj == NULL)
	   ||(cert_obj->get_public_key == NULL)
	   ||(cert_obj->get_subject_name == NULL)
	   ||(cert_obj->get_issuer_name == NULL)
	   ||(cert_obj->get_serial_number == NULL)
	   ||(cert_obj->verify_key == NULL)
	   ||(cert_obj->sign == NULL)
	   ||(cert_obj->verify == NULL))
		{
			return -4;
		}
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);
	subject_name = (*cert_obj->get_subject_name)(cert_st);
	issure_name = (*cert_obj->get_issuer_name)(cert_st);
	serial_no = (*cert_obj->get_serial_number)(cert_st);
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);

	if((subject_name == NULL) || (issure_name == NULL) || (serial_no == NULL))
	{
		return -2;
	}

	wpa_hexdump(MSG_DEBUG, "AE ID subject: ", subject_name->data, subject_name->length);
	wpa_hexdump(MSG_DEBUG, "AE ID: issure", issure_name->data, issure_name->length);
	wpa_hexdump(MSG_DEBUG, "AE ID: Number", serial_no->data, serial_no->length);

	memcpy(temp, subject_name->data, subject_name->length);
	temp += subject_name->length;

	memcpy(temp, issure_name->data, issure_name->length);
	temp += issure_name->length;

	memcpy(temp, serial_no->data, serial_no->length);
	temp +=serial_no->length;

	fixdata_id->id_len = temp - fixdata_id->id_data;
	free_buffer(subject_name,sizeof(byte_data));
	free_buffer(issure_name,sizeof(byte_data));
	free_buffer(serial_no,sizeof(byte_data));

	return 0;
}

static int asue_initialize_alg(struct wapi_sm *sm)
{
	char alg_para_oid_der[16] = {0x06, 0x09,0x2a,(char)0x81,0x1c, (char)0xd7,0x63,0x01,0x01,0x02,0x01};

	os_memset((u8 *)&(sm->sign_alg), 0, sizeof(wai_fixdata_alg));
	sm->sign_alg.alg_length = 16;
	sm->sign_alg.sha256_flag = 1;
	sm->sign_alg.sign_alg = 1;
	sm->sign_alg.sign_para.para_flag = 1;
	sm->sign_alg.sign_para.para_len = 11;
	memcpy(sm->sign_alg.sign_para.para_data, alg_para_oid_der, 11);
	return 0;
}

static int asue_x_x_p_derivation(struct wapi_sm *sm)

{
	if (sm == NULL)
		return -1;

	/* clear buffer */
	sm->asue_eck.length = 0;
	os_memset(sm->asue_eck.data, 0, sizeof(sm->asue_eck.data));
	sm->asue_key_data.length = 0;
	os_memset(sm->asue_key_data.data, 0, sizeof(sm->asue_key_data.data));
	/* get the public key and the private key for ECC */
	if (ecc192_genkey(sm->asue_eck.data, sm->asue_key_data.data) != 0)
	{
		wpa_printf(MSG_ERROR, "ecc192 key generation FAILED!");
		return -1;
	}
	sm->asue_eck.length = SECKEY_LEN;
	sm->asue_key_data.length = PUBKEY2_LEN;

	return 0;
}


static int wapi_cert_2_3_send(struct wapi_sm* sm, u8* payload, int len)
{
	static comm_data data_buff;
	static tsign sign;
	u8 *sign_len_pos = NULL;
	static u8 tbuf[2048];
	u8 *pos = NULL;

	const struct cert_obj_st_t *cert_obj = NULL;

	payload = payload; /*what a f** is this?  disable warnning */
	len = len; /* disable warnning */

	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);	
	pos = tbuf;
	os_memset(pos, 0, sizeof(tbuf));
	os_memset(&data_buff, 0, sizeof(data_buff));
	os_memset(&sign, 0, sizeof(sign));

	pos = wapi_build_hdr(pos, 1/*++sm->txseq*/, 0x04);// WAI_ACCESS_AUTH_REQUEST);
	sm->flag = BIT(2) |sm->flag;
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);
	*pos = sm->flag;
	pos = pos + 1;
	pos= ex_memcpy(pos, &(sm->ae_auth_flag), 32);
	pos= ex_memcpy(pos, &(sm->Nasue), 32);
	pos= ex_memcpy(pos, &(sm->asue_key_data.length), 1);
	pos= ex_memcpy(pos, &(sm->asue_key_data.data), sm->asue_key_data.length);
	SETSHORT(pos,sm->ae_id.id_flag); pos += 2;
	SETSHORT(pos,sm->ae_id.id_len); pos += 2;
	pos = ex_memcpy(pos, sm->ae_id.id_data, sm->ae_id.id_len);	

	SETSHORT(pos,wapi_cert.cert_info.config.used_cert); pos += 2;
	SETSHORT(pos,wapi_cert.cert_info.asue_cert_obj->cert_bin->length); pos += 2;	

	pos= ex_memcpy(pos, wapi_cert.cert_info.asue_cert_obj->cert_bin->data, wapi_cert.cert_info.asue_cert_obj->cert_bin->length);
	//wpa_hexdump(MSG_DEBUG, "cert_bin->data",wapi_cert.cert_info.asue_cert_obj->cert_bin->data, wapi_cert.cert_info.asue_cert_obj->cert_bin->length);

	*pos = sm->ecdh.para_flag;   pos++;
	//pos= os_memcpy(pos, &(wpa_s->ecdh.para_flag), 1);/*ecdh*/
	SETSHORT(pos,sm->ecdh.para_len); pos += 2;
	pos= ex_memcpy(pos, &(sm->ecdh.para_data), sm->ecdh.para_len);/*ecdh*/

	data_buff.length = pos - tbuf - sizeof(struct wapi_hdr);
	wpa_printf(MSG_DEBUG, "data_buff.length: %d: ", data_buff.length);
	os_memcpy(data_buff.data, tbuf + sizeof(struct wapi_hdr), data_buff.length);/*????*/
	cert_obj = get_cert_obj(wapi_cert.cert_info.config.used_cert);

	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,used_cert=%d", __func__, __LINE__,wapi_cert.cert_info.config.used_cert);
	wpa_hexdump(MSG_DEBUG, "private_key ", cert_obj->private_key->data, cert_obj->private_key->length);

	if(!(*cert_obj->sign)(
			      cert_obj->private_key->data,
			      cert_obj->private_key->length,
			      data_buff.data, data_buff.length, sign.data))
		{

			wpa_printf(MSG_ERROR,"fail to sign data and will exit !!\n");
			return -1;
		}
	*pos ++= 1;
	sign_len_pos = pos;
	pos += 2; /* length */
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);

	SETSHORT(pos,wapi_cert.asue_id.id_flag); pos += 2;
	SETSHORT(pos,wapi_cert.asue_id.id_len); pos += 2;
	pos= ex_memcpy(pos, &(wapi_cert.asue_id.id_data), wapi_cert.asue_id.id_len);

	SETSHORT(pos,sm->sign_alg.alg_length); pos += 2;
	*pos = sm->sign_alg.sha256_flag; pos++;
	*pos = sm->sign_alg.sign_alg; pos++;
	*pos = sm->sign_alg.sign_para.para_flag; pos++;
	SETSHORT(pos,sm->sign_alg.sign_para.para_len); pos += 2;
	pos= ex_memcpy(pos, &(sm->sign_alg.sign_para.para_data), sm->sign_alg.sign_para.para_len);
	SETSHORT(pos,SIGN_LEN); pos += 2;
	pos = ex_memcpy(pos, &(sign.data), SIGN_LEN);

	SETSHORT(sign_len_pos,(pos-sign_len_pos-2));

	wpa_set_length(tbuf, (short)(pos-tbuf));

	wapi_sm_ether_send(sm, sm->bssid, ETH_P_WAPI, tbuf, pos-tbuf);

	return 0;
}

static void GetRandom_Value(u8 *buffer, int len)
{
    u8 temp[48];
    u8 smash_key[32] =
    {
        0x09, 0x1A, 0x09, 0x1A, 0xFF, 0x90, 0x67, 0x90,
        0x7F, 0x48, 0x1B, 0xAF, 0x89, 0x72, 0x52, 0x8B,
        0x35, 0x43, 0x10, 0x13, 0x75, 0x67, 0x95, 0x4E,
        0x77, 0x40, 0xC5, 0x28, 0x63, 0x62, 0x8F, 0x75
    };
    kd_hmac_sha256(smash_key, 32, buffer, len, temp, len);
    os_memcpy(buffer,temp,len);
}

static int wapi_process_cert_1_of_3(struct wapi_sm* sm, const unsigned char *src_addr, u8* payload, size_t len)
{
	u8 flag = 0;
	u8 *ae_auth_flag = NULL;
	u8 *asu_id = NULL;
	u8 *ae_cer = NULL;
	u8 *ecdh = NULL;
	int ret = -1;
	u8 auth_act_len = WAPI_FLAG_LEN 
		+32/*random number*/
		+2/*ASU ID Identifier*/
		+2/*ASU ID Length*/
		+2/*AE Cert type*/
		+2/*AE Cert length*/
		+1/*ECDH Parameter Identifier*/
		+2/*ECDH Parameter Length */;

	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);
        
	wapi_sm_set_state(sm, WPA_4WAY_HANDSHAKE);
	
	if(len < auth_act_len) {
		wpa_printf(MSG_ERROR, "WAPI CERT: WAPI frame payload too short");
		return -1;
	}

	flag = payload[0];
	ae_auth_flag = payload+1;
	asu_id = payload+1 + 32;

	len -= auth_act_len;

	//int bk_up = flag & BIT(0);

	if(flag & BIT(0)) {
		if(os_memcmp(ae_auth_flag, sm->ae_auth_flag, 32) !=0) {
			wpa_printf(MSG_ERROR, "WPA: ae_auth_flag not same!\n");
			return -1;
		}
	} else {
		os_memcpy(sm->ae_auth_flag, ae_auth_flag, 32);
	}

	/*get ASU ID*/
	GETSHORT(asu_id, sm->ae_asu_id.id_flag); 
	GETSHORT((asu_id+2), sm->ae_asu_id.id_len);
	if(len < sm->ae_asu_id.id_len) {
		wpa_printf(MSG_ERROR, "WAPI CERT: WAPI frame payload too short");
		return -1;
	}
	else {
		os_memcpy(sm->ae_asu_id.id_data, asu_id+4, sm->ae_asu_id.id_len);
	}
	len -= sm->ae_asu_id.id_len; 

	/*get AE Certificate*/
	ae_cer = asu_id + 2 + 2 + sm->ae_asu_id.id_len;
	GETSHORT(ae_cer, sm->ae_cert.cert_flag); 
	GETSHORT((ae_cer+2), sm->ae_cert.length); 
	if(len < sm->ae_cert.length){
		wpa_printf(MSG_ERROR, "WAPI CERT: WAPI frame payload too short");
		return -1;
	}
	else {
		os_memcpy(sm->ae_cert.data, ae_cer+4, sm->ae_cert.length);
	}
	len -= sm->ae_cert.length;

	/*get AE ID*/
	wapi_fixdata_id_by_ident(&sm->ae_cert, &(sm->ae_id), sm->ae_cert.cert_flag);
	wpa_hexdump(MSG_DEBUG, "AE ID: ", sm->ae_id.id_data, sm->ae_id.id_len);

	/*get ECDH Parameter*/
	ecdh = ae_cer + 2 + 2 + sm->ae_cert.length;
	sm->ecdh.para_flag = ecdh[0];
	GETSHORT((ecdh + 1), sm->ecdh.para_len);
	if(len < sm->ecdh.para_len) {
		wpa_printf(MSG_ERROR, "WAPI CERT: WAPI frame payload too short");
		return -1;
	}
	else {
		os_memcpy(sm->ecdh.para_data, ecdh+3, sm->ecdh.para_len);
	}
	len -= sm->ecdh.para_len;

	if(len != 0) 
        {
		wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);
		return -1;
	}

	asue_initialize_alg(sm);

	ret = asue_x_x_p_derivation(sm);
	if(ret == -1)
		return -1;

	//os_get_random(sm->Nasue, 32);
    GetRandom_Value(sm->Nasue, 32);
	
	if (wapi_cert_2_3_send(sm, payload, len))
	{
		wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);
		return -1;
	}
	return 0;
}

static int asue_certauthbk_derivation(struct wapi_sm *sm, const unsigned char *src_addr)
{
	char input_text[] = "base key expansion for key and additional nonce";
	u8 text[256] = {0,};
	u8 temp_out[48] = {0,};
	u8  ecdhkey[24] = {0,};
	int  ecdhkeyl = sizeof(ecdhkey);
	int ret = -1;

	wpa_hexdump(MSG_DEBUG, "asue_eck", sm->asue_eck.data, sm->asue_eck.length);
	wpa_hexdump(MSG_DEBUG, "ae_key_data", sm->ae_key_data.data, sm->ae_key_data.length);

	ret = ecc192_ecdh(sm->asue_eck.data, sm->ae_key_data.data, ecdhkey);

	if (!ret)
		{
			wpa_printf(MSG_DEBUG, "asue_certauthbk_derivation ECHD fail : in %s:%d", __func__, __LINE__);
			ret = -1;
			return ret;
		}

	wpa_hexdump(MSG_DEBUG, "ecdhkey", ecdhkey,ecdhkeyl);

	os_memset(text, 0, sizeof(text));
	os_memcpy(text, sm->Nae, 32);
	os_memcpy(text + 32, sm->Nasue, 32);
	os_memcpy(text + 32 + 32, input_text, strlen(input_text));

	KD_hmac_sha256(text, 32+32+strlen(input_text), 
		       ecdhkey, 24,
		       temp_out, 16 + 32);

	wpa_hexdump(MSG_DEBUG, "text", text,32+32+strlen(input_text));
	wpa_hexdump(MSG_DEBUG, "temp_out",temp_out,48);

	os_memcpy(sm->pmk, temp_out, 16);
	os_memset(text, 0, sizeof(text));
	os_memcpy(text, sm->bssid, ETH_ALEN);
	os_memcpy(text + ETH_ALEN, src_addr, ETH_ALEN);

	wpa_hexdump(MSG_DEBUG, "text1", text,32+32+strlen(input_text));

	KD_hmac_sha256(text, 12,
		       sm->pmk, 16, 
		       sm->bkid, 16);
	
	mhash_sha256(temp_out + 16, 32, sm->ae_auth_flag);
	wpa_hexdump(MSG_ERROR, "Generated PMK: ", sm->pmk, 16); 
	sm->pmk_len = 16;
	wpa_hexdump(MSG_ERROR, "bkid: ", sm->bkid, 16);
	ret = 0;
	return ret;
}

static int wapi_process_cert_3_of_3(struct wapi_sm* sm, const unsigned char *src_addr, u8* payload, size_t len)
{
	u8 flag = 0 ;
	u8 *Nasue = NULL, *Nae = NULL, *acc_res = NULL;
	u8 *asue_key_data = NULL,*ae_key_data = NULL;
	u8 *ae_id = NULL, *asue_id = NULL, *cert_res = NULL;
	u8 *ae_sign = NULL;
	int request_len = 0;
	const struct cert_obj_st_t  *cert_obj = NULL;

	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);

	/*flag*/
	flag = payload[0];
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,flag=%d", __func__, __LINE__,flag);
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,flag=%d,(flag & BIT(0) )=%d", __func__, __LINE__,flag,(flag & BIT(0) ));
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,sm->flag=%d,(sm->flag & BIT(0) )=%d", __func__, __LINE__,sm->flag,(sm->flag & BIT(0) ));
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,flag=%d,(flag & BIT(1) )=%d", __func__, __LINE__,flag,(flag & BIT(1) ));
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,sm->flag=%d,(sm->flag & BIT(1) )=%d", __func__, __LINE__,sm->flag,(sm->flag & BIT(1) ));

	if((flag &0x03)!= (sm->flag & 0x03))
		{
			wpa_printf(MSG_ERROR, "WAPI CERT: not same flag bit 0,1!\n");
			return -1;
		}

	request_len +=1;
	Nasue = payload+1;
	if(Nasue == NULL) return -1;
	if(os_memcmp(Nasue, sm->Nasue , 32)!=0)
		{
			wpa_printf(MSG_ERROR, "WAPI CERT: not same Nasue!\n");
			return -1;
		}

	request_len +=32;
	Nae = Nasue + 32;
	if(Nae== NULL) return -1;
	os_memcpy(sm->Nae, Nae, 32);
	request_len +=32;
	acc_res = Nae + 32;
	if(acc_res == NULL) return -1;
	if(acc_res[0] != 0) return -2;
	request_len +=1;
	asue_key_data = acc_res + 1;
	if(asue_key_data == NULL) return -1;
	if((asue_key_data[0] != sm->asue_key_data.length)
	   ||os_memcmp((char *)asue_key_data+1, &(sm->asue_key_data.data), sm->asue_key_data.length )!=0)
		{
			wpa_printf(MSG_ERROR, "WAPI CERT: not same asue key data!,asue_len =%d, sm->asue_key_data.length=%d\n",asue_key_data[0],sm->asue_key_data.length);
			return -1;
		}

	request_len += sm->asue_key_data.length + 1;
	ae_key_data = asue_key_data + sm->asue_key_data.length + 1;
	if(ae_key_data == NULL) return -1;
	os_memcpy(sm->ae_key_data.data, ae_key_data+1,ae_key_data[0]);
	sm->ae_key_data.length = ae_key_data[0];
	request_len +=ae_key_data[0] + 1;
	ae_id = ae_key_data + ae_key_data[0] + 1;
	if(ae_id == NULL) return -1;
	if (cmp_var_struct(ae_id, &(sm->ae_id), sm->ae_id.id_len))
		{
			wpa_printf(MSG_ERROR, "ERROR: WAPI CERT: not same ae id!\n");
			wpa_hexdump(MSG_ERROR, "ae_id", ae_id,sm->ae_id.id_len+ 4);
			wpa_hexdump(MSG_ERROR, "sm->ae_id", (unsigned char *)&(sm->ae_id),sm->ae_id.id_len+ 4);
			return -1;
		}

	request_len += sm->ae_id.id_len+ 4;
	wpa_printf(MSG_DEBUG, "WAPI: in %s:%d,request_len=%d, sm->ae_id.id_len=%d", __func__, __LINE__,request_len, sm->ae_id.id_len);
	asue_id = ae_id + sm->ae_id.id_len+ 4;
	if(asue_id == NULL) return -1;
	if (cmp_var_struct(asue_id, &(wapi_cert.asue_id), wapi_cert.asue_id.id_len))
		{
			wpa_printf(MSG_ERROR, "WAPI CERT: not same asue id!\n");
			return -1;
		}

	request_len += wapi_cert.asue_id.id_len+ 4;
	wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,request_len=%d,sm->asue_id.id_len=%d", __func__, __LINE__, request_len, wapi_cert.asue_id.id_len);
#if 0
	if(flag & BIT(3))
		{
			u8 *cert_pos = NULL, *asu_sign = NULL;
			u16 fix_data_len=0, sign_len=0; 
			tkey *pubkey = NULL;
			wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,wapi_cert.asue_id.id_len=%d", __func__, __LINE__,wapi_cert.asue_id.id_len);

			cert_res = asue_id + wapi_cert.asue_id.id_len+ 4;
			if(cert_res[0] != 2)
				{
					wpa_printf(MSG_ERROR, "ERROR: cert result flag is not 2!\n");
					return -1;
				}
			cert_pos = cert_res + 1 +2 + 32 + 32 + 1 + 2 ;
			GETSHORT((cert_pos), fix_data_len);
			cert_pos = cert_pos + fix_data_len + 2;
			if(cert_pos[0] != 0)
				{
					wpa_printf(MSG_ERROR, "ERROR: cert result  is not ok!\n");
					return -2;
				}

			GETSHORT((cert_pos + 1 + 2), fix_data_len);
			asu_sign = cert_pos + fix_data_len + 1 + 2 + 2;
			GETSHORT((asu_sign + 1), sign_len);
			wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);

			GETSHORT((cert_res + 1), fix_data_len);
			cert_obj = get_cert_obj(wapi_cert.cert_info.config.used_cert);
			pubkey = cert_obj->asu_pubkey;
			if(!(*cert_obj->verify)(pubkey->data, pubkey->length,
						cert_res,fix_data_len + 3,
						asu_sign + 1 + 2 + sign_len - 48, 
						48))
				{
					wpa_hexdump(MSG_ERROR, "pubkey->data", pubkey->data,pubkey->length);
					wpa_hexdump(MSG_ERROR, "cert_res", cert_res,fix_data_len + 3);
					wpa_hexdump(MSG_ERROR, "asu_sign", asu_sign + 1 + 2 + sign_len - 48,48);
					wpa_printf(MSG_ERROR, "ASU sign error!!!");
					return -1;
				}

			request_len += fix_data_len + 3+sign_len+3 ;
			wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d,request_len=%d,len=%d", __func__, __LINE__,request_len,len);
		}
	{
		tkey *pubkey = NULL;
		u16 fix_data_len = 0;
		u16 cert_flag = sm->ae_cert.cert_flag;//ntohs(sm->ae_cert.cert_flag);
		ae_sign = payload + len - 48;
		cert_obj = get_cert_obj(cert_flag);
		wpa_printf(MSG_DEBUG, "*** WAPI CERT: in %s:%d, sm->ae_cert.len=%d", __func__, __LINE__, sm->ae_cert.length);

		pubkey = (*cert_obj->get_public_key)((void *)&sm->ae_cert);

		if(!(*cert_obj->verify)(pubkey->data, pubkey->length,
					payload,request_len,
					ae_sign, 
					48))
			{
				pubkey = free_buffer(pubkey, sizeof(tkey));
				wpa_printf(MSG_ERROR, "AE sign error!!!");
				return -1;
			}

		wpa_printf(MSG_DEBUG, "*** WAPI CERT: in %s:%d", __func__, __LINE__);
		pubkey = free_buffer(pubkey, sizeof(tkey));
		wpa_printf(MSG_DEBUG, "WAPI CERT: in %s:%d", __func__, __LINE__);

		GETSHORT((payload+request_len+1), fix_data_len);
		request_len += fix_data_len + 3;
	}

	if(len != request_len)
		{
			wpa_printf(MSG_DEBUG, "ERROR! ---> WAPI CERT: in %s:%d,request_len=%d,len=%d", __func__, __LINE__,request_len,len);
			return -1;
		}
#endif
	wpa_printf(MSG_DEBUG, "*** WAPI CERT: in %s:%d", __func__, __LINE__);
	asue_certauthbk_derivation(sm, src_addr);

	return 0;
}


static void wapi_process_1_of_3(struct wapi_sm *sm,
                                          const unsigned char *src_addr,
                                          const struct wapi_pairwise_key_req *key)
{
        struct wapi_ptk *ptk;

        if (wapi_sm_get_ssid(sm) == NULL) {
                wpa_printf(MSG_WARNING, "WAPI: No SSID info found (msg 1 of "
                           "3).");
                return;
        }

	sm->rekey_flag = key->flag; /* used for rekeying */

	if((key->flag & 0x10) == 0)
		wapi_sm_set_state(sm, WPA_4WAY_HANDSHAKE);

        wpa_printf(MSG_DEBUG, "WAPI: RX message 1 of 3-Way Handshake from "
                   MACSTR, MAC2STR(src_addr));

	if (sm->renew_snonce) {
                if (hostapd_get_rand(sm->snonce, WAPI_NONCE_LEN)) {
                        wpa_msg(sm->ctx->ctx, MSG_WARNING,
                                "WAPI: Failed to get random data for SNonce");
                        return;
                }
                sm->renew_snonce = 0; /* FIXME: should we keep the same nonce here? */
                wpa_hexdump(MSG_DEBUG, "WAPI: Renewed SNonce",
                            sm->snonce, WAPI_NONCE_LEN);
        }
#if 0 /* TEST */
	{
		u8 temp[16+32];
		u8 temp_addid[12] = { 0x00, 0x0b, 0xc0, 0x02, 0x44, 0x67,  0x00, 0x50, 0xc2, 0x6a, 0xf1, 0xbb};
		
	     	//wpa_hexdump(MSG_DEBUG, "###key->addid: ", key->addid, 12);
		wpa_hexdump(MSG_DEBUG, "###key->BKID: ", key->bkid, 16);

		kd_hmac_sha256(sm->pmk, 16, key->addid, 12, temp, 16);
		wpa_hexdump(MSG_DEBUG, "###TEMP BKID: ", temp, 16);
	}
#endif
	ptk = &sm->ptk;
        wapi_pmk_to_ptk(sm->pmk, sm->pmk_len, "pairwise key expansion for unicast and additional keys and nonce",
			key->addid, key->auth_challenge, sm->snonce, 
                       (u8 *) ptk, sizeof(*ptk));
	
	sm->ptk_set = 1;

	if (wapi_send_2_of_3(sm, key, sm->snonce,
				      sm->assoc_wapi_ie, sm->assoc_wapi_ie_len,
				      ptk))
                return;

	if((sm->rekey_flag & 0x10) == 0)
		os_memcpy(sm->anonce, key->auth_challenge, WAPI_NONCE_LEN);

	/* SNonce was successfully used in msg 1/3, so mark it to be renewed
         * for the next 3-Way Handshake. If msg 1 is received again, the old
         * SNonce will still be used to avoid changing PTK. */
        sm->renew_snonce = 1;

	wapi_install_ptk(sm, key);
}

static void wapi_process_3_of_3(struct wapi_sm *sm,
				const unsigned char *src_addr,
				const struct wapi_pairwise_key_confirm *key)
{
	if((sm->rekey_flag & 0x10) == 0)
		wapi_sm_set_state(sm, WPA_4WAY_HANDSHAKE);
	wpa_printf(MSG_DEBUG, "WAPI: RX message 3 of 3-Way Handshake from "
                   MACSTR, MAC2STR(src_addr));

        if (os_memcmp(sm->snonce, key->supp_challenge, WAPI_NONCE_LEN) != 0) {
                wpa_printf(MSG_WARNING, "WAPI: SNonce from message 3 of 3-Way "
                           "Handshake differs from 1 of 3-Way Handshake - drop"
                           " packet (src=" MACSTR ")", MAC2STR(sm->bssid));
                return;
        }

	/* TODO: Calculate localy the MAC and compare with the received */
	//wapi_key_mic(kck, msg, msg_len, key_mic);

	/* TODO: Validate the AP WAPI IE */

	/* FIXME: Check if this is needed */
	//	if (key_info & WPA_KEY_INFO_SECURE) {
	/*wapi_sm_mlme_setprotection(
					  sm, sm->bssid, MLME_SETPROTECTION_PROTECT_TYPE_RX,
					  MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);*/
	eapol_sm_notify_portValid(sm->eapol, TRUE);
	//}
	if((sm->rekey_flag & 0x10) == 0)
		wapi_sm_set_state(sm, WPA_GROUP_HANDSHAKE);
}

static void wapi_key_neg_complete(struct wapi_sm *sm,
				  const u8 *addr, int secure)
{
        wpa_msg(sm->ctx->ctx, MSG_INFO, "WAPI: Key negotiation completed with "
                MACSTR " [PTK=%s GTK=%s]", MAC2STR(addr),
                wapi_cipher_txt(sm->pairwise_cipher),
                wapi_cipher_txt(sm->group_cipher));
        wapi_sm_cancel_auth_timeout(sm);
        wapi_sm_set_state(sm, WPA_COMPLETED);

        if (secure) {
                wapi_sm_mlme_setprotection(
					   sm, addr, MLME_SETPROTECTION_PROTECT_TYPE_RX_TX,
					  MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);
                eapol_sm_notify_portValid(sm->eapol, TRUE);
                //if (sm->key_mgmt == WAPI_KEY_MGMT_PSK)
                        eapol_sm_notify_eap_success(sm->eapol, TRUE);

		/*
                 * Start preauthentication after a short wait to avoid a
                 * possible race condition between the data receive and key
                 * configuration after the 4-Way Handshake. This increases the
                 * likelyhood of the first preauth EAPOL-Start frame getting to
                 * the target AP.
                 */
                //eloop_register_timeout(1, 0, wpa_sm_start_preauth, sm, NULL);
        }

        /*if (sm->cur_pmksa && sm->cur_pmksa->opportunistic) {
                wpa_printf(MSG_DEBUG, "RSN: Authenticator accepted "
                           "opportunistic PMKSA entry - marking it valid");
                sm->cur_pmksa->opportunistic = 0;
		}*/
}

static int wpi_encrypt(unsigned char * pofbiv_in,unsigned char * pbw_in,unsigned int plbw_in,unsigned char * pkey,unsigned char * pcw_out)
{
        unsigned int ofbtmp[4];
        unsigned int * pint0, * pint1;
        unsigned char * pchar0, * pchar1,* pchar2;
        unsigned int counter,comp,i;
        unsigned int prkey_in[32];


        if(plbw_in<1)   return 1;
        //if(plbw_in>65536) return 1;

        SMS4KeyExt(pkey,  prkey_in, 0);

        counter=plbw_in / 16;
        comp=plbw_in % 16;

	//get the iv
        SMS4Crypt(pofbiv_in,(unsigned char *)ofbtmp, prkey_in);
        pint0=(unsigned int *)pbw_in;
        pint1=(unsigned int *)pcw_out;
        for(i=0;i<counter;i++) {
                pint1[0]=pint0[0]^ofbtmp[0];
                pint1[1]=pint0[1]^ofbtmp[1];
                pint1[2]=pint0[2]^ofbtmp[2];
                pint1[3]=pint0[3]^ofbtmp[3];
                SMS4Crypt((unsigned char *)ofbtmp,(unsigned char *)ofbtmp, prkey_in);
                pint0+=4;
                pint1+=4;
        }
	pchar0=(unsigned char *)pint0;
        pchar1=(unsigned char *)pint1;
        pchar2=(unsigned char *)ofbtmp;
        for(i=0;i<comp;i++) {
                pchar1[i]=pchar0[i]^pchar2[i];
        }

        return 0;
}

#define MSK_TEXT "multicast or station key expansion for station unicast and multicast and broadcast"

static void wapi_process_1_of_2(struct wapi_sm *sm,
                                const unsigned char *src_addr,
                                const struct wapi_multicast_key_announce *key)
{
        const u8 *pos;
        u8 nmk[16] = {0,};
        u8 msk[32];
        size_t msk_len;
        u8 rsc[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        u8 temp_msk[16];
        u8 iv[16];
	u8 hash[SHA256_MAC_LEN];
	u8 mic[20];
	u8 buf[3+12+16+16+17];
	int len = 3+12+16+16+17;

        wpa_printf(MSG_DEBUG, "WAPI: RX message 1 of 2 - Multicast Handshake from "
                   MACSTR, MAC2STR(src_addr));

        wapi_sm_set_state(sm, WPA_GROUP_HANDSHAKE);

        pos = (const u8 *) (key + 1); /* 17 bytes from pos, byte 0 is the length and the rest the key itself*/
	
	os_memset(temp_msk, 0, sizeof(temp_msk));
        os_memcpy(temp_msk, pos+1, 16);
        os_memcpy(iv, key->key_id, 16);
        wpa_hexdump(MSG_DEBUG, "IV: ", iv, 16);
        os_memcpy(nmk, temp_msk, 16);

        wpi_encrypt(iv, temp_msk, 16, sm->ptk.kek, nmk);

	/* Mic Calculation */	
	buf[0] = key->flag;
	buf[1] = key->mskid;
	buf[2] = key->uskid;
	os_memcpy(buf+3, key->addid, 12);
	os_memcpy(buf+3+12, key->data_seq_num, 16);
	os_memcpy(buf+3+12+16, key->key_id, 16);
	buf[3+12+16+16] = 16;
	os_memcpy(buf+3+12+16+16+1, pos+1, 16);
	
	hmac_sha256(sm->ptk.mac, 16, buf, len, hash);
	os_memcpy(mic, hash, 20);
	
	if(os_memcmp(mic, pos+1+16, 20) != 0)
		wpa_printf(MSG_DEBUG, "Multicast  announcement packet ---> mic  is WRONG");
	else
		wpa_printf(MSG_DEBUG, "Multicast  announcement packet ---> mic is CORRECT");

        wpa_hexdump(MSG_DEBUG, "NMK: ", nmk, 16);
        msk_len = 32;
        kd_hmac_sha256(nmk, 16, (u8 *)MSK_TEXT, strlen(MSK_TEXT), msk, msk_len);

	wpa_hexdump(MSG_DEBUG, "WAPI: MSK (encryption key + integrity check)", msk, msk_len);

        wpa_printf(MSG_DEBUG, "WAPI: Installing MSK to the driver "
                   "(keyidx=%d tx=%d).", key->mskid, 0);

        rsc[0] = 0x5c;
        rsc[1] = 0x36;

        wapi_send_2_of_2(sm, key);
	/*      if (rekey) {
                wpa_msg(sm->ctx->ctx, MSG_INFO, "WPA: Group rekeying "
                        "completed with " MACSTR " [GTK=%s]",
                        MAC2STR(sm->bssid), wpa_cipher_txt(sm->group_cipher));
                wpa_sm_set_state(sm, WPA_COMPLETED);
                } else {*/

        wapi_sm_set_key(sm, WAPI_ALG_SMS4,
			(u8 *) "\xff\xff\xff\xff\xff\xff",
                        key->mskid, 0, /*key->data_seq_num*/rsc, 8,
                        msk, 32);

        wapi_key_neg_complete(sm, sm->bssid, 1);
}

/**
 * wapi_sm_rx_eapol - Process received WAPI Protocol frames
 * @sm: Pointer to WAPI state machine data from wapi_sm_init()
 * @src_addr: Source MAC address of the WAPI Protocol packet
 * @buf: Pointer to the beginning of the WAPI data (WAPI header)
 * @len: Length of the WAPI frame
 * Returns: 1 = WAPI Key processed, 0 = not a WAPI-Key, -1 failure
 *
 * This function is called for each received WAPI frame. Other than WAPI-Key
 * frames can be skipped if filtering is done elsewhere. wapi_sm_rx_eapol() is
 * only processing WAPI Key frames.
 *
 * The received WAPI-Key packets are validated and valid packets are replied
 * to. In addition, key material (PTK, GTK) is configured at the end of a
 * successful key handshake.
 */
int wapi_sm_rx_eapol(struct wapi_sm *sm, const u8 *src_addr,
                    const u8 *buf, size_t len)
{
	size_t plen, data_len;
	struct wapi_hdr *hdr;
	struct wapi_pairwise_key_req *key_request = NULL;
	struct wapi_pairwise_key_confirm *key_confirm = NULL;
	struct wapi_multicast_key_announce *key_announce = NULL;
	u8 *tmp;
        int ret = -1;

	if (len < sizeof(*hdr) + sizeof(*key_request)) { /* wapi_parwise_key_request is the smallest packet */
                wpa_printf(MSG_DEBUG, "WAPI: WAPI frame too short to be a WAPI "
                           "unicast-Key (len %lu, expecting at least %lu)",
                           (unsigned long) len,
                           (unsigned long) sizeof(*hdr) + sizeof(*key_request));
                return 0;
        }

        tmp = os_malloc(len);
        if (tmp == NULL)
                return -1;
        os_memcpy(tmp, buf, len);

	hdr = (struct wapi_hdr *) tmp;

	switch(hdr->subtype) {
	case WAPI_SUBTYPE_AUTH_ACTIVATION:
		wpa_printf(MSG_DEBUG, "WAPI: Authentication Activation received");
		break;
	case WAPI_SUBTYPE_AUTH_RESPONSE:
		wpa_printf(MSG_DEBUG, "WAPI: Authentication Response received");
		break;
	case WAPI_SUBTYPE_UNICAST_KEY_REQUEST:
		wpa_printf(MSG_DEBUG, "WAPI: Pairwise Key Request received");
		key_request = (struct wapi_pairwise_key_req *) (hdr + 1);
		break;
	case WAPI_SUBTYPE_UNICAST_KEY_CONFIRM:
		wpa_printf(MSG_DEBUG, "WAPI: Pairwise Key Confirmation received");
		key_confirm = (struct wapi_pairwise_key_confirm *)(hdr + 1);
		break;
	case WAPI_SUBTYPE_MULTICAST_KEY_REQUEST:
		wpa_printf(MSG_DEBUG, "WAPI: Multicast Key Request received");
		key_announce = (struct wapi_multicast_key_announce *)(hdr +1);
		break;
	default:
		wpa_printf(MSG_DEBUG, "wapi_sm_rx_eapol: ERROR -> subtype: %d", hdr->subtype); 
		goto out;
	}

        plen = be_to_host16(hdr->length);
        data_len = plen + sizeof(*hdr);
        wpa_printf(MSG_DEBUG, "WAPI RX: version=%d subtype=%d length=%lu",
                   hdr->version, hdr->subtype, (unsigned long) plen);

	eapol_sm_notify_lower_layer_success(sm->eapol);
        if (data_len < len) {
                wpa_printf(MSG_DEBUG, "WAPI: ignoring %lu bytes after the"
                           "WAPI data", (unsigned long) len - data_len);
        }

	//extra_len = data_len - sizeof(*hdr) - sizeof(*key);

	if(hdr->subtype == WAPI_SUBTYPE_UNICAST_KEY_REQUEST)
		wapi_process_1_of_3(sm, src_addr, key_request);
	else if(hdr->subtype == WAPI_SUBTYPE_UNICAST_KEY_CONFIRM)
		wapi_process_3_of_3(sm, src_addr, key_confirm);
	else if (hdr->subtype == WAPI_SUBTYPE_MULTICAST_KEY_REQUEST)
		wapi_process_1_of_2(sm, src_addr, key_announce);
	else if (hdr->subtype == WAPI_SUBTYPE_AUTH_ACTIVATION)
		wapi_process_cert_1_of_3(sm, src_addr, (u8 *)(hdr + 1), (len - sizeof(*hdr)));
	else if (hdr->subtype == WAPI_SUBTYPE_AUTH_RESPONSE)
		wapi_process_cert_3_of_3(sm, src_addr, (u8 *)(hdr + 1), (len - sizeof(*hdr)));
	else {
		wpa_printf(MSG_DEBUG, "WAPI: Unknown subtype %d", hdr->subtype);
		goto out;
	}
	
	ret = 1;

 out:
	os_free(tmp);
	return ret;
}

#else //#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
/******************************************************************************/
/*                 Dummy functions for platforms without WAPI                 */
/******************************************************************************/
int wapi_parse_ie(const u8 *wapi_ie, size_t wapi_ie_len, struct wapi_ie_data *data)
{
   return -1;
}

void wapi_sm_notify_assoc(struct wapi_sm *sm, const u8 *bssid)
{
}

void wapi_sm_notify_disassoc(struct wapi_sm *sm)
{
}

int wapi_sm_set_ap_ie(struct wapi_sm *sm, const u8 *ie, size_t len)
{
   return -1;
}

int wapi_sm_set_assoc_ie_default(struct wapi_sm *sm, u8 *wapi_ie,
                                    size_t *wapi_ie_len)
{
   return -1;
}

void wapi_sm_set_config(struct wapi_sm *sm, struct wpa_ssid *config)
{
}

void wapi_sm_deinit(struct wapi_sm *sm)
{
}

int wapi_sm_rx_eapol(struct wapi_sm *sm, const u8 *src_addr,
                    const u8 *buf, size_t len)
{
   return 0;
}

void wapi_sm_set_eapol(struct wapi_sm *sm, struct eapol_sm *eapol)
{
}

int wapi_sm_set_assoc_ie(struct wapi_sm *sm, const u8 *ie, size_t len)
{
   -1;
}

#endif //#if (DE_BUILTIN_WAPI == CFG_INCLUDED)


/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: nil */
/* End: */
