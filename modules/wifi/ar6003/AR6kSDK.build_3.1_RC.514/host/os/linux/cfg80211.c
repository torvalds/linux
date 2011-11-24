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

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "ar6000_drv.h"


extern unsigned int wmitimeout;
extern int reconnect_flag;


#define RATETAB_ENT(_rate, _rateid, _flags) {   \
    .bitrate    = (_rate),                  \
    .flags      = (_flags),                 \
    .hw_value   = (_rateid),                \
}

#define CHAN2G(_channel, _freq, _flags) {   \
    .band           = IEEE80211_BAND_2GHZ,  \
    .hw_value       = (_channel),           \
    .center_freq    = (_freq),              \
    .flags          = (_flags),             \
    .max_antenna_gain   = 0,                \
    .max_power      = 30,                   \
}

#define CHAN5G(_channel, _flags) {              \
    .band           = IEEE80211_BAND_5GHZ,      \
    .hw_value       = (_channel),               \
    .center_freq    = 5000 + (5 * (_channel)),  \
    .flags          = (_flags),                 \
    .max_antenna_gain   = 0,                    \
    .max_power      = 30,                       \
}

static struct
ieee80211_rate ar6k_rates[] = {
    RATETAB_ENT(10,  0x1,   0),
    RATETAB_ENT(20,  0x2,   0),
    RATETAB_ENT(55,  0x4,   0),
    RATETAB_ENT(110, 0x8,   0),
    RATETAB_ENT(60,  0x10,  0),
    RATETAB_ENT(90,  0x20,  0),
    RATETAB_ENT(120, 0x40,  0),
    RATETAB_ENT(180, 0x80,  0),
    RATETAB_ENT(240, 0x100, 0),
    RATETAB_ENT(360, 0x200, 0),
    RATETAB_ENT(480, 0x400, 0),
    RATETAB_ENT(540, 0x800, 0),
};

#define ar6k_a_rates     (ar6k_rates + 4)
#define ar6k_a_rates_size    8
#define ar6k_g_rates     (ar6k_rates + 0)
#define ar6k_g_rates_size    12

static struct
ieee80211_channel ar6k_2ghz_channels[] = {
    CHAN2G(1, 2412, 0),
    CHAN2G(2, 2417, 0),
    CHAN2G(3, 2422, 0),
    CHAN2G(4, 2427, 0),
    CHAN2G(5, 2432, 0),
    CHAN2G(6, 2437, 0),
    CHAN2G(7, 2442, 0),
    CHAN2G(8, 2447, 0),
    CHAN2G(9, 2452, 0),
    CHAN2G(10, 2457, 0),
    CHAN2G(11, 2462, 0),
    CHAN2G(12, 2467, 0),
    CHAN2G(13, 2472, 0),
    CHAN2G(14, 2484, 0),
};

static struct
ieee80211_channel ar6k_5ghz_a_channels[] = {
    CHAN5G(34, 0),      CHAN5G(36, 0),
    CHAN5G(38, 0),      CHAN5G(40, 0),
    CHAN5G(42, 0),      CHAN5G(44, 0),
    CHAN5G(46, 0),      CHAN5G(48, 0),
    CHAN5G(52, 0),      CHAN5G(56, 0),
    CHAN5G(60, 0),      CHAN5G(64, 0),
    CHAN5G(100, 0),     CHAN5G(104, 0),
    CHAN5G(108, 0),     CHAN5G(112, 0),
    CHAN5G(116, 0),     CHAN5G(120, 0),
    CHAN5G(124, 0),     CHAN5G(128, 0),
    CHAN5G(132, 0),     CHAN5G(136, 0),
    CHAN5G(140, 0),     CHAN5G(149, 0),
    CHAN5G(153, 0),     CHAN5G(157, 0),
    CHAN5G(161, 0),     CHAN5G(165, 0),
    CHAN5G(184, 0),     CHAN5G(188, 0),
    CHAN5G(192, 0),     CHAN5G(196, 0),
    CHAN5G(200, 0),     CHAN5G(204, 0),
    CHAN5G(208, 0),     CHAN5G(212, 0),
    CHAN5G(216, 0),
};

static struct
ieee80211_supported_band ar6k_band_2ghz = {
    .n_channels = ARRAY_SIZE(ar6k_2ghz_channels),
    .channels = ar6k_2ghz_channels,
    .n_bitrates = ar6k_g_rates_size,
    .bitrates = ar6k_g_rates,
};

static struct
ieee80211_supported_band ar6k_band_5ghz = {
    .n_channels = ARRAY_SIZE(ar6k_5ghz_a_channels),
    .channels = ar6k_5ghz_a_channels,
    .n_bitrates = ar6k_a_rates_size,
    .bitrates = ar6k_a_rates,
};

static int
ar6k_set_wpa_version(AR_SOFTC_DEV_T *arPriv, enum nl80211_wpa_versions wpa_version)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: %u\n", __func__, wpa_version));

    if (!wpa_version) {
        arPriv->arAuthMode = WMI_NONE_AUTH;
    } else if (wpa_version & NL80211_WPA_VERSION_1) {
        arPriv->arAuthMode = WMI_WPA_AUTH;
    } else if (wpa_version & NL80211_WPA_VERSION_2) {
        arPriv->arAuthMode = WMI_WPA2_AUTH;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: %u not spported\n", __func__, wpa_version));
        return -ENOTSUPP;
    }

    return A_OK;
}

static int
ar6k_set_auth_type(AR_SOFTC_DEV_T *arPriv, enum nl80211_auth_type auth_type)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: 0x%x\n", __func__, auth_type));

    switch (auth_type) {
    case NL80211_AUTHTYPE_OPEN_SYSTEM:
        arPriv->arDot11AuthMode = OPEN_AUTH;
        break;
    case NL80211_AUTHTYPE_SHARED_KEY:
        arPriv->arDot11AuthMode = SHARED_AUTH;
        break;
    case NL80211_AUTHTYPE_NETWORK_EAP:
        arPriv->arDot11AuthMode = LEAP_AUTH;
        break;
    default:
        arPriv->arDot11AuthMode = OPEN_AUTH;
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: 0x%x not spported\n", __func__, auth_type));
        return -ENOTSUPP;
    }

    return A_OK;
}

