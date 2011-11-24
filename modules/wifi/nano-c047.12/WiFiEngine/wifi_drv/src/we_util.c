/* $Id: we_util.c,v 1.41 2008-04-22 11:41:39 anob Exp $ */
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
This module implements the WiFiEngine utility interface

*****************************************************************************/
/** @defgroup we_util WiFiEngine utility interface
 *
 * @brief This module contains various utility functions that are useful or necessary
 * for a driver using WiFiEngine.
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
#include "hmg_traffic.h"
#include "mlme_proxy.h"
#include "macWrapper.h"
#include "hicWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"

#if (DE_CCX_ROAMING == CFG_INCLUDED)
extern WiFiEngine_net_t * ccxnet;
#endif

/*!
 * @brief Create a new net
 *
 * @return net
 */
WiFiEngine_net_t* WiFiEngine_CreateNet(void)
{
   WiFiEngine_net_t* net;

   net = wei_netlist_create_new_net();

   net->bss_p->bss_description_p = (m80211_bss_description_t*)WrapperAttachStructure(net->bss_p, sizeof(m80211_bss_description_t));

   return (net);
}


/*!
 * @brief Create a new net
 *
 * @return net
 */
WiFiEngine_net_t* WiFiEngine_GetCurrentNet(void)
{
   WiFiEngine_net_t* net;

   net = wei_netlist_get_current_net();

   return (net);
}

/*!
 * @brief Set current net
 *
 * @return nothing
 */
void WiFiEngine_SetCurrentNet(WiFiEngine_net_t* net)
{
   wei_netlist_make_current_net(net);
}

/*!
 * @brief Free current net
 *
 * @return nothing
 */
void WiFiEngine_FreeCurrentNet(void)
{
   wei_netlist_remove_current_net();
}

/*!
 * @brief Checks if data send is ok
 *
 * @return TRUE/FALSE
 */
int WiFiEngine_IsDataSendOk()
{

   return wifiEngineState.dataReqPending < wifiEngineState.txPktWindowMax
      && !WiFiEngine_isCoredumpEnabled();
}

/*!
 * @brief Checks if command send is ok
 *
 * @return TRUE/FALSE
 */
int WiFiEngine_IsCommandSendOk()
{
   return wifiEngineState.cmdReplyPending == 0;
}

/*!
 * @brief Informs WiFiEngine of a dropped data frame
 *
 * @param [in] hic_header pointer to start of hw data header, this
 *                        should be identical to the hdr parameter to
 *                        WiFiEngine_ProcessSendPacket
 *
 * This function informs WiFiEngine that a previously processed TX
 * data frame has been dropped. A call to this function is necessary
 * when dropping such frames since the data window will be closed
 * permanently otherwise. Frames that have not been processed and
 * accepted by WiFiEngine_ProcessSendPacket() must be dropped without
 * calling this funcion.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_DataFrameDropped(const void *hic_header)
{
   const unsigned char *data_hdr;
   uint16_t vlanid_prio;

   DE_ASSERT(wifiEngineState.dataReqPending > 0);

   data_hdr = (const unsigned char*)hic_header +
      HIC_MESSAGE_HDR_SIZE(hic_header);

   vlanid_prio = HIC_DATA_REQ_GET_VLANID_PRIO(data_hdr);

   WIFI_LOCK();
   wifiEngineState.dataReqPending--;


   wifiEngineState.dataReqByPrio[vlanid_prio & 7]--;
   WIFI_UNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Check is a scan is in progress or not
 *
 * @return 1 if a scan is in progress. 0 otherwise.
 */
int WiFiEngine_ScanInProgress(void)
{
   int r;

   WIFI_LOCK();
   r = WES_TEST_FLAG(WES_FLAG_SCAN_IN_PROGRESS);
   WIFI_UNLOCK();

   return r;
}

/*!
 * @brief Check precence of Nanoradio hardware
 *
 * @return Return WIFI_ENGINE_SUCCESS if the hardware is present,
 * WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_HardwareState(void)
{
   if (WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Get the network connection status (connected or disconnected).
 *
 * @param net_status 0 if status is disconnected, 1 if connected.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS.
 */
