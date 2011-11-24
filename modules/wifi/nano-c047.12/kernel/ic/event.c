/* This deals with event passing in Wireless Extensions */
/* $Id: event.c 16988 2010-11-25 14:57:57Z copi $ */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <net/iw_handler.h>
#if WIRELESS_EXT < 18
#include "wireless_wpa.h"
#endif

#include "nanoutil.h"
#include "nanoparam.h"
#include "wifi_engine.h"
#include "driverenv.h"

#if IW_HANDLER_VERSION < 3
#define wireless_send_event(dev, cmd, wrqu, extra) KDEBUG(TRACE, "EVENT %s", #cmd)
#endif

static size_t
format_hex(char *buf, 
           size_t buf_size,
           const void *data,
           size_t data_len)
{
   const unsigned char *p = data;
   size_t buf_len = 0, i;

   for(i = 0; i < data_len; i++)
      buf_len += snprintf(buf + buf_len, buf_size - buf_len, "%02x", p[i]);

   return buf_len;
}

#if WIRELESS_EXT < 18
static void
send_custom_event(struct net_device *dev,
                  const char *prefix, const char *suffix, 
                  void *buf, size_t len)
{
   union iwreq_data wrqu;
   char custom[IW_CUSTOM_MAX];
   size_t cend = 0;

   snprintf(custom, sizeof(custom), "%s", prefix);
   cend = strlen(custom);
   cend += format_hex(custom + cend, sizeof(custom) - cend, buf, len);
   cend += snprintf(custom + cend, sizeof(custom) - cend, "%s", suffix);
   wrqu.data.length = strlen(custom);
   wrqu.data.flags = 0;
   wrqu.data.pointer = (caddr_t)0xdeadbeef;
   wireless_send_event(dev, IWEVCUSTOM, &wrqu, custom);
}
#endif

static void
send_nrx_event(struct net_device *dev,
               const char *fmt, ...)
   __attribute__((format (printf, 2, 3)));

static void
send_nrx_event(struct net_device *dev,
               const char *fmt, ...)
{
#if WIRELESS_EXT > 14
   va_list ap;
   union iwreq_data wrqu;
   char cfmt[IW_CUSTOM_MAX];
   char custom[IW_CUSTOM_MAX];

   snprintf(cfmt, sizeof(cfmt), "NRX(%s)", fmt);
   va_start(ap, fmt);
   vsnprintf(custom, sizeof(custom), cfmt, ap);
   va_end(ap);

   KDEBUG(TRACE, "EVENT: %s", custom);
   wrqu.data.length = strlen(custom);
   wrqu.data.flags = 0;
   wrqu.data.pointer = (caddr_t)0xdeadbeef;
   wireless_send_event(dev, IWEVCUSTOM, &wrqu, custom);
#else
   KDEBUG(TRACE, "Wireless Extensions too old");
#endif
}

static int
nrx_wxevent_assoc_req_ie(struct net_device *dev)
{
#if WIRELESS_EXT >= 18
   union iwreq_data wrqu;
#endif
   int status;
   unsigned char buf[256];
   size_t buf_len;

   buf_len = sizeof(buf);
   status = WiFiEngine_GetCachedAssocReqIEs(buf, &buf_len);
   if(status == WIFI_ENGINE_SUCCESS && buf_len > 0) {

      KDEBUG_BUF(TRACE, buf, buf_len, "IE");
#if WIRELESS_EXT < 18
      send_custom_event(dev, "ASSOCINFO(ReqIEs=", ")", buf, buf_len);
#else
      wrqu.data.length = buf_len;
      wrqu.data.flags = 0;
      wireless_send_event(dev, IWEVASSOCREQIE, &wrqu, buf);
#endif
   }
      
   return 0;
}


static int
nrx_wxevent_assoc_resp_ie(struct net_device *dev)
{
#if (DE_CCX == CFG_INCLUDED)

#if WIRELESS_EXT >= 18
   union iwreq_data wrqu;
#endif
   int status;
   unsigned char buf[256];
   size_t buf_len;

   buf_len = sizeof(buf);
   status = WiFiEngine_GetCachedAssocCfmIEs(buf, &buf_len);
   if(status == WIFI_ENGINE_SUCCESS && buf_len > 0) {

      KDEBUG_BUF(TRACE, buf, buf_len, "IE");
#if WIRELESS_EXT < 18
      send_custom_event(dev, "ASSOCINFO(RespIEs=", ")", buf, buf_len);
#else
      wrqu.data.length = buf_len;
      wrqu.data.flags = 0;
      wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, buf);
#endif
   }
