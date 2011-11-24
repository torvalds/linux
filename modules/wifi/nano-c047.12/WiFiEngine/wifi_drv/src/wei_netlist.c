/* $Id: wei_netlist.c,v 1.109 2008-05-19 15:03:24 peek Exp $ */

/** @defgroup wei_netlist WiFiEngine internal net descriptor implementation
 *
 * 
 * \brief  This module implements WiFiEngine net descriptors (WiFiEngine_net_t).
 * Net descriptors are used to represent known networks, essentially beacon
 * frames, in the driver. The module implements the descriptors and
 * utility functions to allocate, free and manipulate the descriptors
 * and the various lists/slots that they can occupy.
 *
 * A net descriptor is a dynamically allocated object that is assumed
 * to be uniquely identified by the tuple {BSSID,SSID}. Net
 * descriptors are allocated in mlme_proxy.c when handling a scan
 * confirm message.  Descriptors reside on a list, the active
 * list. The active list is used to report known nets to the
 * OS/application. The list handle is kept in
 * wifiEngineState.net_state
 *
 * Before allocating a new descriptor the active list is checked to
 * see if a descriptor with matching {BSSID,SSID} is already
 * present. If so the existing descriptor is used.
 *
 * net descriptors need to be allocated in non-paged memory
 * but the bss_description buffer can be allocated in paged memory
 * (only the list heads needs to be available under a spinlock).
 * Don't allocate net descriptors manually, use functions in this
 * module instead to avoid alloc/free mismatches with respect to
 * paged/nonpaged memory.
 *
 * When a net descriptor is selected for a join attempt it is removed
 * from the active list and and stored separately in
 * registry.network.current_net.
 *
 * Only remove nets from the active list by calling
 * wei_netlist_net_remove_from_active, that will ensure
 * that the active_list pointers in the registry are
 * correctly updated if the net is at the head of the list
 * (WEI_LIST_REMOVE_NAMED() will only unlink the entry from the
 * chain. It does not know of the registry list pointers).
 *
 *  @{
 */

#include "sysdef.h"
#include "sys_cfg.h"
#include "wei_list.h"
#include "wei_netlist.h"
#include "wifi_engine_internal.h"
#include "registry.h"
#include "registryAccess.h"
#include "macWrapper.h"
#include "hicWrapper.h"
   
/*!
 * Frees the network descriptor and the associated bss descriptor
 * if the descriptor is not kept on any net list. If the descriptor 
 * is still in a net list this call is a no-op.
 */
void wei_netlist_free_net_safe(WiFiEngine_net_t *net)
{
   if(net == NULL)
      return;
   DE_ASSERT(net->ref_cnt >= 0);
   if (net->ref_cnt == 0)
   {
      WrapperFreeStructure(net->bss_p);
      DriverEnvironment_Nonpaged_Free(net);
   }
}


void wei_netlist_clear_active_nets()
{
   WiFiEngine_net_t *p, *net;
   wei_netlist_state_t *netlist;
   
   netlist = wei_netlist_get_net_state();
   
   net = netlist->active_list_ref;
   
   while (net)
   {
      p = net;
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
      wei_netlist_remove_from_active(p);
      wei_netlist_free_net_safe(p);
   }
   netlist->active_list_ref = NULL;
}

void wei_netlist_clear_sta_nets(void)
{
   WiFiEngine_net_t *p, *net;
   wei_netlist_state_t *netlist;
   
   netlist = wei_netlist_get_net_state();
   
   net = netlist->sta_list_ref;
   
   while (net)
   {
      p = net;
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, sta_list);
      wei_netlist_remove_from_sta(p);
      wei_netlist_free_net_safe(p);
   }
   netlist->sta_list_ref = NULL;
}

void wei_netlist_free_all_nets()
{
   WiFiEngine_net_t *net;
   
   wei_netlist_clear_active_nets();
   wei_netlist_clear_sta_nets();

   net = wei_netlist_get_current_net();
   if (net)
   {
      wei_netlist_remove_reference(&wifiEngineState.net_state.current_net);
      wei_netlist_free_net_safe(net);
   }
}

void wei_netlist_add_reference(WiFiEngine_net_t **dst, WiFiEngine_net_t *net)
{
   DE_ASSERT(*dst == NULL);
   *dst = net;
   if (net)
   {
      net->ref_cnt++;
   }
}

