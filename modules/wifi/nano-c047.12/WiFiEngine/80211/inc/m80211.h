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
164 40 Kista                       http://www.nanoradio.se
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
#ifndef MACLL_M80211_H
#define MACLL_M80211_H
#include "m80211_defs.h"
#include "tsf_tmr_defs.h"

/* E X P O R T E D  D E F I N E S ********************************************/

/* E X P O R T E D  D A T A T Y P E S ****************************************/

typedef struct
{
   uint16_t power_mgmt_mode;
   bool_t  force_wakeup;
   bool_t  receive_all_dtim;
}m80211_power_mgmt_info_t;

/* G L O B A L  V A R I A B L E S ********************************************/
extern ROM_STORAGE(m80211_mac_addr_t)m80211_null_mac;
extern dot11AuthenticationAlgorithmsTable_t dot11AuthenticationAlgorithmsTable[2];
extern dot11PrivacyTable_t dot11PrivacyTable;
extern uint16_t            m80211_aid;
extern uint8_t             m80211_dtim_period;
extern uint16_t            m80211_listen_interval;
extern uint16_t            m80211aPreambleLength;
extern uint16_t            m80211aMaxPreambleLength;
extern uint16_t            m80211aPLCPHeaderLength;
extern uint16_t            m80211aMaxPLCPHeaderLength;
extern char                m80211ssid[M80211_IE_MAX_LENGTH_SSID];
extern m80211_mac_addr_t   m80211station_address;
extern uint8_t             m80211aShortRetryLimit[M80211_NUM_AC_INSTANCES];
extern uint8_t             m80211aLongRetryLimit[M80211_NUM_AC_INSTANCES];
extern uint16_t            m80211aRTSthreshold;
extern uint32_t            dot11MediumOccupancyLimit;
extern bool_t              dot11MultiDomainCapabilityImplemented;
extern bool_t              dot11MultiDomainCapabilityEnabled;
extern m80211_country_string_t dot11CountryString;
extern uint16_t            m80211aFragmentationThreshold;
extern dot11MultidomainCapabilityTable_t dot11MultidomainCapabilityTable[14];
extern ROM_STORAGE(m80211_rate_attrib_t) m80211_rate_attrib[M80211_XMIT_RATE_NUM_RATES];
extern bool_t              m80211_protect_54mbit_frames_with_cts_to_self;
extern ROM_STORAGE(m80211_oui_t) m80211_oui_wpa;
extern m80211_cipher_struct_t m80211_cipher;
#ifdef X_TEST
extern bool_t              reading_mib_from_persist_mem_OK;
#endif
/* I N T E R F A C E  F U N C T I O N S **************************************/

void m80211_init(void);

void m80211_startup(void);

/*!
  Use this function sets the control field to an 802.11 ack frame.
  
  \return void
*/ 
void mac_pdu_set_control_ack(
char * ack_frame);       /*!< Pointer to the start of an ack frame */ 


/*!
  Use this function sets the control field to an 802.11 cts frame.
  
  \return void
*/ 
void mac_pdu_set_control_cts(
char * cts_frame);       /*!< Pointer to the start of a cts frame */ 


/*!
  Use this function sets the control field to an 802.11 rts frame.
  
  \return void
*/ 
void mac_pdu_set_control_rts(
char * rts_frame);       /*!< Pointer to the start of a rts frame */ 


/*!
  Use this function sets the control field to an 802.11 pspoll frame.
  
  \return void
*/ 
void mac_pdu_set_control_pspoll(
mac_frame_pspoll_t * p_frame);/*!< Pointer to the start of a pspoll frame */ 


/*!
  This function returns a pointer to the ra field of an 802.11 mac frame.
  
  \return pointer to ra field of the mac frame provided as parameter.
*/ 
m80211_mac_addr_t * mac_pdu_ref_get_ra(
char * p_frame);       /*!< Pointer to the start of a mac frame */ 


