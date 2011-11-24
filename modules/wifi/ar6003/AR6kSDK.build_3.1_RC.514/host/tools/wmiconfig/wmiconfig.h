/*
 * Copyright (c) 2004-2009 Atheros Communications Inc.
 * All rights reserved.
 *
 * 
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
 *
 * This file contains the definitions for wmiconfig utility
 */

#ifndef _WMI_CONFIG_H_
#define _WMI_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WMI_GET_VERSION=501,     /* something that doesn't collide with ascii */
    WMI_SET_POWER_MODE,
    WMI_SET_IBSS_PM_CAPS,
    WMI_SET_PM_PARAMS,
    WMI_SET_SCAN_PARAMS,
    WMI_SET_LISTEN_INTERVAL,
    WMI_SET_BMISS_TIME,
    WMI_SET_BSS_FILTER,
    WMI_SET_RSSI_THRESHOLDS,
    WMI_SET_CHANNEL,
    WMI_SET_SSID,
    WMI_SET_BADAP,
    WMI_DELETE_BADAP,
    WMI_CREATE_QOS,
    WMI_DELETE_QOS,
    WMI_GET_QOS_QUEUE,
    WMI_GET_TARGET_STATS,
    WMI_SET_TARGET_ERROR_REPORTING_BITMASK,
    WMI_SET_AC_PARAMS,
    WMI_SET_ASSOC_IE,
    WMI_SET_DISC_TIMEOUT,
    WMI_SET_ADHOC_BSSID,
    WMI_SET_OPT_MODE,
    WMI_OPT_SEND_FRAME,
    WMI_SET_BEACON_INT,
    WMI_SET_VOICE_PKT_SIZE,
    WMI_SET_MAX_SP,
    WMI_GET_ROAM_TBL,
    WMI_SET_ROAM_CTRL,
    WMI_SET_POWERSAVE_TIMERS,
    WMI_SET_POWERSAVE_TIMERS_PSPOLLTIMEOUT,
    WMI_SET_POWERSAVE_TIMERS_TRIGGERTIMEOUT,
    WMI_GET_POWER_MODE,
    WMI_SET_WLAN_STATE,
    WMI_GET_WLAN_STATE,
    WMI_GET_ROAM_DATA,
    WMI_SET_BT_STATUS,
    WMI_SET_BT_PARAMS,
    WMI_SET_BTCOEX_FE_ANT,
    WMI_SET_BTCOEX_COLOCATED_BT_DEV,
    WMI_SET_BTCOEX_SCO_CONFIG,
    WMI_SET_BTCOEX_A2DP_CONFIG,
    WMI_SET_BTCOEX_ACLCOEX_CONFIG,
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG,
    WMI_SET_BTCOEX_DEBUG,
    WMI_SET_BTCOEX_BT_OPERATING_STATUS,
    WMI_GET_BTCOEX_CONFIG,
    WMI_GET_BTCOEX_STATS,
    WMI_SET_RETRYLIMITS,
    WMI_START_SCAN,
    WMI_SET_FIX_RATES,
    WMI_GET_FIX_RATES,
    WMI_SET_SNR_THRESHOLDS,
    WMI_CLR_RSSISNR,
    WMI_SET_LQ_THRESHOLDS,
    WMI_SET_AUTH_MODE,
    WMI_SET_REASSOC_MODE,
    WMI_SET_LPREAMBLE,
    WMI_SET_RTS,
    WMI_SET_WMM,
#ifdef USER_KEYS
    USER_SETKEYS,
