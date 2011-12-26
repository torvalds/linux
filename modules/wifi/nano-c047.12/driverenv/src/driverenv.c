/* $Id: driverenv.c,v 1.180 2008-03-01 15:15:25 ulla Exp $ */
/*****************************************************************************

Copyright (c) 2004 by Nanoradio AB

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
This module is part of the driverenv unit. It performs OS services for WiFiEngine.

*****************************************************************************/

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <asm/bitops.h>

#include "driverenv.h"

#include "ucos_defs.h"
#include "ucos.h"
#include "application.h"
#include "mac_api.h"
#include "registry.h"
#include "registryAccess.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "hmg_ps.h"
#include "hmg_traffic.h"
#include "wifi_engine.h"
#include "wifi_engine_internal.h"
#include "wei_tailq.h"
#include "mlme_proxy.h"

/*****************************************************************************
T E M P O R A R Y   T E S T V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/

/*****************************************************************************
L O C A L   D A T A T Y P E S
*****************************************************************************/

/*****************************************************************************
L O C A L   F U N C T I O N   P R O T O T Y P E S
*****************************************************************************/

/*****************************************************************************
 M O D U L E   V A R I A B L E S
*****************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static spinlock_t crit_sect = SPIN_LOCK_UNLOCKED;
#else
DEFINE_SPINLOCK(crit_sect); 
#endif
static unsigned long irq_flags;

/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/
uint32_t trace_mask = DE_INITIAL_TRACE_MASK;

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/

/*!
 * Set read thread to a higher value. This is a warkaround
 * to speed up wmm power save.
 */
void DriverEnvironment_SetPriorityThreadHigh(void)
{

}

/*!
 * Set read thread to default value.
 */
void DriverEnvironment_SetPriorityThreadLow(void)
{

}


#if 0
void de_trace_stack_usage()
{
   unsigned long stack_start;
   struct task_struct *p;
#ifdef __arm__
   register unsigned long current_sp asm("sp");
#elif defined(__i386__) || defined (__arch_um__)
   register unsigned long current_sp asm("esp");
#endif

   /* Assumes that the stack grows down */
   p = current;
   stack_start = ((long) (current_sp + PAGE_SIZE) & (~(PAGE_SIZE-1))) - 1;
   if ((struct task_struct*)current_sp <= current)
   {
      KDEBUG(STACK, "ALARM! Stack clobbered!");
   }
   KDEBUG(STACK, "Using %ld bytes of stack (%lx - %lx)",
          stack_start - current_sp, stack_start, current_sp);
}
#endif

static inline struct sk_buff*
de_find_skb(struct net_device *dev, void *buf)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct sk_buff *skb;
   unsigned long flags;

   spin_lock_irqsave(&sc->tx_alloc_queue.lock, flags);
   skb_queue_walk(&sc->tx_alloc_queue, skb) {
      if(skb->data == buf) {
         spin_unlock_irqrestore(&sc->tx_alloc_queue.lock, flags);
         return skb;
      }
   }
   spin_unlock_irqrestore(&sc->tx_alloc_queue.lock, flags);

   return NULL;
}

unsigned int DriverEnvironment_Startup(void)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   int i;
   
   KDEBUG(TRACE, "ENTRY");
   skb_queue_head_init(&sc->tx_alloc_queue);
   for (i = 0; i < WE_IND_LAST_MARK; i++)
   {
      sc->ind_cbs[i] = NULL;
   }
   /* Start up host driver environment. */
   app_init();

   WEI_TQ_INIT(&active_timers);

   WiFiEngine_Registry_LoadDefault();
   
   return (0);
}

int DriverEnvironment_Terminate(unsigned int driver_id)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   
   wei_sm_drain_sig_q();

   skb_queue_purge(&sc->tx_alloc_queue);

   return DRIVERENVIRONMENT_SUCCESS;
}

int DriverEnvironment_GetNewTimer(driver_timer_id_t *id, int restartFromCb)
{
   struct de_timer *timer;

   KDEBUG(TRACE, "ENTRY");

   timer = DriverEnvironment_Nonpaged_Malloc(sizeof(*timer));
   DE_ASSERT(timer != NULL);
   init_timer(&timer->timer);
   timer->repeating = 0;
   timer->flags = 0;
   timer->callback = NULL;
   
   *id = (driver_timer_id_t)timer;
   
   return WIFI_ENGINE_SUCCESS;
}