static int
ar6k_set_cipher(AR_SOFTC_DEV_T *arPriv, A_UINT32 cipher, A_BOOL ucast)
{
    A_UINT8  *ar_cipher = ucast ? &arPriv->arPairwiseCrypto :
                                &arPriv->arGroupCrypto;
    A_UINT8  *ar_cipher_len = ucast ? &arPriv->arPairwiseCryptoLen :
                                    &arPriv->arGroupCryptoLen;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: cipher 0x%x, ucast %u\n", __func__, cipher, ucast));

    switch (cipher) {
    case 0:
    case IW_AUTH_CIPHER_NONE:
        *ar_cipher = NONE_CRYPT;
        *ar_cipher_len = 0;
        break;
    case WLAN_CIPHER_SUITE_WEP40:
        *ar_cipher = WEP_CRYPT;
        *ar_cipher_len = 5;
        break;
    case WLAN_CIPHER_SUITE_WEP104:
        *ar_cipher = WEP_CRYPT;
        *ar_cipher_len = 13;
        break;
    case WLAN_CIPHER_SUITE_TKIP:
#ifdef CFG80211_WAPI_ENABLE
		*ar_cipher = WAPI_CRYPT;
#else
        *ar_cipher = TKIP_CRYPT;
#endif
        *ar_cipher_len = 0;
        break;
    case WLAN_CIPHER_SUITE_CCMP:
        *ar_cipher = AES_CRYPT;
        *ar_cipher_len = 0;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: cipher 0x%x not supported\n", __func__, cipher));
        return -ENOTSUPP;
    }

    return A_OK;
}

static void
ar6k_set_key_mgmt(AR_SOFTC_DEV_T *arPriv, A_UINT32 key_mgmt)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: 0x%x\n", __func__, key_mgmt));

    if (WLAN_AKM_SUITE_PSK == key_mgmt) {
        if (WMI_WPA_AUTH == arPriv->arAuthMode) {
            arPriv->arAuthMode = WMI_WPA_PSK_AUTH;
        } else if (WMI_WPA2_AUTH == arPriv->arAuthMode) {
            arPriv->arAuthMode = WMI_WPA2_PSK_AUTH;
        }
    } else if (WLAN_AKM_SUITE_8021X != key_mgmt) {
        arPriv->arAuthMode = WMI_NONE_AUTH;
    }
}

static int
ar6k_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                      struct cfg80211_connect_params *sme)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta  = &arPriv->arSta;
    A_STATUS status;
#ifdef CFG80211_WAPI_ENABLE	
	const unsigned char wps_oui[] = { 0x00, 0x50, 0xf2, 0x04 };
	const unsigned char wpa_oui[] = { 0x00, 0x50, 0xf2, 0x01 };
	unsigned char *ie = sme->ie;
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready yet\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: destroy in progress\n", __func__));
        return -EBUSY;
    }

    if(!sme->ssid_len || IEEE80211_MAX_SSID_LEN < sme->ssid_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ssid invalid\n", __func__));
        return -EINVAL;
    }

    if(arSta->arSkipScan == TRUE &&
       ((sme->channel && sme->channel->center_freq == 0) ||
        (sme->bssid && !sme->bssid[0] && !sme->bssid[1] && !sme->bssid[2] &&
         !sme->bssid[3] && !sme->bssid[4] && !sme->bssid[5])))
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s:SkipScan: channel or bssid invalid\n", __func__));
        return -EINVAL;
    }

    if(down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
        return -ERESTARTSYS;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, destroy in progress\n", __func__));
        up(&ar->arSem);
        return -EBUSY;
    }

    if(ar->arTxPending[wmi_get_control_ep(arPriv->arWmi)]) {
        /*
        * sleep until the command queue drains
        */
        wait_event_interruptible_timeout(arPriv->arEvent,
        ar->arTxPending[wmi_get_control_ep(arPriv->arWmi)] == 0, wmitimeout * HZ);
        if (signal_pending(current)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: cmd queue drain timeout\n", __func__));
            up(&ar->arSem);
            return -EINTR;
        }
    }
    up(&ar->arSem);

    if(down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
        return -ERESTARTSYS;
    }

    if(arPriv->arConnected == TRUE &&
       arPriv->arSsidLen == sme->ssid_len &&
       !A_MEMCMP(arPriv->arSsid, sme->ssid, arPriv->arSsidLen)) {
        reconnect_flag = TRUE;
        status = wmi_reconnect_cmd(arPriv->arWmi,
                                   arSta->arReqBssid,
                                   arPriv->arChannelHint);

        if (status != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_reconnect_cmd failed\n", __func__));
            return -EIO;
        }
        return 0;
    } else if(arPriv->arSsidLen == sme->ssid_len &&
              !A_MEMCMP(arPriv->arSsid, sme->ssid, arPriv->arSsidLen)) {
	    ar6000_disconnect(arPriv);
    }

    A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
    arPriv->arSsidLen = sme->ssid_len;
    A_MEMCPY(arPriv->arSsid, sme->ssid, sme->ssid_len);

    if(sme->channel){
        arPriv->arChannelHint = sme->channel->center_freq;
    }

    A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
    if(sme->bssid){
        if(A_MEMCMP(&sme->bssid, bcast_mac, AR6000_ETH_ADDR_LEN)) {
            A_MEMCPY(arSta->arReqBssid, sme->bssid, sizeof(arSta->arReqBssid));
        }
    }
	
    ar6k_set_wpa_version(arPriv, sme->crypto.wpa_versions);
    ar6k_set_auth_type(arPriv, sme->auth_type);

    if(sme->crypto.n_ciphers_pairwise) {
        ar6k_set_cipher(arPriv, sme->crypto.ciphers_pairwise[0], true);
    } else {
        ar6k_set_cipher(arPriv, IW_AUTH_CIPHER_NONE, true);
    }
    ar6k_set_cipher(arPriv, sme->crypto.cipher_group, false);

    if(sme->crypto.n_akm_suites) {
        ar6k_set_key_mgmt(arPriv, sme->crypto.akm_suites[0]);
    }

    if((sme->key_len) &&
       (WMI_NONE_AUTH == arPriv->arAuthMode) &&
        (WEP_CRYPT == arPriv->arPairwiseCrypto)) {
        struct ar_key *key = NULL;

        if(sme->key_idx < WMI_MIN_KEY_INDEX || sme->key_idx > WMI_MAX_KEY_INDEX) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                            ("%s: key index %d out of bounds\n", __func__, sme->key_idx));
            up(&ar->arSem);
            return -ENOENT;
        }

        key = &arPriv->keys[sme->key_idx];
        key->key_len = sme->key_len;
        A_MEMCPY(key->key, sme->key, key->key_len);
        key->cipher = arPriv->arPairwiseCrypto;
        arPriv->arDefTxKeyIndex = sme->key_idx;

        wmi_addKey_cmd(arPriv->arWmi, sme->key_idx,
                    arPriv->arPairwiseCrypto,
                    GROUP_USAGE | TX_USAGE,
                    key->key_len,
                    NULL,
                    key->key, KEY_OP_INIT_VAL, NULL,
                    NO_SYNC_WMIFLAG);
    }

    if (!arSta->arUserBssFilter) {
        if (wmi_bssfilter_cmd(arPriv->arWmi, ALL_BSS_FILTER, 0) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Couldn't set bss filtering\n", __func__));
            up(&ar->arSem);
            return -EIO;
        }
    }

    arPriv->arNetworkType = arPriv->arNextMode;
	
