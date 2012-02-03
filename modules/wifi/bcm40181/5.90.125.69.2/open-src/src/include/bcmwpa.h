/*
 * bcmwpa.h - interface definitions of shared WPA-related functions
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: bcmwpa.h 278681 2011-08-19 17:50:47Z $
 */

#ifndef _BCMWPA_H_
#define _BCMWPA_H_

#include <proto/wpa.h>
#if defined(BCMSUP_PSK) || defined(BCMSUPPL)
#include <proto/eapol.h>
#endif
#include <proto/802.11.h>
#ifdef WLP2P
#include <proto/p2p.h>
#endif
#include <wlioctl.h>

/* Field sizes for WPA key hierarchy */
#define WPA_MIC_KEY_LEN     16
#define WPA_ENCR_KEY_LEN    16
#define WPA_TEMP_ENCR_KEY_LEN   16
#define WPA_TEMP_TX_KEY_LEN 8
#define WPA_TEMP_RX_KEY_LEN 8

#define PMK_LEN         32
#define TKIP_PTK_LEN        64
#define TKIP_TK_LEN     32
#define AES_PTK_LEN     48
#define AES_TK_LEN      16

/* limits for pre-shared key lengths */
#define WPA_MIN_PSK_LEN     8
#define WPA_MAX_PSK_LEN     64

#define WPA_KEY_DATA_LEN_256	256	/* allocation size of 256 for temp data pointer. */
#define WPA_KEY_DATA_LEN_128	128	/* allocation size of 128 for temp data pointer. */

#define WLC_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & WSEC_SWFLAG)))


#define WSEC_WEP_ENABLED(wsec)  ((wsec) & WEP_ENABLED)
#define WSEC_TKIP_ENABLED(wsec) ((wsec) & TKIP_ENABLED)
#define WSEC_AES_ENABLED(wsec)  ((wsec) & AES_ENABLED)
#define WSEC_ENABLED(wsec)  ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#define WSEC_SES_OW_ENABLED(wsec)   ((wsec) & SES_OW_ENABLED)
#define IS_WPA_AUTH(auth)   ((auth) == WPA_AUTH_NONE ||		\
			     (auth) == WPA_AUTH_UNSPECIFIED ||	\
			     (auth) == WPA_AUTH_PSK)
#define INCLUDES_WPA_AUTH(auth)						\
	((auth) & (WPA_AUTH_NONE | WPA_AUTH_UNSPECIFIED | WPA_AUTH_PSK))

#define IS_WPA2_AUTH(auth)	((auth) == WPA2_AUTH_UNSPECIFIED || \
				(auth) == WPA2_AUTH_PSK || \
				(auth) == BRCM_AUTH_PSK || \
				(auth) == BRCM_AUTH_DPT)
#define INCLUDES_WPA2_AUTH(auth)		\
	((auth) & (WPA2_AUTH_UNSPECIFIED |	\
	           WPA2_AUTH_PSK |		\
	           BRCM_AUTH_PSK |		\
	           BRCM_AUTH_DPT))



#define IS_WPA_AKM(akm) ((akm) == RSN_AKM_NONE ||		\
			 (akm) == RSN_AKM_UNSPECIFIED ||	\
			 (akm) == RSN_AKM_PSK)
#define IS_WPA2_AKM(akm)    ((akm) == RSN_AKM_UNSPECIFIED ||	\
			     (akm) == RSN_AKM_PSK)
#define IS_FBT_AKM(akm) ((akm) == RSN_AKM_FBT_1X ||	\
			 (akm) == RSN_AKM_FBT_PSK)

/* Broadcom(OUI) authenticated key managment suite */
#define BRCM_AKM_NONE           0
#define BRCM_AKM_PSK            1       /* Proprietary PSK AKM */
#define BRCM_AKM_DPT            2       /* Proprietary DPT PSK AKM */

#define IS_BRCM_AKM(akm)        ((akm) == BRCM_AKM_PSK)

#define MAX_ARRAY 1
#define MIN_ARRAY 0

