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
#ifndef MAC_API_IFCTRL_H
#define MAC_API_IFCTRL_H
#include "mac_api_defs.h"

/* E X P O R T E D  D E F I N E S ********************************************/

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_CTRL_INTERFACE_DOWN          (0  | MAC_API_PRIMITIVE_TYPE_RSP) 
#define HIC_CTRL_HIC_VERSION_REQ         (1  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_HEARTBEAT_REQ           (2  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_SET_ALIGNMENT_REQ       (3  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_SCB_ERROR_REQ           (4  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_SLEEP_FOREVER_REQ       (5  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_COMMIT_SUICIDE          (6  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_INIT_COMPLETED_REQ      (7  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_CTRL_HL_SYNC_REQ             (8  | MAC_API_PRIMITIVE_TYPE_REQ)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_CTRL_WAKEUP_IND              (0  | MAC_API_PRIMITIVE_TYPE_IND)
#define HIC_CTRL_HIC_VERSION_CFM         (1  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_HEARTBEAT_CFM           (2  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_HEARTBEAT_IND           (3  | MAC_API_PRIMITIVE_TYPE_IND)
#define HIC_CTRL_SET_ALIGNMENT_CFM       (4  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_SCB_ERROR_CFM           (5  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_SLEEP_FOREVER_CFM       (6  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_SCB_ERROR_IND           (7  | MAC_API_PRIMITIVE_TYPE_IND)
#define HIC_CTRL_INIT_COMPLETED_CFM      (8  | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_CTRL_HL_SYNC_CFM             (9  | MAC_API_PRIMITIVE_TYPE_CFM)


/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/
#define HIC_CTRL_CFM_SUCCESS           0
#define HIC_CTRL_CFM_FAILED            1
#define HIC_CTRL_CFM_NOT_SUPPORTED     2
#define HIC_CTRL_CFM_AUTO_TF_ENABLE    3
#define HIC_CTRL_CFM_AUTO_TF_DISABLE   4

#define SCB_ERROR_KEY_STRING "reqErrReason"

/* NRP status codes */
#define M80211_NRP_MLME_LEAVE_CFM_SUCCESS 0
#define M80211_NRP_MLME_LEAVE_CFM_FAILED  1

typedef enum
{
   HIC_CTRL_SET_ALIGNMENT_CFM_SUCCESS,
   HIC_CTRL_SET_ALIGNMENT_CFM_FAILED
}set_alignment_cfm_result_t;

typedef enum
{
   HIC_CTRL_INTFACE_CFM_SUCCESS,
   HIC_CTRL_INTFACE_CFM_FAILED
}interface_cfm_result_t;

typedef enum
{
   HIC_CTRL_WAKEUP_IND_HOST,
   HIC_CTRL_WAKEUP_IND_MULTICAST_DATA,
   HIC_CTRL_WAKEUP_IND_UNICAST_DATA,
   HIC_CTRL_WAKEUP_IND_ALL_DATA,
   HIC_CTRL_WAKEUP_IND_WMM_TIMER
}wakeup_ind_reason_t;

typedef enum
{
   HIC_CTRL_HEARTBEAT_REQ_ONE_SHOT,
   HIC_CTRL_HEARTBEAT_REQ_FOREVER,
   HIC_CTRL_HEARTBEAT_REQ_STOP
}heartbeat_req_control_t;

/******************************************************************************/
/* TEMPLATE PRIMITIVES                                                         */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           objId;
   uint8_t           errCode;
   uint8_t           reserved[2];
   uint32_t          txDescriptorAddress;
   uint32_t          signalHostAttentionAddress;
}hic_ctrl_scb_error_ul_t;


/****************************/
/* DRIVER TO MAC PRIMITIVES */
/****************************/

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          control;
}hic_ctrl_version_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          interval; /*report interval in seconds*/
   uint8_t           control;
   uint8_t           reserved[3];
}hic_ctrl_heartbeat_req_t;



/* hAttention */
#define HIC_CTRL_ALIGN_HATTN_MASK_POLICY                  0x01
#define HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO              0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO       HIC_CTRL_ALIGN_HATTN_MASK_POLICY
#define HIC_CTRL_ALIGN_HATTN_MASK_OVERRIDE_DEFAULT_PARAM  0x02
#define HIC_CTRL_ALIGN_HATTN_VAL_USE_DEFAULT_PARAM        0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM   HIC_CTRL_ALIGN_HATTN_MASK_OVERRIDE_DEFAULT_PARAM
#define HIC_CTRL_ALIGN_HATTN_MASK_PARAMS                  0xFC
#define HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_TYPE    0x04
#define HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_STD 0x00
#define HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_EXT HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_TYPE
#define HIC_CTRL_ALIGN_HATTN_MASK_GPIOPARAMS_GPIO_ID      0xF8
#define HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID    3