void DriverEnvironment_CancelTimer(driver_timer_id_t id)
{
   struct de_timer *timer = (struct de_timer*)id;
   
   if (timer == NULL)
   {
      DE_TRACE2(TR_NOISE, "%s : called with NULL id\n", __func__);
      return;
   }
   if(test_and_set_bit(DE_TIMER_LOCK, &timer->flags) == 0) {
      del_timer(&timer->timer);
      if (test_and_clear_bit(DE_TIMER_RUNNING, &timer->flags))
      {
         WEI_TQ_REMOVE(&active_timers, timer, tq);
      }
      clear_bit(DE_TIMER_LOCK, &timer->flags);
   }
   timer->repeating = 0;
}

void DriverEnvironment__FreeTimer(driver_timer_id_t id)
{
   struct de_timer *timer = (struct de_timer*)id;
   if(timer == NULL)
      return;
   if(test_and_set_bit(DE_TIMER_LOCK, &timer->flags) == 0) {
      if (test_and_clear_bit(DE_TIMER_RUNNING, &timer->flags))
      {
         WEI_TQ_REMOVE(&active_timers, timer, tq);
      }
      del_timer(&timer->timer);
      DriverEnvironment_Nonpaged_Free(timer);
   } else
      set_bit(DE_TIMER_DESTROY, &timer->flags);
}

static void
timer_callback(unsigned long d)
{
   struct de_timer *timer = (struct de_timer*)d;
   de_callback_t cb = timer->callback;

#if 0
   KDEBUG(TRACE, "ENTRY");
   DE_TRACE3(TR_NOISE, "%s : timer %p\n", __func__, timer);
#endif

   if(test_and_set_bit(DE_TIMER_LOCK, &timer->flags))
      return;

   if(test_bit(DE_TIMER_DESTROY, &timer->flags)) {
      del_timer(&timer->timer);
      WEI_TQ_REMOVE(&active_timers, timer, tq);
      timer->callback = NULL;
      clear_bit(DE_TIMER_LOCK, &timer->flags);
      DriverEnvironment_Nonpaged_Free(timer);
      return;
   } else if(timer->repeating != 0 && test_bit(DE_TIMER_RUNNING, &timer->flags))
      mod_timer(&timer->timer, jiffies + msecs_to_jiffies(timer->repeating) + 1);
   
   clear_bit(DE_TIMER_LOCK, &timer->flags);
   if(cb != NULL)
      (*cb)(NULL, 0);
}

int __de_stop_timer(struct de_timer *timer)
{
   KDEBUG(TRACE, "ENTRY");
   if(test_and_set_bit(DE_TIMER_LOCK, &timer->flags))
      return -1;
   if(test_bit(DE_TIMER_RUNNING, &timer->flags)) {
      KDEBUG(TRACE, "Stopping timer");
      clear_bit(DE_TIMER_RUNNING, &timer->flags);
      del_timer(&timer->timer);
   }
   
   clear_bit(DE_TIMER_LOCK, &timer->flags);

   return 0;
}

int __de_start_timer(struct de_timer *timer)
{
   KDEBUG(TRACE, "ENTRY");
   if(test_and_set_bit(DE_TIMER_LOCK, &timer->flags))
      return -1;
   if(test_and_set_bit(DE_TIMER_RUNNING, &timer->flags))
   {
      KDEBUG(TRACE, "Timer already running");
      clear_bit(DE_TIMER_LOCK, &timer->flags);
      return -1;
   }
   
   setup_timer(&timer->timer, timer_callback, (unsigned long)timer);
   mod_timer(&timer->timer, jiffies + msecs_to_jiffies(timer->time) + 1);

   set_bit(DE_TIMER_RUNNING, &timer->flags);
   
   clear_bit(DE_TIMER_LOCK, &timer->flags);

   return 0;
}

