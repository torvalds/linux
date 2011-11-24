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
#ifndef MAC_API_DLM_H
#define MAC_API_DLM_H
#include "mac_api_defs.h"
 
/* E X P O R T E D  D E F I N E S ********************************************/

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_DLM_LOAD_REQ                 (0 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_DLM_LOAD_FAILED_IND          (1 | MAC_API_PRIMITIVE_TYPE_REQ)


/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_DLM_LOAD_CFM                 (0 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_DLM_SWAP_IND                 (1 | MAC_API_PRIMITIVE_TYPE_IND)


/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/

/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t  transid;
   uint32_t           address;            /* Start adress for this DLM page */
   uint32_t           remaining_size;     /* Remaing size not including this page */
   uint32_t           checksum;           /* Aggregated arithmetic checksum counter per byte */
   uint32_t           reserved;           /* Reserved for future use */
   varstring_t        page;               /* DLM page */
} hic_dlm_load_req_t;
typedef struct
{
   mac_api_transid_t  transid;
   uint32_t           reserved;           /* Reserved for future use */
 } hic_dlm_load_failed_ind_t;


/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t  transid;
   uint32_t           load_memory_address; /* Load adress (e.g "source address" or LMA) for this DLM */
   uint32_t           size;                /* Size for this DLM */
   uint32_t           reserved;            /* Reserved for future use */
} hic_dlm_swap_ind_t;

typedef struct
{
   mac_api_transid_t  transid;           
   uint32_t           address;            /* Start address of the next page to load */
   uint32_t           size;               /* Size of the next page to load */
   uint32_t           remaining_size;     /* Remaing size of the DLM */
 } hic_dlm_load_cfm_t;


typedef union
{
   mac_api_transid_t    transid;           
   hic_dlm_load_req_t   hic_dlm_load_req;
   hic_dlm_load_cfm_t   hic_dlm_load_cfm;
   hic_dlm_swap_ind_t   hic_dlm_swap_ind;
} dlm_api_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_DLM_H */
/* END OF FILE ***************************************************************/
