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
$Workfile:  $
$Revision: 1.4 $
--------------------------------------------------------------------

Module Description :
==================
Message pack/unpack interface.

Revision History:
=================
 * Initial revision coefws.
******************************************************************************/
#ifndef PACKER_H
#define PACKER_H

#include "driverenv.h"
#include "ucos_defs.h"
#include "hicWrapper.h"


int  packer_HIC_Pack(hic_message_context_t* msg_ref);
void packer_HIC_Unpack(hic_message_context_t* msg_ref, Blob_t *blob);
int packer_Unpack(hic_message_context_t *msg_ref, Blob_t *blob);

char* packer_DereferencePacket(driver_packet_ref packet,
                               uint16_t*         packetSize, 
                               uint8_t*          packetType,
                               uint8_t*          packetId);


#endif /* PACKER_H */