void wei_netlist_remove_reference(WiFiEngine_net_t **dst)
{
   if (*dst)
   {
      (*dst)->ref_cnt--;
   }
   *dst = NULL;
}

/* This will allocate memory for a new net */
WiFiEngine_net_t *wei_netlist_create_new_net(void)
{
   WiFiEngine_net_t*      net;

   /* This net is new, allocate a new entry for it */
   /* Only the list heads need to be accessed under a spinlock (in
      __wei_list_insert/remove) so it is safe to allocate the rest
      (bss) using paged memory */
   net = (WiFiEngine_net_t *)DriverEnvironment_Nonpaged_Malloc(sizeof *net);

   if (NULL == net)
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to allocate memory for net\n");
      return NULL;
   }

   DE_MEMSET(net, 0, sizeof *net);

   net->bss_p = (mlme_bss_description_t*)WrapperAllocStructure(NULL, sizeof(mlme_bss_description_t));
   net->status               = weNetworkStatus_Unknown;
   net->listenInterval       = registry.powerManagement.listenInterval;
   net->channels.no_channels = 0;
   net->ref_cnt              = 0;
#ifdef USE_NEW_AGE
   net->heard_at_scan_count  = 0;
#endif
   net->last_heard_tick      = 0;
   net->fail_count           = 0;

   WEI_INIT_LIST_HEAD_NAMED(net, active_list);
   WEI_INIT_LIST_HEAD_NAMED(net, sta_list);

   return net;
}

/* If we have a noise floor, we use this for SNR (relative to RSSI)
 * instead of actual measured SNR level (which depends too much on ADC
 * characteristics for marketing to be happy). If we don't know the
 * noise floor (or if there are no measurements yet), we use old SNR
 * level.
 *
 * Since this value is calculated in the driver, scan filters based on
 * SNR may have unexpected results.
 */
static void
wei_update_snr_from_noise_floor(mlme_bss_description_t *bss)
{
   unsigned int channel;
   struct we_noise_floor noise_floor;

   if(bss == NULL || bss->bss_description_p == NULL)
      return;

   if(bss->bss_description_p->ie.ds_parameter_set.hdr.id
      != M80211_IE_ID_DS_PAR_SET)
      return;

   channel = bss->bss_description_p->ie.ds_parameter_set.channel;
   if(channel < 1 || channel > 14)
      return;

   channel--; /* zero base */

   if(WiFiEngine_GetNoiseFloor(&noise_floor) != WIFI_ENGINE_SUCCESS)
      return;

   if(noise_floor.noise_dbm[channel] != NOISE_UNKNOWN) {
      bss->snr_info = bss->rssi_info - noise_floor.noise_dbm[channel];
   }
}

static int
wei_update_net_from_bss_descr(WiFiEngine_net_t *net, 
                              mlme_bss_description_t *new_bss)
{
   mlme_bss_description_t *tmp1, *tmp2;

   net->peer_mac = new_bss->bssId;
   net->bssId_AP = new_bss->bssId;

   tmp1 = (mlme_bss_description_t*)WrapperAllocStructure(NULL, sizeof(*tmp1));
   WrapperCopy_mlme_bss_description_t(tmp1, tmp1, new_bss);
   if(tmp1->dtim_period == 0)
   {
      /* since this scan ind was a result from a probe response, try
       * to keep any dtim period received earlier from a beacon */
      tmp1->dtim_period = net->bss_p->dtim_period;
   } 
   net->measured_snr = tmp1->snr_info;

   wei_update_snr_from_noise_floor(tmp1);

   /* swap in new and free old bss_descriptor */
   tmp2 = net->bss_p;
   net->bss_p = tmp1;
   WrapperFreeStructure(tmp2);
   
   return TRUE;
}

static int
wei_net_matches_bss_descr(WiFiEngine_net_t *net, 
                          mlme_bss_description_t *new_bss)
{
   return wei_equal_bssid(net->bssId_AP, new_bss->bssId)
      && wei_equal_ssid(net->bss_p->bss_description_p->ie.ssid, 
                        new_bss->bss_description_p->ie.ssid);
}

/*
 * XXX: Should we start to use this function from other places than
 * receiving scan inds, we must start to protect the list entries.
 */
