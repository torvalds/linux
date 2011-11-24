#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22) || defined(CONFIG_WIRELESS_EXT)
#include <net/iw_handler.h>
#if WIRELESS_EXT < 18
#include "wireless_wpa.h"
#endif
#define HAVE_WPA 1

#include "nanonet.h"
#include "nanoutil.h"
#include "nanoparam.h"
#include "wifi_engine.h"
#include "driverenv.h"

#include "mib_defs.h"

/*! @file Wireless tools api implementation. */

#define RTS_MIN 1
#define RTS_MAX 2346
#define RTS_OFF 2348

#define FRAG_MIN 256
#define FRAG_MAX 2346
#define FRAG_OFF 2346

#define DBM2BYTE(N) ((unsigned char)(256 + (N)))

#define DECLARE(X, T, N)                                \
static int                                              \
nrx_iw_##X(struct net_device *dev,                      \
                 struct iw_request_info *iw_info,       \
                 T *N,                                  \
                 void *extra)

#define DECLARE_IW(X, T, N) DECLARE(X, struct iw_##T, N)

#define NOTSUPP(X, T) DECLARE_IW(X, T, dummy) { KDEBUG(TRACE, "ENTRY EOPNOTSUPP"); return -EOPNOTSUPP; }


#define TRACEPARAM(X) KDEBUG(TRACE, "ENTRY flags = %x, value = %d, fixed = %d, disabled = %d", (X)->flags, (X)->value, (X)->fixed, (X)->disabled)

#if WIRELESS_EXT >= 17
#define TXPOW_FLAGS IW_TXPOW_RELATIVE
#define TXPOW_DBM(V) (V)
#else
/* let's fake number, as there is no relative setting; this assumes
 * that max level is 21dBm */
#define TXPOW_FLAGS IW_TXPOW_DBM
#define TXPOW_DBM(V) (21 + (V))
#endif

static inline int
mib_set(const char *mib, uint32_t *tid, void *value, size_t len)
{
   return WiFiEngine_SendMIBSet(mib, tid, value, len);
}

/*!
 * Commit pending changes to driver. Currently does nothing. SIOCSIWCOMMIT
 */
DECLARE(set_commit, void, dummy)
{
   KDEBUG(TRACE, "ENTRY");

   return 0;
}

/*! Get name of wireless protocol used. SIOCGIWNAME
 *  @param name OUT name of wireless protocol
 */
DECLARE(get_name, char, name)
{
   int status;
   int net_status;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);
   
   status = WiFiEngine_GetNetworkStatus(&net_status);
   ASSERT(status == WIFI_ENGINE_SUCCESS);
   if(net_status == 0) {
      strcpy(name, "not connected");
      return 0;
   } else {
        
      strcpy(name, "IEEE 802.11"); /* XXX this should probably
                                      depend on what state we're
                                      in, such as radio-off,
                                      a/b/g etc */
   }

   KDEBUG(TRACE, "EXIT [%s]", name);
    
   return 0;
}


static int
convert_iw_freq2channel(const struct iw_freq *freq, uint8_t *channel)
{
   int32_t mantissa = freq->m;
   int16_t exponent = freq->e;
   
   if(mantissa < 0)
      /* this sometimes mean "auto" */
      return FALSE;

   if(exponent == 0 && mantissa > 0 && mantissa < 256) {
      /* raw channel number, really an abuse of this struct */
      *channel = mantissa;
      return TRUE;
   }
   /* move mantissa to kHz range */
   while(exponent < 3) {
      mantissa /= 10;
      exponent++;
   }
   while(exponent > 3) {
      mantissa *= 10;
      exponent--;
   }
      
   if(WiFiEngine_Frequency2Channel(mantissa, channel) != WIFI_ENGINE_SUCCESS)
      return FALSE;

   return TRUE;
}

/*! Set operating channel or frequency. SIOCSIWFREQ
 * @param freq IN frequency to set f = m*10^e
 */
DECLARE_IW(set_freq, freq, freq)
{
   int status;
   uint8_t channel;
   WiFiEngine_bss_type_t bssType;
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(!convert_iw_freq2channel(freq, &channel)) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }

   status = WiFiEngine_GetBSSType(&bssType);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
   
   switch(bssType) {
      case  M80211_CAPABILITY_ESS:
         WiFiEngine_SetActiveChannel(channel);
         break;
      case M80211_CAPABILITY_IBSS:
         WiFiEngine_SetIBSSTXChannel(channel);
         break;
   }

   KDEBUG(TRACE, "EXIT");
   return 0;
}

/*! Get operating channel or frequency. SIOCGIWFREQ
 *  @param f OUT frequency f = m*10^e
 */
DECLARE_IW(get_freq, freq, f)
{
   WiFiEngine_bss_type_t bssType;
   int status;
   unsigned long freq = 0;
   uint8_t channel;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   status = WiFiEngine_GetBSSType(&bssType);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
   
   switch(bssType) {
      case  M80211_CAPABILITY_ESS:
         status = WiFiEngine_GetActiveChannel(&channel);
         break;
      case M80211_CAPABILITY_IBSS:
         status = WiFiEngine_GetIBSSTXChannel(&channel);
         break;
      default:
         KDEBUG(ERROR, "EXIT EIO");
         return -EIO;
   }         
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO; /* XXX better code */
   }
   status = WiFiEngine_Channel2Frequency(channel, &freq);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO; /* XXX better code */
   }
   
   f->m = freq;
   f->e = 3; /* kHz */

   KDEBUG(TRACE, "EXIT");
   return 0;
}

/* Set BSS mode. SIOCSIWMODE
 * @param mode IN operating mode IW_MODE_INFRA (BSS), IW_MODE_ADHOC (IBSS)
 */
DECLARE(set_mode, __u32, mode)
{
   int status;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   switch (*mode) {
      case IW_MODE_ADHOC:
         status = WiFiEngine_SetBSSType(M80211_CAPABILITY_IBSS);
         break;
      case IW_MODE_INFRA:
      case IW_MODE_AUTO:
         status = WiFiEngine_SetBSSType(M80211_CAPABILITY_ESS);
         break;
      default:
         KDEBUG(ERROR, "EXIT EINVAL");
         return -EINVAL;
   }
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
    
   KDEBUG(TRACE, "EXIT");
   return 0;
}

/* SIOCGIWMODE get BSS mode
   @param mode OUT operating mode
*/
DECLARE(get_mode, __u32, mode)
{
   WiFiEngine_bss_type_t bssType;
   int status;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   status = WiFiEngine_GetBSSType(&bssType);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
    
   switch(bssType) {
      case M80211_CAPABILITY_ESS:
         *mode = IW_MODE_INFRA;
         break;
      case M80211_CAPABILITY_IBSS:
         *mode = IW_MODE_ADHOC;
         break;
      default:
         KDEBUG(ERROR, "Unexpected BSS_Type returned from WiFiEngine_GetBSSType()");
         return -EIO;
   }
    
   KDEBUG(TRACE, "EXIT");
   return 0;
}

static void
channel_to_frequency(uint8_t channel, struct iw_freq *freq)
{
   int status;
   unsigned long khz;

   status = WiFiEngine_Channel2Frequency(channel, &khz);
   if(status != WIFI_ENGINE_SUCCESS) {
      freq->m = 0;
      freq->e = 0;
      freq->i = 0;
      return;
   }
    
   freq->m = khz;
   freq->e = 3;
   freq->i = channel;
}

static inline we_ratemask_t
get_supported_rates(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);

   return sc->supported_rates;
}

static we_ratemask_t
get_operational_rates(struct net_device *dev)
{
   we_ratemask_t m;

   WE_RATEMASK_CLEAR(m);
   WiFiEngine_GetSupportedRates(&m);

   return m;
}

static we_ratemask_t
get_bss_rates(struct net_device *dev)
{
   we_ratemask_t m;

   WE_RATEMASK_CLEAR(m);
   WiFiEngine_GetBSSRates(&m);

   return m;
}

/* helper function to set quality */
static inline void
setqual(struct iw_quality *qual, int32_t rssi, int32_t snr)
{
   if(rssi > 0 || rssi < -255) KDEBUG(ERROR, "rssi out of range (%d)", rssi);

#if WIRELESS_EXT >= 17
   qual->updated = 0;
#endif
   if(snr != SNR_UNKNOWN) {
      qual->qual = snr;
      qual->level = DBM2BYTE(rssi);
      qual->noise = DBM2BYTE(rssi - snr);
   } else {
      qual->qual = 0;
      qual->level = DBM2BYTE(rssi);
      qual->noise = 0;
#if WIRELESS_EXT >= 17
      qual->updated = IW_QUAL_NOISE_INVALID | IW_QUAL_QUAL_INVALID;
#endif
   }
#if WIRELESS_EXT >= 19
   qual->updated |= IW_QUAL_DBM;
#endif
}

static inline int
get_key_index(struct net_device *dev)
{
   int status;
   uint8_t key_index;

   status = WiFiEngine_GetDefaultKeyIndex(&key_index);
   if(status == WIFI_ENGINE_SUCCESS)
      return key_index + 1;

   return 0;
}

