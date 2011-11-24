#include "nanonet.h"
#include "nanoparam.h"
#include "nanoutil.h"
#include "px.h"
#include "registry.h"
#include "registryAccess.h"

#include "wifi_engine_internal.h"

#define SETBOOL(X) ((X) ? 1 : 0)

#define CALLOUT(N, T) static int N##_callout(struct nrx_px_softc *psc, int write, T *value)

CALLOUT(min_channel_time, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   rConnectionPolicy *conn = Registry_GetProperty(ID_connectionPolicy);
   rScanTimeouts *timeout;

   if(basic->activeScanMode)
      timeout = &conn->activeScanTimeouts;
   else
      timeout = &conn->passiveScanTimeouts;

   if(write)
      timeout->minChannelTime = *value;
   else
      *value = timeout->minChannelTime;

   return 0;
}

CALLOUT(max_channel_time, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   rConnectionPolicy *conn = Registry_GetProperty(ID_connectionPolicy);
   rScanTimeouts *timeout;

   if(basic->activeScanMode)
      timeout = &conn->activeScanTimeouts;
   else
      timeout = &conn->passiveScanTimeouts;

   if(write)
      timeout->maxChannelTime = *value;
   else
      *value = timeout->maxChannelTime;

   return 0;
}

CALLOUT(active_scan, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   
   if(write)
      basic->activeScanMode = (*value != 0);
   else
      *value = basic->activeScanMode;

   return 0;
}

CALLOUT(scan_filter_ssid, void)
{
   rScanPolicy *scan = Registry_GetProperty(ID_scanPolicy);
   char ssid[M80211_IE_MAX_LENGTH_SSID + 1];

   KDEBUG(TRACE, "ENTRY");

   memset(ssid, 0, sizeof(ssid));

   if(write) {
      char *buf = nrx_px_data(psc);
      
      KDEBUG(TRACE, "ssid = %s\n", buf);
      memset(ssid, 0, sizeof(ssid));
      if(sscanf(buf, "%32s", ssid) == 1) {
         scan->ssid.hdr.id = M80211_IE_ID_SSID;
         memcpy(scan->ssid.ssid, ssid, sizeof(scan->ssid.ssid));
         scan->ssid.hdr.len = strlen(ssid);
      } else {
         scan->ssid.hdr.id = M80211_IE_ID_NOT_USED;
      }
   } else {
      nrx_px_setsize(psc, 0);
      if(scan->ssid.hdr.id == M80211_IE_ID_SSID) {
         memcpy(ssid, scan->ssid.ssid, scan->ssid.hdr.len);
         nrx_px_printf(psc, "%s\n", ssid);
      }
   }

   return 0;
}

CALLOUT(scan_filter_bssid, struct nrx_px_macaddr)
{
   rScanPolicy *scan = Registry_GetProperty(ID_scanPolicy);

   KDEBUG(TRACE, "ENTRY");

   ASSERT(sizeof(value->addr) == sizeof(scan->bssid.octet));

   if(write)
      memcpy(scan->bssid.octet, value->addr, sizeof(value->addr));
   else
      memcpy(value->addr, scan->bssid.octet, sizeof(value->addr));

   return 0;
}

CALLOUT(scan_filter_bsstype, unsigned int)
{
   rScanPolicy *scan = Registry_GetProperty(ID_scanPolicy);

   KDEBUG(TRACE, "ENTRY");

   if(write)
      scan->bssType = *value;
   else
      *value = scan->bssType;

   return 0;
}

CALLOUT(adaptive_tx_rate, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   
   if(write) {
      basic->txRatePowerControl = *value;
      WiFiEngine_SetLvl1AdaptiveTxRate(basic->txRatePowerControl);
   } else
      *value = basic->txRatePowerControl;

   return 0;
}

CALLOUT(beacon_period, unsigned int)
{
   if(write) 
      WiFiEngine_SetIBSSBeaconPeriod(*value);
   else {
      uint16_t period;
      WiFiEngine_GetIBSSBeaconPeriod(&period);
      *value = period;
   }
   return 0;
}

CALLOUT(dtim_period, unsigned int)
{
   if(write) 
      WiFiEngine_SetIBSSDTIMPeriod(*value);
   else {
      uint8_t period;
      WiFiEngine_GetIBSSDTIMPeriod(&period);
      *value = period;
   }
   return 0;
}

