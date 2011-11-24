/*
 * WPA Supplicant / Network configuration structures
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

#ifndef CONFIG_SSID_H
#define CONFIG_SSID_H

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#define WPA_CIPHER_NONE BIT(0)
#define WPA_CIPHER_WEP40 BIT(1)
#define WPA_CIPHER_WEP104 BIT(2)
#define WPA_CIPHER_TKIP BIT(3)
#define WPA_CIPHER_CCMP BIT(4)
#ifdef CONFIG_IEEE80211W
#define WPA_CIPHER_AES_128_CMAC BIT(5)
#endif /* CONFIG_IEEE80211W */

#define WAPI_CIPHER_SMS4 BIT(6)
#define WPA_KEY_MGMT_IEEE8021X BIT(0)
#define WPA_KEY_MGMT_PSK BIT(1)
#define WPA_KEY_MGMT_NONE BIT(2)
#define WPA_KEY_MGMT_IEEE8021X_NO_WPA BIT(3)
#define WPA_KEY_MGMT_WPA_NONE BIT(4)
#define WAPI_KEY_MGMT_PSK BIT(5)
#define WAPI_KEY_MGMT_CERT BIT(6)

#define WPA_PROTO_WPA BIT(0)
#define WPA_PROTO_RSN BIT(1)

#define WAPI_PROTO BIT(2)
#define WPA_AUTH_ALG_OPEN BIT(0)
#define WPA_AUTH_ALG_SHARED BIT(1)
#define WPA_AUTH_ALG_LEAP BIT(2)

#define MAX_SSID_LEN 32
#define PMK_LEN 32
#define EAP_PSK_LEN_MIN 16
#define EAP_PSK_LEN_MAX 32


#define DEFAULT_EAP_WORKAROUND ((unsigned int) -1)
#define DEFAULT_EAPOL_FLAGS (EAPOL_FLAG_REQUIRE_KEY_UNICAST | \
			     EAPOL_FLAG_REQUIRE_KEY_BROADCAST)
#define DEFAULT_PROTO (WPA_PROTO_WPA | WPA_PROTO_RSN)
#define DEFAULT_KEY_MGMT (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_IEEE8021X)
#define DEFAULT_PAIRWISE (WPA_CIPHER_CCMP | WPA_CIPHER_TKIP)
#define DEFAULT_GROUP (WPA_CIPHER_CCMP | WPA_CIPHER_TKIP | \
		       WPA_CIPHER_WEP104 | WPA_CIPHER_WEP40)
#define DEFAULT_FRAGMENT_SIZE 1398

/**
 * struct wpa_ssid - Network configuration data
 *
 * This structure includes all the configuration variables for a network. This
 * data is included in the per-interface configuration data as an element of
 * the network list, struct wpa_config::ssid. Each network block in the
 * configuration is mapped to a struct wpa_ssid instance.
 */
struct wpa_ssid {
	/**
	 * next - Next network in global list
	 *
	 * This pointer can be used to iterate over all networks. The head of
	 * this list is stored in the ssid field of struct wpa_config.
	 */
	struct wpa_ssid *next;

	/**
	 * pnext - Next network in per-priority list
	 *
	 * This pointer can be used to iterate over all networks in the same
	 * priority class. The heads of these list are stored in the pssid
	 * fields of struct wpa_config.
	 */
	struct wpa_ssid *pnext;

	/**
	 * id - Unique id for the network
	 *
	 * This identifier is used as a unique identifier for each network
	 * block when using the control interface. Each network is allocated an
	 * id when it is being created, either when reading the configuration
	 * file or when a new network is added through the control interface.
	 */
	int id;

	/**
	 * priority - Priority group
	 *
	 * By default, all networks will get same priority group (0). If some
	 * of the networks are more desirable, this field can be used to change
	 * the order in which wpa_supplicant goes through the networks when
	 * selecting a BSS. The priority groups will be iterated in decreasing
	 * priority (i.e., the larger the priority value, the sooner the
	 * network is matched against the scan results). Within each priority
	 * group, networks will be selected based on security policy, signal
	 * strength, etc.
	 *
	 * Please note that AP scanning with scan_ssid=1 and ap_scan=2 mode are
	 * not using this priority to select the order for scanning. Instead,
	 * they try the networks in the order that used in the configuration
	 * file.
	 */
	int priority;