WiFiEngine_net_t *
wei_netlist_add_net_to_active_list(mlme_bss_description_t *new_bss)
{
   WiFiEngine_net_t*      net;
   wei_netlist_state_t*        netlist;
   
   DE_ASSERT(new_bss != NULL);


   /* walk the active list to check if this net is already present */
   netlist = wei_netlist_get_net_state();
   for(net = netlist->active_list_ref; 
       net != NULL;
       net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list)) {
      if(wei_net_matches_bss_descr(net, new_bss)) {
         wei_update_net_from_bss_descr(net, new_bss);
         return net;
      }
   }

   if((net = wei_netlist_get_current_net()) != NULL) {
      if(wei_net_matches_bss_descr(net, new_bss)) {
         wei_update_net_from_bss_descr(net, new_bss);
         /* Link it */
         wei_netlist_add_to_active(net);          
         return net;
      }
   }

   net = wei_netlist_create_new_net();
   wei_update_net_from_bss_descr(net, new_bss); /* a bit redundant work */

   /* Link it */
   wei_netlist_add_to_active(net);

   return net;
}

/* This will merge sta nets into the sta list. The list is used to handle STA:s 
   in an ibss net. By definition the ibss net defined is uniqley by ssid and bssid.
   Each sta in the net is identified by its peer mac address.
   To avoid confusion: even though this method adds new nets it is a single ibss
   net. The net desriptor is used to hold the peer mac for the sta.
   In this implementation we will resuse the bss description for the current/associated
   net to be used by all sta:s */
WiFiEngine_net_t*  wei_netlist_add_net_to_sta_list(m80211_nrp_mlme_peer_status_ind_t *new_sta)
{
   WiFiEngine_net_t*      netMaster;
   WiFiEngine_net_t*      net;
   
   DE_ASSERT(new_sta != NULL);

   net = wei_netlist_get_current_net();
   if (net)
   {
      /* If new incoming STA has a new bssid we shall adapt to it */
      if (!wei_equal_bssid(net->bssId_AP, new_sta->bssid))
      {
         DE_MEMCPY(net->bssId_AP.octet, new_sta->bssid.octet, sizeof(new_sta->bssid));

      }
      
      if (wei_equal_bssid(net->peer_mac, new_sta->peer_mac))
      {
         DE_TRACE_STATIC(TR_NETLIST, "Found net in current_net\n");
         return net;
      }
   }

   netMaster = wei_netlist_get_current_net();
   DE_ASSERT(netMaster != NULL);

   /* Check if already in sta list */
   net = wei_netlist_get_sta_net_by_peer_mac_address(new_sta->peer_mac);
   if(net != NULL)
   {
      return net;
   }
   else
   {
      /* Create net and add it to sta list */
      net = wei_netlist_create_new_net();
      DE_MEMCPY(net->peer_mac.octet, new_sta->peer_mac.octet, sizeof(new_sta->peer_mac));
      DE_MEMCPY(net->bssId_AP.octet, new_sta->bssid.octet, sizeof(new_sta->bssid));

      WrapperFreeStructure(net->bss_p);
      net->bss_p = (mlme_bss_description_t*)WrapperAllocStructure(NULL, sizeof(*net->bss_p));
      WrapperCopy_mlme_bss_description_t(net->bss_p, net->bss_p, netMaster->bss_p);

      /* Link it */
      wei_netlist_add_to_sta(net);
   }
   return net;
}

/*!
 * Search the sta list for the given peer mac address.
 * @return NULL on failure.
 */
WiFiEngine_net_t *wei_netlist_get_sta_net_by_peer_mac_address(rBSSID bssid)
{
   WiFiEngine_net_t *net;
   wei_netlist_state_t *netstate;

   netstate = wei_netlist_get_net_state();
   net = netstate->sta_list_ref;

   while (net)
   {
      if (wei_equal_bssid(bssid, net->peer_mac))
      {
         return net;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, sta_list);
   }

   return net;
}

/*!
 * Search the active list for the next occurance of given ssid.
 * @return NULL on failure.
 */
