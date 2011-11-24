/* $Id: we_nr.c,v 1.110 2008-05-19 16:06:50 ulla Exp $ */
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
This module implements the WiFiEngine Nanoradio proprietary interface

*****************************************************************************/
/** @defgroup we_nr WiFiEngine Nanoradio proprietary interface
 *
 * @brief This module contains the API for configuring Nanoradio-specific
 * properties of WiFiEngine and the device that are not connected to
 * the IEEE standards.
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
#include "release_tag.h"

#ifdef FILE_WE_NR_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_NR_C
#endif //FILE_WE_NR_C

/*!
 * @brief Send list of locally configured channels to firmware.
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE_RESOURCES on memory allocation errors
 */
int WiFiEngine_ActivateRegionChannels(void)
{
   int status;
   unsigned int i;
   channel_list_t channels;
   unsigned int tcount;
   m80211_ie_country_t *cie;

   rBasicWiFiProperties*   basic;

   basic   = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic); 

   status = WiFiEngine_GetRegionalChannels(&channels);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   /* count triplets */
   tcount = 0;
   for(i = 0; i < channels.no_channels; i++) {
      if(i == 0 || channels.channelList[i] != channels.channelList[i-1] + 1)
         tcount++;
   }

   /* Allocate the IE */
   cie = (m80211_ie_country_t *)WrapperAllocStructure(NULL, sizeof(*cie));
   if(cie == NULL)
      return WIFI_ENGINE_FAILURE_RESOURCES;
   cie->channel_info = (m80211_country_channels_t *)WrapperAttachStructure(cie, 
                                              tcount * sizeof(*cie->channel_info));
   if(cie->channel_info == NULL) {
      WrapperFreeStructure(cie);
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   cie->hdr.id = M80211_IE_ID_COUNTRY;
   cie->hdr.len = M80211_IE_LEN_COUNTRY_STRING + tcount * 3 /* sizeof could include padding */;

   /* Since we don't know which country this belongs to, let's use one
    * of the user defined codes. */
   cie->country_string.string[0] = 'X';
   cie->country_string.string[1] = 'A';
   cie->country_string.string[2] = ' ';

   tcount = 0;
   for(i = 0; i < channels.no_channels; i++) {
      if(i == 0 || channels.channelList[i] != channels.channelList[i-1] + 1) {
         tcount++;
         cie->channel_info[tcount - 1].first_channel = channels.channelList[i];
         cie->channel_info[tcount - 1].num_channels = 1;
         cie->channel_info[tcount - 1].max_tx_power = basic->maxPower;
      } else {
         cie->channel_info[tcount - 1].num_channels++;
      }
   }

   if(wifiEngineState.active_channels_ref != NULL)
      WrapperFreeStructure(wifiEngineState.active_channels_ref);
   wifiEngineState.active_channels_ref = cie;
   wei_send_set_scancountryinfo_req(wifiEngineState.active_channels_ref);

   return WIFI_ENGINE_SUCCESS;
}



/*!
 * @brief Check if mic countermeasure has started
 *
 * @param none
 *
 * @return True if started else false
 */
int WiFiEngine_isMicCounterMeasureStarted(void)
{
   if (WES_TEST_FLAG(WES_FLAG_ASSOC_BLOCKED))
   {
      return TRUE;

   }

   return FALSE;
}

/*!
 * @brief Check if coredump has started
 *
 * @param none
 *
 * @return True if started else false
int WiFiEngine_isCoredumpEnabled(void)
{

   if (wifiEngineState.core_dump_state == WEI_CORE_DUMP_ENABLED)
   {
      return TRUE;

   }

   return FALSE;
}
 */



/*!
 * @brief Informs firmware the the initi sequence is complete.
 *      
 *
 * @param void
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
void WiFiEngine_Init_Complete(void)
{
   if (Mlme_Send(Mlme_CreateInitCompleteReq, 0, wei_send_cmd))
   {
      DE_TRACE_STATIC(TR_SM, "Sending HIC_CTRL_INIT_COMPLETED_REQ\n");            
   }
   else 
   {
      DE_TRACE_STATIC(TR_SM, "Failed to create HIC_CTRL_INIT_COMPLETED_REQ)\n");              
      DE_BUG_ON(1, "Mlme_CreateSleepForeverReq() failed in WiFiEngine_SoftShutdown()\n");
   }

}


/*!
 * @brief Set MHG state machine mode.
 *
 * @param mode .WIFI_HMG_TRANSPARENT or
 *              WIFI_HMG_AUTO
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetHmgMode(WiFiEngine_HmgMode_t mode)
{
   rHostDriverProperties *properties;

   properties = (rHostDriverProperties *)Registry_GetProperty(ID_hostDriver);

   properties->hmgAutoMode = mode;
           
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the level 1 adaptive TX rate mode
 *
 * @param mode Adaptive TX rate mode. 0 to disable, 1 to enable.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetLvl1AdaptiveTxRate(uint8_t mode)
{
   if ( WiFiEngine_SendMIBSet(MIB_dot11adaptiveTxRateLvl1,
                         NULL, (char *)&mode, sizeof mode)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the power save traffic timeout. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param timeout Timeout value in ms
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetPSTrafficTimeout(uint32_t timeout)
{
   rPowerManagementProperties *power;
   
   REGISTRY_WLOCK();

   power = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   DE_ASSERT(power != NULL);

   power->psTrafficTimeout = timeout;
   REGISTRY_WUNLOCK();
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11PSTrafficTimeout,
                         NULL, (char *)&timeout, sizeof timeout)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the wmm power save period. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param timeout Timeout value in us
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetPSWMMPeriod(uint32_t timeout)
{
   rBasicWiFiProperties*   basic;
   
   REGISTRY_WLOCK();

   basic   = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   basic->wmmPsPeriod = timeout;
   REGISTRY_WUNLOCK();
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11PSWMMPeriod,
                         NULL, (char *)&timeout, sizeof timeout)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}


/*!
 * @brief Set the pspoll power save period. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param timeout Timeout value in ms
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetPSPollPeriod(uint32_t timeout)
{;
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11PSPollPeriod,
                         NULL, (char *)&timeout, sizeof timeout)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}

/*!
 * @brief Set the listen interval, used in power save. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param interval, value in ms
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetMibListenInterval(uint16_t interval)
{
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11ListenInterval,
                         NULL, (char *)&interval, sizeof interval)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}

/*!
 * @brief Set the receive all dtim flag, used in power save. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param all_dtim, set to true if wakeup on all dtim:s
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetReceiveAllDTIM(bool_t all_dtim)
{
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11ReceiveAllDTIM,
                         NULL, (char *)&all_dtim, sizeof all_dtim)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}

/*!
 * @brief Set the use pspoll flag, used in power save. 
 *
 * This is the time after which the target should go to sleep if no
 * traffic has been received.
 *
 * @param use_ps_poll, TRUE if powersave pspoll shall be used 
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetUsePsPoll(bool_t use_ps_poll)
{
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11UsePSPoll,
                         NULL, (char *)&use_ps_poll, sizeof use_ps_poll)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}

   

/*!
 * @brief Sets a flag that enables or disables beacon skipping. 
 *.
 *
 * @param skip (0=disable, 1=enable)
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetDtimBeaconSkipping(uint32_t skip)
{  
   if ( WiFiEngine_SendMIBSet(MIB_dot11dtimBeaconSkipping,
                         NULL, (char *)&skip, sizeof skip)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set timeout for linksupervision.
 *.
 *
 * @param link_to: Timeout in ms. If there is no activity
 *                 on rx/tx during link_to time a null packet
 *                 is sent by firmware to ap.
 *        null_to: Timeout in ms. If link_to times out a null packet
 *                 is sent to ap. If no ack is received within null_to
 *                 a deauthenticate ind is sent by firmware to driver.
 *
 * @return Always WIFI_ENGINE_SUCCESS if successfully sent
 *         otherwise WIFI_ENGINE_FAILURE
 */