	/**
	 * ssid - Service set identifier (network name)
	 *
	 * This is the SSID for the network. For wireless interfaces, this is
	 * used to select which network will be used. If set to %NULL (or
	 * ssid_len=0), any SSID can be used. For wired interfaces, this must
	 * be set to %NULL. Note: SSID may contain any characters, even nul
	 * (ASCII 0) and as such, this should not be assumed to be a nul
	 * terminated string. ssid_len defines how many characters are valid
	 * and the ssid field is not guaranteed to be nul terminated.
	 */
	u8 *ssid;

	/**
	 * ssid_len - Length of the SSID
	 */
	size_t ssid_len;

	/**
	 * bssid - BSSID
	 *
	 * If set, this network block is used only when associating with the AP
	 * using the configured BSSID
	 */
	u8 bssid[ETH_ALEN];

	/**
	 * bssid_set - Whether BSSID is configured for this network
	 */
	int bssid_set;

	/**
	 * psk - WPA pre-shared key (256 bits)
	 */
	u8 psk[PMK_LEN];

	/**
	 * psk_set - Whether PSK field is configured
	 */
	int psk_set;

	/**
	 * passphrase - WPA ASCII passphrase
	 *
	 * If this is set, psk will be generated using the SSID and passphrase
	 * configured for the network. ASCII passphrase must be between 8 and
	 * 63 characters (inclusive).
	 */
	char *passphrase;

	/**
	 * pairwise_cipher - Bitfield of allowed pairwise ciphers, WPA_CIPHER_*
	 */
	int pairwise_cipher;

	/**
	 * group_cipher - Bitfield of allowed group ciphers, WPA_CIPHER_*
	 */
	int group_cipher;

	/**
	 * key_mgmt - Bitfield of allowed key management protocols
	 *
	 * WPA_KEY_MGMT_*
	 */
	int key_mgmt;

	/**
	 * proto - Bitfield of allowed protocols, WPA_PROTO_*
	 */
	int proto;

	/**
	 * auth_alg -  Bitfield of allowed authentication algorithms
	 *
	 * WPA_AUTH_ALG_*
	 */
	int auth_alg;

	/**
	 * scan_ssid - Scan this SSID with Probe Requests
	 *
	 * scan_ssid can be used to scan for APs using hidden SSIDs.
	 * Note: Many drivers do not support this. ap_mode=2 can be used with
	 * such drivers to use hidden SSIDs.
	 */
	int scan_ssid;
#ifdef EAP_WSC
        /**                                                                                                                              
        * use_wps - Whether Wi-Fi Protected Setup (WPS) will be used to                                                                       
        *           connect to the AP or not. Also indicates which WPS                                                                        
        *           method will be used.                                                                                                      
        *                                                                                                                                     
        * 0: WPS will not be used                                                                                                             
        * 1: WPS PBC method will be used                                                                                                      
        * 2: WPS PIN Config method will be used                                                                                               
        *                                                                                                                                
        */
       int use_wps;
#endif

#ifdef IEEE8021X_EAPOL

	/**
	 * identity - EAP Identity
	 */
	u8 *identity;

	/**
	 * identity_len - EAP Identity length
	 */
	size_t identity_len;

	/**
	 * anonymous_identity -  Anonymous EAP Identity
	 *
	 * This field is used for unencrypted use with EAP types that support
	 * different tunnelled identity, e.g., EAP-TTLS, in order to reveal the
	 * real identity (identity field) only to the authentication server.
	 */
	u8 *anonymous_identity;

	/**
	 * anonymous_identity_len - Length of anonymous_identity
	 */
	size_t anonymous_identity_len;

	/**
	 * eappsk - EAP-PSK/PAX/SAKE pre-shared key
	 */
	u8 *eappsk;

	/**
	 * eappsk_len - EAP-PSK/PAX/SAKE pre-shared key length
	 *
	 * This field is always 16 for the current version of EAP-PSK/PAX and
	 * 32 for EAP-SAKE.
	 */
	size_t eappsk_len;

	/**
	 * nai - User NAI (for EAP-PSK/PAX/SAKE)
	 */
	u8 *nai;

	/**
	 * nai_len - Length of nai field
	 */
	size_t nai_len;

	/**
	 * password - Password string for EAP
	 */
	u8 *password;

	/**
	 * password_len - Length of password field
	 */
	size_t password_len;

