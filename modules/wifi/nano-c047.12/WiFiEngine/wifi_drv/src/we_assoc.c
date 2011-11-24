/* $Id: we_assoc.c,v 1.34 2008-05-19 14:57:31 peek Exp $ */
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
This module implements the WiFiEngine association interface.

*****************************************************************************/
/** @defgroup we_assoc WiFiEngine association interface
 *
 * @brief This module implements the association interface
 * of WiFiEngine: connect/disconnect procedures and
 * scan result retrieval.
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
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"

/*!
 * @brief Connect to the SSID previously set through WiFiEngine_SetSSID().
 *
 * Start the association procedure.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Connect(WiFiEngine_net_t *net)
{
   BAIL_IF_UNPLUGGED;

   DE_TRACE_STATIC(TR_AUTH, "Connecting\n");

   DE_ASSERT(net != NULL);
   DE_ASSERT(wei_netlist_get_current_net() == NULL);
   wei_netlist_make_current_net(net);

   if((net->bss_p->bssType == M80211_CAPABILITY_IBSS)&&(net->status == weNetworkStatus_Start))
   {
      wei_sm_queue_sig(INTSIG_NET_START, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE); 
   }
   else
   {
      wei_sm_queue_sig(INTSIG_NET_CONNECT, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE); 
   }
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Join the access point defined by net.
 *
 * @param net  Pointer to the net
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Join(WiFiEngine_net_t *net)
{
   BAIL_IF_UNPLUGGED;

   DE_ASSERT(net != NULL);
   DE_ASSERT(wei_netlist_get_current_net() == NULL);
   wei_netlist_make_current_net(net);

   wei_sm_queue_sig(INTSIG_NET_JOIN, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE); 

   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Authenticate the access point defined by net.
 *
 * @param net  Pointer to the net
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Authenticate(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_AUTHENTICATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE); 

   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}
/*!
 * @brief Associate the access point defined by net.
 *
 * @param net  Pointer to the net
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Associate(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_ASSOCIATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE); 

   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}
/*!
 * @brief Disconnect from the current network.
 *
 * Disconnect from the currently associated SSID (through a Disassociate request).
 * 
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Disconnect(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_DISASSOCIATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
   
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Leave current IBSS net (turn off beacon transmission and cleanup)
 * 
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_LeaveIbss(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_LEAVE_IBSS, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
   
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Disassociate from the current SSID and turn off the radio. 
 * 
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Disassociate(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_DISASSOCIATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
   
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}



/*!
 * @brief Deauthenticate from the current SSID and turn off the radio. 
 * 
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Deauthenticate(void)
{
   BAIL_IF_UNPLUGGED;

   wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
   
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Reconnect to the currently selected SSID
 *
 * Attempts to reconnect to the SSID previously set through WiFiEngine_SetSSID().
 * 
 * @return WIFI_ENGINE_SUCCESS
 * 
 */
int WiFiEngine_Reconnect(void)
{
   BAIL_IF_UNPLUGGED;
   wei_sm_queue_sig(INTSIG_NET_REASSOCIATE, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);

   if(!wei_netlist_get_current_net())
      return WIFI_ENGINE_FAILURE;

   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current list of known networks.
 *
 * Get the list of scan results. This function is deprecated.
 * Use WiFiEngine_GetScanList() (in we_scan.c) instead.
 *
 * @param list Pointer to a allocated reply buffer. Can be null, in
 *             which case entries will be filled in with the required 
 *             number of entries needed.
 * @param entries IN: Number of entries available in list. 
 *               OUT: Number of entries used or needed in list.
 *               Note that the number of entries needed may change
 *               from call to call as stations/access points come and go.
 * @return WIFI_ENGINE_SUCCESS on success.
 *         WIFI_ENGINE_FAILURE_INVALID_LENGTH if the list array was too small.
 *         WIFI_ENGINE_SUCCESS will always be returned if list was NULL
 *         and entries will always contain the number of entries needed.
 */
int WiFiEngine_GetBSSIDList(WiFiEngine_net_t *list, int *entries)
{
   wei_netlist_state_t *nets;
   WiFiEngine_net_t *net, *pnet;
   WiFiEngine_net_t *p = NULL;
   rConnectionPolicy *cp;
   int needed_entries = 0;
   int avail_entries = *entries;
   int r = WIFI_ENGINE_SUCCESS;

   BAIL_IF_UNPLUGGED;
   if (list == NULL)
   {
      avail_entries = 0;
   }
   else
   {
      p = list;
   }
   cp = (rConnectionPolicy *)Registry_GetProperty(ID_connectionPolicy);
   /* FIXME: wei_netlist internals spilling out on the floor! */
   if ( (nets = wei_netlist_get_net_state()) != NULL) {
      if (nets->active_list_ref)
      {
         net = nets->active_list_ref;
         while (net)
         {
            pnet = net;
            net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
            if (wei_netlist_expire_net(pnet, 
#ifdef USE_NEW_AGE
                                       wifiEngineState.scan_count,
                                       cp->scanResultLifetime
#else
                                       DriverEnvironment_GetTicks(),
                                       DriverEnvironment_msec_to_ticks(cp->scanResultLifetime)
#endif
                   ))
            {
               wei_netlist_free_net_safe(pnet);
            }
            else
            {
               needed_entries++;
               if (avail_entries)
               {
                  avail_entries--;
                  wei_weinet2wenet(p, pnet);
                  p++;
               }
               else
               {
                  /* No entries left. Is the array too small? */
                  if (list)
                  {
                     r = WIFI_ENGINE_FAILURE_INVALID_LENGTH;
                  }
               }
            }
         }
      }
   }
   *entries = needed_entries;

   return r;
}

/*!
 * @brief Get currently associated network
 *
 * @return Pointer pointer to the associated net or NULL if none is present
 */
WiFiEngine_net_t *WiFiEngine_GetAssociatedNet()
{
   BAIL_IF_UNPLUGGED;
   return wei_netlist_get_current_net();
}

/*!
 * @brief Get net by SSID
 *
 * @return Pointer pointer to the found net with given SSID or NULL if none is found
 */
WiFiEngine_net_t *WiFiEngine_GetNetBySSID(m80211_ie_ssid_t ssid)
{
   BAIL_IF_UNPLUGGED;
   return wei_netlist_get_net_by_ssid(ssid);
}

/** @} */ /* End of we_assoc group */


