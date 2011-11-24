/* Copyright (C) 2007 Nanoradio AB */
/* $Id: nrx_lib.h 11634 2009-04-28 17:23:06Z phth $ */

#ifndef __nrx_lib_h__
#define __nrx_lib_h__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* #include <unistd.h> */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <err.h>

#ifdef ANDROID
#include <arpa/inet.h>
#endif

/*! Debug levels */
#define NRX_PRIO_ERR        (1)
#define NRX_PRIO_WRN        (3)
#define NRX_PRIO_LOG        (5)

/*! Callback function for debug information */
typedef int (*nrx_debug_callback_t)(int prio,
                                    const char *file,
                                    int line,
                                    const char *message);

struct nrx_context_data;

typedef struct nrx_context_data *nrx_context;

typedef unsigned int nrx_bool;

/*! callback structure for wireless extensions events */
struct nrx_we_callback_data {
   uint16_t cmd; /*!< Wireless extensions event */
   void *data;   /*!< Pointer to event data */
   size_t len;   /*!< Size of data */
};

enum { NRX_CB_TRIGGER, NRX_CB_CANCEL };

/*!
 * The callback will be called with operation parameter NRX_CB_TRIGGER
 * on a successful notification. If the event_data parameter points to
 * something that data will be freed immediately when the callback
 * returns.
 * The callback will be called with operation parameter NRX_CB_CANCEL
 * if the trigger connected to the callback is canceled for
 * some reason. The callback will be passed the user_data parameter
 * as normal but the event_data parameter will be NULL.
 */
typedef int (*nrx_callback_t)(nrx_context context,
                              int operation,
                              void *event_data,
                              size_t event_data_size,
                              void *user_data); 

/*! callback handle, obtained by registering a callback, its main
 *  purpose is for cancelling callbacks */
typedef uintptr_t nrx_callback_handle;

typedef enum {
   NRX_LONG_PREAMBLE = 0,
   NRX_SHORT_PREAMBLE = 1
} nrx_preamble_type_t;          /* Must agree with WiFiEngin definition of preamble_t */

typedef enum {
    NRX_SCAN_PASSIVE = 0,
    NRX_SCAN_ACTIVE
} nrx_scan_type_t;

typedef enum {
    NRX_SCAN_JOB_STATE_SUSPENDED = 0,
    NRX_SCAN_JOB_STATE_RUNNING
} nrx_scan_job_state_t;

typedef enum
{
   NRX_SSID_ADD = 0,
   NRX_SSID_REMOVE = 1
}nrx_ssid_action_t;
 
typedef enum
{
   NRX_ENCR_DISABLED,
   NRX_ENCR_WEP,            
   NRX_ENCR_TKIP,           
   NRX_ENCR_CCMP            
} nrx_encryption_t;    /* Must agree with WiFiEngin definition */

typedef enum
{
   NRX_AUTH_OPEN = 0,
   NRX_AUTH_SHARED,
   NRX_AUTH_8021X,
   NRX_AUTH_AUTOSWITCH,
   NRX_AUTH_WPA,
   NRX_AUTH_WPA_PSK,
   NRX_AUTH_WPA_NONE,
   NRX_AUTH_WPA_2,
   NRX_AUTH_WPA_2_PSK
} nrx_authentication_t;   /* Must agree with WiFiEngin definition */



/* @{ */
typedef int nrx_job_flags_t;
#define NRX_JOB_FLAG_NOT_ASSOCIATED (1<<0)  /**< Job should be run when not associated */
#define NRX_JOB_FLAG_ASSOCIATED     (1<<1)  /**< Job should be run when associated */
/* @} */

/* @{ */
/*! Scan notification policy */
typedef int nrx_sn_pol_t;
#define NRX_SN_POL_FIRST_HIT    (1<<0)  /**< Indicate on first found net */
#define NRX_SN_POL_JOB_COMPLETE (1<<1)  /**< Indicate on scan job complete */
#define NRX_SN_POL_ALL_DONE     (1<<2)  /**< Indicate when all scan jobs have completed */
#define NRX_SN_POL_JOB_COMPLETE_WITH_HIT (1<<3) /**< Indicate on scan job complete AND a net was found */
/* @} */