#endif
    WMI_APSD_TIM_POLICY,
    WMI_SET_ERROR_DETECTION,
    WMI_GET_HB_CHALLENGE_RESP,
    WMI_SET_TXOP,
    DIAG_ADDR,
    DIAG_DATA,
    DIAG_READ,
    DIAG_WRITE,
    WMI_GET_RD,
    WMI_SET_KEEPALIVE,
    WMI_GET_KEEPALIVE,
    WMI_SET_APPIE,
    WMI_SET_MGMT_FRM_RX_FILTER,
    WMI_DBGLOG_CFG_MODULE,
    WMI_DBGLOG_GET_DEBUG_LOGS,
    WMI_SET_HOST_SLEEP_MODE,
    WMI_SET_WOW_MODE,
    WMI_GET_WOW_LIST,
    WMI_ADD_WOW_PATTERN,
    WMI_DEL_WOW_PATTERN,
    DIAG_DUMP_CHIP_MEM,
    WMI_SET_CONNECT_CTRL_FLAGS,
    DUMP_HTC_CREDITS,
    USER_SETKEYS_INITRSC,
    WMI_SCAN_DFSCH_ACT_TIME,
    WMI_SIMULATED_APSD_TIM_POLICY,
    WMI_SET_AKMP_INFO,
    WMI_AKMP_MULTI_PMKID,
    WMI_NUM_PMKID,
    WMI_PMKID_ENTRY,
    WMI_SET_PMKID_LIST,
    WMI_GET_PMKID_LIST,
    WMI_SET_IEMASK,
    WMI_SCAN_CHANNEL_LIST,
    WMI_SET_BSS_PMKID_INFO,
    WMI_BSS_PMKID_ENTRY,
    WMI_BSSID,
    WMI_ABORT_SCAN,
    WMI_TARGET_EVENT_REPORT,
    WMI_AP_GET_STA_LIST,    /* AP mode */
    WMI_AP_HIDDEN_SSID,     /* AP mode */
    WMI_AP_SET_NUM_STA,     /* AP mode */
    WMI_AP_ACL_POLICY,      /* AP mode */
    WMI_AP_ACL_MAC_LIST1,   /* AP mode */
    WMI_AP_ACL_MAC_LIST2,   /* AP mode */
    WMI_AP_GET_ACL_LIST,    /* AP mode */
    WMI_AP_COMMIT_CONFIG,   /* AP mode */
    WMI_AP_INACT_TIME,      /* AP mode */
    WMI_AP_PROT_TIME,       /* AP mode */    
    WMI_AP_SET_MLME,        /* AP mode */
    WMI_AP_SET_COUNTRY,     /* AP mode */
    WMI_AP_GET_COUNTRY_LIST,/* AP mode */
    WMI_AP_DISABLE_REGULATORY, /* AP mode */
    WMI_AP_SET_DTIM,        /* AP mode */    
    WMI_AP_INTRA_BSS,       /* AP mode */    
    WMI_AP_INTER_BSS,       /* AP mode */    
    WMI_GET_IP,
    WMI_SET_MCAST_FILTER,
    WMI_DEL_MCAST_FILTER,
    WMI_MCAST_FILTER,
    WMI_DUMP_RCV_AGGR_STATS,
    WMI_SETUP_AGGR,
    WMI_CFG_ALLOW_AGGR,
    WMI_CFG_DELE_AGGR,
    WMI_SET_HT_CAP,
    WMI_SET_HT_OP,
    WMI_AP_GET_STAT,        /* AP mode */
    WMI_AP_CLR_STAT,        /* AP mode */
    WMI_SET_TX_SELECT_RATES,
    WMI_SCAN_MAXACT_PER_SSID,
    WMI_AP_GET_HIDDEN_SSID, /* AP mode */
    WMI_AP_GET_COUNTRY,     /* AP mode */
    WMI_AP_GET_WMODE,       /* AP mode */
    WMI_AP_GET_DTIM,        /* AP mode */
    WMI_AP_GET_BINTVL,      /* AP mode */    
    WMI_GET_RTS,
    DIAG_FETCH_TARGET_REGS,
#ifdef ATH_INCLUDE_PAL
    WMI_SEND_PAL_CMD,
    WMI_SEND_PAL_DATA,