int DriverEnvironment_RegisterTimerCallback(long time, 
                                            driver_timer_id_t timer_id, 
                                            de_callback_t cb,
                                            int repeating)
{
   struct de_timer *timer = (struct de_timer*)timer_id;

   KDEBUG(TRACE, "ENTRY");

   ASSERT(timer != NULL);

   WIFI_LOCK();

   if (test_bit(DE_TIMER_RUNNING, &timer->flags))
   {
      DriverEnvironment_CancelTimer(timer_id);
   }
   timer->callback = cb;
   timer->time = time;
   if(repeating)
      timer->repeating = time;
   else
      timer->repeating = 0;
   WEI_TQ_INSERT_TAIL(&active_timers, timer, tq);
   setup_timer(&timer->timer, timer_callback, (unsigned long)timer);
   mod_timer(&timer->timer, jiffies + msecs_to_jiffies(time) + 1);
   set_bit(DE_TIMER_RUNNING, &timer->flags);

   WIFI_UNLOCK();

   return 1;
}

driver_msec_t DriverEnvironment_GetTimestamp_msec(void)
{
   /* XXX check wrapping jiffies */
#if HZ == 100
   return jiffies * 10;
#elif HZ == 108
	return (jiffies * 9 + jiffies / 4); /* 0.1% error! */ 
#elif HZ == 200
   return jiffies * 5;
#elif HZ == 250
   return jiffies * 4;
#elif HZ == 1000
   return jiffies;
#else
   return jiffies_to_msecs(jiffies);
#endif
} 

void DriverEnvironment_GetTimestamp_wall(long *sec, long *usec)
{
   /* 2.5.35 changed timekeeping to use timespec */
   /* 2.5.48 added an api to read xtime properly */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,35)
   struct timespec now;
#else
   struct timeval now;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,48)
   now = current_kernel_time();
#else
   do_gettimeofday(&now); /* this is slower (with better precision)
                           * than current_kernel_time, so we should
                           * perhaps read xtime directly */
#endif
   *sec = now.tv_sec;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,35)
   *usec = now.tv_nsec / 1000;
#else
   *usec = now.tv_usec;
#endif
}

#ifdef MEM_TRACE

#include "wei_tailq.h"

struct meminfo {
   const void *ptr;
   size_t size;
   int type;
   unsigned long time;
   char func[32];
   unsigned int line;
   WEI_TQ_ENTRY(meminfo) next;
};

static WEI_TQ_HEAD(, meminfo) mfree = WEI_TQ_HEAD_INITIALIZER(mfree);
static WEI_TQ_HEAD(, meminfo) mactive = WEI_TQ_HEAD_INITIALIZER(mactive);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
static spinlock_t memlock = SPIN_LOCK_UNLOCKED;
#else
DEFINE_SPINLOCK(memlock); 
#endif

#define MEMSIZE 32
static uint32_t memsize[MEMSIZE];
static size_t memused_cur;
static size_t memused_high;

#define MDEBUG(_M, _F, ...) KDEBUG(MEMORY, _F ":%s:%u:%p(%lu)" , ##__VA_ARGS__ , (_M)->func, (_M)->line, (_M)->ptr, (unsigned long)(_M)->size)

static struct meminfo*
memtrace_find(const void *ptr, int active)
{
   struct meminfo *mp;

   if(active) {
      WEI_TQ_FOREACH(mp, &mactive, next) {
         if(mp->ptr == ptr)
            return mp;
      }
   } else {
      WEI_TQ_FOREACH(mp, &mfree, next) {
         if(mp->ptr == ptr)
            return mp;
      }
   }
   return NULL;
}

static void
memtrace_remove(struct meminfo *mp)
{
   WEI_TQ_REMOVE(&mactive, mp, next);
   WEI_TQ_INSERT_TAIL(&mfree, mp, next);
}

#if 0
static void memtrace_dump(const char *m)
{
   struct meminfo *mp;
   int i = 0;
   
   KDEBUG(MEMORY, "%s ---", m);
   WEI_TQ_FOREACH(mp, &mactive, next) {
      KDEBUG(MEMORY, "%d %s:%u %p(%lu)", i, mp->func, mp->line, mp->ptr, (unsigned long)mp->size);
      i++;
   }
}
#endif

