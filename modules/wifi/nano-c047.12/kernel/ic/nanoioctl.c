/* Upper network driver interface for Nanoradio Linux WiFi driver */
/* $Id: nanoioctl.c 18442 2011-03-22 15:26:38Z joda $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/wireless.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#define HAVE_ETHTOOL
#ifdef HAVE_ETHTOOL
#include <linux/ethtool.h>
#endif
#include <linux/vmalloc.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include "nanoparam.h"
#include "nanonet.h"
#include "nanoutil.h"
#include "nanoproc.h"

#include "wifi_engine.h"
#include "nanoioctl.h"


static int
ns_net_ioctl_command(struct net_device *dev, struct ifreq *ifr, int cmd)
{
   struct nanoioctl ni;
   int error;

   if (!capable(CAP_NET_ADMIN)){
      KDEBUG(ERROR, "EXIT EACCES");
      return -EACCES;
   }
   
   error = copy_from_user(&ni, ifr->ifr_data, sizeof(ni));
   
   if (error) {
      KDEBUG(ERROR, "EXIT EFAULT");
      return -EFAULT;
   }
   if(ni.magic != NR_MAGIC) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
                
   switch(cmd) {
   case SIOCNRXRAWTX:
      error = nrx_raw_tx(dev,ni.data,ni.length);
      break;
      
   case SIOCNRXRAWRX:
      ni.length = sizeof(ni.data);
      error = nrx_raw_rx(dev,ni.data,&ni.length);
      if(error == -ENOENT) {
         /* stupid interface */
         error = 0;
         ni.length = 0;
      }
      break;
      
   }
   if(error != 0)
      return error;
   
   error = copy_to_user(ifr->ifr_data, &ni, sizeof(ni));
   
   if(error) {
      KDEBUG(ERROR, "EXIT EFAULT");
      return -EFAULT;
   }
   
   return 0;
}

/* XXX move this someplace else */
int
nrx_is_ibss(void)
{
   int status;
   WiFiEngine_bss_type_t bssType;

   status = WiFiEngine_GetBSSType(&bssType);
   if(status == WIFI_ENGINE_SUCCESS && bssType == M80211_CAPABILITY_IBSS)
      return 1;

   return 0;
}

#include "registryAccess.h"

static int
nrx_ioctl2(struct net_device *dev, struct ifreq *ifr, int cmd) 
{
   struct nrx_softc *sc = netdev_priv(dev);
   int error;
   unsigned char buf[256];
   struct nrx_ioc *ioc = (struct nrx_ioc*)buf;
   size_t len;

   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);

   CHECK_UNPLUG(dev);

   error = copy_from_user(buf, ifr->ifr_data, sizeof(*ioc));
   if (error) {
      KDEBUG(ERROR, "EXIT EFAULT");
      return -EFAULT;
   }

   if(ioc->magic != NRXIOCMAGIC) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   
   len = NRXIOCSIZE(ioc->cmd);
   if(len > sizeof(buf)) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   if(NRXIOCMODE(ioc->cmd) & NRXIN) {
      error = copy_from_user(buf, ifr->ifr_data, len);
      if (error) {
         KDEBUG(ERROR, "EXIT EFAULT");
         return -EFAULT;
      }
   }