	/**
	 * ca_cert - File path to CA certificate file (PEM/DER)
	 *
	 * This file can have one or more trusted CA certificates. If ca_cert
	 * and ca_path are not included, server certificate will not be
	 * verified. This is insecure and a trusted CA certificate should
	 * always be configured when using EAP-TLS/TTLS/PEAP. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 *
	 * On Windows, trusted CA certificates can be loaded from the system
	 * certificate store by setting this to cert_store://<name>, e.g.,
	 * ca_cert="cert_store://CA" or ca_cert="cert_store://ROOT".
	 * Note that when running wpa_supplicant as an application, the user
	 * certificate store (My user account) is used, whereas computer store
	 * (Computer account) is used when running wpasvc as a service.
	 */
	u8 *ca_cert;

	/**
	 * ca_path - Directory path for CA certificate files (PEM)
	 *
	 * This path may contain multiple CA certificates in OpenSSL format.
	 * Common use for this is to point to system trusted CA list which is
	 * often installed into directory like /etc/ssl/certs. If configured,
	 * these certificates are added to the list of trusted CAs. ca_cert
	 * may also be included in that case, but it is not required.
	 */
	u8 *ca_path;

	/**
	 * client_cert - File path to client certificate file (PEM/DER)
	 *
	 * This field is used with EAP method that use TLS authentication.
	 * Usually, this is only configured for EAP-TLS, even though this could
	 * in theory be used with EAP-TTLS and EAP-PEAP, too. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *client_cert;

	/**
	 * private_key - File path to client private key file (PEM/DER/PFX)
	 *
	 * When PKCS#12/PFX file (.p12/.pfx) is used, client_cert should be
	 * commented out. Both the private key and certificate will be read
	 * from the PKCS#12 file in this case. Full path to the file should be
	 * used since working directory may change when wpa_supplicant is run
	 * in the background.
	 *
	 * Windows certificate store can be used by leaving client_cert out and
	 * configuring private_key in one of the following formats:
	 *
	 * cert://substring_to_match
	 *
	 * hash://certificate_thumbprint_in_hex
	 *
	 * For example: private_key="hash://63093aa9c47f56ae88334c7b65a4"
	 *
	 * Note that when running wpa_supplicant as an application, the user
	 * certificate store (My user account) is used, whereas computer store
	 * (Computer account) is used when running wpasvc as a service.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *private_key;

	/**
	 * private_key_passwd - Password for private key file
	 *
	 * If left out, this will be asked through control interface.
	 */
	u8 *private_key_passwd;

	/**
	 * dh_file - File path to DH/DSA parameters file (in PEM format)
	 *
	 * This is an optional configuration file for setting parameters for an
	 * ephemeral DH key exchange. In most cases, the default RSA
	 * authentication does not use this configuration. However, it is
	 * possible setup RSA to use ephemeral DH key exchange. In addition,
	 * ciphers with DSA keys always use ephemeral DH keys. This can be used
	 * to achieve forward secrecy. If the file is in DSA parameters format,
	 * it will be automatically converted into DH params. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *dh_file;

	/**
	 * subject_match - Constraint for server certificate subject
	 *
	 * This substring is matched against the subject of the authentication
	 * server certificate. If this string is set, the server sertificate is
	 * only accepted if it contains this string in the subject. The subject
	 * string is in following format:
	 *
	 * /C=US/ST=CA/L=San Francisco/CN=Test AS/emailAddress=as@n.example.com
	 */
	u8 *subject_match;

	/**
	 * altsubject_match - Constraint for server certificate alt. subject
	 *
	 * Semicolon separated string of entries to be matched against the
	 * alternative subject name of the authentication server certificate.
	 * If this string is set, the server sertificate is only accepted if it
	 * contains one of the entries in an alternative subject name
	 * extension.
	 *
	 * altSubjectName string is in following format: TYPE:VALUE
	 *
	 * Example: EMAIL:server@example.com
	 * Example: DNS:server.example.com;DNS:server2.example.com
	 *
	 * Following types are supported: EMAIL, DNS, URI
	 */
	u8 *altsubject_match;

