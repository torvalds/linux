#ifndef __compat24_h__
#define __compat24_h__

#include <linux/module.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,29)
#define MSEC_PER_SEC 1000L

#if HZ != 100 && HZ != 250 && HZ != 1000
#error fixme
#endif

static inline unsigned int jiffies_to_msecs(const unsigned long j)
{
   return (MSEC_PER_SEC / HZ) * j;
}

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
   if (m > jiffies_to_msecs(MAX_JIFFY_OFFSET))
      return MAX_JIFFY_OFFSET;
   return (m + (MSEC_PER_SEC / HZ) - 1) / (MSEC_PER_SEC / HZ);
}
#endif /* < 2.4.33 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15) 
static inline void
setup_timer(struct timer_list *timer,
            void (*function)(unsigned long),
            unsigned long data)
{
   timer->function = function;
   timer->data = data;
   init_timer(timer);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,23) 
static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
	return (struct proc_dir_entry *)inode->u.generic_ip;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static inline void i_size_write(struct inode *inode, loff_t i_size)
{
   inode->i_size = i_size;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
static inline int read_trylock(rwlock_t *lock)
{
   atomic_t *count = (atomic_t *)lock;
   atomic_dec(count);
   if (atomic_read(count) >= 0)
      return 1;
   atomic_inc(count);
   return 0;
}
#endif

#endif /* __compat24_h__ */
