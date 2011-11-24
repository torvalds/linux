#ifndef _we_scan_suite_h_
#define _we_scan_suite_h_

#include "driverenv.h"
#include "we_cm.h"

typedef struct scan_suite_t 
{
   struct {
      uint32_t id;
      m80211_ie_ssid_t ssid;
      m80211_mac_addr_t bssid;
      uint8_t type;
      channel_list_t ch_list;
      int flags;
      uint8_t prio;
      uint8_t ap_exclude;
      int filter_id;
      uint8_t run_every_nth_period;
   } scan_job;
   
   int use_scan_filter;

   struct {
      uint32_t id;
      uint8_t bss_type;
      int32_t rssi_thr;
      uint32_t snr_thr;
      uint16_t threshold_type;
   } scan_filter;

   iobject_t *scan_ind_h;
   iobject_t *scan_complete_h;

   void *priv;
} scan_suite_s;


/*************************************************/

int WiFiEngine_CreateScanSuite(struct scan_suite_t **suite);
int WiFiEngine_ActivateScanSuite(scan_suite_s *ss, i_func_t scan_ind_cb, i_func_t scan_complete_cb);
void WiFiEngine_DeactivateScanSuite(scan_suite_s *ss);

#endif /* _we_scan_suite_h_ */

