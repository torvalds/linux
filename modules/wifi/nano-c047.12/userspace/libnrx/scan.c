/* Copyright (C) 2007 Nanoradio AB */
/* $Id: scan.c 15537 2010-06-03 15:53:04Z joda $ */

#include "nrx_priv.h"
#include "mac_mib_defs.h"

/** \defgroup SCAN Scan
 * \brief The Scan functions are used to configure and set up scan jobs based on scan filters 
 * to provide for both rapid scanning and scanning to conserve power in different situations.
 * 	  
 * \b Definitions:
 * 
 * \par Scan configuration	
 * Configuration parameters common to all scan jobs.
 * 
 * \par Scan job
 * A scan job scans for a BSS/IBSS according to the common parameters in the scan configuration, 
 * specific job parameters and an associated scan filter. A scan job may be configured to run in 
 * connected mode or disconnected mode, or in both modes. It can be started as a directed scan 
 * or set up as a periodic scan. By default a new scan job is suspended and must be activated by the host.
 * 
 * \par Scan filter
 * Specific set of filter definitions associated with a scan job 
 * and used to filter out specific BSS:s/IBSS:s
 * 
 * \par Active scan	
 * The station transmits a probe request frame with a specific SSID/BSSID on a channel. If there is 
 * a BSS on the channel that matches the SSID, the AP in that BSS will respond by sending a probe 
 * response frame to the scanning station. Alternatively, in an IBSS, the station that sent the latest 
 * Beacon frame in that IBSS will respond.
 * 
 * \par Passive scan	
 * The station listens for beacons on one channel at a time, and extracts a description of any BSS 
 * available from the frames received. 
 * 
 * \par Directed scan
 * A scan job trigged by the host application with certain scan parameters in order to get an updated 
 * list of available networks.
 * 
 * \par Periodic scan
 * One or more scan jobs that are configured to run periodically.
 * 
 * See the <em>NRX700 Scan Function Description</em> (15516/2-NRX700) for further
 * details about the NRX700 Scan implementation. 
 */

/* CHECK: In general, which calls, if any, are reset on disassociation and what
 *        other such limitations exist?
 */


