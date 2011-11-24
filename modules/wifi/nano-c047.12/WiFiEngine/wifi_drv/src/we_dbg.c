/* $Id: we_dbg.c,v 1.10 2007-10-05 12:12:32 miwi Exp $ */
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
This module implements the WiFiEngine debugging interface

*****************************************************************************/
/** @defgroup we_dbg WiFiEngine debugging interface
 * 
 * @brief This module contains functions to facilitate debugging of WiFiEngine
 * and the driver.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "registry.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_defs.h"
#include "hmg_traffic.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"


/*!
 * @brief Returns the major state for the WiFiEngine state machine
 *
 * For driver debugging.
 *
 * @return WiFiEngine major state
 */
int WiFiEngine_GetState()
{
   return HMG_GetState_traffic();
}

/*!
 * @brief Returns the sub-state for the WiFiEngine state machine
 *
 * For driver debugging.
 *
 * @return WiFiEngine sub-state
 */
int WiFiEngine_GetSubState()
{
   return HMG_GetSubState_traffic();
}

/*!
 * @brief Check command flow control state.
 *
 * @return 1 if WiFiEngine is waiting for a command reply, 0 otherwise.
 */
int WiFiEngine_GetCmdReplyPendingFlag()
{
   return wifiEngineState.cmdReplyPending > 0;
}

/*!
 * @brief Check data window state.
 *
 * WiFiEngine supports a certain number of simultanously outstanding
 * (tx) data packets for improved throughput. This function returns
 * the number of outstanding data packets. When this number reaches
 * the WiFiEngine data window size transmission is suspended until
 * the windows opens (when data confirm messages come back from
 * the device).
 *
 * @return The number of outstanding data packets awaiting data confirm.
 */ 
int WiFiEngine_GetDataReplyPendingFlag()
{
   return wifiEngineState.dataReqPending;
}

int WiFiEngine_GetDataRequestByAccess(unsigned int *ac_bk,
                                      unsigned int *ac_be,
                                      unsigned int *ac_vi,
                                      unsigned int *ac_vo)
{
   *ac_bk = wifiEngineState.dataReqByPrio[1] + wifiEngineState.dataReqByPrio[2];
   *ac_be = wifiEngineState.dataReqByPrio[0] + wifiEngineState.dataReqByPrio[3];
   *ac_vi = wifiEngineState.dataReqByPrio[4] + wifiEngineState.dataReqByPrio[5];
   *ac_vo = wifiEngineState.dataReqByPrio[6] + wifiEngineState.dataReqByPrio[7];
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the data window size.
 *
 * WiFiEngine supports a certain number of simultanously outstanding
 * (tx) data packets for improved throughput. This function returns
 * the size of the WiFiEngine data window (in number of packets).
 */
int WiFiEngine_GetDataReplyWinSize()
{
   return wifiEngineState.txPktWindowMax;
}

/*!
 * @brief Nanoradio internal use only.
 */
int WiFiEngine_Tickle()
{
   DE_TRACE_STATIC(TR_NOISE, "Tickle clearing cmdReplyPending. Here be dragons!\n");
   wifiEngineState.cmdReplyPending = 0;
   
   return WIFI_ENGINE_SUCCESS;
}

/** @} */ /* End of we_dbg group */