#define MAP_WIFI_ERR(wifi_err, linux_err)                               \
   case wifi_err: do {                                                  \
      KDEBUG(ERROR, "EXIT " #linux_err " (due to " #wifi_err ")");      \
      return -linux_err;                                                \
   } while (0)

#define WIFI_CHECK(func) do {                                           \
   switch (func) {                                                      \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_NOT_SUPPORTED, EOPNOTSUPP);      \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_ABORT, EINTR);                   \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED, EOPNOTSUPP);    \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_DEFER, EAGAIN);                  \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_INVALID_DATA, EINVAL);           \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_RESOURCES, ENOMEM);              \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_NOT_ACCEPTED, EINVAL);           \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE_INVALID_LENGTH, EINVAL);         \
      MAP_WIFI_ERR(WIFI_ENGINE_FAILURE, EINVAL);                        \
      case WIFI_ENGINE_SUCCESS:                                         \
      case WIFI_ENGINE_SUCCESS_ABSORBED:                                \
      case WIFI_ENGINE_SUCCESS_DATA_CFM:                                \
      case WIFI_ENGINE_SUCCESS_DATA_IND:                                \
      case WIFI_ENGINE_FEATURE_DISABLED:                                \
      case WIFI_ENGINE_SUCCESS_AGGREGATED:                              \
         break;                                                         \
      default:                                                          \
         panic("Unknown return code");                                  \
}  } while(0)

   switch(ioc->cmd) {
      case NRXIORACTIVESCAN: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         val->value = basic->activeScanMode != 0;
         break;
      }
      case NRXIOWACTIVESCAN: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         basic->activeScanMode = val->value != 0;
         break;
      }
      case NRXIOWSCANCONF: {
         struct nrx_ioc_scan_conf *val = (struct nrx_ioc_scan_conf *)ioc;
         int ret;
         unsigned int flags = 0;
         if(val->notif_pol & 1) 
            flags |= SCAN_NOTIFICATION_FLAG_FIRST_HIT;
         if(val->notif_pol & 2) 
            flags |= SCAN_NOTIFICATION_FLAG_JOB_COMPLETE;
         if(val->notif_pol & 4) 
            flags |= SCAN_NOTIFICATION_FLAG_BG_PERIOD_COMPLETE;
         if(val->notif_pol & 8) 
            flags |= SCAN_NOTIFICATION_FLAG_HIT;
         
         ret = synchronous_ConfigureScan(dev,
                                   (preamble_t)val->preamble, 
                                   val->rate, 
                                   val->probes_per_ch, 
                                   flags,
                                   val->scan_period, 
                                   val->probe_delay, 
                                   val->pa_min_ch_time, 
                                   val->pa_max_ch_time, 
                                   val->ac_min_ch_time, 
                                   val->ac_max_ch_time, 
                                   val->as_scan_period, 
                                   val->as_min_ch_time, 
                                   val->as_max_ch_time,
                                   val->max_scan_period,
                                   val->max_as_scan_period,
                                   val->period_repetition);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWSCANTRIGJOB: {
         int ret;
         struct nrx_ioc_trigger_scan *val = (struct nrx_ioc_trigger_scan *)ioc;
         ret = WiFiEngine_TriggerScanJob(val->sj_id, val->channel_interval);
         if (WIFI_ENGINE_FAILURE_DEFER == ret)
         {
            return -EBUSY;
         }
         else if (ret != WIFI_ENGINE_SUCCESS)
         {
            return -EIO;
         }
         break;
      }
      case NRXIOWSCANJOBSTATE: {
         int ret;
         struct nrx_ioc_scan_job_state *val = (struct nrx_ioc_scan_job_state *)ioc;
         ret = synchronous_SetScanJobState(dev, val->sj_id, val->state);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWRSCANADDFILTER: {
         int ret;
         struct nrx_ioc_scan_add_filter *val = (struct nrx_ioc_scan_add_filter *)ioc;
         ret = synchronous_AddScanFilter(dev, 
                                   &val->sf_id, 
                                   val->bss_type, 
                                   val->rssi_thr, 
                                   val->snr_thr,
                                   val->threshold_type);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWSCANDELFILTER: {
         int ret;
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         ret = synchronous_RemoveScanFilter(dev, val->value);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWRSCANADDJOB: {
         int ret;
         struct nrx_ioc_scan_add_job *val = (struct nrx_ioc_scan_add_job *)ioc;
         m80211_ie_ssid_t ssid;
         m80211_mac_addr_t bssid;
         channel_list_t ch_list;

         if(val->ssid_len > sizeof(ssid.ssid)) {
            KDEBUG(ERROR, "EXIT EINVAL");
            return -EINVAL;
         }
         if(val->channels_len > ARRAY_SIZE(ch_list.channelList)) {
            KDEBUG(ERROR, "EXIT EINVAL");
            return -EINVAL;
         }
         memset(&ssid, 0, sizeof(ssid));
         memcpy(ssid.ssid, val->ssid, val->ssid_len);
         ssid.hdr.id = M80211_IE_ID_SSID;
         ssid.hdr.len = val->ssid_len;
         memcpy(bssid.octet, val->bssid, sizeof(bssid.octet));
         memcpy(ch_list.channelList, val->channels, val->channels_len);
         ch_list.no_channels = val->channels_len;
         ret = synchronous_AddScanJob(dev,
                                      &val->sj_id,
                                      ssid,
                                      bssid,
                                      val->scan_type,
                                      ch_list,
                                      val->flags,
                                      val->prio,
                                      val->ap_exclude,
                                      val->sf_id,
                                      val->run_every_nth_period);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWSCANDELJOB: {
         int ret;
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         /* Scan job 0 is the default scan job invoked by iwlist scan, do not
          * delete it!
          */
         if (val->value == 0) {
             KDEBUG(ERROR, "EXIT EINVAL");
             return -EINVAL;
         }
         ret = synchronous_RemoveScanJob(dev, val->value);
         if (ret != 0) {
            return ret;
         }
         break;
      }
      case NRXIOWSCANLISTFLUSH:
         WIFI_CHECK( WiFiEngine_FlushScanList() );      
         break;

      case NRXIOWCWINCONF: {
         struct nrx_ioc_cwin_conf *val = (struct nrx_ioc_cwin_conf *)ioc;
         char mib[20];
         size_t mib_len = sizeof(sc->cwin[0][0]);
         int ac, minmax;

         for (ac = 0; ac < 4; ac++)
            for (minmax = 0; minmax < 2; minmax++) 
               if (val->cwin[ac][minmax] != 0xFF) {
                  sprintf(mib, "2.4.%d.%d",
                          ac+1,           /* 1=bk, 2=be, 3=vi, 4=vo */
                          minmax+2);      /* 2=min, 3=max */
                  if (val->override) { 
                     /* Save current value */
                     error = nrx_get_mib(dev, mib, &sc->cwin[ac][minmax], &mib_len);
                     if(error) {
                        KDEBUG(ERROR, "EXIT %d", error);
                        return error;
                     }
                     KDEBUG(TRACE, "MIB %s, saved prev val %u\n", mib, sc->cwin[ac][minmax]);
                     
                     WIFI_CHECK( WiFiEngine_SendMIBSet(mib, 
                                                       NULL, 
                                                       &val->cwin[ac][minmax], 
                                                       mib_len) );
                     KDEBUG(TRACE, "MIB %s, stored new val %u\n", mib, val->cwin[ac][minmax]);
                  }
                  else {
                     if (sc->cwin[ac][minmax] != 0xFF) { 
                        WIFI_CHECK( WiFiEngine_SendMIBSet(mib, 
                                                          NULL, 
                                                          &sc->cwin[ac][minmax], 
                                                          mib_len) );
                        KDEBUG(TRACE, "MIB %s, restored prev val %u)\n", mib, sc->cwin[ac][minmax]);
                     }
                     else
                        KDEBUG(TRACE, "MIB %s, don't have prev value\n", mib);
                  }
               }
         break;
      }
      case NRXIORADAPTIVETXRATE: {
         struct nrx_ioc_adaptive_rate *val = (struct nrx_ioc_adaptive_rate*)ioc;
         val->level = basic->txRatePowerControl;
         val->initial_rate = 0; /* XXX? */
         break;
      }
      case NRXIOWADAPTIVETXRATE: {
         struct nrx_ioc_adaptive_rate *val = (struct nrx_ioc_adaptive_rate*)ioc;
         basic->txRatePowerControl = val->level;
         WIFI_CHECK( WiFiEngine_SetLvl1AdaptiveTxRate(basic->txRatePowerControl) );
         break;
      }
      case NRXIORJOINTIMEOUT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*)ioc;
         int tmo;
         WiFiEngine_GetJoin_Timeout(&tmo);
         val->value = tmo;
         break;
      }
      case NRXIOWJOINTIMEOUT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*)ioc;
         WiFiEngine_SetJoin_Timeout(val->value);
         break;
      }
      case NRXIORCORECOUNT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*)ioc;
         val->value = nrx_get_corecount(dev);
         break;
      }
      case NRXIOWCORECOUNT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*)ioc;
         nrx_set_corecount(dev, val->value);
         break;
      }
