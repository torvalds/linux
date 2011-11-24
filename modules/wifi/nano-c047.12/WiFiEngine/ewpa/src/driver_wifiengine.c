/*
 * WPA Supplicant - driver interaction with generic Linux Wireless Extensions
 * Copyright (c) 2003-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements a driver interface for the Linux Wireless Extensions.
 * When used with WE-18 or newer, this interface can be used as-is with number
 * of drivers. In addition to this, some of the common functions in this file
 * can be used by other driver interface implementations that use generic WE
 * ioctls, but require private ioctls for some of the functionality.
 */

#include "includes.h"
#include "common.h"
#include "driver.h"
#include "drivers.h"
#include "wpa_supplicant.h"
#include "wapi.h"
#include "l2_packet.h"

#ifdef __rtke__
#include "Queues.h"
#include "rtke_protos.h"
#endif

#if defined (__embos__) || defined(__nucleus__) || defined (__mqx__) || defined(__linux__)
typedef char PACKET;
#include "driverenv.h"
#endif

struct wpa_driver_WE_data {
	void *ctx;
#define KEYMASK_KEY_CLEARED (1U << 31)
	uint32_t keymask;
};


static int wpa_driver_WE_flush_pmkid(void *priv);
static int wpa_driver_WE_set_mode(void *priv, int mode);


/**
 * wpa_driver_WE_get_bssid - Get BSSID, SIOCGIWAP
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @bssid: Buffer for BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_WE_get_bssid(void *priv, u8 *bssid)
{
	int status;

	m80211_mac_addr_t a;

	status = WiFiEngine_GetBSSID(&a);
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;

	os_memcpy(bssid, a.octet, 6);
	return 0;
}


/**
 * wpa_driver_WE_set_bssid - Set BSSID, SIOCSIWAP
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @bssid: BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_WE_set_bssid(void *priv, const u8 *bssid)
{
	int status;
	m80211_mac_addr_t a;

	if(bssid != NULL)
		os_memcpy(a.octet, bssid, 6);
	else
		os_memset(a.octet, 0xff, 6);
	status = WiFiEngine_SetDesiredBSSID(&a);
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}


/**
 * wpa_driver_WE_get_ssid - Get SSID, SIOCGIWESSID
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @ssid: Buffer for the SSID; must be at least 32 bytes long
 * Returns: SSID length on success, -1 on failure
 */
int wpa_driver_WE_get_ssid(void *priv, u8 *ssid)
{
	m80211_ie_ssid_t id;
	
	if(WiFiEngine_GetSSID(&id) != WIFI_ENGINE_SUCCESS)
		return -1;
	
	DE_MEMCPY(ssid, id.ssid, id.hdr.len);
	return id.hdr.len;
}


/**
 * wpa_driver_WE_set_ssid - Set SSID, SIOCSIWESSID
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @ssid: SSID
 * @ssid_len: Length of SSID (0..32)
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_WE_set_ssid(void *priv, const u8 *ssid, size_t ssid_len)
{
	int status;

	status = WiFiEngine_SetSSID((void*)ssid, ssid_len);
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
   
	return 0;
}


/**
 * wpa_driver_WE_set_freq - Set frequency/channel, SIOCSIWFREQ
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @freq: Frequency in MHz
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_WE_set_freq(void *priv, int freq)
{
	return 0;
}


#if 0
static int wpa_driver_WE_event_wireless_michaelmicfailure(
	void *ctx, const char *ev, size_t len)
{
	return 0;
}


static int wpa_driver_WE_event_wireless_pmkidcand(
	struct wpa_driver_WE_data *drv, const char *ev, size_t len)
{
	return 0;
}


static int wpa_driver_WE_event_wireless_assocreqie(
	struct wpa_driver_WE_data *drv, const char *ev, int len)
{
	return 0;
}


static int wpa_driver_WE_event_wireless_assocrespie(
	struct wpa_driver_WE_data *drv, const char *ev, int len)
{
	return 0;
}


static void wpa_driver_WE_event_assoc_ies(struct wpa_driver_WE_data *drv)
{
}


static void wpa_driver_WE_event_wireless(struct wpa_driver_WE_data *drv,
					   void *ctx, char *data, int len)
{
}
#endif



/**
 * wpa_driver_WE_init - Initialize WE driver interface
 * @ctx: context to be used when calling wpa_supplicant functions,
 * e.g., wpa_supplicant_event()
 * @ifname: interface name, e.g., wlan0
 * Returns: Pointer to private data, %NULL on failure
 */