/*! Get range of parameters */
DECLARE(get_range, void, dummy)
{
   struct iw_range *iwr = (struct iw_range*)extra;
   int i;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);
   
   memset(iwr, 0, sizeof(*iwr));

   iwr->we_version_compiled = WIRELESS_EXT;
   iwr->we_version_source = 19;

   /* set frequency list */
   iwr->num_channels = 0;
   for(i = 0; i < 14; i++) {
      if(iwr->num_channels == IW_MAX_FREQUENCIES) {
         KDEBUG(TRACE, "reached max frequencies");
         break;
      }
      channel_to_frequency(i + 1, &iwr->freq[iwr->num_channels]);
      iwr->num_channels++;
   }
   iwr->num_frequency = iwr->num_channels;

   /* signal level threshold range */
   iwr->sensitivity = 0;

   {
      we_ratemask_t rates;
      int i;
        
      rates = get_operational_rates(dev);

      WE_RATEMASK_FOREACH(i, rates) {
         if(iwr->num_bitrates == IW_MAX_BITRATES) {
            KDEBUG(TRACE, "reached max bitrates");
            break;
         }
         iwr->bitrate[iwr->num_bitrates++] = WiFiEngine_rate_native2bps(i);
      }
   }
#if (DE_ENABLE_HT_RATES == CFG_ON)
   {
      uint32_t ratemask;
      unsigned int mcs_rate;
      const int mcs_rates[] = { 
         6500000,
         13000000,
         19500000,
         26000000,
         39000000,
         52000000,
         58500000,
         65000000
      };
         
      unsigned int bitpos;

      WiFiEngine_GetEnabledHTRates(&ratemask);

      /* this is a bit backwards, but this is the available tool */
      for(mcs_rate = 0; mcs_rate < ARRAY_SIZE(mcs_rates); mcs_rate++) {
         if(WiFiEngine_RateCodeToBitposition(WE_RATE_DOMAIN_HT,
                                             mcs_rate,
                                             &bitpos) != WIFI_ENGINE_SUCCESS)
            continue;
         if((ratemask & (1 << bitpos)) == 0)
            continue;
         if(iwr->num_bitrates == IW_MAX_BITRATES) {
            KDEBUG(TRACE, "reached max bitrates");
            break;
         }
         iwr->bitrate[iwr->num_bitrates++] = mcs_rates[mcs_rate];
      }
   }
#endif   
   setqual(&iwr->max_qual, -104, 30);
   setqual(&iwr->avg_qual, -52, 15);

   iwr->txpower_capa = TXPOW_FLAGS;
   iwr->num_txpower = 0;
   iwr->txpower[iwr->num_txpower++] = TXPOW_DBM(0);
   iwr->txpower[iwr->num_txpower++] = TXPOW_DBM(-19);

   iwr->min_rts = RTS_MIN;
   iwr->max_rts = RTS_MAX;

   iwr->min_frag = FRAG_MIN;
   iwr->max_frag = FRAG_MAX;


   iwr->encoding_size[0] = 5;
   iwr->encoding_size[1] = 13;
   iwr->num_encoding_sizes = 2;
   iwr->max_encoding_tokens = WIFI_ENGINE_MAX_WEP_KEYS;
   
#if WIRELESS_EXT >= 17
   iwr->encoding_login_index = get_key_index(dev);
#endif

#if WIRELESS_EXT >= 17
   IW_EVENT_CAPA_SET_KERNEL(iwr->event_capa);
#endif

#if WIRELESS_EXT >= 18
   iwr->enc_capa = IW_ENC_CAPA_WPA
      | IW_ENC_CAPA_WPA2
      | IW_ENC_CAPA_CIPHER_TKIP
      | IW_ENC_CAPA_CIPHER_CCMP;
#endif

   KDEBUG(TRACE, "EXIT");
   return 0;
}


DECLARE_IW(set_essid, point, essid)
{
   int status;
   int length;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(essid->flags != 0 && essid->length != 0) {
#if WIRELESS_EXT > 20
      length = essid->length;
#else
      length = essid->length - 1;
#endif
   } else
      length = 0;

   /* XXX check for same essid */
   status = WiFiEngine_SetSSID(extra, length);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   return 0;
}

DECLARE_IW(get_essid, point, essid)
{
   int status;
   m80211_ie_ssid_t ssid;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

#if WIRELESS_EXT <= 20
   /* this will handle the bogus zero termination */
   memset(extra, 0, essid->length);
#endif

   status = WiFiEngine_GetSSID(&ssid);
   if(status != WIFI_ENGINE_SUCCESS) {
      essid->flags = 0;
      essid->length = 0;
      return 0;
   }

   memcpy(extra, ssid.ssid, ssid.hdr.len);
   essid->flags = 1; /* active */
   essid->length = ssid.hdr.len;
    
   return 0;
}


static int 
zero_bssid(const m80211_mac_addr_t *bssid)
{
   char all_zero[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
   return !memcmp(bssid->octet, all_zero, sizeof(all_zero));
}

/* associate with the AP with cell identity specified in ap_addr
   if it's all zeros, skip the association, but keep the ap
   if it's the broadcast address, reassociate with the best ap
*/

DECLARE(set_ap, struct sockaddr, ap_addr)
{
   int status;
   m80211_mac_addr_t bssid;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if (ap_addr->sa_family != ARPHRD_ETHER) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }

   memcpy(bssid.octet, ap_addr->sa_data, sizeof(bssid));
   if(zero_bssid(&bssid)) {
      /* all zeros really mean that we should keep the association,
         but allow for roaming, for now treat it the same as a
         re-associate */
      memset(bssid.octet, 0xff, sizeof(bssid));
   }

   /* check for the same address */

   status = WiFiEngine_SetBSSID(&bssid);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   return 0;
}

DECLARE(get_ap, struct sockaddr, ap_addr)
{
   int status;
   m80211_mac_addr_t bssid;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   ap_addr->sa_family = ARPHRD_ETHER;
   
   status = WiFiEngine_GetBSSID(&bssid);
   if(status == WIFI_ENGINE_SUCCESS)
      memcpy(ap_addr->sa_data, bssid.octet, sizeof(bssid.octet));
   else
      memset(ap_addr->sa_data, 0, ETH_ALEN);

   return 0;
}

struct scan_data {
   struct kref kref;
   wait_queue_head_t wq;
   int scan_complete;

   uint32_t sj_id;
   struct iobject *ih;
   uint16_t channel_interval;
   uint32_t probe_delay;
   uint16_t min_channel_time;
   uint16_t max_channel_time;
};

static struct semaphore scan_sem;

static void scan_data_release(struct kref *kref)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
#ifdef CONFIG_HAS_WAKELOCK	   
   struct nrx_softc *sc = netdev_priv(dev);
#endif   
   struct scan_data *sd = container_of(kref, struct scan_data, kref);
   if(sd->sj_id != ~0)
      WiFiEngine_RemoveScanJob(sd->sj_id, NULL);
   we_ind_deregister_null(&sd->ih);
   DriverEnvironment_Free(sd);
   nrx_wxevent_scan_complete(dev);
   up(&scan_sem);
#ifdef CONFIG_HAS_WAKELOCK
   wake_unlock(&sc->nrx_scan_wake_lock);
   KDEBUG(TRACE, "Released nrx_scan_wake_lock");
#endif   
}

static void scan_req_complete(wi_msg_param_t param, void *priv)
{
   m80211_nrp_mlme_scannotification_ind_t *ind = param;   
   struct scan_data *sd = priv;

   /* scan job has completed, remove job */

   if(ind != NULL) {
      KDEBUG(TRACE, "SIOCSIWSCAN ind->job_id = %u, sj_id = %u", 
             ind->job_id, sd->sj_id);
   } else {
      KDEBUG(TRACE, "SIOCSIWSCAN ind = NULL");
   }

   if(ind == NULL) {
      /* We failed to trigger the scan job, either because firmware is
       * in transit state, it because of a core dump. We choose to
       * remove the scan job here, although it would be possible to
       * re-trigger it, but that would ideally require a timeout and a
       * max retry count -- this complicates things too much, and
       * we're holding a mutex during this time, and possibly also
       * rtnl_lock. */
      sd->scan_complete = 2;
      wake_up(&sd->wq);
      kref_put(&sd->kref, scan_data_release);
   } else if(ind->job_id == sd->sj_id) {
      sd->scan_complete = 1;
      wake_up(&sd->wq);
      kref_put(&sd->kref, scan_data_release);
   }
}

static int scan_req_added(we_cb_container_t *cbc)
{
   int status;
   struct scan_data *sd = cbc->ctx;

   KDEBUG(TRACE, "SIOCSIWSCAN status = %d", cbc->status);
   if(cbc->status == 0) {
      /* scan job is added, so now we trigger it */
      sd->ih = we_ind_register(WE_IND_SCAN_COMPLETE, "SIOCSIWSCAN", 
                               scan_req_complete, NULL, 0, sd);
      status = WiFiEngine_TriggerScanJobEx(sd->sj_id, 
                                           sd->channel_interval, 
                                           sd->probe_delay, 
                                           sd->min_channel_time, 
                                           sd->max_channel_time);
      if(status != WIFI_ENGINE_SUCCESS) {
         KDEBUG(TRACE, "failed triggering scan job %d", status);
         sd->scan_complete = 2;
         wake_up(&sd->wq);
         kref_put(&sd->kref, scan_data_release);
      }
   } else {
      sd->scan_complete = 2;
      wake_up(&sd->wq);
      kref_put(&sd->kref, scan_data_release);
   }
   return 0;
}

