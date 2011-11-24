/* $Id: nanoparam.h 18442 2011-03-22 15:26:38Z joda $ */
#ifndef _NANOPARAM_H
#define _NANOPARAM_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <net/iw_handler.h>

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif

#include "wifi_engine.h"
#include "wei_tailq.h"
#include "px.h"

#define NRX_TX_TIMEOUT (20*HZ)

struct rawio_entry {
   WEI_TQ_ENTRY(rawio_entry) next;
   uint32_t tid;
};

struct nrx_softc {
   spinlock_t		lock;
   struct net_device_stats stats;
   struct iw_statistics wstats;
   struct net_device    *dev;
   struct device        *pdev; /* "parent" device, pci or similar */

   unsigned int state;
#define NRX_STATE_WAIT_FW       0
#define NRX_STATE_PLUG          1
#define NRX_STATE_WAIT_MIB      2
#define NRX_STATE_START         3
#define NRX_STATE_RUN           4
#define NRX_STATE_DEFUNCT       5
#define NRX_STATE_WAIT_DEBUGGER 6
#define NRX_STATE_UNPLUG        7
#define NRX_STATE_CONFIGURE     8

   unsigned long flags;
#define NRX_FLAG_HAVE_FW        0
#define NRX_FLAG_HAVE_MIB       1
#define NRX_FLAG_HAVE_REGISTER  2
#define NRX_FLAG_SHUTDOWN       3 /* target needs restart */
#define NRX_FLAG_UNPLUGGED      4 /* nanoloader mode */
#define NRX_FLAG_DESTROY        5 /* driver is shutting down */
#define NRX_FLAG_ATTACHED       6 /* transport driver is attached */
#define NRX_FLAG_IF_DOWN		7 /* network interface is down */
#define NRX_FLAG_WAKE_QUEUE	8 /* wake queue after delay */
#define NRX_FLAG_IF_ATTACHED    9 /* network interface is attached */

   /* parent handles */
   struct nanonet_create_param *transport;
   struct device *transport_data;
    
   /* Our /proc dir */
   struct proc_dir_entry *proc_dir;
   struct proc_dir_entry *core_dir;
   struct proc_dir_entry *counters_dir;
   struct proc_dir_entry *config_dir;

   WEI_TQ_HEAD(, rawio_entry) rawio_head;
   struct rawio_entry *rawio_console_entry;

   struct sk_buff_head tx_alloc_queue;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   struct tq_struct event_work;
   struct timer_list event_timer;
#elif  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
   struct workqueue_struct *event_wq;
   struct delayed_work event_work;
#else
   struct workqueue_struct *event_wq;
   struct work_struct event_work;
#endif

   wait_queue_head_t mib_wait_queue;

   struct nrx_px_entry_head corefiles;
   uint32_t coreindex;
   uint32_t maxcorecount;

   we_ratemask_t supported_rates;

   size_t tx_hlen; /* HIC + DATA header size */
	
   int auth_param[11];
   
   int scan_notif_count;
   
   we_ind_cb_t ind_cbs[WE_IND_LAST_MARK];

   uint8_t cwin[4][2];

   we_ps_control_t *ps_control;

   int tx_queue_stopped;

#ifdef CONFIG_HAS_WAKELOCK
   struct wake_lock nrx_wake_lock;
   struct wake_lock nrx_scan_wake_lock;
#endif
};

static inline void nrx_dev_lock(struct nrx_softc *sc)
{
   spin_lock(&sc->lock);
}

static inline void nrx_dev_unlock(struct nrx_softc *sc)
{
   spin_unlock(&sc->lock);
}

static inline unsigned int nrx_set_state(struct nrx_softc *sc, 
                                        unsigned int new_state)
{
   unsigned int old_state = sc->state;
   sc->state = new_state;
   return old_state;
}

static inline int nrx_set_flag(struct nrx_softc *sc, unsigned int flag)
{
   return test_and_set_bit(flag, &sc->flags);
}

static inline int nrx_clear_flag(struct nrx_softc *sc, unsigned int flag)
{
   return test_and_clear_bit(flag, &sc->flags);
}

static inline int nrx_test_flag(struct nrx_softc *sc, unsigned int flag)
{
   return test_bit(flag, &sc->flags);
}

/* delay in jiffies */
static inline int nrx_schedule_event(struct nrx_softc *sc,
                                     unsigned long delay)
{
   if(nrx_test_flag(sc, NRX_FLAG_DESTROY)) {
      return 0;
   }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   if(delay == 0)
      return schedule_task(&sc->event_work);
   return mod_timer(&sc->event_timer, jiffies + delay);
#else
   if(delay == 0)
     {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
       return queue_work(sc->event_wq, &sc->event_work.work);
#else
       return queue_work(sc->event_wq, &sc->event_work);
#endif
     }
   else
      return queue_delayed_work(sc->event_wq, &sc->event_work, delay);
#endif
}

static inline void nrx_cancel_event(struct nrx_softc *sc)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   del_timer_sync(&sc->event_timer);
#else
   cancel_delayed_work(&sc->event_work);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
#define SYSCTL_FUNCTION(F) static int F(ctl_table *table, int write, struct file *filp, void *buffer, size_t *lenp)
#define SYSCTL_CALL(F) F(table, write, filp, buffer, lenp)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
#define SYSCTL_FUNCTION(F) static int F(ctl_table *table, int write, struct file *filp, void __user *buffer, size_t *lenp, loff_t *ppos)
#define SYSCTL_CALL(F) F(table, write, filp, buffer, lenp, ppos)
#else
#define SYSCTL_FUNCTION(F) static int F(ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
#define SYSCTL_CALL(F) F(table, write, buffer, lenp, ppos)
#endif

#ifndef HAVE_NETDEV_PRIV /* this macro was introduced in 2.4.27 netdevice.h */
#define netdev_priv(X) ((X)->priv)
#endif
#ifndef HAVE_FREE_NETDEV /* this macro was introduced in 2.4.23 netdevice.h */
#define free_netdev(X) kfree((X))
#endif


#define CHECK_UNPLUG(dev) ({                                            \
         if(nrx_test_flag(netdev_priv(dev), NRX_FLAG_SHUTDOWN)          \
            || nrx_test_flag(netdev_priv(dev), NRX_FLAG_UNPLUGGED)) {   \
            return -ENETDOWN;                                           \
         }                                                              \
      })


#endif /* _NANOPARAM_H */