CALLOUT(join_timeout, unsigned int)
{
   if(write)
      WiFiEngine_SetJoin_Timeout(*value);
   else
      WiFiEngine_GetJoin_Timeout(value);

   return 0;
}

CALLOUT(link_monitoring, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   
   if(write) {
      basic->linkSupervision.enable = SETBOOL(*value);
      WiFiEngine_EnableLinkSupervision(basic->linkSupervision.enable);
   } else
      *value = basic->linkSupervision.enable;
   
   return 0;
}

CALLOUT(multi_domain, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   
   if(write) {
      if(*value == 2) {
         basic->multiDomainCapabilityEnforced = 1;
         basic->multiDomainCapabilityEnabled = 1;
      } else if(*value == 1) {
         basic->multiDomainCapabilityEnforced = 0;
         basic->multiDomainCapabilityEnabled = 1;
      } else if(*value == 0) {
         basic->multiDomainCapabilityEnforced = 0;
         basic->multiDomainCapabilityEnabled = 0;
      }
   } else {
      if(basic->multiDomainCapabilityEnforced)
         *value = 2;
      else if(basic->multiDomainCapabilityEnabled)
         *value = 1;
      else
         *value = 0;
   }
   return 0;
}

CALLOUT(wmm, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);

   if(write)
      basic->enableWMM = SETBOOL(*value);
   else
      *value = basic->enableWMM;
   
   return 0;
}

static int
tx_power(struct net_device *dev, int write, int index, unsigned int *value)
{
   uint8_t idx[2];
   size_t len = sizeof(idx);
   
   if(nrx_get_mib(dev, MIB_dot11powerIndex, idx, &len) != 0 || len != 2) {
      KDEBUG(TRACE, "failed to get MIB_dot11powerIndex");
      return 1;
   }

   if(write) {
      if(*value <= 19) {
         idx[index] = *value;

         WiFiEngine_SendMIBSet(MIB_dot11powerIndex, 
                               NULL, idx, sizeof(idx));
      }
   } else {
      *value = idx[index];
   }

   return 0;
}

CALLOUT(tx_power_index_11g, unsigned int)
{
   return tx_power(nrx_px_priv(psc), write, 1, value);
}

CALLOUT(tx_power_index_11b, unsigned int)
{
   return tx_power(nrx_px_priv(psc), write, 0, value);
}


CALLOUT(bgscan_enable, unsigned int)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);
   
   if(write) {
      basic->defaultScanJobDisposition = SETBOOL(*value);
      WiFiEngine_SetBackgroundScanMode(basic->defaultScanJobDisposition);
   } else
      *value = basic->defaultScanJobDisposition;
   
   return 0;
}
 
static int
mib_32(struct net_device *dev, const char *mib, int write, unsigned int *value)
{
   uint32_t u32;
   size_t len = sizeof(u32);
   
   if(write) {
      u32 = *value;
      WiFiEngine_SendMIBSet(mib, NULL, (char*)&u32, sizeof(u32));
   } else {
      if(nrx_get_mib(dev, mib, &u32, &len) != 0 || len != sizeof(u32)) {
         KDEBUG(TRACE, "failed to get %s", mib);
         return 1;
      }
      *value = u32;
   }
   
   return 0;
}
 
static int
mib_16(struct net_device *dev, const char *mib, int write, unsigned int *value)
{
   uint16_t u16;
   size_t len = sizeof(u16);
   
   if(write) {
      u16 = *value;
      WiFiEngine_SendMIBSet(mib, NULL, (char*)&u16, sizeof(u16));
   } else {
      if(nrx_get_mib(dev, mib, &u16, &len) != 0 || len != 2) {
         KDEBUG(TRACE, "failed to get MIB_dot11backgroundScanProbeDelay");
         return 1;
      }
      *value = u16;
   }
   
   return 0;
}
 
CALLOUT(bgscan_period, unsigned int)
{
   int retval;
   rConnectionPolicy *conn = Registry_GetProperty(ID_connectionPolicy);
   
   retval = mib_16(nrx_px_priv(psc), MIB_dot11backgroundScanPeriod, write, value);
   if(write) {
      conn->connectedScanPeriod = *value;
   }
   return retval;
}
 
