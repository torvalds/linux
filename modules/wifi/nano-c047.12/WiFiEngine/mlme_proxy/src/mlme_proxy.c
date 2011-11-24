/** @defgroup mlme_proxy WiFiEngine internal message handling implementation
 *
 * \brief This module implements message handlers for WiFiEngine.
 *
 *  The message handlers operate on unpacked messages (passed through
 *  references to hic_message_context_t) and update the state of the
 *  driver as needed by the requested operation. The state machine
 *  (hmg.c) is normally the caller and is updated depending on the return
 *  value.
 *  
 *  The Mlme_Create* calls initialize a message of the appropriate type
 *  which will then be ready for packing and transmission.
 *  
 *  The Mlme_Handle* calls updates the driver state from the
 *  contents of the message.
 *
 *  @{
 */

#include "driverenv.h"
#include "ucos.h"

#include "wei_list.h"
#include "wei_netlist.h"
#include "wifi_engine_internal.h"
#include "m80211_stddefs.h"
#include "hmg_defs.h"
#include "registry.h"
#include "registryAccess.h"
#include "mlme_proxy.h"
#include "wei_asscache.h"
#include "we_dump.h"
#include "macWrapper.h"
#include "hicWrapper.h"
#include <stddef.h> // for offsetof(...)


CandidateInfo  listCandidateInfo[MAX_CANDIDATE_INFO];
int             num_candidates;
m80211_ie_ssid_t candidatesOfSsid;



#ifdef WORKAROUND_FOR_BUFFALO_AP
/*!
* Build a WPS IE based on the default parameters.
*
* @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
*/
static void copy_wps_ie(
      m80211_mlme_associate_req_t *req, 
      m80211_ie_wps_parameter_set_t *dst)
{
   const char tlvs[10] = {
      0x10, /* WPS Version ID 1/2 */
      0x4a, /* WPS Version ID 2/2 */
      0x00, /* WPS Version Len 1/2 */
      0x01, /* WPS Version Len 2/2 */
      0x10, /* WPS Version Val */
      0x10, /* WPS Request Type ID 1/2 */
      0x3a, /* WPS Request Type ID 2/2 */
      0x00, /* WPS Request Type Len 1/2 */
      0x01, /* WPS Request Type Len 2/2 */
      0x01  /* WPS Request Type Val: Enrollee, open 802.1x */ 
   };
   int size;

   size = sizeof(tlvs);
  
   dst->hdr.hdr.id = M80211_IE_ID_VENDOR_SPECIFIC;
   dst->hdr.hdr.len = 4 + size;
   dst->hdr.OUI_1 = 0x00;
   dst->hdr.OUI_2 = 0x50;
   dst->hdr.OUI_3 = 0xF2;
   dst->hdr.OUI_type = 0x04; /* WPS OUI */
 
   /* Allocate a wps pool */
   dst->wps_pool = (char*)WrapperAttachStructure(req, size);
  
   DE_MEMCPY(dst->wps_pool, &tlvs[0], sizeof(tlvs));
}
#endif /* WORKAROUND_FOR_BUFFALO_AP */

#if (DE_CCX == CFG_INCLUDED)
/*!
* Build a CCX IE based on the default parameters.
*
* @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
*/
static void copy_ccx_ie(
      m80211_mlme_associate_req_t *req, 
      m80211_ie_ccx_parameter_set_t *dst)
{

   dst->hdr.hdr.id = M80211_IE_ID_VENDOR_SPECIFIC;
   dst->hdr.hdr.len = 5;
   dst->hdr.OUI_1 = 0x00;
   dst->hdr.OUI_2 = 0x40;
   dst->hdr.OUI_3 = 0x96;
   dst->hdr.OUI_type = 0x03; /* CCX OUI */

   dst->ccx_version=0x04;
}

/*!
* Build a CCX Radio Management IE based on the default parameters.
*
* @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
*/
static void copy_ccx_rm_ie(
      m80211_mlme_associate_req_t *req, 
      m80211_ie_ccx_rm_parameter_set_t *dst)
{

   dst->hdr.hdr.id = M80211_IE_ID_VENDOR_SPECIFIC;
   dst->hdr.hdr.len = 6;
   dst->hdr.OUI_1 = 0x00;
   dst->hdr.OUI_2 = 0x40;
   dst->hdr.OUI_3 = 0x96;
   dst->hdr.OUI_type = 0x01; /* CCX OUI */

   dst->ccx_rm_status=0x01;
}

/*!
* Build a CCX tspec element
*
* @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
*/
static void copy_wmm_tspec_ie(
      m80211_mlme_associate_req_t *req, 
      m80211_wmm_tspec_ie_t *dst,
      char* tspec_body
      )
{
   
   dst->WMM_hdr.hdr.hdr.id = 0xDD;
   dst->WMM_hdr.hdr.hdr.len = 61;
   dst->WMM_hdr.hdr.OUI_1 = 0x00;
   dst->WMM_hdr.hdr.OUI_2 = 0x50;
   dst->WMM_hdr.hdr.OUI_3 = 0xF2;
   dst->WMM_hdr.hdr.OUI_type = 0x02;

   dst->WMM_hdr.OUI_Subtype = 0x02;
   dst->WMM_Protocol_Version = 0x01;

   DE_MEMCPY(&dst->TSPEC_body, tspec_body, 55);
   
}

/*!
* Build a CCX Reassociation Request IE based on the default parameters.
*
* @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
*/
static void copy_ccx_reassoc_req_ie(
      m80211_mlme_associate_req_t *req, 
      m80211_ie_ccx_reassoc_req_parameter_set_t *dst,
     uint64_t ap_timestamp)
{
   uint8_t hash[DE_HASH_SIZE_SHA1]; // Since SHA1_MAC_LEN (20) > MD5_MAC_LEN (16).
   uint8_t data[128];
   int use_sha1 = 0;
   int byte_count = 6;
   
   dst->hdr.hdr.id =  0x9c;
   dst->hdr.hdr.len = 24;
   dst->hdr.OUI_1 = 0x00;
   dst->hdr.OUI_2 = 0x40;
   dst->hdr.OUI_3 = 0x96;
   dst->hdr.OUI_type = 0x0;
   
   DE_MEMCPY(&dst->timestamp, &ap_timestamp, 8); // DIPA XXX timestamp is 8 bytes, LSB should be first!
   DE_MEMCPY(&dst->request_number, &wifiEngineState.ccxState.request_number, 4);
   
   // Prepare data buffer for mic calculation (STA-ID | BSSID | RSNIEmn | Timestamp | RN).
   WiFiEngine_GetMACAddress(&data[0], &byte_count);
   DE_MEMCPY(&data[6], req->peer_sta.octet, 6);
   
   // Available buffer size for WPA / RSN IE.
   byte_count = 128 - 12;  
   
   // Choose between WPA, RSN.
   if (req->ie.wpa_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED) {
      WiFiEngine_PackWPAIE(&req->ie.wpa_parameter_set, &data[12], &byte_count);
        DE_TRACE_INT(TR_ASSOC, "WPA IE LENGTH %d\n",byte_count);
   }
   else if (req->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED) {
      WiFiEngine_PackRSNIE(&req->ie.rsn_parameter_set, &data[12], &byte_count);
        DE_TRACE_INT(TR_ASSOC, "RSN IE LENGTH %d\n",byte_count);
      use_sha1 = 1;
   }
   DE_TRACE_DATA(TR_ASSOC, "WPA/RSN IE: ", &data[12], 10);
 
   byte_count += 12;
   DE_MEMCPY(&data[byte_count], &dst->timestamp, 8);
   byte_count += 8;
   DE_MEMCPY(&data[byte_count], &dst->request_number, 4);
   byte_count += 4;
   
   // Calculate MIC for data buffer.
   if (use_sha1) {
      DriverEnvironment_HMAC_SHA1(wifiEngineState.ccxState.KRK, 16,
                                  data, byte_count,
                                  hash, sizeof(hash));
   } else {
      DriverEnvironment_HMAC_MD5(wifiEngineState.ccxState.KRK, 16,
                                 data, byte_count,
                                 hash, sizeof(hash));
   }
   DE_MEMCPY(dst->MIC, hash,8);  
}


#endif //DE_CCX


static void mlme_build_capability_info(
      uint16_t *capability_info_p, 
      WiFiEngine_net_t *net)
{
   rBasicWiFiProperties* basicWiFiProperties;
   uint16_t cap = DEFAULT_CAPABILITY_INFO; 
   const uint16_t capability_info = net->bss_p->bss_description_p->capability_info;

   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   if (capability_info & M80211_CAPABILITY_ESS)
   {
      cap |= M80211_CAPABILITY_ESS; 
   }
   else
   {
      if (capability_info & M80211_CAPABILITY_IBSS)
      {
         cap |= M80211_CAPABILITY_IBSS; 
      }
   }

   /* This is added because some ap:s need this bit to be set when
      using wep. According to the 802.11 standard this bit is
      not needed to be set */
   if (!m80211_ie_is_wps_configured(&net->bss_p->bss_description_p->ie.wps_parameter_set)
         && (capability_info & M80211_CAPABILITY_PRIVACY))
   {
      /* only do this if the AP isn't setup for WPS */
      cap |= M80211_CAPABILITY_PRIVACY; 
   }
   
   /* Don't advertise the SHORT_SLOTTIME capability when the AP doesn't since
    * some AP's refuse association when they don't match.
    */
   if ((capability_info & M80211_CAPABILITY_SHORT_SLOTTIME) == 0)
   {
      cap &= ~M80211_CAPABILITY_SHORT_SLOTTIME;
   }

   /* Turn on the QOS capability bit if we and the AP supports QoS */
   if (basicWiFiProperties->enableWMM && (capability_info & M80211_CAPABILITY_QOS))
   {
      cap |= M80211_CAPABILITY_QOS;
   }

   *capability_info_p = cap;
}

bool_t Mlme_Send(mlme_primitive_fn_t mlme_primitive, int param, mlme_send_fn_t send_fn)
{
   hic_message_context_t   msg_ref;
   bool_t                  success;

   /* Do a background scan of present networks. */
   Mlme_CreateMessageContext(msg_ref);

   success = mlme_primitive(&msg_ref, param);
   
   if (success)
   {
      success = (send_fn(&msg_ref) == WIFI_ENGINE_SUCCESS) ? TRUE : FALSE;
   }
   
   Mlme_ReleaseMessageContext(msg_ref);

   return success;
}

int Mlme_CreateScanRequest(hic_message_context_t* msg_ref, uint8_t scan_type, 
                           uint32_t job_id, 
                           uint16_t channel_interval, uint32_t probe_delay, 
                           uint16_t min_ch_time, uint16_t max_ch_time)
{
   mlme_direct_scan_req_t* req;

   DE_TRACE_STACK_USAGE;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_direct_scan_req_t);
   if(req == NULL) return FALSE;

   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_SCAN_REQ;

   req->job_id             = job_id;
   req->channel_interval   = channel_interval;
   req->use_default_params = (probe_delay | min_ch_time | max_ch_time) ? 0x0000 : 0xFFFF;
   req->probe_delay        = probe_delay;
   req->min_ch_time        = min_ch_time;
   req->max_ch_time        = max_ch_time;
   
   WEI_SET_TRANSID(req->trans_id);
   
   DE_TRACE_INT2(TR_ASSOC,"send Scan Request, job %d intv %d\n",job_id, req->channel_interval);
   return TRUE;
}

static int
check_candidate(m80211_mlme_scan_ind_t* scan_ind_p)
{
   WiFiEngine_bss_type_t bssType;
   WiFiEngine_Auth_t auth;
   WiFiEngine_Encryption_t e;

   /* First prerequisite (connected to AP) */
   if(WiFiEngine_GetBSSType(&bssType) != WIFI_ENGINE_SUCCESS || 
         (bssType != (WiFiEngine_bss_type_t)Infrastructure_BSS))
   {
      return 0;
   }

   /* Second prerequisite (authentication mode WPA2) added PSK as OK */
   if(WiFiEngine_GetAuthenticationMode(&auth) != WIFI_ENGINE_SUCCESS || 
         (auth != Authentication_WPA_2 && auth != Authentication_WPA_2_PSK))
   {
      return 0;
   }

   if(wei_netlist_get_current_net() == NULL)
   {
      return 0;
   }

   /* Third prerequisite (currently associated to an access point and authenticated through WPA2  */ 
   /* but some Azimuth test cases have TKIP in a WPA2 connection - this is a temporary solution until  */ 
   /* WiFiEngine stores if RSN or WPA was used) */
   if(WiFiEngine_GetEncryptionMode(&e) != WIFI_ENGINE_SUCCESS || 
         (e != Encryption_CCMP && e != Encryption_TKIP)) 
   { 
      return 0;
   }

   if (!wei_equal_ssid(candidatesOfSsid, 
             scan_ind_p->scan_ind_body.bss_description_p->ie.ssid)) {
#ifdef WIFI_DEBUG_ON
      char ssid1[M80211_IE_MAX_LENGTH_SSID];
      char ssid2[M80211_IE_MAX_LENGTH_SSID];
#endif
      
      DE_TRACE3(TR_PMKID,"cOS:%s != ssid:%s\n", 
         wei_printSSID(&candidatesOfSsid, ssid1, sizeof(ssid1)), 
         wei_printSSID(&scan_ind_p->scan_ind_body.bss_description_p->ie.ssid, ssid2, sizeof(ssid2)));
      return 0;
   }
   
   return 1;
}

