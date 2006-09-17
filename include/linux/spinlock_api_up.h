#ifndef __LINUX_SPINLOCK_API_UP_H
#define __LINUX_SPINLOCK_API_UP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

/*
 * include/linux/spinlock_api_up.h
 *
 * spinlock API implementation on UP-nondebug (inlined implementation)
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

#define in_lock_functions(ADDR)		0

#define assert_spin_locked(lock)	do { (void)(lock); } while (0)

/*
 * In the UP-nondebug case there's no real locking going on, so the
 * only thing we have to do is to keep the preempt counts and irq
 * flags straight, to supress compiler warnings of unused lock
 * variables, and to add the proper checker annotations:
 */
#define __LOCK(lock) \
  do { preempt_disable(); __acquire(lock); (void)(lock); } while (0)

#define __LOCK_BH(lock) \
  do { local_bh_disable(); __LOCK(lock); } while (0)

#define __LOCK_IRQ(lock) \
  do { local_irq_disable(); __LOCK(lock); } while (0)

#define __LOCK_IRQSAVE(lock, flags) \
  do { local_irq_save(flags); __LOCK(lock); } while (0)

#define __UNLOCK(lock) \
  do { preempt_enable(); __release(lock); (void)(lock); } while (0)

#define __UNLOCK_BH(lock) \
  do { preempt_enable_no_resched(); local_bh_enable(); __release(lock); (void)(lock); } while (0)

#define __UNLOCK_IRQ(lock) \
  do { local_irq_enable(); __UNLOCK(lock); } while (0)

#define __UNLOCK_IRQRESTORE(lock, flags) \
  do { local_irq_restore(flags); __UNLOCK(lock); } while (0)

#define _spin_lock(lock)			__LOCK(lock)
#define _spin_lock_nested(lock, subclass)	__LOCK(lock)
#define _read_lock(lock)			__LOCK(lock)
#define _write_lock(lock)			__LOCK(lock)
#define _spin_lock_bh(lock)			__LOCK_BH(lock)
#define _read_lock_bh(lock)			__LOCK_BH(lock)
#define _write_lock_bh(lock)			__LOCK_BH(lock)
#define _spin_lock_irq(lock)			__LOCK_IRQ(lock)
#define _read_lock_irq(lock)			__LOCK_IRQ(lock)
#define _write_lock_irq(lock)			__LOCK_IRQ(lock)
#define _spin_lock_irqsave(lock, flags)		__LOCK_IRQSAVE(lock, flags)
#define _read_lock_irqsave(lock, flags)		__LOCK_IRQSAVE(lock, flags)
#define _write_lock_irqsave(lock, flags)	__LOCK_IRQSAVE(lock, flags)
#define _spin_trylock(lock)			({ __LOCK(lock); 1; })
#define _read_trylock(lock)			({ __LOCK(lock); 1; })
#define _write_trylock(lock)			({ __LOCK(lock); 1; })
#define _spin_trylock_bh(lock)			({ __LOCK_BH(lock); 1; })
#define _spin_unlock(lock)			__UNLOCK(lock)
#define _read_unlock(lock)			__UNLOCK(lock)
#define _write_unlock(lock)			__UNLOCK(lock)
#define _spin_unlock_bh(lock)			__UNLOCK_BH(lock)
#define _write_unlock_bh(lock)			__UNLOCK_BH(lock)
#define _read_unlock_bh(lock)			__UNLOCK_BH(lock)
#define _spin_unlock_irq(lock)			__UNLOCK_IRQ(lock)
#define _read_unlock_irq(lock)			__UNLOCK_IRQ(lock)
#define _write_unlock_irq(lock)			__UNLOCK_IRQ(lock)
#define _spin_unlock_irqrestore(lock, flags)	__UNLOCK_IRQRESTORE(lock, flags)
#define _read_unlock_irqrestore(lock, flags)	__UNLOCK_IRQRESTORE(lock, flags)
#define _write_unlock_irqrestore(lock, flags)	__UNLOCK_IRQRESTORE(lock, flags)

#endif /* __LINUX_SPINLOCK_API_UP_H */