WiFiEngine_net_t *wei_netlist_get_next_net_by_ssid(m80211_ie_ssid_t ssid, WiFiEngine_net_t *net)
{
   DE_ASSERT(net != NULL);

   net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   while (net)
   {
      if (wei_equal_ssid(ssid, net->bss_p->bss_description_p->ie.ssid))
      {
         return net;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   }

   return net;
}

/*!
 * Search the active list for the given ssid.
 * @return NULL on failure.
 */
WiFiEngine_net_t *wei_netlist_get_net_by_ssid(m80211_ie_ssid_t ssid)
{
   WiFiEngine_net_t *net;
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   net = netlist->active_list_ref;

   while (net)
   {
      if (wei_equal_ssid(ssid, net->bss_p->bss_description_p->ie.ssid))
      {
         return net;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   }

   return net;
}

/*!
 * Search the active list for ssid and
 * for each found net store channel (ds parameter)
 * @return NULL on failure.
 */
void wei_netlist_get_nets_ch_by_ssid(m80211_ie_ssid_t ssid, channel_list_t *channel_list)
{
   WiFiEngine_net_t *net;
   wei_netlist_state_t *netlist;
   int i = 0;

   channel_list->no_channels = 0;
   netlist = wei_netlist_get_net_state();
   net = netlist->active_list_ref;

   while (net)
   {
      if(wei_equal_ssid(ssid, net->bss_p->bss_description_p->ie.ssid))
      {
         channel_list->no_channels += 1;
         channel_list->channelList[i] = net->bss_p->bss_description_p->ie.ds_parameter_set.channel;
         i++;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   }
}


/*!
 * Search the active list for the given bssid
 * @return NULL on failure.
 */
WiFiEngine_net_t *wei_netlist_get_net_by_bssid(m80211_mac_addr_t bssid)
{
   WiFiEngine_net_t *net;
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   net = netlist->active_list_ref;

   while (net)
   {
      if (wei_equal_bssid(bssid, net->bss_p->bssId))
      {
         return net;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   }

   return net;
}

/*!
 * Search the active list for a joinable net (matching desiredSSID and desiredBSSID).
 * @return NULL on failure.
 */
WiFiEngine_net_t *wei_netlist_get_joinable_net()
{
   WiFiEngine_net_t *net;
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   net = netlist->active_list_ref;

   while (net)
   {
      if (wei_is_net_joinable(net))
      {
         return net;
      }
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
   }

   return net;
}

int wei_netlist_count_active()
{
   WiFiEngine_net_t*   net;
   wei_netlist_state_t*     netlist;
   int i = 0;

   netlist = wei_netlist_get_net_state();  

   net = netlist->active_list_ref;
   while (net)
   {
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
      i++;
   }

   return i;
}

int wei_netlist_count_sta()
{
   WiFiEngine_net_t*   net;
   wei_netlist_state_t*     netlist;
   int i = 0;

   netlist = wei_netlist_get_net_state();  

   net = netlist->sta_list_ref;
   while (net)
   {
      net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, sta_list);
      i++;
   }

   return i;
}

void wei_netlist_remove_from_active(WiFiEngine_net_t *net)
{
   wei_netlist_state_t*     netlist;
#ifdef WIFI_DEBUG_ON
   char ssid[M80211_IE_MAX_LENGTH_SSID + 1];
#endif

   netlist = wei_netlist_get_net_state();  
   if (net == netlist->active_list_ref)
   {
      wei_netlist_remove_reference(&netlist->active_list_ref);
      wei_netlist_add_reference(&netlist->active_list_ref, WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list));
      if (netlist->active_list_ref)
      {
         netlist->active_list_ref->ref_cnt--;
      }
   }
   else
   {
      if (WEI_LIST_IS_UNLINKED_NAMED(net, active_list))
      {
         return;
      }
      net->ref_cnt--;
   }
   DE_TRACE3(TR_NETLIST, "Removing net " TR_FPTR " (%s) from active list\n", TR_APTR(net), wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
   WEI_LIST_REMOVE_NAMED(net, active_list);
}

void wei_netlist_remove_from_sta(WiFiEngine_net_t *net)
{
   wei_netlist_state_t *netlist;
   
   netlist = wei_netlist_get_net_state();  
   if (net == netlist->sta_list_ref)
   {
      wei_netlist_remove_reference(&netlist->sta_list_ref);
      wei_netlist_add_reference(&netlist->sta_list_ref, WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, sta_list));
      /* The ref count was increased but the next net is already on that list */
      if (netlist->sta_list_ref)
      {
         netlist->sta_list_ref->ref_cnt--;
      }
   }
   else
   {
      if (WEI_LIST_IS_UNLINKED_NAMED(net, sta_list))
      {
         return;
      }
      net->ref_cnt--;
   }
   
   WEI_LIST_REMOVE_NAMED(net, sta_list);
}

void wei_netlist_remove_current_net(void)
{
   WiFiEngine_net_t *tmp;
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   tmp = netlist->current_net;
   wei_netlist_remove_reference(&netlist->current_net);
   wei_netlist_free_net_safe(tmp);
}

void wei_netlist_add_to_active(WiFiEngine_net_t *net)
{
   wei_netlist_state_t *netlist;
   WiFiEngine_net_t *p, *prev;
#ifdef WIFI_DEBUG_ON
   char ssid_str[M80211_IE_MAX_LENGTH_SSID + 1];
   char bssid_str[6*3 + 1];
#endif

   DE_TRACE7(TR_SCAN, "sort net " TR_FPTR " into list: mac %s ssid %s ch %d rssi %d snr %d\n", TR_APTR(net),
         wei_print_mac(&net->bss_p->bssId, bssid_str, sizeof(bssid_str)),
         wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid_str, sizeof(ssid_str)),
         net->bss_p->bss_description_p->ie.ds_parameter_set.channel,
         net->bss_p->rssi_info,
         net->bss_p->snr_info);

   netlist = wei_netlist_get_net_state();  
   if (netlist->active_list_ref == NULL)
   {
      DE_TRACE_STATIC(TR_NETLIST,"First in empty list\n");
      netlist->active_list_ref = net;
      net->ref_cnt++;
      return;
   }

   /* Find position */
   for (p = netlist->active_list_ref, prev = NULL;
        p != NULL;
        p = WEI_GET_NEXT_LIST_ENTRY_NAMED(p, WiFiEngine_net_t, active_list))
   {
      if (net->bss_p->rssi_info >= p->bss_p->rssi_info)
         break;
      prev = p;
   }

   if (p == net)      
   {
      return;
   }

   if (WEI_LIST_IS_UNLINKED_NAMED(net, active_list))
      net->ref_cnt++;
   else {
      WEI_LIST_REMOVE_NAMED(net, active_list);
   }

   if (prev == NULL)
   {
      DE_TRACE_INT(TR_NETLIST, "First in list, before RSSI %d\n", p->bss_p->rssi_info);
      DE_ASSERT(p != NULL);
      WEI_LIST_INSERT_NAMED(p, net, active_list); /* insert net after p */
      netlist->active_list_ref = net;
      WEI_LIST_REMOVE_NAMED(p, active_list);      /* remove p */
      WEI_LIST_INSERT_NAMED(net, p, active_list); /* reinsert p after net */
      return;
   }

   if (p == NULL) {
      DE_TRACE_INT(TR_NETLIST, "Last in list, after RSSI %d\n", prev->bss_p->rssi_info);
   }
   else {
      DE_TRACE_INT2(TR_NETLIST, "Middle of list, inbetween RSSI %d and %d\n", p->bss_p->rssi_info, prev->bss_p->rssi_info);
   }
   WEI_LIST_INSERT_NAMED(prev, net, active_list);
   return;
}

void wei_netlist_add_to_sta(WiFiEngine_net_t *net)
{
   wei_netlist_state_t *netlist;
   WiFiEngine_net_t *p, *prev;
#ifdef WIFI_DEBUG_ON
   char ssid[M80211_IE_MAX_LENGTH_SSID + 1];
#endif

   DE_TRACE3(TR_NETLIST, "Sort net %p into sta list. RSSI %d\n", net, net->bss_p->rssi_info);
   DE_TRACE_STRING(TR_NETLIST, "SSID %s\n", wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid, sizeof(ssid)));

   netlist = wei_netlist_get_net_state();  
   if (netlist->sta_list_ref == NULL)
   {
      DE_TRACE_STATIC(TR_NETLIST,"First in empty list\n");
      netlist->sta_list_ref = net;
      net->ref_cnt++;
      return;
   }

   /* Find position */
   for (p = netlist->sta_list_ref, prev = NULL;
        p != NULL;
        p = WEI_GET_NEXT_LIST_ENTRY_NAMED(p, WiFiEngine_net_t, sta_list))
   {
      if (net->bss_p->rssi_info >= p->bss_p->rssi_info)
         break;
      prev = p;
   }

   if (p == net)    
   {
      return;
   }

   if (WEI_LIST_IS_UNLINKED_NAMED(net, sta_list))
      net->ref_cnt++;
   else {
      WEI_LIST_REMOVE_NAMED(net, sta_list);
   }

   if (prev == NULL)
   {
      DE_TRACE_INT(TR_NETLIST, "First in list, before RSSI %d\n", p->bss_p->rssi_info);
      DE_ASSERT(p != NULL);
      WEI_LIST_INSERT_NAMED(p, net, sta_list); /* insert net after p */
      netlist->sta_list_ref = net;
      WEI_LIST_REMOVE_NAMED(p, sta_list);      /* remove p */
      WEI_LIST_INSERT_NAMED(net, p, sta_list); /* reinsert p after net */
      return;
   }

   if (p == NULL) {
      DE_TRACE_INT(TR_NETLIST, "Last in list, after RSSI %d\n", prev->bss_p->rssi_info);
   }
   else {
      DE_TRACE_INT2(TR_NETLIST, "Middle of list, inbetween RSSI %d and %d\n", p->bss_p->rssi_info, prev->bss_p->rssi_info);
   }
   WEI_LIST_INSERT_NAMED(prev, net, sta_list);
   return;
}