#ifdef CFG80211_WAPI_ENABLE
/*the following codes for wps and wapi, but ONLY test wapi*/	
	if (ie[0] == WLAN_EID_VENDOR_SPECIFIC &&
         memcmp(ie + 2, wps_oui, sizeof(wps_oui)) == 0) {
				/* WPS IE detected, notify target */
				A_PRINTF("WPS IE detected -- setting WPS flag\n");
				arPriv->arSta.arConnectCtrlFlags |= CONNECT_WPS_FLAG;
				arPriv->arAuthMode = 0;
    } else {
     	if ((ie[0]==IEEE80211_ELEMID_RSN) ||
    		(ie[0]==IEEE80211_ELEMID_VENDOR && 
    		memcmp(&ie[2], wpa_oui, sizeof(wpa_oui))==0)) { 
				sme->ie_len = 0; /* Firmware will set for us. Clear the previous one */
   		}
		/* for WAPI */
        else if (ie[0]==IEEE80211_ELEMID_WAPI) 
	    {
        }
        /************/
		arPriv->arSta.arConnectCtrlFlags &= ~CONNECT_WPS_FLAG;
	}
	wmi_set_appie_cmd(arPriv->arWmi, WMI_FRAME_ASSOC_REQ, sme->ie_len, ie);
/*********************************************************/
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Connect called with authmode %d dot11 auth %d"\
                    " PW crypto %d PW crypto Len %d GRP crypto %d"\
                    " GRP crypto Len %d channel hint %u\n",
                    __func__, arPriv->arAuthMode, arPriv->arDot11AuthMode,
                    arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                    arPriv->arGroupCrypto, arPriv->arGroupCryptoLen, arPriv->arChannelHint));
    reconnect_flag = 0;
    status = wmi_connect_cmd(arPriv->arWmi, arPriv->arNetworkType,
                            arPriv->arDot11AuthMode, arPriv->arAuthMode,
                            arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                            arPriv->arGroupCrypto,arPriv->arGroupCryptoLen,
                            arPriv->arSsidLen, arPriv->arSsid,
                            arSta->arReqBssid, arPriv->arChannelHint,
                            arSta->arConnectCtrlFlags);

    up(&ar->arSem);

    if (A_EINVAL == status) {
        A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
        arPriv->arSsidLen = 0;
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Invalid request\n", __func__));
        return -ENOENT;
    } else if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_connect_cmd failed\n", __func__));
        return -EIO;
    }

    if ((!(arSta->arConnectCtrlFlags & CONNECT_DO_WPA_OFFLOAD)) &&
        ((WMI_WPA_PSK_AUTH == arPriv->arAuthMode) || (WMI_WPA2_PSK_AUTH == arPriv->arAuthMode)))
    {
        A_TIMEOUT_MS(&arSta->disconnect_timer, A_DISCONNECT_TIMER_INTERVAL, 0);
    }

    arSta->arConnectCtrlFlags &= ~CONNECT_DO_WPA_OFFLOAD;
    arSta->arConnectPending = TRUE;

    return 0;
}