static int scan_req(__u16 flags, struct iw_scan_req *req)
{
   int status;
   struct scan_data *sd;
   m80211_ie_ssid_t ssid;
   m80211_mac_addr_t bssid;
   unsigned int i;
   uint8_t scan_type = ACTIVE_SCAN;
   channel_list_t channels;
   we_cb_container_t *cbc;
#ifdef CONFIG_HAS_WAKELOCK   
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
#endif

   if(req == NULL)
      flags = 0; /* no parameters so ignore all flags */

   if((flags & IW_SCAN_THIS_ESSID) != 0) {
      if(req->essid_len > sizeof(ssid.ssid)){
      	 up(&scan_sem);
#ifdef CONFIG_HAS_WAKELOCK
		 wake_unlock(&sc->nrx_scan_wake_lock);
		 KDEBUG(TRACE, "Released nrx_scan_wake_lock");
#endif        	 
         return -EINVAL;
      }   
      DE_MEMCPY(ssid.ssid, req->essid, req->essid_len);
      ssid.hdr.len = req->essid_len;
      ssid.hdr.id = M80211_IE_ID_SSID;
   } else {
      DE_MEMSET(&ssid, 0, sizeof(ssid));
      ssid.hdr.id = M80211_IE_ID_NOT_USED;
   }
   if(req != NULL && req->bssid.sa_family == ARPHRD_ETHER) {
      DE_MEMCPY(bssid.octet, req->bssid.sa_data, ETH_ALEN);
   } else {
      DE_MEMSET(&bssid, 0xff, sizeof(bssid));
   }

   if((flags & IW_SCAN_THIS_FREQ) != 0 && req->num_channels > 0) {
      channels.no_channels = 0;
      for(i = 0; i < req->num_channels; i++) {
         if(!convert_iw_freq2channel(&req->channel_list[i], 
                                     &channels.channelList[channels.no_channels])) {
            up(&scan_sem);
#ifdef CONFIG_HAS_WAKELOCK
			wake_unlock(&sc->nrx_scan_wake_lock);
			KDEBUG(TRACE, "Released nrx_scan_wake_lock");
#endif              
            return -EINVAL;
         }
         channels.no_channels++;
      }
   } else {
      status = WiFiEngine_GetRegionalChannels(&channels);
      if(status != WIFI_ENGINE_SUCCESS) {
         up(&scan_sem);
#ifdef CONFIG_HAS_WAKELOCK
		 wake_unlock(&sc->nrx_scan_wake_lock);
		 KDEBUG(TRACE, "Released nrx_scan_wake_lock");
#endif           
         return -EIO; /* shouldn't happen */
      }
   }
   if((flags & IW_SCAN_THIS_MODE) != 0
      && req->scan_type == IW_SCAN_TYPE_PASSIVE)
      scan_type = PASSIVE_SCAN;

   sd = DriverEnvironment_Malloc(sizeof(*sd));
   if(sd == NULL) {
      up(&scan_sem);
#ifdef CONFIG_HAS_WAKELOCK
      wake_unlock(&sc->nrx_scan_wake_lock);
      KDEBUG(TRACE, "Released nrx_scan_wake_lock");
#endif
      return -ENOMEM;
   }

   kref_init(&sd->kref);
   init_waitqueue_head(&sd->wq);
   sd->scan_complete = 0;
   sd->ih = NULL;

   sd->channel_interval = 10; /* XXX what to choose */
   sd->probe_delay = 0; /* use default */

   if(req != NULL && req->min_channel_time != 0)
      sd->min_channel_time = req->min_channel_time;
   else
      sd->min_channel_time = 0;

   if(req != NULL 
      && req->max_channel_time != 0 
      && req->max_channel_time > req->min_channel_time)
      /* not correct for min_channel_time == 0 */
      sd->max_channel_time = req->max_channel_time - req->min_channel_time;
   else
      sd->max_channel_time = 0;

   cbc = WiFiEngine_BuildCBC(scan_req_added, sd, sizeof(*sd), FALSE);
   if(cbc == NULL) {
      kref_put(&sd->kref, scan_data_release);
      return -ENOMEM;
   }
   kref_get(&sd->kref); /* owned by cbc */

   status = WiFiEngine_AddScanJob(&sd->sj_id,
                                  ssid,
                                  bssid,
                                  scan_type,
                                  channels,
                                  ANY_MODE,
                                  33, /* priority */
                                  FALSE,
                                  -1,
                                  1,
                                  cbc);
   if(status != WIFI_ENGINE_SUCCESS)  {
      KDEBUG(TRACE, "SIOCSIWSCAN failed to add scan job, %d", status);
      WiFiEngine_CancelCallback(cbc);
      kref_put(&sd->kref, scan_data_release);
      return -EIO;
   }

   KDEBUG(TRACE, "SIOCSIWSCAN created scan job id %u", sd->sj_id);
#ifndef SCANREQ_NOWAIT
   /* wait for scan request completion */
   wait_event_interruptible(sd->wq, sd->scan_complete != 0);
#endif
   kref_put(&sd->kref, scan_data_release);

   return 0;
}

DECLARE_IW(set_scan, point, dummy)
{
   int status;
    
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

#ifdef CONFIG_HAS_WAKELOCK   
   struct nrx_softc *sc = netdev_priv(dev); 
#endif
   if(dummy->length != 0 && dummy->length != sizeof(struct iw_scan_req)) {
      KDEBUG(TRACE, "EXIT EINVAL");
      return -EINVAL;
   }

   if (WiFiEngine_IsDelayPowerSaveTimerRunning()) {
      KDEBUG(TRACE, "EXIT EINTR");
   //   return -EINTR;
   }
#ifdef SCANREQ_NONBLOCK
   /* if a scan is in progress, just deny this request */
   if(down_trylock(&scan_sem) != 0)
      return -EBUSY;
#else
   /* prevent multiple concurrent scans */
   if(down_interruptible(&scan_sem))
      return -EINTR;
#endif
#ifdef CONFIG_HAS_WAKELOCK
	  wake_lock(&sc->nrx_scan_wake_lock);
	  KDEBUG(TRACE, "Acquired nrx_scan_wake_lock");
#endif

   status = scan_req(dummy->flags, extra);

#if 0
   status = WiFiEngine_StartNetworkScan();
   if(status == WIFI_ENGINE_FAILURE_DEFER) {
      KDEBUG(TRACE, "WiFiEngine_StartNetworkScan was busy");
      return 0;
   }
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "WiFiEngine_StartNetworkScan = %d", status);
      return -EIO;
   }
#endif

   return status;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#define STREAM_ADD(N, ...) iwe_stream_add_##N(iw_info, __VA_ARGS__)
#else
#define STREAM_ADD(N, ...) iwe_stream_add_##N(__VA_ARGS__)
#endif

static inline char*
add_rates(struct iw_request_info *iw_info, 
	  char *start, 
	  char *foo, 
	  char *stop, 
	  uint8_t *rates, 
	  size_t len)
{
   char *foo2;
   struct iw_event iwe;
   size_t i;

   iwe.cmd = SIOCGIWRATE;
   iwe.u.bitrate.fixed = 0;
   iwe.u.bitrate.disabled = 0;
   for(i = 0; i < len; i++) {
      iwe.u.bitrate.value = (rates[i] & 0x7f) * 500000;
      foo2 = STREAM_ADD(value, start, foo, stop, &iwe, IW_EV_PARAM_LEN);
      if(foo2 == foo) {
         /* failed to add rate */
         return start;
      }
      foo = foo2;
   }
   return foo;
}