void
memtrace_alloc(const void *ptr, size_t len, int type, 
               const char *func, unsigned int line)
{
   struct meminfo *mp;
   
   spin_lock(&memlock);

   if((mp = memtrace_find(ptr, 1)) != NULL) {
      MDEBUG(mp, "ptr not free'd");
      memtrace_remove(mp);
   }
   
   if(len < MEMSIZE)
      memsize[len]++;
   else
      memsize[MEMSIZE - 1]++;

   if((mp = WEI_TQ_FIRST(&mfree)) != NULL)
      WEI_TQ_REMOVE(&mfree, mp, next);
   else
      mp = (kmalloc)(sizeof(*mp), GFP_ATOMIC);
   memset(mp, 0, sizeof(mp));
   mp->ptr = ptr;
   mp->size = len;
   mp->type = type;
   mp->time = jiffies;
   strlcpy(mp->func, func, sizeof(mp->func));
   mp->line = line;
   MDEBUG(mp, "alloc");
   WEI_TQ_INSERT_TAIL(&mactive, mp, next);

   memused_cur += mp->size;
   if(memused_cur > memused_high)
      memused_high = memused_cur;

   spin_unlock(&memlock);
}

void
memtrace_free(const void *ptr, int type, int poison,
              const char *func, unsigned int line)
{
   struct meminfo *mp;
   
   if(ptr == NULL)
      return;
   
   spin_lock(&memlock);
   if((mp = memtrace_find(ptr, 1)) != NULL) {
      KDEBUG(MEMORY, "%p(%lu):%s:%u->%s:%u %lu", mp->ptr, (unsigned long)mp->size, mp->func, mp->line, func, line, jiffies - mp->time);
      DE_BUG_ON(type != mp->type, "type(%c) != mp->type(%c)\n", type, mp->type);
      memused_cur -= mp->size;
      if(poison)
         memset((void*)mp->ptr, 0x77, mp->size);
      memtrace_remove(mp);
      spin_unlock(&memlock);
      return;
   }
   
   if((mp = memtrace_find(ptr, 0)) == NULL) {
      KDEBUG(TRACE, "ptr not found %s:%u:%p", func, line, ptr);
   } else {
      MDEBUG(mp, "free");
   }
   spin_unlock(&memlock);
   BUG();
}

#include "px.h"

void memtrace_proc(struct nrx_px_softc *sc)
{
   struct meminfo *mp;
   int i;
   
   nrx_px_setsize(sc, 0);
   spin_lock(&memlock);
   WEI_TQ_FOREACH(mp, &mactive, next) {
      nrx_px_printf(sc, "%s:%lu %p %lu %c\n", mp->func, mp->line, mp->ptr, mp->size, mp->type);
   }
   nrx_px_printf(sc, "--\n");
   nrx_px_printf(sc, "current = %lu\n", (unsigned long)memused_cur);
   nrx_px_printf(sc, "high = %lu\n", (unsigned long)memused_high);
   nrx_px_printf(sc, "--\n");
   for(i = 0; i < MEMSIZE; i++) {
      if(memsize[i] != 0)
         nrx_px_printf(sc, "%u %u\n", i, memsize[i]);
   }
   spin_unlock(&memlock);
}

#endif /* MEM_TRACE */

static inline void
tx_alloc_unlink(struct net_device *dev, struct sk_buff *skb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
   struct nrx_softc *sc = netdev_priv(dev);
   skb_unlink(skb, &sc->tx_alloc_queue);
#else
   skb_unlink(skb);
#endif
}


int DriverEnvironment__HIC_Send(char* message, size_t size)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   int r;
   struct sk_buff *skb;

   skb = de_find_skb(dev, message);
   DE_ASSERT(skb != NULL);
   tx_alloc_unlink(dev, skb);
   DE_BUG_ON(skb->len != size, "allocated size != actual, skb->len = %lu, size = %lu, buf = %p\n", (unsigned long)skb->len, (unsigned long)size, message);

   DE_TRACE_STACK_USAGE;
   r = nrx_send_buf(skb);
   if (r < 0)
   {
      DE_TRACE2(TR_NOISE, "Failed to send command (error nr %d)\n", r);
      return DRIVERENVIRONMENT_FAILURE;
   }
   return DRIVERENVIRONMENT_SUCCESS;

}