void * wpa_driver_WE_init(void *ctx, const char *ifname)
{
	struct wpa_driver_WE_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	drv->keymask = 0;

	/*
	 * Make sure that the driver does not have any obsolete PMKID entries.
	 */
	wpa_driver_WE_flush_pmkid(drv);

	if (wpa_driver_WE_set_mode(drv, 0) < 0) {
		wpa_printf(MSG_DEBUG, "Could not configure driver to use managed mode\n");
	}

	return drv;
}


/**
 * wpa_driver_WE_deinit - Deinitialize WE driver interface
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 *
 * Shut down driver interface and processing of driver events. Free
 * private data buffer if one was allocated in wpa_driver_WE_init().
 */
void wpa_driver_WE_deinit(void *priv)
{
	struct wpa_driver_WE_data *drv = priv;

	/*
	 * Clear possibly configured driver parameters in order to make it
	 * easier to use the driver after wpa_supplicant has been terminated.
	 */
	wpa_driver_WE_set_bssid(drv, (u8 *) "\xff\xff\xff\xff\xff\xff");

	os_free(drv);
}


#if 0
/**
 * wpa_driver_WE_scan_timeout - Scan timeout to report scan completion
 * @eloop_ctx: Unused
 * @timeout_ctx: ctx argument given to wpa_driver_WE_init()
 *
 * This function can be used as registered timeout when starting a scan to
 * generate a scan completed event if the driver does not report this.
 */
static void wpa_driver_WE_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}
#endif


/**
 * wpa_driver_WE_scan - Request the driver to initiate scan
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @ssid: Specific SSID to scan for (ProbeReq) or %NULL to scan for
 *	all SSIDs (either active scan with broadcast SSID or passive
 *	scan
 * @ssid_len: Length of the SSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_WE_scan(void *priv, const u8 *ssid, size_t ssid_len)
{
	return 0;
}


#if 0
/* Compare function for sorting scan results. Return >0 if @b is considered
 * better. */
static int wpa_scan_result_compar(const void *a, const void *b)
{
	const struct wpa_scan_result *wa = a;
	const struct wpa_scan_result *wb = b;

	/* WPA/WPA2/WAPI support preferred */
        if ((wb->wpa_ie_len || wb->rsn_ie_len || wb->wapi_ie_len) &&
            !(wa->wpa_ie_len || wa->rsn_ie_len || wa->wapi_ie_len))
                return 1;
        if (!(wb->wpa_ie_len || wb->rsn_ie_len || wb->wapi_ie_len) &&
            (wa->wpa_ie_len || wa->rsn_ie_len || wa->wapi_ie_len))
                return -1;


	/* privacy support preferred */
	if ((wa->caps & IEEE80211_CAP_PRIVACY) == 0 &&
	    (wb->caps & IEEE80211_CAP_PRIVACY))
		return 1;
	if ((wa->caps & IEEE80211_CAP_PRIVACY) &&
	    (wb->caps & IEEE80211_CAP_PRIVACY) == 0)
		return -1;

	/* best/max rate preferred if signal level close enough XXX */
	if (wa->maxrate != wb->maxrate && abs(wb->level - wa->level) < 5)
		return wb->maxrate - wa->maxrate;

	/* use freq for channel preference */

	/* all things being equal, use signal level; if signal levels are
	 * identical, use quality values since some drivers may only report
	 * that value and leave the signal level zero */
	if (wb->level == wa->level)
		return wb->qual - wa->qual;
	return wb->level - wa->level;
}
#endif

/**
 * wpa_driver_WE_get_scan_results - Fetch the latest scan results
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @results: Pointer to buffer for scan results
 * @max_size: Maximum number of entries (buffer size)
 * Returns: Number of scan result entries used on success, -1 on
 * failure
 *
 * If scan results include more than max_size BSSes, max_size will be
 * returned and the remaining entries will not be included in the
 * buffer.
 */
int wpa_driver_WE_get_scan_results(void *priv,
				     struct wpa_scan_result *results,
				     size_t max_size)
{
	return 0;
}



static int wpa_driver_WE_set_wpa(void *priv, int enabled)
{
	return 0;
}