/*!
  This function returns a pointer to the ra field of an 802.11 cts frame.
  
  \return pointer to ra filed of the cts frame provided as parameter.
*/ 
m80211_mac_addr_t * mac_pdu_ref_get_cts_ra(
char * cts_frame);       /*!< Pointer to the start of a cts frame */ 


/*!
  This function returns a pointer to the ta field of an 802.11 data/management frame.
  
  \return pointer to ta filed of the data/mgmt frame provided as parameter.
*/ 
m80211_mac_addr_t * mac_pdu_ref_get_data_ta(
char * p_frame);       /*!< Pointer to the start of a data/mgmt frame */ 


/*!
  This function returns a pointer to the ta field of an 802.11 RTS frame.
  
  \return pointer to ta filed of the data/mgmt frame provided as parameter.
*/ 
m80211_mac_addr_t * mac_pdu_ref_get_rts_ta(
char * p_frame);       /*!< Pointer to the start of the RTS frame */ 


/*!
  Copies a mac addres from "from" to "to".
  
  \return void
*/ 
void m80211_copymac(
m80211_mac_addr_t * to,    /*< destination mac address */
m80211_mac_addr_t * from); /*< source mac address */


/*!
  Optimized function that copies a mac address from "from" to "to".
  Both to and from must be halfword aligned!
  
  \return void
*/ 
void m80211_halfword_aligned_copymac(
     m80211_mac_addr_t * to,     /*< Halfword aligned destination mac address */
     m80211_mac_addr_t * from);  /*< Halfword aligned source mac address */


/*!
  This function returns a pointer to the control field part of a 802.11 mac frame.
  
  \return pointer to the control field part of a 802.11 mac frame
*/ 
char * mac_pdu_ref_get_control(
char * p_frame);  /*< Ptr to start of the mac frame */



/*!
  This function calculates the duration value for an control response frame.
  
  \return duration value to be placed in the duration field.
*/ 
uint16_t mac_duration_calculate_ctrl_rsp(
m80211_xmit_rate_t ctrl_rate,      /*< Rate to be used for the ctrl rsp frame */
uint16_t preceding_frame_duration, /*< Duration field of the preceding frame */
bool_t   short_preamble); /*< TRUE if short preamble is to be used */


/*!
  This function calculates the duration value for a non-last data fragment.
  
  \return duration value to be placed in the duration field.
*/ 
uint16_t mac_duration_calculate_fragment(
m80211_xmit_rate_t data_rate, /*< The tx rate for the frame */
m80211_xmit_rate_t ctrl_rate, /*< The expected tx rate for the ack frame */
uint16_t next_size,           /*< The size in bytes for the NEXT fragment */
bool_t   short_preamble);     /*< TRUE if short preamble is to be used */


/*!
  This function calculates the duration value for a cts-to-self frame.
  
  \return duration value to be placed in the duration field.
*/ 
uint16_t mac_duration_calculate_cts_to_self(
m80211_xmit_rate_t data_rate, /*< The tx rate for the data frame */
m80211_xmit_rate_t ctrl_rate, /*< The expected tx rate for the ack frame */
uint16_t size,                /*< The size in bytes for the data frame */
bool_t   short_preamble);     /*< TRUE if short preamble is to be used */


/*!
  This function calculates the duration value for a last data/mgmt fragment.
  
  \return duration value to be placed in the duration field.
*/ 
uint16_t           mac_duration_calculate_data(
m80211_xmit_rate_t ctrl_rate,       /*< The tx rate for the ack frame */
bool_t             short_preamble); /*< TRUE if short preamble is to be used */


/*!
  This function calculates the duration value for an RTS frame.
  
  \return duration value to be placed in the duration field.
*/ 
uint16_t mac_duration_calculate_rts(
m80211_xmit_rate_t data_rate,           /*< The tx rate for the data frame */
uint16_t           data_size,           /*< The size in bytes for the data frame */
bool_t             data_short_preamble, /*< TRUE if short preamble is to be used for the data frame */
m80211_xmit_rate_t ack_rate,            /*< The tx rate for the ACK frame */
m80211_xmit_rate_t rts_rate,            /*< The tx rate for the rts frames */
bool_t             rts_short_preamble); /*< TRUE if short preamble is to be used for the rts frame */