/*!
 * @ingroup SCAN
 * @brief <b>Configure parameters for scan</b>
 *
 * Scans can be active (probe request) or passive (listening for
 * beacons) and can be directed (triggered by the application) or
 * periodic (triggered periodically by the device or the driver).  A
 * special case occurs when the device is associated. In this case
 * only active scans are allowed and since the device has to
 * leave the current channel when scanning it will adversely effect
 * traffic throughput. For this reason the scan period and timeouts
 * are specified separately for the associated case. Scan jobs will
 * remain in effect while associated and the scan job parameters
 * for scan while associated will come into effect. The normal
 * scan job parameters will be used again when the association is
 * terminated.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param preamble Long or short preamble for probe requests. Only valid
 *                 for active scan.
 * @param rate Rate in the "supported rates" encoding from the 802.11
 *             standard. This rate is used for probe
 *             requests. Only valid for active scan. A rate value of 0
 *             means that the firmware will pick the rate for probe
 *             requests as it sees fit.
 * @param probes_per_ch Number of probe requests sent on each channel.
 *                    Probe requests will be sent with an interval time
 *                    of min_ch_time. Minimum value is 1 and maximum is 255.
 *                    Only valid for active scan.
 *                    A higher value increases the likelyhood that the APs
 *                    on a channel will hear a probe request in case of
 *                    bad radio conditions.
 *                    The probe request will be directed to the ssid specified
 *                    in the scan job. If an ssid is not specified for the scan
 *                    job, then the probe request will be broadcasted.
 * @param sn_pol Bitmask that defines the notification policy. This defines
 *           when the host should be invoked. The host can be notified 
 * - as soon as the first network is found (FIRST_HIT)
 *   (notified at most once per scan job per scan period). 
 * - when a scan job is complete (JOB_COMPLETE)
 *   (notified once per scan job per scan period)
 * - when all scan jobs in a scan period is complete (ALL_DONE)
 *   (notified once per scan period)
 * - when a scan job is complete and a network is found (JOB_COMPLETE_WITH_HIT)
 *   (notified once per scan job per scan period)
 * The firmware will generate notifications to the driver based on the
 * notification policy set with nrx_conf_scan().  The driver will
 * notify the NRXAPI library which will in turn execute any registered
 * notification callbacks based on the notification policy set in
 * nrx_register_scan_notification_handler(). Because of this,
 * notification handlers registered with the library will only work if
 * the selected notification policy has also been configured in
 * firmware with nrx_conf_scan().
 *
 * @param scan_period Time in ms between scans. If this parameter is 0
 *                    the device will never scan by itself. Otherwise it
 *                    will periodically initiate a new scan when this
 *                    period has passed since the last scan was completed. 
 *                    This parameter is not used when the device is associated.
 *                    The maximum value is 2^32-1 ms.
 * @param probe_delay Time in microseconds spent on the channel before sending 
 *                    the first probe request. This parameter will only be used 
 *                    for active scan.
 *                    The minimum value is 1 and maximum is 2^16-1 microseconds.
 * @param pa_ch_time The time in TU (1 TU = 1024 microseconds) spent on
 *                    each channel during passiv scanning. The channel will be
 *                    changed when this amount of time has passed.
 *                    This parameter is not used when the device is
 *                    associated.  The minimum value is 0. The maximum value
 *                    is 2^16-1 TU.
 * @param ac_min_ch_time The minimum time in TU (1 TU = 1024 microseconds) spent on
 *                    each channel
 *                    during active scanning. If the medium is idle during
 *                    this time, scanning proceeds to the next channel.
 *                    Otherwise the device keeps listening for probe responses
 *                    in the channel for the ac_max_ch_time. This
 *                    parameter is not used when the device is
 *                    associated.  The minimum value is 0. The maximum
 *                    value is 2^16-1 TU.
 * @param ac_max_ch_time The maximum time in TU (1 TU = 1024 microseconds) spent on
 *                    each channel
 *                    during active scanning. This
 *                    parameter is not used when the device is
 *                    associated.  The minimum value is 0. The maximum
 *                    value is 2^16-1 TU.  The value must be larger than
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
 *                    The maximum value is 2^32-1 ms. 
  * @param as_min_ch_time The minimum time in TU (1 TU = 1024 microseconds) spent on
 *                    each channel when the device is associated. If the medium is 
 *                    idle during this time, scanning proceeds to the next channel.
 *                    Otherwise the device keeps listening for probe responses
 *                    in the channel for as_max_ch_time.
 *                    This parameter is only used when the device is associated.
 *                    The minimum value is 0. The maximum value is 2^16-1 TU.
 * @param as_max_ch_time The maximum time in TU (1 TU = 1024 microseconds) spent on
 *                    each channel when the device
 *                    is associated. This parameter is only used when the device is
 *                    associated.
 *                    The minimum value is 0. The maximum value is 2^16-1 TU.
 *                    The value must be larger than as_min_ch_time.
 * @param max_scan_period Maximum time in ms between scan periods when disconnected. 
 *                    This parameter is used as a limitation of the scan period when 
 *                    the nominal scan period doubles after every period_repetition.
 *                    The maximum value is 2^32-1 ms. 
 *                    Scenario: scan_period = 5000, max_scan_period = 50000
 *                       gives the following periods 5000, 10000, 20000, 40000, 50000
 *                       period doubled until 40000 reached, then it will stay with 50000 
 * @param max_as_scan_period Maximum time in ms between scan periods when connected. 
 *                    This parameter is used as a limitation of the scan period when 
 *                    the nominal scan period doubles after every period_repetition.
 *                    The maximum value is 2^32-1 ms. 
 *                    Scenario: as_scan_period = 5000, max_as_scan_period = 50000
 *                       gives the following periods 5000, 10000, 20000, 40000, 50000
 *                       period doubled until 40000 reached, then it will stay with 50000 
 *
 * @param period_repetition Number of repetitions of a scan period before it is doubled.
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 *  
 */
