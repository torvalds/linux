/* Upper network driver interface for Nanoradio Linux WiFi driver */
/* $Id: nanonet.c 19122 2011-05-12 11:54:06Z johe $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/wireless.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <net/dsfield.h>
#include <net/pkt_sched.h>
#include <asm/byteorder.h>

#include "nanoparam.h"
#include "nanonet.h"
#include "nanoutil.h"
#include "nanoproc.h"

#include "wifi_engine.h"
#include "wifi_engine_internal.h"

char *nrx_ifname = "wlan0";
char *nrx_config = "/nanoradio";
char *nrx_macpath = "/data/misc";
int nrx_unplug = 0;

#ifdef MODULE_PARM
#define NRX_MODULE_PARM(N, T1, T2, M) MODULE_PARM(N, T2)
#else
#define NRX_MODULE_PARM(N, T1, T2, M) module_param(N, T1, M)
#endif

MODULE_LICENSE("GPL");

NRX_MODULE_PARM(nrx_ifname, charp, "s", 0444);
MODULE_PARM_DESC(nrx_ifname, "NRX interface name");

NRX_MODULE_PARM(nrx_config, charp, "s", 0644);
MODULE_PARM_DESC(nrx_config, "NRX config directory");

NRX_MODULE_PARM(nrx_macpath, charp, "s", 0644);
MODULE_PARM_DESC(nrx_macpath, "NRX mac file directory");

NRX_MODULE_PARM(nrx_unplug, int, "i", 0644);
MODULE_PARM_DESC(nrx_unplug, "NRX unplugged mode");

#ifdef WIFI_DEBUG_ON
NRX_MODULE_PARM(nrx_debug, int, "i", 0644);
#endif

#include "linux_release_tag.h"

MODULE_INFO(nrversion, LINUX_RELEASE_STRING);

#include "log.h"
#ifdef C_LOGGING
#define LOG_SIZE 16777216
unsigned char log_buf[LOG_SIZE];
#endif
struct log_t logger;

int nano_scan_wait;

static int nano_download_firmware = 1;
static int nrx_debug_wait = 0;

static int nrx_open(struct net_device *dev) 
{
   struct nrx_softc *sc = netdev_priv(dev);
   KDEBUG(TRACE, "ENTRY: %s", dev->name);

   /* If we are in shutdown state, then nrx_exit_shutdown()
    * shall prepare the device for normal operation before
    * calling nrx_open() recursively, via dev_open().
    */
   if (nrx_test_flag(sc, NRX_FLAG_SHUTDOWN))
      return nrx_exit_shutdown(dev);

   nrx_clear_flag(sc, NRX_FLAG_IF_DOWN);
   nrx_tx_queue_wake(dev);
   return 0;
}

static int nrx_stop(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   KDEBUG(TRACE, "ENTRY: %s", dev->name);

   nrx_tx_queue_stop(dev);
   nrx_set_flag(sc, NRX_FLAG_IF_DOWN);
#if DE_SHUTDOWN_ON_IFDOWN == CFG_ON
   return nrx_enter_shutdown(dev);
#else /* DE_SHUTDOWN_ON_IFDOWN == CFG_OFF */
   return 0;
#endif /* DE_SHUTDOWN_ON_IFDOWN */
}

int nrx_enter_shutdown(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int status;

   KDEBUG(TRACE, "ENTRY: %s", dev->name);
  
   /* If already in shutdown, do nothing.
    * If we're going away, there is also no point in trying anything new.
    */
   if (nrx_test_flag(sc, NRX_FLAG_SHUTDOWN) ||
       nrx_test_flag(sc, NRX_FLAG_DESTROY))
         return 0; /* already in shutdown */

   /* In case the network interface is up, bring it down now. */
   if (!nrx_test_flag(sc, NRX_FLAG_IF_DOWN)) {
      status = dev_close(dev);
      if (status)
         return status;

      /* This function may now have been called recursively
       * via dev_close() and nrx_stop(). In this case,
       * the job is already done, so exit.
       */
      if (nrx_test_flag(sc, NRX_FLAG_SHUTDOWN))
      return 0;
   }

   /* Stop default scan job and connection manager scan job */
   WiFiEngine_SetScanJobState(0, 0, NULL); 
   WiFiEngine_SetScanJobState(1, 0, NULL);     

   status = WiFiEngine_SoftShutdown();
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(ERROR, "WiFiEngine_SoftShutdown failed, status %d", status);
      return -EBUSY;
   }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,48)
   if (wait_event_interruptible_timeout(sc->mib_wait_queue,
				       nrx_test_flag(sc, NRX_FLAG_SHUTDOWN),
				       2*HZ) > 0)
      status = 0;
   else
      status = -ETIME;
#else
   status = wait_event_interruptible(sc->mib_wait_queue, 
                                     nrx_test_flag(sc, NRX_FLAG_SHUTDOWN));
#endif
   if (status) {
      KDEBUG(ERROR,"Waiting for NRX_FLAG_SHUTDOWN aborted, status %d", status);
      return status;
   }

   /* If the transport supports NANONET_HARD_SHUTDOWN,
    * ask the transport to drive the target into hard shutdown.
    */
   if (nrx_trsp_ctrl(dev, NANONET_HARD_SHUTDOWN,
                          NANONET_HARD_SHUTDOWN_TEST) == 0) {

      status = nrx_trsp_ctrl(dev, NANONET_HARD_SHUTDOWN,
                                  NANONET_HARD_SHUTDOWN_ENTER);
      KDEBUG(TRACE, "Tried hard shutdown, status %d", status);
   }

   return status;
}

int nrx_exit_shutdown(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int status;

   KDEBUG(TRACE, "ENTRY: %s", dev->name);

   if (!nrx_test_flag(sc, NRX_FLAG_SHUTDOWN))
      return 0; /* already out of shutdown */

   /* If the transport supports NANONET_HARD_SHUTDOWN,
    * ask the transport to pull the target out of hard shutdown.
    */
   if (nrx_trsp_ctrl(dev, NANONET_HARD_SHUTDOWN,
                          NANONET_HARD_SHUTDOWN_TEST) == 0) {

      status = nrx_trsp_ctrl(dev, NANONET_HARD_SHUTDOWN,
                                  NANONET_HARD_SHUTDOWN_EXIT);
      KDEBUG(TRACE, "Tried to exit hard shutdown, status %d\n", status);
      if (status)
         return status;
   }

   nrx_set_state(sc, NRX_STATE_WAIT_FW);
   nrx_schedule_event(sc, 0);
   if(wait_event_interruptible(sc->mib_wait_queue,
                               !nrx_test_flag(sc, NRX_FLAG_SHUTDOWN)) != 0) {
      KDEBUG(ERROR, "Wait for NRX_FLAG_SHUTDOWN failed");
      return -ERESTARTSYS;
   }

   return dev_open(dev);
}

int
nrx_trsp_ctrl(void *data, uint32_t command, uint32_t mode)
{
   struct net_device *dev = data;
   struct nrx_softc *sc = netdev_priv(dev);

   if(!nrx_test_flag(sc, NRX_FLAG_ATTACHED))
      return -ENODEV;
   if(sc->transport == NULL ||
      sc->transport->control == NULL) {
      return -EOPNOTSUPP;
   }
   return (*sc->transport->control)(command, mode, sc->transport_data);
}

