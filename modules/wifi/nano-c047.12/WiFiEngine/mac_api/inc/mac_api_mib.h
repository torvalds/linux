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
#ifndef MAC_API_MIB_H
#define MAC_API_MIB_H
#include "mac_api_defs.h"

/* E X P O R T E D  D E F I N E S ********************************************/


/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define MLME_GET_REQ                   (0  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_SET_REQ                   (1  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_GET_NEXT_REQ              (2  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_MIB_SET_TRIGGER_REQ       (3  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_MIB_REMOVE_TRIGGER_REQ    (4  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_MIB_SET_GATINGTRIGGER_REQ (5  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_GET_RAW_REQ               (6  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_SET_RAW_REQ               (7  | MAC_API_PRIMITIVE_TYPE_REQ)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define MLME_GET_CFM                   (0  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_SET_CFM                   (1  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_MIB_SET_TRIGGER_CFM       (2  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_MIB_REMOVE_TRIGGER_CFM    (3  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_MIB_TRIGGER_IND           (4  | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_MIB_SET_GATINGTRIGGER_CFM (5  | MAC_API_PRIMITIVE_TYPE_CFM)

/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/
typedef uint8_t mib_result_t;
#define MIB_RESULT_OK                        0
#define MIB_RESULT_INVALID_PATH              1
#define MIB_RESULT_NO_SUCH_OBJECT            2
#define MIB_RESULT_SIZE_ERROR                3
#define MIB_RESULT_OBJECT_NOT_A_LEAF         4
#define MIB_RESULT_SET_FAILED                5
#define MIB_RESULT_GET_FAILED                6
#define MIB_RESULT_SET_NOT_ALLOWED           7
#define MIB_RESULT_INTERNAL_ERROR            8
#define MIB_RESULT_GET_NOT_ALLOWED           9
#define MIB_RESULT_MEM_REGION_INVALIDATED   10
#define MIB_ASCII_IDENTIFIER_MAX_LENGTH     32
#define MIB_MAX_GET_VAL_LENGTH             128
#define MIB_IDENTIFIER_MAX_LENGTH            8
#define MIB_MAX_DOTTED_PATH_LENGTH           8
#define MIB_PRE_4_4_IDENTIFIER_MAX_LENGTH    MIB_ASCII_IDENTIFIER_MAX_LENGTH


/* MIB trigger types */
#define REALTRIGGER     0
#define GATINGTRIGGER   0xff

/* MIB trigger modes */
#define ONESHOT         0
#define CONT            1
#define SILENT          2
#define DEACTIVATED     0xffff

/* MIB trigger supervision events */
#define RISING                1
#define RSSIB_RISING          2
#define ANTENNABAR_RISING     3
#define RSSID_RISING          4
#define SNRB_RISING           5
#define SNRD_RISING           6
#define MISSEDBEACON_RISING   7
#define PER_RISING            8
#define PER1_RISING           9

#define FALLING               101
#define RSSIB_FALLING         102
#define ANTENNABAR_FALLING    103
#define RSSID_FALLING         104
#define SNRB_FALLING          105
#define SNRD_FALLING          106
#define MISSEDBEACON_FALLING  107
#define PER_FALLING           108
#define PER1_FALLING          109

#define MATCHING              200

/* bit mask for MIB_dot11DHCPBroadcastFilter "5.22.4" */
#define UDP_BROADCAST_FILTER_FLAG_BOOTP 0x1
#define UDP_BROADCAST_FILTER_FLAG_SSDP  0x2
#define UDP_BROADCAST_FILTER_FLAG_NETBIOS_NAME_SERVICE     0x04
#define UDP_BROADCAST_FILTER_FLAG_NETBIOS_DATAGRAM_SERVICE 0x08
#define UDP_BROADCAST_FILTER_FLAG_NETBIOS_SESSION_SERVICE  0x10

/* MIB object reference types. */
#define MIB_REFERENCE_BY_IDENTIFIER  0x00
#define MIB_REFERENCE_BY_OBJECT      0x01

typedef struct
{
   uint32_t reference;
   uint32_t storage_description;
}mib_object_entry_t;

typedef union
{
   mib_object_entry_t   object;
   char                 identifier[MIB_IDENTIFIER_MAX_LENGTH];
}mib_reference_t; 

/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   char              identifier[MIB_IDENTIFIER_MAX_LENGTH];
}mlme_mib_get_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   char              identifier[MIB_IDENTIFIER_MAX_LENGTH];
   varstring_t       value;
}mlme_mib_set_req_t;

typedef struct
{
   mac_api_transid_t    trans_id;
   char                 identifier[MIB_IDENTIFIER_MAX_LENGTH];
   mib_object_entry_t   object; /* mib_get_object(dotstring, MIB_IDENTIFIER_MAX_LENGTH, &object); */
}mlme_mib_get_raw_req_t;

typedef struct
{
   mac_api_transid_t    trans_id;
   char                 identifier[MIB_IDENTIFIER_MAX_LENGTH];
   mib_object_entry_t   object; /* mib_get_object(dotstring, MIB_IDENTIFIER_MAX_LENGTH, &object); */
   varstring_t          value;
}mlme_mib_set_raw_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           getFirst;
   uint8_t           reserved[3];
}mlme_mib_get_next_req_t;

/* MIB trigger messages */
typedef struct
{
   mac_api_transid_t    trans_id;
   mib_reference_t      reference;
   uint32_t             trigger_id;
   uint32_t             gating_trigger_id;
   uint32_t             supv_interval;
   uint32_t             ind_cb;
   uint32_t             level;
   uint16_t             event;       /* supvevent_t */
   uint16_t             event_count;
   uint16_t             triggmode;
   uint8_t              reference_type; /* MIB_REFERENCE_BY_IDENTIFIER or ... */
   uint8_t              reserved;
}mlme_mib_set_trigger_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          trigger_id;
   uint32_t          gating_trigger_id;
}mlme_mib_set_gatingtrigger_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          trigger_id;
}mlme_mib_remove_trigger_req_t;



/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   mib_result_t      result;
   uint8_t           reserved[3];
   char              identifier[MIB_IDENTIFIER_MAX_LENGTH];
   varstring_t       value;
}mlme_mib_get_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   mib_result_t      result;
   uint8_t           reserved[3];
   char              identifier[MIB_IDENTIFIER_MAX_LENGTH];
}mlme_mib_set_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          trigger_id;
   mib_result_t      result;
   uint8_t           reserved[3];
}mlme_mib_trigger_cfm_t;

typedef mlme_mib_trigger_cfm_t mlme_mib_set_trigger_cfm_t;
typedef mlme_mib_trigger_cfm_t mlme_mib_set_gatingtrigger_cfm_t;
typedef mlme_mib_trigger_cfm_t mlme_mib_remove_trigger_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          trigger_id;
   uint32_t          varsize;
   int32_t           value;
}mlme_mib_trigger_ind_t;


/**********************************************************************************/
/*************************** END OF MESSAGE DEFINITIONS ***************************/
/**********************************************************************************/



typedef union
{
   mac_api_transid_t                transid;
   mlme_mib_get_req_t               mlme_mib_get_req;
   mlme_mib_get_cfm_t               mlme_mib_get_cfm;
   mlme_mib_set_req_t               mlme_mib_set_req;
   mlme_mib_set_cfm_t               mlme_mib_set_cfm;
   mlme_mib_get_raw_req_t           mlme_mib_get_raw_req;
   mlme_mib_set_raw_req_t           mlme_mib_set_raw_req;
   mlme_mib_get_next_req_t          mlme_mib_get_next_req;
   mlme_mib_set_trigger_req_t       mlme_mib_set_trigger_req;
   mlme_mib_set_trigger_cfm_t       mlme_mib_set_trigger_cfm;
   mlme_mib_remove_trigger_req_t    mlme_mib_remove_trigger_req;
   mlme_mib_remove_trigger_cfm_t    mlme_mib_remove_trigger_cfm;
   mlme_mib_trigger_ind_t           mlme_mib_trigger_ind;
   mlme_mib_set_gatingtrigger_req_t mlme_mib_set_gatingtrigger_req;
   mlme_mib_set_gatingtrigger_cfm_t mlme_mib_set_gatingtrigger_cfm;
}mac_mib_body_t;


/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_MIB_H */
/* END OF FILE ***************************************************************/