int
nrx_conf_scan(nrx_context ctx,
              nrx_preamble_type_t preamble,
              uint8_t rate,
              uint8_t probes_per_ch,
              nrx_sn_pol_t sn_pol,
              uint32_t scan_period,
              uint16_t probe_delay,
              uint16_t pa_ch_time,
              uint16_t ac_min_ch_time,
              uint16_t ac_max_ch_time,
              uint32_t as_scan_period,
              uint16_t as_min_ch_time,
              uint16_t as_max_ch_time,
              uint32_t max_scan_period,
              uint32_t max_as_scan_period,
              uint8_t  period_repetition)
{
   struct nrx_ioc_scan_conf param;
   NRX_ASSERT(ctx);
   NRX_CHECK(probes_per_ch>=1);
   NRX_CHECK(ac_min_ch_time <= ac_max_ch_time);
   NRX_CHECK(as_min_ch_time <= as_max_ch_time);
   NRX_CHECK(probe_delay>=1);
   NRX_CHECK(period_repetition>=1);
   NRX_CHECK(rate == 0 || _nrx_is_supp_rate(rate));
   
#define MS2TU(T) ((T) - (T) / 43)

   param.preamble       = (int)preamble;
   param.rate           = rate;
   param.probes_per_ch  = probes_per_ch;
   param.notif_pol      = sn_pol;
   param.scan_period    = MS2TU(scan_period);
   param.probe_delay    = probe_delay;
   param.pa_min_ch_time = pa_ch_time;
   param.pa_max_ch_time = pa_ch_time;
   param.ac_min_ch_time = ac_min_ch_time;
   param.ac_max_ch_time = ac_max_ch_time;
   param.as_scan_period = MS2TU(as_scan_period);
   param.as_min_ch_time = as_min_ch_time;
   param.as_max_ch_time = as_max_ch_time;
   param.max_scan_period = MS2TU(max_scan_period);
   param.max_as_scan_period = MS2TU(max_as_scan_period);
   param.period_repetition = period_repetition;
   return nrx_nrxioctl(ctx, NRXIOWSCANCONF, &param.ioc);
}

/*!
 * @ingroup SCAN
 * @brief <b>Start or stop a scan job</b>
 *
 * Scan jobs added with nrx_add_scan_job() will be stopped by default. The scan
 * job state must be set to RUNNING in order to start the scan job. When the
 * scan job is started it will executed according to the configured time
 * parameters. The scan job state can be set to STOPPED to stop the scan job.
 * Each scan job can be started/stopped independently.
 *
 * During ongoing scanning, any scan jobs that are set to state RUNNING will
 * begin execution during the next scan period. But, if the scan job that is
 * set to RUNNING state has lower priority than the currently running scan job,
 * it will be executed during the current scan period.
 *
 * The first scan job which is set to state running scan job will start the
 * scan period.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sj_id A scan job id. This identifies the scan
 *              job to be started/stopped.
 *
 * @param state The state that the scan job should be set to.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 *
 */
int
nrx_set_scan_job_state(nrx_context ctx,
                       int32_t sj_id,
                       nrx_scan_job_state_t state)
{
   struct nrx_ioc_scan_job_state param;
   NRX_ASSERT(ctx);
   param.sj_id = sj_id;
   param.state = state;
   return nrx_nrxioctl(ctx, NRXIOWSCANJOBSTATE, &param.ioc);
}

/*!
 * @ingroup SCAN
 * @brief <b>Add a scan filter</b>
 *
 * The scan filters reside in the device. When host wakeup is enabled
 * the device will only wake the host up on results that pass the scan
 * filter coupled to the scan job. Filtered scan results are silently
 * discarded by the device and will not be visible to the
 * host. Several scan filters can be defined. They are identified by
 * the sf_id parameter. There is a
 * limit to the number of scan filters that can exist in the device so
 * this call may fail if the current limit is passed. 
 * The scan filters will be associated to scan jobs (see nrx_add_scan_job) and
 * will therefore be used whenever the job is executued (periodic or
 * triggered).
 *
 * The application have to keep track of all the currently configured scan
 * filters.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sf_id The scan filter id output buffer. This identifies the scan
 *              filter so that several scan filters can be defined.
 *              The id value is filled in by this function call.
 * @param bss_type Bitmask defining which BSS type to scan for (infrastructure and/or ad-hoc).
 * @param rssi_thr RSSI threshold. Pass results with a higher RSSI than this
 *                 value [dBm] (signed). Minimum value is -120 dBm and maximum is 100 dBm.
 *                 Values > 0 valid for RELATIVE thresholds only
 * @param snr_thr SNR threshold. Pass results with a higher SNR than this
 *                 value. Minimum value is 0 and maximum 40.
 * @param threshold_type Type of threshold, ABSOLUTE(0) or RELATIVE(1) to the connected STA
 *                       Relative type only active when connected.
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * 
 */
