/* $Id: we_scan.c,v 1.123 2008-05-19 15:03:24 peek Exp $ */
/*****************************************************************************

Copyright (c) 2006 by Nanoradio AB

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
This module implements the WiFiEngine scan handler.

*****************************************************************************/
/** @defgroup we_scan WiFiEngine scan interface
 *
 * @brief This module implements the WiFiEngine scan interface. Scans can be started
 * explicitly (through calls to WiFiEngine_StartNetworkScan()) or implicitly
 * through the timer-based periodic scan facility.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "packer.h"
#include "mlme_proxy.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"

#ifdef FILE_WE_SCAN_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_SCAN_C
#endif //FILE_WE_SCAN_C

/* These parameters should be obtained through some kind of common include */
#define MAX_SCAN_JOBS 9
#define MAX_SCAN_FILTERS 3

struct scan_job_t {
   m80211_nrp_mlme_add_scanjob_req_t ss;
   uint8_t state;
};

struct scan_state_t {
   m80211_nrp_mlme_scan_config_t sp;
   struct scan_job_t sj[MAX_SCAN_JOBS];
   m80211_nrp_mlme_add_scanfilter_req_t sf[MAX_SCAN_FILTERS];
};

#define JOBID(SJ) ((SJ)->ss.job_id)
#define UNUSEDID 0xdeadbeef
#define JOBUNUSED(SJ) (JOBID(SJ) == UNUSEDID)

static struct scan_state_t scan_state;
static int                 last_scan_channel = 0;

static void
print_scan_job(const struct scan_job_t *sj,
	       char *buf, size_t len)
{
   m80211_ie_ssid_t ssid;
   char ssid_str[M80211_IE_MAX_LENGTH_SSID+1]; 

   DE_ASSERT(sj->ss.ssid_len < sizeof(ssid_str));

   ssid.hdr.len = sj->ss.ssid_len;
   ssid.hdr.id = sj->ss.ssid_id;
   DE_MEMCPY(ssid.ssid, sj->ss.ssid, sj->ss.ssid_len);
   
   DE_SNPRINTF(buf, len, 
	       "job_id %u %s chan %x mode %d %s fid %u ssid %s", 
	       sj->ss.job_id, 
	       sj->state ? "RUNNING" : "SUSPENDED",
	       sj->ss.channels, 
	       sj->ss.scan_mode, 
	       sj->ss.scan_type ? "ACTIVE" : "PASSIVE", 
	       sj->ss.filter_id, 
	       wei_printSSID(&ssid, ssid_str, sizeof(ssid_str)));
}

static void
print_scan_filter(const m80211_nrp_mlme_add_scanfilter_req_t *sf, 
		  char *buf, size_t len)
{
   DE_SNPRINTF(buf, len, 
	       "filter_id %u rssi %d snr %d type %s",
	       sf->filter_id,
	       sf->rssi_threshold,
	       sf->snr_threshold,
	       sf->threshold_type ? "RELATIVE" : "ABSOLUTE");
}

#define TRACE_SCAN_JOB(SJ) do {			\
   char __msg[128];				\
   print_scan_job((SJ), __msg, sizeof(__msg));	\
   DE_TRACE_STRING(TR_SCAN, "%s\n", __msg);	\
} while(0)

#define TRACE_SCAN_FILTER(SF) do {			\
   char __msg[128];					\
   print_scan_filter((SF), __msg, sizeof(__msg));	\
   DE_TRACE_STRING(TR_SCAN, "%s\n", __msg);		\
} while(0)

/************************************************************/
/************************************************************/
/************************************************************/


static int cache_clear_scan(void)
{
   int i;
   DE_TRACE_STATIC(TR_SCAN, "ENTRY\n");
   DE_MEMSET(&scan_state, 0, sizeof scan_state);
   for(i = 0; i < MAX_SCAN_JOBS; i++)
      JOBID(&scan_state.sj[i]) = UNUSEDID;
   for(i = 0; i < MAX_SCAN_FILTERS; i++)
      scan_state.sf[i].filter_id = UNUSEDID;

   return WIFI_ENGINE_SUCCESS;
}

static int cache_configure_scan(preamble_t preamble,
                                uint8_t rate,
                                uint8_t probes_per_ch,
                                WiFiEngine_scan_notif_pol_t notif_pol,
                                uint32_t scan_period,
                                uint32_t probe_delay,
                                uint16_t pa_min_ch_time,
                                uint16_t pa_max_ch_time,
                                uint16_t ac_min_ch_time,
                                uint16_t ac_max_ch_time,
                                uint32_t as_scan_period,
                                uint16_t as_min_ch_time,
                                uint16_t as_max_ch_time,
                                uint32_t max_scan_period,
                                uint32_t max_as_scan_period,
                                uint8_t  period_repetition)
{
#define A(X, Y) scan_state.sp.Y = (X)
   A(preamble, preamble);
   A(rate, rate);
   A(probes_per_ch, probes_per_ch);
   scan_state.sp.deliv_pol = 0;
   A(notif_pol, notification_policy);
   A(scan_period, discon_scan_period);
   A(probe_delay, probe_delay);
   A(pa_min_ch_time, pa_min_ch_time);
   A(pa_max_ch_time, pa_max_ch_time);
   A(ac_min_ch_time, ac_min_ch_time);
   A(ac_max_ch_time, ac_max_ch_time);
   A(as_scan_period, conn_scan_period);
   A(as_min_ch_time, as_min_ch_time);
   A(as_max_ch_time, as_max_ch_time);
   A(max_scan_period, max_disconnect_period);
   A(max_as_scan_period, max_connect_period);
   A(period_repetition, period_repetition);
#undef A
   return WIFI_ENGINE_SUCCESS;
}

static int cache_get_scan_job_state(struct scan_job_t* sj)
{
   return sj->state;
}
 
static void cache_set_scan_job_state(struct scan_job_t* sj, uint8_t state)
{
   sj->state = state;
   TRACE_SCAN_JOB(sj);
}


static struct scan_job_t*
cache_find_job(uint32_t sj_id)
{
   int i;
   
   for(i = 0; i < MAX_SCAN_JOBS; i++) {
      if(JOBID(&scan_state.sj[i]) == sj_id) {
	 if(sj_id == UNUSEDID)
	    /* hack to allocate slot */
	    JOBID(&scan_state.sj[i]) = i;
	 return &scan_state.sj[i];
      }
   }
   return NULL;
}

