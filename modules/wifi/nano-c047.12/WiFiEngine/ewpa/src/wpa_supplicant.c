/*
 * WPA/WAPI Supplicant
 * Copyright (c) 2003-2008, Jouni Malinen <j@w1.fi>
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
 * This file implements functions for registering and unregistering
 * %wpa_supplicant interfaces. In addition, this file contains number of
 * functions for managing network connections.
 */

#include "includes.h"

#include "common.h"
#include "eapol_sm.h"
#include "eap.h"
#include "wpa.h"
#include "wapi.h"
#include "wapi_i.h"
#include "cert.h"
#include "ecc.h"
#include "eloop.h"
#include "wpa_supplicant.h"
#include "config.h"
#include "l2_packet.h"
#include "wpa_supplicant_i.h"
#include "ctrl_iface.h"
#include "ctrl_iface_dbus.h"
#include "pcsc_funcs.h"
#include "version.h"
#include "preauth.h"
#include "pmksa_cache.h"
#include "wpa_ctrl.h"
#include "mlme.h"

/*
 * defined in wapi.c
 * TODO: This should be moved to eloop structs
 */
extern struct wapi_cert_data wapi_cert;

const char *wpa_supplicant_version =
"wpa_supplicant v" VERSION_STR "\n"
"Copyright (c) 2003-2008, Jouni Malinen <j@w1.fi> and contributors";

const char *wpa_supplicant_license =
"This program is free software. You can distribute it and/or modify it\n"
"under the terms of the GNU General Public License version 2.\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license. See README and COPYING for more details.\n"
#ifdef EAP_TLS_OPENSSL
"\nThis product includes software developed by the OpenSSL Project\n"
"for use in the OpenSSL Toolkit (http://www.openssl.org/)\n"
#endif /* EAP_TLS_OPENSSL */
;

#ifndef CONFIG_NO_STDOUT_DEBUG
/* Long text divided into parts in order to fit in C89 strings size limits. */
const char *wpa_supplicant_full_license1 =
"This program is free software; you can redistribute it and/or modify\n"
"it under the terms of the GNU General Public License version 2 as\n"
"published by the Free Software Foundation.\n"
"\n"
"This program is distributed in the hope that it will be useful,\n"
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
"GNU General Public License for more details.\n"
"\n";
const char *wpa_supplicant_full_license2 =
"You should have received a copy of the GNU General Public License\n"
"along with this program; if not, write to the Free Software\n"
"Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license.\n"
"\n"
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted provided that the following conditions are\n"
"met:\n"
"\n";
const char *wpa_supplicant_full_license3 =
"1. Redistributions of source code must retain the above copyright\n"
"   notice, this list of conditions and the following disclaimer.\n"
"\n"
"2. Redistributions in binary form must reproduce the above copyright\n"
"   notice, this list of conditions and the following disclaimer in the\n"
"   documentation and/or other materials provided with the distribution.\n"
"\n";
const char *wpa_supplicant_full_license4 =
"3. Neither the name(s) of the above-listed copyright holder(s) nor the\n"
"   names of its contributors may be used to endorse or promote products\n"
"   derived from this software without specific prior written permission.\n"
"\n"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
"LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
"A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n";
const char *wpa_supplicant_full_license5 =
"OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
"SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
"LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
"DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
"THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
"OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n";
#endif /* CONFIG_NO_STDOUT_DEBUG */

extern struct wpa_driver_ops *wpa_supplicant_drivers[];

extern int wpa_debug_use_file;
extern int wpa_debug_level;
extern int wpa_debug_show_keys;
extern int wpa_debug_timestamp;

#ifdef USE_WPS
extern struct wpa_sm *g_wpa;
#endif

extern struct wpa_config *config;

static void wpa_supplicant_scan(void *eloop_ctx, void *timeout_ctx);

#if (DE_BUILTIN_WAPI == CFG_NOT_INCLUDED)
static int eapol_sm_rx_wapi(struct eapol_sm *sm, const u8 *src, const u8 *buf,
		     size_t len)
{
   return 0;
}
#endif //#if (DE_BUILTIN_WAPI == CFG_INCLUDED)

#if defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA)
static u8 * wpa_alloc_eapol(const struct wpa_supplicant *wpa_s, u8 type,
			    const void *data, u16 data_len,
			    size_t *msg_len, void **data_pos)
{
	struct ieee802_1x_hdr *hdr;

	*msg_len = sizeof(*hdr) + data_len;
	hdr = os_malloc(*msg_len);
	if (hdr == NULL)
		return NULL;

	hdr->version = wpa_s->conf->eapol_version;
	hdr->type = type;
	hdr->length = host_to_be16(data_len);

	if (data)
		os_memcpy(hdr + 1, data, data_len);
	else
		os_memset(hdr + 1, 0, data_len);

	if (data_pos)
		*data_pos = hdr + 1;

	return (u8 *) hdr;
}

static u8 * wapi_alloc_packet(const struct wpa_supplicant *wpa_s, u8 subtype,
                            const void *data, u16 data_len,
                            size_t *msg_len, void **data_pos)
{
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
        struct wapi_hdr *hdr;
	size_t seq_num = 2; /* FIXME: seq_num should increase by 1 after rx/tx*/

	switch(subtype) {
	case WAPI_SUBTYPE_AUTH_REQUEST:
		seq_num = 2; /* for wapi cert this will be diferrent */
		break;
	case WAPI_SUBTYPE_UNICAST_KEY_RESPONSE:
		seq_num = 2;
		break;
	case WAPI_SYBTYPE_MULTICAST_KEY_RESPONSE:
		seq_num = 5;
		break;
	default:
		wpa_printf(MSG_DEBUG, "ERROR in seq_num (FIXME) - subtype:%d", subtype);
	}

        *msg_len = sizeof(*hdr) + data_len;
        hdr = os_malloc(*msg_len);
        if (hdr == NULL)
                return NULL;

        hdr->version = host_to_be16(WAPI_VERSION);
        hdr->type = WAPI_TYPE;
	hdr->subtype = subtype;
	hdr->reserved = 0;
        hdr->length = host_to_be16(*msg_len);
	hdr->seq_num = host_to_be16(seq_num);
	hdr->frag_seq_num = 0;
	hdr->flag = 0;

        if (data)
                os_memcpy(hdr + 1, data, data_len);
        else
                os_memset(hdr + 1, 0, data_len);

        if (data_pos)
                *data_pos = hdr + 1;

        return (u8 *) hdr;
#else
        DE_BUG_ON(TRUE,"");
        return NULL;
#endif
}



/**
 * wpa_ether_send - Send Ethernet frame
 * @wpa_s: Pointer to wpa_supplicant data
 * @dest: Destination MAC address
 * @proto: Ethertype in host byte order
 * @buf: Frame payload starting from IEEE 802.1X header
 * @len: Frame payload length
 * Returns: >=0 on success, <0 on failure
 */
static int wpa_ether_send(struct wpa_supplicant *wpa_s, const u8 *dest,
			  u16 proto, const u8 *buf, size_t len)
{
	if (wpa_s->l2) {
		return l2_packet_send(wpa_s->l2, dest, proto, buf, len);
	}

	return wpa_drv_send_eapol(wpa_s, dest, proto, buf, len);
}
#endif /* IEEE8021X_EAPOL || !CONFIG_NO_WPA */


#ifdef IEEE8021X_EAPOL
/**
 * wpa_supplicant_eapol_send - Send IEEE 802.1X EAPOL packet to Authenticator
 * @ctx: Pointer to wpa_supplicant data (wpa_s)
 * @type: IEEE 802.1X packet type (IEEE802_1X_TYPE_*)
 * @buf: EAPOL payload (after IEEE 802.1X header)
 * @len: EAPOL payload length
 * Returns: >=0 on success, <0 on failure
 *
 * This function adds Ethernet and IEEE 802.1X header and sends the EAPOL frame
 * to the current Authenticator.
 */
static int wpa_supplicant_eapol_send(void *ctx, int type, const u8 *buf,
				     size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	u8 *msg, *dst, bssid[ETH_ALEN];
	size_t msglen;
	int res;

	/* TODO: could add l2_packet_sendmsg that allows fragments to avoid
	 * extra copy here */

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_PSK ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_NONE) {
		/* Current SSID is not using IEEE 802.1X/EAP, so drop possible
		 * EAPOL frames (mainly, EAPOL-Start) from EAPOL state
		 * machines. */
		wpa_printf(MSG_DEBUG, "WPA: drop TX EAPOL in non-IEEE 802.1X "
			   "mode (type=%d len=%lu)", type,
			   (unsigned long) len);
		return -1;
	}
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK) {
		/* Current SSID is not using WAPI Certificate authentication, so drop possible
		 * WAPI frames from WAPI state machines. */
		wpa_printf(MSG_DEBUG, "WAPI: drop TX WAPI packet in non-WAPI-Cert "
			   "mode (type=%d len=%lu)", type,
			   (unsigned long) len);
		return -1;
	}
#endif

	if (pmksa_cache_get_current(wpa_s->wpa) &&
	    type == IEEE802_1X_TYPE_EAPOL_START) {
		/* Trying to use PMKSA caching - do not send EAPOL-Start frames
		 * since they will trigger full EAPOL authentication. */
		wpa_printf(MSG_DEBUG, "RSN: PMKSA caching - do not send "
			   "EAPOL-Start");
		return -1;
	}

	if (os_memcmp(wpa_s->bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0)
	{
		wpa_printf(MSG_DEBUG, "BSSID not set when trying to send an "
			   "EAPOL frame");
		if (wpa_drv_get_bssid(wpa_s, bssid) == 0 &&
		    os_memcmp(bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) !=
		    0) {
			dst = bssid;
			wpa_printf(MSG_DEBUG, "Using current BSSID " MACSTR
				   " from the driver as the EAPOL destination",
				   MAC2STR(dst));
		} else {
			dst = wpa_s->last_eapol_src;
			wpa_printf(MSG_DEBUG, "Using the source address of the"
				   " last received EAPOL frame " MACSTR " as "
				   "the EAPOL destination",
				   MAC2STR(dst));
		}
	} else {
		/* BSSID was already set (from (Re)Assoc event, so use it as
		 * the EAPOL destination. */
		dst = wpa_s->bssid;
	}

	/* FIXME add correct subtype */
	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK ||
            wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
		msg = wapi_alloc_packet(wpa_s, type, buf, len, &msglen, NULL);
	else
		msg = wpa_alloc_eapol(wpa_s, type, buf, len, &msglen, NULL);
	
	if (msg == NULL)
		return -1;

	wpa_hexdump(MSG_MSGDUMP, "TX MESSAGE", msg, msglen);

	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK ||
	    wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
	  res = wpa_ether_send(wpa_s, dst, ETH_P_WAPI, msg, msglen);
	else
	res = wpa_ether_send(wpa_s, dst, ETH_P_EAPOL, msg, msglen);
	  
	os_free(msg);
	return res;
}


/**
 * wpa_eapol_set_wep_key - set WEP key for the driver
 * @ctx: Pointer to wpa_supplicant data (wpa_s)
 * @unicast: 1 = individual unicast key, 0 = broadcast key
 * @keyidx: WEP key index (0..3)
 * @key: Pointer to key data
 * @keylen: Key length in bytes
 * Returns: 0 on success or < 0 on error.
 */
static int wpa_eapol_set_wep_key(void *ctx, int unicast, int keyidx,
				 const u8 *key, size_t keylen)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		int cipher = (keylen == 5) ? WPA_CIPHER_WEP40 :
			WPA_CIPHER_WEP104;
		if (unicast)
			wpa_s->pairwise_cipher = cipher;
		else
			wpa_s->group_cipher = cipher;
	}
	return wpa_drv_set_key(wpa_s, WPA_ALG_WEP,
			       unicast ? wpa_s->bssid :
			       (u8 *) "\xff\xff\xff\xff\xff\xff",
			       keyidx, unicast, (u8 *) "", 0, key, keylen);
}


static void wpa_supplicant_aborted_cached(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_sm_aborted_cached(wpa_s->wpa);
}

#endif /* IEEE8021X_EAPOL */


#if defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA)
static void wpa_supplicant_set_config_blob(void *ctx,
					   struct wpa_config_blob *blob)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_config_set_blob(wpa_s->conf, blob);
}


static const struct wpa_config_blob *
wpa_supplicant_get_config_blob(void *ctx, const char *name)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_config_get_blob(wpa_s->conf, name);
}
#endif /* defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA) */


/* Configure default/group WEP key for static WEP */
static int wpa_set_wep_key(void *ctx, int set_tx, int keyidx, const u8 *key,
			   size_t keylen)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_drv_set_key(wpa_s, WPA_ALG_WEP,
			       (u8 *) "\xff\xff\xff\xff\xff\xff",
			       keyidx, set_tx, (u8 *) "", 0, key, keylen);
}


static int wpa_supplicant_set_wpa_none_key(struct wpa_supplicant *wpa_s,
					   struct wpa_ssid *ssid)
{
	u8 key[32];
	size_t keylen;
	wpa_alg alg;
	u8 seq[6] = { 0 };

	/* IBSS/WPA-None uses only one key (Group) for both receiving and
	 * sending unicast and multicast packets. */

	if (ssid->mode != IEEE80211_MODE_IBSS) {
		wpa_printf(MSG_INFO, "WPA: Invalid mode %d (not IBSS/ad-hoc) "
			   "for WPA-None", ssid->mode);
		return -1;
	}

	if (!ssid->psk_set) {
		wpa_printf(MSG_INFO, "WPA: No PSK configured for WPA-None");
		return -1;
	}