static int
update_candidate_list(m80211_mac_addr_t bssId, mlme_rssi_dbm_t rssi_info, uint16_t rsn_capabilities)
{
   int i;
#ifdef WIFI_DEBUG_ON
   char tmp[32];
#endif

   for(i = 0; i < MAX_CANDIDATE_INFO; i++) {
      DE_TRACE_STRING(TR_PMKID,"update_candidate_list: checking %s\n", 
                               wei_print_mac(&listCandidateInfo[i].bssId, tmp, sizeof(tmp)));
      if(i >= num_candidates || 
         wei_equal_bssid(listCandidateInfo[i].bssId, bssId))
         break;
   }

   if(i < MAX_CANDIDATE_INFO) {
      DE_TRACE_STRING(TR_PMKID, "CANDIDATE: Adding candidate %s\n",
                                wei_print_mac(&bssId, tmp, sizeof(tmp)));
      wei_copy_bssid(&listCandidateInfo[i].bssId, &bssId);
      listCandidateInfo[i].rssi_info = rssi_info;
      listCandidateInfo[i].flag = rsn_capabilities & 0x01;
      if (num_candidates <= i)
         num_candidates = i + 1;
     return i; 
   }
   DE_TRACE_STATIC(TR_PMKID, "CANDIDATE: Out of space\n");
   return 0; /* This is a so called don't care */
}

static mlme_scan_filter_t mlme_scan_filter = NULL;
void mlme_set_scan_filter(mlme_scan_filter_t filter)
{
   mlme_scan_filter = filter;
}

bool_t Mlme_HandleScanInd(hic_message_context_t* msg_ref)
{
   WiFiEngine_net_t *net;
   m80211_mlme_scan_ind_t* ind;
   mlme_bss_description_t *sbody;
#ifdef WIFI_DEBUG_ON
   char bssid[32];   
   char ssid[M80211_IE_MAX_LENGTH_SSID];   
#endif
#ifdef MULTIDOMAIN_ENABLED_NOTYET   
   rBasicWiFiProperties*            basicWiFiProperties;
   basicWiFiProperties = 
     (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);     
#endif
   /* Get the raw message buffer from the context. */
   ind = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_mlme_scan_ind_t);
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_SCAN_IND;

   sbody = &ind->scan_ind_body;

   DE_TRACE6(TR_SCAN, "Received net:     mac=%s ch=%d rssi=%d snr=%d ssid=%s \n",
             wei_print_mac(&sbody->bssId, bssid, sizeof(bssid)),
             sbody->bss_description_p->ie.ds_parameter_set.channel,
             sbody->rssi_info,
             sbody->snr_info,
             wei_printSSID(&sbody->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
   

   if(mlme_scan_filter != NULL && FALSE == (*mlme_scan_filter)(sbody)) 
      /* skip this scan ind */
      return TRUE;

#if DE_PROTECT_FROM_DUP_SCAN_INDS == CFG_ON
   if(wei_exclude_from_scan(&sbody->bssId, 
                            &sbody->bss_description_p->ie.ssid, 
                             sbody->bss_description_p->ie.ds_parameter_set.channel)) 
   {
#ifdef WIFI_DEBUG_ON
      char ssid[M80211_IE_MAX_LENGTH_SSID + 1];
      char bssid[32];
#endif
      DE_TRACE6(TR_SCAN, "Excluding net:     mac=%s ch=%d rssi=%d snr=%d ssid=%s \n",
                wei_print_mac(&sbody->bssId, bssid, sizeof(bssid)),
                sbody->bss_description_p->ie.ds_parameter_set.channel,
                sbody->rssi_info,
                sbody->snr_info,
                wei_printSSID(&sbody->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
      return TRUE;
   } 
#endif

/*
         DE_TRACE_STRING(TR_ASSOC, "Adding SSID %s to active\n", wei_printSSID(&sbody->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
*/
         net = wei_netlist_add_net_to_active_list(sbody);
         DE_ASSERT(net != NULL);

#ifdef MULTIDOMAIN_ENABLED_NOTYET
         if (basicWiFiProperties->multiDomainCapabilityEnabled)
         {
            /* Adapt to 802.11d info in beacon */
            if (M80211_IE_ID_COUNTRY == net->bss_p->bss_description_p->ie.country_info_set.hdr.id)
            {
               if (wifiEngineState.active_channels_ref != NULL)
               {
                  WrapperFreeStructure(wifiEngineState.active_channels_ref);
               }
               wifiEngineState.active_channels_ref 
                     = (m80211_ie_country_t*)WrapperAllocStructure(NULL, sizeof(*wifiEngineState.active_channels_ref));
               WrapperCopy_m80211_ie_country_t(wifiEngineState.active_channels_ref, 
                                               wifiEngineState.active_channels_ref, 
                                               &net->bss_p->bss_description_p->ie.country_info_set);
               WES_SET_FLAG(WES_FLAG_COUNTRY_INFO_CHANGED);
            }
         }
#endif /* MULTIDOMAIN_ENABLED_NOTYET */

         if(check_candidate(ind)) 
         {
            int i;
            if (sbody->bss_description_p->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED ) {
               i = update_candidate_list(sbody->bssId, 
                                         sbody->rssi_info, 
                                         sbody->bss_description_p->ie.rsn_parameter_set.rsn_capabilities);
               DriverEnvironment_indicate(WE_IND_CANDIDATE_LIST, &listCandidateInfo[i], sizeof(listCandidateInfo[i])); 
            }
         }
#ifdef USE_NEW_AGE
         net->heard_at_scan_count = wifiEngineState.scan_count;
#endif
         net->last_heard_tick = DriverEnvironment_GetTicks();
         DriverEnvironment_indicate(WE_IND_SCAN_INDICATION, net, sizeof(*net));

#if (DE_MAX_SCAN_LIST_SIZE != 0)
   wei_limit_netlist(DE_MAX_SCAN_LIST_SIZE);
#endif
   
   return TRUE;
}

#if (DE_ENABLE_HT_RATES == CFG_ON)
static int association_uses_tkip(m80211_mlme_associate_req_t*);
#endif

int
wei_build_rate_list(WiFiEngine_net_t* net, 
                    uint32_t *operational_rate_mask, 
                    uint32_t *basic_rate_mask)
{
   rRateList                        tmp_rlist;
   rBasicWiFiProperties*            basicWiFiProperties;
   uint32_t my_orates;
   uint32_t my_brates;
   uint32_t net_orates;
   uint32_t net_brates;
   uint32_t common_rates;
#if (DE_ENABLE_HT_RATES == CFG_ON)
   m80211_mlme_associate_req_t *areq = wei_asscache_get_req();
   int use_ht_rates = areq != NULL && !association_uses_tkip(areq);
#endif

   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   /* combine net rates to single list */
   wei_ie2ratelist(&tmp_rlist, 
                   &net->bss_p->bss_description_p->ie.supported_rate_set,
                   &net->bss_p->bss_description_p->ie.ext_supported_rate_set);

   /* and convert to two rate masks, one for all rates and one for
    * basic rates */
   wei_ratelist2mask(&net_orates, &tmp_rlist);
#if (DE_ENABLE_HT_RATES == CFG_ON)
   if(use_ht_rates && WE_SUPPORTED_HTRATES() != 0) {
      uint32_t tmp_rates;
      wei_htcap2mask(&tmp_rates, 
                     &net->bss_p->bss_description_p->ie.ht_capabilities);
      net_orates |= tmp_rates;
   }
#endif
   wei_prune_nonbasic_ratelist(&tmp_rlist);
   if(!wei_ratelist2mask(&net_brates, &tmp_rlist)) {
      DE_TRACE_STATIC(TR_ASSOC, "AP requires unknown rate\n");
      return WIFI_ENGINE_FAILURE;
   }
#if (DE_ENABLE_HT_RATES == CFG_ON)
   if(use_ht_rates && WE_SUPPORTED_HTRATES() != 0) {
      uint32_t tmp_rates;
      if(!wei_htoper2mask(&tmp_rates, 
                          &net->bss_p->bss_description_p->ie.ht_operation)) {
         DE_TRACE_STATIC(TR_ASSOC, "AP requires unknown rate\n");
         return WIFI_ENGINE_FAILURE;
      }
      net_brates |= tmp_rates;
   }
#endif
                  

   /* do the same for our list of supported rates */
   tmp_rlist = basicWiFiProperties->supportedRateSet;
   wei_ratelist2mask(&my_orates, &tmp_rlist);
   wei_prune_nonbasic_ratelist(&tmp_rlist);
   wei_ratelist2mask(&my_brates, &tmp_rlist);
#if (DE_ENABLE_HT_RATES == CFG_ON)
   my_orates |= WE_SUPPORTED_HTRATES();
   my_brates |= 0; /* XXX where should this come from? */
#endif

   if((my_brates & net_brates) != my_brates) {
      DE_TRACE_INT(TR_WARN, "Required basic rates from registry not present in net rate list (%x)\n", 
                   my_brates & ~net_brates);
      return WIFI_ENGINE_FAILURE;
   }
   /* Check that we support the AP's all basic rates */
   if((net_brates & my_orates) != net_brates) {
      DE_TRACE_INT(TR_ASSOC, "Basic rates in AP not allowed (%x)\n", 
                   net_brates & ~my_orates);
      return WIFI_ENGINE_FAILURE;
   }

#define ALL_BRATES wifiEngineState.rate_bmask

   common_rates = my_orates & net_orates;
   /* Channel 14 (Japan only) may only use B rates */
   if (14 == net->bss_p->bss_description_p->ie.ds_parameter_set.channel) {
      if((net_brates & ~ALL_BRATES) != 0) {
         DE_TRACE_INT(TR_ASSOC, "non-B basic rates on channel 14 (%x)\n", 
                      net_brates & ~ALL_BRATES);
         return WIFI_ENGINE_FAILURE;
      }
      common_rates &= ALL_BRATES;
   }

   *operational_rate_mask = common_rates;
   *basic_rate_mask = net_brates;

   return WIFI_ENGINE_SUCCESS;
}

/* check if a channel is in the list of registry-allowed channels */ 
static int
wei_channel_allowed(unsigned int channel)
{
   int i;
   rBasicWiFiProperties*            basicWiFiProperties;

   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   for(i=0; i < basicWiFiProperties->regionalChannels.no_channels; i++)
   {
      if(channel == basicWiFiProperties->regionalChannels.channelList[i])
         return TRUE;
   }

   return FALSE;
}

#if (DE_CCX_ROAMING == CFG_INCLUDED)
extern WiFiEngine_net_t * ccxnet; // change for reassociation
#endif

int  Mlme_CreateJoinRequest(hic_message_context_t* msg_ref, int dummy)
{
   m80211_mlme_join_req_t*          req;
   WiFiEngine_net_t*                net;
   rBasicWiFiProperties*            basicWiFiProperties;
   bool_t                           result = FALSE;
   int                              i;

   DE_TRACE_STACK_USAGE;

#if (DE_CCX_ROAMING == CFG_INCLUDED)
   if (ccxnet) 
   {
      net = ccxnet;
   }
   else 
   {
     net = wei_netlist_get_current_net();
   }
#else
   net = wei_netlist_get_current_net();
#endif


#ifdef WIFI_DEBUG_ON
   {
      char ssid[M80211_IE_MAX_LENGTH_SSID + 1];
      DE_TRACE_STRING(TR_SM, "JOIN_REQ on SSID \"%s\"\n", wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid, sizeof(ssid)));
   }
#endif

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_join_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_JOIN_REQ;

   /* Get a reference to basic network information from the registry. */
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   i = wei_build_assoc_req(net);
   DE_ASSERT(i == WIFI_ENGINE_SUCCESS);

   /* BSS Description for the selected network. */
   WrapperCopy_mlme_bss_description_t(req, &req->bss, net->bss_p);

   /* Timeouts */
   req->join_timeout = basicWiFiProperties->connectionPolicy.joinTimeout;
   req->probe_delay  = basicWiFiProperties->connectionPolicy.probeDelay;

   /* Update our active channels list from the net */
#ifdef MULTIDOMAIN_ENABLED_NOTYET
   if (basicWiFiProperties->multiDomainCapabilityEnabled)
   {
      /* Is there any country info in the bss description. */
      if (M80211_IE_ID_COUNTRY == net->bss_p->bss_description_p->ie.country_info_set.hdr.id)
      {
         if (wifiEngineState.active_channels_ref != NULL)
         {
            WrapperFreeStructure(wifiEngineState.active_channels_ref);
         }
         wifiEngineState.active_channels_ref 
               = (m80211_ie_country_t*)WrapperAllocStructure(NULL, sizeof(*wifiEngineState.active_channels_ref));
         WrapperCopy_m80211_ie_country_t(wifiEngineState.active_channels_ref,
                                         wifiEngineState.active_channels_ref, 
                                         &net->bss_p->bss_description_p->ie.country_info_set);
      }

      if ( basicWiFiProperties->multiDomainCapabilityEnforced 
       && (wifiEngineState.active_channels_ref == NULL) )
      {
         DE_BUG_ON(1, "Failing join, no country info in beacon and 802.11d is mandatory\n");
      }
   }
#endif /* MULTIDOMAIN_ENABLED_NOTYET */


   if(!wei_channel_allowed(net->bss_p->bss_description_p->ie.ds_parameter_set.channel)) {
      DE_TRACE_INT(TR_ASSOC, "Failing join, net was not on allowed channel(%u)\n",net->bss_p->bss_description_p->ie.ds_parameter_set.channel);
   }

   if(wei_build_rate_list(net, 
                          &req->operational_rate_mask, 
                          &req->basic_rate_mask) != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_WARN, "Failing join, failed to build rate lists\n");
      goto Cleanup;
   }

   if (basicWiFiProperties->enableWMM && 
       (net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_IBSS))
   {
      wei_init_wmm_ie(&req->bss.bss_description_p->ie.wmm_information_element, 1);
   }
   else
   {
      req->bss.bss_description_p->ie.wmm_information_element.WMM_hdr.hdr.hdr.id = M80211_IE_ID_NOT_USED;
   }

   wifiEngineState.main_state = driverJoining;

   net->status = weNetworkStatus_NetAware;
   net->join_time = DriverEnvironment_GetTimestamp_msec();
   
   WEI_SET_TRANSID(req->trans_id);
   DE_TRACE_STATIC(TR_ASSOC, "send join_req\n");
   result = TRUE;

Cleanup:
   if (result == FALSE)
      wifiEngineState.main_state = driverDisconnected;
   return result;
}

/* should move someplace else */
int wei_build_assoc_req(WiFiEngine_net_t *net)
{
   m80211_ie_rsn_parameter_set_t    *rsn_ie;
   m80211_ie_wpa_parameter_set_t    *wpa_ie;
   m80211_cipher_suite_t            best_pairwise_suite;
   m80211_akm_suite_t               akm;
   wei_ie_type_t                    ie_type;
   m80211_mlme_associate_req_t*     assoc_req_p = NULL;
   int                              status;
   
   DE_TRACE_STACK_USAGE;

   /* the net have been filtered before this call, if this fails it is a bug */
   status = wei_filter_net_by_authentication(&akm, &ie_type, &net);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

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

   rsn_ie = &net->bss_p->bss_description_p->ie.rsn_parameter_set;
   wpa_ie = &net->bss_p->bss_description_p->ie.wpa_parameter_set;

   assoc_req_p = (m80211_mlme_associate_req_t*)WrapperAllocStructure(NULL, sizeof(m80211_mlme_associate_req_t));
   
   if(assoc_req_p == NULL)
   {
      return WIFI_ENGINE_FAILURE;
   }
   

   mlme_build_capability_info(
         &assoc_req_p->capability_info, 
         net);

   /* Get the best paiwise suite allowed in the net thats advertised in
    * the same IE/CAP as the AKM suite selected.
    */
   /* the net have been filtered before this call, if this fails it is a bug */
   if (wei_get_highest_pairwise_suite_from_net(&best_pairwise_suite, net,
                                               ie_type) != WIFI_ENGINE_SUCCESS)
   {
      DE_BUG_ON(1, "Unexpected return from wei_get_highest_pairwise_suite_from_net()\n");
      return WIFI_ENGINE_FAILURE;
   }

   switch (ie_type)
   {
      case WEI_IE_TYPE_WAPI:
      {
         if (wei_build_wapi_ie((void*)assoc_req_p, &(assoc_req_p->ie.wapi_parameter_set), akm)
       == WIFI_ENGINE_FAILURE)
     {
       DE_BUG_ON(1, "ERROR: Failed to build new WAPI IE\n");
     }
   
   /* Cache the IE in the association cache, the association handler will pick
    * it up from there. */
   /* if we believe IOL uncomment this line         assoc_req_p->capability_info |= M80211_CAPABILITY_PRIVACY; */
   wei_asscache_add_req(assoc_req_p);
      }
      break;
      case WEI_IE_TYPE_RSN:
      {
         unsigned int npmkid;
         m80211_bssid_info pmkid[16];

         npmkid = WiFiEngine_PMKID_Get(pmkid, DE_ARRAY_SIZE(pmkid),
                                       &net->bssId_AP);

         DE_TRACE_INT(TR_AUTH, "Searching for the best pairwise suite in RSN IE. Number of keys found %d\n", npmkid);
         if (wei_build_rsn_ie((void*)assoc_req_p, &(assoc_req_p->ie.rsn_parameter_set), rsn_ie->group_cipher_suite.type,
                              best_pairwise_suite, akm,
                              npmkid, pmkid)
             == WIFI_ENGINE_FAILURE)
         {
            DE_BUG_ON(1, "ERROR: Failed to build new RSN IE\n");
            return WIFI_ENGINE_FAILURE;
         }
#ifdef BUILD_RSN_IE
         /* we must build the rsn ie */
         assoc_req_p->ie.rsn_parameter_set.rsn_capabilities = 
            M80211_RSN_CAPABILITY_PTKSA_4_REPLAY_COUNTER | M80211_RSN_CAPABILITY_GTKSA_4_REPLAY_COUNTER;
#else
         /* supplicant has provided the ie; no need for us to modify it */
         assoc_req_p->ie.rsn_parameter_set.rsn_capabilities = rsn_ie->rsn_capabilities;
#endif

         /* Cache the IE in the association cache, the association handler will pick
          * it up from there. */
         /* if we believe IOL uncomment this line         assoc_req_p->capability_info |= M80211_CAPABILITY_PRIVACY; */
         wei_asscache_add_req(assoc_req_p);
      }
      break;
      case WEI_IE_TYPE_WPA:
      {
         DE_TRACE_INT(TR_AUTH, "The best pairwise suite (%d) was found in WPA IE.\n", best_pairwise_suite);
         if (wei_build_wpa_ie((void*)assoc_req_p, &(assoc_req_p->ie.wpa_parameter_set), wpa_ie->group_cipher_suite.type,
                              best_pairwise_suite, akm)
             == WIFI_ENGINE_FAILURE)
         {
            DE_BUG_ON(1, "ERROR: Failed to build new WPA IE\n");
            return WIFI_ENGINE_FAILURE;
         }
         /* Cache the IE in the association cache, the association handler will pick
          * it up from there. */
         /* if we believe IOL uncomment this line         assoc_req_p->capability_info |= M80211_CAPABILITY_PRIVACY; */
         wei_asscache_add_req(assoc_req_p);
      }
      break;
      case WEI_IE_TYPE_CAP:
      {
         DE_TRACE_INT(TR_AUTH, "The best pairwise suite (%d) was found in the capabilities field\n", best_pairwise_suite);

         /* Only set the privacy bit when using WEP. */
         if (best_pairwise_suite != M80211_CIPHER_SUITE_NONE)
         {
            assoc_req_p->capability_info |=  M80211_CAPABILITY_PRIVACY;
         }
         /* Cache the IE in the association cache, the association handler will pick
          * it up from there. */
         wei_asscache_add_req(assoc_req_p);
      }
      break;
      default:
         DE_TRACE_INT(TR_AUTH, "ERROR: Unknown IE type %d returned from wei_get_highest_pairwise_suite_from_net()\n", ie_type);
         
   }

   if(assoc_req_p)
   {
      WrapperFreeStructure(assoc_req_p);
   }

   return WIFI_ENGINE_SUCCESS;
}

bool_t Mlme_HandleJoinConfirm(m80211_mlme_join_cfm_t *cfm)
{
   WiFiEngine_net_t*         net;
   DE_TRACE_STACK_USAGE;

   net = wei_netlist_get_current_net();
   DE_TRACE_STATIC(TR_ASSOC, "received Join Confirm\n");

   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_ASSERT(FALSE);
            break;

         case M80211_MLME_RESULT_TIMEOUT:
            DE_TRACE_STATIC(TR_SEVERE, "Join Confirm was bad, timeout\n");
            net->status = weNetworkStatus_Timedout;
            net->fail_count++;
            break;

         case M80211_MLME_RESULT_REFUSED:
            DE_TRACE_STATIC(TR_SEVERE, "Join Confirm was bad, connection refused\n");
            net->status = weNetworkStatus_Refused;
            net->fail_count++;
            break;
         default:
            DE_TRACE_INT(TR_SEVERE, "Join Confirm was bad, unknown err %d\n", cfm->result);
            net->status = weNetworkStatus_Refused;
            net->fail_count++;
            break;
      }
      wei_save_con_failed_reason(WE_CONNECT_JOIN_FAIL, cfm->result);

      return FALSE;
   }
   else
   {
      net->status     = weNetworkStatus_Joined;
   }

   return TRUE;
}