/* convert wsec to WPA mcast cipher. algo is needed only when WEP is enabled. */
#define WPA_MCAST_CIPHER(wsec, algo) \
	(WSEC_WEP_ENABLED(wsec) ? \
	 ((algo) == CRYPTO_ALGO_WEP128 ? WPA_CIPHER_WEP_104 : WPA_CIPHER_WEP_40) : \
	 WSEC_TKIP_ENABLED(wsec) ? WPA_CIPHER_TKIP : \
	 WSEC_AES_ENABLED(wsec) ? WPA_CIPHER_AES_CCM : \
	 WPA_CIPHER_NONE)

#define WPS_ATID_SEL_REGISTRAR      0x1041

#define WPS_IE_FIXED_LEN    6

/* WiFi WPS Attribute fixed portion */
typedef struct wps_at_fixed {
	uint8 at[2];
	uint8 len[2];
	uint8 data[1];
} wps_at_fixed_t;

#define WPS_AT_FIXED_LEN    4

/* Return address of max or min array depending first argument.
 * Return NULL in case of a draw.
 */
extern uint8 *wpa_array_cmp(int max_array, uint8 *x, uint8 *y, uint len);

/* Increment the array argument */
extern void wpa_incr_array(uint8 *array, uint len);

/* Convert WPA IE cipher suite to locally used value */
extern bool wpa_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok);

/* Look for a WPA IE; return it's address if found, NULL otherwise */
extern wpa_ie_fixed_t *bcm_find_wpaie(uint8 *parse, uint len);
/* Look for a WPS IE; return it's address if found, NULL otherwise */
extern wpa_ie_fixed_t *bcm_find_wpsie(uint8 *parse, uint len);
extern wps_at_fixed_t *bcm_wps_find_at(wps_at_fixed_t *at, int len, uint16 id);
#ifdef WLP2P
/* Look for a WiFi P2P IE; return it's address if found, NULL otherwise */
extern wifi_p2p_ie_t *bcm_find_p2pie(uint8 *parse, uint len);
#endif

/* Check whether the given IE looks like WFA IE with the specific type. */
extern bool bcm_is_wfa_ie(uint8 *ie, uint8 **tlvs, uint *tlvs_len, uint8 type);
extern bool bcm_has_ie(uint8 *ie, uint8 **tlvs, uint *tlvs_len,
                       const uint8 *oui, int oui_len, uint8 type);

/* Check whether pointed-to IE looks like WPA. */
#define bcm_is_wpa_ie(ie, tlvs, len)    bcm_is_wfa_ie(ie, tlvs, len, WPA_OUI_TYPE)
/* Check whether pointed-to IE looks like WPS. */
#define bcm_is_wps_ie(ie, tlvs, len)    bcm_is_wfa_ie(ie, tlvs, len, WPS_OUI_TYPE)

#ifdef WLP2P
/* Check whether the given IE looks like WFA P2P IE. */
#define bcm_is_p2p_ie(ie, tlvs, len)    bcm_is_wfa_ie(ie, tlvs, len, WFA_OUI_TYPE_P2P)
#endif

/* Convert WPA2 IE cipher suite to locally used value */
extern bool wpa2_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok);

#if defined(BCMSUP_PSK) || defined(BCMSUPPL)
/* Look for an encapsulated GTK; return it's address if found, NULL otherwise */
extern eapol_wpa2_encap_data_t *wpa_find_gtk_encap(uint8 *parse, uint len);

/* Check whether pointed-to IE looks like an encapsulated GTK. */
extern bool wpa_is_gtk_encap(uint8 *ie, uint8 **tlvs, uint *tlvs_len);

/* Look for encapsulated key data; return it's address if found, NULL otherwise */
extern eapol_wpa2_encap_data_t *wpa_find_kde(uint8 *parse, uint len, uint8 type);
#endif /* defined(BCMSUP_PSK) || defined(BCMSUPPL) */

#ifdef BCMSUP_PSK
/* Calculate a pair-wise transient key */
extern void wpa_calc_ptk(struct ether_addr *auth_ea, struct ether_addr *sta_ea,
                                   uint8 *anonce, uint8* snonce, uint8 *pmk, uint pmk_len,
                                   uint8 *ptk, uint ptk_len);