static int nrx_mib_callback(we_cb_container_t *cbc)
{
   struct net_device *dev = cbc->ctx;
   struct nrx_softc *sc = netdev_priv(dev);

   KDEBUG(TRACE, "ENTRY");

   switch(cbc->status) {
      case MIB_RESULT_OK:
         break;
#define CASE(L) case L: KDEBUG(TRACE, #L); break
         CASE(WIFI_ENGINE_FAILURE_ABORT);
         CASE(WIFI_ENGINE_FAILURE_RESOURCES);
         CASE(MIB_RESULT_INVALID_PATH);
         CASE(MIB_RESULT_NO_SUCH_OBJECT);
         CASE(MIB_RESULT_SIZE_ERROR);
         CASE(MIB_RESULT_OBJECT_NOT_A_LEAF);
         CASE(MIB_RESULT_SET_FAILED);
         CASE(MIB_RESULT_GET_FAILED);
         CASE(MIB_RESULT_SET_NOT_ALLOWED);
         CASE(MIB_RESULT_INTERNAL_ERROR);
         CASE(MIB_RESULT_GET_NOT_ALLOWED);
         CASE(MIB_RESULT_MEM_REGION_INVALIDATED);
#undef CASE
      default:
         DE_BUG_ON(1, "unknown status %d", cbc->status);
         break;
   }
   wake_up(&sc->mib_wait_queue);
   return 1;
}


int nrx_get_mib(struct net_device *dev, const char *id, void *data, size_t *len)
{
   int status;
   we_cb_container_t *cbc;
   uint32_t tid;
   struct nrx_softc *sc = netdev_priv(dev);

   KDEBUG(TRACE, "ENTRY");

   if(nrx_test_flag(sc, NRX_FLAG_SHUTDOWN)) {
      KDEBUG(TRACE, "called when shutdown");
      return -EWOULDBLOCK;
   }

   cbc = WiFiEngine_BuildCBC(nrx_mib_callback, dev, 0, FALSE);
   preempt_disable();
   status = WiFiEngine_GetMIBAsynch(id, cbc);
   if(status != WIFI_ENGINE_SUCCESS) {
      WiFiEngine_FreeCBC(cbc);
      preempt_enable();
      KDEBUG(TRACE, "EXIT EIO");
      return -EIO;
   }
   
   tid = cbc->trans_id;
   preempt_enable();
   while(1) {
      int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,48)
      ret = wait_event_interruptible_timeout(sc->mib_wait_queue, 
                                     (status = WiFiEngine_GetMIBResponse(tid, data, len)) != WIFI_ENGINE_FAILURE_NOT_ACCEPTED, HZ);
      if(ret == 0)
	return -EIO;      
#else
      ret = wait_event_interruptible(sc->mib_wait_queue, 
                                     (status = WiFiEngine_GetMIBResponse(tid, data, len)) != WIFI_ENGINE_FAILURE_NOT_ACCEPTED);
#endif

      if(ret == -ERESTARTSYS)
         return ret;
      
      if(status == WIFI_ENGINE_FAILURE_NOT_ACCEPTED)
         status = WiFiEngine_GetMIBResponse(tid, data, len);
      switch(status) {
         case WIFI_ENGINE_FAILURE_NOT_ACCEPTED:
            continue;
         case WIFI_ENGINE_SUCCESS:
            KDEBUG(TRACE, "EXIT");
            return 0;
         case WIFI_ENGINE_FAILURE:
            KDEBUG(TRACE, "EXIT EIO");
            return -EIO;
         case WIFI_ENGINE_FAILURE_INVALID_LENGTH:
            KDEBUG(TRACE, "EXIT ERANGE");
            return -ERANGE;
         default:
            KDEBUG(TRACE, "EXIT EINVAL");
            return -EINVAL;
      }
   }
}

static int
nrx_set_mac_address(struct net_device *dev, void *data)
{
   struct sockaddr *sa = data;
   int status;

   KDEBUG(TRACE, "ENTRY");
   if(netif_running(dev)) {
      KDEBUG(ERROR, "EXIT EBUSY");
      return -EBUSY;
   }

   status = WiFiEngine_SetMACAddress(sa->sa_data, dev->addr_len);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "EXIT EIO");
      return -EIO;
   }
   memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

   KDEBUG(TRACE, "EXIT");
   return 0;
}

/*
 * Sleep is not allowed here so don't use DriverEnvironment_Malloc or
 * semaphores here.
 */
static void
nrx_set_multicast_list(struct net_device *dev)
{
   m80211_mac_addr_t addr;

   KDEBUG(TRACE, "ENTRY");

   WiFiEngine_MulticastSetFlags(WE_MCAST_FLAG_HOLD);
   if((dev->flags & IFF_ALLMULTI) != 0) {
      WiFiEngine_MulticastSetFlags(WE_MCAST_FLAG_ALLMULTI);
   } else {
      WiFiEngine_MulticastClearFlags(WE_MCAST_FLAG_ALLMULTI);
   }
   WiFiEngine_MulticastClear();
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
   {
      struct dev_mc_list *dmi;

      for(dmi = dev->mc_list; dmi != NULL; dmi = dmi->next) {
         if(dmi->dmi_addrlen != M80211_ADDRESS_SIZE)
            continue;
         memcpy(addr.octet, dmi->dmi_addr, M80211_ADDRESS_SIZE);
         WiFiEngine_MulticastAdd(addr);
      }
   }
#else
   {
      struct netdev_hw_addr *ha;

      netdev_for_each_mc_addr(ha, dev) {
         memcpy(addr.octet, ha->addr, M80211_ADDRESS_SIZE);
         WiFiEngine_MulticastAdd(addr);
      }
   }
#endif
   WiFiEngine_MulticastClearFlags(WE_MCAST_FLAG_HOLD);
   KDEBUG(TRACE, "EXIT");
}

int nrx_send_buf(struct sk_buff *skb)
{
   struct net_device *dev = skb->dev;
   struct nrx_softc *sc = netdev_priv(dev);
 
   KDEBUG(PRINTBUF, "ENTRY: %s", dev->name);
   KDEBUG_DO(PRINTBUF, print_pkt_hdr(skb->data, skb->len));
   KDEBUG_BUF(PRINTBUF, skb->data, skb->len, "TX");

   if(!nrx_test_flag(sc, NRX_FLAG_ATTACHED)) {
      KDEBUG(TRACE, "device not present");
      return 0;
   }
#if DE_ENABLE_PCAPLOG >= CFG_ON
   nrx_pcap_append(0, skb->data, skb->len);
#endif
   if ((*sc->transport->send)(skb, sc->transport_data) == 0) {
      return 0;
   } 

   KDEBUG(ERROR, "Failed send");
   return 0;
}

int nrx_wmm_association;

/* priority for even numbers in lower nybble, and for odd number in
 * high nybble 
 */

/* Default map provides a linear mapping between DSCP Class Selector
 * (which coincides with TOS precedence with zero routing bits) and
 * 802.1d priority. This is by all accounts wrong, but is apparently
 * what people use, and may be more convenient.
 */
static uint8_t dscp_mapping[256 / 2]  = {
#define D(A, B) (((A) << 4) | (B))
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(1, 1), D(1, 1), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(2, 2), D(2, 2), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(3, 3), D(3, 3), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(4, 4), D(4, 4), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(5, 5), D(5, 5), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(6, 6), D(6, 6), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(7, 7), D(7, 7), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0),
   D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0), D(0, 0)
#undef D
};

int nrx_set_dscp_mapping(uint8_t *buf, size_t len)
{
   if(len != sizeof(dscp_mapping))
      return -EINVAL;
   memcpy(dscp_mapping, buf, len);
   return 0;
}

