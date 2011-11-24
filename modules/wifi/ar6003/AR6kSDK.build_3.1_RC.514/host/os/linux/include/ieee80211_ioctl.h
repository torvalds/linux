//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _IEEE80211_IOCTL_H_
#define _IEEE80211_IOCTL_H_

#include <linux/version.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Extracted from the MADWIFI net80211/ieee80211_ioctl.h
 */

/*
 * WPA/RSN get/set key request.  Specify the key/cipher
 * type and whether the key is to be used for sending and/or
 * receiving.  The key index should be set only when working
 * with global keys (use IEEE80211_KEYIX_NONE for ``no index'').
 * Otherwise a unicast/pairwise key is specified by the bssid
 * (on a station) or mac address (on an ap).  They key length
 * must include any MIC key data; otherwise it should be no
 more than IEEE80211_KEYBUF_SIZE.
 */
struct ieee80211req_key {
    u_int8_t    ik_type;    /* key/cipher type */
    u_int8_t    ik_pad;
    u_int16_t   ik_keyix;   /* key index */
    u_int8_t    ik_keylen;  /* key length in bytes */
    u_int8_t    ik_flags;
#define IEEE80211_KEY_XMIT  0x01
#define IEEE80211_KEY_RECV  0x02
#define IEEE80211_KEY_DEFAULT   0x80    /* default xmit key */
    u_int8_t    ik_macaddr[IEEE80211_ADDR_LEN];
    u_int64_t   ik_keyrsc;  /* key receive sequence counter */
    u_int64_t   ik_keytsc;  /* key transmit sequence counter */
    u_int8_t    ik_keydata[IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE];
};
/*
 * Delete a key either by index or address.  Set the index
 * to IEEE80211_KEYIX_NONE when deleting a unicast key.
 */
struct ieee80211req_del_key {
    u_int8_t    idk_keyix;  /* key index */
    u_int8_t    idk_macaddr[IEEE80211_ADDR_LEN];
};
/*
 * MLME state manipulation request.  IEEE80211_MLME_ASSOC
 * only makes sense when operating as a station.  The other
 * requests can be used when operating as a station or an
 * ap (to effect a station).
 */
struct ieee80211req_mlme {
    u_int8_t    im_op;      /* operation to perform */
#define IEEE80211_MLME_ASSOC        1   /* associate station */
#define IEEE80211_MLME_DISASSOC     2   /* disassociate station */
#define IEEE80211_MLME_DEAUTH       3   /* deauthenticate station */
#define IEEE80211_MLME_AUTHORIZE    4   /* authorize station */
#define IEEE80211_MLME_UNAUTHORIZE  5   /* unauthorize station */
    u_int16_t   im_reason;  /* 802.11 reason code */
    u_int8_t    im_macaddr[IEEE80211_ADDR_LEN];
};

struct ieee80211req_addpmkid {
    u_int8_t    pi_bssid[IEEE80211_ADDR_LEN];
    u_int8_t    pi_enable;
    u_int8_t    pi_pmkid[16];
};

#define AUTH_ALG_OPEN_SYSTEM    0x01
#define AUTH_ALG_SHARED_KEY 0x02
#define AUTH_ALG_LEAP       0x04

struct ieee80211req_authalg {
   u_int8_t auth_alg;
};  

/* 
 * Request to add an IE to a Management Frame
 */
enum{
    IEEE80211_APPIE_FRAME_BEACON     = 0,
    IEEE80211_APPIE_FRAME_PROBE_REQ  = 1,
    IEEE80211_APPIE_FRAME_PROBE_RESP = 2,
    IEEE80211_APPIE_FRAME_ASSOC_REQ  = 3,
    IEEE80211_APPIE_FRAME_ASSOC_RESP = 4,
    IEEE80211_APPIE_NUM_OF_FRAME     = 5
};

/*
 * The Maximum length of the IE that can be added to a Management frame
 */
#define IEEE80211_APPIE_FRAME_MAX_LEN  200

struct ieee80211req_getset_appiebuf {
    u_int32_t app_frmtype; /* management frame type for which buffer is added */
    u_int32_t app_buflen;  /*application supplied buffer length */
    u_int8_t  app_buf[];
};

/* 
 * The following definitions are used by an application to set filter
 * for receiving management frames 
 */
enum {
     IEEE80211_FILTER_TYPE_BEACON      =   0x1,
     IEEE80211_FILTER_TYPE_PROBE_REQ   =   0x2,
     IEEE80211_FILTER_TYPE_PROBE_RESP  =   0x4,
     IEEE80211_FILTER_TYPE_ASSOC_REQ   =   0x8,
     IEEE80211_FILTER_TYPE_ASSOC_RESP  =   0x10,
     IEEE80211_FILTER_TYPE_AUTH        =   0x20,
     IEEE80211_FILTER_TYPE_DEAUTH      =   0x40,
     IEEE80211_FILTER_TYPE_DISASSOC    =   0x80,
     IEEE80211_FILTER_TYPE_ALL         =   0xFF  /* used to check the valid filter bits */
};

struct ieee80211req_set_filter {
      u_int32_t app_filterype; /* management frame filter type */
};

enum {
    IEEE80211_PARAM_AUTHMODE = 3,   /* Authentication Mode */
    IEEE80211_PARAM_MCASTCIPHER = 5,
    IEEE80211_PARAM_MCASTKEYLEN = 6,    /* multicast key length */
    IEEE80211_PARAM_UCASTCIPHER = 8,
    IEEE80211_PARAM_UCASTKEYLEN = 9,    /* unicast key length */
    IEEE80211_PARAM_WPA     = 10,   /* WPA mode (0,1,2) */
    IEEE80211_PARAM_ROAMING     = 12,   /* roaming mode */
    IEEE80211_PARAM_PRIVACY     = 13,   /* privacy invoked */
    IEEE80211_PARAM_COUNTERMEASURES = 14,   /* WPA/TKIP countermeasures */
    IEEE80211_PARAM_DROPUNENCRYPTED = 15,   /* discard unencrypted frames */
    IEEE80211_PARAM_WAPI = 16,   /* WAPI policy from wapid */        
};

/*
 * Values for IEEE80211_PARAM_WPA
 */
#define WPA_MODE_WPA1   1
#define WPA_MODE_WPA2   2
#define WPA_MODE_AUTO   3
#define WPA_MODE_NONE   4

struct ieee80211req_wpaie {
    u_int8_t    wpa_macaddr[IEEE80211_ADDR_LEN];
    u_int8_t    wpa_ie[IEEE80211_MAX_IE];
    u_int8_t    rsn_ie[IEEE80211_MAX_IE];
};

#ifndef IW_ENCODE_ALG_PMK
#define IW_ENCODE_ALG_PMK       4
#endif
#ifndef IW_ENCODE_ALG_KRK
#define IW_ENCODE_ALG_KRK       6
#endif
#ifdef __cplusplus
}
#endif

#endif /* _IEEE80211_IOCTL_H_ */