int Mlme_CreateAuthenticateRequest(hic_message_context_t* msg_ref, int dummy)
{
   m80211_mlme_authenticate_req_t*  req;
   rNetworkProperties*              networkProperties;
   WiFiEngine_net_t*                net;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_authenticate_req_t);
   if(req == NULL) return FALSE;

   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_AUTHENTICATE_REQ;

    /* Get a reference to the current network. */
#if (DE_CCX_ROAMING == CFG_INCLUDED)
   if (ccxnet) 
   {
      net = ccxnet;
      DE_TRACE_STATIC(TR_AUTH,"Mlme_CreateAuthenticateRequest -> net = ccxnet\n");
   }
   else 
   {
     DE_TRACE_STATIC(TR_AUTH, "Mlme_CreateAuthenticateRequest -> net = wei_netlist_get_current_net\n");
      net = wei_netlist_get_current_net();
   }
#else
   net = wei_netlist_get_current_net();
#endif

   /* Get network information from the registry. */
   networkProperties = (rNetworkProperties*)Registry_GetProperty(ID_network);
   
   wei_copy_bssid(&req->peer_sta, &net->bssId_AP);
   req->timeout  = networkProperties->basic.timeout;
   /* Type is Open system for all cases except WEP with explicit shared key */

   if(net->auth_mode == Authentication_Shared)
   {
      if (wifiEngineState.key_state.key_enc_type != Encryption_WEP)
      {
         DE_TRACE_STATIC(TR_AUTH, "Current key not a WEP key, Shared key authentication not possible.\n");
         return FALSE;
      }
      else
      {
         req->type = M80211_AUTH_SHARED_KEY;
      }
   }
   else
   {
      req->type = M80211_AUTH_OPEN_SYSTEM;
   }
   
   net->status = weNetworkStatus_Authenticating;

   WEI_SET_TRANSID(req->trans_id);
   DE_TRACE_STATIC(TR_ASSOC, "send auth_req\n");
   return TRUE;
}

bool_t Mlme_HandleAuthenticateConfirm(m80211_mlme_authenticate_cfm_t *cfm)
{
   WiFiEngine_bss_type_t bssType;
   WiFiEngine_net_t* net;   

   WiFiEngine_GetBSSType(&bssType);
   net = wei_netlist_get_current_net();

   DE_TRACE_STACK_USAGE;

   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_ASSERT(FALSE);
            break;

         case M80211_MLME_RESULT_TIMEOUT:
            DE_TRACE_STATIC(TR_SEVERE, "AUTHENTICATION CONFIRM had timeout status\n");
            net->status = weNetworkStatus_Timedout;
            net->fail_count++;
            break;

         default:
         case M80211_MLME_RESULT_REFUSED:
            DE_TRACE_STATIC(TR_SEVERE, "AUTHENTICATION CONFIRM had refused status\n");
            net->status = weNetworkStatus_Refused;
            net->fail_count++;
            break;
      }

      return FALSE;
   }

   if(net->bss_p->bssType == M80211_CAPABILITY_ESS)
   {
      net->status = weNetworkStatus_Associating;
   }

   DE_TRACE_STATIC(TR_ASSOC, "received auth_cfm\n");
   return TRUE;
}

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
int Mlme_CreateAuthenticateCfm(hic_message_context_t* msg_ref, int dummy)
{
   m80211_mlme_authenticate_cfm_t*  cfm;
   WiFiEngine_net_t*                net;

   /* Allocate a context buffer of the appropriate size. */
   cfm = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_authenticate_cfm_t);
   if(cfm == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_AUTHENTICATE_CFM;

   /* Get a reference to the current network. */
   net = wei_netlist_get_current_net();

   wei_copy_bssid(&cfm->peer_sta, &net->bssId_AP);
   cfm->type = M80211_CAPABILITY_IBSS;
   cfm->result = WIFI_ENGINE_SUCCESS;

   wifiEngineState.main_state = driverAuthenticating;
   
   net->status = weNetworkStatus_Authenticating;
   DE_TRACE_STATIC(TR_ASSOC, "send auth_req\n");
   return TRUE;
}
#endif /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */

