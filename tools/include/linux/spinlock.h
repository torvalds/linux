/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPINLOCK_H_
#define __LINUX_SPINLOCK_H_

#include <pthread.h>
#include <stdbool.h>

#define spinlock_t		pthread_mutex_t
#define DEFINE_SPINLOCK(x)	pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER
#define __SPIN_LOCK_UNLOCKED(x)	(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER
#define spin_lock_init(x)	pthread_mutex_init(x, NULL)

#define spin_lock(x)			pthread_mutex_lock(x)
#define spin_lock_nested(x, subclass)	pthread_mutex_lock(x)
#define spin_unlock(x)			pthread_mutex_unlock(x)
#define spin_lock_bh(x)			pthread_mutex_lock(x)
#define spin_unlock_bh(x)		pthread_mutex_unlock(x)
#define spin_lock_irq(x)		pthread_mutex_lock(x)
#define spin_unlock_irq(x)		pthread_mutex_unlock(x)
#define spin_lock_irqsave(x, f)		(void)f, pthread_mutex_lock(x)
#define spin_unlock_irqrestore(x, f)	(void)f, pthread_mutex_unlock(x)

#define arch_spinlock_t pthread_mutex_t
#define __ARCH_SPIN_LOCK_UNLOCKED PTHREAD_MUTEX_INITIALIZER

static inline void arch_spin_lock(arch_spinlock_t *mutex)
{
	pthread_mutex_lock(mutex);
}

static inline void arch_spin_unlock(arch_spinlock_t *mutex)
{
	pthread_mutex_unlock(mutex);
}

static inline bool arch_spin_is_locked(arch_spinlock_t *mutex)
{
	return true;
}

#endif