void
ar6k_cfg80211_connect_event(AR_SOFTC_DEV_T *arPriv, A_UINT16 channel,
                A_UINT8 *bssid, A_UINT16 listenInterval,
                A_UINT16 beaconInterval,NETWORK_TYPE networkType,
                A_UINT8 beaconIeLen, A_UINT8 assocReqLen,
                A_UINT8 assocRespLen, A_UINT8 *assocInfo)
{
    A_UINT16 size = 0;
    A_UINT16 capability = 0;
    struct cfg80211_bss *bss = NULL;
    struct ieee80211_mgmt *mgmt = NULL;
    struct ieee80211_channel *ibss_channel = NULL;
    s32 signal = 50 * 100;
    A_UINT8 ie_buf_len = 0;
    unsigned char ie_buf[256];
    unsigned char *ptr_ie_buf = ie_buf;
    unsigned char *ieeemgmtbuf = NULL;
    A_UINT8 source_mac[ATH_MAC_LEN];

    A_UINT8 assocReqIeOffset = sizeof(A_UINT16)  +  /* capinfo*/
                               sizeof(A_UINT16);    /* listen interval */
    A_UINT8 assocRespIeOffset = sizeof(A_UINT16) +  /* capinfo*/
                                sizeof(A_UINT16) +  /* status Code */
                                sizeof(A_UINT16);   /* associd */
    A_UINT8 *assocReqIe = assocInfo + beaconIeLen + assocReqIeOffset;
    A_UINT8 *assocRespIe = assocInfo + beaconIeLen + assocReqLen + assocRespIeOffset;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    assocReqLen -= assocReqIeOffset;
    assocRespLen -= assocRespIeOffset;

    if((ADHOC_NETWORK & networkType)) {
        if(NL80211_IFTYPE_ADHOC != arPriv->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in ibss mode\n", __func__));
            return;
        }
    }

    if((INFRA_NETWORK & networkType)) {
        if(NL80211_IFTYPE_STATION != arPriv->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in station mode\n", __func__));
            return;
        }
    }

    /* Before informing the join/connect event, make sure that
     * bss entry is present in scan list, if it not present
     * construct and insert into scan list, otherwise that
     * event will be dropped on the way by cfg80211, due to
     * this keys will not be plumbed in case of WEP and
     * application will not be aware of join/connect status. */
    bss = cfg80211_get_bss(arPriv->wdev->wiphy, NULL, bssid,
                           arPriv->wdev->ssid, arPriv->wdev->ssid_len,
                           ((ADHOC_NETWORK & networkType) ? WLAN_CAPABILITY_IBSS : WLAN_CAPABILITY_ESS),
                           ((ADHOC_NETWORK & networkType) ? WLAN_CAPABILITY_IBSS : WLAN_CAPABILITY_ESS));

    if(!bss) {
        if (ADHOC_NETWORK & networkType) {
            /* construct 802.11 mgmt beacon */
            if(ptr_ie_buf) {
                *ptr_ie_buf++ = WLAN_EID_SSID;
                *ptr_ie_buf++ = arPriv->arSsidLen;
                A_MEMCPY(ptr_ie_buf, arPriv->arSsid, arPriv->arSsidLen);
                ptr_ie_buf +=arPriv->arSsidLen;

                *ptr_ie_buf++ = WLAN_EID_IBSS_PARAMS;
                *ptr_ie_buf++ = 2; /* length */
                *ptr_ie_buf++ = 0; /* ATIM window */
                *ptr_ie_buf++ = 0; /* ATIM window */

                /* TODO: update ibss params and include supported rates,
                 * DS param set, extened support rates, wmm. */

                ie_buf_len = ptr_ie_buf - ie_buf;
            }

            capability |= IEEE80211_CAPINFO_IBSS;
            if(WEP_CRYPT == arPriv->arPairwiseCrypto) {
                capability |= IEEE80211_CAPINFO_PRIVACY;
            }
            A_MEMCPY(source_mac, arPriv->arNetDev->dev_addr, ATH_MAC_LEN);
            ptr_ie_buf = ie_buf;
        } else {
            capability = *(A_UINT16 *)(&assocInfo[beaconIeLen]);
            A_MEMCPY(source_mac, bssid, ATH_MAC_LEN);
            ptr_ie_buf = assocReqIe;
            ie_buf_len = assocReqLen;
        }

        size = offsetof(struct ieee80211_mgmt, u)
             + sizeof(mgmt->u.beacon)
             + ie_buf_len;

        ieeemgmtbuf = A_MALLOC_NOWAIT(size);
        if(!ieeemgmtbuf) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                            ("%s: ieeeMgmtbuf alloc error\n", __func__));
            return;
        }

        A_MEMZERO(ieeemgmtbuf, size);
        mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
        mgmt->frame_control = (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
        A_MEMCPY(mgmt->da, bcast_mac, ATH_MAC_LEN);
        A_MEMCPY(mgmt->sa, source_mac, ATH_MAC_LEN);
        A_MEMCPY(mgmt->bssid, bssid, ATH_MAC_LEN);
        mgmt->u.beacon.beacon_int = beaconInterval;
        mgmt->u.beacon.capab_info = capability;
        A_MEMCPY(mgmt->u.beacon.variable, ptr_ie_buf, ie_buf_len);

        ibss_channel = ieee80211_get_channel(arPriv->wdev->wiphy, (int)channel);

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: inform bss with bssid %02x:%02x:%02x:%02x:%02x:%02x "\
                         "channel %d beaconInterval %d capability 0x%x\n",
                        __func__,
                        mgmt->bssid[0], mgmt->bssid[1], mgmt->bssid[2],
                        mgmt->bssid[3], mgmt->bssid[4], mgmt->bssid[5],
                        ibss_channel->hw_value, beaconInterval, capability));

        bss = cfg80211_inform_bss_frame(arPriv->wdev->wiphy,
                                        ibss_channel, mgmt,
                                        le16_to_cpu(size),
                                        signal, GFP_ATOMIC);
        A_FREE(ieeemgmtbuf);
        cfg80211_put_bss(bss);
    }

    if((ADHOC_NETWORK & networkType)) {
        cfg80211_ibss_joined(arPriv->arNetDev, bssid, GFP_ATOMIC);
        return;
    }

    if (FALSE == arPriv->arConnected) {
        /* inform connect result to cfg80211 */
        cfg80211_connect_result(arPriv->arNetDev, bssid,
                                assocReqIe, assocReqLen,
                                assocRespIe, assocRespLen,
                                WLAN_STATUS_SUCCESS, GFP_ATOMIC);
    } else {
        /* inform roam event to cfg80211 */
        cfg80211_roamed(arPriv->arNetDev, bssid,
                        assocReqIe, assocReqLen,
                        assocRespIe, assocRespLen,
                        GFP_ATOMIC);
    }
}

static int
ar6k_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                        A_UINT16 reason_code)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: reason=%u\n", __func__, reason_code));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(ar->bIsDestroyProgress) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, destroy in progress\n", __func__));
        return -EBUSY;
    }

    if(down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: busy, couldn't get access\n", __func__));
        return -ERESTARTSYS;
    }

    reconnect_flag = 0;
    ar6000_disconnect(arPriv);
    A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
    arPriv->arSsidLen = 0;

    if (arPriv->arSta.arSkipScan == FALSE) {
        A_MEMZERO(arPriv->arSta.arReqBssid, sizeof(arPriv->arSta.arReqBssid));
    }

    up(&ar->arSem);

    return 0;
}