int 
nrx_add_scan_filter(nrx_context ctx,
		    int32_t *sf_id,
		    nrx_bss_type_t bss_type,
		    int32_t rssi_thr,
		    uint32_t snr_thr,
          uint16_t threshold_type)
{
   int ret;
   struct nrx_ioc_scan_add_filter param;
   NRX_ASSERT(ctx);
   NRX_ASSERT(sf_id);
   NRX_CHECK(rssi_thr >= -120 && rssi_thr <= 100);
   NRX_CHECK(snr_thr >= 0 && snr_thr <= 40);

   param.bss_type = bss_type;
   param.rssi_thr = rssi_thr;
   param.snr_thr  = snr_thr;
   param.threshold_type  = threshold_type;
   ret = nrx_nrxioctl(ctx, NRXIOWRSCANADDFILTER, &param.ioc);
   if (ret == 0)
      *sf_id = param.sf_id;
   else
      *sf_id = 0;
   return ret;
}

/*! 
 * @ingroup SCAN
 * @brief <b>Delete a scan filter</b>
 *
 * The scan job tied to the scan filter must be deleted before the filter
 * can be deleted for proper operation. If a scan filter that is in use
 * by a scan job is deleted then the scan job will continue to execute
 * without a scan filter. Such a scan job will use any new filters that have
 * the same id as a previously deleted filter. 
 * 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sf_id A user supplied scan filter id. This identifies the scan
 *              filter to be deleted. The scan filter id -1 is reserved and cannot be used.
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * 
 */
int
nrx_delete_scan_filter(nrx_context ctx,
		       int sf_id)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);
   param.value = sf_id;
   return nrx_nrxioctl(ctx, NRXIOWSCANDELFILTER, &param.ioc);
}

/*!
 * @ingroup SCAN
 * @brief <b>Add a scan job</b>
 *
 * The device maintains a list of scan jobs that may be executed
 * sequentially or be aggregated in any way useful according to the parameters
 * setup in
 * nrx_conf_scan(). When a scan job is executed probe requests for the
 * requested SSID will be sent on the channels specified in the job if
 * active scan is enabled. Otherwise the device will passively listen
 * for beacons on the specified channels. A scan job is identified by
 * the sj_id parameter. The scan jobs will
 * be executed in priority order (higher prioiry values run first). If two
 * scan jobs have the same priority their execution order is undefined.
 * There is a limit to the number of scan jobs that can exist in the device so
 * this call may fail if the current limit is passed.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sj_id The scan job id output buffer. This identifies the scan
 *              job so that several scan jobs can be defined. 
 *              The ID value is filled in by this function call.
 * @param ssid Defines the SSID that the scan job should look for. 
 *             A zero-length SSID counts as a "broadcast" SSID
 *             that will cause all SSIDs to be scanned for.
 * @param bssid A BSSID to scan for. If this is set to the broadcast BSSID
 *              (ff:ff:ff:ff:ff:ff) then all BSSIDs will be scanned (subject
 *              to the SSID parameter). BSSID-specific scan can be performed
 *              be using zero-length SSID. Otherwise the results have to match
 *              both ssid and bssid.
 * @param scan_type Active or passive scan. The device will always perform
 *             active scans when it is associated. To prevent execution of a
 *             job when associated the flags parameter should be used.
 * @param ch_list Defines the channels to scan. The channel list is subject
 *                to further filtering due to 802.11d restrictions.
 * @param flags Bitmask that defines whether the scan job should be used when
 *              associated and/or when not associated.
 * @param prio  Priority value for the scan job. Jobs with higher priority
 *              values are run before jobs with lower priority values.
 *              Valid priorities are in the range 1-128, e.g 128 is prioritized.
 * @param ap_exclude May be either 0 or 1. When set to 1 the AP which the
 *                   device is associated with should be exluded from the scan
 *                   result. This means that scan result information and 
 *                   scan notifications triggered by this AP will not be 
 *                   forwarded to the host. However, this AP will still be 
 *                   included in the scan results list presented by e.g
 *                   nrx_get_scan_list().
 *
 *
 * @param sf_id Defines the scan filter to use for the scan job. The value
 *              should be set to -1 if no filter should be used.
 * @param run_every_nth_period Defines how often the scan job should be executed
 *
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - TODO if no more scan jobs can be created.
 * 
 */
