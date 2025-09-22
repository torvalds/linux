/* Public domain. */

#ifndef _LINUX_SPINLOCK_H
#define _LINUX_SPINLOCK_H

#include <linux/spinlock_types.h>
#include <linux/preempt.h>
#include <linux/bottom_half.h>
#include <linux/atomic.h>
#include <linux/lockdep.h>

#define spin_lock_irqsave(_mtxp, _flags) do {			\
		_flags = 0;					\
		mtx_enter(_mtxp);				\
	} while (0)

#define spin_lock_irqsave_nested(_mtxp, _flags, _subclass) do {	\
		(void)(_subclass);				\
		_flags = 0;					\
		mtx_enter(_mtxp);				\
	} while (0)

#define spin_unlock_irqrestore(_mtxp, _flags) do {		\
		(void)(_flags);					\
		mtx_leave(_mtxp);				\
	} while (0)

#define spin_trylock(_mtxp)					\
({								\
	mtx_enter_try(_mtxp) ? 1 : 0;				\
})

#define spin_trylock_irqsave(_mtxp, _flags)			\
({								\
	(void)(_flags);						\
	mtx_enter_try(_mtxp) ? 1 : 0;				\
})

static inline int
atomic_dec_and_lock(volatile int *v, struct mutex *mtxp)
{
	if (*v != 1) {
		atomic_dec(v);
		return 0;
	}

	mtx_enter(mtxp);
	atomic_dec(v);
	return 1;
}

#define atomic_dec_and_lock_irqsave(_a, _mtxp, _flags)		\
	atomic_dec_and_lock(_a, _mtxp)

#define spin_lock(mtxp)			mtx_enter(mtxp)
#define spin_lock_nested(mtxp, l)	mtx_enter(mtxp)
#define spin_unlock(mtxp)		mtx_leave(mtxp)
#define spin_lock_irq(mtxp)		mtx_enter(mtxp)
#define spin_unlock_irq(mtxp)		mtx_leave(mtxp)
#define assert_spin_locked(mtxp)	MUTEX_ASSERT_LOCKED(mtxp)
#define spin_trylock_irq(mtxp)		mtx_enter_try(mtxp)

#define read_lock(mtxp)			mtx_enter(mtxp)
#define read_unlock(mtxp)		mtx_leave(mtxp)
#define write_lock(mtxp)		mtx_enter(mtxp)
#define write_unlock(mtxp)		mtx_leave(mtxp)

#endif