/* @{ */
/*! Scan notification data (RELEASE 5.0) */
typedef struct {
      nrx_sn_pol_t pol;
      int32_t sj_id;
} nrx_scan_notif_t;
/* @} */

/* @{ */
/*! RSSI and SNR detection type */
typedef int nrx_detection_target_t;
#define NRX_DT_BEACON (1<<0)
#define NRX_DT_DATA   (1<<1)
/* @} */

/* @{ */
typedef int nrx_bss_type_t;
#define NRX_BSS_ESS  (1<<0) /**< Infrastructure mode */
#define NRX_BSS_IBSS (1<<1) /**< Ad-hoc mode */
/* @} */

/* @{ */
typedef uint8_t nrx_adaptive_tx_rate_mode_t;
/*! The rate is stepped down when the number of retransmission increase. */
#define NRX_TX_ADAP_RATE_STEPDOWN                (1<<0)
/*! The rate is successively stepped down as a frame/fragment is retransmitted. */
#define NRX_TX_ADAP_RETRANSMISSION_RATE_STEPDOWN (1<<1)
/*! The transmission power is successively stepped down if possible. */
#define NRX_TX_ADAP_TPC (1<<2)
/*! Don't exclude 5.5 or 11 Mbit/s if 6 or 12 Mbit/s is present, respectively. */
#define NRX_TX_ADAP_RATE_MASK_NO_EXLUDE (1<<3)
/* @} */


/* @{ */
/*! Trigger direction */
typedef int nrx_thr_dir_t;
#define NRX_THR_RISING  (1<<0) /**< Trigger when the value rises above the threshold */
#define NRX_THR_FALLING (1<<1) /**< Trigger when the value falls below the threshold */
/* @} */

/* @{ */
typedef int nrx_traffic_type_t;
#define   NRX_TRAFFIC_MULTICAST (1<<0)
#define   NRX_TRAFFIC_BROADCAST (1<<1)
/* @} */

/* @{ */
/*!
 * ARP policy: Also located in fw and WiFiEngine.
 */
typedef enum {
   NRX_ARP_HANDLE_MYIP_FORWARD_REST = 0, /**< FW handles my ip, forwards rest to host.  */
   NRX_ARP_HANDLE_MYIP_FORWARD_NONE = 1, /**< Nothing is forwarded to host.             */
   NRX_ARP_HANDLE_NONE_FORWARD_MYIP = 2, /**< Only forward my ip to host.               */
   NRX_ARP_HANDLE_NONE_FORWARD_ALL  = 3  /**< Forward all arp requests to host.         */
} nrx_arp_policy_t;
/* @} */

/* @{ */
typedef int nrx_antenna_t;
#define   NRX_ANTENNA_1 (1<<0)
#define   NRX_ANTENNA_2 (1<<1)
/* @} */

/* @{ */
typedef int nrx_conn_lost_type_t;
#define NRX_BLR_AUTH_FAIL  (1<<0) /**< Authentication failed. */
#define NRX_BLR_ASSOC_FAIL (1<<1) /**< Association failed. */
#define NRX_BLR_DEAUTH     (1<<2) /**< AP requested deauthentication. */
/* @} */

#define NRX_MAC_ADDR_LEN (6)
#define NRX_MAX_SSID_LEN (32+1) /* Room for NULL char at the end */
#define NRX_MAX_MAC_ADDR_LIST_LEN (16)
#define NRX_MAX_SSID_LIST_LEN (8)
#define NRX_MAX_CHAN_LIST_LEN (14) /* Should agree with M80211_CHANNEL_LIST_MAX_LENGTH */
#define NRX_MAX_RATE_LIST_LEN (14)
#define NRX_MAX_GPIO_LIST_LEN (5)

typedef enum {
   NRX_REGION_FCC = 0x10,    /* United States */
   NRX_REGION_IC = 0x20,     /* Canada */
   NRX_REGION_ETSI = 0x30,   /* Europe */
   NRX_REGION_SPAIN = 0x31,
   NRX_REGION_FRANCE = 0x32,
   NRX_REGION_MKK = 0x40,    /* Japan */

   NRX_REGION_JAPAN   = NRX_REGION_MKK,
   NRX_REGION_AMERICA = NRX_REGION_FCC,
   NRX_REGION_EMEA    = NRX_REGION_ETSI
} nrx_region_code_t;

