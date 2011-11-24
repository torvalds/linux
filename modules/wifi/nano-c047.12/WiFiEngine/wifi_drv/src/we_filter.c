
#include "driverenv.h"
#include "registry.h"
#include "registryAccess.h"
#include "wifi_engine.h"
#include "wifi_engine_internal.h"
#include "we_cm.h"

/* 
 * error codes returned from filter functions.
 * all are non 0 as to make it possible to eval errors as 
 *
 * s = we_filter_out_...(net);
 * if(s)
 * {
 *    printf("net not allowed, reason code: %d",s);
 * } else {
 *    printf("ok to use net");
 * }
 */

static int ssid_matches(WiFiEngine_net_t* net)
{
   DE_ASSERT(net);

   if (registry.network.basic.desiredSSID.hdr.id == M80211_IE_ID_NOT_USED &&
       wei_is_bssid_bcast(registry.network.basic.desiredBSSID))
   {
      return FALSE;
   }

   return TRUE;
}

static int is_joinable(WiFiEngine_net_t* net)
{
   if (!wei_is_net_joinable(net))
   {
      return FALSE;
   }

   return TRUE;
}

#if 0
static int is_type(WiFiEngine_net_t* net)
{
   if (net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_IBSS)
   {    
      if (Infrastructure_BSS == registry.network.basic.connectionPolicy.defaultBssType)
      {
         return FALSE;
      }      
   }
   else
   {
      if (Independent_BSS == registry.network.basic.connectionPolicy.defaultBssType)
      {
         return FALSE;
      }
   }

   return TRUE;
}
#endif


static int is_auth(WiFiEngine_net_t* net)
{
   m80211_cipher_suite_t            best_pairwise_suite;
   m80211_akm_suite_t               akm;
   wei_ie_type_t                    ie_type;

   if (wei_filter_net_by_authentication(&akm, &ie_type, &net) !=
            WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT2(TR_NET_FILTER, "net akm not acceptable. akm: %d ie_type: %d\n", akm, ie_type);
      return FALSE;
   }

   /* Short primer on the WEP/Protected Frame/Privacy bits :
    *
    * The Privacy bit in the capabilities field in beacons and
    * association responses _require_ data confidentiality (WEP/RSN)
    * when set.
    *
    * The Protected Frame bit (formerly called the WEP bit) in the
    * Frame Control field of data and mgmt frames of type Authentication
    * mean that the frame is encrypted (with WEP/RSN for data, only with
    * WEP for mgmt).
    */

   /* Get the best paiwise suite allowed in the net thats advertised in
    * the same IE/CAP as the AKM suite selected.
    */
   if (wei_get_highest_pairwise_suite_from_net(&best_pairwise_suite, net,
                                               ie_type) != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_AUTH, "Unexpected return from wei_get_highest_pairwise_suite_from_net()\n");
      return FALSE;
   }

   /* Validate the found cipher suite, we may not have found one at all */
   if (best_pairwise_suite == M80211_CIPHER_SUITE_NONE)
   {
      if (net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_PRIVACY)
      {
         DE_TRACE_STATIC(TR_NET_FILTER, "no cipher suite found\n");
         return FALSE;
      }
   }
   else 
   {
      int r;
      r = wei_is_encryption_mode_denied(wei_cipher_suite2encryption(best_pairwise_suite));
      if (FALSE != r)
      {
         DE_TRACE_INT2(TR_NET_FILTER, "Cipher suite %d not allowed as encryption mode in driver, reason %d.\n", best_pairwise_suite, r);
         return FALSE;
      }
   }
   
   /* Validate the authentication mode */
   if (wifiEngineState.config.authenticationMode == Authentication_Shared
       && best_pairwise_suite != M80211_CIPHER_SUITE_WEP104)
   {
      DE_TRACE_STATIC(TR_NET_FILTER, "invalid auth mode. WEP required\n");
      return FALSE;
   }

   return TRUE;
}