void wei_netlist_make_current_net(WiFiEngine_net_t *net)
{
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   DE_ASSERT(netlist->current_net == NULL);
   wei_netlist_add_reference(&(netlist->current_net), net);
}

/*!
 * @return 
 * - TRUE if the net was aged and expired.
 * - FALSE if the net was aged but lived.
 */
int wei_netlist_expire_net(WiFiEngine_net_t *net, 
			   driver_tick_t current_time, 
			   driver_tick_t expire_age)
{
   WiFiEngine_net_t *cnet;

#ifdef USE_NEW_AGE
   if (expire_age && (current_time - net->heard_at_scan_count > expire_age))
#else
   if (expire_age && (current_time - net->last_heard_tick > expire_age))
#endif
   {
      cnet = wei_netlist_get_current_net();
      if (cnet && wei_equal_bssid(cnet->peer_mac, net->peer_mac))
      {
         DE_TRACE_STATIC(TR_WEI, "Don't expire the current net\n");
      }
      else
      {
#ifdef WIFI_DEBUG_ON
         char ssid[M80211_IE_MAX_LENGTH_SSID + 1];
#endif

#if DE_PROTECT_FROM_DUP_SCAN_INDS == CFG_ON
         wei_exclude_from_scan(&net->bss_p->bssId, &net->bss_p->bss_description_p->ie.ssid, 0);
#endif
         DE_TRACE_PTR(TR_SCAN, "Expire net %p\n", net);
         DE_TRACE_STRING(TR_SCAN, "SSID %s\n", wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
         wei_netlist_remove_from_active(net);
         wei_netlist_remove_from_sta(net);
         return TRUE;
      }
   }

   return FALSE;
}

int wei_netlist_get_size_of_ies(WiFiEngine_net_t *net)
{
   Blob_t   blob;

   DE_ASSERT(net != NULL && 
	     net->bss_p != NULL && 
	     net->bss_p->bss_description_p != NULL);

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_common_IEs_t(&net->bss_p->bss_description_p->ie, &blob, ACTION_GET_SIZE);

   return (BLOB_CURRENT_SIZE(&blob));
}

void wei_limit_netlist(int size)
{
   wei_netlist_state_t *netlist;
   WiFiEngine_net_t *p, *p2, *cnet;
   int current_net_id;

   //size 0 means unlimited
   if(size == 0)
      return;

   netlist = wei_netlist_get_net_state();  

   p = netlist->active_list_ref;

   if (p == NULL)
   {
      //there where no nets
      return;
   }

   for(current_net_id = 1; current_net_id < size;current_net_id++)
   {
      p = WEI_GET_NEXT_LIST_ENTRY_NAMED(p, WiFiEngine_net_t, active_list);
      
      if(p == NULL)
      {
         //the number of nets where fewer than size
         return;
      }
      
   }

   cnet = wei_netlist_get_current_net();

   p2 = WEI_GET_NEXT_LIST_ENTRY_NAMED(p, WiFiEngine_net_t, active_list);

   while(p2)
   {
      if (cnet && wei_equal_bssid(cnet->peer_mac, p2->peer_mac))
      {
         //Don't expire the current net
      }
      else
      {
         wei_netlist_remove_from_active(p2);
         wei_netlist_free_net_safe(p2);
      }
      p2 = WEI_GET_NEXT_LIST_ENTRY_NAMED(p, WiFiEngine_net_t, active_list);
   }
}

/** @} */ /* End of wei_netlist group */