static void copy_wpa_rsn_etc_ies(
      m80211_mlme_associate_req_t *req, 
      m80211_mlme_associate_req_t *cached_req,
      WiFiEngine_net_t *net,
      int *wmm_enable)
{
   /* do not include wpa/rsn/wapi ies when connecting to a WPS AP expecting cred. negotiation */
#if 0
   if(m80211_ie_is_wps_configured(&net->bss_p->bss_description_p->ie.wps_parameter_set))
   {
#ifdef WORKAROUND_FOR_BUFFALO_AP
      /* needed by some AP's but brakes wps test case 5.1.3 */
      copy_wps_ie(req, &(req->ie.wps_parameter_set));
#endif
      return;
   }
#endif

#if (DE_CCX == CFG_INCLUDED)
   DE_TRACE_STATIC(TR_SM_HIGH_RES, "\n\n\n\n\ninclude ccx IE in assoc request???\n");
   if (net->bss_p->bss_description_p->ie.ccx_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      if (net->bss_p->bss_description_p->ie.ccx_parameter_set.hdr.OUI_type == CCX_IE_OUI_TYPE)
      {
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "YES!\n");
   
   copy_ccx_ie(req, &(req->ie.ccx_parameter_set));
        copy_ccx_rm_ie(req, &(req->ie.ccx_rm_parameter_set));

   if(wifiEngineState.ccxState.cpl_enabled==1)
   {

      int qpsk_lvl=0;
      int ofdm_lvl=0;
      int isDSS;
      DE_TRACE_STATIC(TR_SM_HIGH_RES, "cpl function is enabled\n");

      WiFiEngine_GetTxPowerLevel(&qpsk_lvl, &ofdm_lvl);
         DE_TRACE_STATIC2(TR_SM_HIGH_RES, "cpl=%d\n", wifiEngineState.ccxState.cpl_value);
   
      isDSS = WiFiEngine_is_dss_net(net);

      if(isDSS)   
      {  
         if(DSSS_MAX_LVL - wifiEngineState.power_index.qpsk_index > wifiEngineState.ccxState.cpl_value)
         {
            if(wifiEngineState.ccxState.cpl_value > DSSS_MAX_LVL)
            {
               DE_TRACE_STATIC(TR_SM_HIGH_RES, "set tx power level to the maximum for dsss\n");
   //          WiFiEngine_SetTxPowerLevel_from_cpl_ie(0, ofdm_lvl);
               WiFiEngine_SetTxPowerLevel_from_cpl_ie(0, 0);
               wifiEngineState.ccxState.cpl_tx_value_dsss = OFDM_MAX_LVL;
            }
            DE_TRACE_STATIC(TR_SM_HIGH_RES, "change tx power level for dsss\n");
            WiFiEngine_SetTxPowerLevel_from_cpl_ie(-(DSSS_MAX_LVL - wifiEngineState.ccxState.cpl_value), 0);
            wifiEngineState.ccxState.cpl_tx_value_dsss = wifiEngineState.ccxState.cpl_value;
         }
         else
         {
            DE_TRACE_STATIC2(TR_SM_HIGH_RES, "do not change tx power level for dsss (%d)\n", wifiEngineState.ccxState.cpl_value);
         }
      }
      else
      {        
         if(OFDM_MAX_LVL - wifiEngineState.power_index.ofdm_index > wifiEngineState.ccxState.cpl_value)
         {
            if(wifiEngineState.ccxState.cpl_value > OFDM_MAX_LVL)
            {
               DE_TRACE_STATIC(TR_SM_HIGH_RES, "set tx power level to the maximum\n");
               WiFiEngine_SetTxPowerLevel_from_cpl_ie(0, 0);
               wifiEngineState.ccxState.cpl_tx_value_ofdm = DSSS_MAX_LVL;           
            }
            DE_TRACE_STATIC2(TR_SM_HIGH_RES, "change tx power level for ofdm (%d)\n", OFDM_MAX_LVL - wifiEngineState.ccxState.cpl_value);
            WiFiEngine_SetTxPowerLevel_from_cpl_ie(0, -(OFDM_MAX_LVL - wifiEngineState.ccxState.cpl_value));
            wifiEngineState.ccxState.cpl_tx_value_ofdm = wifiEngineState.ccxState.cpl_value;
         }
         else
         {
            DE_TRACE_STATIC2(TR_SM_HIGH_RES, "do not change tx power level for ofdm (%d)\n", wifiEngineState.ccxState.cpl_value);
         }
      }
   }
   else
   {
      DE_TRACE_STATIC(TR_SM_HIGH_RES, "cpl function is not enabled\n");
      WiFiEngine_SetTxPowerLevel_from_cpl_ie(0, 0);
   }


   DE_TRACE_STATIC2(TR_SM_HIGH_RES, "tsm=%d\n", req->ie.ccx_tsm_parameter_set.interval);
   if(WiFiEngine_is_roaming_in_progress())
   {
      char tspec_body[55];
      DE_TRACE_STATIC(TR_SM_HIGH_RES, "roaming in progress!\n");
      if(WiFiEngine_GetTSPECbody(tspec_body) == TRUE)
          copy_wmm_tspec_ie(req, &(req->ie.wmm_tspec_parameter_set), tspec_body);
   }  
#if 0
   copy_ccx_adjacent_ie(req, &(req->ie.ccx_adj_parameter_set), wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid, wifiEngineState.ccxState.LastAssociatedNetInfo.MacAddress.octet, wifiEngineState.ccxState.LastAssociatedNetInfo.ChannelNumber, DriverEnvironment_GetTimestamp_msec()-wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp);
#endif //0
      }
   }   
   else
   {
   WiFiEngine_store_cpl_state(0, 0);
   }
#endif //DE_CCX
   if (cached_req->ie.wpa_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      WrapperCopy_m80211_ie_wpa_parameter_set_t(req, &(req->ie.wpa_parameter_set), &(cached_req->ie.wpa_parameter_set));
   }
   if (cached_req->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      WrapperCopy_m80211_ie_rsn_parameter_set_t(req, &(req->ie.rsn_parameter_set), &(cached_req->ie.rsn_parameter_set));
      if (wei_get_number_of_ptksa_replay_counters_from_net(net) < 4)
      {
#ifdef WORKAROUND_FOR_TMO_AP
         /* According to standard the reply counter should be >= 4
          * to be able to run WMM. For some reason TMP AP responds
          * with a value less than four (0 => one counter).
          */
         DE_TRACE_STATIC(TR_ASSOC, "ignoring ptksa replay < 4\n");
#else
         *wmm_enable = 0;
         DE_TRACE_STATIC(TR_ASSOC, "wmm_enable = 0 (ptksa replay < 4)\n");
#endif
      }
   }
   if (cached_req->ie.wapi_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
     WrapperCopy_m80211_ie_wapi_parameter_set_t(req, &(req->ie.wapi_parameter_set), &(cached_req->ie.wapi_parameter_set));
   }
   
#if (DE_CCX == CFG_INCLUDED)
   // Check if this is a reassociation request.
   if (wifiEngineState.ccxState.request_number > 1)
   {
      //  Make sure that new AP timestamp is less than 1 sec old.
      uint32_t msec_diff = DriverEnvironment_GetTimestamp_msec() - net->bss_p->bss_description_p->driverenv_timestamp;
        DE_TRACE_INT(TR_ASSOC,"\n\nTimestmap difference in msec : %d\n\n", msec_diff);

      // Fix the timestamp if delay is close to the 1000 ms accepted limit.
      if (msec_diff > 900) {
         msec_diff -= 900;
         net->bss_p->bss_description_p->timestamp += msec_diff * 1000; 
      }
      copy_ccx_reassoc_req_ie(req, &(req->ie.ccx_reassoc_req_parameter_set), net->bss_p->bss_description_p->timestamp);
   }
#endif
}


#if DE_ENABLE_HT_RATES == CFG_ON
static int
association_uses_tkip(m80211_mlme_associate_req_t *req)
{
   m80211_cipher_suite_selector_t *suite;
   unsigned int i;

   if(req->ie.wpa_parameter_set.hdr.hdr.id == M80211_IE_ID_VENDOR_SPECIFIC) {
      suite = M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(&req->ie.wpa_parameter_set);
      for(i = 0; i < req->ie.wpa_parameter_set.pairwise_cipher_suite_count; i++) {
         if(suite[i].type == M80211_CIPHER_SUITE_TKIP)
            return TRUE;
      }
   }
   if(req->ie.rsn_parameter_set.hdr.id == M80211_IE_ID_RSN) {
      suite = M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(&req->ie.rsn_parameter_set);
      for(i = 0; i < req->ie.rsn_parameter_set.pairwise_cipher_suite_count; i++) {
         if(suite[i].type == M80211_CIPHER_SUITE_TKIP)
            return TRUE;
      }
   }
   return FALSE;
}
#endif /* DE_ENABLE_HT_RATES == CFG_ON */

static int
create_assoc_req(m80211_mlme_associate_req_t *req, WiFiEngine_net_t *net)
{
   m80211_mlme_associate_req_t*  cached_req;
   int                           wmm_enable = 0;
   int                           wmm_ps_enable = 0;
   char                          *p, *pn;
   rNetworkProperties*           networkProperties;
   rBasicWiFiProperties*         basicWiFiProperties;
   rPowerManagementProperties*   powerManagementProperties;  

   powerManagementProperties = (rPowerManagementProperties*)Registry_GetProperty(ID_powerManagement);

   /* Get preferred network information from the registry. */
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   networkProperties = (rNetworkProperties*)Registry_GetProperty(ID_network);  

   DE_MEMSET(req, M80211_IE_ID_NOT_USED, sizeof *req);

   wei_copy_bssid(&req->peer_sta, &net->bssId_AP);
   req->timeout         = networkProperties->basic.timeout;
   req->listen_interval = powerManagementProperties->listenInterval;

   /* WMM Association */
   if (M80211_WMM_INFO_ELEM_IS_PRESENT(net->bss_p->bss_description_p) && (basicWiFiProperties->enableWMM == STA_WMM_Enabled))
   {
      wmm_enable = 1;
      wmm_ps_enable =
        !!M80211_WMM_INFO_ELEM_SUPPORT_PS(net->bss_p->bss_description_p);
      DE_TRACE_INT(TR_ASSOC, "wmm_enable = 1, wmm_ps_enable = %d\n", wmm_ps_enable);
   }
   else if(M80211_WMM_PARAM_ELEM_IS_PRESENT(net->bss_p->bss_description_p) && (basicWiFiProperties->enableWMM == STA_WMM_Enabled))
   {
      wmm_enable = 1;
      wmm_ps_enable =
        !!M80211_WMM_PARAM_ELEM_SUPPORT_PS(net->bss_p->bss_description_p);
      DE_TRACE_INT(TR_ASSOC, "wmm_enable = 1, wmm_ps_enable = %d\n", wmm_ps_enable);
   }
   else
   {
      wmm_ps_enable = 0;
      DE_TRACE_STATIC(TR_ASSOC, "wmm_enable = 0, wmm_ps_enable = 0\n");
   }

   cached_req = wei_asscache_get_req();
   DE_ASSERT(cached_req != NULL);

   /* capability_info */
   req->capability_info = cached_req->capability_info;
   p = (char *)&req->capability_info;
   pn = (char *)&net->bss_p->bss_description_p->capability_info;
   DE_TRACE5(TR_ASSOC, "MY_CAP %02X %02X NET_CAP %02X %02X\n", p[1], p[0], pn[1], pn[0]);

   /* WPA/RSN/WPS/WAPI */
   copy_wpa_rsn_etc_ies(req, cached_req, net, &wmm_enable);

#if (DE_ENABLE_HT_RATES == CFG_ON)
#define NET_SUPPORTS_HT(N) ((N)->bss_p->bss_description_p->ie.ht_capabilities.hdr.id == M80211_IE_ID_HT_CAPABILITIES)
   if(NET_SUPPORTS_HT(net) 
      && WE_SUPPORTED_HTRATES() != 0
      /*  802.11n mandates that we don't transmit any TKIP encrypted
       *  HT rates. We solve this by not enabling the HT rate
       *  capability if we can expect to use pairwise TKIP.
       */
      && !association_uses_tkip(req)) {
      m80211_ie_ht_capabilities_t *ht_cap = &req->ie.ht_capabilities;
      uint16_t caps;
      DE_MEMSET(ht_cap, 0, sizeof(*ht_cap));
      ht_cap->hdr.id = M80211_IE_ID_HT_CAPABILITIES;
#define M80211_IE_HT_CAPABILITIES_SIZE 26
      ht_cap->hdr.len = M80211_IE_HT_CAPABILITIES_SIZE;
      /* XXX the default bitmask should come from firmware */
      caps = M80211_HT_CAPABILITY_HT_GREENFIELD
         | M80211_HT_CAPABILITY_RX_STBC_ONE_STREAM
         | M80211_HT_CAPABILITY_SHORT_GI_20MHZ
         | M80211_HT_CAPABILITY_LSIG_TXOP_PROTECT_SUPPORT;
      caps &= basicWiFiProperties->ht_capabilities;
      ht_cap->ht_capabilities_info[0] = caps & 0xff;
      ht_cap->ht_capabilities_info[1] = (caps >> 8) & 0xff;
      ht_cap->supported_mcs_set.rx_mcs_bitmap[0] = 0xff;
   }
   if(!wmm_enable 
      && req->ie.ht_capabilities.hdr.id == M80211_IE_ID_HT_CAPABILITIES) {
      req->ie.qos_capability.hdr.id = M80211_IE_ID_QOS_CAPABILITY;
      req->ie.qos_capability.hdr.len = 1;
      req->ie.qos_capability.qos_info = 0; /* XXX get proper value
                     from AP+registry */
   }
#endif /* (DE_ENABLE_HT_RATES == CFG_ON) */

   if (wmm_enable)
   {
      wei_init_wmm_ie(&req->ie.wmm_information_element, wmm_ps_enable);
      WES_SET_FLAG(WES_FLAG_WMM_ASSOC);
      WES_SET_FLAG(WES_FLAG_QOS_ASSOC);
   }
   else if(req->ie.qos_capability.hdr.id == M80211_IE_ID_QOS_CAPABILITY)
   {
      WES_CLEAR_FLAG(WES_FLAG_WMM_ASSOC);
      WES_SET_FLAG(WES_FLAG_QOS_ASSOC);
   }
   else
   {
      WES_CLEAR_FLAG(WES_FLAG_WMM_ASSOC);
      WES_CLEAR_FLAG(WES_FLAG_QOS_ASSOC);
      req->ie.wmm_information_element.WMM_hdr.hdr.hdr.id       = M80211_IE_ID_NOT_USED;
   }
   return TRUE;
}