static int is_channel(WiFiEngine_net_t* net)
{
   int i;

   /* Checking that the net we want to join is on a channel in the channel-list */ 
   for(i=0; i < registry.network.basic.regionalChannels.no_channels; i++)
   {
      if(net->bss_p->bss_description_p->ie.ds_parameter_set.channel == 
         registry.network.basic.regionalChannels.channelList[i]) 
         break;
   }
   if(i == registry.network.basic.regionalChannels.no_channels)
   {
      DE_TRACE_INT(TR_NET_FILTER, "Failing join, net was not on allowed channel(%u)\n",net->bss_p->bss_description_p->ie.ds_parameter_set.channel);
      return FALSE;
   }

   return TRUE;
}

static int is_multidomain(WiFiEngine_net_t* net)
{
#ifdef MULTIDOMAIN_ENABLED_NOTYET
   /* Update our active channels list from the net */
   if (registry.network.basic.multiDomainCapabilityEnabled)
   {
      if (registry.network.basic.multiDomainCapabilityEnforced 
         && (wifiEngineState.active_channels_ref == NULL))
      {
         DE_TRACE_STATIC(TR_NET_FILTER, "Failing join, no country info in beacon and 802.11d is mandatory\n");
         return FALSE;
      }
   }
#endif /* MULTIDOMAIN_ENABLED_NOTYET */

   return TRUE;
}


/* Compare ssid and bssid to registry 
 *
 */ 
int we_filter_out_ssid(WiFiEngine_net_t* net)
{
   if (net == NULL)
      return WIFI_NET_FILTER_NULL;

   if(!is_joinable(net))
      return WIFI_NET_FILTER_JOINABLE;

   if(!ssid_matches(net))
      return WIFI_NET_FILTER_BSSID;

   return FALSE;
}


/* huge filtering previously done in mlme_proxy:Mlme_CreateJoinRequest() */
int we_filter_out_net(WiFiEngine_net_t* net)
{
   int s;
   DE_TRACE_STACK_USAGE;

   if (net == NULL)
   {     
      return WIFI_NET_FILTER_NULL;
   }

#if 0
   if(!is_type(net))
      return WIFI_NET_FILTER_TYPE;
#endif

   s = we_filter_out_ssid(net);
   if(s) return s;

   if(!is_auth(net))
      return WIFI_NET_FILTER_AUTH;

   if(!is_multidomain(net))
      return WIFI_NET_FILTER_MULTIDOMAIN;

   if(!is_channel(net))
      return WIFI_NET_FILTER_CHANNEL;
   
   // TODO: rate support
   // TODO: channel 14 and g rates

   return FALSE;
}


int we_filter_out_wps_configured(WiFiEngine_net_t* net)
{
   m80211_ie_wps_parameter_set_t *ie;
   
   ie = &net->bss_p->bss_description_p->ie.wps_parameter_set;

   if(ie->hdr.hdr.id == M80211_IE_ID_NOT_USED)
      return WIFI_NET_FILTER_NO_IE;

   if(!m80211_ie_is_wps_configured(ie))
      return WIFI_NET_FILTER_WPS_NOT_CONFIGURED;

   return FALSE;
}


WiFiEngine_net_t* 
WiFiEngine_elect_net(net_filter_t filter_out, void *filter_priv, int use_standard_filters)
{
   WiFiEngine_net_t* candidate_net;
   wei_netlist_state_t *netlist;
   WiFiEngine_net_t* elected_net = NULL;
   int r;

   netlist = wei_netlist_get_net_state();

   for (candidate_net = netlist->active_list_ref;
         candidate_net != NULL;
         candidate_net = WEI_GET_NEXT_LIST_ENTRY_NAMED(candidate_net,
                         WiFiEngine_net_t,
                         active_list)) {

      if(use_standard_filters)
      {
         r = we_filter_out_net(candidate_net);
         if (r) {
            DE_TRACE_INT2(TR_NET_FILTER, "net " TR_FPTR " rejected %d\n",
                          TR_APTR(candidate_net) ,r);
            continue;
         }
      }

      if(filter_out && filter_out(candidate_net,filter_priv))
         continue;

      /* First match */
      if (elected_net == NULL) {
         elected_net = candidate_net;
         continue;
      }

      /* sort net */
      elected_net = candidate_net;
   }

   return elected_net;
}
