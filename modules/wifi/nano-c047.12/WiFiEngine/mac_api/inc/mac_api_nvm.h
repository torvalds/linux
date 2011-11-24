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
#ifndef MAC_API_NVM_H
#define MAC_API_NVM_H
#include "mac_api_defs.h"
#include "mac_api_mib.h"

/* E X P O R T E D  D E F I N E S ********************************************/

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_START_PRG_REQ            (0 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_WRITE_FLASH_REQ          (1 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_END_PRG_REQ              (2 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_START_READ_REQ           (3 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_READ_FLASH_REQ           (4 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_END_READ_REQ             (5 | MAC_API_PRIMITIVE_TYPE_REQ)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_START_PRG_CFM            (0 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_WRITE_FLASH_CFM          (1 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_END_PRG_CFM              (2 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_START_READ_CFM           (3 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_READ_FLASH_CFM           (4 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_END_READ_CFM             (5 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_CFM                      (6 | MAC_API_PRIMITIVE_TYPE_CFM)




/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/
#define NVMEM_CFG_IN_NVM_TAG 0xFE
#define NVMEM_VERSION_IN_FLASH 0x01

typedef struct 
{
   uint8_t cfg_tag;
   uint8_t version;
   uint8_t reserved[6];
}nvmem_cfg_header_t;

typedef struct
{
    uint16_t size;
    uint8_t  identifier[MIB_IDENTIFIER_MAX_LENGTH];
    char     data;
}nvmem_mib_post_t;


typedef uint8_t nvmem_result_t;
#define NVMEM_RC_OK     0
#define NVMEM_RC_NOT_OK 1




/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   uint8_t           mem_type;
   uint8_t           sector;
   uint8_t           reserved[2];
}nvmem_start_rw_flash_req_t;


typedef struct
{
   mac_api_transid_t trans_id;
   char              data;
}nvmem_write_flash_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   char              data;
}nvmem_read_flash_req_t;




/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   nvmem_result_t    result;
   uint8_t           reserved[3];
}nvmem_flash_cfm_t;

typedef struct
{
   mac_api_transid_t trans_id;
   char              data;
}nvmem_read_flash_cfm_t;



typedef union
{
   nvmem_start_rw_flash_req_t       nvmem_start_rw_flash_req;
   nvmem_write_flash_req_t          nvmem_write_flash_req;
   nvmem_read_flash_req_t           nvmem_read_flash_req;
   nvmem_flash_cfm_t                nvmem_flash_cfm;
   nvmem_read_flash_cfm_t           nvmem_read_flash_cfm;
}nvmem_api_t;



/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_NVM_H */
/* END OF FILE ***************************************************************/