#endif
    WMI_SET_WLAN_CONN_PRECDNCE,
    WMI_SET_AP_RATESET,     /* AP mode */
    WMI_SET_TX_WAKEUP_POLICY,
    WMI_SET_TX_NUM_FRAMES_TO_WAKEUP,
    WMI_SET_AP_PS,
    WMI_SET_AP_PS_PSTYPE,
    WMI_SET_AP_PS_IDLE_TIME,
    WMI_SET_AP_PS_PS_PERIOD,
    WMI_SET_AP_PS_SLEEP_PERIOD,
    WMI_SEND_CONNECT_CMD,
    WMI_SEND_CONNECT_CMD1,
    WMI_SEND_CONNECT_CMD2,
    WMI_SET_WOW_FILTER,
    WMI_SET_WOW_HOST_REQ_DELAY,
    WMI_SET_QOS_SUPP,
    WMI_SET_AC_VAL,
    WMI_AP_SET_DFS, /* AP mode */
    BT_HW_POWER_STATE,
    SET_BT_HW_POWER_STATE,
    GET_BT_HW_POWER_STATE,
    DIAG_DUMP_CHIP_MEM_VENUS,
    WMI_SET_TX_SGI_PARAM,
    WMI_SGI_MASK,
    WMI_PER_SGI,
    WMI_WAC_ENABLE,
    WMI_SET_WPA_OFFLOAD_STATE,
    WMI_AP_ACS_DISABLE_HI_CHANNELS,
    WMI_SET_DIVERSITY_PARAM,
    WMI_SET_EXCESS_TX_RETRY_THRES,
    WMI_FORCE_ASSERT,    
    WMI_AP_SET_GNUM_STA,
    WMI_AP_GET_GNUM_STA,
    WMI_AP_GET_NUM_STA,
    WMI_SUSPEND_DRIVER,
    WMI_RESUME_DRIVER,    
    WMI_SCAN_PROBED_SSID,
    WMI_AP_SET_APSD,
    WMI_GET_HT_CAP,
};

/*
***************************************************************************
**  How to form a regcode from CountryCode, Regulatory domain or WWR code:
**
**  WWR code is nothing but a special case of Regulatory domain.
**
**      code is of type U_INT32
**
**      Bit-31  Bit-30      Bit11-0
**        0       0          xxxx    -> Bit11-0 is a Regulatory Domain
**        0       1          xxxx    -> Bit11-0 is a WWR code
**        1       X          xxxx    -> Bit11-0 is a Country code
**  
***************************************************************************
*/
#define REGCODE_IS_CC_BITSET(x)     ((x) & 0x80000000)
#define REGCODE_GET_CODE(x)         ((x) & 0xFFF)
#define REGCODE_IS_WWR_BITSET(x)    ((x) & 0x40000000)
#ifdef ATH_INCLUDE_PAL
#define MAX_BUFFER_SIZE             1512    
#endif

typedef struct {
    A_UINT32        numPMKIDUser;          /* PMKIDs user wants to enter */
    WMI_SET_PMKID_LIST_CMD  *pmkidInfo;
} pmkidUserInfo_t;

#define KEYTYPE_PSK     1
#define KEYTYPE_PHRASE  2

typedef struct {
    char ssid[WMI_MAX_SSID_LEN];
    A_UINT8 ssid_len;
    A_UINT8 wpa;
    A_UINT8 ucipher;
    A_UINT8 mcipher;
    char    psk[64];
    A_UINT8 psk_type;
    A_UINT8 pmk[WMI_PMK_LEN];
    A_UINT8 auth;
    A_UINT8 wep_key[4][26];
    A_UINT8 def_wep_key;
} profile_t;

/*
 * Numbering from ISO 3166
 */