	switch (wpa_s->group_cipher) {
	case WPA_CIPHER_CCMP:
		os_memcpy(key, ssid->psk, 16);
		keylen = 16;
		alg = WPA_ALG_CCMP;
		break;
	case WPA_CIPHER_TKIP:
		/* WPA-None uses the same Michael MIC key for both TX and RX */
		os_memcpy(key, ssid->psk, 16 + 8);
		os_memcpy(key + 16 + 8, ssid->psk + 16, 8);
		keylen = 32;
		alg = WPA_ALG_TKIP;
		break;
	default:
		wpa_printf(MSG_INFO, "WPA: Invalid group cipher %d for "
			   "WPA-None", wpa_s->group_cipher);
		return -1;
	}

	/* TODO: should actually remember the previously used seq#, both for TX
	 * and RX from each STA.. */

	return wpa_drv_set_key(wpa_s, alg, (u8 *) "\xff\xff\xff\xff\xff\xff",
			       0, 1, seq, 6, key, keylen);
}


#ifdef IEEE8021X_EAPOL
static void wpa_supplicant_notify_eapol_done(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_msg(wpa_s, MSG_DEBUG, "WPA: EAPOL processing complete");
        
	if ( (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)) {
		wpa_supplicant_set_state(wpa_s, WPA_4WAY_HANDSHAKE);
	} else {
		eloop_cancel_timeout(wpa_supplicant_scan, wpa_s, NULL);
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	}
}
#endif /* IEEE8021X_EAPOL */


/**
 * wpa_blacklist_get - Get the blacklist entry for a BSSID
 * @wpa_s: Pointer to wpa_supplicant data
 * @bssid: BSSID
 * Returns: Matching blacklist entry for the BSSID or %NULL if not found
 */
struct wpa_blacklist * wpa_blacklist_get(struct wpa_supplicant *wpa_s,
					 const u8 *bssid)
{
	struct wpa_blacklist *e;

	e = wpa_s->blacklist;
	while (e) {
		if (os_memcmp(e->bssid, bssid, ETH_ALEN) == 0)
			return e;
		e = e->next;
	}

	return NULL;
}


/**
 * wpa_blacklist_add - Add an BSSID to the blacklist
 * @wpa_s: Pointer to wpa_supplicant data
 * @bssid: BSSID to be added to the blacklist
 * Returns: 0 on success, -1 on failure
 *
 * This function adds the specified BSSID to the blacklist or increases the
 * blacklist count if the BSSID was already listed. It should be called when
 * an association attempt fails either due to the selected BSS rejecting
 * association or due to timeout.
 *
 * This blacklist is used to force %wpa_supplicant to go through all available
 * BSSes before retrying to associate with an BSS that rejected or timed out
 * association. It does not prevent the listed BSS from being used; it only
 * changes the order in which they are tried.
 */
int wpa_blacklist_add(struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	struct wpa_blacklist *e;

	e = wpa_blacklist_get(wpa_s, bssid);
	if (e) {
		e->count++;
		wpa_printf(MSG_DEBUG, "BSSID " MACSTR " blacklist count "
			   "incremented to %d",
			   MAC2STR(bssid), e->count);
		return 0;
	}

	e = os_zalloc(sizeof(*e));
	if (e == NULL)
		return -1;
	os_memcpy(e->bssid, bssid, ETH_ALEN);
	e->count = 1;
	e->next = wpa_s->blacklist;
	wpa_s->blacklist = e;
	wpa_printf(MSG_DEBUG, "Added BSSID " MACSTR " into blacklist",
		   MAC2STR(bssid));

	return 0;
}


static int wpa_blacklist_del(struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	struct wpa_blacklist *e, *prev = NULL;

	e = wpa_s->blacklist;
	while (e) {
		if (os_memcmp(e->bssid, bssid, ETH_ALEN) == 0) {
			if (prev == NULL) {
				wpa_s->blacklist = e->next;
			} else {
				prev->next = e->next;
			}
			wpa_printf(MSG_DEBUG, "Removed BSSID " MACSTR " from "
				   "blacklist", MAC2STR(bssid));
			os_free(e);
			return 0;
		}
		prev = e;
		e = e->next;
	}
	return -1;
}


/**
 * wpa_blacklist_clear - Clear the blacklist of all entries
 * @wpa_s: Pointer to wpa_supplicant data
 */
void wpa_blacklist_clear(struct wpa_supplicant *wpa_s)
{
	struct wpa_blacklist *e, *prev;

	e = wpa_s->blacklist;
	wpa_s->blacklist = NULL;
	while (e) {
		prev = e;
		e = e->next;
		wpa_printf(MSG_DEBUG, "Removed BSSID " MACSTR " from "
			   "blacklist (clear)", MAC2STR(prev->bssid));
		os_free(prev);
	}
}


/**
 * wpa_supplicant_req_scan - Schedule a scan for neighboring access points
 * @wpa_s: Pointer to wpa_supplicant data
 * @sec: Number of seconds after which to scan
 * @usec: Number of microseconds after which to scan
 *
 * This function is used to schedule a scan for neighboring access points after
 * the specified time.
 */
void wpa_supplicant_req_scan(struct wpa_supplicant *wpa_s, int sec, int usec)
{
	wpa_msg(wpa_s, MSG_DEBUG, "Setting scan request: %d sec %d usec",
		sec, usec);
	eloop_cancel_timeout(wpa_supplicant_scan, wpa_s, NULL);
	eloop_register_timeout(sec, usec, wpa_supplicant_scan, wpa_s, NULL);
}


/**
 * wpa_supplicant_cancel_scan - Cancel a scheduled scan request
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is used to cancel a scan request scheduled with
 * wpa_supplicant_req_scan().
 */
void wpa_supplicant_cancel_scan(struct wpa_supplicant *wpa_s)
{
	wpa_msg(wpa_s, MSG_DEBUG, "Cancelling scan request");
	eloop_cancel_timeout(wpa_supplicant_scan, wpa_s, NULL);
}


static void wpa_supplicant_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	const u8 *bssid = wpa_s->bssid;
	if (os_memcmp(bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0)
		bssid = wpa_s->pending_bssid;
	wpa_msg(wpa_s, MSG_INFO, "Authentication with " MACSTR " timed out.",
		MAC2STR(bssid));
	wpa_blacklist_add(wpa_s, bssid);
	
	if (wpa_s->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_notify_disassoc(wpa_s->wapi);
	else
	wpa_sm_notify_disassoc(wpa_s->wpa);
	wpa_supplicant_disassociate(wpa_s, REASON_DEAUTH_LEAVING);
	wpa_s->reassociate = 1;
	wpa_supplicant_req_scan(wpa_s, 0, 0);
}


/**
 * wpa_supplicant_req_auth_timeout - Schedule a timeout for authentication
 * @wpa_s: Pointer to wpa_supplicant data
 * @sec: Number of seconds after which to time out authentication
 * @usec: Number of microseconds after which to time out authentication
 *
 * This function is used to schedule a timeout for the current authentication
 * attempt.
 */
void wpa_supplicant_req_auth_timeout(struct wpa_supplicant *wpa_s,
				     int sec, int usec)
{
	if (wpa_s->conf && wpa_s->conf->ap_scan == 0 &&
	    wpa_s->driver && os_strcmp(wpa_s->driver->name, "wired") == 0)
		return;

	wpa_msg(wpa_s, MSG_DEBUG, "Setting authentication timeout: %d sec "
		"%d usec", sec, usec);
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);
	eloop_register_timeout(sec, usec, wpa_supplicant_timeout, wpa_s, NULL);
}


/**
 * wpa_supplicant_cancel_auth_timeout - Cancel authentication timeout
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is used to cancel authentication timeout scheduled with
 * wpa_supplicant_req_auth_timeout() and it is called when authentication has
 * been completed.
 */
void wpa_supplicant_cancel_auth_timeout(struct wpa_supplicant *wpa_s)
{
	wpa_msg(wpa_s, MSG_DEBUG, "Cancelling authentication timeout");
	eloop_cancel_timeout(wpa_supplicant_timeout, wpa_s, NULL);
	wpa_blacklist_del(wpa_s, wpa_s->bssid);
}


/**
 * wpa_supplicant_initiate_eapol - Configure EAPOL state machine
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is used to configure EAPOL state machine based on the selected
 * authentication mode.
 */
void wpa_supplicant_initiate_eapol(struct wpa_supplicant *wpa_s)
{
#ifdef IEEE8021X_EAPOL
	struct eapol_config eapol_conf;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if ( (wpa_s->key_mgmt == WPA_KEY_MGMT_PSK) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK)) {
		eapol_sm_notify_eap_success(wpa_s->eapol, FALSE);
		eapol_sm_notify_eap_fail(wpa_s->eapol, FALSE);
	}
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE)
		eapol_sm_notify_portControl(wpa_s->eapol, ForceAuthorized);
	else
		eapol_sm_notify_portControl(wpa_s->eapol, Auto);

	os_memset(&eapol_conf, 0, sizeof(eapol_conf));
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		eapol_conf.accept_802_1x_keys = 1;
		eapol_conf.required_keys = 0;
		if (ssid->eapol_flags & EAPOL_FLAG_REQUIRE_KEY_UNICAST) {
			eapol_conf.required_keys |= EAPOL_REQUIRE_KEY_UNICAST;
		}
		if (ssid->eapol_flags & EAPOL_FLAG_REQUIRE_KEY_BROADCAST) {
			eapol_conf.required_keys |=
				EAPOL_REQUIRE_KEY_BROADCAST;
		}

		if (wpa_s->conf && wpa_s->driver &&
		    os_strcmp(wpa_s->driver->name, "wired") == 0) {
			eapol_conf.required_keys = 0;
		}
	}
	if (wpa_s->conf)
		eapol_conf.fast_reauth = wpa_s->conf->fast_reauth;
	eapol_conf.workaround = ssid->eap_workaround;
	
	/* FIXME-> need to distiguish wapi and eap protocols?*/
	eapol_conf.eap_disabled = wpa_s->key_mgmt != WPA_KEY_MGMT_IEEE8021X &&
		wpa_s->key_mgmt != WAPI_KEY_MGMT_CERT &&
		wpa_s->key_mgmt != WPA_KEY_MGMT_IEEE8021X_NO_WPA;
	eapol_sm_notify_config(wpa_s->eapol, ssid, &eapol_conf);
#endif /* IEEE8021X_EAPOL */
}


/**
 * wpa_supplicant_set_non_wpa_policy - Set WPA parameters to non-WPA mode
 * @wpa_s: Pointer to wpa_supplicant data
 * @ssid: Configuration data for the network
 *
 * This function is used to configure WPA state machine and related parameters
 * to a mode where WPA is not enabled. This is called as part of the
 * authentication configuration when the selected network does not use WPA.
 */
void wpa_supplicant_set_non_wpa_policy(struct wpa_supplicant *wpa_s,
				       struct wpa_ssid *ssid)
{
	int i;

	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA)
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X_NO_WPA;
	else
		wpa_s->key_mgmt = WPA_KEY_MGMT_NONE;
	wpa_sm_set_ap_wpa_ie(wpa_s->wpa, NULL, 0);
	wpa_sm_set_ap_rsn_ie(wpa_s->wpa, NULL, 0);
	wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);
	wpa_s->pairwise_cipher = WPA_CIPHER_NONE;
	wpa_s->group_cipher = WPA_CIPHER_NONE;
	wpa_s->mgmt_group_cipher = 0;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i] > 5) {
			wpa_s->pairwise_cipher = WPA_CIPHER_WEP104;
			wpa_s->group_cipher = WPA_CIPHER_WEP104;
			break;
		} else if (ssid->wep_key_len[i] > 0) {
			wpa_s->pairwise_cipher = WPA_CIPHER_WEP40;
			wpa_s->group_cipher = WPA_CIPHER_WEP40;
			break;
		}
	}

	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_KEY_MGMT, wpa_s->key_mgmt);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PAIRWISE,
			 wpa_s->pairwise_cipher);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_GROUP, wpa_s->group_cipher);
#ifdef CONFIG_IEEE80211W
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_MGMT_GROUP,
			 wpa_s->mgmt_group_cipher);
#endif /* CONFIG_IEEE80211W */

	pmksa_cache_clear_current(wpa_s->wpa);
}


static void wpa_supplicant_cleanup(struct wpa_supplicant *wpa_s)
{
#if (EAP_SIM == 1)
	scard_deinit(wpa_s->scard);
	wpa_s->scard = NULL;
	wpa_sm_set_scard_ctx(wpa_s->wpa, NULL);
	eapol_sm_register_scard_ctx(wpa_s->eapol, NULL);
#endif
	l2_packet_deinit(wpa_s->l2);
	wpa_s->l2 = NULL;
	if (wpa_s->l2_br) {
		l2_packet_deinit(wpa_s->l2_br);
		wpa_s->l2_br = NULL;
	}

	if (wpa_s->ctrl_iface) {
		wpa_supplicant_ctrl_iface_deinit(wpa_s->ctrl_iface);
		wpa_s->ctrl_iface = NULL;
	}
	if (wpa_s->conf != NULL) {
		wpa_config_free(wpa_s->conf);
		wpa_s->conf = NULL;
	}

	os_free(wpa_s->confname);
	wpa_s->confname = NULL;

	if ( (wpa_s->key_mgmt == WPA_KEY_MGMT_PSK) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK))
		wapi_sm_set_eapol(wpa_s->wapi, NULL);
	else
		wpa_sm_set_eapol(wpa_s->wpa, NULL);
	eapol_sm_deinit(wpa_s->eapol);
	wpa_s->eapol = NULL;

	rsn_preauth_deinit(wpa_s->wpa);

	pmksa_candidate_free(wpa_s->wpa);

	wapi_sm_deinit(wpa_s->wapi);
	wpa_s->wapi = NULL;

	wpa_sm_deinit(wpa_s->wpa);
	wpa_s->wpa = NULL;
	wpa_blacklist_clear(wpa_s);

	os_free(wpa_s->scan_results);
	wpa_s->scan_results = NULL;
	wpa_s->num_scan_results = 0;

	wpa_supplicant_cancel_scan(wpa_s);
	wpa_supplicant_cancel_auth_timeout(wpa_s);

	ieee80211_sta_deinit(wpa_s);
}