void* DriverEnvironment__TX_Alloc(int size)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   struct sk_buff *skb;

   skb = dev_alloc_skb(size);
   DE_ASSERT(skb != NULL);
   skb->dev = dev;
   DE_ASSERT(skb->dev != NULL);

   skb_queue_tail(&sc->tx_alloc_queue, skb);

   return skb_put(skb, size);
}

void DriverEnvironment__TX_Free(void *p)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct sk_buff *skb;

   skb = de_find_skb(dev, p);
   DE_ASSERT(skb != NULL);
   tx_alloc_unlink(dev, skb);
   dev_kfree_skb(skb);
}

extern int nrx_wmm_association;

void DriverEnvironment_indicate(we_indication_t type, void *data, size_t len)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   int num;

   num = we_ind_send(type, data);

   switch(type) {
   case WE_IND_SLEEP_FOREVER:
      KDEBUG(TRACE, "WE_IND_SLEEP_FOREVER");
      WiFiEngine_Unplug();
      nrx_set_flag(sc, NRX_FLAG_SHUTDOWN);
      wake_up(&sc->mib_wait_queue);
      break;
   case WE_IND_RSSI_TRIGGER:
      KDEBUG(TRACE, "WE_IND_RSSI_TRIGGER");
      DE_ASSERT(len == sizeof(int32_t));
      nrx_wxevent_rssi_trigger(WiFiEngine_GetAdapter(), *(int32_t*)data);
      break;
   case WE_IND_SCAN_COMPLETE:
      KDEBUG(TRACE, "WE_IND_SCAN_COMPLETE");
      wake_up(&sc->mib_wait_queue);
      if(data == NULL) {
         nrx_wxevent_scan_complete(dev); 
      } else if(len == sizeof(m80211_nrp_mlme_scannotification_ind_t)){
         nrx_wxevent_scan(dev, (m80211_nrp_mlme_scannotification_ind_t *)data);
      }
      break;
   case WE_IND_ROAMING_COMPLETE:
   case WE_IND_80211_IBSS_CONNECTED: 
   case WE_IND_CM_CONNECTED:
      DE_TRACE1(TR_NOISE, "Indicating Media status connected\n");
      netif_carrier_on(dev);
      nrx_tx_queue_wake(dev);
      nrx_wxevent_ap(dev);
      wake_up(&sc->mib_wait_queue);
      nrx_wmm_association = WiFiEngine_IsAssocWMM();
      break;
   case WE_IND_80211_IBSS_DISCONNECTED:
   case WE_IND_CM_DISCONNECTED:
      DE_TRACE1(TR_NOISE, "Indicating Media status disconnected\n");
      netif_carrier_off(dev);
      //ignore useless event,otherwise it will lead supplicant confuse.
      if(NRX_STATE_UNPLUG != sc->state){
      nrx_wxevent_ap(dev);
      }
      wake_up(&sc->mib_wait_queue);
      nrx_wmm_association = 0;
      break;
   case WE_IND_CM_DISCONNECTING:
      {
         /* TODO: FIXME */
#if 0
   WE_CONN_LOST_AUTH_FAIL,
   WE_CONN_LOST_ASSOC_FAIL,
   WE_CONN_LOST_REASSOC_FAIL,   
   WE_CONN_LOST_DEAUTH,
   WE_CONN_LOST_DEAUTH_IND,
   WE_CONN_LOST_DISASS,
   WE_CONN_LOST_DISASS_IND
#endif
         we_conn_lost_ind_t ind;
         we_con_lost_s lost;
         WiFiEngine_get_last_con_lost_reason(&lost);
         ind.reason_code = lost.reason_code;
         ind.type = WE_CONN_LOST_DEAUTH;
         WiFiEngine_GetBSSID(&ind.bssid);
         nrx_wxevent_connection_lost(dev, &ind);
      }
      break;
   case WE_IND_AUTH_STATUS_CHANGE:
      DE_TRACE1(TR_NOISE, "Indicating auth status change (not doing anything in this implementation\n");
      break;
   case WE_IND_CONN_INCOMPATIBLE:
      /* TODO:FIXME: replaced by WE_IND_80211_CONNECT_FAILED */
      DE_ASSERT(len == sizeof(we_conn_incompatible_ind_t));
      nrx_wxevent_incompatible(dev, (we_conn_incompatible_ind_t *)data);
      break;
   case WE_IND_CANDIDATE_LIST: {
      CandidateInfo *cinfo;
      DE_ASSERT(len == sizeof(*cinfo));
      cinfo = data;
      nrx_wxevent_pmkid_candidate(dev, 
                                  cinfo->bssId.octet,
                                  cinfo->rssi_info,
                                  cinfo->flag);
   }
      break;
   case WE_IND_TX_QUEUE_FULL:
      nrx_tx_queue_stop(dev);
      break;
   case WE_IND_ACTIVITY_TIMEOUT:
      DE_TRACE1(TR_NOISE, "Driver inactivity indicated\n");
      break;
   case WE_IND_PAIRWISE_MIC_ERROR: 
   case WE_IND_GROUP_MIC_ERROR: {
      m80211_mac_addr_t *bssid = data;
      nrx_wxevent_michael_mic_failure(dev, 
				      bssid->octet, 
				      type == WE_IND_GROUP_MIC_ERROR);
      break;
   }
   case WE_IND_BEACON:
      nrx_wxevent_no_beacon();
      break;
   case WE_IND_TXFAIL:
      nrx_wxevent_txfail();
      break;
   case WE_IND_SCAN_INDICATION:
      break;
   default:
      if(num==0) {
         KDEBUG(TRACE, "indication of type %d is unhandled", type);
      }
      break;
   }
}