DECLARE_IW(get_scan, point, data) 
{
   int status;
   WiFiEngine_net_t **netlist;
   int i;
   int num_nets = 0;
   int ret = 0;


   char *start;
   char *stop;
   char *foo;
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(nano_scan_wait) {
      if(WiFiEngine_ScanInProgress())
         return -EAGAIN;
   }

   status = WiFiEngine_GetScanList(NULL, &num_nets);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   netlist = kzalloc(num_nets * sizeof(*netlist), GFP_KERNEL);
   if(netlist == NULL) {
      KDEBUG(ERROR, "out of memory");
      return -ENOMEM;
   }
   status = WiFiEngine_GetScanList(netlist, &num_nets);
   if(status != WIFI_ENGINE_SUCCESS) {
      kfree(netlist);
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   start = extra;
   stop = start + data->length;

#define CHECK(C)                                \
   ({                                           \
      char *__end = (C);                        \
      if(__end == start) {                      \
         ret = -E2BIG;                          \
         start = first;                         \
         break;                                 \
      }                                         \
      start = __end;                            \
   })

   for(i = 0; i < num_nets; i++) {
      struct iw_event iwe;
      char *first = start;
      mlme_bss_description_t *bss_p = netlist[i]->bss_p;

      /* AP address */
      iwe.cmd = SIOCGIWAP;
      iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
      memcpy(iwe.u.ap_addr.sa_data, netlist[i]->bssId_AP.octet, ETH_ALEN);
      CHECK(STREAM_ADD(event, start, stop, &iwe, IW_EV_ADDR_LEN));


      /* ESSID */
      iwe.cmd = SIOCGIWESSID;
      iwe.u.data.flags = 1;
      iwe.u.data.length = bss_p->bss_description_p->ie.ssid.hdr.len;
      CHECK(STREAM_ADD(point, start, stop, &iwe, bss_p->bss_description_p->ie.ssid.ssid));
        
      /* mode */
      if(M80211_IS_ESS(bss_p->bss_description_p->capability_info)) {
         iwe.cmd = SIOCGIWMODE;
         iwe.u.mode = IW_MODE_MASTER;
         CHECK(STREAM_ADD(event, start, stop, &iwe, IW_EV_UINT_LEN));
      } else if(M80211_IS_IBSS(bss_p->bss_description_p->capability_info)) {
         iwe.cmd = SIOCGIWMODE;
         iwe.u.mode = IW_MODE_ADHOC;
         CHECK(STREAM_ADD(event, start, stop, &iwe, IW_EV_UINT_LEN));
      }
            
      /* Add encryption capability */
      iwe.cmd = SIOCGIWENCODE;
      if (bss_p->bss_description_p->capability_info & M80211_CAPABILITY_PRIVACY)
         iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
      else
         iwe.u.data.flags = IW_ENCODE_DISABLED;
      iwe.u.data.length = 0;
      CHECK(STREAM_ADD(point, start, stop, &iwe, ""));

#if HAVE_WPA
      {
         char buf[256];
         size_t len;
         int status;
   
         len = sizeof(buf);
         status = WiFiEngine_PackRSNIE(&bss_p->bss_description_p->ie.rsn_parameter_set, buf, &len);
         if(status == WIFI_ENGINE_SUCCESS && len > 0 && len <= sizeof(buf)) {
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = len;
            CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
         }

         len = sizeof(buf);
         status = WiFiEngine_PackWPAIE(&bss_p->bss_description_p->ie.wpa_parameter_set, buf, &len);
         if(status == WIFI_ENGINE_SUCCESS && len > 0 && len <= sizeof(buf)) {
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = len;
            CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
         }

#ifdef WAPI_SUPPORT
         len = sizeof(buf);
         status = WiFiEngine_PackWAPIIE(&bss_p->bss_description_p->ie.wapi_parameter_set, buf, &len);
         if(status == WIFI_ENGINE_SUCCESS && len > 0 && len <= sizeof(buf)) {
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = len;
            CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
         }
#endif

         len = sizeof(buf);
         status = WiFiEngine_PackWPSIE(&bss_p->bss_description_p->ie.wps_parameter_set, buf, &len);
         if(status == WIFI_ENGINE_SUCCESS && len > 0 && len <= sizeof(buf)) {
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = len;
            CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
         }
         
         len = sizeof(buf);
         status = WiFiEngine_PackWMMPE(&bss_p->bss_description_p->ie.wmm_parameter_element, buf, &len);
         if(status == WIFI_ENGINE_SUCCESS && len > 0 && len <= sizeof(buf)) {
            iwe.cmd = IWEVGENIE;
            iwe.u.data.length = len;
            CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
         }
      }
#endif
#if WIRELESS_EXT >= 15
      {
         iwe.cmd = IWEVQUAL;
         setqual(&iwe.u.qual, bss_p->rssi_info, bss_p->snr_info);
         CHECK(STREAM_ADD(event, start, stop, &iwe, IW_EV_QUAL_LEN));
      }
#endif /* WIRELESS_EXT >= 15 */
      /* channel */
      iwe.cmd = SIOCGIWFREQ;
      channel_to_frequency(bss_p->bss_description_p->ie.ds_parameter_set.channel, &iwe.u.freq);
      CHECK(STREAM_ADD(event, start, stop, &iwe, IW_EV_FREQ_LEN));
        
      /* SIOCGIWRATE */
      foo = start + IW_EV_LCP_LEN;
      iwe.cmd = SIOCGIWRATE;
      iwe.u.bitrate.fixed = 0;
      iwe.u.bitrate.disabled = 0;
      if(bss_p->bss_description_p->ie.supported_rate_set.hdr.id == M80211_IE_ID_SUPPORTED_RATES) {
         foo = add_rates(iw_info, start, foo, stop, 
                         bss_p->bss_description_p->ie.supported_rate_set.rates,
                         bss_p->bss_description_p->ie.supported_rate_set.hdr.len);
         if(foo == start) {
            ret = -E2BIG;
            start = first;
            break;
         }
      }
      if(bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.id == M80211_IE_ID_EXTENDED_SUPPORTED_RATES) {
         foo = add_rates(iw_info, start, foo, stop, 
                         bss_p->bss_description_p->ie.ext_supported_rate_set.rates,
                         bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.len);
         if(foo == start) {
            ret = -E2BIG;
            start = first;
            break;
         }
      }
      if((size_t)(foo - start) >= IW_EV_PARAM_LEN)
         start = foo;

      if(bss_p->snr_info != SNR_UNKNOWN) {
        /* Extra parameter output */
        /* SNR */       
        
        char buf[256];
        size_t len;
        
        /* Extra field */
        iwe.cmd = IWEVCUSTOM;
        len = snprintf(buf, sizeof(buf),
                      "Signal to noise: %d",
                      bss_p->snr_info);
        iwe.u.data.length = len;
        iwe.u.data.flags = 0;
        CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
      }

      {
        /* Extra parameter output */
        /* Beacon period */     
        
        char buf[256];
        size_t len;
        
        /* Extra field */
        iwe.cmd = IWEVCUSTOM;
        len = snprintf(buf, sizeof(buf),
                      "Beacon period: %d Kusec",
                      bss_p->bss_description_p->beacon_period);
        iwe.u.data.length = len;
        iwe.u.data.flags = 0;
        CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
      }

      if (bss_p->dtim_period)     /* Skip when we don't have dtim info */
      {
        /* Extra parameter output */
        /* DTIM period */       
        
        char buf[256];
        size_t len;
        
        /* Extra field */
        iwe.cmd = IWEVCUSTOM;
        len = snprintf(buf, sizeof(buf),
                      "DTIM period: %d",
                      bss_p->dtim_period);
        iwe.u.data.length = len;
        iwe.u.data.flags = 0;
        CHECK(STREAM_ADD(point, start, stop, &iwe, buf));
      }
   }
#undef CHECK

   WiFiEngine_ReleaseScanList(netlist, num_nets);
   kfree(netlist);

   data->length = start - (char*)extra;
   data->flags = 0;

#if WIRELESS_EXT < 17
   /* fixed size anyway, can just as well return ok */
   if(ret == -E2BIG)
      ret = 0;
#endif

   return ret;
}

/*! set used bitrates
 *  if not fixed, use speeds up to set rate
 * @param bitrate IN value = bps, fixed = 0: support up to, 1: support only
 */

DECLARE_IW(set_rate, param, bitrate)
{
   int status;
   we_ratemask_t supported_rates;
   we_ratemask_t rate_mask;
   we_xmit_rate_t rate;

   uint8_t rates[WE_XMIT_RATE_NUM_RATES];
   int num_rates;
   int b;

   KDEBUG(TRACE, "ENTRY value = %d, fixed = %d", 
          bitrate->value, bitrate->fixed);
   CHECK_UNPLUG(dev);


   WE_RATEMASK_CLEAR(rate_mask);

   supported_rates = get_supported_rates(dev);
   if(bitrate->fixed == 0 && bitrate->value == -1)
      DE_MEMSET(&rate_mask, 0xff, sizeof(rate_mask));
   else {
      int i;

      rate = WiFiEngine_rate_bps2native(bitrate->value);
      if(rate == WE_XMIT_RATE_INVALID) {
         KDEBUG(ERROR, "invalid rate %d", bitrate->value);
         return -EINVAL;
      }
      if(bitrate->fixed == 0)
         for(i = 0; i <= rate; i++)
            WE_RATEMASK_SETRATE(rate_mask, i);
      else
         WE_RATEMASK_SETRATE(rate_mask, rate);
   }
      
   WE_RATEMASK_AND(supported_rates, rate_mask);
   
   memset(rates, 0, sizeof(rates));

   num_rates = 0;
   WE_RATEMASK_FOREACH(b, supported_rates) {
      rates[num_rates++] = WiFiEngine_rate_native2ieee(b);
   }
   if(num_rates == 0) {
      KDEBUG(ERROR, "no common bitrate");
      return -EINVAL;
   }
    
   status = WiFiEngine_SetSupportedRates(rates, num_rates);

   status = mib_set(MIB_dot11OperationalRatesSet, NULL, rates, num_rates);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   return 0;
}

/*! return current bitrate 
 * In practice we return the max supported rate of the BSS
 */
DECLARE_IW(get_rate, param, bitrate)
{
   we_ratemask_t bss_mask, op_mask;
   int i;
   int rate;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(WiFiEngine_GetCurrentTxRate(&rate) == WIFI_ENGINE_SUCCESS) {
#ifdef WORKAROUND_MISSING_DATA_HT_RATE_SUPPORT
      /* ht rates are reported as 127 (max bg rate value), convert
       * this to something that's at least a proper ht rate */
      if(rate == 127)
         rate = 130; /* 65 MBit/s */
#endif
      bitrate->value = rate * 500000;
      bitrate->fixed = 1;
      return 0;
   }
   
   bss_mask = get_bss_rates(dev);
   op_mask = get_supported_rates(dev);
   WE_RATEMASK_AND(bss_mask, op_mask);

   for(i = WE_XMIT_RATE_MAX_RATE; i >= 0; i--) 
      if(WE_RATEMASK_TESTRATE(bss_mask, i))
         break;

   if(i < 0) {
      bitrate->value = 0;
      bitrate->fixed = 0;
      return 0;
   }
   bitrate->value = WiFiEngine_rate_native2bps(i);
   bitrate->fixed = 1;
   return 0;
}

static int
set_param(struct iw_param *param, int min_val, int max_val, int disabled_val)
{
   if(param->fixed == 0)
      return -EINVAL;

   if(param->disabled)
      return disabled_val;

   if(param->value < min_val || param->value > max_val)
      return -EINVAL;

   return param->value;
}

static void
get_param(struct iw_param *param, int value, int disabled_val)
{
   if(value == disabled_val) {
      param->value = 0;
      param->fixed = 1;
      param->disabled = 1;
   } else {
      param->value = value;
      param->fixed = 1;
      param->disabled = 0;
   }
}

/* rts.disabled -> max size
   rts.fixed       on/off
   rts.value       threshold
*/
DECLARE_IW(set_rts, param, rts)
{
   int status;
   int value;
    
   TRACEPARAM(rts);
   CHECK_UNPLUG(dev);

   value = set_param(rts, RTS_MIN, RTS_MAX, RTS_OFF);
   if(value < 0) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return value;
   }
   
   status = WiFiEngine_SetRTSThreshold(value);
        
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
   return 0;
}