/**
 * wpa_clear_keys - Clear keys configured for the driver
 * @wpa_s: Pointer to wpa_supplicant data
 * @addr: Previously used BSSID or %NULL if not available
 *
 * This function clears the encryption keys that has been previously configured
 * for the driver.
 */
void wpa_clear_keys(struct wpa_supplicant *wpa_s, const u8 *addr)
{
	u8 *bcast = (u8 *) "\xff\xff\xff\xff\xff\xff";

	if (wpa_s->keys_cleared) {
		/* Some drivers (e.g., ndiswrapper & NDIS drivers) seem to have
		 * timing issues with keys being cleared just before new keys
		 * are set or just after association or something similar. This
		 * shows up in group key handshake failing often because of the
		 * client not receiving the first encrypted packets correctly.
		 * Skipping some of the extra key clearing steps seems to help
		 * in completing group key handshake more reliably. */
		wpa_printf(MSG_DEBUG, "No keys have been configured - "
			   "skip key clearing");
		return;
	}

	/* MLME-DELETEKEYS.request */
	wpa_drv_set_key(wpa_s, WPA_ALG_NONE, bcast, 0, 0, NULL, 0, NULL, 0);
	wpa_drv_set_key(wpa_s, WPA_ALG_NONE, bcast, 1, 0, NULL, 0, NULL, 0);
	wpa_drv_set_key(wpa_s, WPA_ALG_NONE, bcast, 2, 0, NULL, 0, NULL, 0);
	wpa_drv_set_key(wpa_s, WPA_ALG_NONE, bcast, 3, 0, NULL, 0, NULL, 0);
	if (addr) {
		wpa_drv_set_key(wpa_s, WPA_ALG_NONE, addr, 0, 0, NULL, 0, NULL,
				0);
		/* MLME-SETPROTECTION.request(None) */
		wpa_drv_mlme_setprotection(
			wpa_s, addr,
			MLME_SETPROTECTION_PROTECT_TYPE_NONE,
			MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);
	}
	wpa_s->keys_cleared = 1;
}


/**
 * wpa_supplicant_state_txt - Get the connection state name as a text string
 * @state: State (wpa_state; WPA_*)
 * Returns: The state name as a printable text string
 */
const char * wpa_supplicant_state_txt(int state)
{
	switch (state) {
	case WPA_DISCONNECTED:
		return "DISCONNECTED";
	case WPA_INACTIVE:
		return "INACTIVE";
	case WPA_SCANNING:
		return "SCANNING";
	case WPA_ASSOCIATING:
		return "ASSOCIATING";
	case WPA_ASSOCIATED:
		return "ASSOCIATED";
	case WPA_4WAY_HANDSHAKE:
		return "4WAY_HANDSHAKE";
	case WPA_GROUP_HANDSHAKE:
		return "GROUP_HANDSHAKE";
	case WPA_COMPLETED:
		return "COMPLETED";
	default:
		return "UNKNOWN";
	}
}


/**
 * wpa_supplicant_set_state - Set current connection state
 * @wpa_s: Pointer to wpa_supplicant data
 * @state: The new connection state
 *
 * This function is called whenever the connection state changes, e.g.,
 * association is completed for WPA/WPA2 4-Way Handshake is started.
 */
void wpa_supplicant_set_state(struct wpa_supplicant *wpa_s, wpa_states state)
{
	wpa_printf(MSG_DEBUG, "State: %s -> %s",
		   wpa_supplicant_state_txt(wpa_s->wpa_state),
		   wpa_supplicant_state_txt(state));

	wpa_supplicant_dbus_notify_state_change(wpa_s, state,
						wpa_s->wpa_state);

/**Nanoradio start**/
	wpa_drv_notify_state_change(wpa_s, state, wpa_s->wpa_state);
/**Nanoradio end**/

	if (state == WPA_COMPLETED && wpa_s->new_connection) {
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
		struct wpa_ssid *ssid = wpa_s->current_ssid;
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_CONNECTED "- Connection to "
			MACSTR " completed %s [id=%d id_str=%s]",
			MAC2STR(wpa_s->bssid), wpa_s->reassociated_connection ?
			"(reauth)" : "(auth)",
			ssid ? ssid->id : -1,
			ssid && ssid->id_str ? ssid->id_str : "");
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
		wpa_s->new_connection = 0;
		wpa_s->reassociated_connection = 1;
		wpa_drv_set_operstate(wpa_s, 1);
		/*WiFiEngine_DhcpReady();*/
	} else if (state == WPA_DISCONNECTED || state == WPA_ASSOCIATING ||
		   state == WPA_ASSOCIATED) {
		wpa_s->new_connection = 1;
		wpa_drv_set_operstate(wpa_s, 0);
	}
	wpa_s->wpa_state = state;
}


/**
 * wpa_supplicant_get_state - Get the connection state
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: The current connection state (WPA_*)
 */
wpa_states wpa_supplicant_get_state(struct wpa_supplicant *wpa_s)
{
	return wpa_s->wpa_state;
}


static void wpa_supplicant_terminate(int sig, void *eloop_ctx,
				     void *signal_ctx)
{
	struct wpa_global *global = eloop_ctx;
	struct wpa_supplicant *wpa_s;
	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_TERMINATING "- signal %d "
			"received", sig);
	}
	eloop_terminate();
}


static void wpa_supplicant_clear_status(struct wpa_supplicant *wpa_s)
{
	wpa_s->pairwise_cipher = 0;
	wpa_s->group_cipher = 0;
	wpa_s->mgmt_group_cipher = 0;
	wpa_s->key_mgmt = 0;
	wpa_s->wpa_state = WPA_DISCONNECTED;
}


/**
 * wpa_supplicant_reload_configuration - Reload configuration data
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: 0 on success or -1 if configuration parsing failed
 *
 * This function can be used to request that the configuration data is reloaded
 * (e.g., after configuration file change). This function is reloading
 * configuration only for one interface, so this may need to be called multiple
 * times if %wpa_supplicant is controlling multiple interfaces and all
 * interfaces need reconfiguration.
 */
int wpa_supplicant_reload_configuration(struct wpa_supplicant *wpa_s)
{
	struct wpa_config *conf;
	int reconf_ctrl;
	if (wpa_s->confname == NULL)
		return -1;
	conf = wpa_config_read(wpa_s->confname);
	if (conf == NULL) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to parse the configuration "
			"file '%s' - exiting", wpa_s->confname);
		return -1;
	}

	reconf_ctrl = !!conf->ctrl_interface != !!wpa_s->conf->ctrl_interface
		|| (conf->ctrl_interface && wpa_s->conf->ctrl_interface &&
		    os_strcmp(conf->ctrl_interface,
			      wpa_s->conf->ctrl_interface) != 0);

	if (reconf_ctrl && wpa_s->ctrl_iface) {
		wpa_supplicant_ctrl_iface_deinit(wpa_s->ctrl_iface);
		wpa_s->ctrl_iface = NULL;
	}

	eapol_sm_invalidate_cached_session(wpa_s->eapol);
	wpa_s->current_ssid = NULL;
	/*
	 * TODO: should notify EAPOL SM about changes in opensc_engine_path,
	 * pkcs11_engine_path, pkcs11_module_path.
	 */
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_PSK) {
		/*
		 * Clear forced success to clear EAP state for next
		 * authentication.
		 */
		eapol_sm_notify_eap_success(wpa_s->eapol, FALSE);
	}
	eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
	
	if (wpa_s->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_set_config(wpa_s->wapi, NULL);
	else
	wpa_sm_set_config(wpa_s->wpa, NULL);
	wpa_sm_set_fast_reauth(wpa_s->wpa, wpa_s->conf->fast_reauth);
	rsn_preauth_deinit(wpa_s->wpa);
	wpa_config_free(wpa_s->conf);
	wpa_s->conf = conf;
	if (reconf_ctrl)
		wpa_s->ctrl_iface = wpa_supplicant_ctrl_iface_init(wpa_s);

	wpa_supplicant_clear_status(wpa_s);
	wpa_s->reassociate = 1;
	wpa_supplicant_req_scan(wpa_s, 0, 0);
	wpa_msg(wpa_s, MSG_DEBUG, "Reconfiguration completed");
	return 0;
}


static void wpa_supplicant_reconfig(int sig, void *eloop_ctx,
				    void *signal_ctx)
{
	struct wpa_global *global = eloop_ctx;
	struct wpa_supplicant *wpa_s;
	wpa_printf(MSG_DEBUG, "Signal %d received - reconfiguring", sig);
	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_supplicant_reload_configuration(wpa_s) < 0) {
			eloop_terminate();
		}
	}
}


static void wpa_supplicant_gen_assoc_event(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;
	union wpa_event_data data;

	ssid = wpa_supplicant_get_ssid(wpa_s);
	if (ssid == NULL)
		return;

	if (wpa_s->current_ssid == NULL)
		wpa_s->current_ssid = ssid;
	wpa_supplicant_initiate_eapol(wpa_s);
	wpa_printf(MSG_DEBUG, "Already associated with a configured network - "
		   "generating associated event");
	os_memset(&data, 0, sizeof(data));
	wpa_supplicant_event(wpa_s, EVENT_ASSOC, &data);
}


static void wpa_supplicant_scan(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	struct wpa_ssid *ssid;
	int enabled, scan_req = 0, ret;

	if (wpa_s->disconnected && !wpa_s->scan_req)
		return;

	enabled = 0;
	ssid = wpa_s->conf->ssid;
	while (ssid) {
		if (!ssid->disabled) {
			enabled++;
			break;
		}
		ssid = ssid->next;
	}
	if (!enabled && !wpa_s->scan_req) {
		wpa_printf(MSG_DEBUG, "No enabled networks - do not scan");
		wpa_supplicant_set_state(wpa_s, WPA_INACTIVE);
		return;
	}
	scan_req = wpa_s->scan_req;
	wpa_s->scan_req = 0;

	if (wpa_s->conf->ap_scan != 0 &&
	    wpa_s->driver && os_strcmp(wpa_s->driver->name, "wired") == 0) {
		wpa_printf(MSG_DEBUG, "Using wired driver - overriding "
			   "ap_scan configuration");
		wpa_s->conf->ap_scan = 0;
	}

	if (wpa_s->conf->ap_scan == 0) {
		wpa_supplicant_gen_assoc_event(wpa_s);
		return;
	}

	if (wpa_s->wpa_state == WPA_DISCONNECTED ||
	    wpa_s->wpa_state == WPA_INACTIVE)
		wpa_supplicant_set_state(wpa_s, WPA_SCANNING);

	ssid = wpa_s->conf->ssid;
	if (wpa_s->prev_scan_ssid != BROADCAST_SSID_SCAN) {
		while (ssid) {
			if (ssid == wpa_s->prev_scan_ssid) {
				ssid = ssid->next;
				break;
			}
			ssid = ssid->next;
		}
	}
	while (ssid) {
		if (!ssid->disabled &&
		    (ssid->scan_ssid || wpa_s->conf->ap_scan == 2))
			break;
		ssid = ssid->next;
	}

	if (scan_req != 2 && wpa_s->conf->ap_scan == 2) {
		/*
		 * ap_scan=2 mode - try to associate with each SSID instead of
		 * scanning for each scan_ssid=1 network.
		 */
		if (ssid == NULL) {
			wpa_printf(MSG_DEBUG, "wpa_supplicant_scan: Reached "
				   "end of scan list - go back to beginning");
			wpa_s->prev_scan_ssid = BROADCAST_SSID_SCAN;
			wpa_supplicant_req_scan(wpa_s, 0, 0);
			return;
		}
		if (ssid->next) {
			/* Continue from the next SSID on the next attempt. */
			wpa_s->prev_scan_ssid = ssid;
		} else {
			/* Start from the beginning of the SSID list. */
			wpa_s->prev_scan_ssid = BROADCAST_SSID_SCAN;
		}
		wpa_supplicant_associate(wpa_s, NULL, ssid);
		return;
	}

	wpa_printf(MSG_DEBUG, "Starting AP scan (%s SSID)",
		   ssid ? "specific": "broadcast");
	if (ssid) {
		wpa_hexdump_ascii(MSG_DEBUG, "Scan SSID",
				  ssid->ssid, ssid->ssid_len);
		wpa_s->prev_scan_ssid = ssid;
	} else
		wpa_s->prev_scan_ssid = BROADCAST_SSID_SCAN;

	if (wpa_s->scan_res_tried == 0 && wpa_s->conf->ap_scan == 1) {
		wpa_s->scan_res_tried++;
		wpa_printf(MSG_DEBUG, "Trying to get current scan results "
			   "first without requesting a new scan to speed up "
			   "initial association");
		wpa_supplicant_event(wpa_s, EVENT_SCAN_RESULTS, NULL);
		return;
	}

	if (wpa_s->use_client_mlme) {
		ret = ieee80211_sta_req_scan(wpa_s, ssid ? ssid->ssid : NULL,
					     ssid ? ssid->ssid_len : 0);
	} else {
		ret = wpa_drv_scan(wpa_s, ssid ? ssid->ssid : NULL,
				   ssid ? ssid->ssid_len : 0);
	}
	if (ret) {
		wpa_printf(MSG_WARNING, "Failed to initiate AP scan.");
		wpa_supplicant_req_scan(wpa_s, 10, 0);
	}
}