int WiFiEngine_LinksupervisionTimeout(uint32_t link_to, uint32_t null_to)
{  
   uint32_t timeouts[2];

   timeouts[0] = link_to*1000;
   timeouts[1] = null_to*1000;  
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11LinksupervisionTimeout,
                         NULL, (char *)&timeouts, sizeof timeouts)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set bitmask for compatibility check. Used against
 * beacon/capability field and capability sent in join request.
 *.
 *
 * @param capability bitmask: either of M80211_CAPABILITY_ESS
 *                                      M80211_CAPABILITY_IBSS
 *                                      M80211_CAPABILITY_APSD
 *
 * @return Always WIFI_ENGINE_SUCCESS if successfully sent
 *         otherwise WIFI_ENGINE_FAILURE
 */
int WiFiEngine_SetCompatibilityMask(uint16_t capability)
{  
   if ( WiFiEngine_SendMIBSet(MIB_dot11compatibilityMask,
                         NULL, (char *)&capability, sizeof capability)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the listen interval for power save
 *
 * This is how often the STA will wake and listen for beacons.
 *
 * @param interval Interval in beacon intervals.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetListenInterval(uint16_t interval)
{
   rPowerManagementProperties *power;
   
   REGISTRY_WLOCK();

   power = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   DE_ASSERT(power != NULL);

   power->listenInterval = interval;
   REGISTRY_WUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the listen interval for power save
 *
 * This is how often the STA will wake and listen for beacons.
 *
 * @param interval Output buffer (interval in beacon intervals).
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetListenInterval(uint16_t *interval)
{
   rPowerManagementProperties *power;
   
   REGISTRY_RLOCK();
   power = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   DE_ASSERT(power != NULL);
   *interval = (uint16_t)power->listenInterval;
   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Delay in ms that will postpone start of ps related to associate success.
 *
 * @param delay (delay in ms).
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetDelayStartOfPs(uint16_t *delay)
{
   rPowerManagementProperties *power;
   
   REGISTRY_RLOCK();
   power = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   DE_ASSERT(power != NULL);
   *delay = (uint16_t)power->psDelayStartOfPs;
   REGISTRY_RUNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Enables/disables reception of all DTIMs.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetReceiveDTIM(unsigned int enabled)
{
   rPowerManagementProperties *power;
   
   REGISTRY_WLOCK();

   power = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   DE_ASSERT(power != NULL);

   power->receiveAll_DTIM = enabled;
   REGISTRY_WUNLOCK(); 

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the flag that indicates if a 20MHz clock is used for SDIO
 *
 * The SDIO clock can be synchronized with RF clock to minimize interference
 *
 * @param enable20MHzClock Output buffer.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetSdioClock(int *enable20MHzClock)
{
   *enable20MHzClock = registry.hostDriver.enable20MHzSdioClock;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the flag that indicates wether the target should be booted or not
 *
 * @param flag Output buffer.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetBootTargetFlag(int *flag)
{
   *flag = registry.hostDriver.automaticFWLoad;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the "delay after reset" timout value
 *
 * @param delay Output buffer for timout value (in milliseconds)
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetDelayAfterReset(unsigned long *delay)
{
   *delay = registry.hostDriver.delayAfterReset;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Enable the link monitoring feature in the device.
 *
 * This means that if the device will generate a deauthentication
 * indication when it has missed a certain number consequtive beacons.
 *
 * @param enable Flag that enables (1) or disables (0) link monitoring.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_EnableLinkSupervision(int enable)
{
   uint8_t flag;
   rBasicWiFiProperties *basic;
   
   flag = (uint8_t)enable;
   
   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.enable = flag;
   REGISTRY_WUNLOCK();
   
   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoring,
                             NULL, (char *)&flag, sizeof(flag))
       != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Minimum count of missed beacons criteria for link monitoring.
 *
 * Sets the minimum number of consequtive beacons that should be
 * missed before link monitoring will generate a deauthentication
 * indication.
 *
 * Link monitoring must be enabled for this function to have an
 * effect. Both the criteria of this function and
 * WiFiEngine_SetLinkSupervisionBeaconTimeout() must be fulfilled
 * before deauthentication.
 *
 * @param beacons Minimum number of missed beacons before firmware
 *        will assume link is terminated. 0 will disable this 
 *        criteria.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetLinkSupervisionBeaconFailCount(unsigned int beacons)
{
   uint32_t xu32;
   rBasicWiFiProperties *basic;
   
   xu32=beacons;
   
   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.beaconFailCount = beacons;
   REGISTRY_WUNLOCK();

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringBeaconCount,
                         NULL, (char *)&xu32, sizeof(xu32))
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Minimum count of missed beacons criteria for link monitoring.
 *
 * Sets the minimum number of consequtive beacons that should be
 * missed before link monitoring will generate a warning
 * indication.
 *
 * Link monitoring must be enabled for this function to have an
 * effect. Both the criteria of this function and
 * WiFiEngine_SetLinkSupervisionBeaconTimeout() must be fulfilled
 * before deauthentication.
 *
 * @param beacons Minimum number of missed beacons before firmware
 *        will assume link is terminated. 0 will disable this 
 *        criteria.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetLinkSupervisionBeaconWarningCount(unsigned int beacons)
{
#if 0 /* has been depricated in fw */
   uint32_t xu32;
   rBasicWiFiProperties *basic;
   
   xu32=beacons;
   
   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.beaconWarningCount = beacons;
   REGISTRY_WUNLOCK();

   DE_TRACE_INT(TR_MIB, "Setting LinkMonitoringBeaconCountWarning to %d\n", xu32);

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringBeaconCountWarning,
                         NULL, (char *)&xu32, sizeof(xu32))
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

#endif
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Minimum time of missed beacons criteria for firmware link
 *        monitoring.
 *
 * Sets the minimum time since last heard beacon that should have
 * elapsed before link monitoring will generate a deauthentication
 * indication. 
 *
 * Link monitoring must be enabled in firmware for this function to
 * have an effect. Both the criteria of this function and
 * WiFiEngine_SetLinkSupervisionBeaconFailCount() must be fulfilled before
 * deauthentication.
 *
 * @param timeout Minimum time in microseconds since last received
 *        beacons before deauthentication. This value is rounded
 *        upwards to the nearest beacon. 0 will disable the timeout
 *        criteria.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetLinkSupervisionBeaconTimeout(unsigned int timeout)
{
   uint32_t xu32;
   rBasicWiFiProperties *basic;

   xu32=timeout;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.beaconTimeout = timeout;
   REGISTRY_WUNLOCK();

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringBeaconTimeout,
                         NULL, (char *)&xu32, sizeof(xu32))
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Configuration of TX fail criteria for link supervision
 *
 * NIC will generate a deauthentication indication when it has failed to
 * transmit a number of frames.
 *
 * @param count Number of consecutive failed attempts to transmit a
 *        frame. No ACK has been received in any of the
 *        attempts to send. Minimum number is 1 attempt. 0 will disable this
 *        feature.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetRegistryTxFailureCount(unsigned int count)
{
   rBasicWiFiProperties *basic;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.TxFailureCount = count;
   REGISTRY_WUNLOCK();

   return  WIFI_ENGINE_SUCCESS;
}
/*!
 * @brief Configuration of TX fail criteria for link supervision
 *
 * NIC will generate a deauthentication indication when it has failed to
 * transmit a number of frames.
 *
 * @param count Number of consecutive failed attempts to transmit a
 *        frame. No ACK has been received in any of the
 *        attempts to send. Minimum number is 1 attempt. 0 will disable this
 *        feature.
 *
 * @return WIFI_ENGINE_SUCCESS on success,
 *         WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetLinkSupervisionTxFailureCount(unsigned int count)
{
   uint32_t xu32;
   rBasicWiFiProperties *basic;

   xu32=count;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.TxFailureCount = count;
   REGISTRY_WUNLOCK();

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringTxFailureCount,
                             NULL, (char *) &xu32, sizeof(xu32))
       != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

   return  WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set fail limit for link supervision roundtrip.
 *
 * This will test the link by monitoring roundtrip messages. The
 * feature will only be used for a few AP configurations where the
 * link status can not be detected unless a roundtrip message is
 * sent. When enough packets have been transmitted without any reply,
 * the link will be determined faulty.
 *
 * @param roundtrip_fail_limit Should no reply have been received
 *        after this number of transmitted messages, the link is 
 *        determined faulty. 0 will disable the roundtrip feature.
 * 
 * @return
 *  - WIFI_ENGINE_SUCCESS on success,
 *  - WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_SetLinkSupervisionRoundtripCount(unsigned int count)
{
   uint32_t xu32;
   rBasicWiFiProperties *basic;

   xu32=count;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.roundtripCount = count;
   REGISTRY_WUNLOCK();

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringRoundtripCount,
                             NULL, (char *) &xu32, sizeof(xu32))
       != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set silent period for link supervision roundtrip.
 *
 * When an AP is connected, the roundtrip monitoring is initially
 * passive and it is assumed that other traffic (e.g. DHCP) will
 * generate enough statistics to determine the link status. After the
 * passive period, the feature might need to inject its own packets
 * until statistical confidence is high enough.
 *
 * @param silent_intervals Number of intervals to wait before this
 *        feature inject its own packets to the recently connected
 *        AP. Each interval is at least 100 ms and could be
 *        considerably longer in power save mode. Should this be set
 *        to 0, there will be no silent period and, thereby, packets 
 *        are injected immediately after an AP is connected. Should
 *        this be 0xffffffff, the feature will work in passive mode
 *        only and not inject any packets of its own.
 *
 * @return
 *  - WIFI_ENGINE_SUCCESS on success,
 *  - WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_SetLinkSupervisionRoundtripSilent(unsigned int intervals)
{
   uint32_t xu32;
   rBasicWiFiProperties *basic;

   xu32=intervals;

   REGISTRY_WLOCK();
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   basic->linkSupervision.roundtripSilent = intervals;
   REGISTRY_WUNLOCK();

   if (WiFiEngine_SendMIBSet(MIB_dot11LinkMonitoringRoundtripSilent,
                             NULL, (char *) &xu32, sizeof(xu32))
       != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set the msg size alignment for lower-level drivers.
 *
 * The target will pad messages in the RX direction so that they are
 * always at least min_size bytes long and always an even multiple of 
 * size_alignment bytes. This call will configure the device with the
 * new settings if the hardware is present (WiFiEngine_Plug() has been called).
 * Otherwise it will just change driver settings, these will be set in
 * the device when WiFiEngine_Plug() is called.
 * @param min_size Minimum packet size (in bytes). All messages from the device 
 *        will be at least this long.
 * @param size_alignment Message size alignment. All messages will have a size
 *        that's an even multiple of this number.
 * @param rx_hdr_size The desired size of the RX HIC+data header. The HIC+data header
 *        of received data packets will be padded to this size. Use this value
 *        to control the alignment and/or offset of the payload.
 *        If this is 0 then the smallest possible size will be used (no padding).
 * @param tx_hdr_size The desired size of the TX HIC+data header. The HIC+data header
 *        of transmitted data packets will be padded to this size. Use this value
 *        to control the alignment and/or offset of the payload.
 *        If this is 0 then the smallest possible size will be used (no padding).
 * @param host_attention Interrupt policy for the device. Can take values
 *        HIC_CTRL_HOST_ATTENTION_GPIO (GPIO interrupt) or HIC_CTRL_HOST_ATTENTION_SDIO
 *        (SDIO interrupt). If the value is HIC_CTRL_HOST_ATTENTION_GPIO there is two
 *        different GPIO-pins to use for interrupt:
 *
 *         ! fpga version ! chip type     ! GPIO     ! host attention  
 *         -------------------------------------------------------------
 *         !   any        ! chip on board ! default  !  0x00
 *         -------------------------------------------------------------
 *         !  < R4B *)    ! module        ! default  !  0x00
 *         -------------------------------------------------------------
 *         !   R4>=B      ! module        ! ext GPIO ! ext gpio1 = 0x0E
 *         -------------------------------------------------------------
 *         !   R5>=A      ! chip on board ! SDIO DAT2! 0x62
 *         -------------------------------------------------------------
 *         *) Always zero in older fpga
 * @param byte_swap_mode Enable or disable byte swapping. To use 16 bit SPI transfer swapping must be 
 *        enabled.possible values are:
 *        HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP    (0x00) disables byte swapping
 *        HIC_CTRL_ALIGN_SWAP_16BIT_BYTESWAP (0x00) enables byte swapping
 * @param host_wakeup Set host wakeup interrupt pin, set to 0xFF to disable host wakeup
 * @param force_interval Force an minimum interval between HIC messages from target [unit 1/10th msec]
 *                                                 
 * @return 
 * - WIFI_ENGINE_SUCCESS
 * - WIFI_ENGINE_FAILURE_INVALID_DATA if the rx_hdr_size was too small.
 */
int WiFiEngine_SetMsgSizeAlignment(uint16_t min_size, 
                                   uint16_t size_alignment,
                                   uint8_t rx_hdr_size,
                                   uint8_t tx_hdr_size,
                                   uint8_t host_attention,
                                   uint8_t byte_swap_mode,
                                   uint8_t host_wakeup,
                                   uint8_t force_interval
                                   )
{
   uint8_t min_hdr_size_rx;
   uint8_t min_hdr_size_tx;

   DE_ASSERT(min_size >= 13);

   /* Make sure that the size alignemnt is a power-of-two */
   DE_ASSERT((wifiEngineState.config.pdu_size_alignment &
              (wifiEngineState.config.pdu_size_alignment - 1)) == 0);
   
   wifiEngineState.config.min_pdu_size       = min_size;
   wifiEngineState.config.pdu_size_alignment = size_alignment;
   wifiEngineState.config.byte_swap_mode     = byte_swap_mode;
   wifiEngineState.config.host_wakeup        = host_wakeup;
   wifiEngineState.config.force_interval     = force_interval;
   wifiEngineState.config.tx_window_size     = 0xFF; // This will be set in 'WiFiEngine_CommitPDUSizeAlignment'
   if(host_attention == HIC_CTRL_HOST_ATTENTION_SDIO)
   {
      wifiEngineState.config.host_attention = host_attention;
   }
   else
   {

      /* Module has radio a disturbanc caused by GPIO-pin
         used for interrupt => use another gpio pin if version
         of fpga is R4B or later */
      if((wifiEngineState.chip_type == WE_CHIP_TYPE_MODULE) &&
         (wifiEngineState.fpga_version[1] == '4') &&
         (wifiEngineState.fpga_version[2] >= 'B'))
      {
         /* If using module and fpga has a revision string 
            equal to or greater than R4B use extended GPIO1 
            pin for interrupt */
         wifiEngineState.config.host_attention = HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO;
         wifiEngineState.config.host_attention |= HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM;
         wifiEngineState.config.host_attention |= HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_EXT; 
         wifiEngineState.config.host_attention |= (1 << HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID);
      }
      else if(wifiEngineState.chip_type == WE_USE_HTOL)
      {
         wifiEngineState.config.host_attention = HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO;
      }      
      else if((wifiEngineState.fpga_version[1] == '8') &&
              (wifiEngineState.fpga_version[2] >= 'A'))
      {
         /* Use DAT1 as interrupt */
         wifiEngineState.config.host_attention = 0x5A;
      }
      else if((wifiEngineState.fpga_version[1] == '9') &&
              (wifiEngineState.fpga_version[2] >= 'A'))
      {
         /* Use native sdio interrupt */
         wifiEngineState.config.host_attention = HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO;
      }      
      else
      {
         /* Use default GPIO-pin use normal pin for interrupt */ 
         wifiEngineState.config.host_attention = host_attention;         
      }
   }
   /* Data rx hdr size includes HIC header */
   min_hdr_size_rx = wei_get_data_req_hdr_size() + wei_get_hic_hdr_min_size();
   if (0 == rx_hdr_size)
   {
      rx_hdr_size = min_hdr_size_rx;
   }
   if (rx_hdr_size < min_hdr_size_rx)
   {
      DE_TRACE_INT2(TR_INITIALIZE, "Bad rx_hic_hdr_size %d, need %d\n", rx_hdr_size, min_hdr_size_rx);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   wifiEngineState.config.rx_hic_hdr_size = rx_hdr_size - wei_get_data_req_hdr_size();

   min_hdr_size_tx = (uint8_t) wei_get_hic_hdr_min_size() + wei_get_data_req_hdr_size();
   if (0 == tx_hdr_size)
   {
      tx_hdr_size = min_hdr_size_tx;
   }
   if (tx_hdr_size < min_hdr_size_tx)
   {
      DE_TRACE_INT2(TR_INITIALIZE, "Bad tx_hic_hdr_size %d, need %d\n", tx_hdr_size, min_hdr_size_tx);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   wifiEngineState.config.tx_hic_hdr_size = tx_hdr_size - wei_get_data_req_hdr_size();
   if (WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
   {
      WiFiEngine_CommitPDUSizeAlignment();
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Send down the configured alignment settings to the device.
 * After this call all messages indicated by the device will
 * be padded accordingly.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS, on success
 * - WIFI_ENGINE_FAILURE, otherwise
 */
int WiFiEngine_CommitPDUSizeAlignment(void)
{
   hic_message_context_t     msg_ref;
   int status;

   Mlme_CreateMessageContext(msg_ref);

   wifiEngineState.config.tx_window_size = wifiEngineState.txPktWindowMax;

   if (Mlme_CreateSetAlignmentReq(&msg_ref, wifiEngineState.config.min_pdu_size, 
                                  wifiEngineState.config.pdu_size_alignment,
                                  wifiEngineState.config.rx_hic_hdr_size,
                                  0, /* trans_id */
                                  wifiEngineState.config.host_attention,
                                  wifiEngineState.config.byte_swap_mode,
                                  wifiEngineState.config.host_wakeup,
                                  wifiEngineState.config.force_interval,
                                  wifiEngineState.config.tx_window_size))
   {
      DE_TRACE6(TR_WEI,"Sending SetAlignmentReq: min_size %d, size_alignment %d, rx_size %d, att %d, swap %d\n",
                wifiEngineState.config.min_pdu_size, 
                wifiEngineState.config.pdu_size_alignment,
                wifiEngineState.config.rx_hic_hdr_size,
                wifiEngineState.config.host_attention,
                wifiEngineState.config.byte_swap_mode);
      DE_TRACE4(TR_WEI,"Sending SetAlignmentReq: host_wakeup %d, force_interval %d, tx_window_size %d\n",
                wifiEngineState.config.host_wakeup,
                wifiEngineState.config.force_interval,
                wifiEngineState.config.tx_window_size);
      status = wei_send_cmd(&msg_ref);
      Mlme_ReleaseMessageContext(msg_ref);
      if (status != WIFI_ENGINE_SUCCESS) 
      { 
         return WIFI_ENGINE_FAILURE; 
      } 
   }
   else
   {
      DE_TRACE_STATIC(TR_WEI, "Failed to create alignment request\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Send down the configured alignment settings to the device
 * with completion callback.
 *
 * The callback will be passed the result byte from the
 * set-alignment confirm message.
 *
 * After this call all messages indicated by the device will
 * be padded accordingly.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS, on success
 * - WIFI_ENGINE_FAILURE, otherwise
 */
int WiFiEngine_CommitPDUSizeAlignmentAsynch(we_cb_container_t *cbc)
{
#if 0
   hic_message_context_t     msg_ref;
   int status;

   Mlme_CreateMessageContext(msg_ref);

   if (Mlme_CreateSetAlignmentReq(&msg_ref, wifiEngineState.config.min_pdu_size, 
                                  wifiEngineState.config.pdu_size_alignment,
                                  0, /* trans_id */
                                  HIC_CTRL_HOST_ATTENTION_SDIO))
   {
      DE_TRACE("Sending SetAlignmentReq: min_size %d, size_alignment %d\n",
               wifiEngineState.config.min_pdu_size, wifiEngineState.config.pdu_size_alignment);
      status = wei_send_cmd(&msg_ref);
      Mlme_ReleaseMessageContext(msg_ref);
      if (status != WIFI_ENGINE_SUCCESS) 
      { 
         return WIFI_ENGINE_FAILURE; 
      } 
   }
   else
   {
      DE_TRACE("Failed to create alignment request\n");
      Mlme_ReleaseMessageContext(msg_ref);
   }
#endif   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Get a release info string for the driver.
 *
 * @param dst Output buffer. The string is NUL-terminated.
 * @param len Size of the output buffer. 
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if dst was too short.
 */
int WiFiEngine_GetReleaseInfo(char *dst, size_t len)
{
   if(dst == NULL || len == 0) {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   if(len > sizeof(release_string))
      len = sizeof(release_string);

   DE_STRNCPY(dst, release_string, len);
   if(dst[len-1] != '\0') {
      dst[len-1] = '\0';
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Get a release info string for the firmware.
 *
 * @param dst Output buffer. The string is NUL-terminated.
 * @param len Size of the output buffer. 
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if dst was too short.
 */
int WiFiEngine_GetFirmwareReleaseInfo(char *dst, size_t len)
{
   if(dst == NULL || len == 0) {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   if(len > sizeof(wifiEngineState.x_mac_version))
      len = sizeof(wifiEngineState.x_mac_version);

   DE_STRNCPY(dst, wifiEngineState.x_mac_version, len);
   if(dst[len-1] != '\0') {
      dst[len-1] = '\0';
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   return WIFI_ENGINE_SUCCESS;
}

int WiFiEngine_GetReadHistory(char *outbuf, int offset, size_t *outlen)
{
  return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
}

/*!
 * @brief Enable/disable multi-domain capability (802.11d)
 *
 * @param mode Multi-domain capability enable flag. 0 to disable, 1 to enable.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetMultiDomainCapability(uint8_t mode)
{
   rBasicWiFiProperties *basic;
   
   REGISTRY_WLOCK();

   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   basic->multiDomainCapabilityEnabled = mode;
   REGISTRY_WUNLOCK();
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11MultiDomainCapabilityEnabled,
                         NULL, (char *)&mode, sizeof mode)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Enable/disable background scan

 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_SetBackgroundScanMode(uint8_t mode)
{
   if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanEnabled,
                         NULL, (char *)&mode, sizeof mode)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;

}

/*!
 * Set background scan parameters.
 * @param scan_period Interval between scans in kusec.
 * @param probe_delay Time (in kusec) to wait on a channel before sending a probe request.
 * @param min_channel_time Time (in kusec) to wait for a probe response on a channel.
 * @param max_channel_time Time (in kusec) to stay on a channel after receiving a probe
 *                         response, waiting for more responses.
 * @param channel_list List of channels to scan.
 * @param channel_list_len Number of channels in channel_list.
 * @param ssid Pointer to a SSID for which to scan. This can be NULL, in which case
 *             the background scan will only probe for the SSID with which we're
 *             currently associated.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success. 
 * - WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_ConfigureBackgroundScan(uint16_t scan_period, uint16_t probe_delay,
                                       uint16_t min_channel_time, uint16_t max_channel_time,
                                       uint8_t *channel_list, int channel_list_len, 
                                       m80211_ie_ssid_t *ssid)
{
   uint32_t                flag;
   
   if (WiFiEngine_SendMIBSet(MIB_dot11backgroundScanPeriod,
                             NULL, (char *)&scan_period, 
                             sizeof scan_period)
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanProbeDelay,
                              NULL, (char *)&probe_delay, 
                              sizeof probe_delay)
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanMinChannelTime,
                              NULL, (char *)&min_channel_time, 
                              sizeof min_channel_time)
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanMaxChannelTime,
                              NULL, (char *)&max_channel_time, 
                              sizeof max_channel_time)
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanChannelList,
                              NULL, (char *)channel_list, channel_list_len)
        != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if (ssid)
   {
      if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanAlternateSSID,
                                 NULL, (char *)ssid->ssid, ssid->hdr.len)
           != WIFI_ENGINE_SUCCESS)
      {
         return WIFI_ENGINE_FAILURE;
      }
      flag = 1;
      if ( WiFiEngine_SendMIBSet(MIB_dot11backgroundScanUseAlternateSSID,
                                 NULL, (char *)&flag, sizeof flag)
           != WIFI_ENGINE_SUCCESS)
      {
         return WIFI_ENGINE_FAILURE;
      }

   }
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Get the information elements from a network.
 *
 * If both the ssid and bssid parameters are NULL the
 * associated net will be assumed (if present).
 *
 * @param dst Output buffer.
 * @param len IN : Pointer to the length of the output buffer. OUT : 
 *  bytes used of the output buffer, or the length needed if
 *  the buffer was too short (returns WIFI_ENGINE_FAILURE_INVALID_LENGTH).
 * @param ssid The SSID for the network. When this parameter is NULL 
 *  the network will be selected only on BSSID.
 * @param bssid The BSSID for the network. When this parameter is
 *  NULL the network will be selected only on SSID.
 * @return
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input buffer was too small
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no matching net was found
 */
int WiFiEngine_GetNetIEs(char *dst, size_t *len, m80211_ie_ssid_t *ssid, 
                            m80211_mac_addr_t *bssid)
{
   WiFiEngine_net_t *net = NULL;
   size_t outlen;

   if (NULL == ssid && NULL == bssid)
   {
      net = wei_netlist_get_current_net();
      if (NULL == net)
      {
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
      }
   }
   if (bssid)
   {
      net = wei_netlist_get_net_by_bssid(*bssid);
      if (NULL == net)
      {
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
      }
      if (ssid && DE_MEMCMP(ssid, &net->bss_p->bss_description_p->ie.ssid, sizeof *ssid) != 0)
      {
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
      }
   }   
   if (NULL == net && ssid)
   {
      net = wei_netlist_get_net_by_ssid(*ssid);
   }
   if (net)
   {
      WiFiEngine_PackIEs(dst, *len, &outlen, net->bss_p->bss_description_p);
      if (outlen > *len)
      {
         *len = outlen;
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
      *len = outlen;
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}


/*!
 * @brief Configure UDP broadcast filtering in firmware.
 *
 * If enabled the firmware will filter out undesired broadcast UDP traffic.
 *
 * @param [in] enable Bitmask to enable or disable a protocol.
 *
 * @return This function can return anything WiFiEngine_SendMIBSet can
 * return.
 */
int WiFiEngine_ConfigUDPBroadcastFilter(uint32_t bitmask)
{
   return WiFiEngine_SendMIBSet(MIB_dot11DHCPBroadcastFilter,
                                NULL, (char *)&bitmask, sizeof(bitmask));
}

/*!
 * @brief Enable/disable Bluetooth coexistence.
 * 
 * @param enable True enables and false disables BT co-existence.
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_EnableBTCoex(bool_t enable)
{
   rBasicWiFiProperties *basic;
   uint8_t en8 = enable;
   
   REGISTRY_WLOCK();

   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   basic->enableBTCoex = enable;
   REGISTRY_WUNLOCK();
   
   if ( WiFiEngine_SendMIBSet(MIB_dot11btCoexEnabled,
                         NULL, (char *)&en8, sizeof en8)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}


/*!
 * @brief Configures BT coexistence 
 *
 * @param bt_vendor Vendor of Bluetooth hardware
 *                   - 0x00 = CSR, 
 *                   - 0x01 = Broadcom, 
 *                   - 0x02 = STMicroelectronics, 
 *                   - 0x03-0xFF = RESERVED
 * @param pta_mode  Vendor specific definition of PTA interface. 
 *                  Settings CSR:
 *                   - 0x00 = 2-wire scheme
 *                   - 0x01 = 3-wire scheme
 *                   - 0x02 = Enhanced 3-wire
 *                  Settings Broadcom:
 *                   - 0x00 = 3-wire scheme
 *                  Settings STMicroelectronics:
 *                   - 0x00 = 3-wire scheme
 *                   - 0x01 = 4-wire scheme
 * @param pta_def  Vector with max five bytes, where for each byte bit 0..3 in each 
 *                  nibble specifies gpio_coex_pin id, and bit 4 defines active logic 
 *                  level. For each PTA interface wire from Bluetooth, specify which 
 *                  gpio_coex_pin id (0 to 4) the wire is connected to and its 
 *                  active logic level (0="Low", 1="High").
 *                  For CSR 2-wire scheme specify wires in the following order:
 *                   - BT_Priority
 *                   - WLAN_Active
 *                  For CSR 3-wire scheme specify wires in the following order:
 *                   - BT_Active
 *                   - WLAN_Active
 *                   - BT_Priority
 *                  For CSR Enhanced 3-wire scheme specify wires in the following order:
 *                   - RF_Active
 *                   - WLAN_Active
 *                   - BT_State
 *                  For Broadcom 3-wire scheme specify wires in the following order:
 *                   - BT_Activity
 *                   - WLAN_Activity
 *                   - BT_Priority and Status
 *                  For ST 3-wire scheme specify wires in the following order:
 *                   - BT_Activity
 *                   - WLAN_Activity
 *                   - BT_Priority and Status
 *                  For STMicroelectronics 3-wire scheme specify wires in the following order:
 *                   - RF_Request
 *                   - RF_Confirm
 *                   - Status
 *                  For STMicroelectronics 4-wire scheme specify wires in the following order:
 *                   - RF_Request
 *                   - RF_Confirm
 *                   - Status
 *                   - Freq
 * @param len Length of pta_def vector.
 * @param antenna_dual  0=Single, 1=Dual. 
 * @param antenna_sel0  0=Don't use AntSel0, 1=Use AntSel0.
 * @param antenna_sel1  0=Don't use AntSel1, 1=Use AntSel1.
 * @param antenna_level0 Logical level for AntSel0 in position BT, 0="Low", 1="High". 
 * @param antenna_level1 Logical level for AntSel1 in position BT, 0="Low", 1="High". 
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_ConfigBTCoex(uint8_t bt_vendor,  
                            uint8_t pta_mode, 
                            uint8_t *pta_def, 
                            int len,
                            uint8_t antenna_dual,
                            uint8_t antenna_sel0,
                            uint8_t antenna_sel1,
                            uint8_t antenna_level0,
                            uint8_t antenna_level1)
{
   int i;

   struct bt_coex_config_t {    /* Perhaps this will get its own .h file someday */
      uint8_t  bt_vendor;
      uint8_t  pta_mode;
      uint8_t  pta_def[5];
      uint8_t  antenna_ctrl;
   } conf;

   /* Sanity check */
   if (len > 5)
      return WIFI_ENGINE_FAILURE;
   for (i = 0; i < len; i++)
      if ((pta_def[i] & 0x0f) > 4)
         return WIFI_ENGINE_FAILURE;

   /* Copy to struct */
   conf.bt_vendor = bt_vendor;
   conf.pta_mode = pta_mode;
   DE_MEMSET(conf.pta_def, 0x0, sizeof(conf.pta_def));
   for (i = 0; i < len; i++) {
      conf.pta_def[i]  = pta_def[i];
   }
   conf.antenna_ctrl = 0;
   if (antenna_dual)
      conf.antenna_ctrl |= (0x01<<0);
   if (antenna_sel0)
      conf.antenna_ctrl |= (0x01<<1);
   if (antenna_sel1)
      conf.antenna_ctrl |= (0x01<<2);
   if (antenna_level0)
      conf.antenna_ctrl |= (0x01<<3);
   if (antenna_level1)
      conf.antenna_ctrl |= (0x01<<4);

   if ( WiFiEngine_SendMIBSet(MIB_dot11btCoexConfig,
                              NULL, (char *)&conf, sizeof conf)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set antenna diversity mode.
 *
 * @param antenna_mode Diversity mode: 
 *        0: use default setting (currently antenna #1)
 *        1: use antenna #1
 *        2: use antenna #2
 *        3: use antenna diversity, i.e. both antenna #1 and #2
 * @param rssi_threshold RSSI threshold used by antenna selection al
 *        gorithm. Only used in mode 3.
 *
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_SetAntennaDiversityMode(uint32_t antenna_mode, int32_t rssi_threshold)
{
   if (antenna_mode < 1 || antenna_mode > 3)    /* Sanity check */
      return WIFI_ENGINE_FAILURE;

   if ( WiFiEngine_SendMIBSet(MIB_dot11diversityAntennaMask, 
                              NULL, (char *)&antenna_mode, sizeof antenna_mode)
        != WIFI_ENGINE_SUCCESS)
      return WIFI_ENGINE_FAILURE;

   if (antenna_mode == 3) 
      if ( WiFiEngine_SendMIBSet(MIB_dot11diversityRssiThreshold, 
                                 NULL, (char *)&rssi_threshold, sizeof rssi_threshold)
           != WIFI_ENGINE_SUCCESS) 
         return WIFI_ENGINE_FAILURE;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Check QoS association status.
 *
 * @retval TRUE if the association supports QoS frames
 * @retval FALSE if the association doesn't support QoS frames, or if
 * unassociated
 *
 * @note Despite the name, this function returning TRUE does not imply
 * that we're associated with WMM.
 */
int WiFiEngine_IsAssocWMM(void)
{
   return WES_TEST_FLAG(WES_FLAG_QOS_ASSOC);
}

/*!@
 * @brief Set the TX packet window size.
 * 
 * The TX pkt window controls how many data packets may be simultaneously
 * outstanding to the device. When win_size number of packets have been
 * transmitted to the device no further packets will be transmitted until
 * a data confirm has been received.
 *
 * @param win_size The size (in packets) of the TX packet window.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_DATA if the window size was invalid.
 */
int WiFiEngine_SetTxPktWindow(uint8_t win_size)
{
   if (win_size > WIFI_ENGINE_MAX_PENDING_REPLIES || win_size == 0)
   {
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   WIFI_LOCK();
   wifiEngineState.txPktWindowMax = win_size;
   WIFI_UNLOCK();
   return WIFI_ENGINE_SUCCESS;
}

/*!@
 * @brief Check TX packet window status.
 * 
 * Check if the tx window is full or not.
 *
 * @return 
 * - TRUE if full
 * - FALSE if not full
 */
int WiFiEngine_TxPktWindowIsFull(void)
{
   return (wifiEngineState.dataReqPending >= wifiEngineState.txPktWindowMax);
}

/*!
 * @brief Enable driver-driven link monitoring.
 *
 * @param miss_thres The miss threshold in percent. Range 1-100.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_EnableLinkMonitoring() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly. Set the repeating parameter
 *            in the cbc if the callback should be executed on every
 *            matching IND, rather than just the first.
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE on failure.
 */

int WiFiEngine_EnableLinkMonitoring(int32_t miss_thres,
                                    we_cb_container_t *cbc)
{
   int32_t trans_id;

   if (WiFiEngine_RegisterMIBTrigger(&trans_id,
                                 MIB_dot11beaconLossRate,
                                 (uint32_t)-1,
                                 2000000,
                                 miss_thres,
                                 MISSEDBEACON_RISING,
                                 1,
                                 CONT,
                                 cbc)
       != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_NOISE, "Failed to create MIB Trigger\n");
      return WIFI_ENGINE_FAILURE;
   }
                                 

   return WIFI_ENGINE_SUCCESS;
}

#ifdef USE_IF_REINIT
/*! 
 * @brief Quiesce the driver.
 *
 * The driver will disable activities asynchronous activities that it
 * can control itself. This includes internal timers, PS traffic timeouts
 * and heartbeat.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Quiesce(void)
{
   DriverEnvironment_DisableTimers();
   
   return WIFI_ENGINE_SUCCESS;
}
#endif

#ifdef USE_IF_REINIT
/*!
 * @brief Renable those activities that were disabled by WiFiEngine_Quiesce().
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_UnQuiesce(void)
{
   DriverEnvironment_EnableTimers();

   return WIFI_ENGINE_SUCCESS;
}
#endif

/* FIXME: move to wifistate */
#ifdef USE_IF_REINIT
struct iobject* inactivity_h = NULL;
/*!
 * @brief Install a handler callback for driver inactivity.
 * @param cb Callback to be executed as the handler.
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_RegisterInactivityCallback(i_func_t cb)
{
   DE_TRACE_PTR(TR_NOISE, "Registering inactivity callback %p\n", cb);

   inactivity_h = we_ind_register(
         WE_IND_ACTIVITY_TIMEOUT, 
         "WE_IND_ACTIVITY_TIMEOUT", 
         cb, 
         NULL,
         RELEASE_IND_ON_UNPLUG,
         NULL);

   if (inactivity_h)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}
#endif

#ifdef USE_IF_REINIT
/*!
 * @brief Remove a installed handler callback for driver inactivity.
 * @return
 * - WIFI_ENGINE_SUCCESS.
 */
int WiFiEngine_DeregisterInactivityCallback(void)
{
   we_ind_deregister_null(&inactivity_h);
   return WIFI_ENGINE_SUCCESS;
}
#endif

#ifdef USE_IF_REINIT
/*!
 * @brief Detect driver inactivity.
 *
 */
#ifndef UNREF
#define UNREF(x) (x = x)
#endif
static int inactivity_detect_cb(void *data, size_t len)
{
   UNREF(data);
   UNREF(len);
   if ( DriverEnvironment_GetTicks() - wifiEngineState.activity_ts > 
        DriverEnvironment_msec_to_ticks(registry.network.basic.activityTimeout) )
   {
      DriverEnvironment_indicate(WE_IND_ACTIVITY_TIMEOUT, NULL, 0);
   }
   return WIFI_ENGINE_SUCCESS;
}
#endif

#ifdef USE_IF_REINIT
/*!
 * @brief Set the activity timeout.
 * 
 * The inactivity event will be triggered when no rx/tx activity has happened
 * for the timeout period.
 *
 * @param timeout The activity timeout in msec. 0 will disable.
 * @param inact_check_interval The interval between timeout checks in msec.
 *        Cannot be 0.
 * @return
 * - WIFI_ENGINE_SUCCESS.
 * - WIFI_ENGINE_FAILURE_INVALID_DATA on bad args.
 */
int WiFiEngine_SetActivityTimeout(uint32_t timeout, uint32_t inact_check_interval)
{
   rBasicWiFiProperties *basic;

   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);

   basic->activityTimeout = timeout;
   DriverEnvironment_CancelTimer(wifiEngineState.inactivity_timer_id);
   if (timeout)
   {
      WE_CHECK(inact_check_interval > 0);
      if (DriverEnvironment_RegisterTimerCallback(inact_check_interval, 
                                                  wifiEngineState.inactivity_timer_id, 
                                                  inactivity_detect_cb, 
                                                  1 ) != 1) 
      {
         DE_TRACE_STATIC(TR_NOISE, "No inactivity callback registered, DE was busy\n");
      }
   }
   else
   {
      DE_TRACE_STATIC(TR_NOISE, "Driver inactivity detection disabled.\n");
   }

   return WIFI_ENGINE_SUCCESS;
}

#endif

void WiFiEngine_Registry_LoadDefault(void)
{
   registry = DefaultRegistry;
}

void WiFiEngine_Registry_Read(char *registry_cache)
{
#if DE_REGISTRY_TYPE == CFG_TEXT
   PersistentStorage_t  registryImage;

   if(registry_cache == NULL)
      return;
   DE_MEMSET(&registry, 0, sizeof registry);
   registryImage.buffer = registry_cache;
   registryImage.ptr = registryImage.buffer;

   Cache_rRegistry(&registryImage, CACHE_ACTION_READ, &registry, "HostDriverRegistry");
#else
   WiFiEngine_Registry_LoadDefault(); /* not ideal, but ~compatible
                                         with older code */
#endif
}

void WiFiEngine_Registry_Write(char* registry_cache)
{
#if DE_REGISTRY_TYPE == CFG_TEXT
   PersistentStorage_t  registryImage;

   registryImage.ptr    = registry_cache;
   Cache_rRegistry(&registryImage, CACHE_ACTION_WRITE, &registry, "HostDriverRegistry");
#else
   DE_STRCPY(registry_cache, "textual registry not supported on this platform\n");
#endif
}

/*!
 * @brief Read the current MAC time.
 *
 * Result in an WE_IND_MAC_TIME indication
 *
 * @return WIFI_ENGINE_SUCCESS on success
 *         WIFI_ENGINE_FAILURE on any error.
 *         
 */
int WiFiEngine_SyncRequest(void)
{
   hic_message_context_t   msg_ref;
   int                status;
   uint32_t trans_id;
   
   BAIL_IF_UNPLUGGED;
   Mlme_CreateMessageContext(msg_ref);

   if (!Mlme_CreateSyncRequest(&msg_ref, (uint32_t *)&trans_id))
   {
      DE_TRACE_STATIC(TR_SEVERE, "SyncRequest failed\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }

   status = wei_send_cmd(&msg_ref);
   Mlme_ReleaseMessageContext(msg_ref);

   DE_ASSERT(status == WIFI_ENGINE_SUCCESS);

   return status;
}

/*!
 * @brief Register a indication callback
 * 
 * The callback will be executed when the defined indication occurs.
 * Each indication can have at most one registered callback, this
 * call will fail if there was a callback already registered for the
 * indication. Note that WiFiEngine may register indication callbacks
 * and if those are removed it may affect the correct functioning 
 * of WiFiEngine.
 *
 * @param type The type of indication to register the callback for.
 * @param cb The callback function.
 * @return
 * - WIFI_ENGINE_SUCCESS if the callback was successfully registered.
 * - WIFI_ENGINE_FAILURE if the indication already had a callback registered.
 */
#if 0
// deprecated by we_ind_register and we_ind_cond_register
int WiFiEngine_RegisterIndHandler(we_indication_t type, 
                                  we_ind_cb_t cb, 
                                  const void *ctx)
{
   if(ctx != NULL) {
      /* temporary restriction */
      return WIFI_ENGINE_FAILURE;
   }
   return DriverEnvironment_Register_Ind_Handler(type, cb, ctx);
}
#endif

/*!
 * @brief Deregister a indication callback
 * 
 * Note that WiFiEngine may register indication callbacks
 * and if those are removed it may affect the correct functioning 
 * of WiFiEngine.
 *
 * @param type The type of indication to deregister the callback for.
 * @return
 * - WIFI_ENGINE_SUCCESS if the callback was successfully registered.
 * - WIFI_ENGINE_FAILURE if the indication already had a callback registered.
 */
#if 0
// deprecated by we_ind_deregister
int WiFiEngine_DeregisterIndHandler(we_indication_t type, 
                                    we_ind_cb_t cb, 
                                    const void *ctx)
{
   if(ctx != NULL) {
      /* temporary restriction */
      return WIFI_ENGINE_FAILURE;
   }
  return DriverEnvironment_Deregister_Ind_Handler(type, cb, ctx);
}
#endif

/*!
 * @brief Check if a indication handler has been registered.
 *
 * @param type The type of indication to check.
 * @param cb The callback function to check for.
 * @return 
 * - WIFI_ENGINE_SUCCESS if the callback is registered on the
 *                       specified indication.
 * - WIFI_ENGINE_FAILURE if the callback was not registered
 *                       with the specified indication.
 */
#if 0
// deprecated: not thread safe
int WiFiEngine_IsIndHandlerRegistered(we_indication_t type,
                                      we_ind_cb_t cb)
{
   if (DriverEnvironment_Is_Ind_Handler_Registered(type, cb)
       == DRIVERENVIRONMENT_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }
   
   return WIFI_ENGINE_FAILURE;
}
#endif

/** @} */ /* End of we_nr group */




/*  LocalWords:  consequtive WIFI
 */