CALLOUT(bgscan_min_channel_time, unsigned int)
{
   int retval;
   rConnectionPolicy *conn = Registry_GetProperty(ID_connectionPolicy);
    
   retval = mib_16(nrx_px_priv(psc), MIB_dot11backgroundScanMinChannelTime, write, value);
   if(write) {
      conn->connectedScanTimeouts.minChannelTime = *value;
   }
   return retval;
}
 
CALLOUT(bgscan_max_channel_time, unsigned int)
{
   int retval;
   rConnectionPolicy *conn = Registry_GetProperty(ID_connectionPolicy);

   retval = mib_16(nrx_px_priv(psc), MIB_dot11backgroundScanMaxChannelTime, write, value);
   
   if(write) {
      conn->connectedScanTimeouts.maxChannelTime = *value;
   } 
   return retval;
}

CALLOUT(bgscan_probe_delay, unsigned int)
{
   return mib_16(nrx_px_priv(psc), MIB_dot11backgroundScanProbeDelay, write, value);
}

CALLOUT(desired_bssid, struct nrx_px_macaddr)
{
   rBasicWiFiProperties *basic = Registry_GetProperty(ID_basic);

   KDEBUG(TRACE, "ENTRY");

   ASSERT(sizeof(value->addr) == sizeof(basic->desiredBSSID.octet));

   if(write)
      memcpy(basic->desiredBSSID.octet, value->addr, sizeof(value->addr));
   else
      memcpy(value->addr, basic->desiredBSSID.octet, sizeof(value->addr));
   return 0;
}

CALLOUT(channels, struct nrx_px_uintvec)
{
   channel_list_t channels;
   size_t i;
   
   if(write) {
      for(i = 0; 
          i < value->size && i < ARRAY_SIZE(channels.channelList); 
          i++)
         channels.channelList[i] = value->vals[i];
      
      channels.no_channels = i;

      WiFiEngine_SetRegionalChannels(&channels);
   } else {
      WiFiEngine_GetRegionalChannels(&channels);
      for(i = 0; 
          i < channels.no_channels && i < ARRAY_SIZE(value->vals); 
          i++)
         value->vals[i] = channels.channelList[i];
      value->size = i;
   }
   return 0;
}

CALLOUT(rates_supported, we_ratemask_t)
{
   struct net_device *dev = nrx_px_priv(psc);
   struct nrx_softc *sc = netdev_priv(dev);
   
   if(write)
      ;
   else
      *value = sc->supported_rates;
   return 0;
}

CALLOUT(manufacturer_id, void)
{
   struct net_device *dev = nrx_px_priv(psc);

   if(write)
      ;
   else {
      char buf[32];
      size_t len = sizeof(buf);
      if(nrx_get_mib(dev, MIB_dot11ManufacturerID, buf, &len) != 0) {
         KDEBUG(TRACE, "failed to get MIB_dot11ManufacturerID");
         return 1;
      }
      if(len >= sizeof(buf))
         len = sizeof(buf) - 1;
      buf[len] = '\0';
      nrx_px_setsize(psc, 0);
      nrx_px_printf(psc, "%s\n", buf);
   }
   return 0;
}

CALLOUT(product_id, unsigned int)
{
   if(write)
      return 0;
   /* this is really wrong, the pruduct id is just one byte (in a four
      byte field) */
   return mib_32(nrx_px_priv(psc), MIB_dot11ProductID, 0, value);
}

CALLOUT(rates_operational, we_ratemask_t)
{
   struct net_device *dev = nrx_px_priv(psc);
   uint8_t rates[M80211_XMIT_RATE_NUM_RATES];
   size_t len = sizeof(rates);
   size_t i;
   we_xmit_rate_t r;
   
   if(write) {
      i = 0;
      WE_RATEMASK_FOREACH(r, *value) {
         if (i >= M80211_XMIT_RATE_NUM_RATES)
            return 1;
         rates[i++] = WiFiEngine_rate_native2ieee(r);
      }
      WiFiEngine_SendMIBSet(MIB_dot11OperationalRatesSet,
                            NULL, 
                            rates, i);
   } else {
      if(nrx_get_mib(dev, MIB_dot11OperationalRatesSet, rates, &len) != 0)
         return 1;
   
      WE_RATEMASK_CLEAR(*value);
      for(i = 0; i < len; i++) {
         r = WiFiEngine_rate_ieee2native(rates[i]);
         WE_RATEMASK_SETRATE(*value, r);
      }
   }
   return 0;
}