int nrx_get_dscp_mapping(uint8_t *buf, size_t len)
{
   if(len != sizeof(dscp_mapping))
      return -EINVAL;
   memcpy(buf, dscp_mapping, len);
   return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define ip_hdr(_skb)    ((_skb)->nh.iph)
#define ipv6_hdr(_skb)  ((_skb)->nh.ipv6h)
#endif

static inline uint16_t
i8021d_priority_raw(struct sk_buff *skb)
{
   uint8_t dscp = 0;
   int have_dscp = 0;

   if(skb->priority >= 256 && skb->priority <= 263)
      return skb->priority - 256;

   /* TODO
    * do we need to distinguish between IP TOS and DS?
    * should we support:
    *   AF (RFC2597)
    *   EF (RFC3246)
    */

   switch(skb->protocol) {
      case __constant_htons(ETH_P_IP):
         dscp = ipv4_get_dsfield(ip_hdr(skb));
         have_dscp = 1;
         break;
      case __constant_htons(ETH_P_IPV6):
         dscp = ipv6_get_dsfield(ipv6_hdr(skb));
         have_dscp = 1;
         break;
      default:
         break;
   }
   if(have_dscp) {
      uint8_t val = dscp_mapping[dscp / 2];
      KDEBUG(TRACE, "DSCP %u %u", dscp, val);
      if(dscp & 1)
         return (val >> 4) & 7;
      else
         return val & 7;
   }
   return 0;
}

static inline uint16_t
i8021d_priority(struct sk_buff *skb)
{
   if(!nrx_wmm_association)
      return 0;
   
   return i8021d_priority_raw(skb);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define USE_MULTIQUEUE 1
#else
#undef USE_MULTIQUEUE
#endif

static void nrx_tx_queue_manage(struct net_device *dev, int extra_packet)
{
   unsigned int queued_packets;
   struct nrx_softc *sc = netdev_priv(dev);
#ifdef USE_MULTIQUEUE
   struct netdev_queue *txq;
   unsigned int qlen[4]; /* fw queue length */
   unsigned int qs[4]; /* tx queue state */
   unsigned int max_frames; /* max fw frames/ac */
   unsigned int active_queues; /* number of fw queues with frames */
   unsigned int q;
   
   WiFiEngine_GetDataRequestByAccess(&qlen[1], &qlen[0], 
                                     &qlen[2], &qlen[3]);

   active_queues = (qlen[0] > 0) + (qlen[1] > 0)
      + (qlen[2] > 0) + (qlen[3] > 0);

   max_frames = wifiEngineState.txPktWindowMax;
   if(active_queues > 0)
      /* try to spread the queue space on active queues; rounding up
       * is the easiest approach, but if there are many active queues
       * and little space, there is bound to be starvation */
      max_frames = (max_frames + active_queues - 1) / active_queues;

   queued_packets = 0;
   for(q = 0; q < dev->num_tx_queues; q++) {
      txq = netdev_get_tx_queue(dev, q);
      queued_packets += txq->qdisc->q.qlen;
      if(sc->tx_queue_stopped || qlen[q] >= max_frames) {
         netif_tx_stop_queue(txq);
         qs[q] = 0;
      } else {
         netif_tx_wake_queue(txq);
         qs[q] = 1;
      }
   }
   KDEBUG(TRACE, "QUEUE active = %u, be=%u/%u, bk=%u/%u, vi=%u/%u, vo=%u/%u",
          active_queues,
          qlen[0], qs[0], 
          qlen[1], qs[1], 
          qlen[2], qs[2], 
          qlen[3], qs[3]);
#else /* !USE_MULTIQUEUE */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
   struct netdev_queue *txq;
   txq = netdev_get_tx_queue(dev, 0);
   queued_packets = txq->qdisc->q.qlen;
#else
   queued_packets = dev->qdisc->q.qlen;
#endif

   if(sc->tx_queue_stopped)
      netif_stop_queue(dev);
   else
      netif_wake_queue(dev);
#endif /* USE_MULTIQUEUE */
   if(extra_packet)
      queued_packets++;
   WiFiEngine_IndicateTXQueueLength(queued_packets);
}

void nrx_tx_queue_stop(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   sc->tx_queue_stopped = TRUE;
   nrx_tx_queue_manage(dev, FALSE);
}

static void nrx_tx_queue_stop_extra_packet(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   sc->tx_queue_stopped = TRUE;
   nrx_tx_queue_manage(dev, TRUE);
}

void nrx_tx_queue_wake(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   sc->tx_queue_stopped = FALSE;
   nrx_tx_queue_manage(dev, FALSE);
}

static void
nrx_wake_queue_after_delay(struct net_device *dev, unsigned long delay)
{
   struct nrx_softc *sc = netdev_priv(dev);
   nrx_set_flag(sc, NRX_FLAG_WAKE_QUEUE);
   nrx_schedule_event(sc, delay);
}

static inline int
fixup_skb(struct sk_buff *skb)
{
   unsigned int padding_bytes;
   
/* #define NRX_DATA_ALIGNMENT 4 */
   /* XXX this won't work as various parts of WiFiEngine assume
    * that packets have HIC_PAYLOAD_OFFSET bytes padding, and
    * this will introduce an alternate reality where this is not
    * the case */
#ifdef NRX_DATA_ALIGNMENT
   int fix_length = 0;
   /* try to fixup unaligned packets 
      XXX will not work if headroom < x bytes */
   if(((uintptr_t)skb->data % NRX_DATA_ALIGNMENT) != 0) {
      uint8_t hdr[6];
      size_t bytes = (uintptr_t)skb->data % NRX_DATA_ALIGNMENT;
      
      memcpy(hdr, skb->data, sizeof(hdr));
      
      if(HIC_MESSAGE_HDR_SIZE(hdr) >= NRX_DATA_ALIGNMENT - bytes + 1) {
         /* decrease internal padding */
         skb_pull(skb, NRX_DATA_ALIGNMENT - bytes);
         HIC_MESSAGE_HDR_SIZE(hdr) -= NRX_DATA_ALIGNMENT - bytes;
      } else {
         /* increase internal padding */
         skb_push(skb, bytes);
         memset(skb->data, 0, bytes + sizeof(hdr)); /* zero padding */
         HIC_MESSAGE_HDR_SIZE(hdr) += bytes;
      }
      /* correct packet length */
      memcpy(skb->data, hdr, sizeof(hdr));
      fix_length++;
   }
#endif

   padding_bytes = HIC_MESSAGE_PADDING_GET(skb->data);
   if(padding_bytes > 0) {
      /* We could use skb_pad here if it wasn't broken. If it fails
       * for some reason the original skb is free:d, which makes it
       * impossible to reclaim the data counters with
       * WiFiEngine_DataFrameDropped. This could be worked around with
       * skb_get, except that pskb_expand_head explicitly test for
       * this scenario. The only workaround seems to be to inline
       * skb_pad here. 
       */
      if(skb_tailroom(skb) < padding_bytes) {
         int err;
         err = pskb_expand_head(skb, 0, padding_bytes, GFP_ATOMIC);
         if(err != 0) {
            return err;
         }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
         err = skb_linearize(skb);
#else
         err = skb_linearize(skb, GFP_ATOMIC);
#endif
         if(err != 0) {
            return err;
         }
      }
      DE_ASSERT(skb_tailroom(skb) >= padding_bytes);
      memset(skb_put(skb, padding_bytes), 0, padding_bytes);
   }
   return 0;
}

/* these macros appeared in 2.6.9, but until 2.6.23 anything non-zero
   was considered BUSY (it still is, but with an annoying error
   message)*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
#define NETDEV_TX_OK   0
#define NETDEV_TX_BUSY 1
#endif

#ifdef USE_MULTIQUEUE
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static spinlock_t tx_lock = SPIN_LOCK_UNLOCKED;
#else
DEFINE_SPINLOCK(tx_lock); 
#endif
#endif

/* This function (hard_start_xmit callback) is externally
 * concurrency-protected.  Two calls to it will never execute
 * concurrently.
 */
static int nrx_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct sk_buff *skb_org = NULL; /* Pointer to original skb if cloned (realloc'd) */
   size_t len;
   int status;
   unsigned int transid;

   KDEBUG(TRACE, "%s: skb=%p len=%d", dev->name, skb, skb->len);

   if(nrx_test_flag(sc, NRX_FLAG_UNPLUGGED)) {
      KDEBUG(TRACE, "dropping packet when shutdown");
      dev_kfree_skb(skb);
      return NETDEV_TX_OK;
   }

   if(!nrx_test_flag(sc, NRX_FLAG_ATTACHED)) {
      KDEBUG(TRACE, "device not present");
      return 0;
   }

   if (nrx_test_flag(sc, NRX_FLAG_IF_DOWN)) {
       KDEBUG(ERROR, "called while netif is down");
       skb_org = skb;
       goto drop_pkt;
   }
#ifdef USE_MULTIQUEUE
   spin_lock(&tx_lock);
#endif

   if (skb->len < ETH_HLEN || 
       skb->len > dev->mtu + dev->hard_header_len) {
      KDEBUG(ERROR, "%s: invalid packet size %d, dropping", 
	     dev->name, 
	     skb->len);
      goto drop_pkt;
   }

   if(skb_headroom(skb) < sc->tx_hlen) {
      /* There's no space left, so we need to reallocate. This typically
       * happens with SOCK_PACKET. */
      KDEBUG(TRACE, "REALLOC");
      printk("realloc: Need %zu extra bytes of headroom\n", 
             sc->tx_hlen - skb_headroom(skb));
      skb_org = skb;
      skb = skb_realloc_headroom(skb, sc->tx_hlen);
      if(skb == NULL) {
         KDEBUG(TRACE, "DROP PACKET");
	 skb = skb_org;
	 skb_org = NULL;
         goto drop_pkt;
      }
   }
   skb_push(skb, sc->tx_hlen);

   len = sc->tx_hlen;
   status = WiFiEngine_ProcessSendPacket(skb->data + sc->tx_hlen,
                                         skb->len - sc->tx_hlen,
                                         skb->len - sc->tx_hlen,
                                         skb->data, &len,
                                         i8021d_priority(skb),
                                         &transid);

   if (status != WIFI_ENGINE_SUCCESS) {
      switch (status)
      {
         case WIFI_ENGINE_FAILURE_INVALID_LENGTH:
            DE_TRACE_INT(TR_DATA, "Invalid packet size %d. Dropping packet.\n", skb->len);
            goto drop_pkt;
         case WIFI_ENGINE_FAILURE_COREDUMP:
         case WIFI_ENGINE_FAILURE_LOCK:
         case WIFI_ENGINE_FAILURE_PS:
         case WIFI_ENGINE_FAILURE_DATA_PATH:
         case WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL:
         case WIFI_ENGINE_FAILURE_RESOURCES:
            WiFiEngine_DebugStatsProcessSendPacket(status);
            /* Target is sleeping, or packet buffer on target is
             * full. The second case should normally be handled by an
             * indication earlier on. */
            /* Stop queue and re-queue packet. The queue will be
             * started again when target re-awakes, or we get a
             * data-cfm from target. */

	    skb_pull(skb, sc->tx_hlen); /* remove our header */
            if(status == WIFI_ENGINE_FAILURE_PS) {
               /* packet will be requeued by kernel, and the tx will
                * be resumed a bit later, at which point we will
                * hopefully be awake */
               KDEBUG(TRACE, "Target sleeping");
               nrx_tx_queue_stop_extra_packet(dev);
            } else {
               KDEBUG(TRACE, "WiFiEngine is busy (%d), stopping send queue",
                      status);
               nrx_tx_queue_stop_extra_packet(dev);

               /* this is a workaround for a logic fault in the hic
                * interface management code; if there is a request
                * issued (a mib get for instance), target is awaken,
                * and the hic interface opened, but the data path is
                * not; if there is a data request issued at this
                * point, it is rejected, and nothing will restart the
                * data queue
                */
               nrx_wake_queue_after_delay(dev, HZ / 50 + 1); /* ~20ms */
            }
            if (skb_org) {
	       /*
		* Original skb is cloned, free the clone sine net/core will
		* retry the original skb when hard_start_xmit is retried.
		*/
	       dev_kfree_skb(skb);
	    }
#ifdef USE_MULTIQUEUE
            spin_unlock(&tx_lock);
#endif
            return NETDEV_TX_BUSY;
            
         case WIFI_ENGINE_FAILURE_NOT_ACCEPTED:
	   KDEBUG(TRACE, "Tried to transmit packet that is not accepted");
	   goto drop_pkt;
         case WIFI_ENGINE_FAILURE_NOT_CONNECTED:
            KDEBUG(TRACE, "Tried to transmit packet to unconnected interface.");
            if(!WiFiEngine_is_roaming_enabled())
               netif_carrier_off(dev);
            goto drop_pkt;

         default:
            KDEBUG(TRACE, "Unknown failure in WiFiEngine_ProcessSendPacket()");
            goto drop_pkt;
      }
   }

   if(fixup_skb(skb) != 0) {
      KDEBUG(ERROR, "failed to pad skb");
      WiFiEngine_DataFrameDropped(skb->data);
      goto drop_pkt;
   }

   KDEBUG_DO(PRINTBUF, print_pkt_hdr(skb->data, skb->len));
   KDEBUG_BUF(PRINTBUF, skb->data, skb->len, "TX");
   dev->trans_start = jiffies; /* save the timestamp */

#if DE_ENABLE_PCAPLOG >= CFG_ON
   nrx_pcap_append(0, skb->data, skb->len);
#endif
   if ((*sc->transport->send)(skb, sc->transport_data) == 0) {
      sc->stats.tx_packets++;
      sc->stats.tx_bytes += skb->len;
      goto sent_pkt;
   } else {
      KDEBUG(ERROR, "Failed send");
      WiFiEngine_DataFrameDropped(skb->data);
      goto drop_pkt;
   }
  drop_pkt:
   sc->stats.tx_packets++;
   sc->stats.tx_dropped++;
   dev_kfree_skb(skb);
#if (DE_CCX == CFG_INCLUDED)
   wei_ccx_free_trans(transid);
#endif
  sent_pkt:
   if (skb_org) {
      dev_kfree_skb(skb_org);
   }
   nrx_tx_queue_manage(dev, FALSE);
#ifdef USE_MULTIQUEUE
   spin_unlock(&tx_lock);
#endif
   return NETDEV_TX_OK;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,41)
/* In linux 2.4, eth_type_trans only works properly for ethernet
 * devices without extra framing, so in our case it will pull back too
 * many bytes (hard_header_len instead of ETH_HLEN). So we need to
 * provide our own working copy. 
 * Implementation from linux 2.4.32.
 */
static unsigned short
nano_eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
   struct ethhdr *eth;
   unsigned char *rawp;
        
   skb->mac.raw = skb->data;
   skb_pull(skb, ETH_HLEN);
   eth = skb->mac.ethernet;
        
   if(*eth->h_dest & 1)
   {
      if(memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
         skb->pkt_type = PACKET_BROADCAST;
      else
         skb->pkt_type = PACKET_MULTICAST;
   }
        
   /*
    *      This ALLMULTI check should be redundant by 1.4
    *      so don't forget to remove it.
    *
    *      Seems, you forgot to remove it. All silly devices
    *      seems to set IFF_PROMISC.
    */
         
   else if(1 /*dev->flags&IFF_PROMISC*/)
   {
      if(memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN))
         skb->pkt_type = PACKET_OTHERHOST;
   }
        
   if (ntohs(eth->h_proto) >= 1536)
      return eth->h_proto;
                
   rawp = skb->data;
        
   /*
    *      This is a magic hack to spot IPX packets. Older Novell breaks
    *      the protocol design and runs IPX over 802.3 without an 802.2 LLC
    *      layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
    *      won't work for fault tolerant netware but does for the rest.
    */
   if (*(unsigned short *)rawp == 0xFFFF)
      return htons(ETH_P_802_3);
                
   /*
    *      Real 802.2 LLC
    */
   return htons(ETH_P_802_2);
}
#undef eth_type_trans
#define eth_type_trans nano_eth_type_trans
#endif