#define HIC_CTRL_ALIGN_HWAKEUP_ENABLED(_param)            (_param != 0xFF)
#define HIC_CTRL_ALIGN_HINTERVAL_ENABLED(_param)          (_param != 0xFF)
#define HIC_CTRL_ALIGN_TX_WINDOW_ENABLED(_param)          (_param != 0xFF)


/* swap */
#define HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP                   0x00
#define HIC_CTRL_ALIGN_SWAP_16BIT_BYTESWAP                0x01

typedef struct
{
   mac_api_transid_t trans_id;
   uint16_t          min_sz;
   uint16_t          padding_sz;
   uint8_t           hAttention;
   uint8_t           ul_header_size;
   uint8_t           swap;
   uint8_t           hWakeup;
   uint8_t           hForceInterval;
   uint8_t           tx_window_size;
   uint16_t          block_mode_bug_workaround_block_size;  /* Only valid if block_mode_bug_workaround is set */
   uint8_t           reserved[2];
}hic_ctrl_set_alignment_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          dummy;
}hic_ctrl_init_completed_req_t;
typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          reserved;
}hic_ctrl_interface_down_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          dummy;
}hic_ctrl_commit_suicide_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   varstring_t       keyString;
}hic_ctrl_scb_error_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           control; /* start/end */
   uint8_t           reserved[3];
}hic_ctrl_sleep_forever_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          reserved;
}hic_ctrl_hl_sync_req_t;

/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/


typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          reasons;
}hic_ctrl_wakeup_ind_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          version;
}hic_ctrl_version_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          tsf_tmr;
}hic_ctrl_heartbeat_ind_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           status;
   uint8_t           reserved[3];
}hic_ctrl_heartbeat_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           result;
   uint8_t           min_sz;
   uint8_t           initial_gpio_state;
   uint8_t           reserved;
}hic_ctrl_set_alignment_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          result;
}hic_ctrl_init_completed_cfm_t;


typedef hic_ctrl_scb_error_ul_t hic_ctrl_scb_error_cfm_t;
typedef hic_ctrl_scb_error_ul_t hic_ctrl_scb_error_ind_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          result;
}hic_ctrl_sleep_forever_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          result;
   uint8_t           src_addr[6];
   uint16_t          seqno;
   uint32_t          rx_timestamp;
   uint32_t          pad[6];
   uint32_t          hic_timestamp;
}hic_ctrl_hl_sync_cfm_t;


/**********************************************************************************/
/*************************** END OF MESSAGE DEFINITIONS ***************************/
/**********************************************************************************/



typedef union
{
   mac_api_transid_t             transid;
   hic_ctrl_wakeup_ind_t         wakeupInd;
   hic_ctrl_interface_down_t     interfaceDown;
   hic_ctrl_version_req_t        versionReq;
   hic_ctrl_version_cfm_t        versionCfm;
   hic_ctrl_heartbeat_req_t      heartbeatReq;
   hic_ctrl_heartbeat_ind_t      heartbeatInd;
   hic_ctrl_heartbeat_cfm_t      heartbeatCfm;
   hic_ctrl_set_alignment_req_t  setAlignmentReq;
   hic_ctrl_set_alignment_cfm_t  setAlignemntCfm;
   hic_ctrl_init_completed_req_t initCompletedReq;
   hic_ctrl_init_completed_cfm_t initCompletedCfm;
   hic_ctrl_hl_sync_req_t        hl_sync_req;
   hic_ctrl_hl_sync_cfm_t        hl_sync_cfm;
   hic_ctrl_commit_suicide_req_t commit_suicide;
   hic_ctrl_scb_error_req_t      scbErrorReq;
   hic_ctrl_scb_error_cfm_t      scbErrorCfm;
   hic_ctrl_scb_error_ind_t      scbErrorInd;   
   hic_ctrl_sleep_forever_req_t  sleepForeverReq;
   hic_ctrl_sleep_forever_cfm_t  sleepForeverCfm;
}hic_ctrl_msg_body_t;

typedef struct
{
   hic_ctrl_msg_body_t  hic_ctrl_msg_body;
}hic_ctrl_msg_t;

typedef struct
{
   uint16_t msgId;
   hic_ctrl_msg_t *ctrlMsg;
}hic_ctrl_msg_param_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_IFCTRL_H */
/* END OF FILE ***************************************************************/