int Mlme_CreateAssociateRequest(hic_message_context_t* msg_ref, int dummy)
{
   WiFiEngine_net_t*             net;
   m80211_mlme_associate_req_t*  req;

   /* Get a reference to the current network. */
   net = wei_netlist_get_current_net();

   DE_TRACE_STATIC(TR_ASSOC, "send assoc_req\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_associate_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_ASSOCIATE_REQ;

#if (DE_CCX == CFG_INCLUDED)
   // Initialize reassociation request number to 1.
   wifiEngineState.ccxState.request_number = 1;
   wifiEngineState.ccxState.KRK_set = 0;
#endif   

   if(!create_assoc_req(req, net))
   {
      return FALSE;
   }
   
   /* Cache the association request (needed by WPA/RSN later) */
   wei_asscache_add_req(req);
   net->status = weNetworkStatus_Associating;
   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
}

bool_t Mlme_HandleAssociateConfirm(m80211_mlme_associate_cfm_t *cfm)
{
   WiFiEngine_net_t*             net;

   DE_TRACE_STACK_USAGE;

   DE_TRACE_INT(TR_ASSOC, "received assoc_cfm with result status %d\n", cfm->result);
   /* Cache the association response (needed by WPA/RSN later) */
   wei_asscache_add_cfm(cfm);

   /* Get a reference to the current network. */
   net = wei_netlist_get_current_net();

   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_TRACE_STATIC(TR_WARN, "ASSOCIATE_CFM status is INVALID_PARAM\n");
            net->status = weNetworkStatus_Unknown;
            net->fail_count++;
            break;

         case M80211_MLME_RESULT_TIMEOUT:
           DE_TRACE_STATIC(TR_WARN, "ASSOCIATE_CFM status is TIMEOUT\n");
            net->status = weNetworkStatus_Timedout;
            net->fail_count++;
            break;

         default:
         case M80211_MLME_RESULT_REFUSED:
            DE_TRACE_STATIC(TR_WARN, "ASSOCIATE_CFM status is REFUSED\n");
            net->status = weNetworkStatus_Refused;
            net->fail_count++;
            break;
      }
      
      WES_CLEAR_FLAG(WES_FLAG_WMM_ASSOC);
      WES_CLEAR_FLAG(WES_FLAG_QOS_ASSOC);
      
      return FALSE;
   }
   else
   {
#if (DE_CCX == CFG_INCLUDED)
      wifiEngineState.ccxState.SendAdjReq = 1;
      //HandleRadioMeasurementReq(11, 6000, 1);
#endif
      net->status = weNetworkStatus_Connected;
   }

   candidatesOfSsid = net->bss_p->bss_description_p->ie.ssid;

   if (net->bss_p->bss_description_p->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED ) {
      update_candidate_list(net->bssId_AP,
                            net->bss_p->rssi_info,
                            net->bss_p->bss_description_p->ie.rsn_parameter_set.rsn_capabilities);
   }

   return TRUE;
}

int Mlme_CreateReassociateRequest(hic_message_context_t* msg_ref, int dummy)
{
   WiFiEngine_net_t*             net;
   m80211_mlme_associate_req_t*  req;

   /* Get a reference to the current network. */
   net = wei_netlist_get_current_net();

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_associate_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_REASSOCIATE_REQ;

#if (DE_CCX == CFG_INCLUDED)
   wifiEngineState.ccxState.request_number++;
#endif  

   if(!create_assoc_req(req, net))
      return FALSE;

   /* Cache the association request (needed by WPA/RSN later) */
   wei_asscache_add_req(req);
   net->status = weNetworkStatus_Associating;
   WEI_SET_TRANSID(req->trans_id);

   DE_TRACE_STATIC(TR_ASSOC, "send re-assoc_req\n");
   return TRUE;
}

/* XXX merge this with Mlme_HandleAssociateConfirm */
bool_t Mlme_HandleReassociateConfirm(m80211_mlme_associate_cfm_t *cfm)
{
   WiFiEngine_net_t*             net;

   DE_TRACE_STACK_USAGE;

   DE_TRACE_INT(TR_ASSOC, "received re-assoc_cfm with result status %d\n", cfm->result);
   /* Cache the association response (needed by WPA/RSN later) */
   wei_asscache_add_cfm(cfm);

   /* Get a reference to the associated network. */
   net = wei_netlist_get_current_net();

   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_TRACE_STATIC(TR_WARN, "REASSOCIATE_CFM status is INVALID_PARAM\n");
            DE_ASSERT(FALSE);
            net->status = weNetworkStatus_Unknown;
            break;

         case M80211_MLME_RESULT_TIMEOUT:
           DE_TRACE_STATIC(TR_WARN, "REASSOCIATE_CFM status is TIMEOUT\n");
            net->status = weNetworkStatus_Timedout;
            break;

         default:
         case M80211_MLME_RESULT_REFUSED:
            DE_TRACE_INT(TR_SEVERE, "REASSOCIATE_CFM status is: %d\n", cfm->result);
            net->status = weNetworkStatus_Refused;
            break;
      }
     
      WES_CLEAR_FLAG(WES_FLAG_WMM_ASSOC);
      
      return FALSE;
   }
   else
   {
      /* Reassociation complete */
      DE_TRACE_STATIC(TR_ASSOC, "Reassociation complete\n");
   }
   
   net->status = weNetworkStatus_Connected;
   candidatesOfSsid = net->bss_p->bss_description_p->ie.ssid;

   if (net->bss_p->bss_description_p->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED ) {
      update_candidate_list(net->bssId_AP, 
                            net->bss_p->rssi_info,
                            net->bss_p->bss_description_p->ie.rsn_parameter_set.rsn_capabilities);
   }
   return TRUE;
}

int Mlme_CreateDeauthenticateRequest(hic_message_context_t* msg_ref, int dummy)
{
   m80211_mlme_deauthenticate_req_t*   req;
   WiFiEngine_net_t*                     net;
   
   DE_TRACE_STACK_USAGE;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_deauthenticate_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id   = MLME_DEAUTHENTICATE_REQ;
   
   /* Get a reference to the associated network. */
   net = wei_netlist_get_current_net();

   if (net != NULL)
   {
      wei_copy_bssid(&req->peer_sta, &net->bssId_AP);
      req->value = M80211_MGMT_REASON_PREV_AUTHENTICATION_NO_LONGER_VALID;
      WEI_SET_TRANSID(req->trans_id);

      DE_TRACE_STATIC(TR_ASSOC, "send deauth_req\n");
      return TRUE;
   }
   return FALSE;
}

bool_t Mlme_HandleDeauthenticateConfirm(m80211_mlme_deauthenticate_cfm_t *cfm)
{
   WiFiEngine_net_t* net;
   
   DE_TRACE_STATIC(TR_ASSOC, "received deauth_cfm\n");
   DE_TRACE_STACK_USAGE;

   /* Get a reference to the associated network. */
   net = wei_netlist_get_current_net();
   net->status = weNetworkStatus_NetAware;

   if (cfm->value != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->value)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_TRACE_STATIC(TR_WARN, "Deauthenticate Confirm returned INVALID_PARAM\n");
            break;
            
         case M80211_MLME_RESULT_TOO_MANY_REQ:
            DE_TRACE_STATIC(TR_WARN, "Deauthenticate Confirm returned TO_MANY_REQ\n");
            break;
         default:
            DE_TRACE_INT(TR_SEVERE, "Deauthenticate Confirm returned %d\n",cfm->value);
            break;
      }
      return FALSE;
   }
  
   return TRUE;
}

bool_t Mlme_HandleStartConfirm(m80211_mlme_start_cfm_t *cfm)
{
   WiFiEngine_net_t*          net;

   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         case M80211_MLME_RESULT_INVALID_PARAM:
            DE_ASSERT(FALSE);
            break;

         case M80211_MLME_RESULT_TIMEOUT:
/*             net->status = weNetworkStatus_Timedout; */
            break;

         default:
         case M80211_MLME_RESULT_REFUSED:
/*             net->status = weNetworkStatus_Refused; */
            break;
      }
#if 0      
      DE_TRACE_STATIC(TR_ASSOC, "Removing current_net\n");
      net = wei_netlist_get_current_net();
      if (net)
      {
         wei_netlist_remove_current_net();
         
         DE_TRACE_STATIC(TR_ASSOC, "Removing remove from active list\n");
         wei_netlist_remove_from_active(net);
         wei_netlist_free_net_safe(net);
      }
#endif
      return FALSE;
   }
   /*    net->status = weNetworkStatus_StartBeacon; */
   DE_TRACE_STATIC(TR_ASSOC, "received start_cfm\n");
   net = wei_netlist_get_current_net();
   net->status = weNetworkStatus_W4_NewSta;

   return TRUE;
}



int Mlme_CreateDisassociateRequest(hic_message_context_t* msg_ref, int dummy)
{
   m80211_mlme_disassociate_req_t*  req;
   WiFiEngine_net_t*                net;

   DE_TRACE_STACK_USAGE;
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_disassociate_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_DISASSOCIATE_REQ;

   /* Get network information from the registry. */
   net = wei_netlist_get_current_net();

   wei_copy_bssid(&req->peer_sta, &net->bssId_AP);
   req->reason = M80211_MGMT_RC_LEAVING_BSS;

   DE_TRACE_STATIC(TR_ASSOC, "send disassoc_req\n");
   return TRUE;
}

bool_t Mlme_HandleDisassociateConfirm(m80211_mlme_disassociate_cfm_t *cfm)
{
   WiFiEngine_net_t*                  net;

   DE_TRACE_STATIC(TR_ASSOC, "received disassoc_cfm\n");
   DE_TRACE_STACK_USAGE;

   net = wei_netlist_get_current_net();
   if (cfm->result != M80211_MLME_RESULT_SUCCESS)
   {
      switch (cfm->result)
      {
         default:
         case M80211_MLME_RESULT_NO_STA:
            DE_TRACE_STATIC(TR_WARN, "Disassociate Confirm returned NO_STA, I now know no networks\n");
            net->status = weNetworkStatus_Unknown;

            break;
      }
      return FALSE;
   }

   return TRUE;
}


bool_t Mlme_HandleBssPeerStatusInd(m80211_nrp_mlme_peer_status_ind_t *sta_ind_p)
{

   WiFiEngine_net_t* net;

   /* Check if associated net */
   net = wei_netlist_get_current_net();

   if(!wei_equal_bssid(sta_ind_p->bssid, net->bss_p->bssId))
   {
      DE_TRACE_STATIC(TR_WARN, "Got PEER_STATUS_IND but bssid does not match\n");      
      return FALSE;
   }

#ifdef WITH_TIMESTAMPS
   if(sta_ind_p->status == MLME_PEER_STATUS_TX_FAILED)
      DE_TIMESTAMP("PEER_STATUS_TX_FAILED\n");
   else if(sta_ind_p->status == MLME_PEER_STATUS_RX_BEACON_FAILED)
      DE_TIMESTAMP("PEER_STATUS_RX_BEACON_FAILED\n");
   else
      DE_TIMESTAMP("Unknown PEER_STATUS_IND\n");
#endif
   
#ifdef WIFI_DEBUG_ON
   switch (sta_ind_p->status) {
      case MLME_PEER_STATUS_CONNECTED:                DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_CONNECTED\n"); break;      
      case MLME_PEER_STATUS_TX_FAILED:                DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_TX_FAILED\n"); break;
      case MLME_PEER_STATUS_RX_BEACON_FAILED:         DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_RX_BEACON_FAILED\n"); break;
      case MLME_PEER_STATUS_ROUNDTRIP_FAILED:         DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_ROUNDTRIP_FAILED\n"); break;
      case MLME_PEER_STATUS_TX_FAILED_WARNING:        DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_TX_FAILED_WARNING\n"); break;
      case MLME_PEER_STATUS_RX_BEACON_FAILED_WARNING: DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_RX_BEACON_FAILED_WARNING\n"); break;
      case MLME_PEER_STATUS_RESTARTED:                DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_RESTARTED\n"); break; 
      case MLME_PEER_STATUS_INCOMPATIBLE:             DE_TRACE_STATIC(TR_WARN, "PEER_STATUS_INCOMPATIBLE\n"); break; 
      default:
         DE_TRACE_INT(TR_SEVERE, "Unknown PEER_STATUS_IND (%d)\n", sta_ind_p->status);
   }
#endif
   printk("NANO:PEER_STATUS_IND (%d)\n", sta_ind_p->status); //20100728 thva: changed for Boxchip disconnect issue
   return TRUE;
}


bool_t Mlme_HandleDisassociateInd(m80211_mlme_disassociate_ind_t *ind_p)
{
   WiFiEngine_net_t*                  net;
   int r;

   DE_TRACE_STATIC(TR_ASSOC, "received disassoc_ind\n");
   
   net = wei_netlist_get_current_net();


   r = ind_p->reason;
   switch (r)
   {
     case M80211_MGMT_RC_UNSPECIFIED:
     case M80211_MGMT_RC_NOT_VALID:
     case M80211_MGMT_RC_LEAVING_IBSS:
     case M80211_MGMT_RC_INACTIVITY:
     case M80211_MGMT_RC_AP_TO_MANY_STA:
     case M80211_MGMT_RC_CLASS2_FRAME:
     case M80211_MGMT_RC_CLASS3_FRAME:
     case M80211_MGMT_RC_LEAVING_BSS:
     case M80211_MGMT_RC_ASS_NOT_AUTH:

      default:
         DE_TRACE_INT(TR_SEVERE, "Disassociate Indication has reason %d\n", r);
         net->status = weNetworkStatus_Unknown;
         break;
   }

   return TRUE;
}

bool_t Mlme_HandleAuthenticateInd(m80211_mlme_authenticate_ind_t *ind_p)
{
   WiFiEngine_net_t*                     net;

   DE_TRACE_STATIC(TR_ASSOC, "received auth_ind\n");

   net = wei_netlist_get_current_net();

   net = wei_netlist_get_sta_net_by_peer_mac_address(ind_p->peer_sta);
   if(net != NULL )
   {
      net->status = weNetworkStatus_Connected;
   }

   return TRUE;
}