int   WiFiEngine_GetNetworkStatus(int *net_status)
{
   if(WiFiEngine_is_roaming_in_progress())
   {
      *net_status = 1;
   }
   else
   {
      if(wifiEngineState.main_state == driverConnected)
      {
         *net_status = 1;
      }
      else
      {
         *net_status = 0;
      }
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Returns true if WifiEngine is inbetween reception of
 *        NRP_MLME_PEER_STATUS_IND and reception of
 *        HIC_CTRL_LEAVE_CFM.
 *
 * @param none.
 *
 * @return True if inbetween eception of
 *         NRP_MLME_PEER_STATUS_IND and reception of
 *         HIC_CTRL_LEAVE_CFM else FALSE.
 */
int   WiFiEngine_isDisconnectInProgress()
{
   if(wifiEngineState.main_state == driverDisconnecting)
   {
      return TRUE;
   }

   return FALSE;
}

/*!
 * @brief Get the WMM association state of the associated net.
 *
 * @return
 * - 1 if the current association is a WMM association.
 * - 0 otherwise (including if we're not associated).
 */
bool_t WiFiEngine_isWMMAssociated(void)
{
   rBasicWiFiProperties* basicWiFiProperties;
   WiFiEngine_net_t*  net;
   bool_t isWMM;

   /* Get a reference to basic network information from the registry. */
   basicWiFiProperties   = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   isWMM = FALSE;

   if(wei_is_80211_connected())
   {
      net = wei_netlist_get_current_net();
      DE_ASSERT(net);

      if ((M80211_WMM_INFO_ELEM_IS_PRESENT(net->bss_p->bss_description_p) ||
           M80211_WMM_PARAM_ELEM_IS_PRESENT(net->bss_p->bss_description_p))
           && (basicWiFiProperties->enableWMM == STA_WMM_Enabled))
      {
         /* WMM Association */
         isWMM = TRUE;
      }
   }

   return isWMM;
}


/*!
 * @brief Set a adapter handle that identifies the device
 */
void WiFiEngine_RegisterAdapter(void *adapter)
{
   DE_ASSERT(wifiEngineState.adapter == NULL);
   wifiEngineState.adapter = adapter;
}

/*!
 * @brief Get the current adapter handle.
 *
 * @return Returns the adapter handle
 */
void *WiFiEngine_GetAdapter(void)
{
   return wifiEngineState.adapter;
}



/*!
 * @brief Register a handler for EAPOL frames.
 *
 * @param [in] handler Specifies a function that is called for all
 * received EAPOL frames.
 *
 * If the handler function returns a TRUE value, the received frame
 * will not be forwarded to the OS network layer. If the handler is
 * specified as NULL, all frames will be forwarded to the network
 * stack.
 *
 * @retval WIFI_ENGINE_SUCCESS this function always succeeds
 */
int
WiFiEngine_RegisterEAPOLHandler(int (*handler)(const void *, size_t))
{
   wifiEngineState.eapol_handler = handler;
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Copy the packed IEs from the cached association request
 *
 * Copy the packed IEs from the cached association request into the
 * provided buffer.
 *
 * @param dst The destination buffer.
 * @param len IN: The length of the destination buffer. OUT: The number of bytes copied.
 *
 * @return WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_INVALID_LENGTH
 *         if len was too small.
 */
int WiFiEngine_GetCachedAssocReqIEs(void *dst, size_t *len)
{
   Blob_t blob;
   m80211_mlme_associate_req_t *req;
   size_t new_len;
   WiFiEngine_net_t *net;
   char *p;

   req = wei_asscache_get_req();
   p = dst;
   /* Pack bss_desc into buffer and copy out the ies */
   if (req !=NULL && req->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
      HicWrapper_m80211_ie_rsn_parameter_set_t(&req->ie.rsn_parameter_set, &blob, ACTION_GET_SIZE);
      if (*len >= BLOB_CURRENT_SIZE(&blob))
      {
         new_len = BLOB_CURRENT_SIZE(&blob);
         INIT_BLOB(&blob, p, *len);
         HicWrapper_m80211_ie_rsn_parameter_set_t(&req->ie.rsn_parameter_set, &blob, ACTION_PACK);
         p += new_len;
         *len -= new_len;
      }
      else
      {
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
   }

   if (req !=NULL && req->ie.wapi_parameter_set.hdr.version == 1)
     {
       INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
       HicWrapper_m80211_ie_wapi_parameter_set_t(&req->ie.wapi_parameter_set, &blob, ACTION_GET_SIZE);
       if (*len >= BLOB_CURRENT_SIZE(&blob))
     {
       new_len = BLOB_CURRENT_SIZE(&blob);
       INIT_BLOB(&blob, p, *len);
       HicWrapper_m80211_ie_wapi_parameter_set_t(&req->ie.wapi_parameter_set, &blob, ACTION_PACK);
       p += new_len;
       *len -= new_len;
     }
       else
     {
       return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
     }
     }

   if (req != NULL && req->ie.wpa_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      /* WPA */
      INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
      HicWrapper_m80211_ie_wpa_parameter_set_t(&req->ie.wpa_parameter_set, &blob, ACTION_GET_SIZE);
      if (*len >= BLOB_CURRENT_SIZE(&blob))
      {
         new_len = BLOB_CURRENT_SIZE(&blob);
         INIT_BLOB(&blob, p, *len);
         HicWrapper_m80211_ie_wpa_parameter_set_t(&req->ie.wpa_parameter_set, &blob, ACTION_PACK);
         p += new_len;
         *len -= new_len;
      }
      else
      {
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
   }

   /* Hack to get the SSID IE (not present in our assoc request message */
   net = wei_netlist_get_current_net();
   if (net)
   {
      INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
      HicWrapper_m80211_ie_ssid_t(&(net->bss_p->bss_description_p->ie.ssid), &blob, ACTION_GET_SIZE);
      if (*len >= BLOB_CURRENT_SIZE(&blob))
      {
         new_len = BLOB_CURRENT_SIZE(&blob);
         INIT_BLOB(&blob, p, *len);
         HicWrapper_m80211_ie_ssid_t(&(net->bss_p->bss_description_p->ie.ssid), &blob, ACTION_PACK);
         p += new_len;
      }
      else
      {
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
   }
   *len = p - (char*)dst;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the cached association confirm message.
 *
 * @param dst Destination buffer.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE if no cached message was found.
 */
int WiFiEngine_GetCachedAssocCfmIEs(void *dst, size_t *len)
{
   m80211_mlme_associate_cfm_t *cfm;
   char *p;

   cfm = wei_asscache_get_cfm();
   p = dst;
#if (DE_CCX == CFG_INCLUDED)
   /* Pack the CCKM reassociation response element */
   if (cfm != NULL && cfm->ie.ccx_reassoc_rsp_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
       Blob_t blob;
       size_t new_len;
       WiFiEngine_net_t *net = NULL;
       
      INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
      HicWrapper_m80211_ie_ccx_reassoc_rsp_parameter_set_t(&cfm->ie.ccx_reassoc_rsp_parameter_set, &blob, ACTION_GET_SIZE);

      if (*len >= BLOB_CURRENT_SIZE(&blob))
      {
         new_len = BLOB_CURRENT_SIZE(&blob);
         INIT_BLOB(&blob, p, *len);
         HicWrapper_m80211_ie_ccx_reassoc_rsp_parameter_set_t(&cfm->ie.ccx_reassoc_rsp_parameter_set, &blob, ACTION_PACK);
         p += new_len;
         *len -= new_len;
      }
      else
      {
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
       
      // Add AP WPA/RSN IE.
#if (DE_CCX_ROAMING == CFG_INCLUDED)
      net = ccxnet;
#endif

     if (net)
     {
        int status;
       
       /* We need to check first for RSN because when RSN is used , WPA seems to have a
          wrong value stored with element ID 221 but length 6 which is not a valid WPA IE.
       */
       new_len = *len;
       status = WiFiEngine_PackRSNIE(&net->bss_p->bss_description_p->ie.rsn_parameter_set, p, &new_len); 
       if (status == WIFI_ENGINE_SUCCESS && new_len > 0)  {
          p += new_len;
         *len -= new_len;  
        }
        else {
            new_len = *len;
            status = WiFiEngine_PackWPAIE(&net->bss_p->bss_description_p->ie.wpa_parameter_set, p, &new_len); 
            if (status == WIFI_ENGINE_SUCCESS && new_len > 0)  {
                p += new_len;
                *len -= new_len;
            }
        }
    }
   }
#endif
    *len = p - (char*)dst;
    return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the cached association confirm message.
 *
 * @param dst Destination buffer.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE if no cached message was found.
 */
int WiFiEngine_GetCachedAssocCfm(m80211_mlme_associate_cfm_t *dst)
{
   m80211_mlme_associate_cfm_t *src;

   src = wei_asscache_get_cfm();
   if (src)
   {
      DE_MEMCPY(dst, src, sizeof *dst);
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Pack information elements
 *
 * Packs the active information elements in a bss descriptor into
 * a flag buffer.
 *
 * @param buf Output buffer (can be NULL if buflen is 0)
 * @param buflen Size of the output buffer
 * @param outlen Size of the packed result, or the required size if the
 *               output buffer was too small.
 * @param bss_desc IE source
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE_INVALID_LENGTH if the destination buffer
 *          was too small (outlen contains the required size).
 */
int WiFiEngine_PackIEs(char *buf, size_t buflen, size_t *outlen,
                       m80211_bss_description_t *bss_desc)
{
   char *p;
   Blob_t  blob;
   uint8_t ielen;
   int32_t l = buflen;

   *outlen = 0;

   p = buf;
   if (bss_desc->ie.ssid.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.ssid.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_ssid_t(&(bss_desc->ie.ssid), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.supported_rate_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_supported_rates_t(&(bss_desc->ie.supported_rate_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.ds_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.ds_parameter_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_ds_par_set_t(&(bss_desc->ie.ds_parameter_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.tim_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.tim_parameter_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_tim_t(&(bss_desc->ie.tim_parameter_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.country_info_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.country_info_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_country_t(&(bss_desc->ie.country_info_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.ibss_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.ibss_parameter_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_ibss_par_set_t(&(bss_desc->ie.ibss_parameter_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.wpa_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.wpa_parameter_set.hdr.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_wpa_parameter_set_t(&(bss_desc->ie.wpa_parameter_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.rsn_parameter_set.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_rsn_parameter_set_t(&(bss_desc->ie.rsn_parameter_set), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.wmm_parameter_element.WMM_hdr.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.wmm_parameter_element.WMM_hdr.hdr.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_WMM_parameter_element_t(&(bss_desc->ie.wmm_parameter_element), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }

   if (bss_desc->ie.wmm_information_element.WMM_hdr.hdr.hdr.id != M80211_IE_ID_NOT_USED)
   {
      ielen = bss_desc->ie.wmm_information_element.WMM_hdr.hdr.hdr.len + 2;
      *outlen += ielen;
      if (l >= ielen)
      {
         INIT_BLOB(&blob, p, ielen);
         HicWrapper_m80211_ie_WMM_information_element_t(&(bss_desc->ie.wmm_information_element), &blob, ACTION_PACK);
         p += ielen;
      }
      l -= ielen;
   }
   if (l < 0)
   {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Encode an RSN IE into a buffer
 *
 * @param rsn IN: the RSN info structure
 * @param data OUT: buffer where encoded RSN IE is written
 * @param len IN: size of data OUT: number of bytes copied, or necessary
 *
 * @return WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_INVALID_LENGTH
 *         if len was too small, or if data is NULL.
 */
int
WiFiEngine_PackRSNIE(m80211_ie_rsn_parameter_set_t *rsn,
                     void *data,
                     size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_rsn_parameter_set_t(rsn, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_rsn_parameter_set_t(rsn, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Encode a  WPA IE into a buffer
 *
 * @param wpa IN: the WPA info structure
 * @param data OUT: buffer where encoded WPA IE is written
 * @param len IN: size of data OUT: number of bytes copied, or necessary
 *
 * @return WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_INVALID_LENGTH
 *         if len was too small, or if data is NULL. this will set len to required length
 */
int
WiFiEngine_PackWPAIE(m80211_ie_wpa_parameter_set_t *wpa,
                     void *data,
                     size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_wpa_parameter_set_t(wpa, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_wpa_parameter_set_t(wpa, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Encode a  WMM Information Element into a buffer
 *
 * @param wmm IN: the WMM IE
 * @param data OUT: buffer where encoded WMM IE is written
 * @param len IN: size of data OUT: number of bytes copied, or necessary
 *
 * @return WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_INVALID_LENGTH
 *         if len was too small, or if data is NULL. this will set len to required length
 */
int
WiFiEngine_PackWMMIE(m80211_ie_WMM_information_element_t *wmm,
                     void *data,
                     size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_WMM_information_element_t(wmm, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_WMM_information_element_t(wmm, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Encode a  WMM Parameter Element into a buffer
 *
 * @param wmm IN: the WMM PE
 * @param data OUT: buffer where encoded WMM IE is written
 * @param len IN: size of data OUT: number of bytes copied, or necessary
 *
 * @return WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_INVALID_LENGTH
 *         if len was too small, or if data is NULL. this will set len to required length
 */
int
WiFiEngine_PackWMMPE(m80211_ie_WMM_parameter_element_t *wmm,
                     void *data,
                     size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_WMM_parameter_element_t(wmm, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_WMM_parameter_element_t(wmm, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Encode a WPS Information Element into a buffer
 *
 * @param [in]    wps   pointer to wps ie
 * @param [out]   data  buffer where encoded WPS IE is written
 * @param [inout] len   size of data in input, required size on output
 *
 * @retval WIFI_ENGINE_SUCCESS on success.
 * @retval WIFI_ENGINE_FAILURE_INVALID_LENGTH if len was too small, or
 *         if data is NULL
 *
 * Len parameter is set to required length regardless.
 */
int
WiFiEngine_PackWPSIE(m80211_ie_wps_parameter_set_t *wps,
                     void *data,
                     size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_wps_parameter_set_t(wps, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_wps_parameter_set_t(wps, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Encode a WAPI Information Element into a buffer
 *
 * @param [in]    wapi  pointer to wapi ie
 * @param [out]   data  buffer where encoded WAPI IE is written
 * @param [inout] len   size of data in input, required size on output
 *
 * @retval WIFI_ENGINE_SUCCESS on success.
 * @retval WIFI_ENGINE_FAILURE_INVALID_LENGTH if len was too small, or
 *         if data is NULL
 */
int
WiFiEngine_PackWAPIIE(m80211_ie_wapi_parameter_set_t *wapi,
                      void *data,
                      size_t *len)
{
   Blob_t blob;

   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_ie_wapi_parameter_set_t(wapi, &blob, ACTION_GET_SIZE);

   if (BLOB_CURRENT_SIZE(&blob) == 0) {
      *len = 0;
      return WIFI_ENGINE_SUCCESS;
   }

   if(data == NULL || *len < BLOB_CURRENT_SIZE(&blob)) {
      *len = BLOB_CURRENT_SIZE(&blob);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   INIT_BLOB(&blob, (char*)data, *len);
   HicWrapper_m80211_ie_wapi_parameter_set_t(wapi, &blob, ACTION_PACK);
   *len = BLOB_CURRENT_SIZE(&blob);

   return WIFI_ENGINE_SUCCESS;
}

int   WiFiEngine_CreateSSID(m80211_ie_ssid_t *ssid, char *name, size_t len)
{
   if (len > sizeof ssid->ssid)
   {
      ssid->hdr.len = 0;
      ssid->hdr.id  = M80211_IE_ID_NOT_USED;
      return FALSE;
   }
   DE_MEMCPY(ssid->ssid, name, len);
   ssid->hdr.len = len;
   ssid->hdr.id = M80211_IE_ID_SSID;

   return TRUE;

}

#if (DE_CCX == CFG_INCLUDED)
int   WiFiEngine_GetLastAssocInfo(char *ssid, uint16_t* ssid_len, char *bssid, uint16_t* channel, uint16_t* msecs)
{
   *ssid_len = wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len;
   *msecs = DriverEnvironment_GetTimestamp_msec()-wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp;
   DE_TRACE_STATIC2(TR_SM_HIGH_RES, "WiFiEngine_GetLastAssocInfo=%d\n", *ssid_len);
 //  DE_MEMCPY(&ssid, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid, *ssid_len);
 //  DE_MEMCPY(&bssid, wifiEngineState.ccxState.LastAssociatedNetInfo.MacAddress.octet, 6);
   return TRUE;

}

int WiFiEngine_GetCurrentCPL(uint8_t* cpl)
{
    WiFiEngine_net_t *net = NULL;

    net = wei_netlist_get_current_net();
    if(net)
    {
    if(net->bss_p->bss_description_p->ie.ccx_cpl_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
    {
            DE_TRACE_STATIC2(TR_SM_HIGH_RES, "Current net's cpl=%d\n", net->bss_p->bss_description_p->ie.ccx_cpl_parameter_set.cpl);
        *cpl = net->bss_p->bss_description_p->ie.ccx_cpl_parameter_set.cpl;
        return TRUE;
    }
    }
    DE_TRACE_STATIC(TR_SM_HIGH_RES, "not connected or connected to a not ccx enabled AP\n");

    return FALSE;
}

int WiFiEngine_GetCurrentTSMinterval(uint8_t* interval)
{
    WiFiEngine_net_t *net = NULL;

    net = wei_netlist_get_current_net();
    if(net)
    {
    if(net->bss_p->bss_description_p->ie.ccx_tsm_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
    {
            DE_TRACE_STATIC2(TR_SM_HIGH_RES, "Current net's tsm interval=%d\n", net->bss_p->bss_description_p->ie.ccx_tsm_parameter_set.interval);
        *interval = net->bss_p->bss_description_p->ie.ccx_tsm_parameter_set.interval;
        return TRUE;
    }
    }
    DE_TRACE_STATIC(TR_SM_HIGH_RES, "not connected or connected to a not ccx enabled AP\n");

    return FALSE;
}
//static we_ps_control_t *ccx_ps_control;
int WiFiEngine_SendAdjacentAPreport()
{
    size_t dhsize = WiFiEngine_GetDataHeaderSize();
    size_t len = 0;

    size_t nr_bytes_added;
    int count=6;
    char dst[6], iface_addr[6];
    m80211_mac_addr_t bssid;
    int status;
    u16 proto=0;
    u16 msecs=0;
    char *pkt;
    size_t pkt_size;
    u16 ccx_packet_len=0;
    u16 ccx_adj_len=0;

  //  ccx_ps_control = WiFiEngine_PSControlAlloc("CCX");
  //  WiFiEngine_InhibitPowerSave(ccx_ps_control);

    ccx_adj_len = 16 + wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len;
    ccx_packet_len = ccx_adj_len + 16;


    WiFiEngine_GetBSSID(&bssid);
    DE_MEMCPY(dst, bssid.octet, 6);

    WiFiEngine_GetMACAddress(iface_addr, &count);

    len = dhsize+55+wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len;
    nr_bytes_added = WiFiEngine_GetPaddingLength(len);

    pkt = DriverEnvironment_TX_Alloc(len+nr_bytes_added);

    DE_MEMCPY(pkt+dhsize, dst, 6);
    DE_MEMCPY(pkt+dhsize+6, iface_addr, 6);
    proto = ntohs(proto);
    DE_MEMCPY(pkt+dhsize+12, (u8*)&proto, 2);
 //   DE_MEMCPY(pkt+dhsize+14, data, data_len);

    pkt[dhsize+14] = 0xaa;
    pkt[dhsize+15] = 0xaa;
    pkt[dhsize+16] = 0x03;

    pkt[dhsize+17] = 0x00;
    pkt[dhsize+18] = 0x40;
    pkt[dhsize+19] = 0x96;
    pkt[dhsize+20] = 0x00;
    pkt[dhsize+21] = 0x00;


    pkt[dhsize+22] = (uint8_t) ((ccx_packet_len & 0xff00) >> 8);
    pkt[dhsize+23] = (uint8_t)  (ccx_packet_len & 0x00ff);

    pkt[dhsize+24] = 0x30;
    pkt[dhsize+25] = 0x00;

    DE_MEMCPY(pkt+dhsize+26, dst, 6);
    DE_MEMCPY(pkt+dhsize+32, iface_addr, 6);

    pkt[dhsize+38] = 0x9B;

    DE_MEMCPY(pkt+dhsize+39, &ccx_adj_len, 2);

    pkt[dhsize+41] = 0x00;
    pkt[dhsize+42] = 0x40;
    pkt[dhsize+43] = 0x96;
    pkt[dhsize+44] = 0x00;

    DE_MEMCPY(pkt+dhsize+45, &wifiEngineState.ccxState.LastAssociatedNetInfo.MacAddress.octet, 6);
    DE_MEMCPY(pkt+dhsize+51, &wifiEngineState.ccxState.LastAssociatedNetInfo.ChannelNumber, 2);
    DE_MEMCPY(pkt+dhsize+53, &wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len, 2);
    DE_MEMCPY(pkt+dhsize+55, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len);
    msecs = DriverEnvironment_GetTimestamp_msec()-wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp;
    DE_MEMCPY(pkt+dhsize+55+wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len, &msecs, 2);



    status = WiFiEngine_ProcessSendPacket(pkt+dhsize, 14,
                      len - dhsize,
                      pkt, &dhsize, 0, NULL);

    pkt_size = HIC_MESSAGE_LENGTH_GET(pkt);

    DE_TRACE_STATIC3(TR_SM_HIGH_RES, "WiFiEngine_SendAdjacentAPreport: pkt_size=%d, dhsize=%d\n", pkt_size, dhsize);

    if(status == WIFI_ENGINE_SUCCESS)
    {
        DriverEnvironment_HIC_Send(pkt, pkt_size);
        DE_TRACE_STATIC(TR_SM_HIGH_RES, "packet was sent!\n");

        //     WiFiEngine_AllowPowerSave(ccx_ps_control);
        //     WiFiEngine_PSControlFree(ccx_ps_control);

        //success, remove pending flag
        wifiEngineState.ccxState.resend_packet_mask &= (~CCX_AP_REPORT_PENDING);
        return TRUE;
    }

    else
    {
        DE_TRACE_STATIC2(TR_SM_HIGH_RES, "WiFiEngine_ProcessSendPacket returned with error %d\n", status);
        DriverEnvironment_TX_Free(pkt);

      //    WiFiEngine_AllowPowerSave(ccx_ps_control);
        //    WiFiEngine_PSControlFree(ccx_ps_control);

        //insert pending flag
        wifiEngineState.ccxState.resend_packet_mask |= CCX_AP_REPORT_PENDING;
        return FALSE;
    }
}

int WiFiEngine_SendTrafficStreamMetricsReport(void)
{
    size_t dhsize = WiFiEngine_GetDataHeaderSize();
    size_t len = 0;

    size_t nr_bytes_added;
    int count=6;
    char dst[6], iface_addr[6];
    m80211_mac_addr_t bssid;
    int status;
    u16 proto=0;
    char *pkt;
    size_t pkt_size;
    WiFiEngine_stats_t stats;
    uint16_t uplink_packet_queue_delay_average = 0;

    uint16_t tmp_s;
    uint32_t tmp_l;

    WiFiEngine_GetBSSID(&bssid);
    DE_MEMCPY(dst, bssid.octet, 6);

    WiFiEngine_GetMACAddress(iface_addr, &count);

    len = dhsize+69;
    nr_bytes_added = WiFiEngine_GetPaddingLength(len);

    pkt = DriverEnvironment_TX_Alloc(len+nr_bytes_added);

    DE_MEMCPY(pkt+dhsize, dst, 6);
    DE_MEMCPY(pkt+dhsize+6, iface_addr, 6);
    proto = ntohs(proto);
    DE_MEMCPY(pkt+dhsize+12, (u8*)&proto, 2);

    //CISCO Aironet SNAP header
    pkt[dhsize+14] = 0xaa;
    pkt[dhsize+15] = 0xaa;
    pkt[dhsize+16] = 0x03;

    pkt[dhsize+17] = 0x00;
    pkt[dhsize+18] = 0x40;
    pkt[dhsize+19] = 0x96;
    pkt[dhsize+20] = 0x00;
    pkt[dhsize+21] = 0x00;


    pkt[dhsize+22] = 0x00;
    pkt[dhsize+23] = 0x2d;

    pkt[dhsize+24] = 0x32;
    pkt[dhsize+25] = 0x81;

    //destination / source mac addresses
    DE_MEMCPY(pkt+dhsize+26, dst, 6);
    DE_MEMCPY(pkt+dhsize+32, iface_addr, 6);

    pkt[dhsize+38] = 0x00; //Dialog Token
    pkt[dhsize+39] = 0x00; //  "      "

    //<<<<<<<<Measurement Report IE>>>>>>>>>>
    // TABLE 56.4, page88, CCXv4 spec
    //Measurement Element ID
    pkt[dhsize+40] = 0x27;
    pkt[dhsize+41] = 0x00;

    //Length (25)
    pkt[dhsize+42] = 0x19;
    pkt[dhsize+43] = 0x00;

    //Measurement Token
    pkt[dhsize+44] = 0x00;
    pkt[dhsize+45] = 0x00;

    //Measurement Mode
    pkt[dhsize+46] = 0x00;
    //Measurement Type
    pkt[dhsize+47] = 0x06;

    /* This is table 56.5 from page 88 of CCXv4 spec */

    //field 1: uplink packet queue delay avg
    uplink_packet_queue_delay_average = (wifiEngineState.ccxState.metrics.driver_pkt_cnt != 0) ?
        (uint16_t) (wifiEngineState.ccxState.metrics.driver_pkt_delay_total / wifiEngineState.ccxState.metrics.driver_pkt_cnt) : 0;
    tmp_s = HTON16(&uplink_packet_queue_delay_average);
    DE_MEMCPY(pkt+dhsize+48,&tmp_s, 2);

    //field 2: uplink packet queue delay histogram
    tmp_s = HTON16(&wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[0]);
    DE_MEMCPY(pkt+dhsize+50, &tmp_s, 2);
    tmp_s = HTON16(&wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[1]);
    DE_MEMCPY(pkt+dhsize+52, &tmp_s, 2);
    tmp_s = HTON16(&wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[2]);
    DE_MEMCPY(pkt+dhsize+54, &tmp_s, 2);
    tmp_s = HTON16(&wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[3]);
    DE_MEMCPY(pkt+dhsize+56, &tmp_s, 2);

    //field 3: uplink packet transmit/media delay avg (from f/w)
    tmp_l = HTON32(&wifiEngineState.ccxState.metrics.uplink_packet_transmit_media_delay);
    DE_MEMCPY(pkt+dhsize+58, &tmp_l, 4);

    //field 4: uplink packet loss
    WiFiEngine_GetStatistics(&stats, 1);
    tmp_s = (uint16_t)stats.dot11FailedCount - wifiEngineState.ccxState.metrics.lastFwPktLoss;
    tmp_s = HTON16(&tmp_s);
    DE_MEMCPY(pkt+dhsize+62, &tmp_s, 2);

    //field 5: uplink packet count
    tmp_s = (uint16_t)stats.dot11TransmittedFrameCount - wifiEngineState.ccxState.metrics.lastFwPktCnt;
    tmp_s = HTON16(&tmp_s);
    DE_MEMCPY(pkt+dhsize+64, &tmp_s, 2);

    //field 6: roaming count
    pkt[dhsize+66] = wifiEngineState.ccxState.metrics.roaming_count;

    //field 7: roaming delay
    tmp_s = HTON16(&wifiEngineState.ccxState.metrics.roaming_delay);
    DE_MEMCPY(pkt+dhsize+67, &tmp_s, 2);

    /* end of table 56.5 from page 88 of CCXv4 spec */

    /* Try to send the packet */
    status = WiFiEngine_ProcessSendPacket(pkt+dhsize, 14, len - dhsize, pkt, &dhsize, 0, NULL);

    pkt_size = HIC_MESSAGE_LENGTH_GET(pkt);

    DE_TRACE_STATIC3(TR_SM_HIGH_RES, "pkt_size=%d, dhsize=%d\n", pkt_size, dhsize);

    if(status == WIFI_ENGINE_SUCCESS)
    {
        DriverEnvironment_HIC_Send(pkt, pkt_size);
        DE_TRACE_STATIC(TR_SM_HIGH_RES, "packet was sent!\n");

        /* success, remove pending flag */
        wifiEngineState.ccxState.resend_packet_mask &= (~CCX_TS_METRICS_PENDING);

        /* reset statistics that can be reset at this level */
        DE_MEMSET((char*)&wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[0], 0, 4*sizeof(uint16_t));
        wifiEngineState.ccxState.metrics.driver_pkt_delay_total = 0;
        wifiEngineState.ccxState.metrics.driver_pkt_cnt = 0;
        wifiEngineState.ccxState.metrics.lastFwPktLoss = (uint16_t)stats.dot11FailedCount;
        wifiEngineState.ccxState.metrics.lastFwPktCnt  = (uint16_t)stats.dot11TransmittedFrameCount;
        wifiEngineState.ccxState.metrics.roaming_count = 0;
        wifiEngineState.ccxState.metrics.roaming_delay = 0;
        return TRUE;
    }

    else
    {
        DE_TRACE_STATIC2(TR_SM_HIGH_RES, "WiFiEngine_ProcessSendPacket returned with error %d\n", status);
        DriverEnvironment_TX_Free(pkt);

        /* insert pending flag */
        wifiEngineState.ccxState.resend_packet_mask |= CCX_TS_METRICS_PENDING;
        return FALSE;
    }
}

int WiFiEngine_GetTSPECbody(char* tspec_body)
{
    int i;

    for(i=0;i<=7;i++)
    {
    if(((wifiEngineState.ccxState.addts_state[i].admission_state == 0)&&(wifiEngineState.ccxState.addts_state[i].active == ADDTS_ACCEPTED))
      ||((wifiEngineState.ccxState.addts_state[i].admission_state == 1)&&((wifiEngineState.ccxState.addts_state[i].active == ADDTS_REFUSED_RETRY)||(wifiEngineState.ccxState.addts_state[i].active == ADDTS_REFUSED_DO_NOT_RETRY_UNTIL_ROAMING))))
    {
        DE_MEMCPY(tspec_body, wifiEngineState.ccxState.addts_state[i].TSPEC_body, 55);
            return TRUE;
    }
    }
    return FALSE;
}

int WiFiEngine_is_in_joining_state(void)
{
    DE_TRACE_STATIC2(TR_SM_HIGH_RES, "state=%d\n", wifiEngineState.main_state);
    return (wifiEngineState.main_state==driverJoining);
}

int WiFiEngine_store_cpl_state(int enabled, int value)
{
    DE_TRACE_STATIC2(TR_SM_HIGH_RES, "cpl function is %d\n", enabled);
    wifiEngineState.ccxState.cpl_enabled = enabled;
    wifiEngineState.ccxState.cpl_value = value;
    return TRUE;
}

int WiFiEngine_is_dss_net(WiFiEngine_net_t* net)
{
    int j, isDSS=1;

    if(net->bss_p->bss_description_p->ie.supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED)
    {
        for(j=0;j<net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len;j++)
        {
            if((net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x82)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x84)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x8B)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x96))
            isDSS = 0;
        }
    }
    if((isDSS==1)&&(net->bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED))
    {
        for(j=0;j<net->bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.len;j++)
        {
            if((net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x82)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x84)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x8B)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x96))
            isDSS = 0;
        }
    }

    return isDSS;
}

int   WiFiEngine_Set_CCKM_Key(char* key, size_t key_len)
{
    // key_len must be 16 for now.
    DE_MEMCPY(wifiEngineState.ccxState.KRK,key, 16);
    wifiEngineState.ccxState.KRK_set = 1;

    return TRUE;
}
#endif //DE_CCX
/** @} */ /* End of we_util group */