void
ar6k_cfg80211_disconnect_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 reason,
                               A_UINT8 *bssid, A_UINT8 assocRespLen,
                               A_UINT8 *assocInfo, A_UINT16 protocolReasonStatus)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: reason=%u\n", __func__, reason));

    if((ADHOC_NETWORK & arPriv->arNetworkType)) {
        if(NL80211_IFTYPE_ADHOC != arPriv->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in ibss mode\n", __func__));
            return;
        }
        A_MEMZERO(bssid, ETH_ALEN);
        cfg80211_ibss_joined(arPriv->arNetDev, bssid, GFP_ATOMIC);
        return;
    }

    if((INFRA_NETWORK & arPriv->arNetworkType)) {
        if(NL80211_IFTYPE_STATION != arPriv->wdev->iftype) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                            ("%s: ath6k not in station mode\n", __func__));
            return;
        }
    }

    if(FALSE == arPriv->arConnected) {
        if(NO_NETWORK_AVAIL == reason) {
            /* connect cmd failed */
            cfg80211_connect_result(arPriv->arNetDev, bssid,
                                    NULL, 0,
                                    NULL, 0,
                                    WLAN_STATUS_UNSPECIFIED_FAILURE,
                                    GFP_ATOMIC);
        }
    } else {
        /* connection loss due to disconnect cmd or low rssi */
        cfg80211_disconnected(arPriv->arNetDev, reason, NULL, 0, GFP_ATOMIC);
    }
}

void
ar6k_cfg80211_scan_node(void *arg, bss_t *ni)
{
    struct wiphy *wiphy = (struct wiphy *)arg;
    A_UINT16 size;
    unsigned char *ieeemgmtbuf = NULL;
    struct ieee80211_mgmt *mgmt;
    struct ieee80211_channel *channel;
    struct ieee80211_supported_band *band;
    struct ieee80211_common_ie  *cie;
    s32 signal;
    int freq;

    cie = &ni->ni_cie;

#define CHAN_IS_11A(x)  (!((x >= 2412) && (x <= 2484)))
    if(CHAN_IS_11A(cie->ie_chan)) {
        /* 11a */
        band = wiphy->bands[IEEE80211_BAND_5GHZ];
    } else if((cie->ie_erp) || (cie->ie_xrates)) {
        /* 11g */
        band = wiphy->bands[IEEE80211_BAND_2GHZ];
    } else {
        /* 11b */
        band = wiphy->bands[IEEE80211_BAND_2GHZ];
    }

    size = ni->ni_framelen + offsetof(struct ieee80211_mgmt, u);
    ieeemgmtbuf = A_MALLOC_NOWAIT(size);
    if(!ieeemgmtbuf)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ieeeMgmtbuf alloc error\n", __func__));
        return;
    }

    /* Note:
       TODO: Update target to include 802.11 mac header while sending bss info.
       Target removes 802.11 mac header while sending the bss info to host,
       cfg80211 needs it, for time being just filling the da, sa and bssid fields alone.
    */
    mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
    A_MEMCPY(mgmt->da, bcast_mac, ATH_MAC_LEN);
    A_MEMCPY(mgmt->sa, ni->ni_macaddr, ATH_MAC_LEN);
    A_MEMCPY(mgmt->bssid, ni->ni_macaddr, ATH_MAC_LEN);
    A_MEMCPY(ieeemgmtbuf + offsetof(struct ieee80211_mgmt, u),
             ni->ni_buf, ni->ni_framelen);

    freq    = cie->ie_chan;
    channel = ieee80211_get_channel(wiphy, freq);
    signal  = ni->ni_snr * 100;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: bssid %02x:%02x:%02x:%02x:%02x:%02x channel %d freq %d size %d\n",
                   __func__,
                   mgmt->bssid[0], mgmt->bssid[1], mgmt->bssid[2],
                   mgmt->bssid[3], mgmt->bssid[4], mgmt->bssid[5],
                   channel->hw_value, freq, size));
    cfg80211_inform_bss_frame(wiphy, channel, mgmt,
                              le16_to_cpu(size),
                              signal, GFP_ATOMIC);

    A_FREE (ieeemgmtbuf);
}

static int
ar6k_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
                   struct cfg80211_scan_request *request)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    int ret = 0;
    A_BOOL forceFgScan = FALSE;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if (!arPriv->arSta.arUserBssFilter) {
        if (wmi_bssfilter_cmd(arPriv->arWmi,
                             (arPriv->arConnected ? ALL_BUT_BSS_FILTER : ALL_BSS_FILTER),
                             0) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Couldn't set bss filtering\n", __func__));
            return -EIO;
        }
    }

    if(request->n_ssids &&
       request->ssids[0].ssid_len) {
        A_UINT8 i;

        if(request->n_ssids > MAX_PROBED_SSID_INDEX) {
            request->n_ssids = MAX_PROBED_SSID_INDEX;
        }

        for (i = 0; i < request->n_ssids; i++) {
            wmi_probedSsid_cmd(arPriv->arWmi, i, SPECIFIC_SSID_FLAG,
                               request->ssids[i].ssid_len,
                               request->ssids[i].ssid);
        }
    }

    if(arPriv->arConnected) {
        forceFgScan = TRUE;
    }

    if(wmi_startscan_cmd(arPriv->arWmi, WMI_LONG_SCAN, forceFgScan, FALSE, \
                         0, 0, 0, NULL) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_startscan_cmd failed\n", __func__));
        ret = -EIO;
    }

    arPriv->scan_request = request;

    return ret;
}