typedef struct
{
   char octet[NRX_MAC_ADDR_LEN];
} nrx_mac_addr_t;

typedef struct 
{
   size_t len;
   nrx_mac_addr_t mac_addr[NRX_MAX_MAC_ADDR_LIST_LEN];
} nrx_mac_addr_list_t;

typedef struct
{
   char ssid[NRX_MAX_SSID_LEN];
   size_t ssid_len;
} nrx_ssid_t;

typedef struct
{
   int len; /**< Number of valid SSIDs in the ssids[] array. */
   nrx_ssid_t ssids[NRX_MAX_SSID_LIST_LEN];
} nrx_ssid_list_t;

/*! Represents a wireless channel. Must be in the range 0..255, other
 *  values are reserved for future use. */
typedef uint16_t nrx_channel_t;
#define NRX_CHANNEL_UNKNOWN 0xffff

typedef struct
{
      /*! Number of valid channels in the chs[] array. */
      int len; 
      /*! The lower byte is used for the channel number. The higher
       *  byte must be zero for the default channel numbering scheme.
       *  A non-zero high byte indicates a different channel-frequency
       *  mapping. */
        nrx_channel_t channel[NRX_MAX_CHAN_LIST_LEN]; 
} nrx_ch_list_t;

struct nrx_conn_lost_data {
      nrx_mac_addr_t bssid; /**< BSSID from the disconnecting entity */
      nrx_conn_lost_type_t type; /**< The type of connection failure */
      /*! The reason code from the connection failure. The meaning of this
       * code is defined in the 802.11 standards and depends on the type
       * field (which defines which message caused the connection to fail). */
      int reason_code;
};

/*! Trigger event information. Sent to callback. */
typedef struct nrx_event_mibtrigger {
   uint32_t id;        /*! Id of trigger/threshold that was activated */
   uint32_t value;     /*! The MIB's value at the particular instance 
                        *  the trigger/threshold was set off */
} nrx_event_mibtrigger_t;

/*! Wireless rate in IEEE units (500kbit/s) */
typedef uint8_t nrx_rate_t;

typedef struct {
      /*! Number of valid rates in the rates[] array. */
      int len;
      /*! Rate list. The rates are encoded in the "supported rates" encoding
       * from the 802.11 standard. The high bit defines a basic rate. */
      nrx_rate_t rates[NRX_MAX_RATE_LIST_LEN];
} nrx_rate_list_t;

typedef struct {
      /*! Number of valid entries in the retries[] array. */
      int len;
      /*! 
       * Vector with number of retries, each corresponding to a
       * rate in a nrx_rate_list_t */
      uint8_t retries[NRX_MAX_RATE_LIST_LEN];
} nrx_retry_list_t;

typedef struct {
      uint8_t gpio_pin;         /* Pin number */
      uint8_t active_high;      /* 1 => active high, 0 => active low */
} nrx_gpio_pin_t;

typedef struct {
      /*! Number of valid entries in the retries[] array. */
      int len;
      /*! Vector with pins */
      nrx_gpio_pin_t pin[NRX_MAX_GPIO_LIST_LEN];
} nrx_gpio_list_t;

/* Service Period length: all buffered frames, 2, 4, 6. */
typedef enum
{
   NRX_SP_LEN_ALL = 0x00, /* Get all buffered frames */
   NRX_SP_LEN_2   = 0x01, /* Get 2 buffered frames */
   NRX_SP_LEN_4   = 0x02, /* Get 4 buffered frames */
   NRX_SP_LEN_6   = 0x03  /* Get 6 buffered frames */
}nrx_sp_len_t;

typedef int nrx_wmm_ac_t;
#define NRX_WMM_AC_BE (1 << 3) /* Best effort */
#define NRX_WMM_AC_BK (1 << 2) /* Background */
#define NRX_WMM_AC_VI (1 << 1) /* Video */
#define NRX_WMM_AC_VO (1 << 0) /* Voice */

typedef struct {
#define NRX_PRIOMAP_SIZE (256 / 2)
   uint8_t priomap[NRX_PRIOMAP_SIZE];
} nrx_priomap;

#include "nrx_proto.h"

#endif /* __nrx_lib_h__ */