static struct scan_job_t*
cache_add_scan_job(m80211_ie_ssid_t ssid,
		   m80211_mac_addr_t bssid,
		   uint8_t scan_type,
		   channel_list_t ch_list, 
		   int flags,
		   uint8_t prio, 
		   uint8_t ap_exclude, 
		   const m80211_nrp_mlme_add_scanfilter_req_t *sf,
		   uint8_t run_every_nth_period)
{
   struct scan_job_t* sj;
   
   sj = cache_find_job(UNUSEDID);
   if(sj == NULL)
      return NULL;

   sj->ss.prio = prio;
   sj->ss.as_exclude = ap_exclude;
   sj->ss.scan_type = scan_type;
   sj->ss.scan_mode = flags;
   sj->ss.channels = wei_channels2bitmask(&ch_list);
   DE_MEMCPY(sj->ss.bssid, bssid.octet, sizeof sj->ss.bssid);
   sj->ss.ssid_id = ssid.hdr.id;
   sj->ss.ssid_len = ssid.hdr.len;
   DE_MEMCPY(sj->ss.ssid, ssid.ssid, sizeof sj->ss.ssid);
   if(sf == NULL)
      sj->ss.filter_id = (uint32_t)-1;
   else
      sj->ss.filter_id = sf->filter_id;
      
   sj->ss.run_every_nth_period = run_every_nth_period;
   
   sj->state = 0;

   TRACE_SCAN_JOB(sj);
      
   return sj;
}


static void cache_del_scan_job(struct scan_job_t* sj)
{
   TRACE_SCAN_JOB(sj);
   JOBID(sj) = UNUSEDID;
}

static m80211_nrp_mlme_add_scanfilter_req_t*
cache_find_filter(uint32_t sf_id)
{
   int i;
   
   for(i = 0; i < MAX_SCAN_FILTERS; i++)
      if(scan_state.sf[i].filter_id == sf_id) {
	 if(sf_id == UNUSEDID)
	    /* hack to allocate slot */
	    scan_state.sf[i].filter_id = i;
	 return &scan_state.sf[i];
      }
   
   return NULL;
}

static m80211_nrp_mlme_add_scanfilter_req_t*
cache_add_scan_filter(uint8_t bss_type,
		      int32_t rssi_thr,
		      uint32_t snr_thr,
		      uint16_t threshold_type)
{
   m80211_nrp_mlme_add_scanfilter_req_t *sf;
   
   sf = cache_find_filter(UNUSEDID);
   if(sf == NULL)
      return NULL;

   sf->bss_type = bss_type;
   sf->rssi_threshold = rssi_thr;
   sf->snr_threshold = snr_thr;
   sf->threshold_type = threshold_type;

   TRACE_SCAN_FILTER(sf);
      
   return sf;
}

static void cache_del_scan_filter(m80211_nrp_mlme_add_scanfilter_req_t *sf)
{
   TRACE_SCAN_FILTER(sf);
   sf->filter_id = UNUSEDID;
}

/************************************************************/
/************************************************************/
/************************************************************/

/*!
 * @brief Scan the network for BSSs. 
 * 
 * Internals
 * If the NIC is associated with a SSID not in the scan
 * results, the currently associated network should be appended
 * to the scan results.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_DEFER if a directed scan was already in progress
 * - WIFI_ENGINE_FAILURE on other failures.
 */
int WiFiEngine_StartNetworkScan(void)
{
   return wei_send_scan_req(0, DEFAULT_CHANNEL_INTERVAL, 0, 0, 0);
}

/*!
 * @brief Trigger a scan job.
 *
 * @param job_id           Identifies the scan job to trigger.
 * @param channel_interval Channel interval in kTU's.
 * @return
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_DEFER if a directed scan was already in progress
 * - WIFI_ENGINE_FAILURE on other failures.
 */
int WiFiEngine_TriggerScanJob(uint32_t job_id, uint16_t channel_interval)
{
   return wei_send_scan_req(job_id,channel_interval, 0, 0, 0);
}

/*!
 * @brief Trigger a scan job with extended parameter set.
 *
 * @param job_id           Identifies the scan job to trigger.
 * @param channel_interval Channel interval in kTU's.
 * @param probe_delay      Probe delay in kTU's (if zero default will be used) 
 * @param min_ch_time      Min channel time in kTU's (if zero default will be used) 
 * @param max_ch_time      Max channel time in kTU's (if zero default will be used) 
 * @return
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_DEFER if a directed scan was already in progress
 * - WIFI_ENGINE_FAILURE on other failures.
 */
int WiFiEngine_TriggerScanJobEx(uint32_t job_id, uint16_t channel_interval, 
                                uint32_t probe_delay, uint16_t min_ch_time, uint16_t max_ch_time)
{
   return wei_send_scan_req(job_id,channel_interval, probe_delay, min_ch_time, max_ch_time);
}

/*!
 * @brief Get the current list of known networks.
 *
 * Get the list of scan results. Note that this is a functional replica
 * of WiFiEngine_GetBSSIDList() in we_assoc.c. The functions perform the
 * same job but this one takes a list of referencies rather than requiring
 * one consecutive area to store all the nets in. 
 *
 * @param list Pointer to a allocated array of net referencies. Can be null, in
 *             which case entries will be filled in with the required 
 *             number of entries needed.
 * @param entries IN: Number of entries available in list. 
 *               OUT: Number of entries used or needed in list.
 *               Note that the number of entries needed may change
 *               from call to call as stations/access points come and go.
 * @return WIFI_ENGINE_SUCCESS on success.
 *         WIFI_ENGINE_FAILURE_INVALID_LENGTH if the list array was too small.
 *         WIFI_ENGINE_SUCCESS will always be returned if list was NULL
 *         and entries will always contain the number of entries needed.
 */