#if 0
// deprecated by we_ind.c
int DriverEnvironment_Register_Ind_Handler(we_indication_t type, 
                                           we_ind_cb_t cb, 
                                           const void *ctx)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   
   DE_ASSERT(ctx == NULL);
   DE_ASSERT(sc->ind_cbs[type] == NULL);
   sc->ind_cbs[type] = cb;

   return DRIVERENVIRONMENT_SUCCESS;
}

int DriverEnvironment_Deregister_Ind_Handler(we_indication_t type,
                                             we_ind_cb_t cb, 
                                             const void *ctx)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);

   DE_ASSERT(ctx == NULL);
   if (sc->ind_cbs[type] == cb) {
      sc->ind_cbs[type] = NULL;
   }
   
   return DRIVERENVIRONMENT_SUCCESS;
}

int DriverEnvironment_Is_Ind_Handler_Registered(we_indication_t type,
						we_ind_cb_t cb)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);

   if(sc->ind_cbs[type] == cb) {
      return DRIVERENVIRONMENT_SUCCESS;
   }
   
   return DRIVERENVIRONMENT_FAILURE;
}
#endif

/*!
 * Important! Never access paged memory while holding a lock using
 * these functions!
 */

void DriverEnvironment_init_trylock(driver_trylock_t *lock)
{
   lock = LOCK_UNLOCKED;
}

driver_trylock_t DriverEnvironment_acquire_trylock(driver_trylock_t *lock)
{
   int ret;
   
   ret = test_and_set_bit(0, (void *)lock);

   return ret;
}

void DriverEnvironment_release_trylock(driver_trylock_t *lock)
{
   clear_bit(0, (void *)lock);
}

void DriverEnvironment_acquire_read_lock(driver_lock_t *lock)
{
   DriverEnvironment_acquire_lock(lock);
}

void DriverEnvironment_release_read_lock(driver_lock_t *lock) 
{
   DriverEnvironment_release_lock(lock);
}

void DriverEnvironment_acquire_write_lock(driver_lock_t *lock)
{
   DriverEnvironment_acquire_lock(lock);
}

void DriverEnvironment_release_write_lock(driver_lock_t *lock)
{
   DriverEnvironment_release_lock(lock);
}

void DriverEnvironment_enable_target_sleep(void) 
{
   nrx_trsp_ctrl(WiFiEngine_GetAdapter(), 
                 NANONET_SLEEP, 
                 NANONET_SLEEP_ON);
}