static void process_rx_packet(struct net_device *dev, struct sk_buff *skb);

void ns_net_rx(struct sk_buff *skb, struct net_device *dev)
{
   char*  apkt;
   size_t apkt_len;
   struct nrx_softc *sc = netdev_priv(dev);

   apkt     = skb->data;
   apkt_len = skb->len;

   KDEBUG_DO(PRINTBUF, print_pkt_hdr(apkt, apkt_len));  
   KDEBUG_BUF(PRINTBUF, apkt, apkt_len, "RX");

   if(nrx_test_flag(sc, NRX_FLAG_DESTROY)) {
      KDEBUG(TRACE, "received packed while shutting down");
      dev_kfree_skb(skb);
      return;
   }

#if DE_ENABLE_PCAPLOG >= CFG_ON
   nrx_pcap_append(1, apkt, apkt_len);
#endif
   process_rx_packet(dev, skb);
}

static void
process_rx_packet(struct net_device *dev, struct sk_buff *skb)
{
   char *apkt, *spkt;
   size_t apkt_len, spkt_len;
   int r;
   uint16_t vlan;
   struct nrx_softc *sc = netdev_priv(dev);

   apkt = skb->data;
   apkt_len = skb->len;
   r = WiFiEngine_ProcessReceivedPacket(apkt, apkt_len,
                                        &spkt, &spkt_len, 
                                        &vlan,
                                        NULL);
   switch (r)
   {
      case WIFI_ENGINE_SUCCESS: /* XXX is this still correct? */
      case WIFI_ENGINE_SUCCESS_DATA_IND:
         /* Pull our header */
         skb_pull(skb, spkt - (char*)apkt);
         skb_trim(skb, spkt_len);

         skb->protocol = eth_type_trans(skb, dev);
         sc->stats.rx_packets++;
         sc->stats.rx_bytes += spkt_len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
		 if (in_interrupt())
			netif_rx(skb);
		 else
			netif_rx_ni(skb);
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24) */
         netif_rx(skb);
#endif /* LINUX_VERSION_CODE */
         dev->last_rx = jiffies;
         break;
      case WIFI_ENGINE_SUCCESS_DATA_CFM:
         nrx_tx_queue_wake(dev);
         dev_kfree_skb(skb);
         break;
      case WIFI_ENGINE_SUCCESS_ABSORBED:
         dev_kfree_skb(skb);
         break;
      case WIFI_ENGINE_SUCCESS_AGGREGATED:
         /* remove aggregation header */
         skb_pull(skb, spkt - (char*)apkt);
         skb_trim(skb, spkt_len);
         while(skb->len > 0) {
            size_t qsize = HIC_MESSAGE_LENGTH_GET(skb->data);
            struct sk_buff *new_skb;

            DE_ASSERT(qsize <= skb->len);

            new_skb = skb_clone(skb, GFP_KERNEL);
            skb_trim(new_skb, qsize);

            process_rx_packet(dev, new_skb);
            skb_pull(skb, qsize);
         }     
         dev_kfree_skb(skb);
         break;
      default:
         DE_TRACE_STATIC(TR_DATA,"Malformed HIC header. Rx failed.\n");
         dev_kfree_skb(skb);
         break;
   }		
}