/**
 * wpa_driver_WE_set_key - Configure encryption key
 * @priv: Private driver interface data
 * @alg: Encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
 *	%WPA_ALG_TKIP, %WPA_ALG_CCMP); %WPA_ALG_NONE clears the key.
 * @addr: Address of the peer STA or ff:ff:ff:ff:ff:ff for
 *	broadcast/default keys
 * @key_idx: key index (0..3), usually 0 for unicast keys
 * @set_tx: Configure this key as the default Tx key (only used when
 *	driver does not support separate unicast/individual key
 * @seq: Sequence number/packet number, seq_len octets, the next
 *	packet number to be used for in replay protection; configured
 *	for Rx keys (in most cases, this is only used with broadcast
 *	keys and set to zero for unicast keys)
 * @seq_len: Length of the seq, depends on the algorithm:
 *	TKIP: 6 octets, CCMP: 6 octets
 * @key: Key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
 *	8-byte Rx Mic Key
 * @key_len: Length of the key buffer in octets (WEP: 5 or 13,
 *	TKIP: 32, CCMP: 16)
 * Returns: 0 on success, -1 on failure
 *
 * This function uses SIOCSIWENCODEEXT by default, but tries to use
 * SIOCSIWENCODE if the extended ioctl fails when configuring a WEP key.
 */
int wpa_driver_WE_set_key(void *priv, wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len)
{
	struct wpa_driver_WE_data *drv = priv;
	int status;
	m80211_mac_addr_t bssid;
	m80211_key_type_t keytype;
	receive_seq_cnt_t rsc, *rscp = NULL;
	
	if(addr == NULL)
		os_memset(bssid.octet, 0xff, M80211_ADDRESS_SIZE);
	else
		os_memcpy(bssid.octet, addr, M80211_ADDRESS_SIZE);

	if(wei_is_bssid_bcast(bssid))
		keytype = M80211_KEY_TYPE_GROUP;
	else
		keytype = M80211_KEY_TYPE_PAIRWISE;

	if(seq != NULL && seq_len != 0) {
		if(seq_len > sizeof(rsc.octet))
			return -1;
		os_memset(rsc.octet, 0, sizeof(rsc.octet));
		os_memcpy(rsc.octet, seq, seq_len);
		rscp = &rsc;
	}

	DE_ASSERT(key_idx >= 0 && key_idx <= 3);
	if(alg == WPA_ALG_NONE) {
		/* this works under the assumption that nobody ever
		 * clears just one key, which is true for the current
		 * supplicant implementation */
		drv->keymask &= ~(1 << key_idx);
		if((drv->keymask & KEYMASK_KEY_CLEARED) == 0) {
			drv->keymask |= KEYMASK_KEY_CLEARED;
			status = WiFiEngine_DeleteAllKeys();
		} else
			status = WIFI_ENGINE_SUCCESS;
	} else {
		status = WiFiEngine_AddKey(key_idx, 
					   key_len, 
					   key, 
					   keytype,
					   M80211_PROTECT_TYPE_RX_TX,
					   TRUE, 
					   &bssid, 
					   rscp, 
					   set_tx);
		drv->keymask |= (1 << key_idx);
		drv->keymask &= ~KEYMASK_KEY_CLEARED;
		/*
		 * case WPA/WPA2
		 *   Pairwise key
		 *      ProtectedFrameBit(1)
		 *   Group key
		 *      ProtectedFrameBit(1)
		 *
		 * case WAPI
		 *   Pairwise key
		 *      No action
		 *   Group key
		 *      ProtectedFrameBit(1)
		 */
		if(keytype == M80211_KEY_TYPE_PAIRWISE)
		{
			WiFiEngine_Encryption_t enc;
			WiFiEngine_GetEncryptionMode(&enc);
			if(enc != Encryption_SMS4)
			{
				WiFiEngine_SetProtectedFrameBit(1);
			}
		} else {
			WiFiEngine_SetProtectedFrameBit(1);
		}
	}
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}


static int wpa_driver_WE_set_countermeasures(void *priv,
					       int enabled)
{
	return 0;
}


static int wpa_driver_WE_set_drop_unencrypted(void *priv,
						int enabled)
{
	int status;
	status = WiFiEngine_SetExcludeUnencryptedFlag(enabled);
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}

static int wpa_driver_WE_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	int status;

	status = WiFiEngine_Deauthenticate();
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}


static int wpa_driver_WE_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	int status;

	status = WiFiEngine_Disconnect();
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}