enum CountryCode {
    CTRY_ALBANIA              = 8,       /* Albania */
    CTRY_ALGERIA              = 12,      /* Algeria */
    CTRY_ARGENTINA            = 32,      /* Argentina */
    CTRY_ARMENIA              = 51,      /* Armenia */
    CTRY_ARUBA                = 533,     /* Aruba */
    CTRY_AUSTRALIA            = 36,      /* Australia (for STA) */
    CTRY_AUSTRALIA_AP         = 5000,    /* Australia (for AP) */
    CTRY_AUSTRIA              = 40,      /* Austria */
    CTRY_AZERBAIJAN           = 31,      /* Azerbaijan */
    CTRY_BAHRAIN              = 48,      /* Bahrain */
    CTRY_BANGLADESH           = 50,      /* Bangladesh */
    CTRY_BARBADOS             = 52,      /* Barbados */
    CTRY_BELARUS              = 112,     /* Belarus */
    CTRY_BELGIUM              = 56,      /* Belgium */
    CTRY_BELIZE               = 84,      /* Belize */
    CTRY_BOLIVIA              = 68,      /* Bolivia */
    CTRY_BOSNIA_HERZEGOWANIA  = 70,      /* Bosnia & Herzegowania */
    CTRY_BRAZIL               = 76,      /* Brazil */
    CTRY_BRUNEI_DARUSSALAM    = 96,      /* Brunei Darussalam */
    CTRY_BULGARIA             = 100,     /* Bulgaria */
    CTRY_CAMBODIA             = 116,     /* Cambodia */
    CTRY_CANADA               = 124,     /* Canada (for STA) */
    CTRY_CANADA_AP            = 5001,    /* Canada (for AP) */
    CTRY_CHILE                = 152,     /* Chile */
    CTRY_CHINA                = 156,     /* People's Republic of China */
    CTRY_COLOMBIA             = 170,     /* Colombia */
    CTRY_COSTA_RICA           = 188,     /* Costa Rica */
    CTRY_CROATIA              = 191,     /* Croatia */
    CTRY_CYPRUS               = 196,
    CTRY_CZECH                = 203,     /* Czech Republic */
    CTRY_DENMARK              = 208,     /* Denmark */
    CTRY_DOMINICAN_REPUBLIC   = 214,     /* Dominican Republic */
    CTRY_ECUADOR              = 218,     /* Ecuador */
    CTRY_EGYPT                = 818,     /* Egypt */
    CTRY_EL_SALVADOR          = 222,     /* El Salvador */
    CTRY_ESTONIA              = 233,     /* Estonia */
    CTRY_FAEROE_ISLANDS       = 234,     /* Faeroe Islands */
    CTRY_FINLAND              = 246,     /* Finland */
    CTRY_FRANCE               = 250,     /* France */
    CTRY_FRANCE2              = 255,     /* France2 */
    CTRY_GEORGIA              = 268,     /* Georgia */
    CTRY_GERMANY              = 276,     /* Germany */
    CTRY_GREECE               = 300,     /* Greece */
    CTRY_GREENLAND            = 304,     /* Greenland */
    CTRY_GRENADA              = 308,     /* Grenada */
    CTRY_GUAM                 = 316,     /* Guam */
    CTRY_GUATEMALA            = 320,     /* Guatemala */
    CTRY_HAITI                = 332,     /* Haiti */
    CTRY_HONDURAS             = 340,     /* Honduras */
    CTRY_HONG_KONG            = 344,     /* Hong Kong S.A.R., P.R.C. */
    CTRY_HUNGARY              = 348,     /* Hungary */
    CTRY_ICELAND              = 352,     /* Iceland */
    CTRY_INDIA                = 356,     /* India */
    CTRY_INDONESIA            = 360,     /* Indonesia */
    CTRY_IRAN                 = 364,     /* Iran */
    CTRY_IRAQ                 = 368,     /* Iraq */
    CTRY_IRELAND              = 372,     /* Ireland */
    CTRY_ISRAEL               = 376,     /* Israel */
    CTRY_ITALY                = 380,     /* Italy */
    CTRY_JAMAICA              = 388,     /* Jamaica */
    CTRY_JAPAN                = 392,     /* Japan */
    CTRY_JAPAN1               = 393,     /* Japan (JP1) */
    CTRY_JAPAN2               = 394,     /* Japan (JP0) */
    CTRY_JAPAN3               = 395,     /* Japan (JP1-1) */
    CTRY_JAPAN4               = 396,     /* Japan (JE1) */
    CTRY_JAPAN5               = 397,     /* Japan (JE2) */
    CTRY_JAPAN6               = 399,     /* Japan (JP6) */
    CTRY_JORDAN               = 400,     /* Jordan */
    CTRY_KAZAKHSTAN           = 398,     /* Kazakhstan */
    CTRY_KENYA                = 404,     /* Kenya */
    CTRY_KOREA_NORTH          = 408,     /* North Korea */
    CTRY_KOREA_ROC            = 410,     /* South Korea (for STA) */
    CTRY_KOREA_ROC2           = 411,     /* South Korea */
    CTRY_KOREA_ROC3           = 412,     /* South Korea (for AP) */
    CTRY_KUWAIT               = 414,     /* Kuwait */
    CTRY_LATVIA               = 428,     /* Latvia */
    CTRY_LEBANON              = 422,     /* Lebanon */
    CTRY_LIBYA                = 434,     /* Libya */
    CTRY_LIECHTENSTEIN        = 438,     /* Liechtenstein */
    CTRY_LITHUANIA            = 440,     /* Lithuania */
    CTRY_LUXEMBOURG           = 442,     /* Luxembourg */
    CTRY_MACAU                = 446,     /* Macau */
    CTRY_MACEDONIA            = 807,     /* the Former Yugoslav Republic of Macedonia */
    CTRY_MALAYSIA             = 458,     /* Malaysia */
    CTRY_MALTA                = 470,     /* Malta */
    CTRY_MEXICO               = 484,     /* Mexico */
    CTRY_MONACO               = 492,     /* Principality of Monaco */
    CTRY_MOROCCO              = 504,     /* Morocco */
    CTRY_NEPAL                = 524,     /* Nepal */   
    CTRY_NETHERLANDS          = 528,     /* Netherlands */
    CTRY_NETHERLAND_ANTILLES  = 530,     /* Netherlands-Antilles */
    CTRY_NEW_ZEALAND          = 554,     /* New Zealand */
    CTRY_NICARAGUA            = 558,     /* Nicaragua */
    CTRY_NORWAY               = 578,     /* Norway */
    CTRY_OMAN                 = 512,     /* Oman */
    CTRY_PAKISTAN             = 586,     /* Islamic Republic of Pakistan */
    CTRY_PANAMA               = 591,     /* Panama */
    CTRY_PARAGUAY             = 600,     /* Paraguay */
    CTRY_PERU                 = 604,     /* Peru */
    CTRY_PHILIPPINES          = 608,     /* Republic of the Philippines */
    CTRY_POLAND               = 616,     /* Poland */
    CTRY_PORTUGAL             = 620,     /* Portugal */
    CTRY_PUERTO_RICO          = 630,     /* Puerto Rico */
    CTRY_QATAR                = 634,     /* Qatar */
    CTRY_ROMANIA              = 642,     /* Romania */
    CTRY_RUSSIA               = 643,     /* Russia */
    CTRY_RWANDA               = 646,     /* Rwanda */
    CTRY_SAUDI_ARABIA         = 682,     /* Saudi Arabia */
    CTRY_MONTENEGRO           = 891,     /* Montenegro */
    CTRY_SINGAPORE            = 702,     /* Singapore */
    CTRY_SLOVAKIA             = 703,     /* Slovak Republic */
    CTRY_SLOVENIA             = 705,     /* Slovenia */
    CTRY_SOUTH_AFRICA         = 710,     /* South Africa */
    CTRY_SPAIN                = 724,     /* Spain */
    CTRY_SRILANKA             = 144,     /* Sri Lanka */
    CTRY_SWEDEN               = 752,     /* Sweden */
    CTRY_SWITZERLAND          = 756,     /* Switzerland */
    CTRY_SYRIA                = 760,     /* Syria */
    CTRY_TAIWAN               = 158,     /* Taiwan */
    CTRY_THAILAND             = 764,     /* Thailand */
    CTRY_TRINIDAD_Y_TOBAGO    = 780,     /* Trinidad y Tobago */
    CTRY_TUNISIA              = 788,     /* Tunisia */
    CTRY_TURKEY               = 792,     /* Turkey */
    CTRY_UAE                  = 784,     /* U.A.E. */
    CTRY_UKRAINE              = 804,     /* Ukraine */
    CTRY_UNITED_KINGDOM       = 826,     /* United Kingdom */
    CTRY_UNITED_STATES        = 840,     /* United States (for STA) */
    CTRY_UNITED_STATES_AP     = 841,     /* United States (for AP) */
    CTRY_UNITED_STATES_PS     = 842,     /* United States - public safety */
    CTRY_URUGUAY              = 858,     /* Uruguay */
    CTRY_UZBEKISTAN           = 860,     /* Uzbekistan */
    CTRY_VENEZUELA            = 862,     /* Venezuela */
    CTRY_VIET_NAM             = 704,     /* Viet Nam */
    CTRY_YEMEN                = 887,     /* Yemen */
    CTRY_ZIMBABWE             = 716      /* Zimbabwe */
};