static wpa_cipher cipher_suite2driver(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_NONE:
		return CIPHER_NONE;
	case WPA_CIPHER_WEP40:
		return CIPHER_WEP40;
	case WPA_CIPHER_WEP104:
		return CIPHER_WEP104;
	case WPA_CIPHER_CCMP:
		return CIPHER_CCMP;
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	case WAPI_CIPHER_SMS4:
		return CIPHER_SMS4;
#endif
	case WPA_CIPHER_TKIP:
	default:
		return CIPHER_TKIP;
	}
}


static wpa_key_mgmt key_mgmt2driver(int key_mgmt)
{
	switch (key_mgmt) {
	case WPA_KEY_MGMT_NONE:
		return KEY_MGMT_NONE;
	case WPA_KEY_MGMT_IEEE8021X_NO_WPA:
		return KEY_MGMT_802_1X_NO_WPA;
	case WPA_KEY_MGMT_IEEE8021X:
		return KEY_MGMT_802_1X;
	case WPA_KEY_MGMT_WPA_NONE:
		return KEY_MGMT_WPA_NONE;
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	case WAPI_KEY_MGMT_PSK:
		return KEY_MGMT_WAPI_PSK;
	case WAPI_KEY_MGMT_CERT:
		return KEY_MGMT_WAPI_CERT;
#endif
	case WPA_KEY_MGMT_PSK:
	default:
		return KEY_MGMT_PSK;
	}
}


static int wpa_supplicant_wpa_suites_from_ai(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid,
					 struct wpa_ie_data *ie)
{
	int ret = wpa_sm_parse_own_wpa_ie(wpa_s->wpa, ie);
	if (ret) {
		if (ret == -2) {
			wpa_msg(wpa_s, MSG_INFO, "WPA: Failed to parse WPA IE "
				"from association info");
		}
		return -1;
	}

	wpa_printf(MSG_DEBUG, "WPA: Using WPA IE from AssocReq to set cipher "
		   "suites");
	if (!(ie->group_cipher & ssid->group_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled group "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->group_cipher, ssid->group_cipher);
		return -1;
	}
	if (!(ie->pairwise_cipher & ssid->pairwise_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled pairwise "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->pairwise_cipher, ssid->pairwise_cipher);
		return -1;
	}
	if (!(ie->key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver used disabled key "
			"management 0x%x (mask 0x%x) - reject",
			ie->key_mgmt, ssid->key_mgmt);
		return -1;
	}

#ifdef CONFIG_IEEE80211W
	if (!(ie->capabilities & WPA_CAPABILITY_MGMT_FRAME_PROTECTION) &&
	    ssid->ieee80211w == IEEE80211W_REQUIRED) {
		wpa_msg(wpa_s, MSG_INFO, "WPA: Driver associated with an AP "
			"that does not support management frame protection - "
			"reject");
		return -1;
	}
#endif /* CONFIG_IEEE80211W */

	return 0;
}

#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
static int wpa_supplicant_wapi_suites_from_ai(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid,
					 struct wapi_ie_data *ie)
{
	int ret = wapi_sm_parse_own_ie(wpa_s->wapi, ie);
	if (ret) {
		if (ret == -2) {
			wpa_msg(wpa_s, MSG_INFO, "WAPI: Failed to parse WAPI IE "
				"from association info");
		}
		return -1;
	}

	wpa_printf(MSG_DEBUG, "WAPI: Using WAPI IE from AssocReq to set cipher "
		   "suites");
	if (!(ie->group_cipher & ssid->group_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WAPI: Driver used disabled group "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->group_cipher, ssid->group_cipher);
		return -1;
	}
	if (!(ie->pairwise_cipher & ssid->pairwise_cipher)) {
		wpa_msg(wpa_s, MSG_INFO, "WAPI: Driver used disabled pairwise "
			"cipher 0x%x (mask 0x%x) - reject",
			ie->pairwise_cipher, ssid->pairwise_cipher);
		return -1;
	}
	if (!(ie->key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_INFO, "WAPI: Driver used disabled key "
			"management 0x%x (mask 0x%x) - reject",
			ie->key_mgmt, ssid->key_mgmt);
		return -1;
	}

	return 0;
}
#endif

/**
 * wpa_supplicant_wpa_set_suites - Set authentication and encryption parameters
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Scan results for the selected BSS, or %NULL if not available
 * @ssid: Configuration data for the selected network
 * @wpa_ie: Buffer for the WPA/RSN IE
 * @wpa_ie_len: Maximum wpa_ie buffer size on input. This is changed to be the
 * used buffer length in case the functions returns success.
 * Returns: 0 on success or -1 on failure
 *
 * This function is used to configure authentication and encryption parameters
 * based on the network configuration and scan result for the selected BSS (if
 * available).
 */
int wpa_supplicant_wpa_set_suites(struct wpa_supplicant *wpa_s,
			      struct wpa_scan_result *bss,
			      struct wpa_ssid *ssid,
			      u8 *wpa_ie, size_t *wpa_ie_len)
{
	struct wpa_ie_data ie;
	int sel, proto;

	if (bss && bss->rsn_ie_len && (ssid->proto & WPA_PROTO_RSN) &&
	    wpa_parse_wpa_ie(bss->rsn_ie, bss->rsn_ie_len, &ie) == 0 &&
	    (ie.group_cipher & ssid->group_cipher) &&
	    (ie.pairwise_cipher & ssid->pairwise_cipher) &&
	    (ie.key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_DEBUG, "RSN: using IEEE 802.11i/D9.0");
		proto = WPA_PROTO_RSN;
	} else if (bss && bss->wpa_ie_len && (ssid->proto & WPA_PROTO_WPA) &&
		   wpa_parse_wpa_ie(bss->wpa_ie, bss->wpa_ie_len, &ie) == 0 &&
		   (ie.group_cipher & ssid->group_cipher) &&
		   (ie.pairwise_cipher & ssid->pairwise_cipher) &&
		   (ie.key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using IEEE 802.11i/D3.0");
		proto = WPA_PROTO_WPA;
	} else if (bss) {
		wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed to select WPA/RSN");
		return -1;
	} else {
		if (ssid->proto & WPA_PROTO_RSN)
			proto = WPA_PROTO_RSN;
		else
			proto = WPA_PROTO_WPA;
		if (wpa_supplicant_wpa_suites_from_ai(wpa_s, ssid, &ie) < 0) {
			os_memset(&ie, 0, sizeof(ie));
			ie.group_cipher = ssid->group_cipher;
			ie.pairwise_cipher = ssid->pairwise_cipher;
			ie.key_mgmt = ssid->key_mgmt;
#ifdef CONFIG_IEEE80211W
			ie.mgmt_group_cipher =
				ssid->ieee80211w != NO_IEEE80211W ?
				WPA_CIPHER_AES_128_CMAC : 0;
#endif /* CONFIG_IEEE80211W */
			wpa_printf(MSG_DEBUG, "WPA: Set cipher suites based "
				   "on configuration");
		} else
			proto = ie.proto;
	}

	wpa_printf(MSG_DEBUG, "WPA: Selected cipher suites: group %d "
		   "pairwise %d key_mgmt %d proto %d",
		   ie.group_cipher, ie.pairwise_cipher, ie.key_mgmt, proto);
#ifdef CONFIG_IEEE80211W
	if (ssid->ieee80211w) {
		wpa_printf(MSG_DEBUG, "WPA: Selected mgmt group cipher %d",
			   ie.mgmt_group_cipher);
	}
#endif /* CONFIG_IEEE80211W */

	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PROTO, proto);

	if (wpa_sm_set_ap_wpa_ie(wpa_s->wpa, bss ? bss->wpa_ie : NULL,
				 bss ? bss->wpa_ie_len : 0) ||
	    wpa_sm_set_ap_rsn_ie(wpa_s->wpa, bss ? bss->rsn_ie : NULL,
				 bss ? bss->rsn_ie_len : 0))
		return -1;

	sel = ie.group_cipher & ssid->group_cipher;
	if (sel & WPA_CIPHER_CCMP) {
		wpa_s->group_cipher = WPA_CIPHER_CCMP;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using GTK CCMP");
	} else if (sel & WPA_CIPHER_TKIP) {
		wpa_s->group_cipher = WPA_CIPHER_TKIP;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using GTK TKIP");
	} else if (sel & WPA_CIPHER_WEP104) {
		wpa_s->group_cipher = WPA_CIPHER_WEP104;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using GTK WEP104");
	} else if (sel & WPA_CIPHER_WEP40) {
		wpa_s->group_cipher = WPA_CIPHER_WEP40;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using GTK WEP40");
	} else {
		wpa_printf(MSG_WARNING, "WPA: Failed to select group cipher.");
		return -1;
	}

	sel = ie.pairwise_cipher & ssid->pairwise_cipher;
	if (sel & WPA_CIPHER_CCMP) {
		wpa_s->pairwise_cipher = WPA_CIPHER_CCMP;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using PTK CCMP");
	} else if (sel & WPA_CIPHER_TKIP) {
		wpa_s->pairwise_cipher = WPA_CIPHER_TKIP;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using PTK TKIP");
	} else if (sel & WPA_CIPHER_NONE) {
		wpa_s->pairwise_cipher = WPA_CIPHER_NONE;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using PTK NONE");
	} else {
		wpa_printf(MSG_WARNING, "WPA: Failed to select pairwise "
			   "cipher.");
		return -1;
	}

	sel = ie.key_mgmt & ssid->key_mgmt;
	if (sel & WPA_KEY_MGMT_IEEE8021X) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT 802.1X");
	} else if (sel & WPA_KEY_MGMT_PSK) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_PSK;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT WPA-PSK");
	} else if (sel & WPA_KEY_MGMT_WPA_NONE) {
		wpa_s->key_mgmt = WPA_KEY_MGMT_WPA_NONE;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using KEY_MGMT WPA-NONE");
	} else {
		wpa_printf(MSG_WARNING, "WPA: Failed to select authenticated "
			   "key management type.");
		return -1;
	}

	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_KEY_MGMT, wpa_s->key_mgmt);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_PAIRWISE,
			 wpa_s->pairwise_cipher);
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_GROUP, wpa_s->group_cipher);

#ifdef CONFIG_IEEE80211W
	sel = ie.mgmt_group_cipher;
	if (ssid->ieee80211w == NO_IEEE80211W ||
	    !(ie.capabilities & WPA_CAPABILITY_MGMT_FRAME_PROTECTION))
		sel = 0;
	if (sel & WPA_CIPHER_AES_128_CMAC) {
		wpa_s->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: using MGMT group cipher "
			"AES-128-CMAC");
	} else {
		wpa_s->mgmt_group_cipher = 0;
		wpa_msg(wpa_s, MSG_DEBUG, "WPA: not using MGMT group cipher");
	}
	wpa_sm_set_param(wpa_s->wpa, WPA_PARAM_MGMT_GROUP,
			 wpa_s->mgmt_group_cipher);
#endif /* CONFIG_IEEE80211W */

	if (wpa_sm_set_assoc_wpa_ie_default(wpa_s->wpa, wpa_ie, wpa_ie_len)) {
		wpa_printf(MSG_WARNING, "WPA: Failed to generate WPA IE.");
		return -1;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_PSK)
		wpa_sm_set_pmk(wpa_s->wpa, ssid->psk, PMK_LEN);
	else
		wpa_sm_set_pmk_from_pmksa(wpa_s->wpa);

	return 0;
}


/**
 * wpa_supplicant_wapi_set_suites - Set authentication and encryption parameters
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Scan results for the selected BSS, or %NULL if not available
 * @ssid: Configuration data for the selected network
 * @wapi_ie: Buffer for the WAPI IE
 * @wapi_ie_len: Maximum wapi_ie buffer size on input. This is changed to be the
 * used buffer length in case the functions returns success.
 * Returns: 0 on success or -1 on failure
 *
 * This function is used to configure authentication and encryption parameters
 * based on the network configuration and scan result for the selected BSS (if
 * available).
 */