void
ar6k_cfg80211_scanComplete_event(AR_SOFTC_DEV_T *arPriv, A_STATUS status)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: status %d\n", __func__, status));

    if(arPriv->scan_request)
    {
        /* Translate data to cfg80211 mgmt format */
        wmi_iterate_nodes(arPriv->arWmi, ar6k_cfg80211_scan_node, arPriv->wdev->wiphy);

        cfg80211_scan_done(arPriv->scan_request,
                          (status & A_ECANCELED) ? true : false);

        if(arPriv->scan_request->n_ssids &&
           arPriv->scan_request->ssids[0].ssid_len) {
            A_UINT8 i;

            for (i = 0; i < arPriv->scan_request->n_ssids; i++) {
                wmi_probedSsid_cmd(arPriv->arWmi, i, DISABLE_SSID_FLAG,
                                   0, NULL);
            }
        }
        arPriv->scan_request = NULL;
    }
}

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
ar6k_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, bool pairwise, const A_UINT8 *mac_addr,
                      struct key_params *params)
#else
ar6k_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, const A_UINT8 *mac_addr,
                      struct key_params *params)
#endif
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    struct ar_key *key = NULL;
    A_UINT8 key_usage;
    A_UINT8 key_type;
    A_STATUS status = 0;
#ifdef CFG80211_WAPI_ENABLE
	A_UINT32    *PN;
    A_INT32     i;
    A_UINT8     wapiKeyRsc[16] = {0};
	#define PN_INIT 0x5c365c36
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s:\n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    key = &arPriv->keys[key_index];
    A_MEMZERO(key, sizeof(struct ar_key));

    if(!mac_addr || is_broadcast_ether_addr(mac_addr)) {
        key_usage = GROUP_USAGE;
    } else {
        key_usage = PAIRWISE_USAGE;
    }

    if(params) {
        if(params->key_len > WLAN_MAX_KEY_LEN ||
            params->seq_len > IW_ENCODE_SEQ_MAX_SIZE)
            return -EINVAL;

        key->key_len = params->key_len;
        A_MEMCPY(key->key, params->key, key->key_len);
        key->seq_len = params->seq_len;
        A_MEMCPY(key->seq, params->seq, key->seq_len);
        key->cipher = params->cipher;
    }

    switch (key->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
    case WLAN_CIPHER_SUITE_WEP104:
        key_type = WEP_CRYPT;
        break;

    case WLAN_CIPHER_SUITE_TKIP:
        key_type = TKIP_CRYPT;
        break;

    case WLAN_CIPHER_SUITE_CCMP:
        key_type = AES_CRYPT;
        break;

    default:
        return -ENOTSUPP;
    }

    if (((WMI_WPA_PSK_AUTH == arPriv->arAuthMode) || (WMI_WPA2_PSK_AUTH == arPriv->arAuthMode)) &&
        (GROUP_USAGE & key_usage))
    {
        A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: index %d, key_len %d, key_type 0x%x,"\
                    " key_usage 0x%x, seq_len %d\n",
                    __func__, key_index, key->key_len, key_type,
                    key_usage, key->seq_len));
    arPriv->arDefTxKeyIndex = key_index;

#ifdef CFG80211_WAPI_ENABLE
	key_type  = WAPI_CRYPT;
	key_usage = 0;
	if (is_broadcast_ether_addr(mac_addr)) {
        key_usage |= GROUP_USAGE;
        PN = (A_UINT32 *)wapiKeyRsc;
        for (i = 0; i < 4; i++) {
            PN[i] = PN_INIT;
        }
    } else {
        key_usage |= PAIRWISE_USAGE;
    }	
    status = wmi_addKey_cmd(arPriv->arWmi, arPriv->arDefTxKeyIndex, key_type, key_usage,
                    key->key_len, wapiKeyRsc, key->key, KEY_OP_INIT_VAL,
                    (A_UINT8*)mac_addr, SYNC_BOTH_WMIFLAG);
#else
    status = wmi_addKey_cmd(arPriv->arWmi, arPriv->arDefTxKeyIndex, key_type, key_usage,
                    key->key_len, key->seq, key->key, KEY_OP_INIT_VAL,
                    (A_UINT8*)mac_addr, SYNC_BOTH_WMIFLAG);
#endif


    if(status != A_OK) {
        return -EIO;
    }

    return 0;
}

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
ar6k_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, bool pairwise, const A_UINT8 *mac_addr)
#else
ar6k_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, const A_UINT8 *mac_addr)
#endif
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    if(!arPriv->keys[key_index].key_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d is empty\n", __func__, key_index));
        return 0;
    }

    arPriv->keys[key_index].key_len = 0;

    return wmi_deleteKey_cmd(arPriv->arWmi, key_index);
}


static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
ar6k_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, bool pairwise, const A_UINT8 *mac_addr,
                      void *cookie,
                      void (*callback)(void *cookie, struct key_params*))
#else
ar6k_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
                      A_UINT8 key_index, const A_UINT8 *mac_addr, void *cookie,
                      void (*callback)(void *cookie, struct key_params*))
#endif
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    struct ar_key *key = NULL;
    struct key_params params;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n", __func__, key_index));
        return -ENOENT;
    }

    key = &arPriv->keys[key_index];
    A_MEMZERO(&params, sizeof(params));
    params.cipher = key->cipher;
    params.key_len = key->key_len;
    params.seq_len = key->seq_len;
    params.seq = key->seq;
    params.key = key->key;

    callback(cookie, &params);

    return key->key_len ? 0 : -ENOENT;
}


static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
ar6k_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev,
                              A_UINT8 key_index,bool unicast, bool multicast)
#else
ar6k_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev,
                              A_UINT8 key_index)
#endif
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    struct ar_key *key = NULL;
    A_STATUS status = A_OK;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                        ("%s: key index %d out of bounds\n",
                        __func__, key_index));
        return -ENOENT;
    }

    if(!arPriv->keys[key_index].key_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: invalid key index %d\n",
                        __func__, key_index));
        return -EINVAL;
    }

    arPriv->arDefTxKeyIndex = key_index;
    key = &arPriv->keys[arPriv->arDefTxKeyIndex];
#ifdef CFG80211_WAPI_ENABLE
	/*if WAPI enable, we donot need to set it*/
#else
    status = wmi_addKey_cmd(arPriv->arWmi, arPriv->arDefTxKeyIndex,
                            arPriv->arPairwiseCrypto, GROUP_USAGE | TX_USAGE,
                            key->key_len, key->seq, key->key, KEY_OP_INIT_VAL,
                            NULL, SYNC_BOTH_WMIFLAG);
    if (status != A_OK) {
        return -EIO;
    }
