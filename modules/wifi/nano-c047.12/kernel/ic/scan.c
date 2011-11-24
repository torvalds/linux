/* Upper network driver interface for Nanoradio Linux WiFi driver */
/* $Id: scan.c 16054 2010-09-06 10:37:00Z joda $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <asm/byteorder.h>

#include "nanoparam.h"
#include "nanonet.h"
#include "nanoutil.h"
#include "nanoproc.h"

#include "driverenv.h"

typedef struct {
      struct net_device *dev;
      int return_code;
} info_container_t;


static int generic_synchronous_cb(we_cb_container_t *cbc)
{
   info_container_t *ic = cbc->ctx;
   struct net_device *dev = ic->dev;
   struct nrx_softc *sc = netdev_priv(dev);

   KDEBUG(TRACE, "ENTRY");

   ic->return_code = -EINVAL;
   switch (cbc->status) {
      case 0:
         KDEBUG(TRACE, "Firmware success");
         ic->return_code = 0;
         break;
      case 1:
         KDEBUG(ERROR, "Firmware failed");
         break;
      case 2:
         KDEBUG(ERROR, "Firmware busy");
         ic->return_code = -EBUSY;
         break;
      case WIFI_ENGINE_FAILURE_ABORT:
         KDEBUG(ERROR, "WIFI_ENGINE_FAILURE_ABORT");
         break;
      case WIFI_ENGINE_FAILURE_RESOURCES:
         KDEBUG(ERROR, "WIFI_ENGINE_FAILURE_RESOURCES");
         break;
      default:
         DE_BUG_ON(1, "unknown status %d", cbc->status);
         break;
   }

   wake_up(&sc->mib_wait_queue);
   return 1;
}

/* Returns 0 on success */
#define FUNCTION_BODY(func, args...)				\
   int ret;							\
   we_cb_container_t *cbc;					\
   struct nrx_softc *sc = netdev_priv(dev);			\
   info_container_t ic;						\
								\
   KDEBUG(TRACE, "ENTRY");					\
								\
   ic.dev = dev;						\
   ic.return_code = -EIO;					\
								\
   if(nrx_test_flag(sc, NRX_FLAG_SHUTDOWN)) {			\
      KDEBUG(TRACE, "called when shutdown");			\
      return -EWOULDBLOCK;					\
   }								\
								\
   cbc = WiFiEngine_BuildCBC(generic_synchronous_cb,		\
                             &ic, sizeof(ic), FALSE);		\
   ret = func(args);						\
   if(ret != WIFI_ENGINE_SUCCESS) {				\
      WiFiEngine_FreeCBC(cbc);                                  \
      KDEBUG(TRACE, "EXIT EIO");				\
      return -EIO;						\
   }								\
								\
   if(wait_event_interruptible(sc->mib_wait_queue,		\
                               ic.return_code != -EIO) != 0) {	\
      WiFiEngine_CancelCallback(cbc);                           \
      return -ERESTARTSYS;					\
   }								\
								\
   return ic.return_code;




int synchronous_ConfigureScan(struct net_device *dev,
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
                              uint8_t  period_repetition)
{
   FUNCTION_BODY(WiFiEngine_ConfigureScan, 
                 preamble,
                 rate,
                 probes_per_ch,
                 notif_pol,
                 scan_period,
                 probe_delay,
                 pa_min_ch_time,
                 pa_max_ch_time,
                 ac_min_ch_time,
                 ac_max_ch_time,
                 as_scan_period,
                 as_min_ch_time,
                 as_max_ch_time,
                 max_scan_period,
                 max_as_scan_period,
                 period_repetition,
                 cbc);
}


int synchronous_AddScanFilter(struct net_device *dev,
                              int32_t *sf_id,
                              WiFiEngine_bss_type_t bss_type,
                              int32_t rssi_thr,
                              uint32_t snr_thr,
                              uint16_t threshold_type)
{
   FUNCTION_BODY(WiFiEngine_AddScanFilter,
                 sf_id,
                 bss_type,
                 rssi_thr,
                 snr_thr,
                 threshold_type,
                 cbc);
}


int synchronous_RemoveScanFilter(struct net_device *dev,
                                 int32_t sf_id)
{
   FUNCTION_BODY(WiFiEngine_RemoveScanFilter,
                 sf_id,
                 cbc);
}


int synchronous_AddScanJob(struct net_device *dev,
                           int32_t *sj_id,
                           m80211_ie_ssid_t ssid,
                           m80211_mac_addr_t bssid,
                           uint8_t scan_type,
                           channel_list_t ch_list,
                           int flags,
                           uint8_t prio,
                           uint8_t ap_exclude,
                           int sf_id,
                           uint8_t run_every_nth_period)
{
   FUNCTION_BODY(WiFiEngine_AddScanJob,
                 sj_id,
                 ssid,
                 bssid,
                 scan_type,
                 ch_list,
                 flags,
                 prio,
                 ap_exclude,
                 sf_id,
                 run_every_nth_period,
                 cbc);
}


int synchronous_RemoveScanJob(struct net_device *dev,
                              int32_t sj_id)
{
   FUNCTION_BODY(WiFiEngine_RemoveScanJob,
                 sj_id,
                 cbc);
}


int synchronous_SetScanJobState(struct net_device *dev,
                                int32_t sj_id, 
                                uint8_t state)
{
   FUNCTION_BODY(WiFiEngine_SetScanJobState,
                 sj_id, 
                 state,
                 cbc);
}

#undef FUNCTION_BODY