/* Raw tx/rx functions to be used in unplugged mode
 */
int nrx_raw_tx(struct net_device * dev, char * data, size_t len)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int status;
   struct rawio_entry *re;

   if(nrx_test_flag(sc, NRX_FLAG_DESTROY))
      return -ENODEV;

   if(!nrx_test_flag(sc, NRX_FLAG_UNPLUGGED)) 
      return -EINVAL;

   re = kmalloc(sizeof(*re), GFP_KERNEL);
   if(re == NULL) {
      KDEBUG(TRACE, "failed to allocate rawio entry");
      return -ENOMEM;
   }

   status = WiFiEngine_SWM500_Command(~0, data, len, &re->tid);
   if(status != WIFI_ENGINE_SUCCESS) {
      KDEBUG(TRACE, "failed sending raw command (%d)", status);
      kfree(re);
      return -EINVAL;
   }
   /* XXX XXX XXX */
#define WE_SWM500_TID_CONSOLE 0x20000000
#define WE_SWM500_TID_FLASH   0x30000000
   /* XXX XXX XXX */
   nrx_dev_lock(sc);
   if(re->tid == WE_SWM500_TID_CONSOLE || re->tid == WE_SWM500_TID_FLASH) {
      /* these don't maintain the transaction id, so we need only one
       * entry, but it needs to be around ~forever */
      if(sc->rawio_console_entry != NULL) {
         nrx_dev_unlock(sc);
         kfree(re);
         return 0;
      } 
      sc->rawio_console_entry = re;
   } 
   WEI_TQ_INSERT_TAIL(&sc->rawio_head, re, next);
   nrx_dev_unlock(sc);
   
   return 0;
}

/* Raw tx/rx functions to be used in unplugged mode
 */
int nrx_raw_rx(struct net_device * dev, char * data, size_t * max_len)
{
   struct nrx_softc *sc = netdev_priv(dev);
   int status;
   struct rawio_entry *re;
   int retval = -ENOENT;
   
   if(nrx_test_flag(sc, NRX_FLAG_DESTROY))
      return -ENODEV;
   
   if(!nrx_test_flag(sc, NRX_FLAG_UNPLUGGED)) 
      return -EINVAL;

   nrx_dev_lock(sc);
   WEI_TQ_FOREACH(re, &sc->rawio_head, next) {
      status = WiFiEngine_SWM500_Reply(re->tid, data, max_len);
      if(status == WIFI_ENGINE_SUCCESS) {
         if(re != sc->rawio_console_entry) {
            WEI_TQ_REMOVE(&sc->rawio_head, re, next);
            kfree(re);
         }
         retval = 0;
         break;
      }
      if(status != WIFI_ENGINE_FAILURE_DEFER) {
         KDEBUG(TRACE, "failed to get reply, tid = %u (%d)", re->tid, status);
         retval = -EINVAL;
         break;
      }
   }
   nrx_dev_unlock(sc);
   return retval;
}


static struct net_device_stats *nrx_get_stats(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);

   return &sc->stats;
}

static void nrx_tx_timeout(struct net_device *dev)
{
   KDEBUG(TRACE, "ENTRY: %s", dev->name);
   printk("tx timeout\n");
   return;
}

void nanonet_attach(struct net_device *dev, void *data)
{
   struct nrx_softc *sc = netdev_priv(dev);
   ASSERT(sc->transport_data == NULL);
   sc->transport_data = data;
   KDEBUG(TRACE, "%s: attach", dev->name);

#ifdef CONFIG_HAS_WAKELOCK
   wake_lock(&sc->nrx_wake_lock);
   KDEBUG(TRACE, "Acquired nrx_wake_lock");
#endif
   
   nrx_set_flag(sc, NRX_FLAG_ATTACHED);
   nrx_set_state(sc, NRX_STATE_UNPLUG);
   nrx_schedule_event(sc, 0); /* kick state machine */
}
EXPORT_SYMBOL(nanonet_attach);

