/* $Id: we_prop.c,v 1.120 2008-05-19 15:08:55 peek Exp $ */
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
This module implements the WiFiEngine 802.11 properties interface

*****************************************************************************/
/** @defgroup we_prop WiFiEngine 802.11 properties interface
 *
 * @brief WiFiEngine interface to various 802.11-related parameters
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
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"
#include "we_ind.h"

/*!
 * @brief Get the desired BSSID
 *
 * Get the desired BSSID. If the BSSID is the broadcast address, it
 * means we associate with any BSSID.
 * 
 * @param [out] bssid Where the BSSID will be stored.
 *
 * @return WIFI_ENGINE_SUCCESS on success, WIFI_ENGINE_FAILURE on
 * internal error.
 */
int
WiFiEngine_GetDesiredBSSID(m80211_mac_addr_t *bssid)
{
   rBasicWiFiProperties *properties;

   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   if (properties == NULL)
      return WIFI_ENGINE_FAILURE;

   REGISTRY_RLOCK();
   DE_MEMCPY(bssid, &properties->desiredBSSID, sizeof(*bssid));
   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the desired BSSID of the NIC
 *
 * Set the desired BSSID. Setting the BSSID will disable roaming.
 * Setting the BSSID to ff:ff:ff:ff:ff:ff (the broadcast MAC)
 * will reenable roaming and clear the desired BSSID.
 * 
 * @param [in] bssid The desired bssid.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input buffer length is
 * not 6. 
 */
int
WiFiEngine_SetDesiredBSSID(const m80211_mac_addr_t *bssid)
{
   rBasicWiFiProperties *properties;

   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   if (properties == NULL)
      return WIFI_ENGINE_FAILURE;

   REGISTRY_WLOCK();
   DE_MEMCPY(&properties->desiredBSSID, bssid, sizeof(*bssid));
   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current BSSID.
 *
 * @param bssid Output buffer.
 * @param byte_count IN: size of the input buffer, OUT: bytes copied.
 *
 * @return WIFI_ENGINE_SUCCESS on success, WIFI_ENGINE_FAILURE if
 *  no BSSID could be found.
 */
int   WiFiEngine_GetBSSID(m80211_mac_addr_t *bssid) 
{
   WiFiEngine_net_t *net;

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");
   REGISTRY_RLOCK();

   net = wei_netlist_get_current_net();
   if (net != NULL) {
      *bssid = net->bssId_AP;
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }
   else
   {
      DE_TRACE_STATIC(TR_NOISE, "No current net available\n");
   }
   REGISTRY_RUNLOCK();
   
   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the desired BSSID of the NIC
 *
 * Set the desired BSSID, and attempt to associate with the BSSID
 * set. If the NIC is already associated with a BSSID it will
 * disassociate from it first (even if it is the same BSSID as the one
 * being set).
 * 
 * @param addr The input buffer.
 * @param c IN: The size of the input buffer (this value should be 6
 * and is only used for sanity checking). OUT: The required
 * size for the input buffer (again, 6).
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input buffer length is
 * not 6. 
 */
int   WiFiEngine_SetBSSID(const m80211_mac_addr_t *bssid) 
{
   int status;

   status = WiFiEngine_SetDesiredBSSID(bssid);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

#ifdef CONNECT_ON_PROP_SET
   //if(wei_is_80211_connected()) {
      //WiFiEngine_sac_stop();
      //WiFiEngine_sac_start();
   //}
#endif

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the adapter MAC address
 * 
 * @param addr Output buffer.
 * @param byte_count IN: size of the input buffer, OUT: bytes copied.
 *
 * @return WIFI_ENGINE_SUCCESS on success, 
 *         WIFI_ENGINE_FAILURE if no BSSID could be found.
 *         WIFI_ENGINE_FAILURE_DEFER if the query should be retried at a later time.
 */
int   WiFiEngine_GetMACAddress(char *addr, IN OUT int *byte_count) 
{
   rGeneralWiFiProperties *prop;
   
   if (*byte_count < M80211_ADDRESS_SIZE) {
      *byte_count = M80211_ADDRESS_SIZE;
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   *byte_count = M80211_ADDRESS_SIZE;
   REGISTRY_RLOCK();
   prop = (rGeneralWiFiProperties*)Registry_GetProperty(ID_general);  
   if (prop != NULL) {
      if (wei_is_bssid_bcast(prop->macAddress))
      {
         REGISTRY_RUNLOCK();
         return WIFI_ENGINE_FAILURE_DEFER;
      }
      DE_MEMCPY(addr, prop->macAddress.octet, *byte_count);
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }
   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the adapter MAC address
 * 
 * @param addr Input buffer.
 * @param byte_count size of the input buffer
 *
 * @return WIFI_ENGINE_SUCCESS on success.
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH on bad buffer length.
 */
int   WiFiEngine_SetMACAddress(char *addr, int byte_count) 
{
   rGeneralWiFiProperties *properties;
   m80211_mac_addr_t a;
   char buf[20];
   
   if (byte_count != sizeof(a.octet)) {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   DE_MEMCPY(a.octet, addr, sizeof(a.octet));
   wei_print_mac(&a, buf, sizeof(buf));
   if(!M80211_IS_UCAST(&a)) {
      DE_TRACE_STRING(TR_WARN, "ignoring multicast address %s\n", buf);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   if((addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]) == 0) {
      DE_TRACE_STRING(TR_WARN, "ignoring zero address %s\n", buf);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   DE_TRACE_STRING(TR_NOISE, "setting mac address %s\n", buf);
   /* Update target */
   WiFiEngine_SendMIBSet(MIB_dot11MACAddress, 
                         NULL, addr, byte_count);

   properties = (rGeneralWiFiProperties *)Registry_GetProperty(ID_general);
   DE_ASSERT(properties != NULL);
   REGISTRY_WLOCK();
   properties->macAddress = a;
   REGISTRY_WUNLOCK();
   return WIFI_ENGINE_SUCCESS;
}



/*!
 * @brief Gets the Beacon period in Kusec (1024 usec)
 *
 * @param period Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_GetBeaconPeriod(unsigned long *period) 
{
   WiFiEngine_net_t*      assoc;

   assoc = wei_netlist_get_current_net();
   if (assoc) {
      *period = (unsigned long)assoc->bss_p->bss_description_p->beacon_period;
      return WIFI_ENGINE_SUCCESS;
   }
   
   return WIFI_ENGINE_FAILURE;
}


/*!
 * @brief Gets the DTIM period in (in beacons periods)
 *
 * @param period Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int WiFiEngine_GetDTIMPeriod(uint8_t *period)
{
   WiFiEngine_net_t*      assoc;
   assoc = wei_netlist_get_current_net();
   if (assoc) {
      *period = (unsigned long)assoc->bss_p->dtim_period;
      return WIFI_ENGINE_SUCCESS;
   }
   
   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Sets the Beacon period in Kusec (1024 usec)
 *
 * @param period Period in Kusec
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_SetBeaconPeriod(unsigned long period) 
{
   WiFiEngine_net_t*      assoc;

   assoc = wei_netlist_get_current_net();
   if (assoc) {
      assoc->bss_p->bss_description_p->beacon_period = (uint16_t)period;
      /* Make it so */
      
      return WIFI_ENGINE_SUCCESS;
   }
   
   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Gets the channel number of the associated net
 *
 * @param [out] channel Will hold channel nummber
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE if there's no active net
 */
int WiFiEngine_GetActiveChannel(uint8_t *channel) 
{
   WiFiEngine_net_t*      assoc;

   assoc = wei_netlist_get_current_net();
   if (assoc == NULL)
      return WIFI_ENGINE_FAILURE;

   *channel = assoc->bss_p->bss_description_p->ie.ds_parameter_set.channel;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Sets the channel number for the associated net
 *
 * @param [in] channel Channel number
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE if there's no active net
 */
int WiFiEngine_SetActiveChannel(uint8_t channel) 
{
   WiFiEngine_net_t*      assoc;
   
   assoc = wei_netlist_get_current_net();
   if (assoc == NULL)
      return WIFI_ENGINE_FAILURE;
   
   assoc->bss_p->bss_description_p->ie.ds_parameter_set.channel = channel;
   return WIFI_ENGINE_SUCCESS;
}

/* 
 * \brief Set channel number used for IBSS creation 
 * \param [in] channel The channel number to use, zero for no channel 
 * \return WIFI_ENGINE_SUCCESS
 */
int
WiFiEngine_SetIBSSTXChannel(uint8_t channel)
{
   rIBSSBeaconProperties* ibss;

   REGISTRY_WLOCK();
   ibss = (rIBSSBeaconProperties*)Registry_GetProperty(ID_ibssBeacon);
   if(channel == 0) {
      ibss->tx_channel.hdr.id = M80211_IE_ID_NOT_USED;
      ibss->tx_channel.hdr.len = 0;
   } else {
      ibss->tx_channel.hdr.id = M80211_IE_ID_DS_PAR_SET;
      ibss->tx_channel.hdr.len = 1;
   }
   ibss->tx_channel.channel = channel;

   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/* 
 * \brief Get channel number used for IBSS creation 
 * \param [out] channel The channel number used, zero for no channel.
 * \return WIFI_ENGINE_SUCCESS
 */
int
WiFiEngine_GetIBSSTXChannel(uint8_t *channel)
{
   rIBSSBeaconProperties* ibss;
   REGISTRY_RLOCK();
   ibss = (rIBSSBeaconProperties*)Registry_GetProperty(ID_ibssBeacon);
   if(ibss->tx_channel.hdr.id  == M80211_IE_ID_NOT_USED ||
      ibss->tx_channel.hdr.len == 0)
      *channel = 0;
   else
      *channel = ibss->tx_channel.channel;

   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Gets the IBSS Beacon period in Kusec (1024 usec)
 *
 * @param period Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_GetIBSSBeaconPeriod(uint16_t *period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   *period = ibss->beacon_period;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Sets the IBSS Beacon period in Kusec (1024 usec)
 *
 * @param period Period in Kusec
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_SetIBSSBeaconPeriod(uint16_t period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   ibss->beacon_period = period;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Gets the IBSS DTIM period (in beacons periods)
 *
 * @param period Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_GetIBSSDTIMPeriod(uint8_t *period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   *period = ibss->dtim_period;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Sets the IBSS DTIM period (in beacons periods)
 *
 * @param period DTIM period
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_SetIBSSDTIMPeriod(uint8_t period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   ibss->dtim_period = period;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Gets the IBSS ATIM Window (in TU)
 *
 * @param period Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_GetIBSSATIMWindow(uint16_t *period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   if(ibss->atim_set.hdr.id == M80211_IE_ID_IBSS_PAR_SET)
      *period = ibss->atim_set.atim_window;
   else 
      *period = 0;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Sets the IBSS ATIM Window (in TU)
 *
 * @param period ATIM window
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int   WiFiEngine_SetIBSSATIMWindow(uint16_t period) 
{
   rIBSSBeaconProperties *ibss;

   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties *)Registry_GetProperty(ID_ibssBeacon);
   DE_ASSERT(ibss != NULL);
   
   if(period == 0) {
      ibss->atim_set.hdr.id = M80211_IE_ID_NOT_USED;
   } else {
      ibss->atim_set.hdr.id = M80211_IE_ID_IBSS_PAR_SET;
   }
   ibss->atim_set.hdr.len = sizeof(ibss->atim_set.atim_window);
   ibss->atim_set.atim_window = period;
   REGISTRY_WUNLOCK();
   
   return WIFI_ENGINE_SUCCESS;
}

static void generic_mib_cb(wi_msg_param_t param, void* priv) {
   /* cb will be deregistered by we_ind_send */
   return;
}

static void rssi_beacon_ind_release(void* priv)
{
   wifiEngineState.rssi_beacon_h = NULL;
}

static void rssi_data_ind_release(void* priv)
{
   wifiEngineState.rssi_data_h = NULL;
}

/*!
 * @brief Get the RSSI level in dBm. Measured on beacons.
 *
 * @param rssi_level Output buffer
 * @param poll for a new value
 *
 * @return Returns WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_DEFER if
 * the value wasn't present in the MIB cache.
 */
int   WiFiEngine_GetRSSI(int32_t *rssi_level, int poll) 
{
   int status;

   if(rssi_level) *rssi_level = (int32_t)wifiEngineState.rssi;

   if(!poll)
      return WIFI_ENGINE_SUCCESS;

   status = we_ind_cond_register(&wifiEngineState.rssi_beacon_h,
                           WE_IND_MIB_DOT11RSSI, 
                           "WE_IND_MIB_DOT11RSSI", 
                           generic_mib_cb,
                           rssi_beacon_ind_release,
                           RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                           NULL);
   DE_ASSERT(status == WIFI_ENGINE_SUCCESS);

   wei_get_mib_with_update(MIB_dot11rssi, 
                           (char *)&wifiEngineState.rssi, 
                           sizeof wifiEngineState.rssi,
                           DriverEnvironment_LittleEndian2Native, 
                           WE_IND_MIB_DOT11RSSI);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the RSSI level in dBm. Measured on data-frames.
 *
 * @param rssi_level Output buffer
 * @param poll for a new value
 *
 * @return Returns WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE_DEFER if
 * the value wasn't present in the MIB cache.
 */
int   WiFiEngine_GetDataRSSI(int32_t *rssi_level, int poll) 
{
   int status;

   *rssi_level = (int32_t)wifiEngineState.data_rssi;

   if(!poll)
      return WIFI_ENGINE_SUCCESS;

   status = we_ind_cond_register(&wifiEngineState.rssi_data_h,
                                 WE_IND_MIB_DOT11RSSI_DATA, 
                                 "WE_IND_MIB_DOT11RSSI_DATA",
                                 generic_mib_cb,
                                 rssi_data_ind_release,
                                 RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                                 NULL);
   DE_ASSERT(status == WIFI_ENGINE_SUCCESS);

   wei_get_mib_with_update(MIB_dot11rssiDataFrame, 
                           (char *)&wifiEngineState.data_rssi, 
                           sizeof wifiEngineState.data_rssi,
                           DriverEnvironment_LittleEndian2Native, 
                           WE_IND_MIB_DOT11RSSI_DATA);

   return WIFI_ENGINE_SUCCESS;
}


int   WiFiEngine_GetSNR(int32_t *snr_level, int poll) 
{
   if(poll)
      wei_get_mib_with_update(MIB_dot11snrBeacon, 
                              (char *)&wifiEngineState.snr, 
                              sizeof wifiEngineState.snr,
                              DriverEnvironment_LittleEndian2Native, 
                              WE_IND_MIB_DOT11SNR_BEACON);
   *snr_level = (int32_t)wifiEngineState.snr;

   return WIFI_ENGINE_SUCCESS;
}


int   WiFiEngine_GetDataSNR(int32_t *snr_level, int poll) 
{
   if(poll)
      wei_get_mib_with_update(MIB_dot11snrData, 
                              (char *)&wifiEngineState.data_snr, 
                              sizeof wifiEngineState.data_snr,
                              DriverEnvironment_LittleEndian2Native,
                              WE_IND_MIB_DOT11SNR_DATA);
   *snr_level = (int32_t)wifiEngineState.data_snr;

   return WIFI_ENGINE_SUCCESS;
}

/* this is the noise level per channel measured between frames */
int
WiFiEngine_GetNoiseFloor(struct we_noise_floor *noise_floor)
{
   int status;
   int8_t mibval[14];
   unsigned int i;

   DE_ASSERT(DE_ARRAY_SIZE(mibval) == DE_ARRAY_SIZE(noise_floor->noise_dbm));

   /* this is slowly changing background noise, so not much point in
    * updating more often than every 5 seconds */
   status = WiFiEngine_RatelimitMIBGet(MIB_dot11noiseFloor, 
                                       5000, 
                                       mibval, 
                                       sizeof(mibval));


   if(status == WIFI_ENGINE_SUCCESS) {
      for(i = 0; i < DE_ARRAY_SIZE(mibval); i++) {
         noise_floor->noise_dbm[i] = mibval[i];
      }
   } else {
      /* this MIB may not be available, so default to some undefined
       * level */
      for(i = 0; i < DE_ARRAY_SIZE(mibval); i++) {
         noise_floor->noise_dbm[i] = NOISE_UNKNOWN;
      }
   }
   return status;
}

/*!
 * @brief Get the current SSID.
 *
 * This will retrieve the SSID we are associated with, or that was
 * previously set by WiFiEngine_SetSSID().
 *
 * @param [out] ssid Output buffer.
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE if no SSID could be found.
 */
int WiFiEngine_GetSSID(m80211_ie_ssid_t *ssid)
{
   rBasicWiFiProperties *properties;
   WiFiEngine_net_t       *assoc;

   REGISTRY_RLOCK();

   /* first try associated SSID */
   assoc = wei_netlist_get_current_net();
   if (assoc) {
      *ssid = assoc->bss_p->bss_description_p->ie.ssid;
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }

   /* else use the desired SSID, if present */
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   if (properties != NULL && 
       properties->desiredSSID.hdr.id == M80211_IE_ID_SSID) {
      *ssid = properties->desiredSSID;
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }

   REGISTRY_RUNLOCK();
   return WIFI_ENGINE_FAILURE;
}


/*!
 * @brief Disable the desired SSID
 *
 * Sets the desired SSID string in the regsitry to M80211_IE_ID_NOT_USED.
 */
void   WiFiEngine_DisableSSID(void)
{
   rBasicWiFiProperties *properties;

   /* Set the desired SSID */
   REGISTRY_WLOCK();
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(properties != NULL);
   properties->desiredSSID.hdr.id = M80211_IE_ID_NOT_USED;
   REGISTRY_WUNLOCK();
}

/*!
 * @brief Set the desired SSID
 *
 * Sets the desired SSID string in the driver.
 * If the driver is associated and the new desired SSID is different
 * from the current one a disassociation is performed followed by
 * a association attempt with the new SSID.
 *
 * The driver will try to associate with the desired SSID.  If the
 * desired SSID is set and no scan results advertising that SSID is
 * seen then no association will take place. However, if a scan result
 * with a matching SSID is seen at a later time then WiFiEngine will
 * attempt to associate with it on it's own accord.
 *
 * @param ssid Input buffer.
 * @param byte_count size of the input buffer. If this is 0
 * then the desired SSID will be unset.
 *
 * @return WIFI_ENGINE_SUCCESS on success. 
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input string was too long.
 */
int   WiFiEngine_SetSSID(char *ssid, int byte_count)
{
   rBasicWiFiProperties *properties;
   m80211_ie_ssid_t newssid;

   /* an SSID is not a NULL terminated string, don't use DE_TRACE_STRING here.
    * Doing so has been known to cause buffer overflows */
   DE_TRACE_DATA(TR_NOISE, "WiFiEngine_SetSSID", ssid, byte_count);

   DE_ASSERT(byte_count <= sizeof(newssid.ssid));

   if (byte_count < 0 || byte_count > M80211_IE_MAX_LENGTH_SSID)
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;

   newssid.ssid[0] = 0x00;;
   newssid.hdr.len = byte_count;
   if (0 == byte_count)
   {
      newssid.hdr.id = M80211_IE_ID_NOT_USED;
   }
   else
   {
      newssid.hdr.id = M80211_IE_ID_SSID;
      DE_MEMCPY(newssid.ssid, ssid, byte_count);
   }

#ifdef WIFI_DEBUG_ON
   {
      char str[M80211_IE_MAX_LENGTH_SSID + 1];
      DE_TRACE_STRING(TR_ASSOC, "Setting SSID \"%s\"\n", wei_printSSID(&newssid, str, sizeof(str)));
   }
#endif

   /* Set the desired SSID */
   REGISTRY_WLOCK();
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(properties != NULL);
   properties->desiredSSID = newssid;
   REGISTRY_WUNLOCK();

   // should be done in the host driver
#ifdef CONNECT_ON_PROP_SET
   WiFiEngine_sac_stop();
   WiFiEngine_sac_start();
#endif

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set list of regional channels to use.
 */
int
WiFiEngine_SetRegionalChannels(const channel_list_t *channels)
{
   rBasicWiFiProperties *basic;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   basic->regionalChannels = *channels;

   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get list of regional channels in use.
 */
int
WiFiEngine_GetRegionalChannels(channel_list_t *channels)
{
   rBasicWiFiProperties *basic;

   REGISTRY_RLOCK();
   basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   *channels = basic->regionalChannels;

   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set operational rates (both basic and extended)
 * 
 * @param rates a list of rates (in 0.5Mb/s units)
 * @param len size of rates
 *
 * @return Always return WIFI_ENGINE_SUCCESS
 */
int
WiFiEngine_SetSupportedRates(uint8_t *rates, size_t len)
{
   rBasicWiFiProperties* bwp;
   rRateList *brates;
    
   REGISTRY_WLOCK();

   bwp = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);  
   brates = &bwp->supportedRateSet;

   if (len > sizeof(brates->rates))
      len = sizeof(brates->rates);

   DE_MEMCPY(brates->rates, rates, len);
   brates->len = len;

   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set IBSS operational rates (both basic and extended)
 * 
 * @param rates a list of rates (in 0.5Mb/s units)
 * @param len size of rates
 *
 * @return Always return WIFI_ENGINE_SUCCESS
 */
int
WiFiEngine_SetIBSSSupportedRates(uint8_t *rates, size_t len)
{
   rIBSSBeaconProperties *ibss;
   rRateList *brates;
    
   REGISTRY_WLOCK();

   ibss = (rIBSSBeaconProperties*)Registry_GetProperty(ID_ibssBeacon);
   brates = &ibss->supportedRateSet;

   if (len > sizeof(brates->rates))
      len = sizeof(brates->rates);

   DE_MEMCPY(brates->rates, rates, len);
   brates->len = len;

   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

static we_ratemask_t wei_ratemask(uint8_t *rates, size_t len)
{
   we_xmit_rate_t r;
   unsigned int i;
   we_ratemask_t mask;

   WE_RATEMASK_CLEAR(mask);
   for (i = 0; i < len; i++) 
   {
      r = WiFiEngine_rate_ieee2native(rates[i]);
      DE_ASSERT(r != WE_XMIT_RATE_INVALID);
      WE_RATEMASK_SETRATE(mask, r);
   }
   return mask;
}

/*!
 * @brief Get a bitmask describing the list of supported data rates
 * 
 * @param rateMask The output buffer (allocated by caller).
 *
 * @return Always return WIFI_ENGINE_SUCCESS
 */
int   WiFiEngine_GetSupportedRates(we_ratemask_t *rateMask) 
{
   rBasicWiFiProperties*   basicWiFiProperties;

   REGISTRY_RLOCK();
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   *rateMask = wei_ratemask(basicWiFiProperties->supportedRateSet.rates, 
                            basicWiFiProperties->supportedRateSet.len);
   REGISTRY_RUNLOCK();
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get a bitmask describing the basic rate set for the current association
 * 
 * @param rateMask The output buffer (allocated by caller).
 *
 * @return WIFI_ENGINE_SUCCESS on success. 
 * WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no association has been made.
 */
int   WiFiEngine_GetBSSRates(we_ratemask_t *rateMask) 
{
   we_ratemask_t m;
   WiFiEngine_net_t *net;
   m80211_bss_description_t *bd;

   WE_RATEMASK_CLEAR(*rateMask);
   if (!wei_is_80211_connected())
   {
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }
   net = wei_netlist_get_current_net();
   if (net == NULL)
   {
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }
   bd = net->bss_p->bss_description_p;
   if(bd->ie.supported_rate_set.hdr.id == M80211_IE_ID_SUPPORTED_RATES) {
      m = wei_ratemask(bd->ie.ext_supported_rate_set.rates,
                       bd->ie.ext_supported_rate_set.hdr.len);
      WE_RATEMASK_OR(*rateMask, m);
   }
   if(bd->ie.ext_supported_rate_set.hdr.id == M80211_IE_ID_EXTENDED_SUPPORTED_RATES) {
      m = wei_ratemask(bd->ie.supported_rate_set.rates,
                       bd->ie.supported_rate_set.hdr.len);
      WE_RATEMASK_OR(*rateMask, m);
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the fragmentation threshold.
 *
 * @param frag_threshold Input buffer
 *
 * @return Always returns WIFI_ENGINE_SUCCESS.
 */
int   WiFiEngine_GetFragmentationThreshold(int *frag_threshold)
{
   wei_get_mib_with_update(MIB_dot11FragmentationThreshold, 
                           (char *)&wifiEngineState.frag_thres, 
                           sizeof wifiEngineState.frag_thres,
                           DriverEnvironment_LittleEndian2Native,
                           WE_IND_NOOP);
   *frag_threshold = wifiEngineState.frag_thres;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the fragmentation threshold.
 *
 * @param frag_threshold Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS on success
 * WIFI_ENGINE_FAILURE otherwise.
 */
int   WiFiEngine_SetFragmentationThreshold(int frag_threshold)
{
   uint16_t frag;

   /* Set the frag threshold in the device MIB */
   frag = frag_threshold;
   wifiEngineState.frag_thres = frag;
   if ( WiFiEngine_SendMIBSet(MIB_dot11FragmentationThreshold,
                         NULL, (char *)&frag, sizeof(frag))
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Get the RTS threshold.
 *
 * @param rts_threshold Input buffer
 *
 * @return Always return WIFI_ENGINE_SUCCESS.
 */
int   WiFiEngine_GetRTSThreshold(int *rts_threshold)
{
   wei_get_mib_with_update(MIB_dot11RTSThreshold, 
                           (char *)&wifiEngineState.rts_thres, 
                           sizeof wifiEngineState.rts_thres,
                           DriverEnvironment_LittleEndian2Native,
                           WE_IND_NOOP);
   *rts_threshold = (int)wifiEngineState.rts_thres;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the RTS threshold.
 *
 * @param rts_threshold Input buffer
 *
 * @return WIFI_ENGINE_SUCCESS on success
 * WIFI_ENGINE_FAILURE otherwise.
 */
int   WiFiEngine_SetRTSThreshold(int rts_threshold)
{
   uint16_t thres;
   
   /* Set the frag threshold in the device MIB */
   thres = rts_threshold;
   if ( WiFiEngine_SendMIBSet(MIB_dot11RTSThreshold,
                         NULL, (char *)&thres, sizeof thres)
        == WIFI_ENGINE_SUCCESS)
   {
      wifiEngineState.rts_thres = (uint16_t)rts_threshold;
      return WIFI_ENGINE_SUCCESS;
   }
   return WIFI_ENGINE_FAILURE;
}


/*!
 * @brief Get the join timeout
 *
 * Gets the join timeout. 
 *
 * @param joinTimeout OUT: Timeout value given in beacon intervals.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS.
 */
int WiFiEngine_GetJoin_Timeout(int* joinTimeout)
{
   rBasicWiFiProperties*   basicWiFiProperties;
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   *joinTimeout = (int)basicWiFiProperties->connectionPolicy.joinTimeout;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the join timeout
 *
 * Sets the join timeout. 
 *
 * @param joinTimeout Timeout value given in beacon intervals.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS.
 */
int WiFiEngine_SetJoin_Timeout(int joinTimeout)
{
   rBasicWiFiProperties*   basicWiFiProperties;
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   basicWiFiProperties->connectionPolicy.joinTimeout = (rTimeout)joinTimeout;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the BSS basic rate set
 * 
 * Get the basic rate set for the currently associated BSS
 *
 * @param rates The output buffer.
 *
 * @return Return WIFI_ENGINE_SUCCESS on success,
 * WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no BSS is associated with the NIC.
 */
int   WiFiEngine_GetBSSBasicRateSet(m80211_ie_supported_rates_t *rates)
{
   WiFiEngine_net_t*      net;

   REGISTRY_RLOCK();
   
   net = wei_netlist_get_current_net();

   if (net == NULL)
   {
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }
   if (!wei_is_80211_connected())
   {
      REGISTRY_RUNLOCK();
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }
   DE_MEMCPY((rSupportedRates *)rates, &(net->bss_p->bss_description_p->ie.supported_rate_set), sizeof *rates);
   
   REGISTRY_RUNLOCK();
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current BSS type
 *
 * Get the current BSS Type, Infrastructure or Independent.
 * 
 * @param bss_type Output buffer. Will contain the values M802
 */
int WiFiEngine_GetBSSType(WiFiEngine_bss_type_t *bss_type)
{
   rBasicWiFiProperties*   basicWiFiProperties;
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   *bss_type = (WiFiEngine_bss_type_t)basicWiFiProperties->connectionPolicy.defaultBssType;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the BSS type
 *
 * Sets the BSS (network infrastructure) type. This will reset the network
 * association algorithm and delete all WEP keys.
 *
 * @param bss_type The network type to set. Takes the values Infrastructure_BSS
 * for a ESS or Independent_BSS for a BSS.
 *
 * @return Returns WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE on invalid bss_type.
 */
int WiFiEngine_SetBSSType(WiFiEngine_bss_type_t bss_type)
{
   rBasicWiFiProperties*   basicWiFiProperties;
   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   if (bss_type != (WiFiEngine_bss_type_t)Infrastructure_BSS && bss_type != (WiFiEngine_bss_type_t)Independent_BSS)
   {
      DE_TRACE_STATIC(TR_NOISE, "Setting invalid bss_type\n");
      return WIFI_ENGINE_FAILURE;
   }

   if((wifiEngineState.main_state == driverStarting) &&
      (bss_type == (WiFiEngine_bss_type_t)Infrastructure_BSS))
   {
      /* We have created an ibss net but we are not connected */
      /* Reset state machine to StateFunction_PowerUp */
      WiFiEngine_LeaveIbss();
   }

   if((wifiEngineState.main_state == driverStarting) &&
      (bss_type == (WiFiEngine_bss_type_t)Independent_BSS))
   {
      /* Mode is already set */
      return WIFI_ENGINE_SUCCESS;
   }
   
   if (wei_network_status_busy()) 
   {
      WiFiEngine_bss_type_t type;
      
      WiFiEngine_GetBSSType(&type);
      if(type == M80211_CAPABILITY_ESS)
      {
         /* Deauthenticate is a more thourough cleanup than disassociate. */
         /*
         Note: it seems that we send a de-authentication as soon as we associate because of this.
         Need further investigation.  Right now we disable it because we can do without this call.
         WiFiEngine_Deauthenticate();
         */
         DE_TRACE_STATIC(TR_NOISE, "Wanted to WiFiEngine_Deauthenticate in WiFiEngine_SetBSSType\n");
      }
      else
      {
         WiFiEngine_LeaveIbss();
      }

   }
   
   basicWiFiProperties->connectionPolicy.defaultBssType = (rBSS_Type)bss_type;
 
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get a supported network type
 *
 * Get the supported network type given by the index.
 * Iterate over this call with increasing index to
 * get all the supported types.
 *
 * @param type Output buffer.
 * @param index The network type index (0 based).
 *
 * @return WIFI_ENGINE_SUCCESS on success. 
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH if the index was out of bounds.
 */
int WiFiEngine_GetSupportedNetworkTypes(WiFiEngine_phy_t *type,
                                        int index) 
{
   switch(index) {
      case 0:
         *type = PHY_TYPE_DS;
         break;
      case 1:
         *type = PHY_TYPE_OFDM24;
         break;
      default:
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current transmission rate.
 *
 * Get the rate used to send the most recently transmitted data frame.
 * This speed can change dynamically depending on conditions.
 *
 * @param rate The rate in units of 0.5Mbit/second.
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int WiFiEngine_GetCurrentTxRate(unsigned int *rate)
{
   if((*rate = WiFiEngine_rate_native2ieee(wifiEngineState.current_tx_rate)) == 0)
      return WIFI_ENGINE_FAILURE;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current reception rate.
 *
 * Get the rate that the most recently received data packet was
 * transmitted with. This speed can change dynamically depending on
 * conditions.
 *
 * @param rate The rate in units of 0.5Mbit/second.
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE
 */
int WiFiEngine_GetCurrentRxRate(unsigned int *rate)
{
   if((*rate = WiFiEngine_rate_native2ieee(wifiEngineState.current_rx_rate)) == 0)
      return WIFI_ENGINE_FAILURE;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the network type currently in use.
 *
 * The type is derived from the rate used in the current association.
 *
 * @param type Output buffer
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetNetworkTypeInUse(WiFiEngine_phy_t *type) 
{
   

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Foo
 */
int   WiFiEngine_GetTxPowerLevel(int *qpsk_level, int *ofdm_level)
{
   *qpsk_level = -(int)wifiEngineState.power_index.qpsk_index;
   *ofdm_level = -(int)wifiEngineState.power_index.ofdm_index;

   wei_get_mib_with_update(MIB_dot11powerIndex, 
                           (void*)&wifiEngineState.power_index, 
                           sizeof(wifiEngineState.power_index),
                           NULL,
                           WE_IND_NOOP);

   return WIFI_ENGINE_SUCCESS;
}


#if (DE_CCX == CFG_INCLUDED)
/*!
 * @brief Unfoo
 */
int   WiFiEngine_SetTxPowerLevel_from_cpl_ie(int qpsk_level, int ofdm_level)
{
   int r;
   uint8_t index[2];

   DE_TRACE_STATIC3(TR_NOISE, "ofdm_level = %d, qpsk_level=%d\n", ofdm_level, qpsk_level);

   if(qpsk_level < -19 || qpsk_level > 0)
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   if(ofdm_level < -19 || ofdm_level > 0)
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   

   wifiEngineState.ccxState.cpl_tx_value_dsss = DSSS_MAX_LVL+qpsk_level;
   wifiEngineState.ccxState.cpl_tx_value_ofdm = DSSS_MAX_LVL+ofdm_level;

   index[0] = -qpsk_level;
   index[1] = -ofdm_level;


   r = WiFiEngine_SendMIBSet(MIB_dot11powerIndex,
                                NULL,
                                (char*)index, sizeof(index));

   return r;
}
#endif

/*!
 * @brief Unfoo
 */
int   WiFiEngine_SetTxPowerLevel(int qpsk_level, int ofdm_level)
{
   if(qpsk_level < -19 || qpsk_level > 0)
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   if(ofdm_level < -19 || ofdm_level > 0)
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   
   wifiEngineState.power_index.qpsk_index = -qpsk_level;
   wifiEngineState.power_index.ofdm_index = -ofdm_level;

   return WiFiEngine_SendMIBSet(MIB_dot11powerIndex,
                                NULL,
                                (void*)&wifiEngineState.power_index, 
                                sizeof(wifiEngineState.power_index));
}
/*!
 * @brief Sets current chip type
 */
int WiFiEngine_Set_Chip_Type(int chip_type)
{
   wifiEngineState.chip_type = (wifi_chip_type_t)chip_type;
   return WIFI_ENGINE_SUCCESS;
}
/*!
 * @brief Sets current fpga version
 */
int WiFiEngine_Set_Fpga_Version(char *version)
{
   DE_MEMCPY(wifiEngineState.fpga_version,version, sizeof(wifiEngineState.fpga_version));
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set WMM support flag in on/off/auto mode.
 *
 * @param enable Mode for WMM.
 *               Flag STA_WMM_Disabled: No WMM support
 *               Flag STA_WMM_Enabled:  WMM Enabled
 *               Flag STA_WMM_Auto:     WMM Enabled with AP supporting WMM
 *
 * @return WIFI_ENGINE_SUCCESS on success, else WIFI_ENGINE_FAILURE.
 */
int   WiFiEngine_SetWMMEnable(rSTA_WMMSupport enable)
{
   rBasicWiFiProperties *basic;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->enableWMM = enable;
   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get WMM support flag.
 *
 * @param enable Mode for WMM.
 *               Flag STA_WMM_Disabled: No WMM support
 *               Flag STA_WMM_Enabled:  WMM Enabled
 *               Flag STA_WMM_Auto:     WMM Enabled with AP supporting WMM
 *
 * @return WIFI_ENGINE_SUCCESS on success, else WIFI_ENGINE_FAILURE.
 */
int WiFiEngine_GetWMMEnable(rSTA_WMMSupport *enable)
{
   rBasicWiFiProperties *basic;
   REGISTRY_RLOCK();
   basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   *enable = basic->enableWMM;
   REGISTRY_RUNLOCK();
   return WIFI_ENGINE_SUCCESS;
}


/***** Not yet implemented ***/

/*!
 * @brief Not yet implemented
 */
int   WiFiEngine_GetNumberOfAntennas(unsigned long *count)
{
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
}
/*!
 * @brief Not yet implemented
 */
int   WiFiEngine_GetRxAntenna(unsigned long *n)
{
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
}
/*!
 * @brief Not yet implemented
 */
int   WiFiEngine_GetTxAntenna(unsigned long *n)
{
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
}

int get_link_stats_cb(we_cb_container_t *cbc)
{
        dot11CountersEntry_t *ctr;

        DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

        if (NULL == cbc)
        {
                DE_TRACE_STATIC(TR_MIB, "received NULL cbc\n");
                goto ret;

        }
        if ( WIFI_ENGINE_FAILURE_ABORT == cbc->status )
        {
                DE_TRACE_STATIC(TR_MIB, "received ABORT status\n");
                goto ret;
        } else
        {
                DE_TRACE_INT(TR_MIB, "got status %d\n", cbc->status);
        }
        if ( cbc->data )
        {
                DBG_PRINTBUF("get_link_stats_cb() got data ", (unsigned char *)cbc->data, cbc->data_len);
        }

        if (cbc->data_len != sizeof *ctr)
        {
                DE_TRACE_STATIC(TR_MIB, "got invalid length mac address from MIB get\n");
                goto ret;
        }

        if (cbc->data && cbc->data_len)
        {
                ctr = (dot11CountersEntry_t *)cbc->data;
                /* WiFiEngine_stats_t is an alias fo dot11CountersEntry_t */
                DE_MEMCPY(&(wifiEngineState.link_stats), ctr, sizeof wifiEngineState.link_stats);
        }
  ret:

        return 0;
}

/*!
 * @brief Get Statistics.
 *
 * @param stats The output buffer.
 * @param update Normally a MIB query is issued to the device to update
 *        the cached statistics information. When update is 0 this does
 *        not happen. The cached statistics is just returned.
 * 
 * @return WIFI_ENGINE_SUCCESS on success or WIFI_ENGINE_FAILURE on failure
 */
int   WiFiEngine_GetStatistics(WiFiEngine_stats_t* stats, int update)
{
        int status = WIFI_ENGINE_SUCCESS;
        we_cb_container_t *cbc;

        if (update)
        {
                cbc = WiFiEngine_BuildCBC(get_link_stats_cb, NULL, 0, FALSE);
                WiFiEngine_GetMIBAsynch(MIB_dot11CountersTable, cbc);
        }
        *stats = wifiEngineState.link_stats;

	return status;
}

/*!
 * @brief Configures aggregation between WFE and target
 *
 * Enables aggregation of messages sent from target to host-driver via SDIO/SPI.
 * Please note that this function only queues the configuration messages, the 
 * acctual messages will be sent to target later.
 * This functon cannot be called when we are unplugged.
 *
 * @param type   the type of messages that should be aggregated
 * @param aggr_max_size maximun size in bytes of the aggregated packets from
 *      target
 *
 * @return WIFI_ENGINE_SUCCESS on success
 *         WIFI_ENGINE_FAILURE on any error.
 *         
 */
int
WiFiEngine_ConfigureHICaggregation(we_aggr_type_t type, uint32_t aggr_max_size)
{

   if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
      return WIFI_ENGINE_FAILURE;
      
   //There is no scientiffic reason for choosing theese values
   if((aggr_max_size < 256) || (aggr_max_size > 8192))
   {
      DE_TRACE_INT(TR_WARN, "Invalid aggregation max-size: %u\n", aggr_max_size);
      return WIFI_ENGINE_FAILURE;
   }

   if(type >= AGGR_LAST_MARK)
   {
      DE_TRACE_INT(TR_WARN, "Invalid aggregation max-size: %u\n", aggr_max_size);
      return WIFI_ENGINE_FAILURE;
   }

   wei_enable_aggregation(type, aggr_max_size);

    return WIFI_ENGINE_SUCCESS;
   
}

/** @} */ /* End of we_prop group */