#endif /* DE_CCX */      
   return 0;
}


int
nrx_wxevent_ap(struct net_device *dev)
{
   struct sockaddr sa;
   int status;
   int sa_len;
   m80211_mac_addr_t bssid;

   KDEBUG(TRACE, "ENTRY");

   nrx_wxevent_assoc_req_ie(dev);
   nrx_wxevent_assoc_resp_ie(dev);

   sa.sa_family = ARPHRD_ETHER;
   sa_len = sizeof(sa.sa_data);
   status = WiFiEngine_GetBSSID(&bssid);
   memcpy(sa.sa_data, bssid.octet, sizeof(bssid.octet));

   if(status != WIFI_ENGINE_SUCCESS) {
       memset(sa.sa_data, 0, sizeof(sa.sa_data));
       printk("[nano] NULL BSSID sent\n");
   }
   else
       printk("[nano] BSSID sent - 0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
              bssid.octet[0], bssid.octet[1], bssid.octet[2],
              bssid.octet[3], bssid.octet[4], bssid.octet[5]);
   
   wireless_send_event(dev, SIOCGIWAP, (union iwreq_data*)&sa, NULL);

   return 0;
}

int
nrx_wxevent_pmkid_candidate(struct net_device *dev,
			 void *bssid,
			 int32_t rssi,
			 uint16_t caps)
{
   union iwreq_data wrqu;

   struct iw_pmkid_cand cand;
   
   KDEBUG(TRACE, "ENTRY");

   wrqu.data.length = sizeof(cand);
   wrqu.data.flags = 0;

   memset(&cand, 0, sizeof(cand));

   cand.bssid.sa_family = ARPHRD_ETHER;
   memcpy(cand.bssid.sa_data, bssid, 6);

   cand.flags = 0;
   if(caps & 1) /* PREAUTH */
      cand.flags |= IW_PMKID_CAND_PREAUTH;

   ASSERT(rssi >= -128 && rssi <= 127);
   cand.index = 127 - rssi; /* smaller is better */

   wireless_send_event(dev, IWEVPMKIDCAND, &wrqu, (char*)&cand);

   return 0;
}

int
nrx_wxevent_michael_mic_failure(struct net_device *dev, 
				void *bssid, 
				int group_failure)
{
   unsigned int keyid = 0;
#if WIRELESS_EXT < 18
   char buf[128];
   char addr[32];
   format_hex(addr, sizeof(addr), bssid, 6);
   snprintf(buf, sizeof(buf), 
	    "MLME-MICHAELMICFAILURE.indication(keyid=%u %s addr=%s)",
	    keyid,
	    group_failure ? "broadcast" : "unicast",
	    addr);
   send_custom_event(dev, buf, "", NULL, 0);
   
#else
   struct iw_michaelmicfailure mic;
   union iwreq_data wrqu;

   wrqu.data.length = sizeof(mic);
   wrqu.data.flags = 0;

   mic.flags = keyid;
   if(group_failure)
      mic.flags |= IW_MICFAILURE_GROUP;
   else
      mic.flags |= IW_MICFAILURE_PAIRWISE;
   mic.src_addr.sa_family = ARPHRD_ETHER;
   memcpy(mic.src_addr.sa_data, bssid, 6);
   memset(mic.tsc, 0, sizeof(mic.tsc));

   wireless_send_event(dev, IWEVMICHAELMICFAILURE, &wrqu, (char*)&mic);

#endif
   return 0;
}

int
nrx_wxevent_scan_complete(struct net_device *dev)
{
   union iwreq_data wrqu;
   wrqu.data.length = 0;
   wrqu.data.flags = 0;

   wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
      
   return 0;
}

int
nrx_wxevent_rssi_trigger(struct net_device *dev, int32_t threshold)
{
   union iwreq_data wrqu;

   wrqu.sens.value = threshold;
   wrqu.sens.fixed = 0;
   wrqu.sens.disabled = 0;
   wrqu.sens.flags = 0;
   
   wireless_send_event(dev, SIOCGIWSENS, &wrqu, NULL);

   return 0;
}


int
nrx_wxevent_device_reset(struct net_device *dev)
{
   send_nrx_event(dev, "RESET");
   return 0;
}

