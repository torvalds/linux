#ifndef __LINUX_SPINLOCK_UP_H
#define __LINUX_SPINLOCK_UP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

#include <asm/processor.h>	/* for cpu_relax() */
#include <asm/barrier.h>

/*
 * include/linux/spinlock_up.h - UP-debug version of spinlocks.
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 *
 * In the debug case, 1 means unlocked, 0 means locked. (the values
 * are inverted, to catch initialization bugs)
 *
 * No atomicity anywhere, we are on UP. However, we still need
 * the compiler barriers, because we do not want the compiler to
 * move potentially faulting instructions (notably user accesses)
 * into the locked sequence, resulting in non-atomic execution.
 */

#ifdef CONFIG_DEBUG_SPINLOCK
#define arch_spin_is_locked(x)		((x)->slock == 0)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	lock->slock = 0;
	barrier();
}

static inline void
arch_spin_lock_flags(arch_spinlock_t *lock, unsigned long flags)
{
	local_irq_save(flags);
	lock->slock = 0;
	barrier();
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	char oldval = lock->slock;

	lock->slock = 0;
	barrier();

	return oldval > 0;
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	barrier();
	lock->slock = 1;
}

/*
 * Read-write spinlocks. No debug version.
 */
#define arch_read_lock(lock)		do { barrier(); (void)(lock); } while (0)
#define arch_write_lock(lock)		do { barrier(); (void)(lock); } while (0)
#define arch_read_trylock(lock)	({ barrier(); (void)(lock); 1; })
#define arch_write_trylock(lock)	({ barrier(); (void)(lock); 1; })
#define arch_read_unlock(lock)		do { barrier(); (void)(lock); } while (0)
#define arch_write_unlock(lock)	do { barrier(); (void)(lock); } while (0)

#else /* DEBUG_SPINLOCK */
#define arch_spin_is_locked(lock)	((void)(lock), 0)
/* for sched/core.c and kernel_lock.c: */
# define arch_spin_lock(lock)		do { barrier(); (void)(lock); } while (0)
# define arch_spin_lock_flags(lock, flags)	do { barrier(); (void)(lock); } while (0)
# define arch_spin_unlock(lock)	do { barrier(); (void)(lock); } while (0)
# define arch_spin_trylock(lock)	({ barrier(); (void)(lock); 1; })
#endif /* DEBUG_SPINLOCK */

#define arch_spin_is_contended(lock)	(((void)(lock), 0))

#endif /* __LINUX_SPINLOCK_UP_H */