#if 0
static int wpa_driver_WE_set_gen_ie(void *priv, const u8 *ie,
				      size_t ie_len)
{
	return -1;
}
#endif

#if 0
static int wpa_driver_WE_cipher2wext(int cipher)
{
	return 0;
}


static int wpa_driver_WE_keymgmt2wext(int keymgmt)
{
	return 0;
}
#endif

static int
wpa_driver_WE_associate(void *priv,
			  struct wpa_driver_associate_params *params)
{
	return 0;
}


static int wpa_driver_WE_set_auth_alg(void *priv, int auth_alg)
{
	return 0;
}


/**
 * wpa_driver_WE_set_mode - Set wireless mode (infra/adhoc), SIOCSIWMODE
 * @priv: Pointer to private wext data from wpa_driver_WE_init()
 * @mode: 0 = infra/BSS (associate with an AP), 1 = adhoc/IBSS
 * Returns: 0 on success, -1 on failure
 */
static int wpa_driver_WE_set_mode(void *priv, int mode)
{
	int status;

	status = WiFiEngine_SetBSSType(mode ? weIndependent_BSS : weInfrastructure_BSS);
	if(status != WIFI_ENGINE_SUCCESS)
		return -1;
	return 0;
}


static int wpa_driver_WE_add_pmkid(void *priv, const u8 *bssid,
				     const u8 *pmkid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
	int status;
	m80211_mac_addr_t b;
	m80211_pmkid_value p;

	os_memcpy(b.octet, bssid, sizeof(b.octet));
	os_memcpy(p.octet, pmkid, sizeof(p.octet));

	status = WiFiEngine_PMKID_Add(&b, &p);
	if(status == WIFI_ENGINE_SUCCESS)
		return 0;
	return -1;
#else
        return 0;
#endif
}


static int wpa_driver_WE_remove_pmkid(void *priv, const u8 *bssid,
		 			const u8 *pmkid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
	int status;
	m80211_mac_addr_t b;
	m80211_pmkid_value p;

	os_memcpy(b.octet, bssid, sizeof(b.octet));
	os_memcpy(p.octet, pmkid, sizeof(p.octet));

	status = WiFiEngine_PMKID_Remove(&b, &p);
	if(status == WIFI_ENGINE_SUCCESS)
		return 0;
	return -1;
#else
        return 0;
#endif
}


static int wpa_driver_WE_flush_pmkid(void *priv)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
	WiFiEngine_PMKID_Clear();
#endif
	return 0;
}


static int wpa_driver_WE_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	memset(capa, 0, sizeof(*capa));
	capa->key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA
		| WPA_DRIVER_CAPA_KEY_MGMT_WPA2
		| WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK
		| WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK
		/* | WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE */;
	
	capa->enc =  WPA_DRIVER_CAPA_ENC_WEP40
		| WPA_DRIVER_CAPA_ENC_WEP104
		| WPA_DRIVER_CAPA_ENC_TKIP
		| WPA_DRIVER_CAPA_ENC_CCMP;
	
	capa->auth = WPA_DRIVER_AUTH_OPEN
		| WPA_DRIVER_AUTH_SHARED
		| WPA_DRIVER_AUTH_LEAP;

	capa->flags = WPA_DRIVER_FLAGS_DRIVER_IE
		| WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC; /* XXX needed? */
	
	return 0;
}


static char iface_addr[M80211_ADDRESS_SIZE];

static const u8 * wpa_driver_WE_get_mac_addr(void *priv)
{
	int byte_count = sizeof(iface_addr);
	int status;
	
	status = WiFiEngine_GetMACAddress(iface_addr, &byte_count);
	if(status != WIFI_ENGINE_SUCCESS)
		return NULL;
	return (u8*)iface_addr;
}

#ifndef __rtke__
/* @brief Detect final WPA 4-way & WAPI handshake message.
 * 
 * We need to delay the final 4-way handshake message until we've had
 * time to set the keys, but we also can't set the keys before we send
 * the message (since then it would be encrypted). This is solved by
 * delaying the frame in the firmware until encryption is enabled (for
 * a maximum of 50ms). 
 *
 * @param [in] frame       points to ethernet frame
 * @param [in] frame_len   size of ethernet frame
 *
 * @retval TRUE if frame is the final 4-way handshake message
 * @retval FALSE if frame is not the final 4-way handshake message
 */
