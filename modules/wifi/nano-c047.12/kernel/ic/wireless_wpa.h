#ifndef __wireless_wpa_h__
#define __wireless_wpa_h__

/*
 * Definitions required for WPA from Wireless extensions 18 
 */

#define SIOCSIWGENIE    0x8B30
#define SIOCGIWGENIE    0x8B31
#define SIOCSIWMLME     0x8B16
#define SIOCSIWAUTH     0x8B32
#define SIOCGIWAUTH     0x8B33
#define SIOCSIWENCODEEXT 0x8B34
#define SIOCGIWENCODEEXT 0x8B35
#define SIOCSIWPMKSA    0x8B36

#define IWEVGENIE       0x8C05
#define IWEVMICHAELMICFAILURE 0x8C06
#define IWEVASSOCREQIE  0x8C07
#define IWEVASSOCRESPIE 0x8C08
#define IWEVPMKIDCAND   0x8C09

#define IW_GENERIC_IE_MAX	1024

#define IW_MLME_DEAUTH          0
#define IW_MLME_DISASSOC        1

#define IW_AUTH_INDEX           0x0FFF
#define IW_AUTH_FLAGS           0xF000

#define IW_AUTH_WPA_VERSION             0
#define IW_AUTH_CIPHER_PAIRWISE         1
#define IW_AUTH_CIPHER_GROUP            2
#define IW_AUTH_KEY_MGMT                3
#define IW_AUTH_TKIP_COUNTERMEASURES    4
#define IW_AUTH_DROP_UNENCRYPTED        5
#define IW_AUTH_80211_AUTH_ALG          6
#define IW_AUTH_WPA_ENABLED             7
#define IW_AUTH_RX_UNENCRYPTED_EAPOL    8
#define IW_AUTH_ROAMING_CONTROL         9
#define IW_AUTH_PRIVACY_INVOKED         10

#define IW_AUTH_WPA_VERSION_DISABLED    1
#define IW_AUTH_WPA_VERSION_WPA         2
#define IW_AUTH_WPA_VERSION_WPA2        4

#define IW_AUTH_CIPHER_NONE     1
#define IW_AUTH_CIPHER_WEP40    2
#define IW_AUTH_CIPHER_TKIP     4
#define IW_AUTH_CIPHER_CCMP     8
#define IW_AUTH_CIPHER_WEP104   16

#define IW_AUTH_KEY_MGMT_802_1X 1
#define IW_AUTH_KEY_MGMT_PSK    2

#define IW_AUTH_ALG_OPEN_SYSTEM 1
#define IW_AUTH_ALG_SHARED_KEY  2
#define IW_AUTH_ALG_LEAP        4

#define IW_AUTH_ROAMING_ENABLE  0
#define IW_AUTH_ROAMING_DISABLE 1

#define IW_ENCODE_SEQ_MAX_SIZE  8

#define IW_ENCODE_ALG_NONE      0
#define IW_ENCODE_ALG_WEP       1
#define IW_ENCODE_ALG_TKIP      2
#define IW_ENCODE_ALG_CCMP      3

#define IW_ENCODE_EXT_TX_SEQ_VALID      1
#define IW_ENCODE_EXT_RX_SEQ_VALID      2
#define IW_ENCODE_EXT_GROUP_KEY         4
#define IW_ENCODE_EXT_SET_TX_KEY        8

#define IW_MICFAILURE_KEY_ID    3
#define IW_MICFAILURE_GROUP     4
#define IW_MICFAILURE_PAIRWISE  8
#define IW_MICFAILURE_STAKEY    16
#define IW_MICFAILURE_COUNT     96

#define IW_ENC_CAPA_WPA         1
#define IW_ENC_CAPA_WPA2        2
#define IW_ENC_CAPA_CIPHER_TKIP 4
#define IW_ENC_CAPA_CIPHER_CCMP 8

struct iw_encode_ext {
   uint32_t        ext_flags;
   uint8_t         tx_seq[IW_ENCODE_SEQ_MAX_SIZE];
   uint8_t         rx_seq[IW_ENCODE_SEQ_MAX_SIZE];
   struct sockaddr addr;
   uint16_t        alg;
   uint16_t        key_len;
   uint8_t         key[0];
};

struct iw_mlme {
   uint16_t        cmd;
   uint16_t        reason_code;
   struct sockaddr addr;
};

#define IW_PMKSA_ADD            1
#define IW_PMKSA_REMOVE         2
#define IW_PMKSA_FLUSH          3

#define IW_PMKID_LEN    16

struct iw_pmksa {
   uint32_t        cmd;
   struct sockaddr bssid;
   uint8_t         pmkid[IW_PMKID_LEN];
};

struct iw_michaelmicfailure {
   uint32_t        flags;
   struct sockaddr src_addr;
   uint8_t         tsc[IW_ENCODE_SEQ_MAX_SIZE];
};

#define IW_PMKID_CAND_PREAUTH   1
struct iw_pmkid_cand {
   uint32_t        flags;
   uint32_t        index;
   struct sockaddr bssid;
};

#endif /* __wireless_wpa_h__ */
