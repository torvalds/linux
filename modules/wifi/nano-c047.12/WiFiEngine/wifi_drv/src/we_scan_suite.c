
#include "driverenv.h"

#include "we_scan_suite.h"

int WiFiEngine_CreateScanSuite(struct scan_suite_t **suite)
{
   scan_suite_s *ss;
   int s;

   if(!suite)
      return WIFI_ENGINE_FAILURE_INVALID_DATA;

   if(*suite)
   {
      ss = *suite;
   } else {
      ss = (scan_suite_s*)DriverEnvironment_Malloc(sizeof(scan_suite_s));
      if(!ss)
         return WIFI_ENGINE_FAILURE_RESOURCES;
      *suite = ss;
   }

   DE_MEMSET(ss,0,sizeof(scan_suite_s));
   DE_MEMSET(&ss->scan_job.bssid,0xff,sizeof(ss->scan_job.bssid));
   ss->scan_job.type = ACTIVE_SCAN;
   ss->scan_job.filter_id = -1; // no filter
   ss->scan_job.run_every_nth_period = 1; // every period
   ss->scan_job.ap_exclude = 0; // all ap's
   ss->scan_job.prio = 16; // some priority; higher is more
   ss->scan_job.flags = ANY_MODE;

   s = WiFiEngine_GetRegionalChannels(&ss->scan_job.ch_list);
   DE_ASSERT(s == WIFI_ENGINE_SUCCESS);

   ss->scan_filter.bss_type = M80211_CAPABILITY_ESS;
   ss->scan_filter.rssi_thr = -150; /* some low rssi */
   ss->scan_filter.snr_thr = 0;    /* some low snr */

   ss->use_scan_filter = 0;

   return WIFI_ENGINE_SUCCESS;
}


int WiFiEngine_ActivateScanSuite(
      scan_suite_s *ss, 
      i_func_t scan_ind_cb, 
      i_func_t scan_complete_cb)
{
   int s;

   if(scan_ind_cb)
   {
      ss->scan_ind_h = we_ind_register(
            WE_IND_SCAN_INDICATION,
            "scan_suite",
            scan_ind_cb, 
            NULL, 
            RELEASE_IND_ON_UNPLUG,
            ss);

      if(!ss->scan_ind_h)
      {
         return WIFI_ENGINE_FAILURE_RESOURCES;
      }
   }

   if(scan_complete_cb)
   {
      ss->scan_complete_h = we_ind_register(
            WE_IND_SCAN_COMPLETE,
            "scan_suite",
            scan_complete_cb,
            NULL,
            RELEASE_IND_ON_UNPLUG,
            ss);

      if(!ss->scan_complete_h)
      {
         we_ind_deregister(ss->scan_complete_h);
         return WIFI_ENGINE_FAILURE_RESOURCES;
      }
   }

   s = WiFiEngine_AddScanJob(&ss->scan_job.id, 
         ss->scan_job.ssid ,
         ss->scan_job.bssid ,
         ss->scan_job.type ,
         ss->scan_job.ch_list ,
         ss->scan_job.flags ,
         ss->scan_job.prio ,
         ss->scan_job.ap_exclude,
         ss->scan_job.filter_id, 
         ss->scan_job.run_every_nth_period,
         NULL);

   if(s != WIFI_ENGINE_SUCCESS)
   {
      we_ind_deregister_null(&ss->scan_ind_h);
      we_ind_deregister_null(&ss->scan_complete_h);
      return s;
   }

   if(!ss->use_scan_filter)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   s = WiFiEngine_AddScanFilter( 
         &ss->scan_filter.id, 
         ss->scan_filter.bss_type,
         ss->scan_filter.rssi_thr,
         ss->scan_filter.snr_thr,
         ss->scan_filter.threshold_type,
         NULL);

   if(s != WIFI_ENGINE_SUCCESS)
   {
      we_ind_deregister_null(&ss->scan_ind_h);
      we_ind_deregister_null(&ss->scan_complete_h);
      WiFiEngine_RemoveScanJob(ss->scan_job.id, NULL);
      return s;
   }

   return WIFI_ENGINE_SUCCESS;
}


void WiFiEngine_DeactivateScanSuite(scan_suite_s *ss)
{
   we_ind_deregister_null(&ss->scan_ind_h);
   we_ind_deregister_null(&ss->scan_complete_h);
   WiFiEngine_RemoveScanJob(ss->scan_job.id, NULL);
   if(ss->use_scan_filter)
   {
      WiFiEngine_RemoveScanFilter(ss->scan_filter.id,NULL);
   }
}