/* The reason this doesn't live in WiFiEngine (which would make it
 * work automagically on all platforms), is that ProcessSendPacket
 * doesn't conceptually take a complete frame, only the ethernet
 * header. */
static int
is_wpa_4way_final(const void *frame, size_t frame_len)
{
	int i;
	const unsigned char *pkt = frame;

	/* Ethernet frame layout (WAPI):
	 * [offset][size]
	 * [ 0][ 6] dst address
	 * [ 6][ 6] src address
	 * [12][ 2] ether type       (88b4)
	 * [14][ 2] protocol version (1)
	 * [16][ 1] packet type      (1: WAI Protocol Message)
	 * [17][ 1] packet subtype   (0x0c: multicast response)
	 * [18][ 2] reserved
	 * [20][ 2] packet length
	 * more stuff follows...
	 */
	if(pkt[12] == 0x88 && pkt[13] == 0xb4) { /* WAPI ethernet frame */   
		if(frame_len < 3+12+16+20) /* flags + addid + key + mic */
			return FALSE;
		if(pkt[16] != 1)
			return FALSE;
		if(pkt[17] != 0x0c)
			return FALSE;
	} else {  
		/* Ethernet frame layout (WPA):
		 * [offset][size]
		 * [ 0][ 6] dst address
		 * [ 6][ 6] src address
		 * [12][ 2] ether type       (888e)
		 * [14][ 1] protocol version (1)
		 * [15][ 1] packet type      (3: EAPOL-Key)
		 * [16][ 2] packet body length
		 * [18][ 1] descriptor type  (2: RSN, 254: WPA)
		 * [19][ 2] key information  (various flags)
		 * [21][ 2] key length
		 * [23][ 8] replay counter
		 * [31][32] key nonce        (all zeros)
		 * more stuff follows...
		 */
		if(frame_len < 31 + 32)
			return FALSE;
		if(pkt[12] != 0x88 || pkt[13] != 0x8e) /* EAPOL ethernet frame */
			return FALSE;
		if(pkt[14] != 1) /* Version 1 */
			return FALSE;
		if(pkt[15] != 3) /* EAPOL-Key */
			return FALSE;
		if(pkt[18] != 2 && pkt[18] != 254)   /* descriptor type is RSN or WPA */
			return FALSE;
		if((pkt[19] & 0x1d) != 0x01 || /* MIC, !ERROR, !REQUEST, !ENCKEY */
		   (pkt[20] & 0xc8) != 0x08)   /* PAIRWISE, !INSTALL, !ACK */
			return FALSE;
		for(i = 31; i < 31 + 32; i++) { /* check for zero nonce */
			if(pkt[i] != 0)
				return FALSE;
		}
	}

	return TRUE;
}
#endif /* __rtke__ */

/* This function also supports wapi protocol packets */
static int wpa_driver_WE_send_eapol(void *priv, const u8 *dst, u16 proto,
				    const u8 *data, size_t data_len)
{
#ifdef __rtke__
	struct queued_packet *qp;
	
	qp = nr_rtke_alloc_tx_packet(data_len);
	if(qp == NULL)
		return -1;

	qp->tid = 197;

	/*
	 * DST SRC PROTO DATA 
	 *  6   6    2    N 
	 */
	DE_MEMCPY(qp->pkt, dst, 6);
	DE_MEMCPY(qp->pkt + 6, iface_addr, 6);
	proto = ntohs(proto);
	DE_MEMCPY(qp->pkt + 12, (u8 *)&proto, 2);
	DE_MEMCPY(qp->pkt + 14, data, data_len);

	if(nr_tx_pkt2(qp) != 0) {
		nr_rtke_free_tx_packet(qp);
		return -1;
	}
	return 0;
#else /* not __rtke__ */
	size_t dhsize = WiFiEngine_GetDataHeaderSize();
	size_t len = dhsize + 14 + data_len;
	size_t pkt_size;
	int status;
	PACKET *pkt;
   size_t nr_bytes_added;

   nr_bytes_added = WiFiEngine_GetPaddingLength(len);
   pkt            = DriverEnvironment_TX_Alloc(len + nr_bytes_added);

	DE_MEMCPY(pkt + dhsize, dst, 6);
	DE_MEMCPY(pkt + dhsize + 6, iface_addr, 6);
	proto = ntohs(proto);
	DE_MEMCPY(pkt + dhsize + 12, (u8 *)&proto, 2);
	DE_MEMCPY(pkt + dhsize + 14, data, data_len);
	status = WiFiEngine_ProcessSendPacket(pkt + dhsize, 14, 
					      len - dhsize, 
					      pkt, &dhsize, 0, NULL);
   pkt_size = HIC_MESSAGE_LENGTH_GET(pkt);
   
	if(status == WIFI_ENGINE_SUCCESS) {
		if(is_wpa_4way_final(pkt + dhsize, len - dhsize)) {
			pkt[pkt[4] + 4 + 2] |= 4; /* set wait-encrypt flag */
			wpa_printf(MSG_DEBUG, "Detected final 4-way frame");
			wpa_hexdump(MSG_MSGDUMP, "EAPOL-Key Frame:", 
				    (u8*)pkt, len);
		}
   	DriverEnvironment_HIC_Send(pkt, pkt_size);
		return 0;
	}
	DriverEnvironment_TX_Free(pkt);
	return -1;
#endif /* __rtke__ */
}