int wpa_supplicant_wapi_set_suites(struct wpa_supplicant *wpa_s,
			      struct wpa_scan_result *bss,
			      struct wpa_ssid *ssid,
			      u8 *wapi_ie, size_t *wapi_ie_len)
{
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	struct wapi_ie_data ie;
	int sel;
	int proto = 0;
   
	if (bss && bss->wapi_ie_len && (ssid->proto & WAPI_PROTO) &&
		   wapi_parse_ie(bss->wapi_ie, bss->wapi_ie_len, &ie) == 0 &&
		   (ie.group_cipher & ssid->group_cipher) &&
		   (ie.pairwise_cipher & ssid->pairwise_cipher) &&
		   (ie.key_mgmt & ssid->key_mgmt)) {
		wpa_msg(wpa_s, MSG_DEBUG, "WAPI: using WAPI protocol");
		proto = WAPI_PROTO;
	} else if (bss) {
		wpa_msg(wpa_s, MSG_WARNING, "WAPI: Failed to select WAPI");
		return -1;
	} else {
		if (ssid->proto & WAPI_PROTO)
			proto = WAPI_PROTO;

		if (wpa_supplicant_wapi_suites_from_ai(wpa_s, ssid, &ie) < 0) {
			os_memset(&ie, 0, sizeof(ie));
			ie.group_cipher = ssid->group_cipher;
			ie.pairwise_cipher = ssid->pairwise_cipher;
			ie.key_mgmt = ssid->key_mgmt;

			wpa_printf(MSG_DEBUG, "WAPI: Set cipher suites based "
				   "on configuration");
		} else
			proto = ie.proto;
	}

	wpa_printf(MSG_DEBUG, "WAPI: Selected cipher suites: group %d "
		   "pairwise %d key_mgmt %d proto %d",
		   ie.group_cipher, ie.pairwise_cipher, ie.key_mgmt, proto);

	wapi_sm_set_param(wpa_s->wapi, WAPI_PARAM_PROTO, proto);

	if (wapi_sm_set_ap_ie(wpa_s->wapi, bss ? bss->wapi_ie : NULL,bss ? bss->wapi_ie_len : 0)) {
		return -1;
	}

	sel = ie.group_cipher & ssid->group_cipher;
	
	if (sel & WAPI_CIPHER_SMS4) {
		wpa_s->group_cipher = WAPI_CIPHER_SMS4;
		wpa_msg(wpa_s, MSG_DEBUG, "WAPI: using GTK SMS4");
	} else {
		wpa_printf(MSG_WARNING, "WAPI: Failed to select group cipher.");
		return -1;
	}

	sel = ie.pairwise_cipher & ssid->pairwise_cipher;
	if (sel & WAPI_CIPHER_SMS4) {
		wpa_s->pairwise_cipher = WAPI_CIPHER_SMS4;
		wpa_msg(wpa_s, MSG_DEBUG, "WAPI: using PTK SMS4");
	} else {
		wpa_printf(MSG_WARNING, "WAPI: Failed to select pairwise "
			   "cipher.");
		return -1;
	}

	sel = ie.key_mgmt & ssid->key_mgmt;
	if (sel & WAPI_KEY_MGMT_PSK) {
		wpa_s->key_mgmt = WAPI_KEY_MGMT_PSK;
		wpa_msg(wpa_s, MSG_DEBUG, "WAPI: using KEY_MGMT WAPI-PSK");
	} else if (sel & WAPI_KEY_MGMT_CERT) {
		wpa_s->key_mgmt = WAPI_KEY_MGMT_CERT;
		wpa_msg(wpa_s, MSG_DEBUG, "WAPI: using KEY_MGMT WAPI-CERT");
	} else {
		wpa_printf(MSG_WARNING, "WAPI: Failed to select authenticated "
			   "key management type.");
		return -1;
	}

	wapi_sm_set_param(wpa_s->wapi, WAPI_PARAM_KEY_MGMT, wpa_s->key_mgmt);
	wapi_sm_set_param(wpa_s->wapi, WAPI_PARAM_PAIRWISE,
			 wpa_s->pairwise_cipher);
	wapi_sm_set_param(wpa_s->wapi, WAPI_PARAM_GROUP, wpa_s->group_cipher);

	if (wapi_sm_set_assoc_ie_default(wpa_s->wapi, wapi_ie, wapi_ie_len)) {
		wpa_printf(MSG_WARNING, "WAPI: Failed to generate WAPI IE.");
		return -1;
	}

	//os_memcpy(wapi_ie, wpa_s->wapi_ie, wpa_s->wapi_ie_len);

	//wapi_sm_set_assoc_ie(wpa_s->wapi, wapi_ie, *wapi_ie_len);

	if (ssid->key_mgmt &  WAPI_KEY_MGMT_PSK) {
		wapi_sm_set_pmk(wpa_s->wapi, ssid->psk, 16);
	}
	//	else
	//	wpa_sm_set_pmk_from_pmksa(wpa_s->wapi);

	return 0;
#else //#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
   return -1;
#endif
}




/**
 * wpa_supplicant_associate - Request association
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Scan results for the selected BSS, or %NULL if not available
 * @ssid: Configuration data for the selected network
 *
 * This function is used to request %wpa_supplicant to associate with a BSS.
 */
void wpa_supplicant_associate(struct wpa_supplicant *wpa_s,
			      struct wpa_scan_result *bss,
			      struct wpa_ssid *ssid)
{
	u8 wpa_ie[80];
	size_t wpa_ie_len;
	int use_crypt, ret, i;
	int algs = AUTH_ALG_OPEN_SYSTEM;
	wpa_cipher cipher_pairwise, cipher_group;
	struct wpa_driver_associate_params params;
	int wep_keys_set = 0;
	struct wpa_driver_capa capa;
	int assoc_failed = 0;

	wpa_s->reassociate = 0;
	if (bss) {
		wpa_msg(wpa_s, MSG_INFO, "Trying to associate with " MACSTR
			" (SSID='%s' freq=%d MHz)", MAC2STR(bss->bssid),
			wpa_ssid_txt(bss->ssid, bss->ssid_len), bss->freq);
		os_memset(wpa_s->bssid, 0, ETH_ALEN);
		os_memcpy(wpa_s->pending_bssid, bss->bssid, ETH_ALEN);
	} else {
		wpa_msg(wpa_s, MSG_INFO, "Trying to associate with SSID '%s'",
			wpa_ssid_txt(ssid->ssid, ssid->ssid_len));
		os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
	}
	wpa_supplicant_cancel_scan(wpa_s);

	/* Starting new association, so clear the possibly used WPA IE from the
	 * previous association. */
	if (ssid->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_set_assoc_ie(wpa_s->wapi, NULL, 0);
	else
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);

#ifdef IEEE8021X_EAPOL
	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if (ssid->leap) {
			if (ssid->non_leap == 0)
				algs = AUTH_ALG_LEAP;
			else
				algs |= AUTH_ALG_LEAP;
		}
	}
#endif /* IEEE8021X_EAPOL */
	wpa_printf(MSG_DEBUG, "Automatic auth_alg selection: 0x%x", algs);
	if (ssid->auth_alg) {
		algs = 0;
		if (ssid->auth_alg & WPA_AUTH_ALG_OPEN)
			algs |= AUTH_ALG_OPEN_SYSTEM;
		if (ssid->auth_alg & WPA_AUTH_ALG_SHARED)
			algs |= AUTH_ALG_SHARED_KEY;
		if (ssid->auth_alg & WPA_AUTH_ALG_LEAP)
			algs |= AUTH_ALG_LEAP;
		wpa_printf(MSG_DEBUG, "Overriding auth_alg selection: 0x%x",
			   algs);
	}
	wpa_drv_set_auth_alg(wpa_s, algs);

	if (bss && (bss->wpa_ie_len || bss->rsn_ie_len) &&
	    (ssid->key_mgmt & (WPA_KEY_MGMT_IEEE8021X | WPA_KEY_MGMT_PSK))) {
		int try_opportunistic;
		try_opportunistic = ssid->proactive_key_caching &&
			(ssid->proto & WPA_PROTO_RSN);
		if (pmksa_cache_set_current(wpa_s->wpa, NULL, bss->bssid,
					    wpa_s->current_ssid,
					    try_opportunistic) == 0)
			eapol_sm_notify_pmkid_attempt(wpa_s->eapol, 1);
		wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_wpa_set_suites(wpa_s, bss, ssid,
					      wpa_ie, &wpa_ie_len)) {
			wpa_printf(MSG_WARNING, "WPA: Failed to set WPA key "
				   "management and encryption suites");
			return;
		}
	} else if (ssid->key_mgmt &
		   (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_IEEE8021X |
		    WPA_KEY_MGMT_WPA_NONE)) {
		wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_wpa_set_suites(wpa_s, NULL, ssid,
					      wpa_ie, &wpa_ie_len)) {
			wpa_printf(MSG_WARNING, "WPA: Failed to set WPA key "
				   "management and encryption suites (no scan "
				   "results)");
			return;
		}
	}
	else if (ssid->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT)) {
		wpa_ie_len = sizeof(wpa_ie);
		if (wpa_supplicant_wapi_set_suites(wpa_s, NULL, ssid,
					      wpa_ie, &wpa_ie_len)) {
			wpa_printf(MSG_WARNING, "WAPI: Failed to set WAPI key "
				   "management and encryption suites (no scan "
				   "results)");
			return;
		}
	} else {
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		wpa_ie_len = 0;
	}

	wpa_clear_keys(wpa_s, bss ? bss->bssid : NULL);
	use_crypt = 1;
	cipher_pairwise = cipher_suite2driver(wpa_s->pairwise_cipher);
	cipher_group = cipher_suite2driver(wpa_s->group_cipher);
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE)
			use_crypt = 0;
		for (i = 0; i < NUM_WEP_KEYS; i++) {
			if (ssid->wep_key_len[i]) {
				use_crypt = 1;
				wep_keys_set = 1;
				wpa_set_wep_key(wpa_s,
						i == ssid->wep_tx_keyidx,
						i, ssid->wep_key[i],
						ssid->wep_key_len[i]);
			}
		}
	}

#ifdef IEEE8021X_EAPOL
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if ((ssid->eapol_flags &
		     (EAPOL_FLAG_REQUIRE_KEY_UNICAST |
		      EAPOL_FLAG_REQUIRE_KEY_BROADCAST)) == 0 &&
		    !wep_keys_set) {
			use_crypt = 0;
		} else {
			/* Assume that dynamic WEP-104 keys will be used and
			 * set cipher suites in order for drivers to expect
			 * encryption. */
			cipher_pairwise = cipher_group = CIPHER_WEP104;
		}
	}
#endif /* IEEE8021X_EAPOL */

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/* Set the key before (and later after) association */
		wpa_supplicant_set_wpa_none_key(wpa_s, ssid);
	}

	wpa_drv_set_drop_unencrypted(wpa_s, use_crypt);
	wpa_supplicant_set_state(wpa_s, WPA_ASSOCIATING);
	os_memset(&params, 0, sizeof(params));
	if (bss) {
		params.bssid = bss->bssid;
		params.ssid = bss->ssid;
		params.ssid_len = bss->ssid_len;
		params.freq = bss->freq;
	} else {
		params.ssid = ssid->ssid;
		params.ssid_len = ssid->ssid_len;
	}
	if (ssid->mode == 1 && ssid->frequency > 0 && params.freq == 0)
		params.freq = ssid->frequency; /* Initial channel for IBSS */
	params.wpa_ie = wpa_ie;
	params.wpa_ie_len = wpa_ie_len;
	params.pairwise_suite = cipher_pairwise;
	params.group_suite = cipher_group;
	params.key_mgmt_suite = key_mgmt2driver(wpa_s->key_mgmt);
	params.auth_alg = algs;
	params.mode = ssid->mode;
	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i])
			params.wep_key[i] = ssid->wep_key[i];
		params.wep_key_len[i] = ssid->wep_key_len[i];
	}
	params.wep_tx_keyidx = ssid->wep_tx_keyidx;

#ifdef CONFIG_IEEE80211W
	switch (ssid->ieee80211w) {
	case NO_IEEE80211W:
		params.mgmt_frame_protection = NO_MGMT_FRAME_PROTECTION;
		break;
	case IEEE80211W_OPTIONAL:
		params.mgmt_frame_protection = MGMT_FRAME_PROTECTION_OPTIONAL;
		break;
	case IEEE80211W_REQUIRED:
		params.mgmt_frame_protection = MGMT_FRAME_PROTECTION_REQUIRED;
		break;
	}
#endif /* CONFIG_IEEE80211W */

	if (wpa_s->use_client_mlme)
		ret = ieee80211_sta_associate(wpa_s, &params);
	else
		ret = wpa_drv_associate(wpa_s, &params);
	if (ret < 0) {
		wpa_msg(wpa_s, MSG_INFO, "Association request to the driver "
			"failed");
		/* try to continue anyway; new association will be tried again
		 * after timeout */
		assoc_failed = 1;
	}

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPA_NONE) {
		/* Set the key after the association just in case association
		 * cleared the previously configured key. */
		wpa_supplicant_set_wpa_none_key(wpa_s, ssid);
		/* No need to timeout authentication since there is no key
		 * management. */
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	} else {
		/* Timeout for IEEE 802.11 authentication and association */
		int timeout;
		if (assoc_failed)
			timeout = 5;
		else if (wpa_s->conf->ap_scan == 1)
			timeout = 10;
		else
			timeout = 60;
		wpa_supplicant_req_auth_timeout(wpa_s, timeout, 0);
	}

	if (wep_keys_set && wpa_drv_get_capa(wpa_s, &capa) == 0 &&
	    capa.flags & WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC) {
		/* Set static WEP keys again */
		int j;
		for (j = 0; j < NUM_WEP_KEYS; j++) {
			if (ssid->wep_key_len[j]) {
				wpa_set_wep_key(wpa_s,
						j == ssid->wep_tx_keyidx,
						j, ssid->wep_key[j],
						ssid->wep_key_len[j]);
			}
		}
	}

	if (wpa_s->current_ssid && wpa_s->current_ssid != ssid) {
		/*
		 * Do not allow EAP session resumption between different
		 * network configurations.
		 */
		eapol_sm_invalidate_cached_session(wpa_s->eapol);
	}
	wpa_s->current_ssid = ssid;

	if (ssid->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_set_config(wpa_s->wapi, wpa_s->current_ssid);
	else
	wpa_sm_set_config(wpa_s->wpa, wpa_s->current_ssid);
	wpa_supplicant_initiate_eapol(wpa_s);
}