/*!
  This function returns the duration value of a mac frame
  
  \return duration value
*/ 
uint16_t mac_pdu_get_duration(
char * p_frame); /*< Pointer to mac frame */


/*!
  This function returns the SIFS time to be used with the actual rate
  
  \return SIFS time for the actual xmit rate
*/ 
uint8_t mac_get_sifs(void);


/*!
  This function returns the DIFS time to be used with the actual rate
  
  \return DIFS time for the actual xmit rate
*/ 
uint16_t mac_get_difs(void);

/*!
  This function returns the rate expressed in kilobits/second
  corresponding to the rate encoded as m80211_xmit_rate_t.
  
  \return SIFS time for the actual xmit rate
*/ 
uint16_t mac_get_rate_in_kilobit_per_second(
m80211_xmit_rate_t rate);


/*!
  This function returns the length expressed in microsecond 
  for a ctrl response frame (ACK, CTS...) including BB header.
  
  \return SIFS time for the actual xmit rate
*/ 
uint16_t mac_get_ctrl_rsp_time(
m80211_xmit_rate_t rate,  /*< rate to be used for the ctrl frame */
bool_t   short_preamble); /*< TRUE if short preamble is to be used */



/*!
  This function compares the provided mac address with the mac
  address of this station and returns TRUE if match.
  
  \return TRUE if provided address is identical with this 
   stations address, otherwise FALSE
*/ 
bool_t m80211_is_this_stations_address(
m80211_mac_addr_t * mac);  /*< Address to check */


bool_t m80211_is_broadcast_address(m80211_mac_addr_t * mac);


m80211_mac_addr_t * m80211_get_broadcast_address(void);



/*!
  This function compares the two given mac addresses and returns TRUE if the are equal.
  
  \return TRUE if provided addresses are equal
   otherwise FALSE
*/ 
bool_t m80211_is_equal_mac(
m80211_mac_addr_t * mac1, /*< First address to comaper */
m80211_mac_addr_t * mac2);/*< Secone address to comaper */

bool_t m80211_is_equal_ssid(m80211_ie_ssid_t * ssid_1, m80211_ie_ssid_t * ssid_2);

bool_t m80211_is_broadcast_ssid(m80211_ie_ssid_t * ssid);

/*!
  This function extracts and returns the type and subtype
  respectively from a MAC header.
  
  \return void
*/ 
void mac_pdu_get_frame_type_and_subtype(
char * p_frame,      /*< Pointer to start of MAC frame (header) */
uint8_t * p_type,    /*< Pointer to returned type field */
uint8_t * p_subtype);/*< Pointer to returned subtype field */


/*!
  This is an optimized function for calculation time to terms
  of microseconds from terms of multiples of slottime.
  
  \return Time expressed in microseconds
*/ 
uint16_t mac_get_N_slottimes(
uint16_t num_slots);  /*< Number of slottime */


/*!
  Updates the station mac address.
  
  \return void
*/ 
void m80211station_address_mib_set(
m80211_mac_addr_t * new_mac); /*< New mac */

/*!
  Gets the station mac address.
  
  \return mac address
*/ 
m80211_mac_addr_t *m80211station_address_mib_get(void); 

bool_t mib_access_dot11MACAddress(bool_t     get,
                                  void     * reference,
                                  uint16_t * size);

/*!
  Updates the station SSID.
  
  \return void
*/ 
void m80211_ssid_mib_set(
char* new_ssid); /*< New ssid */

/*!
  Gets the station SSID.
  
  \return ssid
*/ 
char* m80211_ssid_mib_get(void); 


