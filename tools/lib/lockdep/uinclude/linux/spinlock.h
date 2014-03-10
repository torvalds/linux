#ifndef _LIBLOCKDEP_SPINLOCK_H_
#define _LIBLOCKDEP_SPINLOCK_H_

#include <pthread.h>
#include <stdbool.h>

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