int
nrx_add_scan_job(nrx_context ctx,
                 int32_t *sj_id,
                 nrx_ssid_t ssid,
		 nrx_mac_addr_t bssid,
                 nrx_scan_type_t scan_type,
                 nrx_ch_list_t ch_list,
                 nrx_job_flags_t flags,
                 uint8_t prio,
                 uint8_t ap_exclude,
                 int sf_id,
                 uint8_t run_every_nth_period)
{
   int ret;
   struct nrx_ioc_scan_add_job param;
   int i;
   NRX_ASSERT(ctx);
   NRX_ASSERT(sj_id);
   NRX_CHECK(prio>=1 && prio<=128);
   NRX_CHECK(ap_exclude==0 || ap_exclude==1);

   if(ssid.ssid_len > sizeof(param.ssid))
      return EINVAL;
   memcpy(param.ssid, ssid.ssid, ssid.ssid_len);
   param.ssid_len = ssid.ssid_len;

   memcpy(param.bssid, bssid.octet, sizeof(bssid.octet));

   param.scan_type  = scan_type;

   if(ch_list.len > sizeof(param.channels))
      return EINVAL;
   
   for(i = 0; i < ch_list.len; i++) {
      if((ch_list.channel[i] & 0xff00) != 0)
         return EINVAL;
      param.channels[i] = ch_list.channel[i];
   }
   param.channels_len = i;
   param.flags      = flags;
   param.prio       = prio;
   param.ap_exclude = ap_exclude;
   param.sf_id      = sf_id;
   param.run_every_nth_period = run_every_nth_period;
   ret = nrx_nrxioctl(ctx, NRXIOWRSCANADDJOB, &param.ioc);
   if (ret == 0)
      *sj_id = param.sj_id;
   else
      *sj_id = 0;
   return ret;
}

/*!
 * @ingroup SCAN
 * @brief <b>Delete a scan job</b>
 *
 * If the scan job is deleted during ongoing scan an error code will be
 * returned. In this case the application can retry again later until the
 * the deletion is successful.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sj_id A scan job id. This identifies the scan
 *              job to be deleted. 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - EBUSY if the scan job is currently executing.
 * 
 */
int
nrx_delete_scan_job(nrx_context ctx,
                    int32_t sj_id)
{
   struct nrx_ioc_int32_t param;
   NRX_ASSERT(ctx);

   param.value = sj_id;
   return nrx_nrxioctl(ctx, NRXIOWSCANDELJOB, &param.ioc);
}


struct nrx_scan_notification_t {
      nrx_sn_pol_t sn_pol;
      nrx_callback_t handler;
      void *user_data;
};

/*!
 * @internal
 *
 * This function will filter so that only messages subscribed for 
 * are passed along to the user's callback.  
 *
 */
static int
nrx_handle_scan_notification(nrx_context ctx,
                             int operation,
                             void *event_data,
                             size_t event_data_size,
                             void *user_data)
{
   struct nrx_scan_notification_t *priv = (struct nrx_scan_notification_t *)user_data;
   struct nrx_we_custom_data *cdata = (struct nrx_we_custom_data *)event_data;
   nrx_scan_notif_t notif;
   int i;
   
   if(operation == NRX_CB_CANCEL) {
      (*priv->handler)(ctx, NRX_CB_CANCEL, NULL, 0, priv->user_data);
      free(priv);
      return 0;
   }
   for(i = 0; i < cdata->nvar; i++) {
      if(strcmp(cdata->var[i], "policy") == 0) {
         notif.pol = strtoul(cdata->val[i], NULL, 0);
      }
      else if(strcmp(cdata->var[i], "jobid") == 0) {
         notif.sj_id = strtoul(cdata->val[i], NULL, 0);
      }
   }
   if (((int)notif.pol & (int)priv->sn_pol) != 0) {
      (*priv->handler)(ctx, operation, &notif, 
                          sizeof(notif), 
                          priv->user_data);
   } else {
      /* Message format incorrect. This should never happen */
   }
   return 0;
}