int WiFiEngine_GetScanList(WiFiEngine_net_t **list, int *entries)
{
   wei_netlist_state_t *nets;
   WiFiEngine_net_t *net, *pnet;
   WiFiEngine_net_t **p = NULL;
   rConnectionPolicy *cp;
   int needed_entries = 0;
   int avail_entries = *entries;

   BAIL_IF_UNPLUGGED;
   if (list == NULL)
   {
      avail_entries = 0;
   }
   else
   {
      p = list;
   }
   cp = (rConnectionPolicy *)Registry_GetProperty(ID_connectionPolicy);
   /* FIXME: wei_netlist internals spilling out on the floor! */
   if ( (nets = wei_netlist_get_net_state()) != NULL) {
      if (nets->active_list_ref)
      {
         net = nets->active_list_ref;
         while (net)
         {
            pnet = net;
            net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net, WiFiEngine_net_t, active_list);
            if (wei_netlist_expire_net(pnet, 
#ifdef USE_NEW_AGE
                                       wifiEngineState.scan_count,
                                       cp->scanResultLifetime
#else
                                       DriverEnvironment_GetTicks(),
                                       DriverEnvironment_msec_to_ticks(cp->scanResultLifetime)
#endif
                   ))
            {
               wei_netlist_free_net_safe(pnet);
            }
            else
            {
               needed_entries++;
               /* Do not consider it as an error if the list has changed since the number of networks where counted. */
               if (avail_entries)
               {
                  avail_entries--;
                  /* Increase reference count for the net when adding it to the list. */
                  /* This to avoid the net beeing removed 
                     before WiFiEngine_ReleaseScanListis called */
                  wei_netlist_add_reference(p, pnet);
                  p++;
               }
            }
         }
      }
   }
   *entries = needed_entries;

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Release the current list of known networks.
 *
 * Release all references to nets retrieved with WiFiEngine_GetScanList. 
 *
 * @param list Pointer to an array of net referencies retrieved with WiFiEngine_GetScanList.
 * @param entries Number of entries available in list. 
 * @return WIFI_ENGINE_SUCCESS on success.
 *         WIFI_ENGINE_FAILURE_INVALID_LENGTH if the list array was too small.
 *         WIFI_ENGINE_SUCCESS will always be returned if list was NULL
 *         and entries will always contain the number of entries needed.
 */