/* Compute Message Integrity Code (MIC) over EAPOL message */
extern bool wpa_make_mic(eapol_header_t *eapol, uint key_desc, uint8 *mic_key,
                                   uchar *mic);

/* Check MIC of EAPOL message */
extern bool wpa_check_mic(eapol_header_t *eapol, uint key_desc, uint8 *mic_key);

/* Calculate PMKID */
extern void wpa_calc_pmkid(struct ether_addr *auth_ea, struct ether_addr *sta_ea,
                                     uint8 *pmk, uint pmk_len, uint8 *pmkid);

/* Calculate PMKR0 for FT association */
extern void wpa_calc_pmkR0(uchar *ssid, int ssid_len, uint16 mdid, uint8 *r0kh,
	uint r0kh_len, struct ether_addr *sta_ea,
	uint8 *pmk, uint pmk_len, uint8 *pmkid, uint8 *pmkr0name);

/* Calculate PMKR1 for FT association */
extern void wpa_calc_pmkR1(struct ether_addr *r1kh, struct ether_addr *sta_ea,
	uint8 *pmk, uint pmk_len, uint8 *pmkr0name, uint8 *pmkid, uint8 *pmkr1name);

/* Calculate PTK for FT association */
extern void wpa_calc_ft_ptk(struct ether_addr *bssid, struct ether_addr *sta_ea,
	uint8 *anonce, uint8* snonce, uint8 *pmk, uint pmk_len,
	uint8 *ptk, uint ptk_len);

/* Encrypt key data for a WPA key message */
extern bool wpa_encr_key_data(eapol_wpa_key_header_t *body, uint16 key_info,
                              uint8 *ekey, uint8 *gtk);

/* Decrypt key data from a WPA key message */
extern bool wpa_decr_key_data(eapol_wpa_key_header_t *body, uint16 key_info,
                                        uint8 *ekey, uint8 *gtk);

/* Decrypt a group transient key from a WPA key message */
extern bool wpa_decr_gtk(eapol_wpa_key_header_t *body, uint16 key_info,
                                   uint8 *ekey, uint8 *gtk);
#endif  /* BCMSUP_PSK */

extern bool bcmwpa_akm2WPAauth(uint8 *akm, uint32 *auth, bool sta_iswpa);

extern bool bcmwpa_cipher2wsec(uint8 *cipher, uint32 *wsec);

#if defined(MFP) || defined(WLFBT)
/* Calculate PMKID */
extern void kdf_calc_pmkid(struct ether_addr *auth_ea, struct ether_addr *sta_ea,
	uint8 *pmk, uint pmk_len, uint8 *pmkid, uint8 *data, uint8 *digest);
extern void kdf_calc_ptk(struct ether_addr *auth_ea, struct ether_addr *sta_ea,
                                   uint8 *anonce, uint8* snonce, uint8 *pmk, uint pmk_len,
                                   uint8 *ptk, uint ptk_len);
#endif

#ifdef WLFBT
/* Calculate PMKR0 for FT association */
extern void wpa_calc_pmkR0(uchar *ssid, int ssid_len, uint16 mdid, uint8 *r0kh,
                           uint r0kh_len, struct ether_addr *sta_ea,
                           uint8 *pmk, uint pmk_len, uint8 *pmkid, uint8 *pmkr0name);

/* Calculate PMKR1 for FT association */
extern void wpa_calc_pmkR1(struct ether_addr *r1kh, struct ether_addr *sta_ea,
                           uint8 *pmk, uint pmk_len, uint8 *pmkr0name,
                           uint8 *pmkid, uint8 *pmkr1name);

/* Calculate PTK for FT association */
extern void wpa_calc_ft_ptk(struct ether_addr *bssid, struct ether_addr *sta_ea,
                            uint8 *anonce, uint8* snonce, uint8 *pmk, uint pmk_len,
                            uint8 *ptk, uint ptk_len);
#endif /* WLFBT */

#endif  /* _BCMWPA_H_ */