int
nrx_wxevent_connection_lost(struct net_device *dev, we_conn_lost_ind_t *data)
{
   char bssid[13];
   format_hex(bssid, sizeof(bssid), 
              data->bssid.octet, sizeof(data->bssid.octet));

   send_nrx_event(dev, "CONNLOST,type=%#x,bssid=%s,reason=%#x",
                  data->type,
                  bssid,
                  data->reason_code);
   return 0;
}

int
nrx_wxevent_incompatible(struct net_device *dev,
                         we_conn_incompatible_ind_t *data)
{
   send_nrx_event(dev, "CONNINCOMPATIBLE,reason=%#x", data->reason_code);
   return 0;
}

int
nrx_wxevent_scan(struct net_device *dev, 
                 m80211_nrp_mlme_scannotification_ind_t *ind)
{
   unsigned int flags = 0;
   if(ind->flags & SCAN_NOTIFICATION_FLAG_FIRST_HIT)
      flags |= 1; /* NRX_SN_POL_FIRST_HIT */
   if(ind->flags & SCAN_NOTIFICATION_FLAG_JOB_COMPLETE)
      flags |= 2; /* NRX_SN_POL_JOB_COMPLETE */
   if(ind->flags & SCAN_NOTIFICATION_FLAG_BG_PERIOD_COMPLETE)
      flags |= 4; /* NRX_SN_POL_ALL_DONE */
   if(ind->flags & SCAN_NOTIFICATION_FLAG_HIT)
      flags |= 8; /* NRX_SN_POL_JOB_COMPLETE_WITH_HIT */
   /* SCAN_NOTIFICATION_FLAG_DIRECT_SCAN_JOB */
   send_nrx_event(dev, "SCANNOTIFICATION,policy=%#x,jobid=%#x", 
                  flags, ind->job_id);
   return 0;
}

int
nrx_wxevent_mibtrig_ind(struct net_device *dev, 
                          uint32_t trig_id, 
                          uint32_t value)
{
   send_nrx_event(dev, "MIBTRIG,id=%#x,value=%#x", 
                  trig_id,
                  value);
   return 0;
}

int
nrx_wxevent_mibtrig_cancel(struct net_device *dev,
                           uint32_t trig_id,
                           uint32_t reason)
{
   send_nrx_event(dev, "CANCEL,MIBTRIG,id=%#x,reason=%#x", trig_id, reason);
   return 0;
}

int
nrx_wxevent_mibtrig(void *data, size_t len)
{
   we_vir_trig_data_t *p = (we_vir_trig_data_t *)data;
   struct net_device *dev = WiFiEngine_GetAdapter();
   
   DE_BUG_ON(len != sizeof(*p), "Size don't agree");
   
   switch(p->type) {
      case WE_TRIG_TYPE_IND:
         nrx_wxevent_mibtrig_ind(dev, p->trig_id, p->value);
         break;
      case WE_TRIG_TYPE_CANCEL:
         nrx_wxevent_mibtrig_cancel(dev, p->trig_id, p->reason);
         break;
      default: 
         panic("Unknown type");
         break;
   }

   return WIFI_ENGINE_SUCCESS;
}

int
nrx_wxevent_threshold(const char *type_str, void *data, size_t len)
{
   we_vir_trig_data_t *p = (we_vir_trig_data_t *)data;
   struct net_device *dev = WiFiEngine_GetAdapter();

  DE_BUG_ON(len != sizeof(*p), "Size don't agree");

   switch(p->type) {
      case WE_TRIG_TYPE_IND:
         send_nrx_event(dev, "%s,id=%#x,value=%#x", type_str, p->trig_id, p->value);
         break;
      case WE_TRIG_TYPE_CANCEL:
         send_nrx_event(dev, "CANCEL,%s,id=%#x,reason=%#x", type_str, p->trig_id, p->reason);
         break;
      default: 
         panic("Unknown type");
         break;
   }

   return WIFI_ENGINE_SUCCESS;
}

int
nrx_wxevent_rxrate(void *data, size_t len)
{
   return nrx_wxevent_threshold("RXRATE", data, len);
}

int
nrx_wxevent_txrate(void *data, size_t len)
{
   return nrx_wxevent_threshold("TXRATE", data, len);
}

int
nrx_wxevent_no_beacon(void)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   send_nrx_event(dev, "NOBEACON");
   return WIFI_ENGINE_SUCCESS;
}

int
nrx_wxevent_txfail(void)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   send_nrx_event(dev, "TXFAIL");
   return WIFI_ENGINE_SUCCESS;
}