/*!
 * @ingroup SCAN
 * @brief <b>Get notification of completed scans</b>
 *
 * The caller will be notified when a scan sequence is complete
 * (periodic or manually triggered). The current scan results can then be
 * retrieved by a call to nrx_get_scan_list(). The callback is invoked every
 * time (until disabled) when the condition specified by sn_pol is met.
 *
 * This function can be called several times, e.g. with different notification
 * policies for different callbacks. 
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sn_pol Bitmask that defines the notification policy. This
 * defines when the callback should be invoked. Observe that the
 * callback will not be invoked unless the bitmask is a subset of the
 * notification policy used in the scan configuration command. The
 * callback can be notified
 * - as soon as the first network is found (FIRST_HIT) (notified at
 *   most once per scan job per scan period). This policy only
 *   works when a scan filter has been configured.
 * - when a scan job is complete (JOB_COMPLETE) (notified once per
 *   scan job per scan period)
 * - when all scan jobs in a scan period is complete (ALL_DONE)
 *   (notified once per scan period)
 * - when a scan job is complete and a network is found
 *    (JOB_COMPLETE_WITH_HIT) (notified once per scan job per scan
 *    period)
 *
 * @param cb Callback that is invoked to notify the caller that
 *        a directed scan sequence has completed.  The callback is
 *        invoked with operation NRX_CB_TRIGGER on a successful 
 *        notification.
 *        It will be passed a pointer to nrx_scan_notif_t, containing
 *        the policy that triggered the callback and the job id for
 *        the notifying job, for event_data. The job id element is not valid
 *        in case the policy is ALL_DONE.
 *
 * @param cb_ctx Pointer to a user-defined callback context that will
 *               be passed to the callback on invocation. This is for
 *               caller use only, it will not be parsed or modified in
 *               any way by this library. This parameter can be NULL.
 * @return 
 * - Pointer to callback handle if success.
 * - 0 if error.
 *
 */
nrx_callback_handle 
nrx_register_scan_notification_handler(nrx_context ctx,
                                       nrx_sn_pol_t sn_pol,
                                       nrx_callback_t cb,
                                       void *cb_ctx)
{
   struct nrx_scan_notification_t *sn;
   NRX_ASSERT(ctx);
   NRX_ASSERT(cb);

   sn = (struct nrx_scan_notification_t *)malloc(sizeof(struct nrx_scan_notification_t));
   if (sn == NULL)
      return 0;
   sn->sn_pol    = sn_pol;
   sn->handler   = cb;
   sn->user_data = cb_ctx;
   return nrx_register_custom_event(ctx, "SCANNOTIFICATION", 0, 
                                    nrx_handle_scan_notification, sn);
}

/*!
 * @ingroup SCAN
 * @brief <b>Disable scan notifications</b>
 *
 * The scan notification feature is disabled and no further
 * scan notifications will be made.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param handle A handle previously obtained from
 * nrx_register_scan_notification_handler. The handle will no longer be
 * valid after this call.
 * 
 * @return
 * - 0 on success.
 * - EINVAL on invalid parameters, e.g. the callback is not registered.
 */
int
nrx_cancel_scan_notification_handler(nrx_context ctx,
                                     nrx_callback_handle handle)
{
   NRX_ASSERT(ctx);
   NRX_ASSERT(handle);
   return nrx_cancel_custom_event(ctx, handle);
}


/*!
 * @ingroup SCAN
 * @brief <b>Start a directed scan and specify channel interval</b>
 *
 * Any scan complete notification callbacks registered will be triggered
 * when the scan is complete. A directed scan can be performed in
 * parallel with a periodic scan.
 * It is always safe to call this function, when directed scans are
 * prohibited the call will do nothing.
 * A directed scan can trigger scan notifications in the same way as periodic
 * scans since the only difference between directed and period scan is the time
 * when the scan job is executed.
 * Note that scan jobs that are in state SUSPENDED cannot be triggered.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sj_id A scan job id to trigger. 
 * @param channel_interval interval in ms between channels
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - EBUSY if a directed scan is already in progress (RELEASE 5).
 * 
 */
