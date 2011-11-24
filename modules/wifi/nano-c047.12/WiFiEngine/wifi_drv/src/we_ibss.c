
#include "driverenv.h"

/* make it possible to compile the driver without IBSS */
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)

#include "wei_netlist.h"
#include "registryAccess.h"
#include "hicWrapper.h"
#include "wifi_engine_internal.h"

static void generate_random_mac(int32_t flags, void *mac, size_t len);
static void generate_random_bssid(m80211_mac_addr_t *bssid);



static bool_t init_bss_description(mlme_bss_description_t* bss_p)
{
   rIBSSBeaconProperties* beacon;
   rBasicWiFiProperties*  properties; 
   m80211_ie_ssid_t*      pssid;

   /* Get network information from the registry. */
   beacon = (rIBSSBeaconProperties*)Registry_GetProperty(ID_ibssBeacon);
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic); 

   bss_p->bss_description_p = (m80211_bss_description_t*)WrapperAttachStructure(bss_p, sizeof(m80211_bss_description_t));

   if (bss_p->bss_description_p == NULL)
   {
      return FALSE;
   }    

   bss_p->bssType = M80211_CAPABILITY_IBSS;
   bss_p->dtim_period = (uint8_t)beacon->dtim_period;
   /* timestamps ? */
   bss_p->bss_description_p->beacon_period = beacon->beacon_period;
   /* Capability info when starting IBSS */
   bss_p->bss_description_p->capability_info = DEFAULT_CAPABILITY_INFO | M80211_CAPABILITY_IBSS;
   /* Short slottime field must be 0 in IBSS */
   bss_p->bss_description_p->capability_info &= (~M80211_CAPABILITY_SHORT_SLOTTIME);
   /* ESS must be 0 in IBSS */
   bss_p->bss_description_p->capability_info &= (~M80211_CAPABILITY_ESS);
   /* QoS must be 0 in IBSS with WMM (802.11e may require otherwise) */
   if (properties->enableWMM)
   {
      bss_p->bss_description_p->capability_info &= (~M80211_CAPABILITY_QOS);
   }
   /* Get the SSID */
   REGISTRY_WLOCK();
   pssid = (m80211_ie_ssid_t*)&properties->desiredSSID;
   DE_MEMCPY(&bss_p->bss_description_p->ie.ssid, pssid, sizeof(bss_p->bss_description_p->ie.ssid));
   REGISTRY_WUNLOCK();

   wei_ratelist2ie(&bss_p->bss_description_p->ie.supported_rate_set, &bss_p->bss_description_p->ie.ext_supported_rate_set, 
                   &beacon->supportedRateSet);

   if(wifiEngineState.config.encryptionLimit == Encryption_TKIP)
   {
      /* WPA parameters used*/
      m80211_cipher_suite_t group_suite;
      m80211_cipher_suite_t pairwise_suite;
      m80211_akm_suite_t akm_suite;

      group_suite = M80211_CIPHER_SUITE_TKIP;
      pairwise_suite  = M80211_CIPHER_SUITE_TKIP;
      akm_suite = M80211_AKM_SUITE_PSK;

      wei_build_wpa_ie((void*)bss_p, &bss_p->bss_description_p->ie.wpa_parameter_set, group_suite, pairwise_suite, akm_suite);      

   }
   else if(wifiEngineState.config.encryptionLimit == Encryption_CCMP)
   {
      /* WPA parameters used*/
      m80211_cipher_suite_t group_suite;
      m80211_cipher_suite_t pairwise_suite;
      m80211_akm_suite_t akm_suite;

      group_suite = M80211_CIPHER_SUITE_CCMP;
      pairwise_suite  = M80211_CIPHER_SUITE_CCMP;
      akm_suite = M80211_AKM_SUITE_PSK;

      wei_build_wpa_ie((void*)bss_p, &bss_p->bss_description_p->ie.wpa_parameter_set, group_suite, pairwise_suite, akm_suite);      

   }
   else
   {
      /* WPA parameters not used*/
      bss_p->bss_description_p->ie.wpa_parameter_set.hdr.hdr.id = M80211_IE_ID_NOT_USED;
   }

   /* FH parameters not used*/
   bss_p->bss_description_p->ie.fh_parameter_set.hdr.id = M80211_IE_ID_NOT_USED;
   DE_MEMCPY(&bss_p->bss_description_p->ie.ds_parameter_set, 
             &beacon->tx_channel, 
             sizeof(bss_p->bss_description_p->ie.ds_parameter_set));
   /* CF parameters not used*/
   bss_p->bss_description_p->ie.cf_parameter_set.hdr.id = M80211_IE_ID_NOT_USED;
   DE_MEMCPY(&bss_p->bss_description_p->ie.ibss_parameter_set, 
             &beacon->atim_set, 
             sizeof(bss_p->bss_description_p->ie.ibss_parameter_set));
   /* TIM parameters not used - power save to be used later*/
   bss_p->bss_description_p->ie.tim_parameter_set.hdr.id = M80211_IE_ID_NOT_USED;
   /* RSN parameters not used*/
   bss_p->bss_description_p->ie.rsn_parameter_set.hdr.id = M80211_IE_ID_NOT_USED;
   /* Country parameters not used*/
   bss_p->bss_description_p->ie.country_info_set.hdr.id  = M80211_IE_ID_NOT_USED;
   /* WMM parameters not used*/
   bss_p->bss_description_p->ie.wmm_parameter_element.WMM_hdr.hdr.hdr.id  = M80211_IE_ID_NOT_USED;
   bss_p->bss_description_p->ie.wmm_information_element.WMM_hdr.hdr.hdr.id  = M80211_IE_ID_NOT_USED;
   
   return TRUE;
}