#define CTRY_DEBUG      0
#define CTRY_DEFAULT    0x1ff
#define REGCODE_COUNTRY_BIT 0x80000000

typedef struct {
    A_UINT16    countryCode;       
    A_CHAR      isoName[3];
} COUNTRY_CODE_MAP;

static COUNTRY_CODE_MAP allCountries[] = {
    {CTRY_DEBUG,        "DB"}, 
    {CTRY_DEFAULT,      "NA"},
    {CTRY_ALBANIA,      "AL"},
    {CTRY_ALGERIA,      "DZ"},
    {CTRY_ARGENTINA,    "AR"},
    {CTRY_ARMENIA,      "AM"},
    {CTRY_ARUBA,        "AW"},
    {CTRY_AUSTRALIA,    "AU"},
    {CTRY_AUSTRALIA_AP, "AU"},
    {CTRY_AUSTRIA,      "AT"},
    {CTRY_AZERBAIJAN,   "AZ"},
    {CTRY_BAHRAIN,      "BH"},
    {CTRY_BANGLADESH,   "BD"},
    {CTRY_BARBADOS,     "BB"},
    {CTRY_BELARUS,      "BY"},
    {CTRY_BELGIUM,      "BE"},
    {CTRY_BELIZE,       "BZ"},
    {CTRY_BOLIVIA,      "BO"},
    {CTRY_BOSNIA_HERZEGOWANIA,   "BA"},
    {CTRY_BRAZIL,       "BR"},
    {CTRY_BRUNEI_DARUSSALAM, "BN"},
    {CTRY_BULGARIA,       "BG"},
    {CTRY_CAMBODIA,       "KH"},
    {CTRY_CANADA,         "CA"},
    {CTRY_CANADA_AP,      "CA"},
    {CTRY_CHILE,          "CL"},
    {CTRY_CHINA,          "CN"},
    {CTRY_COLOMBIA,       "CO"},
    {CTRY_COSTA_RICA,     "CR"},
    {CTRY_CROATIA,        "HR"},
    {CTRY_CYPRUS,         "CY"},
    {CTRY_CZECH,          "CZ"},
    {CTRY_DENMARK,        "DK"},
    {CTRY_DOMINICAN_REPUBLIC, "DO"},
    {CTRY_ECUADOR,        "EC"},
    {CTRY_EGYPT,          "EG"},
    {CTRY_EL_SALVADOR,    "SV"},
    {CTRY_ESTONIA,        "EE"},
    {CTRY_FINLAND,        "FI"},
    {CTRY_FRANCE,         "FR"},
    {CTRY_FRANCE2,        "F2"},
    {CTRY_GEORGIA,        "GE"},
    {CTRY_GERMANY,        "DE"},
    {CTRY_GREECE,         "GR"},
    {CTRY_GREENLAND,      "GL"},
    {CTRY_GRENADA,        "GD"},
    {CTRY_GUAM,           "GU"},
    {CTRY_GUATEMALA,      "GT"},
    {CTRY_HAITI,          "HT"},
    {CTRY_HONDURAS,       "HN"},
    {CTRY_HONG_KONG,      "HK"},
    {CTRY_HUNGARY,        "HU"},
    {CTRY_ICELAND,        "IS"},
    {CTRY_INDIA,          "IN"},
    {CTRY_INDONESIA,      "ID"},
    {CTRY_IRAN,           "IR"},
    {CTRY_IRELAND,        "IE"},
    {CTRY_ISRAEL,         "IL"},
    {CTRY_ITALY,          "IT"},
    {CTRY_JAMAICA,        "JM"},
    {CTRY_JAPAN,          "JP"},
    {CTRY_JORDAN,         "JO"},
    {CTRY_KAZAKHSTAN,     "KZ"},
    {CTRY_KENYA,          "KE"},
    {CTRY_KOREA_NORTH,    "KP"},
    {CTRY_KOREA_ROC,      "KR"},
    {CTRY_KOREA_ROC2,     "K2"},
    {CTRY_KOREA_ROC3,     "K3"},
    {CTRY_KUWAIT,         "KW"},
    {CTRY_LATVIA,         "LV"},
    {CTRY_LEBANON,        "LB"},
    {CTRY_LIECHTENSTEIN,  "LI"},
    {CTRY_LITHUANIA,     "LT"},
    {CTRY_LUXEMBOURG,    "LU"},
    {CTRY_MACAU,         "MO"},
    {CTRY_MACEDONIA,     "MK"},
    {CTRY_MALAYSIA,      "MY"},
    {CTRY_MALTA,         "MT"},
    {CTRY_MEXICO,        "MX"},
    {CTRY_MONACO,        "MC"},
    {CTRY_MOROCCO,       "MA"},
    {CTRY_NEPAL,         "NP"},
    {CTRY_NEW_ZEALAND,   "NZ"},
    {CTRY_NETHERLANDS,   "NL"},
    {CTRY_NETHERLAND_ANTILLES,    "AN"},
    {CTRY_NORWAY,         "NO"},
    {CTRY_OMAN,           "OM"},
    {CTRY_PAKISTAN,       "PK"},
    {CTRY_PANAMA,         "PA"},
    {CTRY_PERU,           "PE"},
    {CTRY_PHILIPPINES,    "PH"},
    {CTRY_POLAND,         "PL"},
    {CTRY_PORTUGAL,       "PT"},
    {CTRY_PUERTO_RICO,    "PR"},
    {CTRY_QATAR,          "QA"},
    {CTRY_ROMANIA,        "RO"},
    {CTRY_RUSSIA,         "RU"},
    {CTRY_RWANDA,         "RW"},
    {CTRY_SAUDI_ARABIA,   "SA"},
    {CTRY_MONTENEGRO,     "CS"},
    {CTRY_SINGAPORE,      "SG"},
    {CTRY_SLOVAKIA,       "SK"},
    {CTRY_SLOVENIA,       "SI"},
    {CTRY_SOUTH_AFRICA,   "ZA"},
    {CTRY_SPAIN,          "ES"},
    {CTRY_SRILANKA,       "LK"},
    {CTRY_SWEDEN,         "SE"},
    {CTRY_SWITZERLAND,    "CH"},
    {CTRY_SYRIA,          "SY"},
    {CTRY_TAIWAN,         "TW"},
    {CTRY_THAILAND,       "TH"},
    {CTRY_TRINIDAD_Y_TOBAGO, "TT"},
    {CTRY_TUNISIA,        "TN"},
    {CTRY_TURKEY,         "TR"},
    {CTRY_UKRAINE,        "UA"},
    {CTRY_UAE,            "AE"},
    {CTRY_UNITED_KINGDOM, "GB"},
    {CTRY_UNITED_STATES,    "US"},
    {CTRY_UNITED_STATES_AP, "US"},
    {CTRY_UNITED_STATES_PS, "PS"},
    {CTRY_URUGUAY,        "UY"},
    {CTRY_UZBEKISTAN,     "UZ"},    
    {CTRY_VENEZUELA,      "VE"},
    {CTRY_VIET_NAM,       "VN"},
    {CTRY_YEMEN,          "YE"},
    {CTRY_ZIMBABWE,       "ZW"}
};

#ifdef __cplusplus
}
#endif

#endif /* _WMI_CONFIG_H_ */