#ifdef USE_IF_REINIT
      case NRXIOWACTIVITYTIMEOUT: {
         struct nrx_ioc_activitytimeout *val = (struct nrx_ioc_activitytimeout *)ioc;
         WIFI_CHECK( WiFiEngine_SetActivityTimeout(val->timeout, val->inact_check_interval) );
         break;
      }
#endif
      case NRXIOWMULTIDOMAIN: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         WIFI_CHECK( WiFiEngine_SetMultiDomainCapability(val->value) );
         break;
      }
      case NRXIOWMULTIDOMAINENFORCE: { /* This should be merged into multidomain next release!!! */
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         basic->multiDomainCapabilityEnforced = val->value;
         break;
      }
      case NRXIOWRREGIERTRIG: {
         struct nrx_ioc_ier_threshold *val = (struct nrx_ioc_ier_threshold *)ioc;
         WIFI_CHECK( WiFiEngine_RegisterVirtualIERTrigger(&val->thr_id,
                                                          val->ier_thr,
                                                          val->per_thr,
                                                          val->chk_period,
                                                          val->dir,
                                                          nrx_wxevent_mibtrig) );
         break;
      }
      case NRXIOWDELIERTRIG: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         WIFI_CHECK( WiFiEngine_DelVirtualIERTrigger(val->value) );
         break;
      }
      case NRXIOWARPCONF: {
         struct nrx_ioc_arp_conf *val = (struct nrx_ioc_arp_conf *)ioc;
         WIFI_CHECK( WiFiEngine_ConfigARPFilter(val->mode, val->ip) );
         break;
      }
      case NRXIOWBTCOEXENABLE: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         WIFI_CHECK( WiFiEngine_EnableBTCoex(val->value) );
         break;
      }
      case NRXIOWBTCOEXCONF: {
         struct nrx_ioc_bt_conf *val = (struct nrx_ioc_bt_conf *)ioc;
         WIFI_CHECK( WiFiEngine_ConfigBTCoex(val->bt_vendor,
                                             val->pta_mode,
                                             val->pta_def,
                                             val->len,
                                             val->antenna_dual,
                                             val->antenna_sel0,
                                             val->antenna_sel1,
                                             val->antenna_level0,
                                             val->antenna_level1) );
         break;
      }
      case NRXIOWANTENNADIV: {
         struct nrx_ioc_ant_div *val = (struct nrx_ioc_ant_div*)ioc;
         WIFI_CHECK( WiFiEngine_SetAntennaDiversityMode(val->antenna_mode, val->rssi_threshold) );
         break;
      }
      case NRXIOWOPRATES: {
         struct nrx_ioc_rates *val = (struct nrx_ioc_rates *)ioc;
         if(nrx_is_ibss()) {
            WIFI_CHECK( WiFiEngine_SetIBSSSupportedRates(val->rates, val->num_rates) );
         } else {
            WIFI_CHECK( WiFiEngine_SetSupportedRates(val->rates, val->num_rates) );
         }
         break;
      }
      case NRXIORENABLEHTRATES: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         uint32_t ratemask;
         WiFiEngine_GetEnabledHTRates(&ratemask);
         val->value = ratemask != 0;
         break;
      }
      case NRXIOWENABLEHTRATES: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool*)ioc;
         if(val->value)
            WiFiEngine_SetEnabledHTRates(~0);
         else
            WiFiEngine_SetEnabledHTRates(0);
         break;
      }
      case NRXIORREGCHANNELS: {
         unsigned int i;
         struct nrx_ioc_channels *val = (struct nrx_ioc_channels *)ioc;
         channel_list_t channels;

         WIFI_CHECK( WiFiEngine_GetRegionalChannels(&channels) );
         for(i = 0; 
             i < ARRAY_SIZE(val->channel) && i < channels.no_channels; 
             i++)
            val->channel[i] = channels.channelList[i];
         val->num_channels = i;
         break;
      }
      case NRXIOWREGCHANNELS: {
         unsigned int i;
         struct nrx_ioc_channels *val = (struct nrx_ioc_channels *)ioc;
         channel_list_t channels;

         for(i = 0; 
             i < ARRAY_SIZE(channels.channelList) && i < val->num_channels; 
             i++)
            channels.channelList[i] = val->channel[i];
         channels.no_channels = i;

         WIFI_CHECK( WiFiEngine_SetRegionalChannels(&channels) );
         
         /* Bug 524 */
         WiFiEngine_ActivateRegionChannels();
         
         break;
      }
      case NRXIOWLINKSUPERV: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool *)ioc;
         WIFI_CHECK( WiFiEngine_EnableLinkSupervision(val->value) );
         break;
      }
      case NRXIOWLINKSUPERVRXBEACONCOUNT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_SetLinkSupervisionBeaconFailCount(val->value) );
         break;
      }
      case NRXIOWLINKSUPERVRXBEACONTIMEOUT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_SetLinkSupervisionBeaconTimeout(val->value) );
         break;
      }
      case NRXIOWLINKSUPERVTXFAILCOUNT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_SetLinkSupervisionTxFailureCount(val->value) );
         break;
      }
      case NRXIOWLINKSUPERVRTRIPCOUNT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_SetLinkSupervisionRoundtripCount(val->value) );
         break;
      }
      case NRXIOWLINKSUPERVRTRIPSILENT: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_SetLinkSupervisionRoundtripSilent(val->value) );
         break;
      }
      case NRXIORWMM: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool *)ioc;
         rSTA_WMMSupport wmm;
         ASSERT(WiFiEngine_GetWMMEnable(&wmm) == WIFI_ENGINE_SUCCESS);
         val->value = (wmm != STA_WMM_Disabled);
         break;
      }
      case NRXIOWWMM: {
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool *)ioc;
         if (val->value)
            WiFiEngine_SetWMMEnable(STA_WMM_Enabled);
         else
            WiFiEngine_SetWMMEnable(STA_WMM_Disabled);
         break;
      }
      case NRXIOWWMMPSCONF: {
         struct nrx_ioc_wmm_power_save_conf *val = (struct nrx_ioc_wmm_power_save_conf *)ioc;
         WIFI_CHECK( WiFiEngine_WmmPowerSaveConfigure(val->tx_period, 
                                          val->be != 0, 
                                          val->bk != 0, 
                                          val->vi != 0, 
                                          val->vo != 0) );
         break;
      }