void wpa_ps_state(int inhibit);

static int wpa_driver_WE_set_operstate(void *priv, int state)
{
	if(state == 1) {
		DriverEnvironment_indicate(WE_IND_WPA_CONNECTED, NULL, 0);
	} else {
		DriverEnvironment_indicate(WE_IND_WPA_DISCONNECTED, NULL, 0);
	}
	return 0;
}

static void wpa_driver_WE_notify_state_change(void *priv, 
					      wpa_states new_state, 
					      wpa_states old_state)
{
	switch(new_state) {
		case WPA_ASSOCIATED:
		case WPA_GROUP_HANDSHAKE:
			wpa_ps_state(TRUE);
			break;
		case WPA_COMPLETED:
		case WPA_DISCONNECTED:
			wpa_ps_state(FALSE);
			break;
		default:
			break;
	}
}

#ifdef __GNUC__
#define MEMBER(N, V) .N = V
#else
#define MEMBER(N, V) V
#endif
const struct wpa_driver_ops wpa_driver_WE_ops = {
	MEMBER(name, "wifiengine"),
	MEMBER(desc, "WiFiEngine"),
	MEMBER(get_bssid, wpa_driver_WE_get_bssid),
	MEMBER(get_ssid, wpa_driver_WE_get_ssid),
	MEMBER(set_wpa, wpa_driver_WE_set_wpa),
	MEMBER(set_key, wpa_driver_WE_set_key),
	MEMBER(init, wpa_driver_WE_init),
	MEMBER(deinit, wpa_driver_WE_deinit),
	MEMBER(set_param, NULL),
	MEMBER(set_countermeasures, wpa_driver_WE_set_countermeasures),
	MEMBER(set_drop_unencrypted, wpa_driver_WE_set_drop_unencrypted),
	MEMBER(scan, wpa_driver_WE_scan),
	MEMBER(get_scan_results, wpa_driver_WE_get_scan_results),
	MEMBER(deauthenticate, wpa_driver_WE_deauthenticate),
	MEMBER(disassociate, wpa_driver_WE_disassociate),
	MEMBER(associate, wpa_driver_WE_associate),
	MEMBER(set_auth_alg, wpa_driver_WE_set_auth_alg),
	MEMBER(add_pmkid, wpa_driver_WE_add_pmkid),
	MEMBER(remove_pmkid, wpa_driver_WE_remove_pmkid),
	MEMBER(flush_pmkid, wpa_driver_WE_flush_pmkid),
	MEMBER(get_capa, wpa_driver_WE_get_capa),
	MEMBER(poll, NULL),
	MEMBER(get_ifname, NULL),
	MEMBER(get_mac_addr, wpa_driver_WE_get_mac_addr),
	MEMBER(send_eapol, wpa_driver_WE_send_eapol),
	MEMBER(set_operstate, wpa_driver_WE_set_operstate),
	MEMBER(mlme_setprotection, NULL),
	MEMBER(get_hw_feature_data, NULL),
	MEMBER(set_channel, NULL),
	MEMBER(set_ssid, NULL),
	MEMBER(set_bssid, NULL),
	MEMBER(send_mlme, NULL),
	MEMBER(mlme_add_sta, NULL),
	MEMBER(mlme_remove_sta, NULL),
	MEMBER(notify_state_change, wpa_driver_WE_notify_state_change)
};
/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