extern int nrx_wmm_association;
CALLOUT(wmm_association, unsigned int)
{
   if(write)
      return 0;
   *value = nrx_wmm_association;
   return 0;
}

static struct nrx_px_entry_head cfg_entries = WEI_TQ_HEAD_INITIALIZER(cfg_entries);

struct nrx_px_entry*
nrx_config_create(struct net_device *dev, 
                  const char *name,
                  int (*open)(struct nrx_px_softc*, struct inode*, struct file*),
                  int (*release)(struct nrx_px_softc*, struct inode*, struct file*),
                  nrx_px_callout callout)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct nrx_px_entry *e;
   e = nrx_px_create_dynamic(dev, name, callout, sizeof(callout),
                             0, NULL, open, release, 
                             sc->config_dir);
   if(e == NULL)
      return NULL;

   e->list = &cfg_entries;
   WEI_TQ_INSERT_TAIL(e->list, e, next);

   return e;
}

CALLOUT(driver_version, void)
{
   if(write)
      ;
   else {
      int status;
      char v[128];
      status = WiFiEngine_GetReleaseInfo(v, sizeof(v));
      if(status != WIFI_ENGINE_SUCCESS) 
         strcpy(v, "failure");
      
      nrx_px_setsize(psc, 0);
      nrx_px_printf(psc, "%s\n", v);
   }
   return 0;
}

CALLOUT(x_mac_version, void)
{
   if(write)
      ;
   else {
      nrx_px_setsize(psc, 0);
      nrx_px_printf(psc, "%s\n", wifiEngineState.x_mac_version);
   }
   return 0;
}

void
create_config_entries(struct net_device *dev)
{
#define CREATE(N, F, T) nrx_config_create(dev, #N, nrx_px_##T##_open, nrx_px_##T##_release, (nrx_px_callout)F##_callout)
#define CREATERO(N, F, T) nrx_config_create(dev, #N, nrx_px_##T##_open, NULL, (nrx_px_callout)F##_callout)

   CREATE(active-scan, active_scan, uint);
   CREATE(adaptive-tx-rate, adaptive_tx_rate, uint);
   CREATE(ibss-beacon-period, beacon_period, uint);
   CREATE(ibss-dtim-period, dtim_period, uint);
   CREATE(join-timeout, join_timeout, uint);
   CREATE(link-monitoring, link_monitoring, uint);
   CREATE(max-channel-timeout, max_channel_time, uint);
   CREATE(min-channel-timeout, min_channel_time, uint);
   CREATE(multi-domain, multi_domain, uint);
   CREATE(wmm, wmm, uint);
   
   CREATE(background-scan-enable, bgscan_enable, uint);
   CREATE(background-scan-period, bgscan_period, uint);
   CREATE(background-scan-min-channel-time, bgscan_min_channel_time, uint);
   CREATE(background-scan-max-channel-time, bgscan_max_channel_time, uint);
   CREATE(background-scan-probe-delay, bgscan_probe_delay, uint);
     
   CREATE(desired-bssid, desired_bssid, macaddr);

   CREATE(channel-list, channels, uintvec);

   CREATE(tx-power-index-11b, tx_power_index_11b, uint);
   CREATE(tx-power-index-11g, tx_power_index_11g, uint);

   CREATE(scan-filter-ssid, scan_filter_ssid, string);
   CREATE(scan-filter-bssid, scan_filter_bssid, macaddr);
   CREATE(scan-filter-bsstype, scan_filter_bsstype, uint);

   CREATE(operational-rates, rates_operational, rates);
   CREATERO(supported-rates, rates_supported, rates);

   CREATERO(manufacturer-id, manufacturer_id, string);
   CREATERO(product-id, product_id, uint);

   CREATERO(driver-version, driver_version, string);
   CREATERO(x-mac-version, x_mac_version, string);
   CREATERO(wmm-association, wmm_association, uint);
}

void
remove_config_entries(struct net_device *dev)
{
   struct nrx_softc *psc = netdev_priv(dev);
   struct nrx_px_entry *e;

   while((e = WEI_TQ_FIRST(&cfg_entries)) != NULL) {
      nrx_px_remove(e, psc->config_dir);
   }
}

