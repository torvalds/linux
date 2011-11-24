/* $Id: $ */

/** @defgroup we_lqm WiFiEngine link quality management
 *
 * \brief  This module implements WiFiEngine link quality management
 *
 * On recieving PEER_STATUS Warnings lqm will issue probes to the AP to
 * force the AP to sent responces. This will make the link less vonurable
 * to AP'n that tometimes 'forget' to send beacons. (mostly D-Link)
 *
 */

#include "driverenv.h"
#include "m80211_stddefs.h"
#include "driverenv_kref.h"
#include "we_ind.h"
#include "wifi_engine_internal.h"
#include "wei_netlist.h"
#include "hmg_traffic.h"
#include "registry.h"
#include "registryAccess.h"


#ifdef FILE_WE_LQM_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_LQM_C
#endif

/* this has been depricated in fw */
#if 0

static uint32_t lqm_enabled = FALSE;
static uint32_t lqm_scan_job_id;

static int we_lqm_create_scan_job(WiFiEngine_net_t* net, uint32_t *lqm_job_id)
{
   int r;
   channel_list_t ch_list;

   if (net)
   {
      ch_list.reserved = 0;
      ch_list.no_channels = 1;
      ch_list.channelList[0] = net->bss_p->bss_description_p->ie.ds_parameter_set.channel;

      r = WiFiEngine_AddScanJob(lqm_job_id,
                            net->bss_p->bss_description_p->ie.ssid,
                            net->bssId_AP,
                            1,
                            ch_list,
                            CONNECTED_MODE,
                            1,
                            0,
                            -1,
                            1,
                            NULL);
      return r;
   }
   return WIFI_ENGINE_FAILURE;
}
#endif


/*
 * Configure a scan job to be used if mlme_peer_status_ind is received
 * with status warning. The job will initiate sending of a probe
 * request to the associated AP.
 */
int we_lqm_configure_lqm_job(void)
{
#if 0
   int r;
   // TODO: make it optional to use a pre configured scan job
   r = we_lqm_create_scan_job(
      wei_netlist_get_current_net(),
      &lqm_scan_job_id);

   if(r==WIFI_ENGINE_SUCCESS) lqm_enabled = TRUE;

   // TODO: add indication for connected/disconnected to avoid scanning when not connected
#endif
   return WIFI_ENGINE_SUCCESS;
}

int wei_lqm_shutdown(void)
{
#if 0
   if(lqm_enabled == TRUE) {
      WiFiEngine_RemoveScanJob(lqm_scan_job_id, NULL);
      lqm_enabled = FALSE;
   }
#endif
   return 0;
}

/*
 * Activate the lqm job
 */
int we_lqm_trigger_scan(int peer_status)
{
#if 0
   /* TODO: check if connected */

   if(lqm_enabled == TRUE) {
      wei_send_scan_req(lqm_scan_job_id, 0, 0, 0, 0);
   }

#endif
   return WIFI_ENGINE_SUCCESS;
}

/** @} */ /* End of we_roam group */