	/**
	 * ca_cert2 - File path to CA certificate file (PEM/DER) (Phase 2)
	 *
	 * This file can have one or more trusted CA certificates. If ca_cert2
	 * and ca_path2 are not included, server certificate will not be
	 * verified. This is insecure and a trusted CA certificate should
	 * always be configured. Full path to the file should be used since
	 * working directory may change when wpa_supplicant is run in the
	 * background.
	 *
	 * This field is like ca_cert, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *ca_cert2;

	/**
	 * ca_path2 - Directory path for CA certificate files (PEM) (Phase 2)
	 *
	 * This path may contain multiple CA certificates in OpenSSL format.
	 * Common use for this is to point to system trusted CA list which is
	 * often installed into directory like /etc/ssl/certs. If configured,
	 * these certificates are added to the list of trusted CAs. ca_cert
	 * may also be included in that case, but it is not required.
	 *
	 * This field is like ca_path, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *ca_path2;

	/**
	 * client_cert2 - File path to client certificate file
	 *
	 * This field is like client_cert, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *client_cert2;

	/**
	 * private_key2 - File path to client private key file
	 *
	 * This field is like private_key, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *private_key2;

	/**
	 * private_key2_passwd -  Password for private key file
	 *
	 * This field is like private_key_passwd, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *private_key2_passwd;

	/**
	 * dh_file2 - File path to DH/DSA parameters file (in PEM format)
	 *
	 * This field is like dh_file, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication. Full path to the
	 * file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 *
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	u8 *dh_file2;

	/**
	 * subject_match2 - Constraint for server certificate subject
	 *
	 * This field is like subject_match, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *subject_match2;

	/**
	 * altsubject_match2 - Constraint for server certificate alt. subject
	 *
	 * This field is like altsubject_match, but used for phase 2 (inside
	 * EAP-TTLS/PEAP/FAST tunnel) authentication.
	 */
	u8 *altsubject_match2;

	/**
	 * eap_methods - Allowed EAP methods
	 *
	 * (vendor=EAP_VENDOR_IETF,method=EAP_TYPE_NONE) terminated list of
	 * allowed EAP methods or %NULL if all methods are accepted.
	 */
	struct eap_method_type *eap_methods;

	/**
	 * phase1 - Phase 1 (outer authentication) parameters
	 *
	 * String with field-value pairs, e.g., "peapver=0" or
	 * "peapver=1 peaplabel=1".
	 *
	 * 'peapver' can be used to force which PEAP version (0 or 1) is used.
	 *
	 * 'peaplabel=1' can be used to force new label, "client PEAP
	 * encryption",	to be used during key derivation when PEAPv1 or newer.
	 *
	 * Most existing PEAPv1 implementation seem to be using the old label,
	 * "client EAP encryption", and wpa_supplicant is now using that as the
	 * default value.
	 *
	 * Some servers, e.g., Radiator, may require peaplabel=1 configuration
	 * to interoperate with PEAPv1; see eap_testing.txt for more details.
	 *
	 * 'peap_outer_success=0' can be used to terminate PEAP authentication
	 * on tunneled EAP-Success. This is required with some RADIUS servers
	 * that implement draft-josefsson-pppext-eap-tls-eap-05.txt (e.g.,
	 * Lucent NavisRadius v4.4.0 with PEAP in "IETF Draft 5" mode).
	 *
	 * include_tls_length=1 can be used to force wpa_supplicant to include
	 * TLS Message Length field in all TLS messages even if they are not
	 * fragmented.
	 *
	 * sim_min_num_chal=3 can be used to configure EAP-SIM to require three
	 * challenges (by default, it accepts 2 or 3).
	 *
	 * fast_provisioning=1 can be used to enable in-line provisioning of
	 * EAP-FAST credentials (PAC)
	 */
	char *phase1;

	/**
	 * phase2 - Phase2 (inner authentication with TLS tunnel) parameters
	 *
	 * String with field-value pairs, e.g., "auth=MSCHAPV2" for EAP-PEAP or
	 * "autheap=MSCHAPV2 autheap=MD5" for EAP-TTLS.
	 */
	char phase2[24];
	/**
	 * pcsc - Parameters for PC/SC smartcard interface for USIM and GSM SIM
	 *
	 * This field is used to configure PC/SC smartcard interface.
	 * Currently, the only configuration is whether this field is %NULL (do
	 * not use PC/SC) or non-NULL (e.g., "") to enable PC/SC.
	 *
	 * This field is used for EAP-SIM and EAP-AKA.
	 */
	char *pcsc;

	/**
	 * pin - PIN for USIM, GSM SIM, and smartcards
	 *
	 * This field is used to configure PIN for SIM and smartcards for
	 * EAP-SIM and EAP-AKA. In addition, this is used with EAP-TLS if a
	 * smartcard is used for private key operations.
	 *
	 * If left out, this will be asked through control interface.
	 */
	char *pin;