void nanonet_detach(struct net_device *dev, void *data)
{
   struct nrx_softc *sc = netdev_priv(dev);
   ASSERT(sc->transport_data == data);
   KDEBUG(TRACE, "%s: detach", dev->name);
   nrx_clear_flag(sc, NRX_FLAG_ATTACHED);
   nrx_clear_flag(sc, NRX_FLAG_IF_ATTACHED);
   netif_device_detach(dev);
   sc->transport_data = NULL;
}
EXPORT_SYMBOL(nanonet_detach);

static void rawio_init(struct nrx_softc *sc)
{
   WEI_TQ_INIT(&sc->rawio_head);
   sc->rawio_console_entry = NULL;
}

static void rawio_flush(struct nrx_softc *sc)
{
   struct rawio_entry *re;

   nrx_dev_lock(sc);
   sc->rawio_console_entry = NULL;
   while((re = WEI_TQ_FIRST(&sc->rawio_head)) != NULL) {
      WEI_TQ_REMOVE(&sc->rawio_head, re, next);
      kfree(re);
   }
   nrx_dev_unlock(sc);
}

void
nanonet_destroy(struct net_device *dev)
{
   
   struct nrx_softc *sc = netdev_priv(dev);
   
   KDEBUG(TRACE, "ENTRY: %s", dev->name);

   nrx_set_flag(sc, NRX_FLAG_DESTROY);

   nrx_cancel_event(sc);

   rawio_flush(sc);
     
   if (nrx_test_flag(sc, NRX_FLAG_HAVE_REGISTER)) {
      nrx_tx_queue_stop(dev);
      unregister_netdev(dev);
   }

   WiFiEngine_Unplug();

   WiFiEngine_PSControlFree(sc->ps_control);
   sc->ps_control = NULL;

   nrx_trsp_ctrl(dev, NANONET_SHUTDOWN, 0);
   
   WiFiEngine_Shutdown(0);

   nrx_proc_cleanup(dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,41)
   flush_scheduled_tasks();
#else
   flush_workqueue(sc->event_wq);
   destroy_workqueue(sc->event_wq);
   sc->event_wq = NULL;
   flush_scheduled_work();
#endif
   free_netdev(dev);

#ifdef CONFIG_HAS_WAKELOCK
   if (wake_lock_active(&sc->nrx_wake_lock))
      wake_unlock(&sc->nrx_wake_lock);
   wake_lock_destroy(&sc->nrx_wake_lock);
   KDEBUG(TRACE, "Destroyed nrx_wake_lock");
   if (wake_lock_active(&sc->nrx_scan_wake_lock))
   	  wake_unlock(&sc->nrx_scan_wake_lock);
   wake_lock_destroy(&sc->nrx_scan_wake_lock);	
   KDEBUG(TRACE, "Destroyed nrx_scan_wake_lock");  
#endif

   KDEBUG(TRACE, "EXIT:");
}


#ifdef USE_IF_REINIT

void
nrx_reg_inact_cb(int (*cb)(void *, size_t len))
{
   WiFiEngine_RegisterInactivityCallback(cb);
}
void
nrx_dereg_inact_cb(int (*cb)(void *, size_t len))
{
   WiFiEngine_DeregisterInactivityCallback(cb);
}
void
nrx_drv_quiesce(void)
{
   WiFiEngine_Quiesce();
}
void
nrx_drv_unquiesce(void)
{
   WiFiEngine_UnQuiesce();
}

#endif

EXPORT_SYMBOL(nanonet_destroy);
EXPORT_SYMBOL(nanonet_create);
EXPORT_SYMBOL(ns_net_rx);

#ifdef USE_IF_REINIT
EXPORT_SYMBOL(nrx_reg_inact_cb);
EXPORT_SYMBOL(nrx_dereg_inact_cb);
EXPORT_SYMBOL(nrx_drv_quiesce);
EXPORT_SYMBOL(nrx_drv_unquiesce);
#endif

typedef void (*mib_callback_t)(struct net_device*, void*, size_t);
struct mib_call {
   int count;
   const char *mib;
   uint32_t tid;
   mib_callback_t callback;
   WEI_TQ_ENTRY(mib_call) next;
};

WEI_TQ_HEAD(, mib_call) mib_head = WEI_TQ_HEAD_INITIALIZER(mib_head);

static inline void
add_mib_call(const char *mib, mib_callback_t callback)
{
   struct mib_call *m;

   m = kmalloc(sizeof(*m), GFP_KERNEL);
   if(m == NULL)
      return;

   m->count = 0;
   m->mib = mib;
   m->tid = 0;
   m->callback = callback;
   WEI_TQ_INSERT_TAIL(&mib_head, m, next);
}

static inline struct mib_call*
free_mib_call(struct mib_call *m)
{
   struct mib_call *next;
   WEI_TQ_REMOVE(&mib_head, m, next);
   next = WEI_TQ_NEXT(m, next);
   kfree(m);
   return next;
}

static inline int
macaddr_valid(uint8_t *data, size_t data_len)
{
   if(data == NULL || data_len != 6)
      return FALSE;
   
   if(data[0] & 0x1)
      return FALSE;

   if((data[0] | data[1] | data[2] | data[3] | data[4] | data[5]) == 0)
      return FALSE;

#if 1
   /* these addresses are common on cards, and we don't want to use
    * them */
   if(memcmp(data, "\x10\x20\x30\x40\x50", 5) == 0 &&
      (data[5] == 0x60 || data[5] == 0xbb))
      return FALSE;
#endif

   return TRUE;
}

static void
mac_callback(struct net_device *dev, void *data, size_t data_len)
{
   struct sockaddr sa;

   if(!macaddr_valid(data, data_len)) {
      int status;
      /* we failed to get a mac address, use the randomly generated one */
      status = WiFiEngine_SetMACAddress(dev->dev_addr, dev->addr_len);
      if(status != WIFI_ENGINE_SUCCESS) {
         KDEBUG(ERROR, "failed to set mac address");
      }
   } else {
      sa.sa_family = dev->type;
      memcpy(sa.sa_data, data, data_len);
      nrx_set_mac_address(dev, &sa);
   }
}

static void
rates_callback(struct net_device *dev, void *data, size_t data_len)
{
   struct nrx_softc *sc  = netdev_priv(dev);
   int status;

   if(data == NULL)
      return;

   WiFiEngine_SetSupportedRates(data, data_len);

   status = WiFiEngine_GetSupportedRates(&sc->supported_rates);
   ASSERT(status == WIFI_ENGINE_SUCCESS);
}

#if DE_LOAD_DEVICE_PARAMS == CFG_INCLUDED
static void load_parameters(struct device *dev)
{
   int status;
   const struct firmware *fw;

   status = request_firmware(&fw, "nanoradio/settings.bin", dev);
   if(status == 0 && fw != NULL) {
      status = WiFiEngine_LoadWLANParameters(fw->data, fw->size);
      release_firmware(fw);
   }
}
#endif /* DE_LOAD_DEVICE_PARAMS == CFG_INCLUDED */

static void
nrx_event_work(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
   struct work_struct *work
#else
   void *data
#endif
   )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
   struct delayed_work *dwork = container_of(work, struct delayed_work, work);
   struct nrx_softc *sc  = container_of(dwork, struct nrx_softc, event_work);
   struct net_device *dev = sc->dev;
#else
   struct net_device *dev = data;
   struct nrx_softc *sc  = netdev_priv(dev);