#endif
    return 0;
}

static int
ar6k_cfg80211_set_default_mgmt_key(struct wiphy *wiphy, struct net_device *ndev,
                                   A_UINT8 key_index)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: index %d\n", __func__, key_index));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));
    return -ENOTSUPP;
}

void
ar6k_cfg80211_tkip_micerr_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 keyid, A_BOOL ismcast)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: keyid %d, ismcast %d\n", __func__, keyid, ismcast));

    cfg80211_michael_mic_failure(arPriv->arNetDev, arPriv->arBssid,
                                 (ismcast ? NL80211_KEYTYPE_GROUP : NL80211_KEYTYPE_PAIRWISE),
                                 keyid, NULL, GFP_KERNEL);
}

static int
ar6k_cfg80211_set_wiphy_params(struct wiphy *wiphy, A_UINT32 changed)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)wiphy_priv(wiphy);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: changed 0x%x\n", __func__, changed));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
        if (wmi_set_rts_cmd(arPriv->arWmi,wiphy->rts_threshold) != A_OK){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_set_rts_cmd failed\n", __func__));
            return -EIO;
        }
    }

    return 0;
}

static int
ar6k_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *dev,
                               const A_UINT8 *peer,
                               const struct cfg80211_bitrate_mask *mask)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    A_STATUS status;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: mask 0x%x\n", __func__, mask->fixed));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    status = wmi_set_fixrates_cmd(arPriv->arWmi, mask->fixed);

    if(status == A_EINVAL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: invalid params\n", __func__));
        return -EINVAL;
    } else if(status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_set_fixrates_cmd failed\n", __func__));
        return -EIO;
    }

    return 0;
#else
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Setting rates: Not supported\n"));
    return -EIO;
#endif
}

/* The type nl80211_tx_power_setting replaces the following data type from 2.6.36 onwards */
static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
ar6k_cfg80211_set_txpower(struct wiphy *wiphy, enum nl80211_tx_power_setting type, int dbm)
#else
ar6k_cfg80211_set_txpower(struct wiphy *wiphy, enum tx_power_setting type, int dbm)
#endif
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)wiphy_priv(wiphy);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    A_UINT8 ar_dbm;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type 0x%x, dbm %d\n", __func__, type, dbm));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    arPriv->arTxPwrSet = FALSE;
    switch(type) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
    case NL80211_TX_POWER_AUTOMATIC:
#else
    case TX_POWER_AUTOMATIC:
#endif
        return 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
    case NL80211_TX_POWER_LIMITED:
#else
    case TX_POWER_LIMITED:
#endif
        arPriv->arTxPwr = ar_dbm = dbm;
        arPriv->arTxPwrSet = TRUE;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type 0x%x not supported\n", __func__, type));
        return -EOPNOTSUPP;
    }

    wmi_set_txPwr_cmd(arPriv->arWmi, ar_dbm);

    return 0;
}

static int
ar6k_cfg80211_get_txpower(struct wiphy *wiphy, int *dbm)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)wiphy_priv(wiphy);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if((arPriv->arConnected == TRUE)) {
        arPriv->arTxPwr = 0;

        if(wmi_get_txPwr_cmd(arPriv->arWmi) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_get_txPwr_cmd failed\n", __func__));
            return -EIO;
        }

        wait_event_interruptible_timeout(arPriv->arEvent, arPriv->arTxPwr != 0, 5 * HZ);

        if(signal_pending(current)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Target did not respond\n", __func__));
            return -EINTR;
        }
    }

    *dbm = arPriv->arTxPwr;
    return 0;
}

static int
ar6k_cfg80211_set_power_mgmt(struct wiphy *wiphy,
                             struct net_device *dev,
                             bool pmgmt, int timeout)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    WMI_POWER_MODE_CMD pwrMode;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: pmgmt %d, timeout %d\n", __func__, pmgmt, timeout));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(!pmgmt) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Max Perf\n", __func__));
        pwrMode.powerMode = MAX_PERF_POWER;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Rec Power\n", __func__));
        pwrMode.powerMode = REC_POWER;
    }

    if(wmi_powermode_cmd(arPriv->arWmi, pwrMode.powerMode) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: wmi_powermode_cmd failed\n", __func__));
        return -EIO;
    }

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
struct net_device *
ar6k_cfg80211_add_virtual_intf(struct wiphy *wiphy, char *name,
                                            enum nl80211_iftype type, u32 *flags,
                                            struct vif_params *params)
#else
static int
ar6k_cfg80211_add_virtual_intf(struct wiphy *wiphy, char *name,
            				    enum nl80211_iftype type, u32 *flags,
            				    struct vif_params *params)
#endif
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));

    /* Multiple virtual interface is not supported.
     * The default interface supports STA and IBSS type
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
    return NULL;
#else
    return -EOPNOTSUPP;
#endif
}

static int
ar6k_cfg80211_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
{

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: not supported\n", __func__));

    /* Multiple virtual interface is not supported.
     * The default interface supports STA and IBSS type
     */
    return -EOPNOTSUPP;
}

static int
ar6k_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
                           enum nl80211_iftype type, u32 *flags,
                           struct vif_params *params)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ndev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    struct wireless_dev *wdev = arPriv->wdev;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: type %u\n", __func__, type));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    switch (type) {
    case NL80211_IFTYPE_STATION:
        arPriv->arNextMode = INFRA_NETWORK;
        break;
    case NL80211_IFTYPE_ADHOC:
        arPriv->arNextMode = ADHOC_NETWORK;
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: type %u\n", __func__, type));
        return -EOPNOTSUPP;
    }

    wdev->iftype = type;

    return 0;
}