int WiFiEngine_ReleaseScanList(WiFiEngine_net_t **list, int entries)
{
   WiFiEngine_net_t **p = list;

   BAIL_IF_UNPLUGGED;
   DE_ASSERT(list != NULL);  

   for (;entries>0;entries--)
   {
      wei_netlist_remove_reference(p);
      p++;
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_configure_scan(m80211_nrp_mlme_scan_config_t *sp, 
		       we_cb_container_t *cbc)
{
   hic_message_context_t   msg_ref;
   int status;
   uint32_t trans_id;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateSetScanParamRequest(&msg_ref, sp, &trans_id))
   {
      if (cbc)
      {
         cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
      wei_send_cmd(&msg_ref);
      status = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_ASSOC, "Failed to create SetScanParamRequest\n");
      status = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   return status;
}

/*!
 * @brief Configure the scan engine.
 *
 * This sets the general scan parameters for the scan engine in the device.
 *
 * @param preamble Long or short preamble for probe requests. Only valid
 *                 for active scan.
 * @param rate Rate in the "supported rates" encoding from the 802.11
 *             standard. This first rate in this list is used for probe
 *             requests. Only valid for active scan. A rate value of 0
 *             means that the firmware will pick the rate for probe
 *             requests as it sees fit.
 * @param probes_per_ch Number of probe requests sent on each channel.
 *                    Probe requests will be sent with an interval
 *                    of min_ch_time us. Maximum value is 255. Only valid for
 *                    active scan.
 *                    A higher value increases the likelyhood that the APs
 *                    on a channel will hear a probe request in case of
 *                    bad radio conditions.
 *                    The probe request will be directed to the ssid specified
 *                    in the scan job. If an ssid is not specified for the scan
 *                    job, then the probe request will be broadcasted.
 * @param notif_pol   Scan notification deliver policy.
 * @param scan_period Time in ms between scans. If this parameter is 0
 *                    the device will never scan by itself. Otherwise it
 *                    will periodically initiate a new scan when this
 *                    period has passed since the last scan was completed. 
 *                    This parameter is not used when the device is associated.
 *                    The maximum value is 2^32 ms.
 * @param probe_delay Time in us spent on the channel before sending the first
 *                    probe request. This parameter will only be used for
 *                    active scan.
 *                    The minimum value is 0. The maximum value is 2^32 us.
 * @param pa_min_ch_time The minimum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel
 *                    during passive scanning. The channel will be
 *                    changed when this amount of time has passed and
 *                    no beacons have been received. This parameter is
 *                    not used when the device is associated.  The
 *                    minimum value is 0. The maximum value is 2^16 kus.
 * @param pa_max_ch_time The maximum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel
 *                    during passiv scanning. The channel will be
 *                    changed when this amount of time has passed and
 *                    no beacons have been received.  This parameter is
 *                    not used when the device is associated.  The
 *                    minimum value is 0. The maximum value is 2^16 kus.
 *                    The value must be larger than pa_min_ch_time.
 * @param ac_min_ch_time The minimum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel
 *                    during active scanning. The channel will be
 *                    changed when this amount of time has passed and
 *                    probe responses have been received.  This
 *                    parameter is not used when the device is
 *                    associated.  The minimum value is 0. The maximum
 *                    value is 2^16 kus.
 * @param ac_max_ch_time The maximum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel
 *                    during active scanning. The channel will be
 *                    changed when this amount of time has passed and
 *                    no probe responses have been received. This
 *                    parameter is not used when the device is
 *                    associated.  The minimum value is 0. The maximum
 *                    value is 2^16 kus.  The value must be larger than
 *                    min_ch_time.
 * @param as_scan_period Time in ms between scans when the device is
 *                    associated. 
 *                    If this parameter is 0 the device will never scan when
 *                    associated. Otherwise it will periodically initiate a
 *                    new scan when this period has passed since the last scan
 *                    was completed.
 *                    This parameter affects traffic throughput and latency,
 *                    shorter times degrade performance more.
 *                    This parameter is only used when the device is associated.
 *                    The maximum value is 2^32 ms. 
 * @param as_min_ch_time The minimum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel when the device is associated. The channel
 *                    will be changed when this amount of time 
 *                    has passed and beacons/probe responses have been received.
 *                    This parameter is only used when the device is associated.
 *                    The minimum value is 0. The maximum value is 2^16 kus.
 * @param as_max_ch_time The maximum time in kus (1 kus = 1.024 ms) spent on
 *                    each channel when the device
 *                    is associated. The channel will be changed when this
 *                    amount of time has passed and no beacons/probe responses
 *                    have been received.
 *                    This parameter is only used when the device is
 *                    associated.
 *                    The minimum value is 0. The maximum value is 2^16 kus.
 *                    The value must be larger than as_min_ch_time.
 * @param max_scan_period Maximum time in ms between scan periods when disconnected. 
 *                    This parameter is used as a limitation(>=) of the scan period when 
 *                    the nominal scan period doubles after every period_repetition.
 *                    The maximum value is 2^32-1 ms. 
 * @param max_as_scan_period Maximum time in ms between scan periods when connected. 
 *                    This parameter is used as a limitation(>=) of the scan period when 
 *                    the nominal scan period doubles after every period_repetition.
 *                    The maximum value is 2^32-1 ms. 
 * @param period_repetition Number of repetitions of a scan period before it is doubled.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_ConfigureScan() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE otherwise.
 */


int WiFiEngine_ConfigureScan(
   preamble_t preamble,
   uint8_t rate,
   uint8_t probes_per_ch,
   WiFiEngine_scan_notif_pol_t notif_pol,
   uint32_t scan_period,
   uint32_t probe_delay,
   uint16_t pa_min_ch_time,
   uint16_t pa_max_ch_time,
   uint16_t ac_min_ch_time,
   uint16_t ac_max_ch_time,
   uint32_t as_scan_period,
   uint16_t as_min_ch_time,
   uint16_t as_max_ch_time,
   uint32_t max_scan_period,
   uint32_t max_as_scan_period,
   uint8_t  period_repetition,
   we_cb_container_t *cbc)
{
   cache_configure_scan(preamble, rate, probes_per_ch, notif_pol,
			scan_period, probe_delay, pa_min_ch_time,
			pa_max_ch_time, ac_min_ch_time,
			ac_max_ch_time, as_scan_period,
			as_min_ch_time, as_max_ch_time,
			max_scan_period, max_as_scan_period,
			period_repetition);
   
   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
      return WIFI_ENGINE_SUCCESS;
   }
   
   return wei_configure_scan(&scan_state.sp, cbc);
}


int WiFiEngine_ReConfigureScan(void)
{
   wei_reinitialize_scan();
   return wei_configure_scan(&scan_state.sp, NULL);
}

int WiFiEngine_GetScanConfig(
   preamble_t* preamble,
   uint8_t* rate,
   uint8_t* probes_per_ch,
   WiFiEngine_scan_notif_pol_t* notif_pol,
   uint32_t* scan_period,
   uint32_t* probe_delay,
   uint16_t* pa_min_ch_time,
   uint16_t* pa_max_ch_time,
   uint16_t* ac_min_ch_time,
   uint16_t* ac_max_ch_time,
   uint32_t* as_scan_period,
   uint16_t* as_min_ch_time,
   uint16_t* as_max_ch_time,
   uint32_t* max_scan_period,
   uint32_t* max_as_scan_period,
   uint8_t*  period_repetition)
{
#define A(X, Y) if((X) != NULL) *(X) = scan_state.sp.Y
   if (preamble != NULL)
   {
      *preamble = (preamble_t)scan_state.sp.preamble;
   }
   A(rate, rate);
   A(probes_per_ch, probes_per_ch);
   A(notif_pol, notification_policy);
   A(scan_period, discon_scan_period);
   A(probe_delay, probe_delay);
   A(pa_min_ch_time, pa_min_ch_time);
   A(pa_max_ch_time, pa_max_ch_time);
   A(ac_min_ch_time, ac_min_ch_time);
   A(ac_max_ch_time, ac_max_ch_time);
   A(as_scan_period, conn_scan_period);
   A(as_min_ch_time, as_min_ch_time);
   A(as_max_ch_time, as_max_ch_time);
   A(max_scan_period, max_disconnect_period);
   A(max_as_scan_period, max_connect_period);
   A(period_repetition, period_repetition);
#undef A
   return WIFI_ENGINE_SUCCESS;
}

static int
wei_add_scan_filter(m80211_nrp_mlme_add_scanfilter_req_t *sf,
		    we_cb_container_t *cbc)
{
   hic_message_context_t   msg_ref;
   int r = WIFI_ENGINE_SUCCESS;
   uint32_t trans_id;
   
   TRACE_SCAN_FILTER(sf);

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateAddScanFilterRequest(&msg_ref, sf, &trans_id))
   {
      if (cbc)
      {
         cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
      wei_send_cmd(&msg_ref);
      r = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_ASSOC, "Failed to create addscanfilter_req\n");
      r = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   
   return r;
}

/*!
 * @brief Add a scan filter to the device.
 *
 * @param sf_id The scan filter id output buffer. This identifies the scan
 *              filter so that several scan filters can be defined.
 *              The id value is filled in by this function call.
 * @param bss_type Bitmask defining which BSS type to scan for
 *        (M80211_CAPABILITY_ESS and/or M80211_CAPABILITY_IBSS).
 * @param rssi_thr RSSI threshold. Pass results with a higher RSSI than this
 *                 value [dBm] (signed).
 * @param snr_thr SNR threshold. Pass results with a higher SNR than this
 *                 value.
 * @param threshold_type Type of threshold, ABSOLUTE(0) or RELATIVE(1)
 * @param cbc Pointer to a caller-allocated callback-container.
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_AddScanJob() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 */
int WiFiEngine_AddScanFilter(uint32_t *sf_id,
                             uint8_t bss_type,
                             int32_t rssi_thr,
                             uint32_t snr_thr,
                             uint16_t threshold_type,
                             we_cb_container_t *cbc)
{
   m80211_nrp_mlme_add_scanfilter_req_t *sf;

   sf = cache_add_scan_filter(bss_type, rssi_thr, snr_thr, threshold_type);
   if(sf == NULL) {
      DE_TRACE_STATIC(TR_SCAN, "failed to add scan filter\n");
      return WIFI_ENGINE_FAILURE;
   }

   if(sf_id != NULL)
      *sf_id = sf->filter_id;
   
   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
      return WIFI_ENGINE_SUCCESS;
   }

   return wei_add_scan_filter(sf, cbc);
}

static int 
wei_remove_scan_filter(m80211_nrp_mlme_add_scanfilter_req_t *sf, 
		       we_cb_container_t *cbc)
{
   m80211_nrp_mlme_remove_scanfilter_req_t req;
   hic_message_context_t   msg_ref;
   int r = WIFI_ENGINE_SUCCESS;
   uint32_t trans_id;

   TRACE_SCAN_FILTER(sf);

   req.filter_id = sf->filter_id;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateRemoveScanFilterRequest(&msg_ref, &req, &trans_id))
   {
     if (cbc)
      {
         cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
     wei_send_cmd(&msg_ref);
     r = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_SCAN, "Failed to create removescanfilter_req\n");
      r = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   
   return r;
}

/*!
 * @brief Remove a scan filter from the device hardware.
 *
 * @param sf_id This identifies the scan filter to be deleted.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_RemoveScanFilter() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 */
