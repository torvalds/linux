/* Copyright (c) 2004 by COMPANY_NAME */

/*****************************************************************************/

/*! \file

  \brief 
*/
/*****************************************************************************/
#ifndef HIC_DEFS_H
#define HIC_DEFS_H

#ifndef WIFI_ENGINE
#include "s_bufman.h"
#endif

/* E X P O R T E D  D E F I N E S ********************************************/
#if 0
#define HIC_MESSAGE_TYPE_DATA        0
#define HIC_MESSAGE_TYPE_MGMT        1
#define HIC_MESSAGE_TYPE_MIB         2
#define HIC_MESSAGE_TYPE_ECHO        3
#define HIC_MESSAGE_TYPE_CONSOLE     4
#define HIC_MESSAGE_TYPE_FLASH_PRG   5
#define HIC_MESSAGE_TYPE_CUSTOM      6
#define HIC_MESSAGE_TYPE_CTRL        7
#define HIC_MESSAGE_TYPE_DLM         8 
#define HIC_MESSAGE_TYPE_NUM_TYPES   9
#endif

#define FW_6_6_x
#ifdef FW_6_6_x
#define HIC_MESSAGE_TYPE_AGGR        63
#else
#define HIC_MESSAGE_TYPE_AGGR        255
#endif

#define HIC_MESSAGE_ID_SIMPLE_AGGREGATION 0

#define HIC_32BIT_ALIGNED_PAYLOAD_OFFSET  3
#define HIC_32BIT_ALIGNED_HEADER_SIZE     8

#define HIC_MIN_UL_MESSAGE_SIZE 32  /* Must be bigger than Tx FIFO size in SDIO block */


#define HIC_MAC_DATA_STATUS_OK     0
#define HIC_MAC_DATA_STATUS_ERROR  1

#define MLME_START_AUDIO_REQ      1
#define MLME_START_AUDIO_CFM      2
#define MLME_STOP_AUDIO_REQ       3
#define MLME_STOP_AUDIO_CFM       4
#define MLME_CLOSE_LOOP_REQ       5
#define MLME_CLOSE_LOOP_CFM       6
#define MLME_OPEN_LOOP_REQ        7
#define MLME_OPEN_LOOP_CFM        8

#define HIC_NO_ENTRY_FOUND        0xFF


/* E X P O R T E D  D A T A T Y P E S ****************************************/
/* Used when receiving data from MAC-layer */

/** @defgroup Group1 Error codes
 *  
 *  @{
 */

/*! See #SYSDEF_OBJID_HIC */

typedef enum
{
   HIC_RC_OK = 0,
   HIC_RC_UNKNOWN_SIGNAL_RECEIVED,      /*!< 1 = Unknown message received from host */ 
   HIC_RC_UNDEFINED_CALLBACK,           /*!< 2 = Not used */ 
   NO_RESOURCES,                        /*!< 3 = Not used */ 
   HIC_RC_INVALID_MESSAGE_TYPE_RECEIVED /*!< 4 = Unknown type of message received from host */  
}hic_rc_t;

/** @} */ 

#if 0
typedef uint16_t hic_message_length_t;
typedef uint8_t  hic_message_type_t;
typedef uint8_t  hic_message_id_t;
typedef uint8_t  hic_message_ul_header_size_t;
typedef uint8_t  hic_message_nr_padding_bytes_added_t;

#ifndef WIFI_ENGINE
typedef void (* hic_msg_ind_cb_t)(S_BUF_BufType        payloadDescriptor,
                                  hic_message_id_t     messageId);

typedef void (* hic_cfm_cb_t)(uintptr_t transid);
#endif /* WIFI_ENGINE */

typedef struct
{
   hic_message_type_t                   type;
   hic_message_id_t                     id;
   hic_message_ul_header_size_t         header_size;
   hic_message_nr_padding_bytes_added_t nr_padding_bytes_added;
}hic_message_control_t;

typedef struct
{
   hic_message_length_t  len;
   hic_message_control_t control;
}hic_message_header_t;
#endif


#ifndef WIFI_ENGINE
typedef struct HIC_mac_uplink_data
{
   uint16_t      msgId;
   S_BUF_BufType descriptor;
   void          *param;
} HIC_mac_uplink_data_t;
#endif /* WIFI_ENGINE */

typedef void (* hic_unpack_param_t)(char **source_p, char **dest_p, bool_t update);
typedef void (* hic_pack_param_t)(char **source_p, char **dest_p);

/* I N T E R F A C E  F U N C T I O N S **************************************/
/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/

#endif /* #ifndef HIC_DEFS_H */
/* E N D  O F  F I L E *******************************************************/