bool_t Mlme_HandleDeauthenticateInd(m80211_mlme_deauthenticate_ind_t *ind_p)
{
   WiFiEngine_net_t*                   net;
   int r;

   DE_TRACE_STATIC(TR_ASSOC, "received deauth_ind\n");

//      wifiEngineState.main_state = driverIdle;

   /* Get network information from the registry. */
   net = wei_netlist_get_current_net();
   r = ind_p->value;
   switch (r)
   {
     case M80211_MGMT_RC_UNSPECIFIED:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_UNSPECIFIED\n");
         break;
     case M80211_MGMT_RC_NOT_VALID:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_NOT_VALID\n");
         break;
     case M80211_MGMT_RC_LEAVING_IBSS:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_LEAVING_IBSS\n");
         break;
     case M80211_MGMT_RC_INACTIVITY:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_INACTIVITY\n");
         break;
     case M80211_MGMT_RC_AP_TO_MANY_STA:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_AP_TO_MANY_STA\n");
         break;
     case M80211_MGMT_RC_CLASS2_FRAME:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_CLASS2_FRAME\n");
         break;
     case M80211_MGMT_RC_CLASS3_FRAME:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_CLASS3_FRAME\n");
         break;
     case M80211_MGMT_RC_LEAVING_BSS:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_LEAVING_BSS\n");
         break;
     case M80211_MGMT_RC_ASS_NOT_AUTH:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_ASS_NOT_AUTH\n");
         break;
     case M80211_MGMT_RC_AUTHETICATION_FAILED:
         DE_TRACE_STATIC(TR_ASSOC, "Deauthenticate Indication has reason M80211_MGMT_RC_AUTHETICATION_FAILED\n");
         break;
     default:
         DE_TRACE_INT(TR_ASSOC, "Deauthenticate Indication has reason %d\n", r);
         net->status = weNetworkStatus_Unknown;
         break;
   }

   return TRUE;
}


bool_t Mlme_CreateMIBGetRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, 
                                uint32_t *trans_id) 
{
   mlme_mib_get_req_t *req;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif
      
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_get_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_GET_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   DE_MEMCPY(req->identifier, mib_id.octets, sizeof(req->identifier));
   DE_TRACE_STRING(TR_MIB, "Created MIB get request %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));
   return TRUE;
}

#if DE_MIB_TABLE_SUPPORT == CFG_ON
bool_t Mlme_CreateMIBGetRawRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, 
                                uint32_t *trans_id) 
{
   mlme_mib_get_raw_req_t *req;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif
      
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_get_raw_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_GET_RAW_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   DE_MEMCPY(req->identifier, mib_id.octets, sizeof(req->identifier));
   
   if(wei_get_mib_object(&mib_id, &req->object) != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_MIB, "Failed to created MIB get raw request: no object found\n");
      return FALSE;
   }

   DE_TRACE_STRING(TR_MIB, "Created MIB get raw request %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));
   return TRUE;
}
#endif /* DE_MIB_TABLE_SUPPORT */


bool_t Mlme_CreateMIBSetRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, const void *inbuf, uint16_t inbuflen, uint32_t *trans_id) 
{
   mlme_mib_set_req_t *req;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_set_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_SET_REQ;
   if (inbuflen)
   {
      req->value.ref = (char*)DriverEnvironment_Nonpaged_Malloc(inbuflen);    
      
      if (NULL == req->value.ref)
      {
         DE_TRACE_STATIC(TR_SEVERE, "req->value.ref missing\n");
         return FALSE;
      }
      DE_MEMCPY(req->value.ref, inbuf, inbuflen);
   }
   else
   {
      req->value.ref = NULL;
   }

   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   DE_MEMCPY(req->identifier, mib_id.octets, sizeof(req->identifier));
   req->value.size = inbuflen;

   DE_TRACE_STRING(TR_MIB, "Created MIB set request %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));
   return TRUE;
}

#if DE_MIB_TABLE_SUPPORT == CFG_ON
bool_t Mlme_CreateMIBSetRawRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, const void *inbuf, uint16_t inbuflen, uint32_t *trans_id) 
{
   mlme_mib_set_raw_req_t *req;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_set_raw_req_t);
   if(req == NULL) return FALSE;

   DE_MEMCPY(req->identifier, mib_id.octets, sizeof(req->identifier));
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_SET_RAW_REQ;
   if (inbuflen)
   {
      req->value.ref = (char*)DriverEnvironment_Nonpaged_Malloc(inbuflen);    
      
      if (NULL == req->value.ref)
      {
         DE_TRACE_STATIC(TR_SEVERE, "req->value.ref missing\n");
         return FALSE;
      }
      DE_MEMCPY(req->value.ref, inbuf, inbuflen);
   }
   else
   {
      req->value.ref = NULL;
   }

   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }

   if(wei_get_mib_object(&mib_id,&req->object) != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_MIB, "Failed to created MIB set raw request: no object found\n");      
      return FALSE;
   }
   
   req->value.size = inbuflen;

   DE_TRACE_STRING(TR_MIB, "Created MIB set request %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));
   return TRUE;
}
#endif /* DE_MIB_TABLE_SUPPORT */

bool_t Mlme_CreateSetKeyRequest(hic_message_context_t* msg_ref, 
                                int key_idx, 
                                size_t key_len, 
                                const void *key, 
                                m80211_cipher_suite_t suite, 
                                m80211_key_type_t key_type, 
                                bool_t config_by_authenticator, 
                                m80211_mac_addr_t *bssid, 
                                receive_seq_cnt_t *rsc)
{
   m80211_mlme_set_key_req_t*    req;
   m80211_set_key_descriptor_t*  desc;

   /* Validate parameters. */
   DE_ASSERT(key_len <= sizeof(desc->key.part));
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_set_key_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_SET_KEY_REQ;

   DE_MEMSET(req, 0, sizeof(*req));
   desc = &req->set_key_descriptor;
   
   /* Set up cipher_key */
   DE_MEMCPY(desc->key.part, key, key_len);
   desc->key_len              = key_len;
   desc->key_id               = (uint8_t)key_idx;
   desc->key_type             = key_type;

   desc->mac_addr = *bssid;
   if (rsc != NULL)
   {
      desc->receive_seq_cnt = *rsc;
   }
   desc->config_by_authenticator = config_by_authenticator;
   desc->cipher_suite         = suite;

   WEI_SET_TRANSID(req->trans_id);
   DE_TRACE_STATIC(TR_ASSOC, "Created SetKey request\n");
   return TRUE;
}

bool_t Mlme_CreateDeleteKeyRequest(hic_message_context_t* msg_ref, int key_idx, 
                                   m80211_key_type_t key_type, m80211_mac_addr_t *bssid)
{
   m80211_mlme_delete_key_req_t*    req;
   m80211_delete_key_descriptor_t*  desc;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_delete_key_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_DELETE_KEY_REQ;

   DE_MEMSET(req, 0, sizeof *req);
   desc = &req->delete_key_descriptor;
   
   desc->key_id = (uint8_t)key_idx;
   desc->key_type = key_type;

   desc->mac_addr = *bssid;
   
   WEI_SET_TRANSID(req->trans_id);
   DE_TRACE_STATIC(TR_ASSOC, "Created DeleteKey request\n");
   return TRUE;
}

bool_t Mlme_CreateSetProtectionReq(hic_message_context_t* msg_ref, m80211_mac_addr_t *bssid,
                                   m80211_protect_type_t *prot, m80211_key_type_t *key_type)
{
   m80211_mlme_set_protection_req_t* req;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_set_protection_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_SET_PROTECTION_REQ;
   
   DE_MEMSET(req, 0, sizeof *req);
   wei_copy_bssid(&req->protect_list_element.mac_addr, bssid);
   req->protect_list_element.protect_type = *prot;
   req->protect_list_element.key_type = *key_type;
   WEI_SET_TRANSID(req->trans_id);

   DE_TRACE_STATIC(TR_ASSOC, "Created SetProtection request\n");
   return TRUE;
}

bool_t Mlme_HandleHICInterfaceSleepForeverConfirm(hic_ctrl_sleep_forever_cfm_t *cfm_p)
{
   switch(cfm_p->result)
   {
      case HIC_CTRL_INTFACE_CFM_SUCCESS:
         DE_TRACE_STATIC(TR_ALWAYS, "HIC_CTRL_SLEEP_FOREVER_CFM has success status\n");         
         return TRUE;
      case HIC_CTRL_INTFACE_CFM_FAILED:
         DE_TRACE_STATIC(TR_SEVERE, "HIC_CTRL_SLEEP_FOREVER_CFM had failed status\n");
         return FALSE;
   }

   return FALSE;
}

bool_t Mlme_HandleHICInitCompleteConfirm(hic_ctrl_init_completed_cfm_t *cfm_p)
{
   switch(cfm_p->result)
   {
      case HIC_CTRL_INTFACE_CFM_SUCCESS:
         DE_TRACE_STATIC(TR_ALWAYS, "HIC_CTRL_INIT_COMPLETED_CFM has success status\n");         
         return TRUE;
      case HIC_CTRL_INTFACE_CFM_FAILED:
         DE_TRACE_STATIC(TR_SEVERE, "HIC_CTRL_INIT_COMPLETED_CFM had failed status\n");
         return FALSE;
      default:
         DE_TRACE_INT(TR_SEVERE, "HIC_CTRL_INIT_COMPLETED_CFM had unknown status %d\n", cfm_p->result);
         DE_ASSERT(FALSE);
   }

   return FALSE;
}

bool_t Mlme_CreateSetAlignmentReq(hic_message_context_t* msg_ref,
                                  uint16_t  min_sz,
                                  uint16_t  padding_sz,
                                  uint8_t  rx_hic_hdr_size,
                                  uint32_t trans_id,
                                  uint8_t  hAttention,
                                  uint8_t  swap,
                                  uint8_t  hWakeup,
                                  uint8_t  hForceInterval,
                                  uint8_t  tx_window_size)
{
   hic_ctrl_set_alignment_req_t* req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_set_alignment_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id   = HIC_CTRL_SET_ALIGNMENT_REQ;
   
   DE_TRACE_STATIC(TR_INITIALIZE, "send Set alignment Request\n");
   
   /* Store HIC message MIN size and alignment. */
   /* Assert that min size is aligned.          */
   DE_ASSERT((min_sz & (padding_sz - 1)) == 0);
   
   req->min_sz          = min_sz;
   req->padding_sz      = padding_sz;
   req->trans_id        = trans_id;
   req->hAttention      = hAttention;
   req->ul_header_size  = rx_hic_hdr_size;
   req->swap            = swap;
   req->hWakeup         = hWakeup;
   req->hForceInterval  = hForceInterval;
   req->tx_window_size  = tx_window_size + 1; //20100728 thva: changed for Boxchip BUG2466
   req->block_mode_bug_workaround_block_size = 0;
   
   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
}

bool_t Mlme_HandleSetAlignmentConfirm(hic_message_context_t* msg_ref, uint32_t *trans_id)
{
   hic_ctrl_set_alignment_cfm_t *cfm;
   
   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, hic_ctrl_set_alignment_cfm_t);
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_SET_ALIGNMENT_CFM;
   *trans_id = cfm->trans_id;

   switch(cfm->result)
   {
      case HIC_CTRL_SET_ALIGNMENT_CFM_SUCCESS:
         DE_TRACE_STATIC(TR_INITIALIZE, "SET_ALIGNMENT_CFM success\n");
         return TRUE;
      case HIC_CTRL_SET_ALIGNMENT_CFM_FAILED:
         DE_TRACE_STATIC(TR_SEVERE, "SET_ALIGNMENT_CFM had failed status\n");
         return FALSE;
   }

   return FALSE;
}



bool_t Mlme_HandleHICWakeupInd(hic_ctrl_wakeup_ind_t  *ind_p)
{
   DE_TRACE_STATIC(TR_PS, "HICWakeupInd() called\n");
   switch(ind_p->reasons)
   {
      case HIC_CTRL_WAKEUP_IND_HOST:
         DE_TRACE_STATIC(TR_PS_DEBUG, "WAKEUP_IND has reason HIC_CTRL_WAKEUP_IND_HOST\n");
         break;
      case HIC_CTRL_WAKEUP_IND_MULTICAST_DATA:
         DE_TRACE_STATIC(TR_PS_DEBUG, "WAKEUP_IND has reason HIC_CTRL_WAKEUP_IND_MULTICAST_DATA\n");
         break;
      case HIC_CTRL_WAKEUP_IND_UNICAST_DATA:
         DE_TRACE_STATIC(TR_PS_DEBUG, "WAKEUP_IND has reason HIC_CTRL_WAKEUP_IND_UNICAST_DATA\n");
         break;
      case HIC_CTRL_WAKEUP_IND_ALL_DATA:
         DE_TRACE_STATIC(TR_PS_DEBUG, "WAKEUP_IND has reason HIC_CTRL_WAKEUP_IND_ALL_DATA\n");
         break;
      case HIC_CTRL_WAKEUP_IND_WMM_TIMER:
         DE_TRACE_STATIC(TR_PS_DEBUG, "WAKEUP_IND has reason HIC_CTRL_SIG_WAKEUP_IND_WMM_TIMER\n");
         break; 
      default:
         DE_TRACE_STATIC(TR_PS, "WAKEUP_IND has unknown reason \n");
         break;          
         
   }
   return TRUE;
}


bool_t Mlme_CreateSleepForeverReq(hic_message_context_t* msg_ref, int dummy)
{
   hic_ctrl_sleep_forever_req_t *req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_sleep_forever_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_SLEEP_FOREVER_REQ;
   req->control = 0;
   WEI_SET_TRANSID(req->trans_id);
   
   return TRUE;
}