/**
 * wpa_supplicant_disassociate - Disassociate the current connection
 * @wpa_s: Pointer to wpa_supplicant data
 * @reason_code: IEEE 802.11 reason code for the disassociate frame
 *
 * This function is used to request %wpa_supplicant to disassociate with the
 * current AP.
 */
void wpa_supplicant_disassociate(struct wpa_supplicant *wpa_s,
				 int reason_code)
{
	u8 *addr = NULL;
	if (os_memcmp(wpa_s->bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) != 0)
	{
		if (wpa_s->use_client_mlme)
			ieee80211_sta_disassociate(wpa_s, reason_code);
		else
			wpa_drv_disassociate(wpa_s, wpa_s->bssid, reason_code);
		addr = wpa_s->bssid;
	}
	wpa_clear_keys(wpa_s, addr);
	wpa_supplicant_mark_disassoc(wpa_s);
	wpa_s->current_ssid = NULL;

	if (wpa_s->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_set_config(wpa_s->wapi, NULL);
	else
	wpa_sm_set_config(wpa_s->wpa, NULL);
	eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
}


/**
 * wpa_supplicant_deauthenticate - Deauthenticate the current connection
 * @wpa_s: Pointer to wpa_supplicant data
 * @reason_code: IEEE 802.11 reason code for the deauthenticate frame
 *
 * This function is used to request %wpa_supplicant to disassociate with the
 * current AP.
 */
void wpa_supplicant_deauthenticate(struct wpa_supplicant *wpa_s,
				   int reason_code)
{
	u8 *addr = NULL;
	wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
	if (os_memcmp(wpa_s->bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) != 0)
	{
		if (wpa_s->use_client_mlme)
			ieee80211_sta_deauthenticate(wpa_s, reason_code);
		else
			wpa_drv_deauthenticate(wpa_s, wpa_s->bssid,
					       reason_code);
		addr = wpa_s->bssid;
	}
	wpa_clear_keys(wpa_s, addr);
	wpa_s->current_ssid = NULL;

	if (wpa_s->key_mgmt & (WAPI_KEY_MGMT_PSK | WAPI_KEY_MGMT_CERT))
		wapi_sm_set_config(wpa_s->wapi, NULL);
	else
	wpa_sm_set_config(wpa_s->wpa, NULL);
	eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
	eapol_sm_notify_portEnabled(wpa_s->eapol, FALSE);
	eapol_sm_notify_portValid(wpa_s->eapol, FALSE);
}


/**
 * wpa_supplicant_get_scan_results - Get scan results
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: 0 on success, -1 on failure
 *
 * This function is request the current scan results from the driver and stores
 * a local copy of the results in wpa_s->scan_results.
 */
int wpa_supplicant_get_scan_results(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_DRIVER_WIFIENGINE
#define SCAN_AP_LIMIT 1
#else
#define SCAN_AP_LIMIT 128
#endif
	struct wpa_scan_result *results, *tmp;
	int num;

	results = os_malloc(SCAN_AP_LIMIT * sizeof(struct wpa_scan_result));
	if (results == NULL) {
		wpa_printf(MSG_WARNING, "Failed to allocate memory for scan "
			   "results");
		return -1;
	}

	if (wpa_s->use_client_mlme) {
		num = ieee80211_sta_get_scan_results(wpa_s, results,
						     SCAN_AP_LIMIT);
	} else
		num = wpa_drv_get_scan_results(wpa_s, results, SCAN_AP_LIMIT);
	wpa_printf(MSG_DEBUG, "Scan results: %d", num);
	if (num < 0) {
		wpa_printf(MSG_DEBUG, "Failed to get scan results");
		os_free(results);
		return -1;
	}
	if (num > SCAN_AP_LIMIT) {
		wpa_printf(MSG_INFO, "Not enough room for all APs (%d < %d)",
			   num, SCAN_AP_LIMIT);
		num = SCAN_AP_LIMIT;
	}

	/* Free unneeded memory for unused scan result entries */
	tmp = os_realloc(results, num * sizeof(struct wpa_scan_result));
	if (tmp || num == 0) {
		results = tmp;
	}

	os_free(wpa_s->scan_results);
	wpa_s->scan_results = results;
	wpa_s->num_scan_results = num;

	return 0;
}


#ifndef CONFIG_NO_WPA
static int wpa_get_beacon_ie(struct wpa_supplicant *wpa_s)
{
	int i, ret = 0;
	struct wpa_scan_result *results, *curr = NULL;

	results = wpa_s->scan_results;
	if (results == NULL) {
		return -1;
	}

	for (i = 0; i < wpa_s->num_scan_results; i++) {
		struct wpa_ssid *ssid = wpa_s->current_ssid;
		if (os_memcmp(results[i].bssid, wpa_s->bssid, ETH_ALEN) != 0)
			continue;
		if (ssid == NULL ||
		    ((results[i].ssid_len == ssid->ssid_len &&
		      os_memcmp(results[i].ssid, ssid->ssid, ssid->ssid_len)
		      == 0) ||
		     ssid->ssid_len == 0)) {
			curr = &results[i];
			break;
		}
	}

	if (curr) {
		if (wpa_sm_set_ap_wpa_ie(wpa_s->wpa, curr->wpa_ie,
					 curr->wpa_ie_len) ||
		    wpa_sm_set_ap_rsn_ie(wpa_s->wpa, curr->rsn_ie,
					 curr->rsn_ie_len) ||
		    wapi_sm_set_ap_ie(wpa_s->wapi, curr->wapi_ie,
					 curr->wapi_ie_len))
			ret = -1;
	} else {
		ret = -1;
	}

	return ret;
}


static int wpa_supplicant_get_beacon_ie(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_get_beacon_ie(wpa_s) == 0) {
		return 0;
	}

	/* No WPA/RSN IE found in the cached scan results. Try to get updated
	 * scan results from the driver. */
	if (wpa_supplicant_get_scan_results(wpa_s) < 0) {
		return -1;
	}

	return wpa_get_beacon_ie(wpa_s);
}
#endif /* CONFIG_NO_WPA */


/**
 * wpa_supplicant_get_ssid - Get a pointer to the current network structure
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: A pointer to the current network structure or %NULL on failure
 */
struct wpa_ssid * wpa_supplicant_get_ssid(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *entry;
	u8 ssid[MAX_SSID_LEN];
	int res;
	size_t ssid_len;
	u8 bssid[ETH_ALEN];
	int wired;

	if (wpa_s->use_client_mlme) {
		if (ieee80211_sta_get_ssid(wpa_s, ssid, &ssid_len)) {
			wpa_printf(MSG_WARNING, "Could not read SSID from "
				   "MLME.");
			return NULL;
		}
	} else {
		res = wpa_drv_get_ssid(wpa_s, ssid);
		if (res < 0) {
			wpa_printf(MSG_WARNING, "Could not read SSID from "
				   "driver.");
			return NULL;
		}
		ssid_len = res;
	}

	if (wpa_s->use_client_mlme)
		os_memcpy(bssid, wpa_s->bssid, ETH_ALEN);
	else if (wpa_drv_get_bssid(wpa_s, bssid) < 0) {
		wpa_printf(MSG_WARNING, "Could not read BSSID from driver.");
		return NULL;
	}

	wired = wpa_s->conf->ap_scan == 0 && wpa_s->driver &&
		os_strcmp(wpa_s->driver->name, "wired") == 0;

	entry = wpa_s->conf->ssid;
	while (entry) {
		if (!entry->disabled &&
		    ((ssid_len == entry->ssid_len &&
		      os_memcmp(ssid, entry->ssid, ssid_len) == 0) || wired) &&
		    (!entry->bssid_set ||
		     os_memcmp(bssid, entry->bssid, ETH_ALEN) == 0))
			return entry;
		entry = entry->next;
	}

	return NULL;
}


#ifndef CONFIG_NO_WPA
static u8 * _wpa_alloc_eapol(void *wpa_s, u8 type,
			     const void *data, u16 data_len,
			     size_t *msg_len, void **data_pos)
{
	return wpa_alloc_eapol(wpa_s, type, data, data_len, msg_len, data_pos);
}

#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
static u8 * _wapi_alloc_packet(void *wpa_s, u8 subtype,
                             const void *data, u16 data_len,
                             size_t *msg_len, void **data_pos)
{
        return wapi_alloc_packet(wpa_s, subtype, data, data_len, msg_len, data_pos);
}
#endif

static int _wpa_ether_send(void *wpa_s, const u8 *dest, u16 proto,
			   const u8 *buf, size_t len)
{
	return wpa_ether_send(wpa_s, dest, proto, buf, len);
}


static void _wpa_supplicant_req_scan(void *wpa_s, int sec, int usec)
{
	wpa_supplicant_req_scan(wpa_s, sec, usec);
}


static void _wpa_supplicant_cancel_auth_timeout(void *wpa_s)
{
	wpa_supplicant_cancel_auth_timeout(wpa_s);
}


static void _wpa_supplicant_set_state(void *wpa_s, wpa_states state)
{
	wpa_supplicant_set_state(wpa_s, state);
}


static wpa_states _wpa_supplicant_get_state(void *wpa_s)
{
	return wpa_supplicant_get_state(wpa_s);
}


static void _wpa_supplicant_disassociate(void *wpa_s, int reason_code)
{
	wpa_supplicant_disassociate(wpa_s, reason_code);
}


static void _wpa_supplicant_deauthenticate(void *wpa_s, int reason_code)
{
	wpa_supplicant_deauthenticate(wpa_s, reason_code);
}


static struct wpa_ssid * _wpa_supplicant_get_ssid(void *wpa_s)
{
	return wpa_supplicant_get_ssid(wpa_s);
}


static int wpa_supplicant_get_bssid(void *ctx, u8 *bssid)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->use_client_mlme) {
		os_memcpy(bssid, wpa_s->bssid, ETH_ALEN);
		return 0;
	}
	return wpa_drv_get_bssid(wpa_s, bssid);
}


static int wpa_supplicant_set_key(void *wpa_s, wpa_alg alg,
				  const u8 *addr, int key_idx, int set_tx,
				  const u8 *seq, size_t seq_len,
				  const u8 *key, size_t key_len)
{
	return wpa_drv_set_key(wpa_s, alg, addr, key_idx, set_tx, seq, seq_len,
			       key, key_len);
}


static int wpa_supplicant_mlme_setprotection(void *wpa_s, const u8 *addr,
					     int protection_type,
					     int key_type)
{
	return wpa_drv_mlme_setprotection(wpa_s, addr, protection_type,
					  key_type);
}


static int wpa_supplicant_add_pmkid(void *wpa_s,
				    const u8 *bssid, const u8 *pmkid)
{
	return wpa_drv_add_pmkid(wpa_s, bssid, pmkid);
}


static int wpa_supplicant_remove_pmkid(void *wpa_s,
				       const u8 *bssid, const u8 *pmkid)
{
	return wpa_drv_remove_pmkid(wpa_s, bssid, pmkid);
}
#endif /* CONFIG_NO_WPA */


static int wpa_supplicant_set_driver(struct wpa_supplicant *wpa_s,
				     const char *name)
{
	int i;

	if (wpa_s == NULL)
		return -1;

	if (wpa_supplicant_drivers[0] == NULL) {
		wpa_printf(MSG_ERROR, "No driver interfaces build into "
			   "wpa_supplicant.");
		return -1;
	}

	if (name == NULL) {
		/* default to first driver in the list */
		wpa_s->driver = wpa_supplicant_drivers[0];
		return 0;
	}

	for (i = 0; wpa_supplicant_drivers[i]; i++) {
		if (os_strcmp(name, wpa_supplicant_drivers[i]->name) == 0) {
			wpa_s->driver = wpa_supplicant_drivers[i];
			return 0;
		}
	}

	wpa_printf(MSG_ERROR, "Unsupported driver '%s'.\n", name);
	return -1;
}


void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr,
			     const u8 *buf, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_printf(MSG_DEBUG, "RX EAPOL from " MACSTR, MAC2STR(src_addr));
	wpa_hexdump(MSG_MSGDUMP, "RX EAPOL", buf, len);

	if (wpa_s->wpa_state == WPA_DISCONNECTED ||
            wpa_s->wpa_state == WPA_INACTIVE) {
		wpa_printf(MSG_DEBUG, "Ignored received EAPOL frame since we were not associated");
		return;
	}

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_NONE) {
		wpa_printf(MSG_DEBUG, "Ignored received EAPOL frame since "
			   "no key management is configured");
		return;
	}

	if (wpa_s->eapol_received == 0) {
		/* Timeout for completing IEEE 802.1X and WPA authentication */
		wpa_supplicant_req_auth_timeout(
			wpa_s,
			(wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X ||
			 wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA ||
			 wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT) ?
			70 : 10, 0);
	}
	wpa_s->eapol_received++;

	if (wpa_s->countermeasures) {
		wpa_printf(MSG_INFO, "WPA: Countermeasures - dropped EAPOL "
			   "packet");
		return;
	}

	/* Source address of the incoming EAPOL frame could be compared to the
	 * current BSSID. However, it is possible that a centralized
	 * Authenticator could be using another MAC address than the BSSID of
	 * an AP, so just allow any address to be used for now. The replies are
	 * still sent to the current BSSID (if available), though. */

	os_memcpy(wpa_s->last_eapol_src, src_addr, ETH_ALEN);

	if( (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK) || (wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)) {
		if (wpa_s->key_mgmt != WAPI_KEY_MGMT_PSK &&
		    eapol_sm_rx_wapi(wpa_s->eapol, src_addr, buf, len) > 0)
			return;
			} else {
	if (wpa_s->key_mgmt != WPA_KEY_MGMT_PSK &&
	    eapol_sm_rx_eapol(wpa_s->eapol, src_addr, buf, len) > 0)
		return;
	}

	wpa_drv_poll(wpa_s);

	if (wpa_s->key_mgmt == WAPI_KEY_MGMT_PSK || wpa_s->key_mgmt == WAPI_KEY_MGMT_CERT)
		wapi_sm_rx_eapol(wpa_s->wapi, src_addr, buf, len);
	else
	wpa_sm_rx_eapol(wpa_s->wpa, src_addr, buf, len);
}