#endif

   int status;

   struct mib_call *m;

   KDEBUG(TRACE, "ENTRY: state = %d", sc->state);

   switch(sc->state) {
      case NRX_STATE_UNPLUG:
         WiFiEngine_Unplug();
         nrx_set_state(sc, NRX_STATE_WAIT_FW);
         nrx_schedule_event(sc, 0);
         break;

      case NRX_STATE_WAIT_FW:
         if(!nrx_test_flag(sc, NRX_FLAG_ATTACHED)) {
            break;
         }
         if(nrx_debug_wait) {
            nrx_set_state(sc, NRX_STATE_WAIT_DEBUGGER);
            nrx_schedule_event(sc, 0);
            break;
         }
         if(!nrx_test_flag(sc, NRX_FLAG_HAVE_FW))
            break;
         if(!nano_download_firmware) {
            nrx_schedule_event(sc, HZ);
            break;
         }
         if (nano_fw_download(dev) < 0) {
            nrx_clear_flag(sc, NRX_FLAG_HAVE_FW);
         }
         else
         {
            nrx_set_state(sc, NRX_STATE_PLUG);
            nrx_schedule_event(sc, 0);
         }
         break;
         
      case NRX_STATE_WAIT_DEBUGGER:
         if(nrx_debug_wait) {
            nrx_schedule_event(sc, HZ);
            break;
         }
         nrx_trsp_ctrl(dev, NANONET_INIT_SDIO, 0);

         nrx_set_state(sc, NRX_STATE_PLUG);
         nrx_schedule_event(sc, 0);
         break;

      case NRX_STATE_PLUG:
         WiFiEngine_Plug();
         if(WiFiEngine_GetRegistryPowerFlag() == PowerSave_Enabled_Deactivated_From_Start) 
         { 
            WiFiEngine_PSControlInhibit(sc->ps_control); 
         } 
         
         nrx_set_state(sc, NRX_STATE_CONFIGURE);
         nrx_schedule_event(sc, 0);
         break;

      case NRX_STATE_CONFIGURE:
         /* Ensure that no auto connect is started on old ssid:s */
         WiFiEngine_DisableSSID();
         WiFiEngine_Configure_Device(nrx_test_flag(sc, NRX_FLAG_UNPLUGGED));
         nrx_set_state(sc, NRX_STATE_WAIT_MIB);
         nrx_schedule_event(sc, 0);
         break;

      case NRX_STATE_WAIT_MIB:
         for(m = WEI_TQ_FIRST(&mib_head); m != NULL; ) {
            if(m->count == 0) {
               status = WiFiEngine_SendMIBGet(m->mib, &m->tid);
               if(status != WIFI_ENGINE_SUCCESS) {
                  m = free_mib_call(m);
                  continue;
               }
               m->count++;
               m = WEI_TQ_NEXT(m, next);
            } else {
               unsigned char data[64];
               size_t data_len = sizeof(data);
               status = WiFiEngine_GetMIBResponse(m->tid, data, &data_len);
               if(status == WIFI_ENGINE_FAILURE_NOT_ACCEPTED) {
                  if(m->count == 10) {
                     (*m->callback)(dev, NULL, 0);
                     m = free_mib_call(m);
                     continue;
                  }
                  m->count++;
                  m = WEI_TQ_NEXT(m, next);
                  continue;
               }
               if(status != WIFI_ENGINE_SUCCESS) {
                  m = free_mib_call(m);
                  continue;
               }
               (*m->callback)(dev, data, data_len);
               m = free_mib_call(m);
            }
         }
         if(!WEI_TQ_EMPTY(&mib_head))
            nrx_schedule_event(sc, HZ / 4);
         else {
            nrx_set_state(sc, NRX_STATE_START);
            nrx_schedule_event(sc, 0);
         }
         break;

      case NRX_STATE_START:
         if(!nrx_test_flag(sc, NRX_FLAG_HAVE_REGISTER)) {
            status = register_netdev(dev);
            if(status != 0) {
               KDEBUG(ERROR, "failed to register net device (%d)", status);
               printk("failed to register net device (%d)\n", status);
               nrx_set_state(sc, NRX_STATE_DEFUNCT);
               break;
            }
            if(sc->pdev != NULL) 
              //dev_info(sc->pdev, "Registered interface %s\n", sc->dev->name);
              printk("%s, Registered interface %s\n", dev_name(sc->pdev), sc->dev->name);
         }
         
         nrx_set_flag(sc, NRX_FLAG_HAVE_REGISTER);

         if (!nrx_test_flag(sc, NRX_FLAG_IF_ATTACHED)) {
            netif_device_attach(dev);
            netif_carrier_on(dev);           	
            nrx_set_flag(sc, NRX_FLAG_IF_ATTACHED);
#ifdef CONFIG_HAS_WAKELOCK
            wake_unlock(&sc->nrx_wake_lock);
            KDEBUG(TRACE, "Released nrx_wake_lock");
#endif
         }

      // nrx_wxevent_device_reset(dev); // useless
         nrx_wxevent_ap(dev);
         
         nrx_set_state(sc, NRX_STATE_RUN);
         nrx_clear_flag(sc, NRX_FLAG_SHUTDOWN);
         wake_up(&sc->mib_wait_queue);
         break;

      case NRX_STATE_RUN:
         if(nrx_clear_flag(sc, NRX_FLAG_WAKE_QUEUE)) {
            nrx_tx_queue_wake(dev);
         }
         break;
   }

}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static void
nrx_event_timer(unsigned long d)
{
  struct nrx_softc *sc = (struct nrx_softc*)d;
  nrx_schedule_event(sc, 0);
}
#endif

int ns_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

#ifdef USE_MULTIQUEUE
static u16 nrx_select_queue(struct net_device *dev, struct sk_buff *skb)
{
   u16 prio = i8021d_priority_raw(skb);

   switch(prio) {
      case 0:
      case 3:
      default:
         return 0;
      case 1:
      case 2:
         return 1;
      case 4:
      case 5:
         return 2;
      case 6:
      case 7:
         return 3;
   }
}
#endif

#ifdef HAVE_NET_DEVICE_OPS
const struct net_device_ops nanonet_ops = {
   .ndo_open                    = nrx_open,
   .ndo_stop			= nrx_stop,
   .ndo_start_xmit		= nrx_hard_start_xmit,
   .ndo_do_ioctl		= ns_net_ioctl,
   .ndo_get_stats		= nrx_get_stats,
   .ndo_tx_timeout		= nrx_tx_timeout,
   .ndo_set_mac_address		= nrx_set_mac_address,
   .ndo_set_rx_mode		= nrx_set_multicast_list,
#ifdef USE_MULTIQUEUE
   .ndo_select_queue            = nrx_select_queue,
#endif
};
#endif