void DriverEnvironment_disable_target_sleep(void) 
{
   nrx_trsp_ctrl(WiFiEngine_GetAdapter(), 
                 NANONET_SLEEP, 
                 NANONET_SLEEP_OFF);
}

void DriverEnvironment_disable_target_interface(void) 
{
   DE_TRACE1(TR_NOISE, "Calling disableTargetInterface\n");
}

void DriverEnvironment_Enable_Boot(void) 
{
   nrx_trsp_ctrl(WiFiEngine_GetAdapter(), 
                 NANONET_BOOT, 
                 NANONET_BOOT_ENABLE);
}


void DriverEnvironment_Disable_Boot(void) 
{
   nrx_trsp_ctrl(WiFiEngine_GetAdapter(), 
                 NANONET_BOOT, 
                 NANONET_BOOT_DISABLE);
}

struct de_coredump_priv {
   size_t size;
   size_t offset;
   char *data;
};

/*
 * needs to allocate 164k for coredump but can not use vmalloc/vfree.
 * may need to be freed from interupt ctx (command_to_cb).
 */
#define CORE_DUMP_MAX_SIZE 170000
static char core_dump_buf[CORE_DUMP_MAX_SIZE];

void DriverEnvironment_Core_Dump_Started(
      int coredump,
      int restart, 
      uint8_t objId, 
      uint8_t errCode, 
      size_t expected_size, /* or more */
      size_t max_size, /* used to help malloc */
      void **ctx)
{
   struct de_coredump_priv *s;

   KDEBUG(TRACE, "ENTRY");

   *ctx = NULL;

   if(!coredump)
      return;

   if(max_size > CORE_DUMP_MAX_SIZE)
      return;

   if(nrx_get_corecount(WiFiEngine_GetAdapter()) <= 0)
      return;

   s = (struct de_coredump_priv*)DriverEnvironment_Nonpaged_Malloc(sizeof(struct de_coredump_priv));
   if(!s) {
      KDEBUG(TRACE, "failed to alloc coredump_priv");
      return;
   }
   s->size = max_size;
   s->data = (char*)&core_dump_buf[0];
   s->offset = 0;

   *ctx = s;
}

/*!
 * @brief Write coredump data to media.
 *
 * The function is called after DriverEnvironment_Core_Dump_Started if *ctx != NULL
 */
void DriverEnvironment_Core_Dump_Write(
   void *ctx,
   void *data, 
   size_t len)
{
   struct de_coredump_priv *s = (struct de_coredump_priv*)ctx;

   if(!s)
      return;

   if( s->size < s->offset + len ) {
      KDEBUG(TRACE, "warning: tryed to save more data then available (%lu<%lu)", (unsigned long)s->size, (unsigned long)(s->offset+len));
      return;
   }

   DE_MEMCPY(s->data + s->offset, data, len);
   s->offset += len;
}

void DriverEnvironment_Core_Dump_Abort(
      int coredump,
      int restart, 
      uint8_t objid, 
      uint8_t err_code, 
      void **ctx)
{
   struct de_coredump_priv *s = (struct de_coredump_priv*)*ctx;

   KDEBUG(TRACE, "ENTRY");

   if(s) {
      DriverEnvironment_Nonpaged_Free(s);
   }
   *ctx = NULL;
}

void DriverEnvironment_Core_Dump_Complete(
      int coredump,
      int restart, 
      uint8_t objid, 
      uint8_t err_code, 
      void **ctx)
{
   struct de_coredump_priv *s = (struct de_coredump_priv*)*ctx;
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);

   KDEBUG(TRACE, "ENTRY");
   
   nrx_set_flag(sc, NRX_FLAG_SHUTDOWN);

   if(coredump && s)
      nrx_create_coredump(dev, objid, err_code, s->data, s->offset);

   /* Ignore the restart flag */
   nrx_set_state(sc, NRX_STATE_UNPLUG);
   nrx_schedule_event(sc, 0);

   if(s) {
      DriverEnvironment_Nonpaged_Free(s);
      *ctx = NULL;
   }
}

/*! 
 * This will be called when the target device is woken up.  It does
 * driver-specific things that need to be taken care of (such as
 * starting to send queued packets).
 */
void DriverEnvironment_handle_driver_wakeup(void) 
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   nrx_tx_queue_wake(dev);
}

