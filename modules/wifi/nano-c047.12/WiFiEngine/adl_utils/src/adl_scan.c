/* $Id: globals.c,v 1.3 2007/09/27 08:42:16 maso Exp $ */
/*****************************************************************************

Copyright (c) 2006 by Nanoradio AB

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
Contains platform independent code for simplify the scan procedure on most
platforms.

*****************************************************************************/

#include "driverenv.h"
#include "adl_utils.h"
#include "registryAccess.h"

static uint32_t scan_job = -1;
static nrwifi_scan_done_cb done_cb = NULL;
static struct iobject* scan_complete_h = NULL;

static void scan_done_cb(wi_msg_param_t param, void* priv);

void nrwifi_clear_scanlist(void)
{
   // BUG 2060
   //WiFiEngine_FlushScanList();
}


void nrwifi_scan_all(nrwifi_scan_done_cb callback_fn)
{
   nrwifi_scan_one(callback_fn,"");

   return;
}

void nrwifi_scan_one(nrwifi_scan_done_cb callback_fn, const char* ssid)
{
   m80211_ie_ssid_t nrssid;
   m80211_mac_addr_t bssid;
   channel_list_t ch_list;
   rBasicWiFiProperties* b;

   done_cb = callback_fn;

   we_ind_cond_register(&scan_complete_h, WE_IND_SCAN_COMPLETE,
                     (char *) NULL,
                     scan_done_cb,
                     NULL,
                     RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                     NULL);

   b = (rBasicWiFiProperties*) Registry_GetProperty(ID_basic);

   //setting SSID to broadcast
   nrssid.hdr.id = M80211_IE_ID_SSID;
   nrssid.hdr.len = (m80211_ie_len_t) DE_STRLEN(ssid);
   DE_MEMCPY(nrssid.ssid,ssid,nrssid.hdr.len);


   //setting mac-address of ap to broadcast
   DE_MEMSET(bssid.octet, 0xff, sizeof(bssid.octet));

   //get the channel list from the registry
   ch_list = b->regionalChannels;
   
   if(scan_job == -1)
   {         
      if(WiFiEngine_AddScanJob(&scan_job, //scan job ID
                               nrssid,
                               bssid,
                               1,         //active scan
                               ch_list,
                               3,         //scan both in connected and disconnected mode
                               1,         //Priority
                               FALSE,    //exclude the current AP
                               -1,        //use no scan filter
                               1,         //run every period
                               NULL) != WIFI_ENGINE_SUCCESS)
      {
         DE_TRACE_STATIC(TR_WARN, "WiFiEngine_AddScanJob() failed\n");
         scan_done_cb(NULL,NULL);
         return;
      }
   }

   if(WiFiEngine_TriggerScanJob(scan_job, 40) != WIFI_ENGINE_SUCCESS)
   {
      scan_done_cb(NULL,NULL);
   }
}

static void scan_done_cb(wi_msg_param_t param, void* priv)
{
   int i;
   int num_of_nets = 0;
   WiFiEngine_net_t **netlist= NULL;
   WiFiEngine_net_t *net_array= NULL;

   if(scan_job != -1)
   {
      WiFiEngine_RemoveScanJob(scan_job,NULL);
      scan_job = -1;
   }
   

   WiFiEngine_GetScanList(NULL, &num_of_nets);
   
   if(num_of_nets == 0)
   {
      if(done_cb)
         done_cb(NULL,0);
      return;
   }

   //netlist is an array of _pointers_ to nets (not an array of nets)
   netlist = DriverEnvironment_Malloc(sizeof(WiFiEngine_net_t *) * num_of_nets);

   if(netlist == NULL)
   {
      if(done_cb)
         done_cb(NULL,0);
      return;
   }

   DE_MEMSET(netlist, 0, sizeof(WiFiEngine_net_t *) * num_of_nets);


   net_array = DriverEnvironment_Malloc(sizeof(WiFiEngine_net_t) * num_of_nets);

   if(net_array == NULL)
   {
      DriverEnvironment_Free(netlist);
      if(done_cb)
         done_cb(NULL,0);
      return;
   }

   DE_MEMSET(net_array, 0, sizeof(WiFiEngine_net_t) * num_of_nets);

   if(WiFiEngine_GetScanList(netlist, &num_of_nets) != WIFI_ENGINE_SUCCESS)
   {
      DriverEnvironment_Free(netlist);
      DriverEnvironment_Free(net_array);
      if(done_cb)
         done_cb(NULL,0);
      return;
   }

   /* Copy the network information. 
      Note that this is only ok since we have locked the referred information in memory.
      In prevoius implementation the WiFiEngine_net_t::bss_p member was not safe to use.
    */
   for(i=0; i < num_of_nets;i++)
   {
      net_array[i] = *netlist[i];
   }

   if(done_cb)
      done_cb(net_array, num_of_nets);

   /* The information referred to by net_array will be locked in memory until this call is done. */ 
   WiFiEngine_ReleaseScanList(netlist, num_of_nets);
   DriverEnvironment_Free(netlist);
   DriverEnvironment_Free(net_array);

   return;
}