bool_t  Mlme_CreateInitCompleteReq(hic_message_context_t* msg_ref, int dummy)
{
   hic_ctrl_init_completed_req_t *req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_init_completed_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_INIT_COMPLETED_REQ;
   req->dummy = 0;
   WEI_SET_TRANSID(req->trans_id);
   
   return TRUE;
}

bool_t Mlme_CreateCommitSuicideReq(hic_message_context_t* msg_ref)
{
   hic_ctrl_commit_suicide_req_t *req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_commit_suicide_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_COMMIT_SUICIDE;
   req->dummy = 0;
   WEI_SET_TRANSID(req->trans_id);
   
   return TRUE;
}

bool_t Mlme_CreateScbErrorReq(hic_message_context_t* msg_ref, char *dst_str)
{
   hic_ctrl_scb_error_req_t *req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_scb_error_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_SCB_ERROR_REQ;

   req->keyString.ref = dst_str;
   req->keyString.size = sizeof(SCB_ERROR_KEY_STRING);
   DE_MEMCPY(req->keyString.ref, SCB_ERROR_KEY_STRING, sizeof(SCB_ERROR_KEY_STRING));
   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
}

bool_t
Mlme_CreateHeartbeatReq(hic_message_context_t* msg_ref, 
                        uint8_t control, 
                        uint32_t interval)
{
   hic_ctrl_heartbeat_req_t *req;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_heartbeat_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_HEARTBEAT_REQ;

   req->control = control;
   req->interval = interval;

   WEI_SET_TRANSID(req->trans_id);

   return TRUE;
}

bool_t
Mlme_CreateWakeUpInd(hic_message_context_t* msg_ref)
{
   hic_ctrl_wakeup_ind_t *ind;
   /* Allocate a context buffer of the appropriate size. */
   ind = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_wakeup_ind_t);
   if(ind == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_WAKEUP_IND;
   ind->reasons = HIC_CTRL_WAKEUP_IND_MULTICAST_DATA;
   return TRUE;
}

bool_t Mlme_CreateHICInterfaceDown(hic_message_context_t *msg_ref, int dummy)
{
   hic_ctrl_interface_down_t *rsp_p;
   
   /* Allocate a context buffer of the appropriate size. */
   rsp_p = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_interface_down_t);
   if(rsp_p == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id = HIC_CTRL_INTERFACE_DOWN;

   DE_TRACE_STATIC(TR_PS, "send HIC_CTRL_INTERFACE_DOWN \n");
   return TRUE;
}



bool_t Mlme_CreateHICWMMPeriodStartReq(hic_message_context_t* msg_ref, int dummy)
{
   m80211_nrp_mlme_wmm_ps_period_start_req_t *req;  
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_wmm_ps_period_start_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_WMM_PS_PERIOD_START_REQ;

   DE_TRACE_STATIC(TR_PS, "send NRP_MLME_WMM_PS_PERIOD_START_REQ\n");
   if(wei_is_wmm_ps_enabled())
   {   
      req->activity = TRUE;
   }
   else
   {
      req->activity = FALSE;
   }
   WEI_SET_TRANSID(req->trans_id);

   return TRUE;
}

bool_t Mlme_HandleHICWMMPeriodStartCfm(m80211_nrp_mlme_wmm_ps_period_start_cfm_t *cfm,uint32_t *result)
{
   
   DE_TRACE_STATIC(TR_PS, "Mlme_HandleHICWMMPeriodStartCfm() called\n");
   switch (cfm->result)
   {
      case HIC_CTRL_CFM_SUCCESS:
         DE_TRACE_STATIC(TR_PS, "NRP_MLME_WMM_PS_PERIOD_START_CFM had success status\n");
         break;
      case HIC_CTRL_CFM_FAILED:
         DE_TRACE_STATIC(TR_PS, "NRP_MLME_WMM_PS_PERIOD_START_CFM had failed status\n");
         break;
      case HIC_CTRL_CFM_NOT_SUPPORTED:
         DE_TRACE_STATIC(TR_PS, "NRP_MLME_WMM_PS_PERIOD_START_CFM had HIC_CTRL_CFM_NOT_SUPPORTED status\n");
         break;
      default:
         DE_TRACE_STATIC(TR_SEVERE, "Unknown error status received in Mlme_HandleHICWMMPeriodStartCfm()\n");
         break;
   }
   *result = cfm->result;
   
   return TRUE;
}



bool_t Mlme_CreatePowerManagementRequest(hic_message_context_t* msg_ref, int mode)
{
   m80211_mlme_power_mgmt_req_t* req;
   rPowerManagementProperties*   powerManagement;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_power_mgmt_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_POWER_MGMT_REQ;

   /* Get power management information from the registry. */
   powerManagement = (rPowerManagementProperties*)Registry_GetProperty(ID_powerManagement);  

   DE_TRACE_STATIC(TR_PS, "send Power Management Request\n");

   if (mode == 0xFF)
   {
      req->power_mgmt_mode  = powerManagement->mode;
   }
   else
   {
      req->power_mgmt_mode  = mode;
   }

   req->use_ps_poll = (uint8_t)WiFiEngine_LegacyPsPollPowerSave();
  

   
   req->receive_all_dtim = powerManagement->receiveAll_DTIM;
   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
}

bool_t Mlme_HandlePowerManagementConfirm(m80211_mlme_power_mgmt_cfm_t *cfm)
{
   bool_t result = TRUE;
   
   DE_TRACE_STATIC(TR_PS, "HandlePowerManagementConfim() called\n");
   switch (cfm->result)
   {
   case M80211_MLME_RESULT_SUCCESS:
      DE_TRACE_STATIC(TR_PS, "POWER_MGMT_CFM had M80211_MLME_RESULT_SUCCESS status\n");
         break;
      case M80211_RESULT_PMCFM_NOT_SUPPORTED:
         result = FALSE;
         DE_TRACE_STATIC(TR_SEVERE, "POWER_MGMT_CFM had \"NOT_SUPPORTED\" status\n");
         break;
      case M80211_RESULT_PMCFM_INVALID_PARAMETERS:
         result = FALSE;
         DE_TRACE_STATIC(TR_SEVERE, "POWER_MGMT_CFM had \"RESULT_PMCFM_INVALID_PARAMETERS\" status\n");
         break;            
      default:
         DE_TRACE_STATIC(TR_SEVERE, "Unknown error status received in HandlePowerManagementConfim()\n");
         break;
   }
   return result;
}

bool_t Mlme_CreateLeaveIBSSRequest(hic_message_context_t* msg_ref, int dummy)
{
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
   m80211_nrp_mlme_ibss_leave_req_t *req;

   WiFiEngine_net_t*    net;

   net = wei_netlist_get_current_net();

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_ibss_leave_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   /* Custom type is not taking care of in target */
   /* msg_ref->msg_type = HIC_MESSAGE_TYPE_CUSTOM; */
   msg_ref->msg_id = NRP_MLME_IBSS_LEAVE_REQ;

   wei_copy_bssid(&req->bssId, &net->bssId_AP);
   DE_TRACE_STATIC(TR_ASSOC, "Created LeaveIBSS request\n");
   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
#else
   return FALSE;
#endif /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */
}

bool_t Mlme_HandleLeaveIBSSConfirm(m80211_nrp_mlme_ibss_leave_cfm_t *cfm)
{

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
   DE_TRACE_STATIC(TR_ASSOC, "received IbssLeave_cfm\n");
   DE_TRACE_STACK_USAGE;

   if (cfm->result == M80211_NRP_MLME_LEAVE_IBSS_CFM_SUCCESS)
   {
      return TRUE;
   }
   else
   {
      DE_TRACE_STATIC(TR_ASSOC, "LeaveIBSS Confirm returned status failed\n");
      return FALSE;
   }
#else
   return FALSE;
#endif /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */
}

bool_t Mlme_CreateLeaveBSSRequest(hic_message_context_t* msg_ref, int dummy)
{
   m80211_nrp_mlme_bss_leave_req_t *req;
   WiFiEngine_net_t*    net;

   net = wei_netlist_get_current_net();
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_bss_leave_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_BSS_LEAVE_REQ;

   wei_copy_bssid(&req->bssId, &net->bssId_AP);
   DE_TRACE_STATIC(TR_ASSOC, "Created LeaveBSS request\n");
   WEI_SET_TRANSID(req->trans_id);


   return TRUE;
}

bool_t Mlme_HandleLeaveBSSConfirm(m80211_nrp_mlme_bss_leave_cfm_t *cfm)
{
   DE_TRACE_STATIC(TR_ASSOC, "received bssLeave Confirm\n");
   DE_TRACE_STACK_USAGE;

   if (cfm->result == M80211_NRP_MLME_LEAVE_BSS_CFM_SUCCESS)
   {
      return TRUE;
   }
   else
   {
      DE_TRACE_STATIC(TR_ASSOC, "LeaveBSS Confirm returned status failed\n");
      return FALSE;
   }
}

int Mlme_CreateStartRequest(hic_message_context_t* msg_ref, int dummy)
{
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
   m80211_mlme_start_req_t*      req;
   rBasicWiFiProperties*         basicWiFiProperties;
   rIBSSBeaconProperties*        beacon;
   rConnectionPolicy*            policy;   
   WiFiEngine_net_t*             net;

   DE_TRACE_STATIC(TR_ASSOC, "WiFiEngine Create Start Request\n"); 

   /* Get preferred network information from the registry. */
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   policy = (rConnectionPolicy*)Registry_GetProperty(ID_connectionPolicy); 

   policy->defaultBssType = (rBSS_Type)M80211_CAPABILITY_IBSS;
   
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_mlme_start_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = MLME_START_REQ;

   /* Get network information from the registry. */
   beacon = (rIBSSBeaconProperties*)Registry_GetProperty(ID_ibssBeacon); 
   
   /* We are starting an IBSS net */
   net = wei_netlist_get_current_net();

   /* Initiate start request message */
   req->ie.ssid = net->bss_p->bss_description_p->ie.ssid;
   req->bssid = net->bssId_AP;
   req->bss_type = policy->defaultBssType ;
   req->beacon_period = beacon->beacon_period;
   req->dtim_period = (uint8_t) beacon->dtim_period;
   req->ie.ds_parameter_set = beacon->tx_channel;
   /* CF parameters not used */
   req->ie.cf_parameter_set.hdr.id = M80211_IE_ID_NOT_USED;
   req->ie.ibss_parameter_set = beacon->atim_set;
   req->probe_delay  = basicWiFiProperties->connectionPolicy.probeDelay;
   req->capability_info = net->bss_p->bss_description_p->capability_info;

   /* This is a hack. The operational-basic mixup is intentional. It will be fixed properly
    * in a later release. */
   wei_ratelist2ie(&req->ie.supported_rate_set, &req->ie.ext_supported_rate_set,
                   &beacon->supportedRateSet);

   /* CF parameters not used */
   req->ie.country_info_set.hdr.id = M80211_IE_ID_NOT_USED;
   req->ie.wpa_parameter_set.hdr.hdr.id = M80211_IE_ID_NOT_USED;

   /* WMM parameters not used */
   req->ie.wmm_parameter_element.WMM_hdr.hdr.hdr.id = M80211_IE_ID_NOT_USED;

   if (basicWiFiProperties->enableWMM)
   {
      wei_init_wmm_ie(&req->ie.wmm_information_element, 1);
   }
   else
   {
      req->ie.wmm_information_element.WMM_hdr.hdr.hdr.id = M80211_IE_ID_NOT_USED;
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

   if (wifiEngineState.config.encryptionLimit == Encryption_WEP)
   {
      DE_TRACE_STATIC(TR_ASSOC, "Using WEP for IBSS\n");
      req->capability_info |=  M80211_CAPABILITY_PRIVACY;
      wifiEngineState.config.encryptionMode = Encryption_WEP;
   }

   if (wifiEngineState.config.encryptionLimit == Encryption_TKIP)
   {
      /* WPA parameters used*/
      m80211_cipher_suite_t group_suite;
      m80211_cipher_suite_t pairwise_suite;
      m80211_akm_suite_t akm_suite;

      group_suite = M80211_CIPHER_SUITE_TKIP;
      pairwise_suite  = M80211_CIPHER_SUITE_TKIP;
      akm_suite = M80211_AKM_SUITE_PSK;

      wei_build_wpa_ie((void*)req, &req->ie.wpa_parameter_set, group_suite, pairwise_suite, akm_suite);
      req->capability_info |=  M80211_CAPABILITY_PRIVACY;
      wifiEngineState.config.encryptionMode = Encryption_TKIP;
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

      req->capability_info |=  M80211_CAPABILITY_PRIVACY;
      wei_build_wpa_ie((void*)req, &req->ie.wpa_parameter_set, group_suite, pairwise_suite, akm_suite);      

   }

   wifiEngineState.main_state = driverStarting;
   net->status = weNetworkStatus_Start;

   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
#else
   return FALSE;
#endif /* (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS) */
}

bool_t Mlme_HandleMichaelMICFailureInd(m80211_mlme_michael_mic_failure_ind_t *ind)
{
   m80211_michael_mic_failure_ind_descriptor_t *ind_p = &ind->michael_mic_failure_ind_descriptor;
#ifdef WIFI_DEBUG_ON
   char bssid[18];
#endif

   DE_TRACE_STRING(TR_WARN, "MIC failure detected on BSSID %s\n", 
                   wei_print_mac(&ind_p->mac_addr, bssid, sizeof(bssid)));

   WiFiEngine_HandleMICFailure(ind_p);

   if(wei_netlist_get_current_net())
   {
     DriverEnvironment_indicate(WE_IND_MIC_ERROR, ind_p, sizeof(*ind_p));
   }
   return TRUE;
}

bool_t Mlme_CreateSyncRequest(hic_message_context_t* msg_ref,
      uint32_t *trans_id) 
{
   hic_ctrl_hl_sync_req_t *req;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_ctrl_hl_sync_req_t);
   if(req == NULL) return FALSE;

   msg_ref->msg_type = HIC_MESSAGE_TYPE_CTRL;
   msg_ref->msg_id   = HIC_CTRL_HL_SYNC_REQ;

   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }

   return TRUE;
}