DECLARE_IW(get_rts, param, rts)
{
   int status;
   int value;
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

      
#if 0
   {
      uint16_t tmp_val;
      size_t len = sizeof(tmp_val);
      status = nrx_get_mib(dev, MIB_dot11RTSThreshold, &tmp_val, &len);
      
      if(status != 0) {
         return status;
      }
      if(len != sizeof(tmp_val)) {
         return -EIO;
      }
      value = tmp_val;
   }
#else
   status = WiFiEngine_GetRTSThreshold(&value);

   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
#endif

      
   get_param(rts, value, RTS_OFF);

   return 0;
}

/* set fragmentation threshold
 */
DECLARE_IW(set_frag, param, frag)
{
   int status;
   int value;
    
   TRACEPARAM(frag);
   CHECK_UNPLUG(dev);

   value = set_param(frag, FRAG_MIN, FRAG_MAX, FRAG_OFF);
   if(value < 0) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return value;
   }
   
   status = WiFiEngine_SetFragmentationThreshold(value);
        
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
   return 0;
}

DECLARE_IW(get_frag, param, frag)
{
   int status;
   int value;
    
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   status = WiFiEngine_GetFragmentationThreshold(&value);

   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   get_param(frag, value, FRAG_OFF);

   return 0;
}


/*
  txpower.disabled   turn radio off
  txpower.flags      IW_TXPOW_DBM / MWATT
  txpower.value      power
  txpower.fixed      0 == auto
*/
DECLARE_IW(set_txpow, param, txpower)
{
   int status;

   TRACEPARAM(txpower);
   CHECK_UNPLUG(dev);
    
   if(txpower->disabled == 1) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   if(txpower->value < TXPOW_DBM(-19) || txpower->value > TXPOW_DBM(0)) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   txpower->value -= TXPOW_DBM(0);

   ASSERT(txpower->value >= -19 && txpower->value <= 0);

   status = WiFiEngine_SetTxPowerLevel(txpower->value, txpower->value);

   if(status == WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED) {
      KDEBUG(ERROR, "EXIT EOPNOTSUPP");
      return -EOPNOTSUPP;
   }

   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }

   return 0;
}

DECLARE_IW(get_txpow, param, txpower)
{
   int status;
   int qpsk_level, ofdm_level;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

#if 1
   {
      uint8_t buf[16];
      size_t len = sizeof(buf);
      status = nrx_get_mib(dev, MIB_dot11powerIndex, buf, &len);
      if(status != 0)
         return status;
      if(len == 1) {
         ofdm_level = -(int)buf[0];
         qpsk_level = 0;
      } else if(len == 2) {
         ofdm_level = -(int)buf[0];
         qpsk_level = -(int)buf[1];
      } else {
         KDEBUG(ERROR, "got more than two bytes");
         return -EIO;
      }
   }
#else
   status = WiFiEngine_GetTxPowerLevel(&qpsk_level, &ofdm_level);

   if(status == WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED) {
      KDEBUG(ERROR, "EXIT EOPNOTSUPP");
      return -EOPNOTSUPP;
   }

   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "EXIT EIO");
      return -EIO;
   }
#endif

   if(qpsk_level != ofdm_level)
      KDEBUG(TRACE, "qpsk = %d, ofdm = %d", qpsk_level, ofdm_level);

   txpower->value = TXPOW_DBM(ofdm_level);
   txpower->fixed = 0;
   txpower->flags = TXPOW_FLAGS;
   txpower->disabled = 0;

   return 0;
}


/*! signal level where packets are dropped */
NOTSUPP(set_sens, param);

NOTSUPP(get_sens, param);

NOTSUPP(get_aplist, point);

DECLARE_IW(set_retry, param, retry)
{
   uint32_t tid;
   int status;
   uint8_t value;
   const char *mib;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if((retry->flags & IW_RETRY_TYPE) != IW_RETRY_LIMIT) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   if(retry->value < 0 || retry->value > 255) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   switch(retry->flags & IW_RETRY_MODIFIER) {
      case IW_RETRY_MAX:
         mib = MIB_dot11LongRetryLimit;
         break;
      case IW_RETRY_MIN:
         mib = MIB_dot11ShortRetryLimit;
         break;
      default:
         return -EINVAL;
   }
   value = retry->value;
   status = mib_set(mib, &tid, &value, sizeof(value));

   if(status == WIFI_ENGINE_SUCCESS)
      return 0;

   KDEBUG(ERROR, "EXIT EIO");
   return -EIO;
}


DECLARE_IW(get_retry, param, retry)
{
   int status = WIFI_ENGINE_SUCCESS;
   uint8_t value = 0;
   size_t len = sizeof(value);

   TRACEPARAM(retry);
   CHECK_UNPLUG(dev);

   switch(retry->flags & IW_RETRY_TYPE) {
      case IW_RETRY_LIMIT:
      case 0:
         break;
      default:
         KDEBUG(ERROR, "EXIT EINVAL");
         return -EINVAL;
   }
   retry->disabled = 0;
   retry->fixed = 0;

   switch(retry->flags & IW_RETRY_MODIFIER) {
      case IW_RETRY_MAX:
         status = nrx_get_mib(dev, MIB_dot11LongRetryLimit, &value, &len);
         break;
      case IW_RETRY_MIN:
         status = nrx_get_mib(dev, MIB_dot11ShortRetryLimit, &value, &len);
         break;
      default:
         return -EINVAL;
   }
   
   if(status != 0) {
      KDEBUG(ERROR, "EXIT EIO");
      return status;
   }
   retry->flags = (retry->flags & IW_RETRY_MODIFIER) | IW_RETRY_LIMIT;

   if((retry->value = value) == 0)
      retry->disabled = 1;

   return 0;
}

static inline void
copy_key(struct nrx_softc *sc, 
         unsigned int index, 
         struct iw_point *encoding, 
         void *extra)
{
   int status;
   size_t key_len = IW_ENCODING_TOKEN_MAX;
   status = WiFiEngine_GetKey(index, extra, &key_len);
   
   
   if(status != WIFI_ENGINE_SUCCESS) {
      encoding->flags |= IW_ENCODE_DISABLED;
      return;
   }
   
   encoding->length = key_len;
}

DECLARE_IW(get_encode, point, encoding)
{
   int status;
   struct nrx_softc *sc = netdev_priv(dev);
   WiFiEngine_Auth_t amode;
   int32_t key_index;
   int index_present = 0;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   key_index = encoding->flags & IW_ENCODE_INDEX;
   if(key_index == 0)
      key_index = get_key_index(dev);
   else
      index_present = 1;
   
   key_index--;

   encoding->flags = key_index + 1;
   copy_key(sc, key_index, encoding, extra);
   
   status = WiFiEngine_GetAuthenticationMode(&amode);
   if(status == WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "amode = %d", amode);
      if(amode == Authentication_Open)
         encoding->flags |= IW_ENCODE_OPEN;
   }
    
   if(amode == Authentication_Shared)
      encoding->flags |= IW_ENCODE_RESTRICTED;

   return 0;
}

#if HAVE_WPA
DECLARE_IW(set_genie, point, ie)
{
   KDEBUG(TRACE, "ENTRY flags = %x", ie->flags);

   KDEBUG_BUF(TRACE, extra, ie->length, "genie");
   
   /* Returning an error here is correct, but sadly the supplicant
    * gets confused by this. The actual association IEs will be
    * returned in an event later on. */

   return 0;
}

DECLARE_IW(get_genie, point, ie)
{
   KDEBUG(TRACE, "ENTRY flags = %x", ie->flags);

   return -EOPNOTSUPP;
}

DECLARE_IW(set_mlme, point, point)
{
   struct iw_mlme *mlme = extra;
   int status;
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(sizeof(*mlme) != point->length) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }
   if(mlme->addr.sa_family != ARPHRD_ETHER) {
      KDEBUG(ERROR, "EXIT EINVAL");
      return -EINVAL;
   }

   switch(mlme->cmd) {
      case IW_MLME_DEAUTH:
         KDEBUG(TRACE, "mlme deauthenticate, reason = %d", mlme->reason_code);
         status = WiFiEngine_sac_stop();
         break;
      case IW_MLME_DISASSOC:
         KDEBUG(TRACE, "mlme disassociate, reason = %d", mlme->reason_code);
         status = WiFiEngine_sac_stop();
         break;
      default:
         KDEBUG(TRACE, "unknown mlme command %d, reason = %d", 
                mlme->cmd, mlme->reason_code);
         return -EINVAL;
   }
   if(status == WIFI_ENGINE_SUCCESS)
      return 0;
   KDEBUG(ERROR, "EXIT EIO: status = %d", status);
   return -EIO;
}

/* wireless extensions don't support wapi yet...*/
#ifdef WAPI_SUPPORT
#define IW_AUTH_KEY_MGMT_WAPI_CERT 4
#define IW_AUTH_KEY_MGMT_WAPI_PSK  8
#endif