int WiFiEngine_RemoveScanFilter(uint32_t sf_id,
                                we_cb_container_t *cbc)
{
   m80211_nrp_mlme_add_scanfilter_req_t *sf;
   int status;

   sf = cache_find_filter(sf_id);
   if(sf == NULL) {
      DE_TRACE_INT(TR_SCAN, "no scan filter with id %u present\n", sf_id);
      return WIFI_ENGINE_FAILURE;
   }
   
   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
   } else {
      status = wei_remove_scan_filter(sf, cbc);
      if(status != WIFI_ENGINE_SUCCESS)
	 return status;
   }
   cache_del_scan_filter(sf);
   return WIFI_ENGINE_SUCCESS;
}


static int
wei_add_scan_job(struct scan_job_t *sj, we_cb_container_t *cbc)
{
   hic_message_context_t   msg_ref;
   uint32_t trans_id;
   int status;
   m80211_nrp_mlme_add_scanjob_req_t req;
   channel_list_t channels;
   uint32_t channel_mask = ~0;

   TRACE_SCAN_JOB(sj);
      
   /* filter channels with regional channel list */
   req = sj->ss;
   status = WiFiEngine_GetRegionalChannels(&channels);
   if(status == WIFI_ENGINE_SUCCESS) {
      channel_mask = wei_channels2bitmask(&channels);
   }
   req.channels &= channel_mask;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateAddScanJobRequest(&msg_ref, &req, &trans_id))
   {
      if (cbc)
      {
	 cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
      wei_send_cmd(&msg_ref);
      status = WIFI_ENGINE_SUCCESS;
}
   else 
   {
      DE_TRACE_STATIC(TR_ASSOC, "Failed to create addscanjob_req\n");
      status = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   return status;
}


/*! 
 * @brief Add a scan job to the device.
 *
 * @param sj_id The scan job id output buffer (can be NULL). This 
 *              identifies the scan job so that several scan jobs can be defined. 
 *              The ID value is filled in by this function call.
 * @param ssid Defines the SSID that the scan job should look for. 
 *             A zero-length SSID counts as a "broadcast" SSID
 *             which will cause all SSIDs to be scanned for.
 * @param bssid A BSSID to scan for. If this is set to the broadcast BSSID
 *              (ff:ff:ff:ff:ff:ff) then all BSSIDs will be scanned (subject
 *              to the SSID parameter). BSSID-specific scan can be performed
 *              be using zero-length SSID. Otherwise the results have to match
 *              both ssid and bssid.
 * @param scan_type 1 for active scan. 0 for passive scan. The device will 
 *             always perform active scans when it is associated. To prevent 
 *             execution of a job when association the flags parameter should be 
 *             used.
 * @param ch_list Defines the channels to scan. The channel list is subject
 *                to further filtering due to 802.11d restrictions.
 * @param flags Bitmask that defines whether the scan job should be used when
 *              associated and/or when not associated.
 * @param prio  Priority value for the scan job. Jobs with higher priority
 *              values are run before jobs with lower priority values.
 *              Valid priorities are in the range 1-128.
 * @param ap_exclude If this value is set to 1 the AP which the device is
 *                   associated with should be exluded from the scan result.
 * @param sf_id Defines the scan filter to use for the scan job. The value
 *              should be set to -1 if no filter should be used.
 * @param run_every_nth_period Defines how often the job should be run.
 *                             Default = 1 (every period)
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_AddScanJob() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE on failure.
 */
int WiFiEngine_AddScanJob(uint32_t *sj_id,
                          m80211_ie_ssid_t ssid,
                          m80211_mac_addr_t bssid,
                          uint8_t scan_type,
                          channel_list_t ch_list,
                          int flags,
                          uint8_t prio,
                          uint8_t ap_exclude,
                          int sf_id,
                          uint8_t run_every_nth_period,
                          we_cb_container_t *cbc)
{
   m80211_nrp_mlme_add_scanfilter_req_t *sf = NULL;
   struct scan_job_t *sj;
   
   /* XXX a way to add hidden SSID jobs is missing from this */

   if(sf_id != -1 && sf_id != (int)UNUSEDID) {
      sf = cache_find_filter(sf_id);
      if(sf == NULL) {
	 DE_TRACE_INT(TR_NOISE, "bad scan filter id %d\n", sf_id);
	 return WIFI_ENGINE_FAILURE;
      }
   }

   sj = cache_add_scan_job(ssid, bssid, scan_type, ch_list, flags, 
			   prio, ap_exclude, sf, run_every_nth_period);
   if(sj == NULL) {
      DE_TRACE_STATIC(TR_SCAN, "failed to add scan job\n");
      return WIFI_ENGINE_FAILURE;
   }

   if(sj_id != NULL)
      *sj_id = JOBID(sj);

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
      return WIFI_ENGINE_SUCCESS;
   }

   return wei_add_scan_job(sj, cbc);
}

static int
wei_remove_scan_job(struct scan_job_t *sj, we_cb_container_t *cbc)
{
   m80211_nrp_mlme_remove_scanjob_req_t req;
   hic_message_context_t   msg_ref;
   uint32_t trans_id;
   int status;
   
   TRACE_SCAN_JOB(sj);
   
   req.job_id = JOBID(sj);
   req.trans_id = 0;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateRemoveScanJobRequest(&msg_ref, &req, &trans_id))
   {
      if (cbc)
      {
         cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
      wei_send_cmd(&msg_ref);
      status = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_SCAN, "Failed to create removescanjob_req\n");
      status = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);
   
   return status;
}


/*!
 * @brief Remove a scan job from the device hardware.
 *
 * @param sj_id This identifies the scan job to be deleted.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_RemoveScanJob() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_DATA on invalid sj_id.
 */
int WiFiEngine_RemoveScanJob(uint32_t sj_id,
                             we_cb_container_t *cbc)
{
   struct scan_job_t *sj;
   int status;
   
   sj = cache_find_job(sj_id);
   if(sj == NULL) {
      DE_TRACE_INT(TR_SCAN, "no scan job with id %u present\n", sj_id);
      return WIFI_ENGINE_FAILURE;
   }

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
   } else {
      status = wei_remove_scan_job(sj, cbc);
      if(status != WIFI_ENGINE_SUCCESS)
	 return status;
   }

   cache_del_scan_job(sj);
   return WIFI_ENGINE_SUCCESS;
}

int WiFiEngine_RemoveAllScanJobs(void)
{
   unsigned int i;
   
   for(i = 0; i < MAX_SCAN_JOBS; i++)
      WiFiEngine_RemoveScanJob(i, NULL);

   return WIFI_ENGINE_SUCCESS;
}


