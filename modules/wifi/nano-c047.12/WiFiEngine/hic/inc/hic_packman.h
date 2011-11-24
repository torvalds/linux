/******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the software remain the
property of Nanoradio AB.  This software may only be used in accordance
with the corresponding license agreement.  Any unauthorized use, duplication,
transmission, distribution, or disclosure of this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without notice.

Nanoradio AB 
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

--------------------------------------------------------------------
$Workfile:   hic_packman.h  $
$Revision: 1.5 $
--------------------------------------------------------------------

Module Description :
==================

This header file holds definitions for related to uart.


Revision History:
=================
 * Initial revision coefws.
******************************************************************************/
#ifndef HIC_UNPACK_H
#define HIC_UNPACK_H
#include "hic_defs.h"

/******************************************************************************
C O N S T A N T S / M A C R O S
******************************************************************************/


/******************************************************************************
T Y P E D E F ' S
******************************************************************************/



/***************************************************************************
 I N T E R F A C E  F U N C T I O N S
****************************************************************************/
typedef void (* unpack_param_cb_t)(char **source_p, char **dest_p,uint8_t packed_bytes);
typedef void (* pack_param_cb_t)(char **source_p, char **dest_p, uint8_t packed_bytes);

void hic_set_ul_message_properties(hic_message_ul_header_size_t header_size,
                                   uint8_t                      min_message_size,
                                   uint8_t                      message_modulo);
S_BUF_BufType hic_pack_message_to_host(hic_message_type_t type,
                                       hic_message_id_t   id, 
                                       S_BUF_BufType descriptor_ref);
void hic_unpack_int8(char **source_p, char **dest_p,uint8_t packed_size);
void hic_unpack_int8_aligned_32(char **source_p, char **dest_p, uint8_t packed_bytes);
void hic_unpack_int16(char **source_p, char **dest_p,uint8_t packed_size);
void hic_unpack_int32(char **source_p, char **dest_p,uint8_t packed_size);
void hic_unpack_array(char **source_p, char **dest_p,uint8_t packed_size);
void hic_unpack_array_aligned_16(char **source_p, char **dest_p, uint8_t packed_bytes);
void hic_unpack_ssid(char **source_p, char **dest_p,uint8_t packed_size);

void hic_pack_int8(char **source_p, char **dest_p,uint8_t packed_size);
void hic_pack_int16(char **source_p, char **dest_p,uint8_t packed_size);
void hic_pack_int32(char **source_p, char **dest_p,uint8_t packed_size);
void hic_pack_int32_p(char **source_p, char **dest_p,uint8_t packed_size);
void hic_pack_array(char **source_p, char **dest_p,uint8_t packed_size);
void hic_pack_array_aligned_16(char **source_p, char **dest_p, uint8_t packed_bytes);
void hic_pack_array_aligned_32(char **source_p, char **dest_p, uint8_t packed_bytes);





#endif    /* HIC_UNPACK_H */

