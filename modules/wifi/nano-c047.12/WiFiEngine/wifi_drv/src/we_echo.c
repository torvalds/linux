/* $Id: we_data.c,v 1.154 2008/04/07 08:33:27 anob Exp $ */
/*****************************************************************************

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
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
This module implements the WiFiEngine echo function.

*****************************************************************************/
/** @defgroup we_echo
 *
 * @brief Sends echo-request to target, which then should respond with echo-
 * reply. This function is only for development and debuging of the host-
 * interface hardware and driver.
 * Please note that echo-messages has nothing to do with ICMP. The type of echo-
 * protocol used here is a Nanoradio internal and has nothing to do with
 * networks.
 *
 * It is not "fool-proof" and does not honnour WFE settings and configurations.
 *
 *  @{
 */
#include "driverenv.h"
#include "wifi_engine_internal.h"

/*!
 * @brief Prepare echo-request to target.
 *
 * See comment 'WiFiEngine_SendTargetEchoRequest' for complete 
 * comment
 */
int WiFiEngine_SendTargetEchoRequest_Prepare(char * buffer, int size)
{
   uint16_t hdrMsgLength = 0;

   BAIL_IF_UNPLUGGED;

   if(wifiEngineState.config.tx_hic_hdr_size > size)
   {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   if(wifiEngineState.ps_inhibit_state == 0)
   {
      //do not allow this if there is a risk we might sleep...
      return WIFI_ENGINE_FAILURE_PS;
   }

   if(WiFiEngine_isCoredumpEnabled())
   {
      return WIFI_ENGINE_FAILURE_COREDUMP;
   }

   if (DriverEnvironment_acquire_trylock(&wifiEngineState.send_lock) == LOCK_LOCKED)
   {
      return WIFI_ENGINE_FAILURE_LOCK;
   }

   //do not allow data and echo packets to mix, it would complicate things...
   if (wifiEngineState.dataReqPending)
   {
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
      return WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL;
   }

   if(!wei_request_resource_hic(RESOURCE_USER_DATA_PATH))
   {
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);    
      // TODO: rewrite wei_request_resource_hic(...) to return reason for denial so it can be passed on
      return WIFI_ENGINE_FAILURE_PS;
   }
 
   if(wifiEngineState.dataPathState == DATA_PATH_CLOSED)
   {
      wei_release_resource_hic(RESOURCE_USER_DATA_PATH);
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);      
      return WIFI_ENGINE_FAILURE_DATA_PATH;
   }   

   hdrMsgLength = size;
   HIC_MESSAGE_LENGTH_SET(buffer, hdrMsgLength);
   HIC_MESSAGE_TYPE(buffer)         = HIC_MESSAGE_TYPE_ECHO;
   HIC_MESSAGE_ID(buffer)           = 0;
   HIC_MESSAGE_HDR_SIZE(buffer)     = wifiEngineState.config.tx_hic_hdr_size;
   HIC_MESSAGE_PADDING_SET(buffer, 0);

   DriverEnvironment_release_trylock(&wifiEngineState.send_lock);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Send echo-request to target.
 *
 * This function will send an echo-request to target.
 * This function will fail if:
 * -Power save is not disabled
 * -there is one or more outgoing data packets in target
 * -The supplied buffer is to small
 *
 *
 * @param buffer a buffer that will be filed with data and sent to target.
 *               the memory type must be one that can be handle by lower layers
 *               Please note that the header of the buffer will be changed
 *               The buffer will not be freed or locked after this function has
 *               returned
 *
 * @param size   size of the buffer
 */
int WiFiEngine_SendTargetEchoRequest(char * buffer, int size)
{
   int result;
   
   result = WiFiEngine_SendTargetEchoRequest_Prepare(buffer, size);
   if(result != WIFI_ENGINE_SUCCESS)
   {
      return result;
   }   
      
   if(DriverEnvironment_HIC_Send(buffer,size) != DRIVERENVIRONMENT_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE_DATA_PATH;
   }

   return WIFI_ENGINE_SUCCESS;
}


/** @} */ /* End of we_echo group */
