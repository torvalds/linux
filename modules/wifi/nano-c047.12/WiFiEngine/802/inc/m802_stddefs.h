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
#ifndef M802_STDDEFS_H
#define M802_STDDEFS_H

/* E X P O R T E D  D E F I N E S ********************************************/
#define M802_ISO_IEC802_MAX_LEN  0x0600
#define M802_ADDRESS_SIZE        6
#define M802_CFI_FLAG            0x1000

/* E X P O R T E D  D A T A T Y P E S ****************************************/
typedef uint16_t m802_ethertype_t;

typedef struct 
{
   char octet[M802_ADDRESS_SIZE];
}m802_mac_addr_t;

typedef struct
{
   m802_mac_addr_t  dst;
   m802_mac_addr_t  src;
   m802_ethertype_t type_len;
}m802_ethernet_header_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* M802_STDDEFS_H */
/* END OF FILE ***************************************************************/
