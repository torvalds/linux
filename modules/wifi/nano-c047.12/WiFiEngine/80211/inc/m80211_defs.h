/*******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
 All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                       http://www.wep.com
SWEDEN
*******************************************************************************/
/*----------------------------------------------------------------------------*/
/*! \file

\brief [this module handles things related to life, universe and everythig]

This module is part of the macll block.
Thing are coming in and things are coming out, bla bla bla.
]
*/
/*----------------------------------------------------------------------------*/
#ifndef M80211_DEFS_H
#define M80211_DEFS_H
#include "m80211_stddefs.h"
#ifndef WIFI_ENGINE
#include "s_bufman_defs.h"
#endif

/* E X P O R T E D  D E F I N E S ********************************************/
#define MAX_SEQ_NO 5

/*! 802.11 possible rates. DO NOT REORDER THIS LIST!!! */
typedef uint8_t m80211_xmit_rate_t;
#define M80211_XMIT_RATE_1MBIT      0
#define M80211_XMIT_RATE_2MBIT      1
#define M80211_XMIT_RATE_5_5MBIT    2
#define M80211_XMIT_RATE_6MBIT      3
#define M80211_XMIT_RATE_9MBIT      4
#define M80211_XMIT_RATE_11MBIT     5
#define M80211_XMIT_RATE_12MBIT     6
#define M80211_XMIT_RATE_18MBIT     7
#define M80211_XMIT_RATE_22MBIT     8
#define M80211_XMIT_RATE_24MBIT     9
#define M80211_XMIT_RATE_33MBIT    10
#define M80211_XMIT_RATE_36MBIT    11
#define M80211_XMIT_RATE_48MBIT    12
#define M80211_XMIT_RATE_54MBIT    13
#define M80211_XMIT_RATE_NUM_RATES 14
#define M80211_XMIT_RATE_NUM_NON_OFDM_RATES 4
#define M80211_XMIT_RATE_INVALID   M80211_XMIT_RATE_NUM_RATES
#define M80211_XMIT_RATE_MIN_RATE  M80211_XMIT_RATE_1MBIT
#define M80211_XMIT_RATE_MAX_RATE  M80211_XMIT_RATE_54MBIT

#define M80211_XMIT_NON_OFDM_RATE_BIT_MASK   ((1<<M80211_XMIT_RATE_1MBIT)   |\
                                              (1<<M80211_XMIT_RATE_2MBIT)   |\
                                              (1<<M80211_XMIT_RATE_5_5MBIT) |\
                                              (1<<M80211_XMIT_RATE_11MBIT))


#define M80211_XMIT_OFDM_RATE_BIT_MASK       ((1<<M80211_XMIT_RATE_6MBIT)   |\
                                              (1<<M80211_XMIT_RATE_9MBIT)   |\
                                              (1<<M80211_XMIT_RATE_12MBIT)  |\
                                              (1<<M80211_XMIT_RATE_18MBIT)  |\
                                              (1<<M80211_XMIT_RATE_24MBIT)  |\
                                              (1<<M80211_XMIT_RATE_36MBIT)  |\
                                              (1<<M80211_XMIT_RATE_48MBIT)  |\
                                              (1<<M80211_XMIT_RATE_54MBIT))


typedef struct
{
   uint16_t                   rate_in_kilobit;
   uint16_t                   ctrl_rsp_mac_duration;
   bool_t                     is_ofdm;
   bool_t                     is_supported;
   bool_t                     short_preamble_capable;
   m80211_std_rate_encoding_t std_encoded_rate;
}m80211_rate_attrib_t;

typedef enum
{
   M80211_PROTECTION_NO_PROTECTION,
   M80211_PROTECTION_RTS,
   M80211_PROTECTION_CTS_TO_SELF
}m80211_protection_t;

typedef struct
{
   uint16_t dot11AuthenticationAlgorithm;
   bool_t   dot11AuthenticationAlgorithmEnable;
}dot11AuthenticationAlgorithmsTable_t;

typedef struct
{
   bool_t   dot11PrivacyInvoked;
   uint8_t  dot11WEPDefaultKeyID;
   uint32_t dot11WEPKeyMappingLength;
   bool_t   dot11ExcludeUnencrypted;
   uint32_t dot11WEPICVCorrectCount;
   uint32_t dot11WEPICVErrorCount;
   uint32_t dot11WEPExcludeCount;
   bool_t   dot11RSNAEnabled;
}dot11PrivacyTable_t;

typedef struct
{
   m80211_protect_type_t default_protection;
   uint8_t               default_key_id;
   m80211_cipher_key_t   group_cipher[4];
}m80211_cipher_struct_t;

typedef struct
{
   uint32_t dot11FirstChannelNumber;
   uint32_t dot11NumberOfChannels;
   uint32_t dot11MaximunTransmitPowerLevel;
}dot11MultidomainCapabilityTable_t;


typedef enum
{
   M80211_BSS_TYPE_INFRASTRUCTURE,
   M80211_BSS_TYPE_IBSS,
   M80211_BSS_TYPE_NONE
}m80211_bss_type_t;

#ifndef WIFI_ENGINE
typedef struct
{
   S_BUF_BufType     head;
   uint16_t last_seq[MAX_SEQ_NO];
}m80211_tuple_cache_t;
#endif

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* M80211_DEFS_H */
/* END OF FILE ***************************************************************/