/*!
  This function returns TRUE if the modulation method for the provided
  rate is OFDM.
  \return TRUE if OFDM modulation.
*/ 
bool_t m80211_rate_is_ofdm(
m80211_xmit_rate_t rate); /*< rate to check OFDM modulation for */


/*!
  This function returns TRUE if short preamble should be used for transmision,
  FALSE otherwise
  \return TRUE if short preamble shuld be used.
*/ 
bool_t m80211_use_short_preamble(
m80211_xmit_rate_t rate,
bool_t             request_short_preamble);


/*!
  This function converts a time expressed in TU into a value expressed
  in microseconds.
  \return Corresponding time value expressed in units of microseconds.
*/ 
tsf_time_t m80211_TU_to_usec(
uint16_t num_TU); /*< Number of TimeUnits to convert */


/*!
  This function calculates the complete frame time including preamble,
  plcp header payload, FCS and signal extention of a mac frame.
  Note that fcs size must NOT be included in input size parameter.
  \return Complete 802.11 frame time including signal extention
*/ 
uint16_t m80211_get_frame_time(
m80211_xmit_rate_t rate,
uint16_t           size_without_fcs,
bool_t             short_preamble);


/*!
  This function calculates the complete frame time including preamble,
  plcp header but without signal extention of a mac frame.
  \return Complete 802.11 frame time excluding signal extention
*/ 
uint16_t m80211_get_physical_frame_time(
m80211_xmit_rate_t rate,
uint16_t           size,
bool_t             short_preamble);


/*!
  This function returns the preamble time for a mac frame.
  \return preamble time for a mac frame.
*/ 
tsf_time_t m80211_get_preamble_duaration(
m80211_xmit_rate_t rate, /*< xmit rate */
bool_t             short_preamble); /*< TRUE if short preamble (only applicable
                                       for CCK) */


/*!
  This function returns the plcp header time for a mac frame.
  \return plcp header time for a mac frame.
*/ 
tsf_time_t m80211_get_plcp_header_duaration(
m80211_xmit_rate_t rate, /*< xmit rate */
bool_t             short_preamble); /*< TRUE if short preamble (only applicable
                                        for CCK) */

void m80211_adjust_rx_ts(tsf_time_t         * p_ts,
                         m80211_xmit_rate_t   rate,
                         bool_t               short_preamble);


/*!
  This function returns the rx processing delay for the baseband
  \return rx processing delay for rf and bb.
*/ 
tsf_time_t m80211_get_bbrf_rx_proc_delay(void);


/*!
  This function gets power management info
*/
m80211_power_mgmt_info_t *m80211_get_power_mgmt_info(void);

/*!
  This function sets power management info
*/
void m80211_set_power_mgmt_info(m80211_power_mgmt_info_t *power_mgmt_info);

bool_t m80211_identical_ie(m80211_ie_format_t * a, m80211_ie_format_t * b);

m80211_xmit_rate_t m80211_rate_conv_std_to_local(uint8_t std_rate);

bool_t mib_access_dot11WEPDefaultKeyValue_1(bool_t     get,
                                            void     * reference,
                                            uint16_t * size);
bool_t mib_access_dot11WEPDefaultKeyValue_2(bool_t     get,
                                            void     * reference,
                                            uint16_t * size);
bool_t mib_access_dot11WEPDefaultKeyValue_3(bool_t     get,
                                            void     * reference,
                                            uint16_t * size);
bool_t mib_access_dot11WEPDefaultKeyValue_4(bool_t     get,
                                            void     * reference,
                                            uint16_t * size);
m80211_ie_format_t * m80211_get_ie_from_beacon(char     * p_mgmt,
                                               uint16_t         frame_size,
                                               m80211_ie_id_t   ie_id);

/* 
  This function performs a linear search through rate table to find the native
  rate for a rate coded in 802.11 standard format.
*/
m80211_xmit_rate_t mac_std2native_rate(m80211_std_rate_encoding_t std_rate);


void init_mib_from_flash(void);

#endif    /* MACLL_M80211_H */
/* END OF FILE ***************************************************************/