static void
set_auth_mode(struct nrx_softc *sc) 
{
   WiFiEngine_Auth_t amode = Authentication_Open;
   
   switch(sc->auth_param[IW_AUTH_80211_AUTH_ALG]) {
      case IW_AUTH_ALG_OPEN_SYSTEM | IW_AUTH_ALG_SHARED_KEY:
         amode = Authentication_Autoselect;
         break;
      case IW_AUTH_ALG_SHARED_KEY:
         amode = Authentication_Shared;
         break;
      case IW_AUTH_ALG_LEAP:
         amode = Authentication_8021X;
         break;
      case IW_AUTH_ALG_OPEN_SYSTEM:
      default:
         amode = Authentication_Open;
         break;
   }

#ifdef WAPI_SUPPORT
   if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_WAPI_PSK)
     amode = Authentication_WAPI_PSK;
   else if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_WAPI_CERT)
     amode = Authentication_WAPI;
   else {
#endif
   if(sc->auth_param[IW_AUTH_WPA_ENABLED]) {
      if(sc->auth_param[IW_AUTH_WPA_VERSION] & IW_AUTH_WPA_VERSION_WPA2) {
         if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_802_1X) {
            amode = Authentication_WPA_2;
         } else if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_PSK) {
            amode = Authentication_WPA_2_PSK;
         } else {
            KDEBUG(ERROR, "unknown key management mode: %x", 
                   sc->auth_param[IW_AUTH_KEY_MGMT]);
         }
      } else if(sc->auth_param[IW_AUTH_WPA_VERSION] & IW_AUTH_WPA_VERSION_WPA) {
         if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_802_1X) {
            amode = Authentication_WPA;
         } else if(sc->auth_param[IW_AUTH_KEY_MGMT] & IW_AUTH_KEY_MGMT_PSK) {
            amode = Authentication_WPA_PSK;
         } else {
            KDEBUG(ERROR, "unknown key management mode: %x", 
                   sc->auth_param[IW_AUTH_KEY_MGMT]);
         }
      } else {
         KDEBUG(ERROR, "unknown WPA version: %x", 
                sc->auth_param[IW_AUTH_WPA_VERSION]);
      }
   }

#ifdef WAPI_SUPPORT
   }

   if(amode >= Authentication_WAPI) {
     KDEBUG(TRACE, "ENC MODE = SMS4");
     WiFiEngine_SetEncryptionMode(Encryption_SMS4);
   } else {
#endif
     if(amode > Authentication_8021X) {
       WiFiEngine_SetEncryptionMode(Encryption_CCMP);
     } else {
       WiFiEngine_SetEncryptionMode(Encryption_WEP);
     }
#ifdef WAPI_SUPPORT
   }
#endif

   KDEBUG(TRACE, "AUTH MODE = %d", amode);
   WiFiEngine_SetAuthenticationMode(amode);

   WiFiEngine_SetExcludeUnencryptedFlag(sc->auth_param[IW_AUTH_DROP_UNENCRYPTED]);
}   

DECLARE_IW(set_auth, param, auth)
{
   unsigned int index = auth->flags & IW_AUTH_INDEX;
   struct nrx_softc *sc = netdev_priv(dev);

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

#define CASE(X) case X: KDEBUG(TRACE, #X " %d", auth->value)
   switch(index) {
      CASE(IW_AUTH_WPA_VERSION);
      break;
      CASE(IW_AUTH_CIPHER_PAIRWISE);
      break;
      CASE(IW_AUTH_CIPHER_GROUP);
      break;
      CASE(IW_AUTH_KEY_MGMT);
      break;
      CASE(IW_AUTH_TKIP_COUNTERMEASURES);
      break;
      CASE(IW_AUTH_DROP_UNENCRYPTED);
      break;
      CASE(IW_AUTH_80211_AUTH_ALG);
      break;
      CASE(IW_AUTH_WPA_ENABLED);
      break;
      CASE(IW_AUTH_RX_UNENCRYPTED_EAPOL);
      break;
      CASE(IW_AUTH_ROAMING_CONTROL);
      break;
      CASE(IW_AUTH_PRIVACY_INVOKED);
      break;
      default:
         return -EINVAL;
   }

   sc->auth_param[index] = auth->value;

   set_auth_mode(sc);
   
   return 0;
}

DECLARE_IW(get_auth, param, auth)
{
   unsigned int index = auth->flags & IW_AUTH_INDEX;
   struct nrx_softc *sc = netdev_priv(dev);

   TRACEPARAM(auth);
   CHECK_UNPLUG(dev);

   if(index > sizeof(sc->auth_param) / sizeof(sc->auth_param[0]))
      return -EINVAL;

   auth->value = sc->auth_param[index];
   
   return 0;
}


/*! Set WEP keys and other parameters
 *
 * iwconfig key commands are converted into the following:

 on: get current parameters and remove DISABLED flag
 off: set DISABLED flag
 open: set OPEN flag
 restricted: set RESTRICTED flag
 temporary: set TEMP flag
 [n]: set index to n
 <key>: set key
 

 */
DECLARE_IW(set_encodeext, point, encoding)
{
   struct nrx_softc *sc = netdev_priv(dev);
#if HAVE_WPA
   struct iw_encode_ext *ext = NULL;
#endif
   int32_t key_index;
   int index_present = 0;
   int status;
#ifdef WAPI_SUPPORT
   WiFiEngine_Encryption_t enc;
#endif
   m80211_key_type_t keytype = M80211_KEY_TYPE_PAIRWISE;

   KDEBUG(TRACE, "ENTRY flags = %x", encoding->flags);
   CHECK_UNPLUG(dev);

#if HAVE_WPA
   if(iw_info->cmd == SIOCSIWENCODEEXT) {
      ext = extra;
#if (DE_CCX == CFG_INCLUDED)
          // Use the PMK interface to get the CCKM key.
	  if (ext != NULL && ext->alg == 4) { // IW_ENCODE_ALG_PMK
		  
		printk(KERN_INFO"CCKM_key(%d, %02x:%02x:%02x:%02x, %02x:%02x:%02x:%02x:%02x:%02x) \n",
                ext->key_len,
				ext->key[0],
				ext->key[1],
				ext->key[2],
				ext->key[3],
                (unsigned char)ext->addr.sa_data[0], 
                (unsigned char)ext->addr.sa_data[1], 
                (unsigned char)ext->addr.sa_data[2], 
                (unsigned char)ext->addr.sa_data[3], 
                (unsigned char)ext->addr.sa_data[4], 
                (unsigned char)ext->addr.sa_data[0]);
		WiFiEngine_Set_CCKM_Key(ext->key, ext->key_len);
	}	
#endif
	}
#endif
   
   key_index = encoding->flags & IW_ENCODE_INDEX;

   KDEBUG(TRACE, "key_index = %d", key_index);
      
   if(key_index == 0)
      key_index = get_key_index(dev);
   else
      index_present = 1;

   key_index--;
   
   

   if((encoding->flags & IW_ENCODE_NOKEY) == 0) {
      m80211_protect_type_t ptype;
      bool_t auth_conf = TRUE;
      m80211_mac_addr_t addr;
      receive_seq_cnt_t rsc, *rscp = NULL;
      bool_t set_tx = FALSE;
      size_t keylen = 0;
      void *keydata = NULL;

#if HAVE_WPA
      if(ext != NULL) {
         if(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
            keytype = M80211_KEY_TYPE_GROUP;
         else
            keytype = M80211_KEY_TYPE_PAIRWISE;
      } else
#endif
         keytype = M80211_KEY_TYPE_GROUP;

      ptype = M80211_PROTECT_TYPE_RX_TX;

      ASSERT(sizeof(addr.octet) == ETH_ALEN);
#if HAVE_WPA
      if(ext != NULL) {
         ASSERT(ext->addr.sa_family == ARPHRD_ETHER);
         memcpy(addr.octet, ext->addr.sa_data, ETH_ALEN);
      } else
#endif
         memset(addr.octet, 0xff, sizeof(addr.octet));

#if HAVE_WPA
      ASSERT(sizeof(rsc.octet) >= IW_ENCODE_SEQ_MAX_SIZE);
      if(ext != NULL && ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
         memcpy(rsc.octet, ext->rx_seq, IW_ENCODE_SEQ_MAX_SIZE);
         rscp = &rsc;
      }

      if(ext == NULL || ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
         set_tx = TRUE;

      if(ext != NULL) {
         keylen = ext->key_len;
         keydata = ext->key;
      } else {
         keylen = encoding->length;
         keydata = extra;
      }
#else
      set_tx = TRUE;

      keylen = encoding->length;
      keydata = extra;
#endif

      
      if(keylen == 0) {
         KDEBUG(TRACE, "del_key(%d, %d, %02x:%02x:%02x:%02x:%02x:%02x)",
                key_index,
                keytype,
                (unsigned char)addr.octet[0], 
                (unsigned char)addr.octet[1], 
                (unsigned char)addr.octet[2], 
                (unsigned char)addr.octet[3], 
                (unsigned char)addr.octet[4], 
                (unsigned char)addr.octet[5]);
         status = WiFiEngine_DeleteKey(key_index, 
                                       keytype,
                                       &addr);
         if(status != WIFI_ENGINE_SUCCESS)
            KDEBUG(ERROR, "WiFiEngine_DeleteKey = %d", status);
      } else {

         KDEBUG(TRACE, "add_key(%d, %zu, %p, %d, %d, %d, %02x:%02x:%02x:%02x:%02x:%02x, %p, %d)",
                key_index,
                keylen,
                keydata,
                keytype,
                ptype,
                auth_conf,
                (unsigned char)addr.octet[0], 
                (unsigned char)addr.octet[1], 
                (unsigned char)addr.octet[2], 
                (unsigned char)addr.octet[3], 
                (unsigned char)addr.octet[4], 
                (unsigned char)addr.octet[5], 
                rscp,
                set_tx);

         status = WiFiEngine_AddKey(key_index, 
                                    keylen, 
                                    keydata, 
                                    keytype,
                                    ptype,
                                    auth_conf, 
                                    &addr, 
                                    rscp, 
                                    set_tx);
         if(status != WIFI_ENGINE_SUCCESS)
            KDEBUG(ERROR, "WiFiEngine_AddKey = %d", status);
      }
      

   } else if(index_present) {
      status = WiFiEngine_SetDefaultKeyIndex(key_index);
      if(status != WIFI_ENGINE_SUCCESS)
         KDEBUG(ERROR, "WiFiEngine_SetDefaultKeyIndex = %d", status);
   }

   if(encoding->flags & IW_ENCODE_RESTRICTED) {
      sc->auth_param[IW_AUTH_80211_AUTH_ALG] = IW_AUTH_ALG_SHARED_KEY;
      sc->auth_param[IW_AUTH_PRIVACY_INVOKED] = 1;
      sc->auth_param[IW_AUTH_DROP_UNENCRYPTED] = 1; 
   } else if(encoding->flags & IW_ENCODE_OPEN) {
      sc->auth_param[IW_AUTH_80211_AUTH_ALG] = IW_AUTH_ALG_OPEN_SYSTEM;
      sc->auth_param[IW_AUTH_PRIVACY_INVOKED] = 0;
      sc->auth_param[IW_AUTH_DROP_UNENCRYPTED] = 0;
   }

   set_auth_mode(sc);
   
   if(encoding->flags & IW_ENCODE_DISABLED) {
      WiFiEngine_DeleteAllKeys();
      status = WiFiEngine_SetProtectedFrameBit(0);
      WiFiEngine_SetEncryptionMode(Encryption_Disabled);
   } else {
#ifdef WAPI_SUPPORT
     status = WiFiEngine_GetEncryptionMode(&enc);
     if(status != WIFI_ENGINE_SUCCESS) {
       DE_TRACE_STATIC(TR_AUTH, "ERROR Getting Encryption Mode!\n");
       return WIFI_ENGINE_FAILURE;
     }

     if(enc == Encryption_SMS4) {
       if(keytype == M80211_KEY_TYPE_GROUP) {
         status = WiFiEngine_SetProtectedFrameBit(1);
       }
     } else
       status = WiFiEngine_SetProtectedFrameBit(1);     
#else
      status = WiFiEngine_SetProtectedFrameBit(1);
#endif
   }

   return 0;
}

DECLARE_IW(get_encodeext, point, enc)
{
   struct iw_encode_ext *ext = extra;

   int status;
   WiFiEngine_Encryption_t emode;
   WiFiEngine_Auth_t amode;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   enc->flags = get_key_index(dev);


   status = WiFiEngine_GetEncryptionMode(&emode);
   if(status == WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "emode = %d", emode);

      if(emode == Encryption_Disabled)
         ext->alg = IW_ENCODE_ALG_NONE;
      else if(emode == Encryption_WEP)
         ext->alg = IW_ENCODE_ALG_WEP;
      else if(emode == Encryption_TKIP)
         ext->alg = IW_ENCODE_ALG_TKIP;
      else if(emode == Encryption_CCMP)
         ext->alg = IW_ENCODE_ALG_CCMP;
#ifdef WAPI_SUPPORT
      /* WAPI FIXME: how do we handle this? */
      else if(emode == Encryption_SMS4)
        ext->alg = 4; /*IW_ENCODE_ALG_SMS4;*/
#endif
      if(emode == Encryption_Disabled)
         enc->flags |= IW_ENCODE_DISABLED;
   }

   status = WiFiEngine_GetAuthenticationMode(&amode);
   if(status == WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "amode = %d", amode);
      if(amode == Authentication_Open)
         enc->flags |= IW_ENCODE_OPEN;
   }
    
   if(amode == Authentication_Shared)
      enc->flags |= IW_ENCODE_RESTRICTED;

   return -EOPNOTSUPP;
}


