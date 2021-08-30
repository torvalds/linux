#ifndef SPINLOCK_H_STUB
#define SPINLOCK_H_STUB

#include <pthread.h>

typedef pthread_spinlock_t  spinlock_t;

static inline void spin_lock_init(spinlock_t *lock)
{
	int r = pthread_spin_init(lock, 0);
	assert(!r);
}

static inline void spin_lock(spinlock_t *lock)
{
	int ret = pthread_spin_lock(lock);
	assert(!ret);
}

static inline void spin_unlock(spinlock_t *lock)
{
	int ret = pthread_spin_unlock(lock);
	assert(!ret);
}

static inline void spin_lock_bh(spinlock_t *lock)
{
	spin_lock(lock);
}

static inline void spin_unlock_bh(spinlock_t *lock)
{
	spin_unlock(lock);
}

static inline void spin_lock_irq(spinlock_t *lock)
{
	spin_lock(lock);
}

static inline void spin_unlock_irq(spinlock_t *lock)
{
	spin_unlock(lock);
}

static inline void spin_lock_irqsave(spinlock_t *lock, unsigned long f)
{
	spin_lock(lock);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long f)
{
	spin_unlock(lock);
}

#endif