/*      case NRXIORWMMPS: { */
/*         bool_t ps = WiFiEngine_PeriodicWmmPowerSave(); */
/*         break; */

      case NRXIOCWMMPSDISABLE:
         WIFI_CHECK( WiFiEngine_WmmPowerSaveDisable() );
         break;
      case NRXIOWWMMPSENABLE: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t *)ioc;
         WIFI_CHECK( WiFiEngine_WmmPowerSaveEnable(val->value) );
         break;
      }
      case NRXIOCPSENABLE: {
         WiFiEngine_PSControlAllow(sc->ps_control);
         break;
      }
      case NRXIORPSENABLE: {
         WiFiEngine_PowerMode_t pmode;
         struct nrx_ioc_bool *val = (struct nrx_ioc_bool *)ioc;
         WIFI_CHECK( WiFiEngine_GetPowerMode(&pmode) ); /* Read mode */
         if (pmode == WIFI_ENGINE_PM_ALWAYS_ON)
            val->value = 0;     /* PS disabled */
         else 
            val->value = 1;     /* PS enabled */
      }
      case NRXIOCPSDISABLE: {
         WiFiEngine_PSControlInhibit(sc->ps_control);
         break;
      }
      case NRXIOWPSCONF: {
         struct nrx_ioc_ps_conf *val = (struct nrx_ioc_ps_conf *)ioc;
         WIFI_CHECK( WiFiEngine_SetPSTrafficTimeout(val->traffic_timeout) );
         /* Set value in register */
         WiFiEngine_SetListenInterval(val->listen_interval);
         /* Set new value in fw */
         WiFiEngine_SetMibListenInterval(val->listen_interval);

         /* Set value in register */         
         WiFiEngine_SetReceiveDTIM(val->rx_all_dtim);
         /* Set new value in fw */         
         WiFiEngine_SetReceiveAllDTIM(val->rx_all_dtim);

         /* Set value in register */   
         WiFiEngine_EnableLegacyPsPollPowerSave(val->ps_poll);
         /* Set new value in fw */           
         WiFiEngine_SetUsePsPoll(val->ps_poll);  
         
         break;
      }
      case NRXIOCREASSOCIATE: {
         WIFI_CHECK( WiFiEngine_Reconnect() );
         break;
      }
         /* ===== IBSS functions ===== */
      case NRXIORBEACONPERIOD: {
         struct nrx_ioc_uint16_t *val = (struct nrx_ioc_uint16_t *)ioc;
         WiFiEngine_GetIBSSBeaconPeriod(&val->value);
         break;
      }
      case NRXIOWBEACONPERIOD: {
         struct nrx_ioc_uint16_t *val = (struct nrx_ioc_uint16_t *)ioc;
         WiFiEngine_SetIBSSBeaconPeriod(val->value);
         break;
      }
      case NRXIORDTIMPERIOD: {
         struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *)ioc;
         WiFiEngine_GetIBSSDTIMPeriod(&val->value);
         break;
      }
      case NRXIOWDTIMPERIOD:{
         struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *)ioc;
         WiFiEngine_SetIBSSDTIMPeriod(val->value);
         break;
      }
      case NRXIORATIMWINDOW:{
         struct nrx_ioc_uint16_t *val = (struct nrx_ioc_uint16_t *)ioc;
         WiFiEngine_GetIBSSATIMWindow(&val->value);
         break;
      }
      case NRXIOWATIMWINDOW:{
         struct nrx_ioc_uint16_t *val = (struct nrx_ioc_uint16_t *)ioc;
         WiFiEngine_SetIBSSATIMWindow(val->value);
         break;
      }
      case NRXIOWCONSOLEWRITE:
      {
         struct nrx_ioc_console_string *val = (struct nrx_ioc_console_string *)ioc;
         char *str = kmalloc(val->str_size+1, GFP_KERNEL);
         uint32_t tid;
         int retval = 0;

         if (copy_from_user(str, val->str, val->str_size)) {
            retval = -EFAULT;
         } else {
            str[val->str_size] = 0;
            if (WiFiEngine_SendConsoleRequest(str, &tid) != WIFI_ENGINE_SUCCESS) {
               retval = -EINVAL;
            }
         }
         kfree(str);
         if (retval) {
            return retval;
         }
         break;
      }
      case NRXIOWRCONSOLEREAD:
      {
         struct nrx_ioc_console_string *val = (struct nrx_ioc_console_string *)ioc;
         size_t buflen = val->str_size;
         size_t hiclen = buflen;
         hic_message_header_t *hic;
         int retval = 0;

         val->str_size = 0;

         if ((hic = kmalloc(hiclen, GFP_KERNEL)) == NULL) {
            retval = -ENOMEM;
         } else if (WiFiEngine_GetConsoleReply(hic, &hiclen) != WIFI_ENGINE_SUCCESS) {
            /* No console replies available or invalid length */
         } else if (copy_to_user(val->str, hic, hiclen)) {
            retval = -EFAULT;
         } else {
            val->str_size = hiclen;
         }
         kfree(hic);
         if (retval) {
            return retval;
         }
         break;
      }
      case NRXIOWRGETMIB: {
         struct nrx_ioc_mib_value *val = (struct nrx_ioc_mib_value *)ioc;
         unsigned char mib_param[512];
         size_t mib_len;
         
         mib_len = sizeof(mib_param);
         error = nrx_get_mib(dev, val->mib_id, mib_param, &mib_len);
         if(error) {
            KDEBUG(ERROR, "EXIT %d", error);
            return error;
         }
         if(mib_len > val->mib_param_size) {
            KDEBUG(ERROR, "EXIT EINVAL");
            return -EINVAL;
         }
         error = copy_to_user(val->mib_param, mib_param, mib_len);
         if(error) {
            KDEBUG(ERROR, "EXIT EFAULT");
            return -EFAULT;
         }
         val->mib_param_size = mib_len;
         break;
      }
      case NRXIOWSETMIB: {
         struct nrx_ioc_mib_value *val = (struct nrx_ioc_mib_value *)ioc;
         unsigned char mib_param[512];
         
         if(val->mib_param_size > sizeof(mib_param)) {
            KDEBUG(ERROR, "EXIT EINVAL");
            return -EINVAL;
         }
         error = copy_from_user(mib_param, val->mib_param, val->mib_param_size);
         if (error) {
            KDEBUG(ERROR, "EXIT EFAULT");
            return -EFAULT;
         }
         WIFI_CHECK( WiFiEngine_SendMIBSet(val->mib_id, NULL, mib_param, val->mib_param_size) );
         break;
      }
      case NRXIOWREGMIBTRIG: {
         int trig_id;
         struct nrx_ioc_mib_trigger *val = (struct nrx_ioc_mib_trigger *)ioc;
         char mib_id[val->mib_id_len + 1];
         strncpy(mib_id, val->mib_id, val->mib_id_len);
         mib_id[val->mib_id_len] = '\0';
         WIFI_CHECK( WiFiEngine_RegisterVirtualTrigger(&trig_id, /* Assigned trigger id */
                                                mib_id, 
                                                val->gating_trig_id,
                                                val->supv_interval,
                                                val->level,
                                                val->dir,
                                                val->event_count,
                                                val->trigmode,
                                                nrx_wxevent_mibtrig) );
         memcpy(&val->trig_id, &trig_id, sizeof(val->trig_id));
         break;
      }
      case NRXIOWDELMIBTRIG: {
         struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*)ioc;
         WIFI_CHECK( WiFiEngine_DelVirtualTrigger(val->value) );
         break;
      }
      case NRXIOWRDOESTRIGEXIST: {
         struct nrx_ioc_verify_mib_trigger *val = (struct nrx_ioc_verify_mib_trigger *)ioc;
         char mib_id[val->mib_id_len + 1];
         strncpy(mib_id, val->mib_id, val->mib_id_len);
         mib_id[val->mib_id_len] = '\0';
         val->does_exist = WiFiEngine_DoesVirtualTriggerExist(val->trig_id, 
                                                              val->mib_id_len ? mib_id : NULL);
         break;
      }
      case NRXIORDSCPMAP: {
         struct nrx_ioc_dscpmap *val = (struct nrx_ioc_dscpmap*)ioc;
         error = nrx_get_dscp_mapping(val->dscpmap, sizeof(val->dscpmap));
         if(error)
            return error;
         break;
      }
      case NRXIOWDSCPMAP: {
         struct nrx_ioc_dscpmap *val = (struct nrx_ioc_dscpmap*)ioc;
         error = nrx_set_dscp_mapping(val->dscpmap, sizeof(val->dscpmap));
         if(error)
            return error;
         break;
      }

         
    case NRXIOWROAMENABLE: {
       struct nrx_ioc_uint32_t *val = (struct nrx_ioc_uint32_t*) ioc;
       WIFI_CHECK(WiFiEngine_roam_enable(val->value));
       break;
    }
 
    case NRXIOWROAMADDSSIDFILTER: {
       m80211_ie_ssid_t ssid;
       struct nrx_ioc_roam_ssid *val = (struct nrx_ioc_roam_ssid*) ioc;
 
       if(val->ssid_len > sizeof(ssid.ssid)) {
          KDEBUG(ERROR, "EXIT EINVAL");
          return -EINVAL;
       }
 
       memset(&ssid, 0, sizeof(ssid));
       memcpy(ssid.ssid, val->ssid, val->ssid_len);
       ssid.hdr.id = M80211_IE_ID_SSID;
       ssid.hdr.len = val->ssid_len;
          
       WIFI_CHECK(WiFiEngine_roam_add_ssid_filter(ssid));
       break;
    }
 
    case NRXIOWROAMDELSSIDFILTER: {
       m80211_ie_ssid_t ssid;
       struct nrx_ioc_roam_ssid *val = (struct nrx_ioc_roam_ssid*) ioc;
 
       if(val->ssid_len > sizeof(ssid.ssid)) {
          KDEBUG(ERROR, "EXIT EINVAL");
          return -EINVAL;
       }
 
       memset(&ssid, 0, sizeof(ssid));
       memcpy(ssid.ssid, val->ssid, val->ssid_len);
       ssid.hdr.id = M80211_IE_ID_SSID;
       ssid.hdr.len = val->ssid_len;
          
       WIFI_CHECK(WiFiEngine_roam_del_ssid_filter(ssid));
       break;
    }
                  
    case NRXIOWROAMCONFFILTER: {
       struct nrx_ioc_roam_filter *val =
          (struct nrx_ioc_roam_filter*) ioc;
       WIFI_CHECK(WiFiEngine_roam_configure_filter(val->enable_blacklist,
                                                   val->enable_wmm,
                                                   val->enable_ssid));
       break;
    }
 
    case NRXIOWROAMCONFRSSITHR: {
       struct nrx_ioc_roam_rssi_thr *val = (struct nrx_ioc_roam_rssi_thr*) ioc;
       WIFI_CHECK(WiFiEngine_roam_configure_rssi_thr(val->enable,
                                                     val->roam_thr,
                                                     val->scan_thr,
                                                     val->margin));
       break;
    }

   case NRXIOWROAMCONFSNRTHR: {
       struct nrx_ioc_roam_snr_thr *val = (struct nrx_ioc_roam_snr_thr*) ioc;
       WIFI_CHECK(WiFiEngine_roam_configure_snr_thr(val->enable,
                                                     val->roam_thr,
                                                     val->scan_thr,
                                                     val->margin));
       break;
    }

   case NRXIOWROAMCONFDSTHR: {
       struct nrx_ioc_roam_ds_thr *val = (struct nrx_ioc_roam_ds_thr*) ioc;
       WIFI_CHECK(WiFiEngine_roam_configure_ds_thr(val->enable,
                                                   val->roam_thr,
                                                   val->scan_thr));
       break;
    }

   case NRXIOWROAMCONFRATETHR: {
       struct nrx_ioc_roam_rate_thr *val = (struct nrx_ioc_roam_rate_thr*) ioc;
       WIFI_CHECK(WiFiEngine_roam_configure_rate_thr(val->enable,
                                                     val->roam_thr,
                                                     val->scan_thr));
       break;
    }

   case NRXIOWROAMCONFNETELECTION: {
      struct nrx_ioc_roam_net_election *val =
         (struct nrx_ioc_roam_net_election*) ioc;
      WIFI_CHECK(WiFiEngine_roam_configure_net_election(val->k1, val->k2));
      break;
   }
      
   case NRXIOWROAMCONFAUTH: {
      struct nrx_ioc_roam_conf_auth *val = (struct nrx_ioc_roam_conf_auth*) ioc;
      WIFI_CHECK(WiFiEngine_roam_conf_auth(val->enable,
                                           (WiFiEngine_Auth_t) val->auth_mode,
                                           (WiFiEngine_Encryption_t)
                                           val->enc_mode));
      break;
   }
      
      case NRXIOWTXRATEMONENABLE: {
         struct nrx_ioc_ratemon *val = (struct nrx_ioc_ratemon*) ioc;
         WIFI_CHECK( WiFiEngine_EnableTxRateMon(&val->thr_id, val->sample_len, val->thr_level, WE_TRIG_THR_FALLING, nrx_wxevent_txrate) );
         break;
      }
      case NRXIOCTXRATEMONDISABLE: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t*) ioc;
         WIFI_CHECK( WiFiEngine_DisableTxRateMon(val->value) );
         break;
      }
      case NRXIOWRXRATEMONENABLE: {
         struct nrx_ioc_ratemon *val = (struct nrx_ioc_ratemon*) ioc;
         WIFI_CHECK( WiFiEngine_EnableRxRateMon(&val->thr_id, val->sample_len, val->thr_level, WE_TRIG_THR_FALLING, nrx_wxevent_rxrate) );
         break;
      }
      case NRXIOCRXRATEMONDISABLE: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t*) ioc;
         WIFI_CHECK( WiFiEngine_DisableRxRateMon(val->value) );
         break;
      }
      case NRXIORTXRATE: {
         struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *) ioc;
         WIFI_CHECK( WiFiEngine_GetTxRate(&val->value) );
         break;
      }
      case NRXIORRXRATE: {
         struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *) ioc;
         WIFI_CHECK( WiFiEngine_GetRxRate(&val->value) );
         break;
      }

   case NRXIOWCONFDELAYSPREAD: {
       struct nrx_ioc_ds_conf *val = (struct nrx_ioc_ds_conf*) ioc;
       WIFI_CHECK(WiFiEngine_configure_delay_spread(val->thr, val->winsize));
       break;
   }


      case NRXIORFRAGMENTTHR: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         WiFiEngine_GetFragmentationThreshold(&val->value);
         break;
      }
      case NRXIOWFRAGMENTTHR: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         WiFiEngine_SetFragmentationThreshold(val->value);
         break;
      }
      case NRXIORRTSTHR: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         WiFiEngine_GetRTSThreshold(&val->value);
         break;
      }
      case NRXIOWRTSTHR: {
         struct nrx_ioc_int32_t *val = (struct nrx_ioc_int32_t *)ioc;
         WiFiEngine_SetRTSThreshold(val->value);
         break;
      }
      case NRXIOWRREGISTRY: {
         struct nrx_ioc_len_buf *val = (struct nrx_ioc_len_buf *)ioc;
         unsigned int i;
         char *buf;
         const unsigned int len = 4096;
         buf = kmalloc(len, GFP_KERNEL);
         if (buf == NULL)
            return -EINVAL;
         memset(buf, 0xff, len);
         WiFiEngine_Registry_Write(buf);

         /* Convert strings that are \n\0 terminated */
         for(i = 0; i < len && i < val->len && buf[i] != '\xff'; i++) {
            if(buf[i] == '\0') {
               if(i > 0 && buf[i-1] == '\n')
                  buf[i - 1] = ' ';
               buf[i] = '\n';
            }
         }
         if(i > val->len) {
            KDEBUG(ERROR, "EXIT EINVAL");
            return -EINVAL;
         }
         val->len = i;
         error = copy_to_user(val->buf, buf, val->len);
         if(error) {
            KDEBUG(ERROR, "EXIT EFAULT");
            return -EFAULT;
         }
         kfree(buf);
         break;
      }
      case NRXIOCFWSUICIDE: {
         WiFiEngine_RequestSuicide();
         break;
      }