/*! Generate (pseudo-)random data. Level of randomness is not
 *  specified, so it should not be used for cryptographic purposes.
 */
void
DriverEnvironment_RandomData(void *data, size_t len)
{
   get_random_bytes(data, len);
}

int DriverEnvironment_LittleEndian2Native(char *dst, size_t len)
{
   switch(len) {
      case 0:
      case 1:
         break;
      case 2:
         le16_to_cpus(dst);
         break;
      case 4:
         le32_to_cpus(dst);
         break;
      case 8:
         le64_to_cpus(dst);
         break;
      default:
         DE_BUG_ON(len, "len = %lu\n", (unsigned long)len);
   }
   return 0;
}

/* Design note
 *
 * Multiple events on platforms with only one system event object
 *
 * Each de_event_t points to the same system event object. Each
 * de_signal_event() will signal a specific de_event, setting that
 * event to state DE_SIG_SIGNALLED. It then signals the system event
 * object which will wakeup all blocked threads.  de_wait_on_event()
 * will wakeup and has to check event.state to see if the state is
 * CLEAR or SIGNALLED, if CLEAR some other event was signalled so
 * de_wait_on_event() should resume waiting on the system event
 * object. Making the timeout accurate in this case might be tricky
 * though.
 *
 * Platforms without event support :
 * 
 * Make DriverEnvironment_IsEventWaitAllowed() always return 0.
 */
/*!
 * Check if waiting on an event is allowed in this context.
 * For drivers not supporting events this should always return 0.
 *
 * @return 1 if waiting on an event is allowed in this context.
 */
int DriverEnvironment_IsEventWaitAllowed(void)
{
#ifdef NOTYET
   if(in_interrupt())
      return 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
#define current_is_keventd() (current->pid == 2)
   if(current_is_keventd()) /* do we need to special case other threads too? 
                               what about 2.6? */
      return 0;
#endif
   return 1;
#else
   return 0;
#endif
}

int __de_init_event(de_event_t *ev)
{
   init_waitqueue_head(&ev->wait_queue);
   return 1;
}

int __de_uninit_event(de_event_t *ev)
{
   return 1;
}

int __de_signal_event(de_event_t *ev)
{
   wake_up(&ev->wait_queue);
   return 1;
}

int __de_reset_event(de_event_t *ev)
{
   return 1;
}

/*!
 * Platform specific wait on event.
 *
 * @param ev Event struct to wait on.
 * @param ms_to_wait Wait timeout in ms. 0 to wait forever.
 * @return 
 * - 1 if the event was signalled. 
 * - 0 on timeout.
 */
int __de_wait_on_event(de_event_t *ev, int ms_to_wait)
{
   DECLARE_WAITQUEUE(wait, current);
   long timeout;
   if(ms_to_wait == 0)
      timeout = MAX_SCHEDULE_TIMEOUT;
   else
      timeout = msecs_to_jiffies(ms_to_wait);
   while(timeout != 0) {
      add_wait_queue(&ev->wait_queue, &wait);
      set_current_state(TASK_UNINTERRUPTIBLE);
      if(ev->state != DE_SIG_SIGNALLED)
         timeout = schedule_timeout(timeout);
      remove_wait_queue(&ev->wait_queue, &wait);
      set_current_state(TASK_RUNNING);
      if(ev->state == DE_SIG_SIGNALLED)
	 break;
   }
   return (ev->state == DE_SIG_SIGNALLED);
}


#ifdef C_LOGGING_WITH_TIMESTAMPS
void DriverEnvironment_get_current_time(de_time_t *ts)
{
   unsigned long j = jiffies;
   ts->tv_sec = j / HZ;
   ts->tv_usec = (j % HZ) * 1000000 / HZ;
}
#endif

void DriverEnvironment_MonitorEnter(void)
{
    spin_lock_irqsave(&crit_sect, irq_flags);
}

void DriverEnvironment_MonitorExit(void)
{
   spin_unlock_irqrestore(&crit_sect, irq_flags);
}

/*****************************************************************************
L O C A L    F U N C T I O N S
*****************************************************************************/
/******************************* END OF FILE ********************************/