	/**
	 * engine - Enable OpenSSL engine (e.g., for smartcard access)
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	int engine;

	/**
	 * engine_id - Engine ID for OpenSSL engine
	 *
	 * "opensc" to select OpenSC engine or "pkcs11" to select PKCS#11
	 * engine.
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *engine_id;

	/**
	 * key_id - Key ID for OpenSSL engine
	 *
	 * This is used if private key operations for EAP-TLS are performed
	 * using a smartcard.
	 */
	char *key_id;

#define EAPOL_FLAG_REQUIRE_KEY_UNICAST BIT(0)
#define EAPOL_FLAG_REQUIRE_KEY_BROADCAST BIT(1)
	/**
	 * eapol_flags - Bit field of IEEE 802.1X/EAPOL options (EAPOL_FLAG_*)
	 */
	int eapol_flags;

#endif /* IEEE8021X_EAPOL */

#define NUM_WEP_KEYS 4
#define MAX_WEP_KEY_LEN 16
	/**
	 * wep_key - WEP keys
	 */
	u8 wep_key[NUM_WEP_KEYS][MAX_WEP_KEY_LEN];

	/**
	 * wep_key_len - WEP key lengths
	 */
	size_t wep_key_len[NUM_WEP_KEYS];

	/**
	 * wep_tx_keyidx - Default key index for TX frames using WEP
	 */
	int wep_tx_keyidx;

	/**
	 * proactive_key_caching - Enable proactive key caching
	 *
	 * This field can be used to enable proactive key caching which is also
	 * known as opportunistic PMKSA caching for WPA2. This is disabled (0)
	 * by default. Enable by setting this to 1.
	 *
	 * Proactive key caching is used to make supplicant assume that the APs
	 * are using the same PMK and generate PMKSA cache entries without
	 * doing RSN pre-authentication. This requires support from the AP side
	 * and is normally used with wireless switches that co-locate the
	 * authenticator.
	 */
	int proactive_key_caching;

	/**
	 * mixed_cell - Whether mixed cells are allowed
	 *
	 * This option can be used to configure whether so called mixed cells,
	 * i.e., networks that use both plaintext and encryption in the same
	 * SSID, are allowed. This is disabled (0) by default. Enable by
	 * setting this to 1.
	 */
	int mixed_cell;

#ifdef IEEE8021X_EAPOL

	/**
	 * otp - One-time-password
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when OTP is entered through the control interface.
	 */
	u8 *otp;

	/**
	 * otp_len - Length of the otp field
	 */
	size_t otp_len;

	/**
	 * pending_req_identity - Whether there is a pending identity request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_identity;

	/**
	 * pending_req_password - Whether there is a pending password request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_password;

	/**
	 * pending_req_pin - Whether there is a pending PIN request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_pin;

	/**
	 * pending_req_new_password - Pending password update request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_new_password;

	/**
	 * pending_req_passphrase - Pending passphrase request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	int pending_req_passphrase;

	/**
	 * pending_req_otp - Whether there is a pending OTP request
	 *
	 * This field should not be set in configuration step. It is only used
	 * internally when control interface is used to request needed
	 * information.
	 */
	char *pending_req_otp;

	/**
	 * pending_req_otp_len - Length of the pending OTP request
	 */
	size_t pending_req_otp_len;

	/**
	 * leap - Number of EAP methods using LEAP
	 *
	 * This field should be set to 1 if LEAP is enabled. This is used to
	 * select IEEE 802.11 authentication algorithm.
	 */
	int leap;

	/**
	 * non_leap - Number of EAP methods not using LEAP
	 *
	 * This field should be set to >0 if any EAP method other than LEAP is
	 * enabled. This is used to select IEEE 802.11 authentication
	 * algorithm.
	 */
	int non_leap;

	/**
	 * eap_workaround - EAP workarounds enabled
	 *
	 * wpa_supplicant supports number of "EAP workarounds" to work around
	 * interoperability issues with incorrectly behaving authentication
	 * servers. This is recommended to be enabled by default because some
	 * of the issues are present in large number of authentication servers.
	 *
	 * Strict EAP conformance mode can be configured by disabling
	 * workarounds with eap_workaround = 0.
	 */
	unsigned int eap_workaround;