#if (DE_CCX == CFG_INCLUDED)
      case NRXIOSNDADDTS:{
          struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *)ioc;
         WiFiEngine_SendAddtsReq(val->value);
         break;
      }
      case NRXIOSNDDELTS:{
         struct nrx_ioc_uint8_t *val = (struct nrx_ioc_uint8_t *)ioc;
         WiFiEngine_SendDelts(val->value);
         break;
      }
#endif /* DE_CCX == CFG_INCLUDED */
	  case NRXIOCSHUTDOWN: {
		  int status;
		  status = nrx_enter_shutdown(dev);
		  if (status)
		     return status;
		  break;
      }
         default:
         KDEBUG(ERROR, "EXIT EOPNOTSUPP");
         return -EOPNOTSUPP;
   }
#undef WIFI_CHECK
   if(NRXIOCMODE(ioc->cmd) & NRXOUT) {
      error = copy_to_user(ifr->ifr_data, buf, len);
      if (error) {
         KDEBUG(ERROR, "EXIT EFAULT");
         return -EFAULT;
      }
   }
   return 0;
}

#ifdef HAVE_ETHTOOL
static int nrx_ioctl_ethtool(struct net_device *dev, struct ifreq *ifr)
{
   uint32_t cmd;

   CHECK_UNPLUG(dev);

   if (get_user(cmd, (uint32_t *)ifr->ifr_data))
      return -EFAULT;

   switch (cmd) {
      case ETHTOOL_GDRVINFO: {
         struct ethtool_drvinfo info;
         
         memset(&info, 0, sizeof(info));
         info.cmd = ETHTOOL_GDRVINFO;

         strcpy (info.driver, "Nanoradio");
         WiFiEngine_GetReleaseInfo(info.version, sizeof(info.version));
         WiFiEngine_GetFirmwareReleaseInfo(info.fw_version, sizeof(info.fw_version));
         if (copy_to_user (ifr->ifr_data, &info, sizeof (info)))
            return -EFAULT;
         return 0;
      }

      default:
         break;
   }

   return -EOPNOTSUPP;
}
#endif

int ns_net_ioctl_iw(struct net_device *dev, struct ifreq *ifr, int cmd);

int
ns_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd) 
{
   int retval = -EOPNOTSUPP;
   KDEBUG(TRACE, "ENTRY: %s", dev->name);

   switch(cmd) {
      case SIOCNRXIOCTL:
         retval = nrx_ioctl2(dev, ifr, cmd);
         break;
   
      case SIOCNRXRAWTX:
      case SIOCNRXRAWRX:
         retval = ns_net_ioctl_command(dev, ifr, cmd);
         break;

#ifdef HAVE_ETHTOOL
      case SIOCETHTOOL:
         retval = nrx_ioctl_ethtool(dev, ifr);
         break;
#endif
      default:
         /* this is only used with old wireless extensions that lack
          * native WPA support, the normal handler for these are in
          * the kernel, with function pointers in nanoiw.c */
         if(cmd >= SIOCIWFIRST && cmd <=  SIOCIWLAST)
            retval = ns_net_ioctl_iw(dev, ifr, cmd);
         break;
   }

   return retval;
}
