#ifndef _LOCKHELP_H
#define _LOCKHELP_H
#include <linux/config.h>

#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

/* Header to do help in lock debugging. */

#ifdef CONFIG_NETFILTER_DEBUG
struct spinlock_debug
{
	spinlock_t l;
	atomic_t locked_by;
};

struct rwlock_debug
{
	rwlock_t l;
	long read_locked_map;
	long write_locked_map;
};

#define DECLARE_LOCK(l) 						\
struct spinlock_debug l = { SPIN_LOCK_UNLOCKED, ATOMIC_INIT(-1) }
#define DECLARE_LOCK_EXTERN(l) 			\
extern struct spinlock_debug l
#define DECLARE_RWLOCK(l)				\
struct rwlock_debug l = { RW_LOCK_UNLOCKED, 0, 0 }
#define DECLARE_RWLOCK_EXTERN(l)		\
extern struct rwlock_debug l

#define MUST_BE_LOCKED(l)						\
do { if (atomic_read(&(l)->locked_by) != smp_processor_id())		\
	printk("ASSERT %s:%u %s unlocked\n", __FILE__, __LINE__, #l);	\
} while(0)

#define MUST_BE_UNLOCKED(l)						\
do { if (atomic_read(&(l)->locked_by) == smp_processor_id())		\
	printk("ASSERT %s:%u %s locked\n", __FILE__, __LINE__, #l);	\
} while(0)

/* Write locked OK as well. */
#define MUST_BE_READ_LOCKED(l)						    \
do { if (!((l)->read_locked_map & (1UL << smp_processor_id()))		    \
	 && !((l)->write_locked_map & (1UL << smp_processor_id())))	    \
	printk("ASSERT %s:%u %s not readlocked\n", __FILE__, __LINE__, #l); \
} while(0)

#define MUST_BE_WRITE_LOCKED(l)						     \
do { if (!((l)->write_locked_map & (1UL << smp_processor_id())))	     \
	printk("ASSERT %s:%u %s not writelocked\n", __FILE__, __LINE__, #l); \
} while(0)

#define MUST_BE_READ_WRITE_UNLOCKED(l)					  \
do { if ((l)->read_locked_map & (1UL << smp_processor_id()))		  \
	printk("ASSERT %s:%u %s readlocked\n", __FILE__, __LINE__, #l);	  \
 else if ((l)->write_locked_map & (1UL << smp_processor_id()))		  \
	 printk("ASSERT %s:%u %s writelocked\n", __FILE__, __LINE__, #l); \
} while(0)

#define LOCK_BH(lk)						\
do {								\
	MUST_BE_UNLOCKED(lk);					\
	spin_lock_bh(&(lk)->l);					\
	atomic_set(&(lk)->locked_by, smp_processor_id());	\
} while(0)

#define UNLOCK_BH(lk)				\
do {						\
	MUST_BE_LOCKED(lk);			\
	atomic_set(&(lk)->locked_by, -1);	\
	spin_unlock_bh(&(lk)->l);		\
} while(0)

#define READ_LOCK(lk) 						\
do {								\
	MUST_BE_READ_WRITE_UNLOCKED(lk);			\
	read_lock_bh(&(lk)->l);					\
	set_bit(smp_processor_id(), &(lk)->read_locked_map);	\
} while(0)

#define WRITE_LOCK(lk)							  \
do {									  \
	MUST_BE_READ_WRITE_UNLOCKED(lk);				  \
	write_lock_bh(&(lk)->l);					  \
	set_bit(smp_processor_id(), &(lk)->write_locked_map);		  \
} while(0)

#define READ_UNLOCK(lk)							\
do {									\
	if (!((lk)->read_locked_map & (1UL << smp_processor_id())))	\
		printk("ASSERT: %s:%u %s not readlocked\n", 		\
		       __FILE__, __LINE__, #lk);			\
	clear_bit(smp_processor_id(), &(lk)->read_locked_map);		\
	read_unlock_bh(&(lk)->l);					\
} while(0)

#define WRITE_UNLOCK(lk)					\
do {								\
	MUST_BE_WRITE_LOCKED(lk);				\
	clear_bit(smp_processor_id(), &(lk)->write_locked_map);	\
	write_unlock_bh(&(lk)->l);				\
} while(0)

#else
#define DECLARE_LOCK(l) spinlock_t l = SPIN_LOCK_UNLOCKED
#define DECLARE_LOCK_EXTERN(l) extern spinlock_t l
#define DECLARE_RWLOCK(l) rwlock_t l = RW_LOCK_UNLOCKED
#define DECLARE_RWLOCK_EXTERN(l) extern rwlock_t l

#define MUST_BE_LOCKED(l)
#define MUST_BE_UNLOCKED(l)
#define MUST_BE_READ_LOCKED(l)
#define MUST_BE_WRITE_LOCKED(l)
#define MUST_BE_READ_WRITE_UNLOCKED(l)

#define LOCK_BH(l) spin_lock_bh(l)
#define UNLOCK_BH(l) spin_unlock_bh(l)

#define READ_LOCK(l) read_lock_bh(l)
#define WRITE_LOCK(l) write_lock_bh(l)
#define READ_UNLOCK(l) read_unlock_bh(l)
#define WRITE_UNLOCK(l) write_unlock_bh(l)
#endif /*CONFIG_NETFILTER_DEBUG*/

#endif /* _LOCKHELP_H */
