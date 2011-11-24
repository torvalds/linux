/*
 * WPA Supplicant - driver interface definition
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

#ifndef DRIVER_H
#define DRIVER_H

#define WPA_SUPPLICANT_DRIVER_VERSION 2

#include "defs.h"

#define AUTH_ALG_OPEN_SYSTEM	0x01
#define AUTH_ALG_SHARED_KEY	0x02
#define AUTH_ALG_LEAP		0x04

#define IEEE80211_MODE_INFRA	0
#define IEEE80211_MODE_IBSS	1

#define IEEE80211_CAP_ESS	0x0001
#define IEEE80211_CAP_IBSS	0x0002
#define IEEE80211_CAP_PRIVACY	0x0010

#define SSID_MAX_WPA_IE_LEN 40
#define SSID_MAX_WAPI_IE_LEN 40
/**
 * struct wpa_scan_result - Scan results
 * @bssid: BSSID
 * @ssid: SSID
 * @ssid_len: length of the ssid
 * @wpa_ie: WPA IE
 * @wpa_ie_len: length of the wpa_ie
 * @rsn_ie: RSN IE
 * @rsn_ie_len: length of the RSN IE
 * @freq: frequency of the channel in MHz (e.g., 2412 = channel 1)
 * @caps: capability information field in host byte order
 * @qual: signal quality
 * @noise: noise level
 * @level: signal level
 * @maxrate: maximum supported rate
 *
 * This structure is used as a generic format for scan results from the
 * driver. Each driver interface implementation is responsible for converting
 * the driver or OS specific scan results into this format.
 */