/**
 * wpa_supplicant_driver_init - Initialize driver interface parameters
 * @wpa_s: Pointer to wpa_supplicant data
 * @wait_for_interface: 0 = do not wait for the interface (reports a failure if
 * the interface is not present), 1 = wait until the interface is available
 * Returns: 0 on success, -1 on failure
 *
 * This function is called to initialize driver interface parameters.
 * wpa_drv_init() must have been called before this function to initialize the
 * driver interface.
 */
int wpa_supplicant_driver_init(struct wpa_supplicant *wpa_s,
			       int wait_for_interface)
{
	static int interface_count = 0;
	static int id_inv = -1;
	struct wpa_ssid *ssid;

	for (;;) {
		if (wpa_s->driver->send_eapol) {
			const u8 *addr = wpa_drv_get_mac_addr(wpa_s);
			if (addr)
				os_memcpy(wpa_s->own_addr, addr, ETH_ALEN);
			break;
		}
		/* FIXME */
		if(!config) {
			wpa_printf(MSG_DEBUG, "ERROR: No configuration found! Aborting...");
			return -1;
		}
		ssid = wpa_config_get_network(config, id_inv);
		if (ssid && (wpa_s->conf->ssid->key_mgmt == WAPI_KEY_MGMT_PSK ||
			     wpa_s->conf->ssid->key_mgmt == WAPI_KEY_MGMT_CERT)) {
			wpa_printf(MSG_DEBUG, "Capturing WAPI protocol packets...");
			wpa_s->l2 = l2_packet_init(wpa_s->ifname,
						   wpa_drv_get_mac_addr(wpa_s),
						   ETH_P_WAPI,
						   wpa_supplicant_rx_eapol, wpa_s, 0);
		}
		else {
			wpa_printf(MSG_DEBUG, "Capturing EAPOL protocol packets...");
			wpa_s->l2 = l2_packet_init(wpa_s->ifname,
						   wpa_drv_get_mac_addr(wpa_s),
						   ETH_P_EAPOL,
						   wpa_supplicant_rx_eapol, wpa_s, 0);
		}

		if (wpa_s->l2)
			break;
		else if (!wait_for_interface)
			return -1;
		wpa_printf(MSG_DEBUG, "Waiting for interface..");
		os_sleep(5, 0);
	}

	if (wpa_s->l2 && l2_packet_get_own_addr(wpa_s->l2, wpa_s->own_addr)) {
		wpa_printf(MSG_ERROR, "Failed to get own L2 address");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "Own MAC address: " MACSTR,
		   MAC2STR(wpa_s->own_addr));

	if (wpa_s->bridge_ifname[0]) {
		wpa_printf(MSG_DEBUG, "Receiving packets from bridge interface"
			   " '%s'", wpa_s->bridge_ifname);
		wpa_s->l2_br = l2_packet_init(wpa_s->bridge_ifname,
					      wpa_s->own_addr,
					      ETH_P_EAPOL,
					      wpa_supplicant_rx_eapol, wpa_s,
					      0);
		if (wpa_s->l2_br == NULL) {
			wpa_printf(MSG_ERROR, "Failed to open l2_packet "
				   "connection for the bridge interface '%s'",
				   wpa_s->bridge_ifname);
			return -1;
		}
	}

	/* Backwards compatibility call to set_wpa() handler. This is called
	 * only just after init and just before deinit, so these handler can be
	 * used to implement same functionality. */
	if (wpa_drv_set_wpa(wpa_s, 1) < 0) {
		struct wpa_driver_capa capa;
		if (wpa_drv_get_capa(wpa_s, &capa) < 0 ||
		    !(capa.flags & (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				    WPA_DRIVER_CAPA_KEY_MGMT_WPA2))) {
			wpa_printf(MSG_DEBUG, "Driver does not support WPA.");
			/* Continue to allow non-WPA modes to be used. */
		} else {
			wpa_printf(MSG_ERROR, "Failed to enable WPA in the "
				"driver.");
			return -1;
		}
	}

	wpa_clear_keys(wpa_s, NULL);

	/* Make sure that TKIP countermeasures are not left enabled (could
	 * happen if wpa_supplicant is killed during countermeasures. */
	wpa_drv_set_countermeasures(wpa_s, 0);

	wpa_drv_set_drop_unencrypted(wpa_s, 1);

	wpa_s->prev_scan_ssid = BROADCAST_SSID_SCAN;
	if ( wpa_s->conf->ap_scan > 0) 
	{
 	   wpa_supplicant_req_scan(wpa_s, interface_count, 100000);
	}
	interface_count++;

	return 0;
}


static int wpa_supplicant_daemon(const char *pid_file)
{
	wpa_printf(MSG_DEBUG, "Daemonize..");
	return os_daemonize(pid_file);
}


static struct wpa_supplicant * wpa_supplicant_alloc(void)
{
	struct wpa_supplicant *wpa_s;

	wpa_s = os_zalloc(sizeof(*wpa_s));
	if (wpa_s == NULL)
		return NULL;
	wpa_s->scan_req = 0;

	return wpa_s;
}


static int wpa_supplicant_init_iface(struct wpa_supplicant *wpa_s,
				     struct wpa_interface *iface)
{
	wpa_printf(MSG_DEBUG, "Initializing interface '%s' conf '%s' driver "
		   "'%s' ctrl_interface '%s' bridge '%s'", iface->ifname,
		   iface->confname ? iface->confname : "N/A",
		   iface->driver ? iface->driver : "default",
		   iface->ctrl_interface ? iface->ctrl_interface : "N/A",
		   iface->bridge_ifname ? iface->bridge_ifname : "N/A");

	if (wpa_supplicant_set_driver(wpa_s, iface->driver) < 0) {
		return -1;
	}

	if (iface->confname) {
#ifdef CONFIG_BACKEND_FILE
		wpa_s->confname = os_rel2abs_path(iface->confname);
		if (wpa_s->confname == NULL) {
			wpa_printf(MSG_ERROR, "Failed to get absolute path "
				   "for configuration file '%s'.",
				   iface->confname);
			return -1;
		}
		wpa_printf(MSG_DEBUG, "Configuration file '%s' -> '%s'",
			   iface->confname, wpa_s->confname);
#else /* CONFIG_BACKEND_FILE */
		wpa_s->confname = os_strdup(iface->confname);
#endif /* CONFIG_BACKEND_FILE */

		wpa_s->conf = wpa_config_read(wpa_s->confname);

		if (wpa_s->conf == NULL) {
			wpa_printf(MSG_ERROR, "Failed to read or parse "
				   "configuration '%s'.", wpa_s->confname);
			return -1;
		}

		/*
		 * Override ctrl_interface and driver_param if set on command
		 * line.
		 */
		if (iface->ctrl_interface) {
			os_free(wpa_s->conf->ctrl_interface);
			wpa_s->conf->ctrl_interface =
				os_strdup(iface->ctrl_interface);
		}

		if (iface->driver_param) {
			os_free(wpa_s->conf->driver_param);
			wpa_s->conf->driver_param =
				os_strdup(iface->driver_param);
		}
	} else
		wpa_s->conf = wpa_config_alloc_empty(iface->ctrl_interface,
						     iface->driver_param);

	if (wpa_s->conf == NULL) {
		wpa_printf(MSG_ERROR, "\nNo configuration found.");
		return -1;
	}

	if (iface->ifname == NULL) {
		wpa_printf(MSG_ERROR, "\nInterface name is required.");
		return -1;
	}
	if (os_strlen(iface->ifname) >= sizeof(wpa_s->ifname)) {
		wpa_printf(MSG_ERROR, "\nToo long interface name '%s'.",
			   iface->ifname);
		return -1;
	}
	os_strncpy(wpa_s->ifname, iface->ifname, sizeof(wpa_s->ifname));

	if (iface->bridge_ifname) {
		if (os_strlen(iface->bridge_ifname) >=
		    sizeof(wpa_s->bridge_ifname)) {
			wpa_printf(MSG_ERROR, "\nToo long bridge interface "
				   "name '%s'.", iface->bridge_ifname);
			return -1;
		}
		os_strncpy(wpa_s->bridge_ifname, iface->bridge_ifname,
			   sizeof(wpa_s->bridge_ifname));
	}

	return 0;
}


static int wpa_supplicant_init_eapol(struct wpa_supplicant *wpa_s)
{
#ifdef IEEE8021X_EAPOL
	struct eapol_ctx *ctx;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate EAPOL context.");
		return -1;
	}

	ctx->ctx = wpa_s;
	ctx->msg_ctx = wpa_s;
	ctx->eapol_send_ctx = wpa_s;
	ctx->preauth = 0;
	ctx->eapol_done_cb = wpa_supplicant_notify_eapol_done;
	ctx->eapol_send = wpa_supplicant_eapol_send;
	ctx->set_wep_key = wpa_eapol_set_wep_key;
	ctx->set_config_blob = wpa_supplicant_set_config_blob;
	ctx->get_config_blob = wpa_supplicant_get_config_blob;
	ctx->aborted_cached = wpa_supplicant_aborted_cached;
	ctx->opensc_engine_path = wpa_s->conf->opensc_engine_path;
	ctx->pkcs11_engine_path = wpa_s->conf->pkcs11_engine_path;
	ctx->pkcs11_module_path = wpa_s->conf->pkcs11_module_path;
	wpa_s->eapol = eapol_sm_init(ctx);
	if (wpa_s->eapol == NULL) {
		os_free(ctx);
		wpa_printf(MSG_ERROR, "Failed to initialize EAPOL state "
			   "machines.");
		return -1;
	}
#endif /* IEEE8021X_EAPOL */

	return 0;
}


static int wpa_supplicant_init_wpa(struct wpa_supplicant *wpa_s)
{
#ifndef CONFIG_NO_WPA
	struct wpa_sm_ctx *ctx;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate WPA context.");
		return -1;
	}

	ctx->ctx = wpa_s;
	ctx->set_state = _wpa_supplicant_set_state;
	ctx->get_state = _wpa_supplicant_get_state;
	ctx->req_scan = _wpa_supplicant_req_scan;
	ctx->deauthenticate = _wpa_supplicant_deauthenticate;
	ctx->disassociate = _wpa_supplicant_disassociate;
	ctx->set_key = wpa_supplicant_set_key;
	ctx->scan = wpa_supplicant_scan;
	ctx->get_ssid = _wpa_supplicant_get_ssid;
	ctx->get_bssid = wpa_supplicant_get_bssid;
	ctx->ether_send = _wpa_ether_send;
	ctx->get_beacon_ie = wpa_supplicant_get_beacon_ie;
	ctx->alloc_eapol = _wpa_alloc_eapol;
	ctx->cancel_auth_timeout = _wpa_supplicant_cancel_auth_timeout;
	ctx->add_pmkid = wpa_supplicant_add_pmkid;
	ctx->remove_pmkid = wpa_supplicant_remove_pmkid;
	ctx->set_config_blob = wpa_supplicant_set_config_blob;
	ctx->get_config_blob = wpa_supplicant_get_config_blob;
	ctx->mlme_setprotection = wpa_supplicant_mlme_setprotection;

	wpa_s->wpa = wpa_sm_init(ctx);
	if (wpa_s->wpa == NULL) {
		wpa_printf(MSG_ERROR, "Failed to initialize WPA state "
			   "machine");
		return -1;
	}
#endif /* CONFIG_NO_WPA */

	return 0;
}

#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
static int wpa_supplicant_init_wapi(struct wpa_supplicant *wpa_s)
{
	struct wapi_sm_ctx *ctx;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate WAPI context.");
		return -1;
	}

	ctx->ctx = wpa_s;
	ctx->set_state = _wpa_supplicant_set_state;
	ctx->get_state = _wpa_supplicant_get_state;
	ctx->deauthenticate = _wpa_supplicant_deauthenticate;
	ctx->disassociate = _wpa_supplicant_disassociate;
	ctx->set_key = wpa_supplicant_set_key;
	ctx->scan = wpa_supplicant_scan;
	ctx->get_ssid = _wpa_supplicant_get_ssid;
	ctx->get_bssid = wpa_supplicant_get_bssid;
	ctx->ether_send = _wpa_ether_send;
	ctx->get_beacon_ie = wpa_supplicant_get_beacon_ie;
	ctx->alloc_eapol = _wapi_alloc_packet;
	ctx->cancel_auth_timeout = _wpa_supplicant_cancel_auth_timeout;
	ctx->add_pmkid = wpa_supplicant_add_pmkid;
	ctx->remove_pmkid = wpa_supplicant_remove_pmkid;
	ctx->set_config_blob = wpa_supplicant_set_config_blob;
	ctx->get_config_blob = wpa_supplicant_get_config_blob;
	ctx->mlme_setprotection = wpa_supplicant_mlme_setprotection;

	wpa_s->wapi = wapi_sm_init(ctx);
	if (wpa_s->wapi == NULL) {
		wpa_printf(MSG_ERROR, "Failed to initialize WAPI state "
			   "machine");
		return -1;
	}

	return 0;
}
#endif