bool_t Mlme_CreateMIBSetTriggerRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, 
                                       uint32_t          gating_trig_id,
                                       uint32_t          supv_interval,
                                       int32_t           level,
                                       uint16_t          event,
                                       uint16_t          event_count,
                                       uint16_t          triggmode,
                                       uint32_t *trans_id) 
{
   mlme_mib_set_trigger_req_t *req;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif
      
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_set_trigger_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_MIB_SET_TRIGGER_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   req->trigger_id = req->trans_id;

#if DE_MIB_TABLE_SUPPORT == CFG_ON
   req->reference_type = MIB_REFERENCE_BY_OBJECT;
   if(wei_get_mib_object(&mib_id, &req->reference.object) != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_MIB, "Failed to created MIB get raw request: no object found\n");
      return FALSE;
   }
#else
   req->reference_type = MIB_REFERENCE_BY_IDENTIFIER;
   DE_MEMCPY(req->reference.identifier, mib_id.octets, sizeof(req->reference.identifier));
#endif /* DE_MIB_TABLE_SUPPORT */
   
   req->gating_trigger_id = gating_trig_id;
   req->supv_interval = supv_interval;
   req->ind_cb = 0;
   req->level = level;
   req->event = event;
   req->event_count = event_count;
   req->triggmode = triggmode;
   DE_TRACE_STRING(TR_MIB, "Created MIB Set Trigger request %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));

   return TRUE;
}

bool_t Mlme_ChangeGatingMIBSetTriggerRequest(hic_message_context_t* msg_ref, 
                                             uint32_t          gating_trig_id)
{
   mlme_mib_set_trigger_req_t *req;
   req = HIC_GET_RAW_FROM_CONTEXT(msg_ref, mlme_mib_set_trigger_req_t);
   req->gating_trigger_id = gating_trig_id;
   return TRUE;
}


bool_t Mlme_CreateMIBRemoveTriggerRequest(hic_message_context_t* msg_ref,
                                          uint32_t trig_id) 
{
   mlme_mib_remove_trigger_req_t *req;
      
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_remove_trigger_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_MIB_REMOVE_TRIGGER_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   req->trigger_id = trig_id;

   return TRUE;
}

bool_t Mlme_CreateMIBSetGatingtriggerRequest(hic_message_context_t* msg_ref,
                                             uint32_t trig_id,
                                             uint32_t gating_trig_id,
                                             uint32_t *trans_id)
{
   mlme_mib_set_gatingtrigger_req_t *req;
      
   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, mlme_mib_set_gatingtrigger_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref->msg_id = MLME_MIB_SET_GATINGTRIGGER_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   req->trigger_id = trig_id;
   req->gating_trigger_id = gating_trig_id;
   if (trans_id)
      *trans_id = req->trans_id;

   return TRUE;
}

/* New scan interface */
int Mlme_CreateSetScanParamRequest(hic_message_context_t* msg_ref, 
                                   m80211_nrp_mlme_scan_config_t *sp,
                                   uint32_t *trans_id)
{
   m80211_nrp_mlme_set_scanparam_req_t *req;
   DE_TRACE_STACK_USAGE;

   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_set_scanparam_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_SETSCANPARAM_REQ;
   req->config = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleSetScanParamCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_set_scanparam_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received SetScanParamCfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_set_scanparam_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

int Mlme_CreateAddScanFilterRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_add_scanfilter_req_t *sp,
                                    uint32_t *trans_id)
{
   m80211_nrp_mlme_add_scanfilter_req_t *req;
   DE_TRACE_STACK_USAGE;

   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_add_scanfilter_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_ADD_SCANFILTER_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleAddScanFilterCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_add_scanfilter_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received addscanfilter_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_add_scanfilter_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }

   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

int Mlme_CreateRemoveScanFilterRequest(hic_message_context_t* msg_ref, 
                                       m80211_nrp_mlme_remove_scanfilter_req_t *sp,
                                       uint32_t *trans_id)
{
   m80211_nrp_mlme_remove_scanfilter_req_t *req;
   DE_TRACE_STACK_USAGE;

   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_remove_scanfilter_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_REMOVE_SCANFILTER_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleRemoveScanFilterCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_remove_scanfilter_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received removescanfilter_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_remove_scanfilter_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

int Mlme_CreateAddScanJobRequest(hic_message_context_t* msg_ref, 
                                 m80211_nrp_mlme_add_scanjob_req_t *sp,
                                 uint32_t *trans_id)
{
   m80211_nrp_mlme_add_scanjob_req_t *req;
   DE_TRACE_STACK_USAGE;

   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_add_scanjob_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_ADD_SCANJOB_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleAddScanJobCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_add_scanjob_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received addscanjob_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_add_scanjob_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);
   return TRUE;
}

int Mlme_CreateRemoveScanJobRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_remove_scanjob_req_t *sp,
                                    uint32_t *trans_id)
{
   m80211_nrp_mlme_remove_scanjob_req_t *req;
   DE_TRACE_STACK_USAGE;

   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_remove_scanjob_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_REMOVE_SCANJOB_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleRemoveScanJobCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_remove_scanjob_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received removescanjob_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_remove_scanjob_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

int Mlme_CreateSetScanJobStateRequest(hic_message_context_t* msg_ref, 
                                      m80211_nrp_mlme_set_scanjobstate_req_t *sp,
                                      uint32_t *trans_id)
{
   m80211_nrp_mlme_set_scanjobstate_req_t *req;
   DE_TRACE_STACK_USAGE;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_set_scanjobstate_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_SET_SCANJOBSTATE_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleSetScanJobStateCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_set_scanjobstate_cfm_t *cfm ;
   
   DE_TRACE_STATIC(TR_SCAN, "received setscanjobstate_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_set_scanjobstate_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

int Mlme_CreateGetScanFilterRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_get_scanfilter_req_t *sp,
                                    uint32_t *trans_id)
{
   m80211_nrp_mlme_get_scanfilter_req_t *req;
   DE_TRACE_STACK_USAGE;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_get_scanfilter_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_SET_SCANJOBSTATE_REQ;
   *req = *sp;
   WEI_SET_TRANSID(req->trans_id);
   *trans_id = req->trans_id;

   return TRUE;
}

bool_t Mlme_HandleGetScanFilterCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_get_scanfilter_cfm_t *cfm ;
   WiFiEngine_scan_filter_t *p;
   
   DE_TRACE_STATIC(TR_SCAN, "received getscanfilter_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_get_scanfilter_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
      p = NULL;
   }
   else
   {
      p = (WiFiEngine_scan_filter_t *)DriverEnvironment_Nonpaged_Malloc(sizeof *p);
      p->filter_id = cfm->filter_id;
      p->bss_type = (WiFiEngine_bss_type_t)cfm->bss_type;
      p->rssi_thr = cfm->rssi_threshold;
      p->snr_thr = cfm->snr_threshold;
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, p, sizeof *p, cfm->result);

   return TRUE;
}

int Mlme_CreateScanCountryInfoRequest(hic_message_context_t* msg_ref, 
                                      m80211_ie_country_t *ci)
{
   m80211_nrp_mlme_set_scancountryinfo_req_t*  req;

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_set_scancountryinfo_req_t);
   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
   msg_ref->msg_id = NRP_MLME_SET_SCANCOUNTRYINFO_REQ;

   WrapperCopy_m80211_ie_country_t(req, &req->country_info_set, ci);

   WEI_SET_TRANSID(req->trans_id);
   return TRUE;
}

bool_t Mlme_HandleScanCountryInfoCfm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_set_scancountryinfo_cfm_t *cfm;
   
   DE_TRACE_STATIC(TR_SCAN, "received set_scancountryinfo_cfm\n");

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_set_scancountryinfo_cfm_t);
   if (cfm->result != 0)
   {
      DE_TRACE_INT(TR_SEVERE, "cfm was %d\n",cfm->result);
   }
   wei_schedule_data_cb_with_status(cfm->trans_id, NULL, 0, cfm->result);

   return TRUE;
}

#if (DE_CCX == CFG_INCLUDED)

int Mlme_CreateADDTSRequest(hic_message_context_t* msg_ref, m80211_nrp_mlme_addts_req_t* myreq, uint32_t *trans_id)
{
    m80211_nrp_mlme_addts_req_t* req;

    DE_TRACE_STACK_USAGE;

    // Allocate a context buffer of the appropriate size.
    req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_addts_req_t);
    if(req == NULL)
       return FALSE;

    msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
    msg_ref->msg_id = NRP_MLME_ADDTS_REQ;
    *req = *myreq;

    WEI_SET_TRANSID(req->trans_id);

    *trans_id = req->trans_id;
    return TRUE;
}

bool_t Mlme_HandleADDTSConfirm(hic_message_context_t* msg_ref)
{
   m80211_nrp_mlme_addts_cfm_t *cfm;

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_addts_cfm_t);

   DE_TRACE_INT(TR_ASSOC, "received addts_cfm with result status %d\n", cfm->value);

   return TRUE;
}

int Mlme_CreateDELTSRequest(hic_message_context_t* msg_ref, m80211_nrp_mlme_delts_req_t* myreq, uint32_t *trans_id){

    m80211_nrp_mlme_delts_req_t* req;

    DE_TRACE_STACK_USAGE;

    // Allocate a context buffer of the appropriate size.
    req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_delts_req_t);
    if(req == NULL)
       return FALSE;

    msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
    msg_ref->msg_id = NRP_MLME_DELTS_REQ;
    *req = *myreq;

    WEI_SET_TRANSID(req->trans_id);

    *trans_id = req->trans_id;

    return TRUE;
}

bool_t Mlme_HandleDeltsConfirm(hic_message_context_t* msg_ref)
{
    m80211_nrp_mlme_delts_cfm_t *cfm;

    /* Get the raw message buffer from the context. */
    cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_delts_cfm_t);

    DE_TRACE_INT(TR_ASSOC, "received delts_cfm with result status %d\n", cfm->value);

    if (cfm->value == M80211_MLME_RESULT_SUCCESS)
    {
        wifiEngineState.ccxState.metrics.collect_metrics_enabled = FALSE;

        if(wifiEngineState.ccxState.ccx_traffic_stream_metrics_id)
        {
            DriverEnvironment_CancelTimer(wifiEngineState.ccxState.ccx_traffic_stream_metrics_id);
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

int Mlme_CreateFWStatsRequest(hic_message_context_t* msg_ref, bool_t init, uint32_t *trans_id)
{
    m80211_nrp_mlme_fw_stats_req_t* req;

    DE_TRACE_STACK_USAGE;

    // Allocate a context buffer of the appropriate size.
    req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, m80211_nrp_mlme_fw_stats_req_t);
    if(req == NULL)
        return FALSE;

    req->init = (init==TRUE) ? 1 : 0;

    msg_ref->msg_type = HIC_MESSAGE_TYPE_MGMT;
    msg_ref->msg_id = NRP_MLME_GET_FW_STATS_REQ;

    WEI_SET_TRANSID(req->trans_id);

    *trans_id = req->trans_id;
    return TRUE;
}

void Mlme_HandleFWStatsCfm(hic_message_context_t *msg_ref)
{
    m80211_nrp_mlme_fw_stats_cfm_t * cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_nrp_mlme_fw_stats_cfm_t);

    if(cfm->init == 0)
    {
        /* DEBUG ONLY
        DE_TRACE_STATIC(TR_NOISE, "NIKS: Now calling WiFiEngine_SendTrafficStreamMetricsReport()\n");
        DE_TRACE_STATIC5(TR_SM_HIGH_RES, "NIKS: metrics: fw raw: %u / %u for transid=%d, init=%d\n", cfm->pkt_xmit_delay_total, cfm->pkt_xmitted_cnt, cfm->trans_id, cfm->init);
        */

        wifiEngineState.ccxState.metrics.uplink_packet_transmit_media_delay = (cfm->pkt_xmitted_cnt != 0) ? cfm->pkt_xmit_delay_total / cfm->pkt_xmitted_cnt : 0;
        WiFiEngine_SendTrafficStreamMetricsReport();
    }
}


#endif //DE_CCX
bool_t
Mlme_CreateConsoleRequest(hic_message_context_t *msg_ref, 
                          const char *command, 
                          uint32_t *trans_id) 
{
   hic_mac_console_req_t *req;
      
   /* HIC_ALLOCATE_RAW_CONTEXT */
   msg_ref->raw_size = sizeof(*req) + strlen(command);
   msg_ref->raw = WrapperAllocStructure(NULL, msg_ref->raw_size);
   req = (hic_mac_console_req_t *)msg_ref->raw;

   if(req == NULL) return FALSE;
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_CONSOLE;
   msg_ref->msg_id = HIC_MAC_CONSOLE_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   DE_STRCPY(((char*)req + offsetof(hic_mac_console_req_t, string)), command);
   return TRUE;
}
/** @} */ /* End of mlme_proxy group */