	/**
	 * pac_file - File path or blob name for the PAC entries (EAP-FAST)
	 *
	 * wpa_supplicant will need to be able to create this file and write
	 * updates to it when PAC is being provisioned or refreshed. Full path
	 * to the file should be used since working directory may change when
	 * wpa_supplicant is run in the background.
	 * Alternatively, a named configuration blob can be used by setting
	 * this to blob://<blob name>.
	 */
	char *pac_file;

#endif /* IEEE8021X_EAPOL */

	/**
	 * mode - IEEE 802.11 operation mode (Infrastucture/IBSS)
	 *
	 * 0 = infrastructure (Managed) mode, i.e., associate with an AP.
	 *
	 * 1 = IBSS (ad-hoc, peer-to-peer)
	 *
	 * Note: IBSS can only be used with key_mgmt NONE (plaintext and
	 * static WEP) and key_mgmt=WPA-NONE (fixed group key TKIP/CCMP). In
	 * addition, ap_scan has to be set to 2 for IBSS. WPA-None requires
	 * following network block options: proto=WPA, key_mgmt=WPA-NONE,
	 * pairwise=NONE, group=TKIP (or CCMP, but not both), and psk must also
	 * be set (either directly or using ASCII passphrase).
	 */
	int mode;

#ifdef IEEE8021X_EAPOL

	/**
	 * mschapv2_retry - MSCHAPv2 retry in progress
	 *
	 * This field is used internally by EAP-MSCHAPv2 and should not be set
	 * as part of configuration.
	 */
	int mschapv2_retry;

	/**
	 * new_password - New password for password update
	 *
	 * This field is used during MSCHAPv2 password update. This is normally
	 * requested from the user through the control interface and not set
	 * from configuration.
	 */
	u8 *new_password;

	/**
	 * new_password_len - Length of new_password field
	 */
	size_t new_password_len;

#endif /* IEEE8021X_EAPOL */

	/**
	 * disabled - Whether this network is currently disabled
	 *
	 * 0 = this network can be used (default).
	 * 1 = this network block is disabled (can be enabled through
	 * ctrl_iface, e.g., with wpa_cli or wpa_gui).
	 */
	int disabled;

	/**
	 * peerkey -  Whether PeerKey handshake for direct links is allowed
	 *
	 * This is only used when both RSN/WPA2 and IEEE 802.11e (QoS) are
	 * enabled.
	 *
	 * 0 = disabled (default)
	 * 1 = enabled
	 */
	int peerkey;

#ifdef IEEE8021X_EAPOL

	/**
	 * fragment_size - Maximum EAP fragment size in bytes (default 1398)
	 *
	 * This value limits the fragment size for EAP methods that support
	 * fragmentation (e.g., EAP-TLS and EAP-PEAP). This value should be set
	 * small enough to make the EAP messages fit in MTU of the network
	 * interface used for EAPOL. The default value is suitable for most
	 * cases.
	 */
	int fragment_size;

#endif /* IEEE8021X_EAPOL */

	/**
	 * id_str - Network identifier string for external scripts
	 *
	 * This value is passed to external ctrl_iface monitors in
	 * WPA_EVENT_CONNECTED event and wpa_cli sets this as WPA_ID_STR
	 * environment variable for action scripts.
	 */
	char *id_str;

#ifdef CONFIG_IEEE80211W
	/**
	 * ieee80211w - Whether management frame protection is enabled
	 *
	 * This value is used to configure policy for management frame
	 * protection (IEEE 802.11w). 0 = disabled, 1 = optional, 2 = required.
	 */
	enum {
		NO_IEEE80211W = 0,
		IEEE80211W_OPTIONAL = 1,
		IEEE80211W_REQUIRED = 2
	} ieee80211w;
#endif /* CONFIG_IEEE80211W */

	/**
	 * frequency - Channel frequency in megahertz (MHz) for IBSS
	 *
	 * This value is used to configure the initial channel for IBSS (adhoc)
	 * networks, e.g., 2412 = IEEE 802.11b/g channel 1. It is ignored in
	 * the infrastructure mode. In addition, this value is only used by the
	 * station that creates the IBSS. If an IBSS network with the
	 * configured SSID is already present, the frequency of the network
	 * will be used instead of this configured value.
	 */
	int frequency;
};

int wpa_config_allowed_eap_method(struct wpa_ssid *ssid, int vendor,
				  u32 method);

#endif /* CONFIG_SSID_H */