static int wpa_supplicant_init_iface2(struct wpa_supplicant *wpa_s,
				      int wait_for_interface)
{
	const char *ifname;
	struct wpa_driver_capa capa;

	wpa_printf(MSG_DEBUG, "Initializing interface (2) '%s'",
		   wpa_s->ifname);

	if (wpa_supplicant_init_eapol(wpa_s) < 0)
		return -1;

	/* RSNA Supplicant Key Management - INITIALIZE */
	eapol_sm_notify_portEnabled(wpa_s->eapol, FALSE);
	eapol_sm_notify_portValid(wpa_s->eapol, FALSE);

	/* Initialize driver interface and register driver event handler before
	 * L2 receive handler so that association events are processed before
	 * EAPOL-Key packets if both become available for the same select()
	 * call. */
	wpa_s->drv_priv = wpa_drv_init(wpa_s, wpa_s->ifname);
	if (wpa_s->drv_priv == NULL) {
		wpa_printf(MSG_ERROR, "Failed to initialize driver interface");
		return -1;
	}
	if (wpa_drv_set_param(wpa_s, wpa_s->conf->driver_param) < 0) {
		wpa_printf(MSG_ERROR, "Driver interface rejected "
			   "driver_param '%s'", wpa_s->conf->driver_param);
		return -1;
	}

	ifname = wpa_drv_get_ifname(wpa_s);
	if (ifname && os_strcmp(ifname, wpa_s->ifname) != 0) {
		wpa_printf(MSG_DEBUG, "Driver interface replaced interface "
			   "name with '%s'", ifname);
		os_strncpy(wpa_s->ifname, ifname, sizeof(wpa_s->ifname));
	}

	if (wpa_supplicant_init_wpa(wpa_s) < 0)
		return -1;

#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	if (wpa_supplicant_init_wapi(wpa_s) < 0)
		return -1;

	wapi_sm_set_ifname(wpa_s->wapi, wpa_s->ifname,
			  wpa_s->bridge_ifname[0] ? wpa_s->bridge_ifname :
			  NULL);
	wapi_sm_set_eapol(wpa_s->wapi, wpa_s->eapol);
#endif
	wpa_sm_set_ifname(wpa_s->wpa, wpa_s->ifname,
			  wpa_s->bridge_ifname[0] ? wpa_s->bridge_ifname :
			  NULL);
	wpa_sm_set_fast_reauth(wpa_s->wpa, wpa_s->conf->fast_reauth);
	wpa_sm_set_eapol(wpa_s->wpa, wpa_s->eapol);

	if (wpa_s->conf->dot11RSNAConfigPMKLifetime &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_PMK_LIFETIME,
			     wpa_s->conf->dot11RSNAConfigPMKLifetime)) {
		wpa_printf(MSG_ERROR, "Invalid WPA parameter value for "
			   "dot11RSNAConfigPMKLifetime");
		return -1;
	}

	if (wpa_s->conf->dot11RSNAConfigPMKReauthThreshold &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_PMK_REAUTH_THRESHOLD,
			     wpa_s->conf->dot11RSNAConfigPMKReauthThreshold)) {
		wpa_printf(MSG_ERROR, "Invalid WPA parameter value for "
			"dot11RSNAConfigPMKReauthThreshold");
		return -1;
	}

	if (wpa_s->conf->dot11RSNAConfigSATimeout &&
	    wpa_sm_set_param(wpa_s->wpa, RSNA_SA_TIMEOUT,
			     wpa_s->conf->dot11RSNAConfigSATimeout)) {
		wpa_printf(MSG_ERROR, "Invalid WPA parameter value for "
			   "dot11RSNAConfigSATimeout");
		return -1;
	}

	if (wpa_supplicant_driver_init(wpa_s, wait_for_interface) < 0) {
		return -1;
	}
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	wapi_sm_set_own_addr(wpa_s->wapi, wpa_s->own_addr);
#endif
	wpa_sm_set_own_addr(wpa_s->wpa, wpa_s->own_addr);

	wpa_s->ctrl_iface = wpa_supplicant_ctrl_iface_init(wpa_s);
	if (wpa_s->ctrl_iface == NULL) {
		wpa_printf(MSG_ERROR,
			   "Failed to initialize control interface '%s'.\n"
			   "You may have another wpa_supplicant process "
			   "already running or the file was\n"
			   "left by an unclean termination of wpa_supplicant "
			   "in which case you will need\n"
			   "to manually remove this file before starting "
			   "wpa_supplicant again.\n",
			   wpa_s->conf->ctrl_interface);
		return -1;
	}

	if (wpa_drv_get_capa(wpa_s, &capa) == 0 &&
	    capa.flags & WPA_DRIVER_FLAGS_USER_SPACE_MLME) {
		wpa_s->use_client_mlme = 1;
		if (ieee80211_sta_init(wpa_s))
			return -1;
	}

	return 0;
}


static void wpa_supplicant_deinit_iface(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->drv_priv) {
		wpa_supplicant_deauthenticate(wpa_s, REASON_DEAUTH_LEAVING);

		/* Backwards compatibility call to set_wpa() handler. This is
		 * called only just after init and just before deinit, so these
		 * handler can be used to implement same functionality. */
		if (wpa_drv_set_wpa(wpa_s, 0) < 0) {
			wpa_printf(MSG_ERROR, "Failed to disable WPA in the "
				   "driver.");
		}

		wpa_drv_set_drop_unencrypted(wpa_s, 0);
		wpa_drv_set_countermeasures(wpa_s, 0);
		wpa_clear_keys(wpa_s, NULL);
	}

	wpas_dbus_unregister_iface(wpa_s);

	wpa_supplicant_cleanup(wpa_s);

	if (wpa_s->drv_priv)
		wpa_drv_deinit(wpa_s);
}


/**
 * wpa_supplicant_add_iface - Add a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @iface: Interface configuration options
 * Returns: Pointer to the created interface or %NULL on failure
 *
 * This function is used to add new network interfaces for %wpa_supplicant.
 * This can be called before wpa_supplicant_run() to add interfaces before the
 * main event loop has been started. In addition, new interfaces can be added
 * dynamically while %wpa_supplicant is already running. This could happen,
 * e.g., when a hotplug network adapter is inserted.
 */
struct wpa_supplicant * wpa_supplicant_add_iface(struct wpa_global *global,
						 struct wpa_interface *iface)
{
	struct wpa_supplicant *wpa_s;

	if (global == NULL || iface == NULL)
		return NULL;

	wpa_s = wpa_supplicant_alloc();
	if (wpa_s == NULL)
		return NULL;

	if (wpa_supplicant_init_iface(wpa_s, iface) ||
	    wpa_supplicant_init_iface2(wpa_s,
				       global->params.wait_for_interface)) {
		wpa_printf(MSG_DEBUG, "Failed to add interface %s",
			   iface->ifname);
		wpa_supplicant_deinit_iface(wpa_s);
		os_free(wpa_s);
		return NULL;
	}

	wpa_s->global = global;

	/* Register the interface with the dbus control interface */
	if (wpas_dbus_register_iface(wpa_s)) {
		wpa_supplicant_deinit_iface(wpa_s);
		os_free(wpa_s);
		return NULL;
	}
		
	wpa_s->next = global->ifaces;
	global->ifaces = wpa_s;

	wpa_printf(MSG_DEBUG, "Added interface %s", wpa_s->ifname);

	return wpa_s;
}


/**
 * wpa_supplicant_remove_iface - Remove a network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @wpa_s: Pointer to the network interface to be removed
 * Returns: 0 if interface was removed, -1 if interface was not found
 *
 * This function can be used to dynamically remove network interfaces from
 * %wpa_supplicant, e.g., when a hotplug network adapter is ejected. In
 * addition, this function is used to remove all remaining interfaces when
 * %wpa_supplicant is terminated.
 */
int wpa_supplicant_remove_iface(struct wpa_global *global,
				struct wpa_supplicant *wpa_s)
{
	struct wpa_supplicant *prev;

	/* Remove interface from the global list of interfaces */
	prev = global->ifaces;
	if (prev == wpa_s) {
		global->ifaces = wpa_s->next;
	} else {
		while (prev && prev->next != wpa_s)
			prev = prev->next;
		if (prev == NULL)
			return -1;
		prev->next = wpa_s->next;
	}

	wpa_printf(MSG_DEBUG, "Removing interface %s", wpa_s->ifname);

	wpa_supplicant_deinit_iface(wpa_s);
	os_free(wpa_s);

	return 0;
}


/**
 * wpa_supplicant_get_iface - Get a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @ifname: Interface name
 * Returns: Pointer to the interface or %NULL if not found
 */
struct wpa_supplicant * wpa_supplicant_get_iface(struct wpa_global *global,
						 const char *ifname)
{
	struct wpa_supplicant *wpa_s;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (os_strcmp(wpa_s->ifname, ifname) == 0)
			return wpa_s;
	}
	return NULL;
}


/**
 * wpa_supplicant_init - Initialize %wpa_supplicant
 * @params: Parameters for %wpa_supplicant
 * Returns: Pointer to global %wpa_supplicant data, or %NULL on failure
 *
 * This function is used to initialize %wpa_supplicant. After successful
 * initialization, the returned data pointer can be used to add and remove
 * network interfaces, and eventually, to deinitialize %wpa_supplicant.
 */
struct wpa_global * wpa_supplicant_init(struct wpa_params *params)
{
	struct wpa_global *global;
	int ret;

	if (params == NULL)
		return NULL;

	wpa_debug_open_file(params->wpa_debug_file_path);

	ret = eap_peer_register_methods();
	if (ret) {
		wpa_printf(MSG_ERROR, "Failed to register EAP methods");
		if (ret == -2)
			wpa_printf(MSG_ERROR, "Two or more EAP methods used "
				   "the same EAP type.");
		return NULL;
	}

	global = os_zalloc(sizeof(*global));
	if (global == NULL)
		return NULL;
	global->params.daemonize = params->daemonize;
	global->params.wait_for_interface = params->wait_for_interface;
	global->params.wait_for_monitor = params->wait_for_monitor;
	global->params.dbus_ctrl_interface = params->dbus_ctrl_interface;
	if (params->pid_file)
		global->params.pid_file = os_strdup(params->pid_file);
	if (params->ctrl_interface)
		global->params.ctrl_interface =
			os_strdup(params->ctrl_interface);
	wpa_debug_level = global->params.wpa_debug_level =
		params->wpa_debug_level;
	wpa_debug_show_keys = global->params.wpa_debug_show_keys =
		params->wpa_debug_show_keys;
	wpa_debug_timestamp = global->params.wpa_debug_timestamp =
		params->wpa_debug_timestamp;

	if (eloop_init(global)) {
		wpa_printf(MSG_ERROR, "Failed to initialize event loop");
		wpa_supplicant_deinit(global);
		return NULL;
	}

	global->ctrl_iface = wpa_supplicant_global_ctrl_iface_init(global);
	if (global->ctrl_iface == NULL) {
		wpa_supplicant_deinit(global);
		return NULL;
	}

	if (global->params.dbus_ctrl_interface) {
		global->dbus_ctrl_iface =
			wpa_supplicant_dbus_ctrl_iface_init(global);
		if (global->dbus_ctrl_iface == NULL) {
			wpa_supplicant_deinit(global);
			return NULL;
		}
	}

	if (global->params.wait_for_interface && global->params.daemonize &&
	    wpa_supplicant_daemon(global->params.pid_file)) {
		wpa_supplicant_deinit(global);
		return NULL;
	}

	return global;
}


/**
 * wpa_supplicant_run - Run the %wpa_supplicant main event loop
 * @global: Pointer to global data from wpa_supplicant_init()
 * Returns: 0 after successful event loop run, -1 on failure
 *
 * This function starts the main event loop and continues running as long as
 * there are any remaining events. In most cases, this function is running as
 * long as the %wpa_supplicant process in still in use.
 */
int wpa_supplicant_run(struct wpa_global *global)
{
	struct wpa_supplicant *wpa_s;

	if (!global->params.wait_for_interface && global->params.daemonize &&
	    wpa_supplicant_daemon(global->params.pid_file))
		return -1;

	if (global->params.wait_for_monitor) {
		for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next)
			if (wpa_s->ctrl_iface)
				wpa_supplicant_ctrl_iface_wait(
					wpa_s->ctrl_iface);
	}

	eloop_register_signal_terminate(wpa_supplicant_terminate, NULL);
	eloop_register_signal_reconfig(wpa_supplicant_reconfig, NULL);

	eloop_run();

	return 0;
}


/**
 * wpa_supplicant_deinit - Deinitialize %wpa_supplicant
 * @global: Pointer to global data from wpa_supplicant_init()
 *
 * This function is called to deinitialize %wpa_supplicant and to free all
 * allocated resources. Remaining network interfaces will also be removed.
 */
void wpa_supplicant_deinit(struct wpa_global *global)
{
	if (global == NULL)
		return;

	while (global->ifaces)
		wpa_supplicant_remove_iface(global, global->ifaces);

	if (global->ctrl_iface)
		wpa_supplicant_global_ctrl_iface_deinit(global->ctrl_iface);
	if (global->dbus_ctrl_iface)
		wpa_supplicant_dbus_ctrl_iface_deinit(global->dbus_ctrl_iface);

	eap_peer_unregister_methods();

	eloop_destroy();

	if (global->params.pid_file) {
		os_daemonize_terminate(global->params.pid_file);
		os_free(global->params.pid_file);
	}
	os_free(global->params.ctrl_interface);

	os_free(global);
	wpa_debug_close_file();
}

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