struct wpa_scan_result {
	u8 bssid[ETH_ALEN];
	u8 ssid[32];
	size_t ssid_len;
	u8 wpa_ie[SSID_MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[SSID_MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
#if (DE_BUILTIN_WAPI == CFG_INCLUDED)
	u8 wapi_ie[SSID_MAX_WAPI_IE_LEN];
	size_t wapi_ie_len;
#endif
	int freq;
	u16 caps;
	int qual;
	int noise;
	int level;
	int maxrate;
};

/**
 * struct wpa_driver_associate_params - Association parameters
 * Data for struct wpa_driver_ops::associate().
 */
struct wpa_driver_associate_params {
	/**
	 * bssid - BSSID of the selected AP
	 * This can be %NULL, if ap_scan=2 mode is used and the driver is
	 * responsible for selecting with which BSS to associate. */
	const u8 *bssid;

	/**
	 * ssid - The selected SSID
	 */
	const u8 *ssid;
	size_t ssid_len;

	/**
	 * freq - Frequency of the channel the selected AP is using
	 * Frequency that the selected AP is using (in MHz as
	 * reported in the scan results)
	 */
	int freq;

	/**
	 * wpa_ie - WPA information element for (Re)Association Request
	 * WPA information element to be included in (Re)Association
	 * Request (including information element id and length). Use
	 * of this WPA IE is optional. If the driver generates the WPA
	 * IE, it can use pairwise_suite, group_suite, and
	 * key_mgmt_suite to select proper algorithms. In this case,
	 * the driver has to notify wpa_supplicant about the used WPA
	 * IE by generating an event that the interface code will
	 * convert into EVENT_ASSOCINFO data (see wpa_supplicant.h).
	 * When using WPA2/IEEE 802.11i, wpa_ie is used for RSN IE
	 * instead. When using WAPI, wpa_ie is used for WAPI IE instead.
	 * The driver can determine which version is used by
	 * looking at the first byte of the IE (0xdd for WPA, 0x30 for
	 * WPA2/RSN).
	 */
	const u8 *wpa_ie;
	/**
	 * wpa_ie_len - length of the wpa_ie
	 */
	size_t wpa_ie_len;

	/* The selected pairwise/group cipher and key management
	 * suites. These are usually ignored if @wpa_ie is used. */
	wpa_cipher pairwise_suite;
	wpa_cipher group_suite;
	wpa_key_mgmt key_mgmt_suite;

	/**
	 * auth_alg - Allowed authentication algorithms
	 * Bit field of AUTH_ALG_*
	 */
	int auth_alg;

	/**
	 * mode - Operation mode (infra/ibss) IEEE80211_MODE_*
	 */
	int mode;

	/**
	 * wep_key - WEP keys for static WEP configuration
	 */
	const u8 *wep_key[4];

	/**
	 * wep_key_len - WEP key length for static WEP configuration
	 */
	size_t wep_key_len[4];

	/**
	 * wep_tx_keyidx - WEP TX key index for static WEP configuration
	 */
	int wep_tx_keyidx;

	/**
	 * mgmt_frame_protection - IEEE 802.11w management frame protection
	 */
	enum {
		NO_MGMT_FRAME_PROTECTION,
		MGMT_FRAME_PROTECTION_OPTIONAL,
		MGMT_FRAME_PROTECTION_REQUIRED
	} mgmt_frame_protection;
};

/**
 * struct wpa_driver_capa - Driver capability information
 */
struct wpa_driver_capa {
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA		0x00000001
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2		0x00000002
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK	0x00000004
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK	0x00000008
#define WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE	0x00000010
#define WPA_DRIVER_CAPA_KEY_MGMT_WAPI_PSK	0x00000020
#define WPA_DRIVER_CAPA_KEY_MGMT_WAPI_CERT	0x00000040
	unsigned int key_mgmt;

#define WPA_DRIVER_CAPA_ENC_WEP40	0x00000001
#define WPA_DRIVER_CAPA_ENC_WEP104	0x00000002
#define WPA_DRIVER_CAPA_ENC_TKIP	0x00000004
#define WPA_DRIVER_CAPA_ENC_CCMP	0x00000008
#define WPA_DRIVER_CAPA_ENC_SMS4	0x00000010
	unsigned int enc;

#define WPA_DRIVER_AUTH_OPEN		0x00000001
#define WPA_DRIVER_AUTH_SHARED		0x00000002
#define WPA_DRIVER_AUTH_LEAP		0x00000004
	unsigned int auth;

/* Driver generated WPA/RSN IE */
#define WPA_DRIVER_FLAGS_DRIVER_IE	0x00000001
#define WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC 0x00000002
#define WPA_DRIVER_FLAGS_USER_SPACE_MLME 0x00000004
	unsigned int flags;
};


#define WPA_CHAN_W_SCAN 0x00000001
#define WPA_CHAN_W_ACTIVE_SCAN 0x00000002
#define WPA_CHAN_W_IBSS 0x00000004

struct wpa_channel_data {
	short chan; /* channel number (IEEE 802.11) */
	short freq; /* frequency in MHz */
	int flag; /* flag for user space use (WPA_CHAN_*) */
};

#define WPA_RATE_ERP 0x00000001
#define WPA_RATE_BASIC 0x00000002
#define WPA_RATE_PREAMBLE2 0x00000004
#define WPA_RATE_SUPPORTED 0x00000010
#define WPA_RATE_OFDM 0x00000020
#define WPA_RATE_CCK 0x00000040
#define WPA_RATE_MANDATORY 0x00000100

struct wpa_rate_data {
	int rate; /* rate in 100 kbps */
	int flags; /* WPA_RATE_ flags */
};

typedef enum {
	WPA_MODE_IEEE80211B,
	WPA_MODE_IEEE80211G,
	WPA_MODE_IEEE80211A,
	NUM_WPA_MODES
} wpa_hw_mode;

struct wpa_hw_modes {
	wpa_hw_mode mode;
	int num_channels;
	struct wpa_channel_data *channels;
	int num_rates;
	struct wpa_rate_data *rates;
};


struct ieee80211_rx_status {
        int channel;
        int ssi;
};


/**
 * struct wpa_driver_ops - Driver interface API definition
 *
 * This structure defines the API that each driver interface needs to implement
 * for core wpa_supplicant code. All driver specific functionality is captured
 * in this wrapper.
 */
struct wpa_driver_ops {
	/** Name of the driver interface */
	const char *name;
	/** One line description of the driver interface */
	const char *desc;

	/**
	 * get_bssid - Get the current BSSID
	 * @priv: private driver interface data
	 * @bssid: buffer for BSSID (ETH_ALEN = 6 bytes)
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Query kernel driver for the current BSSID and copy it to bssid.
	 * Setting bssid to 00:00:00:00:00:00 is recommended if the STA is not
	 * associated.
	 */
	int (*get_bssid)(void *priv, u8 *bssid);

	/**
	 * get_ssid - Get the current SSID
	 * @priv: private driver interface data
	 * @ssid: buffer for SSID (at least 32 bytes)
	 *
	 * Returns: Length of the SSID on success, -1 on failure
	 *
	 * Query kernel driver for the current SSID and copy it to ssid.
	 * Returning zero is recommended if the STA is not associated.
	 *
	 * Note: SSID is an array of octets, i.e., it is not nul terminated and
	 * can, at least in theory, contain control characters (including nul)
	 * and as such, should be processed as binary data, not a printable
	 * string.
	 */
	int (*get_ssid)(void *priv, u8 *ssid);

	/**
	 * set_wpa - Enable/disable WPA support (OBSOLETE)
	 * @priv: private driver interface data
	 * @enabled: 1 = enable, 0 = disable
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Note: This function is included for backwards compatibility. This is
	 * called only just after init and just before deinit, so these
	 * functions can be used to implement same functionality and the driver
	 * interface need not define this function.
	 *
	 * Configure the kernel driver to enable/disable WPA support. This may
	 * be empty function, if WPA support is always enabled. Common
	 * configuration items are WPA IE (clearing it when WPA support is
	 * disabled), Privacy flag configuration for capability field (note:
	 * this the value need to set in associate handler to allow plaintext
	 * mode to be used) when trying to associate with, roaming mode (can
	 * allow wpa_supplicant to control roaming if ap_scan=1 is used;
	 * however, drivers can also implement roaming if desired, especially
	 * ap_scan=2 mode is used for this).
	 */
	int (*set_wpa)(void *priv, int enabled);

	/**
	 * set_key - Configure encryption key
	 * @priv: private driver interface data
	 * @alg: encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
	 *	%WPA_ALG_TKIP, %WPA_ALG_CCMP, %WPA_ALG_IGTK, %WPA_ALG_DHV);
	 *	%WPA_ALG_NONE clears the key.
	 * @addr: address of the peer STA or ff:ff:ff:ff:ff:ff for
	 *	broadcast/default keys
	 * @key_idx: key index (0..3), usually 0 for unicast keys; 0..4095 for
	 *	IGTK
	 * @set_tx: configure this key as the default Tx key (only used when
	 *	driver does not support separate unicast/individual key
	 * @seq: sequence number/packet number, seq_len octets, the next
	 *	packet number to be used for in replay protection; configured
	 *	for Rx keys (in most cases, this is only used with broadcast
	 *	keys and set to zero for unicast keys)
	 * @seq_len: length of the seq, depends on the algorithm:
	 *	TKIP: 6 octets, CCMP: 6 octets, IGTK: 6 octets
	 * @key: key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
	 *	8-byte Rx Mic Key
	 * @key_len: length of the key buffer in octets (WEP: 5 or 13,
	 *	TKIP: 32, CCMP: 16, IGTK: 16, DHV: 16)
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Configure the given key for the kernel driver. If the driver
	 * supports separate individual keys (4 default keys + 1 individual),
	 * addr can be used to determine whether the key is default or
	 * individual. If only 4 keys are supported, the default key with key
	 * index 0 is used as the individual key. STA must be configured to use
	 * it as the default Tx key (set_tx is set) and accept Rx for all the
	 * key indexes. In most cases, WPA uses only key indexes 1 and 2 for
	 * broadcast keys, so key index 0 is available for this kind of
	 * configuration.
	 *
	 * Please note that TKIP keys include separate TX and RX MIC keys and
	 * some drivers may expect them in different order than wpa_supplicant
	 * is using. If the TX/RX keys are swapped, all TKIP encrypted packets
	 * will tricker Michael MIC errors. This can be fixed by changing the
	 * order of MIC keys by swapping te bytes 16..23 and 24..31 of the key
	 * in driver_*.c set_key() implementation, see driver_ndis.c for an
	 * example on how this can be done.
	 */
	int (*set_key)(void *priv, wpa_alg alg, const u8 *addr,
		       int key_idx, int set_tx, const u8 *seq, size_t seq_len,
		       const u8 *key, size_t key_len);

	/**
	 * init - Initialize driver interface
	 * @ctx: context to be used when calling wpa_supplicant functions,
	 * e.g., wpa_supplicant_event()
	 * @ifname: interface name, e.g., wlan0
	 *
	 * Returns: Pointer to private data, %NULL on failure
	 *
	 * Initialize driver interface, including event processing for kernel
	 * driver events (e.g., associated, scan results, Michael MIC failure).
	 * This function can allocate a private configuration data area for
	 * @ctx, file descriptor, interface name, etc. information that may be
	 * needed in future driver operations. If this is not used, non-NULL
	 * value will need to be returned because %NULL is used to indicate
	 * failure. The returned value will be used as 'void *priv' data for
	 * all other driver_ops functions.
	 *
	 * The main event loop (eloop.c) of wpa_supplicant can be used to
	 * register callback for read sockets (eloop_register_read_sock()).
	 *
	 * See wpa_supplicant.h for more information about events and
	 * wpa_supplicant_event() function.
	 */
	void * (*init)(void *ctx, const char *ifname);

	/**
	 * deinit - Deinitialize driver interface
	 * @priv: private driver interface data from init()
	 *
	 * Shut down driver interface and processing of driver events. Free
	 * private data buffer if one was allocated in init() handler.
	 */
	void (*deinit)(void *priv);

	/**
	 * set_param - Set driver configuration parameters
	 * @priv: private driver interface data from init()
	 * @param: driver specific configuration parameters
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Optional handler for notifying driver interface about configuration
	 * parameters (driver_param).
	 */
	int (*set_param)(void *priv, const char *param);

	/**
	 * set_countermeasures - Enable/disable TKIP countermeasures
	 * @priv: private driver interface data
	 * @enabled: 1 = countermeasures enabled, 0 = disabled
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Configure TKIP countermeasures. When these are enabled, the driver
	 * should drop all received and queued frames that are using TKIP.
	 */
	int (*set_countermeasures)(void *priv, int enabled);

	/**
	 * set_drop_unencrypted - Enable/disable unencrypted frame filtering
	 * @priv: private driver interface data
	 * @enabled: 1 = unencrypted Tx/Rx frames will be dropped, 0 = disabled
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Configure the driver to drop all non-EAPOL frames (both receive and
	 * transmit paths). Unencrypted EAPOL frames (ethertype 0x888e) must
	 * still be allowed for key negotiation.
	 */
	int (*set_drop_unencrypted)(void *priv, int enabled);

	/**
	 * scan - Request the driver to initiate scan
	 * @priv: private driver interface data
	 * @ssid: specific SSID to scan for (ProbeReq) or %NULL to scan for
	 *	all SSIDs (either active scan with broadcast SSID or passive
	 *	scan
	 * @ssid_len: length of the SSID
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Once the scan results are ready, the driver should report scan
	 * results event for wpa_supplicant which will eventually request the
	 * results with wpa_driver_get_scan_results().
	 */
	int (*scan)(void *priv, const u8 *ssid, size_t ssid_len);

	/**
	 * get_scan_results - Fetch the latest scan results
	 * @priv: private driver interface data
	 * @results: pointer to buffer for scan results
	 * @max_size: maximum number of entries (buffer size)
	 *
	 * Returns: Number of scan result entries used on success, -1 on
	 * failure
	 *
	 * If scan results include more than max_size BSSes, max_size will be
	 * returned and the remaining entries will not be included in the
	 * buffer.
	 */
	int (*get_scan_results)(void *priv,
				struct wpa_scan_result *results,
				size_t max_size);

	/**
	 * deauthenticate - Request driver to deauthenticate
	 * @priv: private driver interface data
	 * @addr: peer address (BSSID of the AP)
	 * @reason_code: 16-bit reason code to be sent in the deauthentication
	 *	frame
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*deauthenticate)(void *priv, const u8 *addr, int reason_code);

	/**
	 * disassociate - Request driver to disassociate
	 * @priv: private driver interface data
	 * @addr: peer address (BSSID of the AP)
	 * @reason_code: 16-bit reason code to be sent in the disassociation
	 *	frame
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*disassociate)(void *priv, const u8 *addr, int reason_code);

	/**
	 * associate - Request driver to associate
	 * @priv: private driver interface data
	 * @params: association parameters
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*associate)(void *priv,
			 struct wpa_driver_associate_params *params);

	/**
	 * set_auth_alg - Set IEEE 802.11 authentication algorithm
	 * @priv: private driver interface data
	 * @auth_alg: bit field of AUTH_ALG_*
	 *
	 * If the driver supports more than one authentication algorithm at the
	 * same time, it should configure all supported algorithms. If not, one
	 * algorithm needs to be selected arbitrarily. Open System
	 * authentication should be ok for most cases and it is recommended to
	 * be used if other options are not supported. Static WEP configuration
	 * may also use Shared Key authentication and LEAP requires its own
	 * algorithm number. For LEAP, user can make sure that only one
	 * algorithm is used at a time by configuring LEAP as the only
	 * supported EAP method. This information is also available in
	 * associate() params, so set_auth_alg may not be needed in case of
	 * most drivers.
	 *
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_auth_alg)(void *priv, int auth_alg);

	/**
	 * add_pmkid - Add PMKSA cache entry to the driver
	 * @priv: private driver interface data
	 * @bssid: BSSID for the PMKSA cache entry
	 * @pmkid: PMKID for the PMKSA cache entry
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when a new PMK is received, as a result of
	 * either normal authentication or RSN pre-authentication.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), add_pmkid() can be used to add new PMKSA cache entries
	 * in the driver. If the driver uses wpa_ie from wpa_supplicant, this
	 * driver_ops function does not need to be implemented. Likewise, if
	 * the driver does not support WPA, this function is not needed.
	 */
	int (*add_pmkid)(void *priv, const u8 *bssid, const u8 *pmkid);

	/**
	 * remove_pmkid - Remove PMKSA cache entry to the driver
	 * @priv: private driver interface data
	 * @bssid: BSSID for the PMKSA cache entry
	 * @pmkid: PMKID for the PMKSA cache entry
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when the supplicant drops a PMKSA cache
	 * entry for any reason.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), remove_pmkid() can be used to synchronize PMKSA caches
	 * between the driver and wpa_supplicant. If the driver uses wpa_ie
	 * from wpa_supplicant, this driver_ops function does not need to be
	 * implemented. Likewise, if the driver does not support WPA, this
	 * function is not needed.
	 */
	int (*remove_pmkid)(void *priv, const u8 *bssid, const u8 *pmkid);

	/**
	 * flush_pmkid - Flush PMKSA cache
	 * @priv: private driver interface data
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is called when the supplicant drops all PMKSA cache
	 * entries for any reason.
	 *
	 * If the driver generates RSN IE, i.e., it does not use wpa_ie in
	 * associate(), remove_pmkid() can be used to synchronize PMKSA caches
	 * between the driver and wpa_supplicant. If the driver uses wpa_ie
	 * from wpa_supplicant, this driver_ops function does not need to be
	 * implemented. Likewise, if the driver does not support WPA, this
	 * function is not needed.
	 */
	int (*flush_pmkid)(void *priv);

	/**
	 * flush_pmkid - Flush PMKSA cache
	 * @priv: private driver interface data
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * Get driver/firmware/hardware capabilities.
	 */
	int (*get_capa)(void *priv, struct wpa_driver_capa *capa);

	/**
	 * poll - Poll driver for association information
	 * @priv: private driver interface data
	 *
	 * This is an option callback that can be used when the driver does not
	 * provide event mechanism for association events. This is called when
	 * receiving WPA EAPOL-Key messages that require association
	 * information. The driver interface is supposed to generate associnfo
	 * event before returning from this callback function. In addition, the
	 * driver interface should generate an association event after having
	 * sent out associnfo.
	 */
	void (*poll)(void *priv);

	/**
	 * get_ifname - Get interface name
	 * @priv: private driver interface data
	 *
	 * Returns: Pointer to the interface name. This can differ from the
	 * interface name used in init() call.
	 *
	 * This optional function can be used to allow the driver interface to
	 * replace the interface name with something else, e.g., based on an
	 * interface mapping from a more descriptive name.
	 */
	const char * (*get_ifname)(void *priv);

	/**
	 * get_mac_addr - Get own MAC address
	 * @priv: private driver interface data
	 *
	 * Returns: Pointer to own MAC address or %NULL on failure
	 *
	 * This optional function can be used to get the own MAC address of the
	 * device from the driver interface code. This is only needed if the
	 * l2_packet implementation for the OS does not provide easy access to
	 * a MAC address. */
	const u8 * (*get_mac_addr)(void *priv);

	/**
	 * send_eapol - Optional function for sending EAPOL packets
	 * @priv: private driver interface data
	 * @dest: Destination MAC address
	 * @proto: Ethertype
	 * @data: EAPOL packet starting with IEEE 802.1X header
	 * @data_len: Size of the EAPOL packet
	 *
	 * Returns: 0 on success, -1 on failure
	 *
	 * This optional function can be used to override l2_packet operations
	 * with driver specific functionality. If this function pointer is set,
	 * l2_packet module is not used at all and the driver interface code is
	 * responsible for receiving and sending all EAPOL packets. The
	 * received EAPOL packets are sent to core code by calling
	 * wpa_supplicant_rx_eapol(). The driver interface is required to
	 * implement get_mac_addr() handler if send_eapol() is used.
	 */
	int (*send_eapol)(void *priv, const u8 *dest, u16 proto,
			  const u8 *data, size_t data_len);

	/**
	 * set_operstate - Sets device operating state to DORMANT or UP
	 * @priv: private driver interface data
	 * @state: 0 = dormant, 1 = up
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function that can be used on operating systems
	 * that support a concept of controlling network device state from user
	 * space applications. This function, if set, gets called with
	 * state = 1 when authentication has been completed and with state = 0
	 * when connection is lost.
	 */
	int (*set_operstate)(void *priv, int state);

	/**
	 * mlme_setprotection - MLME-SETPROTECTION.request primitive
	 * @priv: Private driver interface data
	 * @addr: Address of the station for which to set protection (may be
	 * %NULL for group keys)
	 * @protect_type: MLME_SETPROTECTION_PROTECT_TYPE_*
	 * @key_type: MLME_SETPROTECTION_KEY_TYPE_*
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is an optional function that can be used to set the driver to
	 * require protection for Tx and/or Rx frames. This uses the layer
	 * interface defined in IEEE 802.11i-2004 clause 10.3.22.1
	 * (MLME-SETPROTECTION.request). Many drivers do not use explicit
	 * set protection operation; instead, they set protection implicitly
	 * based on configured keys.
	 */
	int (*mlme_setprotection)(void *priv, const u8 *addr, int protect_type,
				  int key_type);

	/**
	 * get_hw_feature_data - Get hardware support data (channels and rates)
	 * @priv: Private driver interface data
	 * @num_modes: Variable for returning the number of returned modes
	 * flags: Variable for returning hardware feature flags
	 * Returns: Pointer to allocated hardware data on success or %NULL on
	 * failure. Caller is responsible for freeing this.
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	struct wpa_hw_modes * (*get_hw_feature_data)(void *priv,
						     u16 *num_modes,
						     u16 *flags);

	/**
	 * set_channel - Set channel
	 * @priv: Private driver interface data
	 * @phymode: WPA_MODE_IEEE80211B, ..
	 * @chan: IEEE 802.11 channel number
	 * @freq: Frequency of the channel in MHz
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_channel)(void *priv, wpa_hw_mode phymode, int chan,
			   int freq);

	/**
	 * set_ssid - Set SSID
	 * @priv: Private driver interface data
	 * @ssid: SSID
	 * @ssid_len: SSID length
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_ssid)(void *priv, const u8 *ssid, size_t ssid_len);

	/**
	 * set_bssid - Set BSSID
	 * @priv: Private driver interface data
	 * @bssid: BSSID
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*set_bssid)(void *priv, const u8 *bssid);

	/**
	 * send_mlme - Send management frame from MLME
	 * @priv: Private driver interface data
	 * @data: IEEE 802.11 management frame with IEEE 802.11 header
	 * @data_len: Size of the management frame
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	int (*send_mlme)(void *priv, const u8 *data, size_t data_len);

	/**
	 * mlme_add_sta - Add a STA entry into the driver/netstack
	 * @priv: Private driver interface data
	 * @addr: MAC address of the STA (e.g., BSSID of the AP)
	 * @supp_rates: Supported rate set (from (Re)AssocResp); in IEEE 802.11
	 * format (one octet per rate, 1 = 0.5 Mbps)
	 * @supp_rates_len: Number of entries in supp_rates
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant. When the MLME code
	 * completes association with an AP, this function is called to
	 * configure the driver/netstack with a STA entry for data frame
	 * processing (TX rate control, encryption/decryption).
	 */
	 int (*mlme_add_sta)(void *priv, const u8 *addr, const u8 *supp_rates,
			     size_t supp_rates_len);

	/**
	 * mlme_remove_sta - Remove a STA entry from the driver/netstack
	 * @priv: Private driver interface data
	 * @addr: MAC address of the STA (e.g., BSSID of the AP)
	 * Returns: 0 on success, -1 on failure
	 *
	 * This function is only needed for drivers that export MLME
	 * (management frame processing) to wpa_supplicant.
	 */
	 int (*mlme_remove_sta)(void *priv, const u8 *addr);

/**Nanoradio start**/
	/**
	 * notify_state_change - Notify driver about pending state change
	 * @priv: Private driver interface data
	 * @new_state: new supplicant state
	 * @old_state: new supplicant state
	 * Returns: nothing
	 */
	void (*notify_state_change)(void *priv,
				    wpa_states new_state,
				    wpa_states old_state);
/**Nanoradio end**/
};

#endif /* DRIVER_H */

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