int
nrx_trigger_scan_ex(nrx_context ctx,
                 int32_t sj_id,
                 uint16_t channel_interval)
{
   struct nrx_ioc_trigger_scan param;
   NRX_ASSERT(ctx);

   param.sj_id = sj_id;
   param.channel_interval = channel_interval;
   return nrx_nrxioctl(ctx, NRXIOWSCANTRIGJOB, &param.ioc);
}


/*!
 * @ingroup SCAN
 * @brief <b>Start a directed scan</b>
 *
 * Any scan complete notification callbacks registered will be triggered
 * when the scan is complete. A directed scan can be performed in
 * parallel with a periodic scan.
 * It is always safe to call this function, when directed scans are
 * prohibited the call will do nothing.
 * A directed scan can trigger scan notifications in the same way as periodic
 * scans since the only difference between directed and period scan is the time
 * when the scan job is executed.
 * Note that scan jobs that are in state SUSPENDED cannot be triggered.
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 * @param sj_id A scan job id to trigger. 
 * @return 
 * - 0 on success.
 * - EINVAL on invalid arguments.
 * - EBUSY if a directed scan is already in progress (RELEASE 5).
 * 
 */
int
nrx_trigger_scan(nrx_context ctx,
                 int32_t sj_id)
{
   return nrx_trigger_scan_ex(ctx,
                              sj_id, 
                              100); /* DEFAULT_CHANNEL_INTERVAL */ /* channel_interval; */
}


/*!
 * @ingroup SCAN
 * @brief <b>Flush scan list</b>
 *
 * @param ctx NRX context that was created by the call to nrx_init_context().
 *
 * Flushes the scan list
 *
 * @return 
 * - 0 on success.
 * 
 */
int
nrx_flush_scanlist(nrx_context ctx)
{
   struct nrx_ioc ioc;
   NRX_ASSERT(ctx);
   return nrx_nrxioctl(ctx, NRXIOWSCANLISTFLUSH, &ioc);

}

/*!
 * @ingroup SCAN
 * @internal
 * @deprecated This interface is not available anymore.
 * @brief <b>Administrate SSID Pool</b>
 *
 * Adds/Removes SSID's to/from the SSID-pool in firmware
 *
 * @param action Defines if ssid should be added (NRX_SSID_ADD)
 *        or removed (NRX_SSID_REMOVE) to/from the pool.
 * @param ssid  String containing ssid octets, the string "any" will be passed as a NULL ssid.
 * @return always return EOPNOTSUPP 
 * 
 */
int
nrx_scan_adm_ssid_pool(nrx_context ctx,
                       nrx_ssid_action_t action,
                       nrx_ssid_t ssid)
{
   return EOPNOTSUPP;
}

/*!
 * @ingroup SCAN
 * @internal
 * @deprecated This interface is not available anymore.
 * @brief <b>Administrate connections of SSID to Job </b>
 *
 * Adds/Removes a SSID connection to/from a scan Job in firmware
 *
 * @param action Defines if the ssid should be added (NRX_SSID_ADD) or
 *        removed (NRX_SSID_REMOVE) to/from the scan job.
 * @param job_id Defines to which scan job the ssid shuold be Added/Removed to/from
 * @param ssid  String containing ssid octets, the string "any" will be passed as a NULL ssid.
 * @return always return EOPNOTSUPP 
 * 
 */
int
nrx_scan_adm_job_ssid(nrx_context ctx,
                      nrx_ssid_action_t action,
                      uint32_t job_id,
                      nrx_ssid_t ssid)
{
   return EOPNOTSUPP;
}

int
nrx_scan_get_event(nrx_context context,
                   void *scan_buf,
                   size_t scan_size,
                   void **event,
                   size_t *event_size)
{
   uint16_t len;
   uint8_t *start = (uint8_t *)scan_buf;
   uint8_t *end = start + scan_size;
   uint8_t *p = (uint8_t *)*event;
   
   if(p == NULL) {
      p = start;
   } else {
      /* skip to next entry */
      if(end - p < sizeof(len))
         return E2BIG;
      memcpy(&len, p, sizeof(len));
      p += len;
   }
   if(end - p < sizeof(len))
      return E2BIG;
   memcpy(&len, p, sizeof(len));
   if(end - p < len)
      return E2BIG;
   *event_size = len;
   *event = p;
   return 0;
}
                   
