/* $Id: we_init.c 18622 2011-04-11 08:35:34Z anob $ */
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
This module implements the WiFiEngine initialization interface.

*****************************************************************************/

/** @defgroup we_init WiFiEngine initialization interface
 *
 * @brief The WiFiEngine intialization interface initializes and
 * configures WiFiEngine for operation and release resources
 * prior to driver unload/deactivation.
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
#include "hmg_ps.h"
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
#include "hmg_pal.h"
#endif
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"
#include "we_cm.h"
#include "we_dlm.h"

/*!
 * @brief Initialize WiFiEngine.
 *
 * Note that WiFiEngine_Plug() must be called after this function but
 * before WiFiEngine is ready for use. See WiFiEngine_Plug() for the
 * rationale.
 *
 * @param[in] adapter  Handle to driver specific data, if anything but
 *                     NULL WiFiEngine_RegisterAdapter should not be
 *                     called.
 */
unsigned int WiFiEngine_Initialize(void *adapter)
{
   /* Initialize globals vars (they may not be properly initialized despite the scope) */
   DE_MEMSET(&wifiEngineState, 0, sizeof wifiEngineState);

   WiFiEngine_RegisterAdapter(adapter);

   DriverEnvironment_initialize_lock(&wifiEngineState.lock);
   DriverEnvironment_initialize_lock(&wifiEngineState.resource_hic_lock);    
   DriverEnvironment_initialize_lock(&wifiEngineState.rlock);
   DriverEnvironment_init_trylock(&wifiEngineState.sm_lock);
   DriverEnvironment_init_trylock(&wifiEngineState.send_lock);
   DriverEnvironment_init_trylock(&wifiEngineState.cmd_lock);

   wei_cb_init();
   we_ind_init();
   wei_ps_ctrl_alloc_init();

   WiFiEngine_SetMsgSizeAlignment(13, 0, 0, 0, HIC_CTRL_HOST_ATTENTION_GPIO, HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP, 0xFF, 0xFF);
   
   DriverEnvironment_Startup();
   init_state_trace(TRACE_LOG_SIZE);

   wei_hmg_ps_init();
   wei_hmg_traffic_init();
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
   wei_hmg_pal_init();
#endif
   
   wei_pmkid_init();

   wei_virt_trig_init();
   WiFiEngine_RateMonInit();

   wei_initialize_mib();
   wei_initialize_auth();
   wei_initialize_scan();
   wei_initialize_roam();
   wei_multicast_init();
   if(registry.hostDriver.hmgAutoMode)
   {
      wei_cm_initialize(&wifiEngineState.cm_priv);
   }

   wei_pm_initialize(&wifiEngineState.pm_priv);   
   wei_interface_init();
   wei_data_init();
   wei_arp_filter_init();

   /* depends on: wei_ps_ctrl_alloc_init() */
   we_dlm_initialize();
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
   wei_pal_init();
#endif

   WiFiEngine_sac_init();

   DE_TRACE_STATIC(TR_INITIALIZE, "Done!\n");

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Shutdown WiFiEngine.
 *
 * Cleanup and shut down WiFiEngine.
 */
int WiFiEngine_Shutdown(unsigned int driver_id)
{

   wei_ps_ctrl_shutdown();
   
   wei_interface_shutdown();
   we_dlm_shutdown();

   we_wlp_shutdown();

   wei_arp_filter_shutdown();

   wei_clear_cmd_queue();

   wei_clear_mib_reply_list();
   wei_clear_console_reply_list();
   deinit_state_trace();   
   wifiEngineState.main_state = Halted;

   wei_shutdown_roam();
   wei_cm_shutdown(wifiEngineState.cm_priv);
   wei_pm_shutdown(wifiEngineState.pm_priv);
   wei_netlist_remove_current_net();

   /* Free the net list */
   wei_netlist_free_all_nets();
   wei_asscache_free();

   wei_shutdown_scan();
   wei_shutdown_auth();
   wei_shutdown_mib();
   wei_data_shutdown();
   
   WiFiEngine_RateMonShutdown();

   wei_ind_shutdown();

   /* This must happen last since some components may be using cbc structures. */
   wei_cb_shutdown();

   DriverEnvironment_free_lock(&wifiEngineState.lock);
   DriverEnvironment_free_lock(&wifiEngineState.resource_hic_lock);    
   DriverEnvironment_free_lock(&wifiEngineState.rlock);
   
   DE_TRACE_STATIC(TR_INITIALIZE, "Done!\n");
   
   return DriverEnvironment_Terminate(driver_id);
}

/*!
 * @brief Start WiFiEngine.
 *
 * If the current platform supports having the driver/WiFiEngine
 * loaded while the device hardware is not present then WiFiEngine
 * can be loaded (initialized) but unplugged (not sending to the
 * device). WiFiEngine_Plug() activates WiFiEngine and should be
 * called when the hardware device is present and ready to start
 * processing requests.
 */
int WiFiEngine_Plug()
{
   int i;
   rHostDriverProperties *hp;

#if (DE_DEBUG_MODE & CFG_MEMORY)
   {
      /* Always trace how much dynamic memory the driver have allocated.                 */
      /* If it increases after a deactivate-activate, list the buffers to find the leak. */
      static int last_sum_allocated = 0;
      int        sum_allocated =  malloc_sum();     
      DE_TRACE_INT(TR_INITIALIZE, "Memory allocated by malloc at plug: %d\n", sum_allocated );
      if (sum_allocated > last_sum_allocated)
      {
         malloc_list();
      }
      last_sum_allocated = sum_allocated;
   }
#endif

   WIFI_LOCK();
   wifiEngineState.dataReqPending = 0;
   DE_MEMSET(&wifiEngineState.dataReqByPrio, 0, 
             sizeof(wifiEngineState.dataReqByPrio));
   wifiEngineState.txPktWindowMax = WIFI_ENGINE_MAX_PENDING_REPLIES;
   wifiEngineState.cmdReplyPending = 0;
   wifiEngineState.dataPathState = DATA_PATH_OPENED;
   wifiEngineState.users = 0;
   wifiEngineState.users = RESOURCE_DISABLE_PS;   
   wifiEngineState.flags = 0;
   wifiEngineState.ps_inhibit_state = 0; 
   WES_SET_FLAG(WES_FLAG_HW_PRESENT);
   WES_SET_FLAG(WES_FLAG_8021X_PORT_OPEN);
   wifiEngineState.pkt_cnt = 1;
   wifiEngineState.current_seq_num = 0;
   wifiEngineState.last_seq_num = 0;
   wifiEngineState.frag_thres = 0;
   wifiEngineState.periodic_scan_interval = 0;
   wifiEngineState.ps_data_ind_received = 0;
   DE_MEMSET(&wifiEngineState.key_state, 0, sizeof wifiEngineState.key_state);
   wifiEngineState.core_dump_state = WEI_CORE_DUMP_DISABLED;
#ifdef USE_IF_REINIT   
   WEI_ACTIVITY();
#endif
   WEI_CMD_TX();
   wifiEngineState.forceRestart = 0;
#ifdef USE_NEW_AGE
   wifiEngineState.scan_count = 0;
#endif
   WIFI_UNLOCK();
   
   /* These two queues are declared in wifi_engine_internal.h */
   init_queue(&cmd_queue);
   wei_console_init();
   wei_data_plug();
   wei_ps_plug();
   wei_interface_plug();

   wei_cm_plug(wifiEngineState.cm_priv);

#ifdef ENABLE_STATE_TRACE
   transid_hist_init();
#endif /* ENABLE_STATE_TRACE */
   
   wei_asscache_init();

   i = DriverEnvironment_GetNewTimer(&wifiEngineState.mic_cm_detect_timer_id, FALSE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);
   i = DriverEnvironment_GetNewTimer(&wifiEngineState.mic_cm_assoc_holdoff_timer_id , FALSE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);
   i = DriverEnvironment_GetNewTimer(&wifiEngineState.monitor_traffic_timer_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);

#ifdef USE_IF_REINIT
   i = DriverEnvironment_GetNewTimer(&wifiEngineState.inactivity_timer_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);
#endif
   i = DriverEnvironment_GetNewTimer(&wifiEngineState.cmd_timer_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);
   i = DriverEnvironment_GetNewTimer(&wifiEngineState.ps_traffic_timeout_timer_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);

#if (DE_CCX == CFG_INCLUDED)
   wei_ccx_plug();
#endif
   wifiEngineState.main_state = driverDisconnected;

#if 0
   /* Initialize the state machine */
   if(registry.hostDriver.hmgAutoMode)
   {
      HMG_Set_Traffic_Mode(TRAFFIC_AUTO_CONNECT);
   }
   else
   {
      HMG_Set_Traffic_Mode(TRAFFIC_TRANSPARENT);
   }
#endif
   wei_sm_init();

   hp = (rHostDriverProperties *)Registry_GetProperty(ID_hostDriver);  
   WiFiEngine_SetTxPktWindow(hp->txPktWinSize);

   WiFiEngine_CommitPDUSizeAlignment(); 

#ifdef USE_IF_REINIT
   /* Check once per second. */
   WiFiEngine_SetActivityTimeout(registry.network.basic.activityTimeout, 1000);
#endif 

   wifiEngineState.driver_start_ts = DriverEnvironment_GetTimestamp_msec();

   DE_TRACE_STATIC(TR_INITIALIZE, "Done!\n");

   return WIFI_ENGINE_SUCCESS;
}

#if (DE_BUILTIN_SUPPLICANT == CFG_INCLUDED)
int wpa_init(void);
int wpa_exit(void);
#endif

/*!
 * @brief Setup hardware.
 *
 * Write some registry settings to the device hardware.
 * WiFiEngine_Plug() must be called before calling this function.
 * 
 * @param test_mode TRUE if it's a rf test firmware running, FALSE if
 *                  it's a regular firmware
 *
 * @retval WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_Configure_Device(int test_mode)
{
   rGeneralWiFiProperties* generalWiFiProperties;
   rBasicWiFiProperties*   basic;

   basic   = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   generalWiFiProperties = (rGeneralWiFiProperties*)Registry_GetProperty(ID_general);  

   we_wlp_configure_device(); /* this should come ~first */

   /* Set the MAC address from registry unless it is 0xffffffff */
   if (! wei_is_bssid_bcast(generalWiFiProperties->macAddress))
   {
      WiFiEngine_SendMIBSet(MIB_dot11MACAddress, NULL, 
                            generalWiFiProperties->macAddress.octet, 
                            sizeof(generalWiFiProperties->macAddress.octet));
   }
   else
   {
      wei_get_mib_with_update(MIB_dot11MACAddress,
                              generalWiFiProperties->macAddress.octet,
                              sizeof generalWiFiProperties->macAddress.octet,
                              NULL,
                              WE_IND_NOOP);
   }
   wei_rate_configure();

   if(test_mode) {
      /* let's leave it at this */
      return WIFI_ENGINE_SUCCESS;
   }

#if (DE_AGGREGATE_HI_DATA  == CFG_AGGR_ALL)
   wei_enable_aggregation(AGGR_ALL, DE_HI_MAX_SIZE);
#elif (DE_AGGREGATE_HI_DATA  == CFG_AGGR_SCAN_IND)
   wei_enable_aggregation(AGGR_SCAN_IND, DE_HI_MAX_SIZE);
#elif (DE_AGGREGATE_HI_DATA  == CFG_AGGR_ALL_BUT_DATA)
   wei_enable_aggregation(AGGR_ALL_BUT_DATA, DE_HI_MAX_SIZE);
#elif (DE_AGGREGATE_HI_DATA  == CFG_AGGR_ONLY_DATA_CFM)
   wei_enable_aggregation(AGGR_ONLY_DATA_CFM, DE_HI_MAX_SIZE);
#elif (DE_AGGREGATE_HI_DATA  == CFG_AGGR_LAST_MARK)
   wei_enable_aggregation(AGGR_LAST_MARK, DE_HI_MAX_SIZE);
#endif

   WiFiEngine_EnableLinkSupervision(basic->linkSupervision.enable);
   WiFiEngine_SetLinkSupervisionBeaconFailCount(basic->linkSupervision.beaconFailCount);
   /* 
   TODO: implement this before enabling this
   WiFiEngine_SetLinkSupervisionBeaconWarningCount(basic->linkSupervision.beaconWarningCount);
   */
   WiFiEngine_LinksupervisionTimeout(4000,5);   
   WiFiEngine_SetLinkSupervisionBeaconTimeout(basic->linkSupervision.beaconTimeout);
   WiFiEngine_SetLinkSupervisionTxFailureCount(basic->linkSupervision.TxFailureCount);
   WiFiEngine_SetLinkSupervisionRoundtripCount(basic->linkSupervision.roundtripCount);
   WiFiEngine_SetLinkSupervisionRoundtripSilent(basic->linkSupervision.roundtripSilent);
   WiFiEngine_SetPSTrafficTimeout(registry.powerManagement.psTrafficTimeout);
   WiFiEngine_SetPSWMMPeriod(basic->wmmPsPeriod);   
   WiFiEngine_SetLvl1AdaptiveTxRate(basic->txRatePowerControl);
   if (basic->multiDomainCapabilityEnforced)
   {
      basic->multiDomainCapabilityEnabled = 1;
   }
   WiFiEngine_SetMultiDomainCapability(basic->multiDomainCapabilityEnabled);

   WiFiEngine_ConfigUDPBroadcastFilter(basic->DHCPBroadcastFilter);

   WiFiEngine_ReregisterAllVirtualTriggers();

   WiFiEngine_ActivateRegionChannels();

   wei_reconfigure_roam();
   wei_reconfigure_mib();
   wei_reconfigure_auth();
   we_lqm_configure_lqm_job();
   wei_multicast_update();
   
   wei_arp_filter_configure();

   WiFiEngine_EnableBTCoex((bool_t)basic->enableBTCoex);
   wei_get_mib_with_update(MIB_dot11manufacturerProductVersion, 
                           wifiEngineState.x_mac_version, 
                           sizeof(wifiEngineState.x_mac_version),
                           NULL,
                           WE_IND_NOOP);
   DE_MEMSET(&wifiEngineState.fw_capabilities, 0, 
          sizeof(wifiEngineState.fw_capabilities));
   wei_get_mib_with_update(MIB_dot11firmwareCapabilites, 
                           &wifiEngineState.fw_capabilities, 
                           sizeof wifiEngineState.fw_capabilities,
                           NULL,
                           WE_IND_NOOP);
   if(registry.network.basic.cmdTimeout)
   {
      WiFiEngine_CommandTimeoutStart();
   }

   /* *************************************************************/
   /* Init complete must be called before starting any scan jobs! */
   /* *************************************************************/
   WiFiEngine_Init_Complete();

   wei_reconfigure_scan();
   if(basic->defaultScanJobDisposition)
   {
      int status;
      
      /* Start default scan job */
      status = WiFiEngine_SetScanJobState(0, 1, NULL);   
      if(status != WIFI_ENGINE_SUCCESS)
         DE_TRACE_INT(TR_INITIALIZE, "WiFiEngine_SetScanJobState=%d\n",  status);      
   }  
   wifiEngineState.periodic_scan_interval = basic->connectionPolicy.disconnectedScanInterval;
   
#if (DE_BUILTIN_SUPPLICANT == CFG_INCLUDED)
   wpa_init();
#endif

   WES_SET_FLAG(WES_DEVICE_CONFIGURED);

   wei_sm_queue_sig(INTSIG_POWER_UP, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
   wei_sm_queue_sig(INTSIG_POWER_UP, SYSDEF_OBJID_HOST_MANAGER_PS,      DUMMY, FALSE);
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
   wei_sm_queue_sig(INTSIG_POWER_UP, SYSDEF_OBJID_HOST_MANAGER_PAL,     DUMMY, FALSE);
#endif
   wei_sm_execute();
   
#ifdef CONNECT_ON_PROP_SET
   if (basic->desiredSSID.hdr.id == M80211_IE_ID_SSID) {
      int status;

      DE_TRACE_STATIC(TR_INITIALIZE, "Desired SSID set in the registry");
      status = WiFiEngine_sac_start();
      if (status != WIFI_ENGINE_SUCCESS)
         DE_TRACE_INT(TR_INITIALIZE, "WiFiEngine_sac_start=%d\n",  status);      
   }
#endif

   DE_TRACE_STATIC(TR_INITIALIZE, "Done!\n");
   
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Reset WiFi_engine. 
 *
 * This will reset the state machine to the PowerOff state.  This
 * function should be called if the hardware is removed but the
 * driver/WiFiEngine remains loaded.
 */
int WiFiEngine_Unplug() 
{
   if (! WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
   {
      DE_TRACE_STATIC(TR_INITIALIZE, "No HW present!\n");
      return WIFI_ENGINE_SUCCESS;
   }
   WIFI_LOCK();
   WES_CLEAR_FLAG(WES_FLAG_HW_PRESENT);
   WIFI_UNLOCK();

#if (DE_BUILTIN_SUPPLICANT == CFG_INCLUDED)
   /* there is an asymmetry here, we init wpa supplicant in
      configure_device, but there is not un-configure_device */
   if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
   {
      wpa_exit();
   }
#endif

   /* Reset state machine */
   HMG_Unplug_ps();
   HMG_Unplug_traffic();

#if (DE_ENABLE_CM_SCAN == CFG_ON)
   /* XXX this should be handled with generic unplug mechanism */
   wei_cm_scan_unplug();
#endif
   wei_cm_unplug(wifiEngineState.cm_priv);
   wei_interface_unplug();
   wei_data_unplug();
   wei_ps_unplug();   

   /* Drain signals */
   wei_sm_drain_sig_q();

   wei_clear_cmd_queue();
   wei_clear_mib_reply_list();
   wei_clear_console_reply_list();

   wei_asscache_free();

   wei_arp_filter_unplug();
   wei_ind_unplug();

   /*  Free timers */
   DriverEnvironment_FreeTimer(wifiEngineState.mic_cm_detect_timer_id);
   DriverEnvironment_FreeTimer(wifiEngineState.mic_cm_assoc_holdoff_timer_id);
   DriverEnvironment_FreeTimer(wifiEngineState.monitor_traffic_timer_id);

#ifdef USE_IF_REINIT   
   DriverEnvironment_FreeTimer(wifiEngineState.inactivity_timer_id);
#endif
   DriverEnvironment_FreeTimer(wifiEngineState.cmd_timer_id);   
   DriverEnvironment_FreeTimer(wifiEngineState.ps_traffic_timeout_timer_id);  

#if (DE_CCX == CFG_INCLUDED)
   wei_ccx_unplug();
#endif
   /*  Free dynamic members in wifiEngineState */
   WrapperFreeStructure(wifiEngineState.active_channels_ref);
   wifiEngineState.active_channels_ref = NULL;

   WES_CLEAR_FLAG(WES_DEVICE_CONFIGURED);

   /* some stuff from WiFiEngine_Reinitialize */
   wei_netlist_free_all_nets();
   WiFiEngine_RateMonInit(); 
   wei_pmkid_unplug();
   wei_shutdown_roam();
   wei_shutdown_scan();
   wei_shutdown_auth();
   wei_unplug_mib();   
   wei_initialize_mib();
   wei_initialize_auth();
   wei_initialize_scan(); 
   wei_virt_trig_unplug();

   /* This must happen last since some components may be using cbc structures. */
   wei_cb_unplug();

#if (DE_DEBUG_MODE & CFG_MEMORY)
   {
      /* Always trace how much dynamic memory the driver have allocated.                 */
      /* If it increases after a activate-deactivate, list the buffers to find the leak. */
      static int last_sum_allocated = 0;
      int        sum_allocated =  malloc_sum();     
      DE_TRACE_INT(TR_INITIALIZE, "Memory allocated by malloc at unplug: %d\n", sum_allocated );
      if (sum_allocated > last_sum_allocated)
      {
         malloc_list();
      }
      last_sum_allocated = sum_allocated;
   }
#endif

   DE_TRACE_STATIC(TR_INITIALIZE, "Done!\n");

   return WIFI_ENGINE_SUCCESS;
}

/** @} */ /* End of we_init group */