static int
pmkid_add(void *bssid, void *pmkid)
{
   int status;
   m80211_mac_addr_t bssid_value;
   m80211_pmkid_value pmkid_value;

   memcpy(bssid_value.octet, bssid, sizeof(bssid_value.octet));
   memcpy(pmkid_value.octet, pmkid, sizeof(pmkid_value.octet));

   status = WiFiEngine_PMKID_Add(&bssid_value, &pmkid_value);
   if(status == WIFI_ENGINE_FAILURE_RESOURCES)
      return -ENOMEM;
   if(status != WIFI_ENGINE_SUCCESS)
      return -EINVAL;
   return 0;
}

static int
pmkid_remove(void *bssid, void *pmkid)
{
   int status;
   m80211_mac_addr_t bssid_value;
   m80211_pmkid_value pmkid_value;

   memcpy(bssid_value.octet, bssid, sizeof(bssid_value.octet));
   memcpy(pmkid_value.octet, pmkid, sizeof(pmkid_value.octet));

   status = WiFiEngine_PMKID_Remove(&bssid_value, &pmkid_value);
   if(status == WIFI_ENGINE_FAILURE_RESOURCES)
      return -ENOMEM;
   if(status != WIFI_ENGINE_SUCCESS)
      return -EINVAL;


   return 0;
}

DECLARE_IW(set_pmksa, point, point)
{
   struct iw_pmksa *pmksa = extra;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(point->length != sizeof(*pmksa)) {
      KDEBUG(ERROR, "wrong size");
      return -EINVAL;
   }
   KDEBUG_BUF(TRACE, pmksa, point->length, "pmksa");

   switch(pmksa->cmd) {
      case IW_PMKSA_ADD:
         return pmkid_add(pmksa->bssid.sa_data, pmksa->pmkid);
         break;
      case IW_PMKSA_REMOVE:
         return pmkid_remove(pmksa->bssid.sa_data, pmksa->pmkid);
         break;
      case IW_PMKSA_FLUSH:
         WiFiEngine_PMKID_Clear();
         break;
   }
   return 0;
}
#endif /* HAVE_WPA */

/* 
 power.disabled         1 == no power management
 power.flags            modifiers: MIN/MAX/RELATIVE
                        type: PERIOD/TIMEOUT
                        mode: ALL_R/UNICAST_R/MULTICAST_R/FORCE_S/REPEATER
 power.value            time microseconds
*/
DECLARE_IW(set_power, param, power)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int type;
   int mode;
   WiFiEngine_PowerMode_t wifi_power_mode;
   int status;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);
    
   if(power->disabled == 1) {
      /* work-around for broken iwconfig -> it doesn't initialise the
       * struct when disabled is set */
      power->flags = 0;
      power->value = 0;
   }
   type = power->flags & IW_POWER_TYPE;
   mode = power->flags & IW_POWER_MODE;
   if(mode != 0) {
      KDEBUG(TRACE, "EXIT EINVAL");
      return -EINVAL;
   }
   
   if(type == IW_POWER_PERIOD) {
      if(power->value > 1000000) {
         /* iwconfig treats this as usecs, but we want beacon periods,
          * if the user just did iwconfig power period 14, this will
          * get it right  */
         power->value /= 1000000;
      }
      KDEBUG(TRACE, "listen interval = %d", power->value);
      WiFiEngine_SetListenInterval(power->value);
   } else if(type == IW_POWER_TIMEOUT) {
      power->value /= 1000;
      if(power->value < 5)
         power->value = 5;
      KDEBUG(TRACE, "timeout = %d ms", power->value);
      WiFiEngine_SetPSTrafficTimeout(power->value);
   } else if (type != IW_POWER_ON) {
	  /* type is IW_POWER_ON when power management
	   * is enabled or disabled with no params specified
	   */
      KDEBUG(TRACE, "EXIT EINVAL");
      return -EINVAL;
   }

   status = WiFiEngine_GetPowerMode(&wifi_power_mode);
   if (status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "EXIT EIO");
      return -EIO;
   }

   if (wifi_power_mode == (WiFiEngine_PowerMode_t)PowerSave_Disabled_Permanently) {
      if (power->disabled)
         return 0; /* power mgmt already off */
      else {
         KDEBUG(TRACE, "EXIT EOPNOTSUPP");
         return -EOPNOTSUPP; /* refuse to switch on */
      }
   }

   if(power->disabled) {
      KDEBUG(TRACE, "disable power management");
      WiFiEngine_PSControlInhibit(sc->ps_control);
   } else {
      KDEBUG(TRACE, "enable power management");
      WiFiEngine_PSControlAllow(sc->ps_control);
   }

   return 0;
}

DECLARE_IW(get_power, param, power)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int type = power->flags & IW_POWER_TYPE;
   int mode = power->flags & IW_POWER_MODE;
   WiFiEngine_PowerMode_t wifi_power_mode;
   int status;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(mode != 0) {
      KDEBUG(TRACE, "EXIT EINVAL");
      return -EINVAL;
   }
   if(type != 0) {
      KDEBUG(TRACE, "EXIT EINVAL");
      return -EINVAL;
   }
    
   status = WiFiEngine_GetPowerMode(&wifi_power_mode);
   if (status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "EXIT EIO");
      return -EIO;
   }

   if (wifi_power_mode == (WiFiEngine_PowerMode_t)PowerSave_Disabled_Permanently)
      power->disabled = 1; /* power mgmt disabled at registry */
   else
      power->disabled = !WiFiEngine_isPSCtrlAllowed(sc->ps_control);
   power->flags = 0;
   power->value = 0;
   return 0;
}

/*
 * Get wireless statistics.
 * Called by /proc/net/wireless
 * Also called by SIOCGIWSTATS
 */
static struct iw_statistics *
nrx_get_wireless_stats(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   KDEBUG(TRACE, "ENTRY");

   memset(&sc->wstats, 0, sizeof(sc->wstats));
   if(!in_atomic()) {
      int status;
      int32_t rssi;
      int32_t snr;
      size_t len = sizeof(rssi);
      status = nrx_get_mib(dev, MIB_dot11rssi, &rssi, &len);
      if(status != 0)
         goto out;

      status = nrx_get_mib(dev, MIB_dot11snrBeacon, &snr, &len);
      if(status != 0)
         goto out;
      
      setqual(&sc->wstats.qual, rssi, snr);
   }
   
 out:
   KDEBUG(TRACE, "EXIT");
   return &sc->wstats;

}