static WiFiEngine_net_t* create_ibss_net(void)
{
   WiFiEngine_net_t *net;
   
   net = wei_netlist_create_new_net();
   if (!net) return NULL;
   
   generate_random_bssid(&net->bssId_AP);
   if (!init_bss_description(net->bss_p)) {
      wei_netlist_free_net_safe(net);
      return NULL;
   }

   wei_netlist_add_to_active(net);
   net->status = weNetworkStatus_Start;
   return net;
}

static WiFiEngine_net_t* find_ibss_net(void)
{
   WiFiEngine_net_t *net;

   rBasicWiFiProperties *basicWiFiProperties;
   basicWiFiProperties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);  

   net = wei_netlist_get_net_by_ssid(basicWiFiProperties->desiredSSID);
   if (!net) net = wei_netlist_get_joinable_net();

   return net;
}


/*! 
 * @brief Look for an IBSS network in the netlist or create one if none exists
 *
 * @param conf
 * @return net
 *
 * switch(net->status) {
 *      case weNetworkStatus_Start:
 *         // a new network was created
 *      case weNetworkStatus__has_not_yet_been_determined
 *         // found a joinable ibss network
 *      default:
 *         // assert
 *   }
 *
 */
WiFiEngine_net_t* wei_find_create_ibss_net(void)
{
   WiFiEngine_net_t *net;

   rBasicWiFiProperties*  properties; 
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic); 

   if (properties->desiredSSID.hdr.id != M80211_IE_ID_SSID)
      return NULL;

   net = find_ibss_net();
   if(!net) net = create_ibss_net();
      
   return net;
}

bool_t  wei_is_ibss_connected(m80211_nrp_mlme_peer_status_ind_t *ind)
{
   bool_t is_connected = FALSE;
   
   if(ind->status == MLME_PEER_STATUS_CONNECTED)
   {
     if(wei_netlist_count_sta() == 0)
     {
        is_connected = TRUE;
     }
   }
   
   /* Store the station in sta list */
   wei_netlist_add_net_to_sta_list(ind);

   return is_connected;
}

void wei_ibss_remove_net(m80211_nrp_mlme_peer_status_ind_t *ind)
{
   WiFiEngine_net_t *net;
   
   net = wei_netlist_get_sta_net_by_peer_mac_address(ind->peer_mac);
   if(net != NULL)
   {
      /* Remove it */
      DE_TRACE_STATIC(TR_ASSOC, "Removing net from sta list\n");
      wei_netlist_remove_from_sta(net);
      wei_netlist_free_net_safe(net);
   }
}


bool_t wei_ibss_disconnect(m80211_nrp_mlme_peer_status_ind_t *ind)
{
   bool_t is_disconnected = FALSE;

   /* if there is no sta left we shall disconnect */
   if(!wei_netlist_count_sta())
   {
      is_disconnected = TRUE;
   }

   return is_disconnected;
}

#else /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */

bool_t  wei_is_ibss_connected(m80211_nrp_mlme_peer_status_ind_t *ind)
{
   return FALSE;
}

bool_t wei_is_ibss_disconnected(m80211_nrp_mlme_peer_status_ind_t *ind)
{
   return TRUE;
}

WiFiEngine_net_t*
wei_find_create_ibss_net(void)
{
   return NULL;
}

#endif /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */

/* 
 * code that is mostly used by ibss byt may be used on some platform. 
 * place them here so that they will be removed by the linker on thous platforms 
 */

/*! 
 * @brief Generate a random MAC address
 *
 * @param flags IN specifies attributes of generated address, it's a bitmask with flags:
 * M80211_MAC_UNICAST, 
 * M80211_MAC_MULTICAST,
 * M80211_MAC_GLOBAL, 
 * M80211_MAC_LOCAL
 * @param mac OUT where the address is stored
 * @param len OUT size of mac buffer, must be M80211_ADDRESS_SIZE
 */
static void generate_random_mac(int32_t flags, void *mac, size_t len)
{
   DE_ASSERT(len == M80211_ADDRESS_SIZE);
 
   DriverEnvironment_RandomData(mac, len);

   ((unsigned char*)mac)[0] &= ~3;
   ((unsigned char*)mac)[0] |= flags & 3;
}

/*!
 * @brief Generate a random valid unicast bssid.
 *
 * @param bssid Output buffer.
 */
static void generate_random_bssid(m80211_mac_addr_t *bssid)
{
   generate_random_mac(M80211_MAC_LOCAL | M80211_MAC_UNICAST,
                        bssid->octet, sizeof(bssid->octet));
}

void WiFiEngine_RandomMAC(int32_t flags, void *mac, size_t len)
{
   generate_random_mac(flags, mac, len);
}