static int
wei_set_scan_job_state(struct scan_job_t *sj, 
		       we_cb_container_t *cbc)
{
   m80211_nrp_mlme_set_scanjobstate_req_t req;
   hic_message_context_t   msg_ref;
   int status;
   uint32_t trans_id;
   
   TRACE_SCAN_JOB(sj);

   DE_MEMSET(&req, 0, sizeof(req));   

   req.job_id   = JOBID(sj);
   req.state    = sj->state;
   req.reserved[0] = 0;
   req.reserved[1] = 0;
   req.reserved[2] = 0;


   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateSetScanJobStateRequest(&msg_ref, &req, &trans_id))
   {
     if (cbc)
      {
         cbc->trans_id = trans_id;
         wei_cb_queue_pending_callback(cbc);
      }
     wei_send_cmd(&msg_ref);
     status = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_SCAN, "Failed to create setscanjobstate_req\n");
      status = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   return status;
}


/*!
 * @brief Start or stop a scan job.
 *
 * Scan jobs added with nrx_add_scan_job() will be stopped by default. The scan
 * job state must be set to 1 in order to start the scan job. When the
 * scan job is started it will executed according to the configured time
 * parameters. The scan job state can be set to 0 to stop the scan job.
 * Each scan job can be started/stopped independently.
 *
 * The scan job state change will take effect the next scan period.
 *
 * @param sj_id A scan job id. This identifies the scan
 *              job.
 *
 * @param state The state which the scan job should be set to. 1 to start the
 *            scan job, 0 to stop it.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a NULL data member
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_SetScanJobState() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *
 * @return
 */
int WiFiEngine_SetScanJobState(uint32_t sj_id, 
                               uint8_t state,
                               we_cb_container_t *cbc)
{
   struct scan_job_t *sj;
   uint8_t old_state;
   
   sj = cache_find_job(sj_id);
   if(sj == NULL) {
      DE_TRACE_INT(TR_SCAN, "no scan job with id %u present\n", sj_id);
      return WIFI_ENGINE_FAILURE;
   }

   old_state = cache_get_scan_job_state(sj);
   DE_TRACE_INT3(TR_SCAN, "job_id %u: %u -> %u\n", sj->ss.job_id, old_state, state);
   cache_set_scan_job_state(sj, state);

   if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
      return WIFI_ENGINE_SUCCESS;
   }

   if(state == old_state) {
      if(cbc != NULL) {
	 cbc->status = 0; /* XXX zero means success */
	 WiFiEngine_RunCallback(cbc);
      }
      return WIFI_ENGINE_SUCCESS;
   }

   return wei_set_scan_job_state(sj, cbc);
}

/*!
 * @brief Retrieve a scan filter.
 *
 *
 * @param sf_id A scan filter id. This identifies the filter to be
 *              retrieved.
 * @param cbc Pointer to a caller-allocated callback-container (can be NULL).
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            The callback will be passed a cbc with a pointer to
 *            a WiFiEngine_scan_filter_t for the data member
 *            in case of success status (otherwise the data member is NULL)
 *            and the status from the operation in the status member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_GetScanFilter() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly and WiFiEngine_FreeCBC()
 *            if it should be freed.
 *
 * @return
 */
int WiFiEngine_GetScanFilter(uint32_t sf_id, 
			     WiFiEngine_scan_filter_t *filter)
{
   m80211_nrp_mlme_add_scanfilter_req_t *sf;

   sf = cache_find_filter(sf_id);
   if(sf == NULL) {
      DE_TRACE_INT(TR_SCAN, "no scan filter with id %u present\n", sf_id);
      return WIFI_ENGINE_FAILURE;
   }
   if(filter != NULL) {
      
      filter->filter_id = sf->filter_id;
      filter->bss_type = (WiFiEngine_bss_type_t)sf->bss_type;
      filter->rssi_thr = sf->rssi_threshold;
      filter->snr_thr = sf->snr_threshold;
   }
   return WIFI_ENGINE_SUCCESS;
}

#ifndef UNREF
#define UNREF(x) (x = x)
#endif
#ifdef WITH_LOST_SCAN_WORKAROUND
static int scan_hung_detect_cb(void *data, size_t len)
{
   UNREF(data);
   UNREF(len);

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

   return WIFI_ENGINE_SUCCESS;
}
#endif

/*!
 * @brief Flush scan list.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 */
int WiFiEngine_FlushScanList(void)
{
   wei_netlist_clear_active_nets();
   wei_netlist_clear_sta_nets();
   return WIFI_ENGINE_SUCCESS;
}

int wei_send_scan_req(uint32_t job_id, uint16_t channel_interval, uint32_t probe_delay, uint16_t min_ch_time, uint16_t max_ch_time)
{
   hic_message_context_t   msg_ref;
   rBasicWiFiProperties *basic;

   int r = WIFI_ENGINE_SUCCESS;

   BAIL_IF_UNPLUGGED;
  
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   
   Mlme_CreateMessageContext(msg_ref);
   
   DE_TRACE_INT2(TR_SCAN, "trig scan job %d channel intervall %d \n", job_id, channel_interval);

   if (Mlme_CreateScanRequest(&msg_ref, basic->activeScanMode, job_id, channel_interval, probe_delay, min_ch_time, max_ch_time))
   {
      wei_send_cmd(&msg_ref);
      r = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_SCAN, "Failed to create ScanRequest\n");
      r = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   return r;
}

int wei_handle_set_scancountryinfo_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleScanCountryInfoCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_send_set_scancountryinfo_req(m80211_ie_country_t *ci)
{
   hic_message_context_t   msg_ref;

   int r = WIFI_ENGINE_SUCCESS;

   BAIL_IF_UNPLUGGED;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateScanCountryInfoRequest(&msg_ref, ci))
   {
      wei_send_cmd(&msg_ref);
      r = WIFI_ENGINE_SUCCESS;
   }
   else 
   {
      DE_TRACE_STATIC(TR_SCAN, "Failed to create ScanCountryInfoRequest\n");
      r = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);

   return r;
}