static int
nrx_iw_handler(struct net_device *dev, 
               struct iw_request_info *info,
               union iwreq_data *wrqu, 
               char *extra)
{
   int retval = -EOPNOTSUPP;
#undef SET
#undef GET
#define SET(x, y, z)                                        \
   case SIOCSIW##x:                                         \
      retval = nrx_iw_set_##y(dev, info, &wrqu->z, extra);  \
      break
#define GET(x, y, z)                                        \
   case SIOCGIW##x:                                         \
      retval = nrx_iw_get_##y(dev, info, &wrqu->z, extra);  \
      break
   switch(info->cmd) {
      SET(COMMIT, commit, name[0]);
      GET(NAME, name, name[0]);

      SET(FREQ, freq, freq);
      GET(FREQ, freq, freq);
      SET(MODE, mode, mode);
      GET(MODE, mode, mode);
      SET(SENS, sens, sens);
      GET(SENS, sens, sens);

      GET(RANGE, range, name[0]);
      SET(AP, ap, ap_addr);
      GET(AP, ap, ap_addr);
      GET(APLIST, aplist, data);        /* deprecated */
      SET(SCAN, scan, data);
      GET(SCAN, scan, data);
      SET(ESSID, essid, essid);
      GET(ESSID, essid, essid);
      SET(RATE, rate, bitrate);
      GET(RATE, rate, bitrate);
      SET(RTS, rts, rts);
      GET(RTS, rts, rts);
      SET(FRAG, frag, frag);
      GET(FRAG, frag, frag);
      SET(TXPOW, txpow, txpower);
      GET(TXPOW, txpow, txpower);
      SET(RETRY, retry, retry);
      GET(RETRY, retry, retry);
      SET(ENCODE, encodeext, encoding);
      GET(ENCODE, encode, encoding);
      SET(POWER, power, power);
      GET(POWER, power, power);
#if WIRELESS_EXT >= 18
      SET(GENIE, genie, data);          /* not implemented */
      GET(GENIE, genie, data);          /* not implemented */
      SET(MLME, mlme, data);
      SET(AUTH, auth, param);
      GET(AUTH, auth, param);
      SET(ENCODEEXT, encodeext, encoding);
      GET(ENCODEEXT, encodeext, encoding);
      SET(PMKSA, pmksa, data);
#endif
   }
   return retval;
}

static iw_handler nrx_iw_handlers[] = {
#undef IW_IOCTL
#undef NS_NET
#undef SET
#undef GET
#define IW_IOCTL(x) [(x)-(SIOCIWFIRST)]
#define NS_NET(X) (iw_handler) nrx_iw_ ## X
#define GET(x, y) IW_IOCTL(SIOCGIW ## x) = nrx_iw_handler
#define SET(x, y) IW_IOCTL(SIOCSIW ## x) = nrx_iw_handler
    SET(COMMIT, commit),
    GET(NAME, name),

    SET(FREQ, freq),
    GET(FREQ, freq),
    SET(MODE, mode),
    GET(MODE, mode),
    SET(SENS, sens),
    GET(SENS, sens),

    GET(RANGE, range),
#if 0
    SET(PRIV, priv),            /* unused */
    GET(PRIV, priv),            /* unused */
    SET(STATS, stats),          /* unused */
    GET(STATS, stats),          /* handled internally in wireless.c */
#endif
#if 0
    IW_IOCTL(SIOCSIWSPY)        = (iw_handler) iw_handler_set_spy,
    IW_IOCTL(SIOCGIWSPY)        = (iw_handler) iw_handler_get_spy,
    IW_IOCTL(SIOCSIWTHRSPY)     = (iw_handler) iw_handler_set_thrspy,
    IW_IOCTL(SIOCGIWTHRSPY)     = (iw_handler) iw_handler_get_thrspy,
#endif
    SET(AP, ap),
    GET(AP, ap),
    GET(APLIST, aplist),        /* deprecated */
    SET(SCAN, scan),
    GET(SCAN, scan),
    SET(ESSID, essid),
    GET(ESSID, essid),
    SET(RATE, rate),
    GET(RATE, rate),
    SET(RTS, rts),
    GET(RTS, rts),
    SET(FRAG, frag),
    GET(FRAG, frag),
    SET(TXPOW, txpow),
    GET(TXPOW, txpow),
    SET(RETRY, retry),
    GET(RETRY, retry),
    SET(ENCODE, encodeext),
    GET(ENCODE, encode),
    SET(POWER, power),
    GET(POWER, power),
#if WIRELESS_EXT >= 18
    SET(GENIE, genie),          /* not implemented */
    GET(GENIE, genie),          /* not implemented */
    SET(MLME, mlme),
    SET(AUTH, auth),
    GET(AUTH, auth),
    SET(ENCODEEXT, encodeext),
    GET(ENCODEEXT, encodeext),
    SET(PMKSA, pmksa),
#endif
#undef IOCTL
};

static struct nrx_iw_handler_info {
   int cmd;
   iw_handler handler;
   size_t min_size;
   size_t max_size;
} nrx_iw_handler_info[] = {
#if HAVE_WPA && WIRELESS_EXT < 18
   { SIOCSIWMLME, NS_NET(set_mlme), sizeof(struct iw_mlme), sizeof(struct iw_mlme) },
   { SIOCSIWAUTH, NS_NET(set_auth), 0, 0 },
   { SIOCGIWAUTH, NS_NET(get_auth), 0, 0 },
   { SIOCSIWGENIE, NS_NET(set_genie), 0, IW_GENERIC_IE_MAX },
   { SIOCGIWGENIE, NS_NET(get_genie), 0, IW_GENERIC_IE_MAX },
   { SIOCSIWPMKSA, NS_NET(set_pmksa), 
     sizeof(struct iw_pmksa), 
     sizeof(struct iw_pmksa)
   },
   { SIOCSIWENCODEEXT, NS_NET(set_encodeext),
     sizeof(struct iw_encode_ext),
     sizeof(struct iw_encode_ext) + IW_ENCODING_TOKEN_MAX
   },
   { SIOCGIWENCODEEXT, NS_NET(get_encodeext),
     sizeof(struct iw_encode_ext),
     sizeof(struct iw_encode_ext) + IW_ENCODING_TOKEN_MAX
   }
#endif
};

static struct nrx_iw_handler_info*
find_handler(int cmd)
{
   struct nrx_iw_handler_info *iwh;

   for(iwh = nrx_iw_handler_info; 
       iwh < &nrx_iw_handler_info[ARRAY_SIZE(nrx_iw_handler_info)]; 
       iwh++) {
      if(iwh->cmd == cmd)
         return iwh;
   }
   return NULL;
}

int
ns_net_ioctl_iw(struct net_device *dev, struct ifreq *ifr, int cmd)
{
   struct iwreq *iwr = (struct iwreq *)ifr;
   int ret;
   struct iw_request_info info;
   void *extra = NULL;
   int is_point;
   struct nrx_iw_handler_info *iwh;
   int orig_len = 0;

   iwh = find_handler(cmd);
   if(iwh == NULL)
      return -EOPNOTSUPP;

   is_point = (iwh->min_size != 0 || iwh->max_size != 0);

   info.cmd = cmd;
   info.flags = 0;

   /* assumes token_size == 1 */

   if(is_point) {
      if(IW_IS_SET(cmd)) {
         if(iwr->u.data.length != 0 && iwr->u.data.pointer == NULL)
            return -EFAULT;
   
         if(iwr->u.data.length < iwh->min_size)
            return -EINVAL;
   
         if(iwr->u.data.length > iwh->max_size)
            return -E2BIG;

         extra = kmalloc(iwr->u.data.length, GFP_KERNEL);
         if(extra == NULL)
            return -ENOMEM;
   
         ret = copy_from_user(extra, 
                              iwr->u.data.pointer,
                              iwr->u.data.length);
         if (ret) {
            kfree(extra);
            return -EFAULT;
         }
      } else {
         if(iwr->u.data.length < iwh->min_size)
            return -EINVAL;
         if(iwr->u.data.pointer == NULL)
            return -EINVAL;
         
         extra = kmalloc(iwh->max_size, GFP_KERNEL);
         if(extra == NULL)
            return -ENOMEM;
         orig_len = iwr->u.data.length;
      }
   }
   ret = (*iwh->handler)(dev, &info, &iwr->u, extra);

   if(is_point) {
      if(IW_IS_GET(cmd) && ret == 0) {
         if(orig_len < iwr->u.data.length) {
            kfree(extra);
            return -E2BIG;
         }
         ret = copy_to_user(iwr->u.data.pointer, 
                            extra,
                            iwr->u.data.length);
         if (ret) {
            kfree(extra);
            return -EFAULT;
         }

      }
   }

   if(extra != NULL)
      kfree(extra);

   return ret;
}

static struct iw_handler_def nrx_iw_handler_def = {
   .standard = nrx_iw_handlers,
   .num_standard = ARRAY_SIZE(nrx_iw_handlers),
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33) || defined(CONFIG_WEXT_PRIV)
   .private = NULL,
   .num_private = 0,
   .private_args = NULL,
   .num_private_args = 0,
#endif
#if WIRELESS_EXT >= 17
   .get_wireless_stats = nrx_get_wireless_stats
#endif
};
#endif /* < 2.6.22 || WEXT */


int
ns_setup_iw(struct net_device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
   dev->get_wireless_stats = nrx_get_wireless_stats;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22) || defined(CONFIG_WIRELESS_EXT)
   dev->wireless_handlers = &nrx_iw_handler_def;
#else
#warning kernel not compiled with wireless extensions
#endif
   sema_init(&scan_sem, 1);
   return 0;
}