struct net_device *
nanonet_create(struct device *pdev, void *data, struct nanonet_create_param *param)
{
   struct net_device *dev;
   struct nrx_softc *sc;
   int status;

#ifdef USE_MULTIQUEUE
   dev = alloc_etherdev_mq(sizeof(*sc), 4);
#else
   dev = alloc_etherdev(sizeof(struct nrx_softc));
#endif

   ASSERT(param->size == sizeof(*param));

   if(dev == NULL) {
      printk(KERN_ERR "nano_if: Failed to allocate ns device struct. Bailing out.\n");
      return NULL;
   }
    
   if(nrx_ifname != NULL && strlen(nrx_ifname) < sizeof(dev->name))
      strcpy(dev->name, nrx_ifname);

   status = dev_alloc_name(dev, dev->name);
   if(status < 0) {
      printk(KERN_ERR "nano_if: Failed to allocate interface name. Bailing out.\n");
      free_netdev(dev);
      return NULL;
   }

   /* Set up private area */
   sc = netdev_priv(dev);
   memset(sc, 0, sizeof (struct nrx_softc));
   spin_lock_init(&sc->lock);
   sc->dev = dev;
   sc->pdev = pdev;

#ifdef HAVE_NET_DEVICE_OPS
   dev->netdev_ops = &nanonet_ops;
#else
   dev->open			= nrx_open;
   dev->stop			= nrx_stop;
   dev->hard_start_xmit		= nrx_hard_start_xmit;
   dev->do_ioctl		= ns_net_ioctl;
   dev->get_stats		= nrx_get_stats;
   dev->tx_timeout		= nrx_tx_timeout;
   dev->set_mac_address		= nrx_set_mac_address;
   dev->set_multicast_list      = nrx_set_multicast_list;
#ifdef USE_MULTIQUEUE
   dev->select_queue = nrx_select_queue;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
   dev->hard_header_cache	= NULL;
#endif
#endif
   dev->watchdog_timeo		= NRX_TX_TIMEOUT;

   sc->transport_data = data;
   sc->transport = param;

   sc->maxcorecount = 32;
   sc->coreindex = 0;
   WEI_TQ_INIT(&sc->corefiles);
   
   memset(sc->cwin, 0xFF, sizeof(sc->cwin));

#ifdef CONFIG_HAS_WAKELOCK
   wake_lock_init(&sc->nrx_wake_lock, WAKE_LOCK_SUSPEND, "nrx");
   KDEBUG(TRACE, "Created nrx_wake_lock, type WAKE_LOCK_SUSPEND");
   wake_lock_init(&sc->nrx_scan_wake_lock, WAKE_LOCK_SUSPEND, "nrx-scan");
   KDEBUG(TRACE, "Created nrx_scan_wake_lock, type WAKE_LOCK_SUSPEND");    
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
   INIT_TQUEUE(&sc->event_work, nrx_event_work, dev);
   init_timer(&sc->event_timer);
   sc->event_timer.function = nrx_event_timer;
   sc->event_timer.data = (unsigned long)sc;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
   INIT_DELAYED_WORK(&sc->event_work, 
		     nrx_event_work);
#else
   INIT_WORK(&sc->event_work, nrx_event_work, dev);
#endif
 
   WiFiEngine_Initialize(dev);
   WiFiEngine_SetMsgSizeAlignment(param->min_size, 
                                  param->size_align, 
                                  param->header_size,
                                  param->header_size,
                                  param->host_attention,
                                  param->byte_swap_mode,
                                  param->host_wakeup,
                                  param->force_interval);
   
   sc->ps_control = WiFiEngine_PSControlAlloc("LINUX");

   sc->tx_hlen = WiFiEngine_GetDataHeaderSize();
   dev->hard_header_len = ETH_HLEN + sc->tx_hlen + param->tx_headroom;

   sc->tx_queue_stopped = TRUE;

   ns_setup_iw(dev);
   sc->event_wq = create_singlethread_workqueue("nrx-ev-work");
   init_waitqueue_head(&sc->mib_wait_queue);
   nrx_proc_setup(dev);

#if DE_LOAD_DEVICE_PARAMS == CFG_INCLUDED
   /* load device parameters (such as mac address) from filesystem; we
    * could in principle use dev->dev as struct device, except it's
    * not properly initialised before register_netdev, and we need
    * this before
    */
   if(sc->pdev != NULL)
      load_parameters(sc->pdev);
#endif /* DE_LOAD_DEVICE_PARAMS == CFG_INCLUDED */

   /* this is the default mac address, if we don't find a real one */
   dev->addr_len = 6;
   //WiFiEngine_RandomMAC(0, dev->dev_addr, dev->addr_len);
   //memcpy(dev->dev_addr, "\x2\x3\x4", 3);
   {
        extern int get_mac_addr(char* mac);
        get_mac_addr(dev->dev_addr);
   }

   add_mib_call(MIB_dot11MACAddress, mac_callback);
   add_mib_call(MIB_dot11OperationalRatesSet, rates_callback);

   rawio_init(sc);
   
   if(nrx_unplug) {
	  KDEBUG(TRACE, "Started in unplugged mode");
      nrx_set_flag(sc,NRX_FLAG_UNPLUGGED);
   }

   if(sc->transport->fw_download == NULL ||
      nano_download_firmware == 0) {
      nrx_set_state(sc, NRX_STATE_PLUG);
      nrx_schedule_event(sc, 0);
   } else {
      nrx_set_flag(sc, NRX_FLAG_SHUTDOWN);
      nrx_set_state(sc, NRX_STATE_WAIT_FW);
      if(nrx_debug_wait)
      	 printk("[nano] nrx_schedule_event\n"); // XXXXXXXXXXXXXXXX possibly wrong syntax
         nrx_schedule_event(sc, 0);
   }
   
   nrx_set_flag(sc, NRX_FLAG_ATTACHED);
   nrx_set_flag(sc, NRX_FLAG_IF_ATTACHED);
   KDEBUG(TRACE, "registered if %s", dev->name);

   return dev;
}

static int target_crash;
static unsigned int priomap[8];
SYSCTL_FUNCTION(nano_doreg)
{
   int status;
   
   if(write) {
      status = SYSCTL_CALL(proc_dointvec);
      if(status)
         return status;
   }
   if(strcmp(table->procname, "target_crash") == 0) {
      if(write)
	{
	  // preemption workaround for BUG2355
	  preempt_disable();
	  WiFiEngine_RequestSuicide();
	  preempt_enable();
	}
      else
         target_crash = 0;
      status = 0;
   } else if(strcmp(table->procname, "priomap") == 0) {
      int i;
      if(write) {
         for(i = 0; i < 8; i++) {
#define D(A) (((A) << 4) | (A))
            dscp_mapping[(i << 4) + 0] = D(priomap[i] & 0x7);
            dscp_mapping[(i << 4) + 1] = D(priomap[i] & 0x7);
#undef D
         }
      } else {
         for(i = 0; i < 8; i++)
            priomap[i] = dscp_mapping[i << 4] & 0x0f;
      }
      status = 0;
   } else {
      status = -EINVAL;
   }
   if(!write && status == 0) {
      status = SYSCTL_CALL(proc_dointvec);
   }	
   return status;
}

static const char release_tag[] = LINUX_RELEASE_STRING;

static ctl_table nano_net_ctable[] = {
   { SYSCTLENTRY(target_crash, target_crash, nano_doreg) },
   { SYSCTLENTRY(priomap, target_crash, nano_doreg) },
   { SYSCTLENTRY(scan_wait, nano_scan_wait, proc_dointvec) },
   { SYSCTLENTRY(fwdl, nano_download_firmware, proc_dointvec) },
   { SYSCTLENTRY(debug_wait, nrx_debug_wait, proc_dointvec) },
   { SYSCTLENTRY_MODE(release_tag, release_tag, proc_dostring, 0400) },
   { SYSCTLEND }
};

static int __init nanonet_init(void)
{
#ifdef C_LOGGING
   logger_init(&logger, log_buf, LOG_SIZE);
   KDEBUG(TRACE, "logger initiated; %d bytes in buffer",LOG_SIZE);
#endif
   nano_util_init();
   nano_util_register_sysctl(nano_net_ctable);
#if DE_ENABLE_PCAPLOG >= CFG_ON
   nrx_pcap_setup();
#endif
#ifdef C_LOGGING
   nrx_log_setup();
#endif
   return 0;
}


static void __exit nanonet_cleanup(void)
{
#ifdef PCAPLOG
   nrx_pcap_cleanup();
#endif
#ifdef C_LOGGING
   nrx_log_cleanup();
#endif
   nano_util_unregister_sysctl(nano_net_ctable);
   nano_util_cleanup();
}

module_init(nanonet_init);
module_exit(nanonet_cleanup);