int wei_handle_scan_ind(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleScanInd(msg_ref))
   {
      /* Notify roaming agent that there is some new info about
       * current nets. */
      wei_roam_notify_scan_ind();

#ifdef MULTIDOMAIN_ENABLED_NOTYET
      if(WES_TEST_FLAG(WES_FLAG_COUNTRY_INFO_CHANGED)) {
         if(wifiEngineState.fw_capabilities & 1) {
            wei_send_set_scancountryinfo_req(wifiEngineState.active_channels_ref);
            WES_CLEAR_FLAG(WES_FLAG_COUNTRY_INFO_CHANGED);
         }
      }
#endif /* MULTIDOMAIN_ENABLED_NOTYET */
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_scan_cfm(hic_message_context_t *msg_ref)
{
   m80211_mlme_scan_cfm_t* cfm;

   /* Get the raw message buffer from the context. */
   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, m80211_mlme_scan_cfm_t);

   if (cfm->result == SCAN_FAILURE)
   {
      DE_TRACE_STATIC(TR_SCAN, "SCAN CFM is marked as failed\n");
      
      DriverEnvironment_indicate(WE_IND_SCAN_COMPLETE, NULL, 0);
      /* Notify roaming agent that trigscan failed */
      wei_roam_notify_scan_failed();
   }

   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_set_scan_param_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleSetScanParamCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "Scan cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "Scan cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_add_scan_filter_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleAddScanFilterCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "Scan filter cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "Scan filter cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_remove_scan_filter_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleRemoveScanFilterCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }

   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_add_scan_job_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleAddScanJobCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_remove_scan_job_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleRemoveScanJobCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_set_scan_job_state_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleSetScanJobStateCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_get_scan_filter_cfm(hic_message_context_t *msg_ref)
{
   if (Mlme_HandleGetScanFilterCfm(msg_ref))
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm received\n");
   }
   else
   {
      DE_TRACE_STATIC(TR_SCAN, "cfm handle failure\n");
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_handle_scan_notification_ind(hic_message_context_t *msg_ref)
{
   int job_complete = 0;
   int bg_job_complete = 0;
   int direct_scan = 0;
   char msg[128];
   size_t len = 0;
   m80211_nrp_mlme_scannotification_ind_t *ind;
   ind = HIC_GET_RAW_FROM_CONTEXT(msg_ref, 
                                  m80211_nrp_mlme_scannotification_ind_t);
   if(TRACE_ENABLED(TR_SCAN)) {
      len += DE_SNPRINTF(msg + len, sizeof(msg) - len, "job %u", ind->job_id);
      len = DE_MIN(sizeof(msg), len);
   }

   if(ind->flags & SCAN_NOTIFICATION_FLAG_FIRST_HIT) {
      if(TRACE_ENABLED(TR_SCAN)) {
         len += DE_SNPRINTF(msg + len, sizeof(msg) - len, " FIRST_HIT");
         len = DE_MIN(sizeof(msg), len);
      }
   }
   if(ind->flags & SCAN_NOTIFICATION_FLAG_JOB_COMPLETE) {
      job_complete = TRUE;
      if(TRACE_ENABLED(TR_SCAN)) {
         len += DE_SNPRINTF(msg + len, sizeof(msg) - len, " JOB_COMPLETE");
         len = DE_MIN(sizeof(msg), len);
      }
   }
   if(ind->flags & SCAN_NOTIFICATION_FLAG_BG_PERIOD_COMPLETE) {
      bg_job_complete = TRUE;
      if(TRACE_ENABLED(TR_SCAN)) {
         len += DE_SNPRINTF(msg + len, sizeof(msg) - len, " BG_PERIOD_COMPLETE");
         len = DE_MIN(sizeof(msg), len);
      }
#ifdef USE_NEW_AGE
      wifiEngineState.scan_count++;
#endif
   }
   if(ind->flags &SCAN_NOTIFICATION_FLAG_HIT) {
      if(TRACE_ENABLED(TR_SCAN)) {
         len += DE_SNPRINTF(msg + len, sizeof(msg) - len, " HIT");
         len = DE_MIN(sizeof(msg), len);
      }
   }
   if(ind->flags & SCAN_NOTIFICATION_FLAG_DIRECT_SCAN_JOB) {
      direct_scan = TRUE;
      if(TRACE_ENABLED(TR_SCAN)) {
         len += DE_SNPRINTF(msg + len, sizeof(msg) - len, " DIRECT_SCAN_JOB");
         len = DE_MIN(sizeof(msg), len);
      }
#ifdef USE_NEW_AGE
      wifiEngineState.scan_count++;
#endif      
   }
   DE_TRACE_STRING(TR_SCAN, "SCANNOTIFICATION_IND: %s\n", msg);

#if 0
   /* XXX how is this supposed to work */
   rConnectionPolicy* c;
   c = (rConnectionPolicy*) Registry_GetProperty(ID_connectionPolicy);
   if ((c->disconnectedScanInterval == 0 && !wei_is_80211_connected()) ||
       (c->connectedScanPeriod == 0) && wei_is_80211_connected()) {
      wifiEngineState.scan_count++;
   }
#endif

   if((job_complete && direct_scan) || (bg_job_complete))
   {

      /* Reset scan indication filter. */
      last_scan_channel = 0;
      
      /* Notify roaming agent that a trig scan is complete */ 
      wei_roam_notify_scan_complete();

      DriverEnvironment_indicate(WE_IND_SCAN_COMPLETE, ind, sizeof(*ind));
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_reinitialize_scan(void)
{
   rConnectionPolicy* c;
   rBasicWiFiProperties*   basic;   

   basic = (rBasicWiFiProperties*) Registry_GetProperty(ID_basic);
   c = (rConnectionPolicy*) &basic->connectionPolicy;

   cache_configure_scan(SHORT_PREAMBLE, 11, c->probesPerChannel, 
                        basic->scanNotificationPolicy,
                        c->disconnectedScanInterval, 
                        c->probeDelay, c->passiveScanTimeouts.minChannelTime,
                        c->passiveScanTimeouts.maxChannelTime,
                        c->activeScanTimeouts.minChannelTime,
                        c->activeScanTimeouts.maxChannelTime,
                        c->connectedScanPeriod,
                        c->connectedScanTimeouts.minChannelTime,
                        c->connectedScanTimeouts.maxChannelTime,
                        c->max_disconnectedScanInterval,
                        c->max_connectedScanPeriod, 
                        (uint8_t) c->periodicScanRepetition);
   
   return WIFI_ENGINE_SUCCESS;
}




int wei_initialize_scan(void)
{
   m80211_ie_ssid_t bcast_ssid;
   m80211_mac_addr_t bcast_bssid;
   rBasicWiFiProperties*   basic;
   rConnectionPolicy* c;
   rScanPolicy * s;
   struct scan_job_t *sj;
   m80211_nrp_mlme_add_scanfilter_req_t *sf = NULL;   

   basic = (rBasicWiFiProperties*) Registry_GetProperty(ID_basic);
   c = (rConnectionPolicy*) &basic->connectionPolicy;
   s = (rScanPolicy*) &basic->scanPolicy;
   
   DE_MEMSET(&bcast_ssid, 0, sizeof bcast_ssid); /* IE id for ssid is 0 */
   DE_MEMSET(&bcast_bssid, 0xFF, sizeof bcast_bssid);

   cache_clear_scan();
   cache_configure_scan(SHORT_PREAMBLE, 11, c->probesPerChannel, 
                        basic->scanNotificationPolicy,
                        c->disconnectedScanInterval, 
                        c->probeDelay, c->passiveScanTimeouts.minChannelTime,
                        c->passiveScanTimeouts.maxChannelTime,
                        c->activeScanTimeouts.minChannelTime,
                        c->activeScanTimeouts.maxChannelTime,
                        c->connectedScanPeriod,
                        c->connectedScanTimeouts.minChannelTime,
                        c->connectedScanTimeouts.maxChannelTime,
                        c->max_disconnectedScanInterval,
                        c->max_connectedScanPeriod, 
                        (uint8_t) c->periodicScanRepetition);

   if(basic->defaultScanJobDisposition)
   {   
      if (s->bssType == 1 || s->bssType == 2) 
      {
         DE_TRACE_STRING(TR_NOISE, "Filter on bss type in default scan job. Keep %s only.\n", s->bssType == 2 ? "IBSS" : "BSS");
         sf = cache_add_scan_filter(s->bssType,
   				 -150,  /* some low rssi */
   				 0,     /* some low snr */
   				 0);    /* threshold type absolute */
      }

      sj = cache_add_scan_job(bcast_ssid, bcast_bssid, basic->activeScanMode,
   			   basic->regionalChannels, ANY_MODE, 1, 0, 
   			   sf, 1);
      cache_set_scan_job_state(sj, 0);
   }
   return WIFI_ENGINE_SUCCESS;
}

int wei_shutdown_scan(void)
{
   return WIFI_ENGINE_SUCCESS;
}

int wei_reconfigure_scan(void)
{
   int i;
   int status;
   rBasicWiFiProperties*   basic;

   basic = (rBasicWiFiProperties*) Registry_GetProperty(ID_basic);
   
   wei_configure_scan(&scan_state.sp, NULL);

   if(basic->defaultScanJobDisposition)
   {  
      /* Add scan filters first since they're referenced by the scan jobs */
      for(i = 0; i < MAX_SCAN_FILTERS; i++) {
         m80211_nrp_mlme_add_scanfilter_req_t* sf = &scan_state.sf[i];
         
         /* Empty slot, then continue */
         if(sf->filter_id == UNUSEDID)
            continue;

         status = wei_add_scan_filter(sf, NULL);
         if(status != WIFI_ENGINE_SUCCESS)
            DE_TRACE_INT(TR_NOISE, "wei_add_scan_filter=%d\n",  status);
      }
      
      /* Add scan jobs and set state */
      for(i = 0; i < MAX_SCAN_JOBS; i++) {
         struct scan_job_t* sj = &scan_state.sj[i];
         
         /* Empty slot, then continue */
         if(JOBUNUSED(sj))
            continue;
         
         status = wei_add_scan_job(sj, NULL);

         if(status != WIFI_ENGINE_SUCCESS)
            DE_TRACE_INT(TR_NOISE, "wei_add_scan_job=%d\n",  status);

         /* Skip setting scan job state for SUSPENDED jobs, since that's the
          * default value.
          */
         if(!sj->state)
            continue;
         
         status = wei_set_scan_job_state(sj, NULL);
         
         if(status != WIFI_ENGINE_SUCCESS)
            DE_TRACE_INT(TR_NOISE, "wei_set_scan_job_state=%d\n",  status);
      }
   }
   return WIFI_ENGINE_SUCCESS;
}

static bool_t m80211_identical_ie(m80211_ie_format_t * a, m80211_ie_format_t * b)
{
   bool_t rc;

   if ( (a==NULL) || (b==NULL) )
   {
      return FALSE;
   }

   if (a->hdr.id == b->hdr.id)
   {
      if (a->hdr.id == M80211_IE_ID_NOT_USED)
      {
         rc = TRUE;
      }
      else
      {
         rc = MEMCMP(a,b,a->hdr.len+sizeof(a->hdr)) == 0 ? TRUE : FALSE;
      }
   }
   else
   {
      rc = FALSE;
   }
   return rc;
}


#if DE_PROTECT_FROM_DUP_SCAN_INDS == CFG_ON
int wei_exclude_from_scan(m80211_mac_addr_t* bssid_p, m80211_ie_ssid_t* ssid_p, int current_channel)
{
   #define WRAP_MASK (sizeof(duplicate_filter)/sizeof(duplicate_filter[0]) - 1) 

   typedef struct
   {
      m80211_mac_addr_t bssid;
      m80211_ie_ssid_t  ssid;
   } duplicate_filter_entry_t;

   /* Number of filter entries must be 2^x */
   static duplicate_filter_entry_t duplicate_filter[1<<5];  
   static int filter_wr_idx;
   static int filter_rd_idx;

   int i;

   /* Just filter out AP's on the same channel. */
   if (last_scan_channel != current_channel)
   {
      filter_wr_idx = 0;
      filter_rd_idx = 0;
      last_scan_channel = current_channel;
   }

   /* The filter is a circular buffer. */
   for(i=filter_rd_idx; i!=filter_wr_idx; i=(WRAP_MASK & (i+1)))
   {
      if ((MEMCMP(&duplicate_filter[i].bssid, bssid_p, sizeof(duplicate_filter[0].bssid)) == 0)
        && m80211_identical_ie((m80211_ie_format_t*)&duplicate_filter[i].ssid, 
                               (m80211_ie_format_t*)ssid_p) )
      {
         /* We have already seen the ap on this channel. */
         return TRUE;
      }
   }
   
   /* Save this AP. */
   MEMCPY(&duplicate_filter[filter_wr_idx].bssid, bssid_p, sizeof(duplicate_filter[0].bssid));
   MEMCPY(&duplicate_filter[filter_wr_idx].ssid,  ssid_p,  sizeof(duplicate_filter[0].ssid));

   filter_wr_idx = (WRAP_MASK & (filter_wr_idx + 1));

   /* The circular buffer is full, reuse the oldest entry. */
   if (filter_rd_idx == filter_wr_idx)
   {
      filter_rd_idx = (WRAP_MASK & (filter_rd_idx + 1));
   }
   return FALSE;
}
#endif


/** @} */ /* End of we_scan group */


