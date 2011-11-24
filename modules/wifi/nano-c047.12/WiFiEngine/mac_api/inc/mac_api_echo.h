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
#ifndef MAC_API_ECHO_H
#define MAC_API_ECHO_H
#include "mac_api_defs.h"

/* E X P O R T E D  D E F I N E S ********************************************/


/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define ECHO_REQ                (0  | MAC_API_PRIMITIVE_TYPE_REQ)
#define ECHO_ADVANCED_REQ       (1  | MAC_API_PRIMITIVE_TYPE_REQ)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define ECHO_CFM                (0  | MAC_API_PRIMITIVE_TYPE_CFM)


/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/

/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   char              first_char;
}echo_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          echo_size;
   uint32_t          echo_count;
   char              first_char;
}echo_advanced_req_t;



/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   char              first_char;
}echo_cfm_t;


/**********************************************************************************/
/*************************** END OF MESSAGE DEFINITIONS ***************************/
/**********************************************************************************/


/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_ECHO_H */
/* END OF FILE ***************************************************************/