static int
ar6k_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
                        struct cfg80211_ibss_params *ibss_param)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta;
    A_STATUS status;

    arSta  = &arPriv->arSta;
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    if(!ibss_param->ssid_len || IEEE80211_MAX_SSID_LEN < ibss_param->ssid_len) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ssid invalid\n", __func__));
        return -EINVAL;
    }

    arPriv->arSsidLen = ibss_param->ssid_len;
    A_MEMCPY(arPriv->arSsid, ibss_param->ssid, arPriv->arSsidLen);

    if(ibss_param->channel) {
        arPriv->arChannelHint = ibss_param->channel->center_freq;
    }

    if(ibss_param->channel_fixed) {
        /* TODO: channel_fixed: The channel should be fixed, do not search for
         * IBSSs to join on other channels. Target firmware does not support this
         * feature, needs to be updated.*/
    }

    A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
    if(ibss_param->bssid) {
        if(A_MEMCMP(&ibss_param->bssid, bcast_mac, AR6000_ETH_ADDR_LEN)) {
            A_MEMCPY(arSta->arReqBssid, ibss_param->bssid, sizeof(arSta->arReqBssid));
        }
    }

    ar6k_set_wpa_version(arPriv, 0);
    ar6k_set_auth_type(arPriv, NL80211_AUTHTYPE_OPEN_SYSTEM);

    if(ibss_param->privacy) {
        ar6k_set_cipher(arPriv, WLAN_CIPHER_SUITE_WEP40, true);
        ar6k_set_cipher(arPriv, WLAN_CIPHER_SUITE_WEP40, false);
    } else {
        ar6k_set_cipher(arPriv, IW_AUTH_CIPHER_NONE, true);
        ar6k_set_cipher(arPriv, IW_AUTH_CIPHER_NONE, false);
    }

    arPriv->arNetworkType = arPriv->arNextMode;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: Connect called with authmode %d dot11 auth %d"\
                    " PW crypto %d PW crypto Len %d GRP crypto %d"\
                    " GRP crypto Len %d channel hint %u\n",
                    __func__, arPriv->arAuthMode, arPriv->arDot11AuthMode,
                    arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                    arPriv->arGroupCrypto, arPriv->arGroupCryptoLen, arPriv->arChannelHint));

    status = wmi_connect_cmd(arPriv->arWmi, arPriv->arNetworkType,
                            arPriv->arDot11AuthMode, arPriv->arAuthMode,
                            arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                            arPriv->arGroupCrypto,arPriv->arGroupCryptoLen,
                            arPriv->arSsidLen, arPriv->arSsid,
                            arSta->arReqBssid, arPriv->arChannelHint,
                            arSta->arConnectCtrlFlags);
    arSta->arConnectPending = TRUE;

    return 0;
}

static int
ar6k_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{

    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(ar->arWmiReady == FALSE) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wmi not ready\n", __func__));
        return -EIO;
    }

    if(ar->arWlanState == WLAN_DISABLED) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Wlan disabled\n", __func__));
        return -EIO;
    }

    ar6000_disconnect(arPriv);
    A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
    arPriv->arSsidLen = 0;

    return 0;
}


static const
A_UINT32 cipher_suites[] = {
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_CCMP,
};

static struct
cfg80211_ops ar6k_cfg80211_ops = {
    .change_virtual_intf = ar6k_cfg80211_change_iface,
    .add_virtual_intf = ar6k_cfg80211_add_virtual_intf,
    .del_virtual_intf = ar6k_cfg80211_del_virtual_intf,
    .scan = ar6k_cfg80211_scan,
    .connect = ar6k_cfg80211_connect,
    .disconnect = ar6k_cfg80211_disconnect,
    .add_key = ar6k_cfg80211_add_key,
    .get_key = ar6k_cfg80211_get_key,
    .del_key = ar6k_cfg80211_del_key,
    .set_default_key = ar6k_cfg80211_set_default_key,
    .set_default_mgmt_key = ar6k_cfg80211_set_default_mgmt_key,
    .set_wiphy_params = ar6k_cfg80211_set_wiphy_params,
    .set_bitrate_mask = ar6k_cfg80211_set_bitrate_mask,
    .set_tx_power = ar6k_cfg80211_set_txpower,
    .get_tx_power = ar6k_cfg80211_get_txpower,
    .set_power_mgmt = ar6k_cfg80211_set_power_mgmt,
    .join_ibss = ar6k_cfg80211_join_ibss,
    .leave_ibss = ar6k_cfg80211_leave_ibss,
};

struct wireless_dev *
ar6k_cfg80211_init(struct device *dev)
{
    int ret = 0;
    struct wireless_dev *wdev;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
    if(!wdev) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't allocate wireless device\n", __func__));
        return ERR_PTR(-ENOMEM);
    }

    /* create a new wiphy for use with cfg80211 */
    wdev->wiphy = wiphy_new(&ar6k_cfg80211_ops, sizeof(AR_SOFTC_T));
    if(!wdev->wiphy) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't allocate wiphy device\n", __func__));
        kfree(wdev);
        return ERR_PTR(-ENOMEM);
    }

    /* set device pointer for wiphy */
    set_wiphy_dev(wdev->wiphy, dev);

    wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
                                   BIT(NL80211_IFTYPE_ADHOC);
    /* max num of ssids that can be probed during scanning */
    wdev->wiphy->max_scan_ssids = MAX_PROBED_SSID_INDEX;
    wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &ar6k_band_2ghz;
    wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &ar6k_band_5ghz;
    wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

    wdev->wiphy->cipher_suites = cipher_suites;
    wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

    ret = wiphy_register(wdev->wiphy);
    if(ret < 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                        ("%s: Couldn't register wiphy device\n", __func__));
        wiphy_free(wdev->wiphy);
        return ERR_PTR(ret);
    }

    return wdev;
}

void
ar6k_cfg80211_deinit(AR_SOFTC_DEV_T *arPriv)
{
    struct wireless_dev *wdev = arPriv->wdev;


    AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("%s: \n", __func__));

    if(arPriv->scan_request) {
        cfg80211_scan_done(arPriv->scan_request, true);
        arPriv->scan_request = NULL;
    }

    if(!wdev)
        return;

    wiphy_unregister(wdev->wiphy);
    wiphy_free(wdev->wiphy);
    kfree(wdev);
}







