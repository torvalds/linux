/* Public domain. */

#ifndef _LINUX_MUTEX_H
#define _LINUX_MUTEX_H

#include <sys/stdint.h>
#include <sys/rwlock.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/lockdep.h>

#define DEFINE_MUTEX(x)		struct rwlock x = RWLOCK_INITIALIZER(#x)

#define mutex_lock_interruptible_nested(rwl, subc) \
					mutex_lock_interruptible(rwl)
#define mutex_lock(rwl)			rw_enter_write(rwl)
#define mutex_lock_nest_lock(rwl, sub)	rw_enter_write(rwl)
#define mutex_lock_nested(rwl, sub)	rw_enter_write(rwl)
#define mutex_trylock(rwl)		(rw_enter(rwl, RW_WRITE | RW_NOSLEEP) == 0)
#define mutex_unlock(rwl)		rw_exit_write(rwl)
#define mutex_is_locked(rwl)		(rw_status(rwl) != 0)
#define mutex_destroy(rwl)

static inline int
mutex_lock_interruptible(struct rwlock *rwl)
{
	if (rw_enter(rwl, RW_WRITE | RW_INTR) != 0)
		return -EINTR;
	return 0;
}

enum mutex_trylock_recursive_result {
	MUTEX_TRYLOCK_FAILED,
	MUTEX_TRYLOCK_SUCCESS,
	MUTEX_TRYLOCK_RECURSIVE
};

static inline enum mutex_trylock_recursive_result
mutex_trylock_recursive(struct rwlock *rwl)
{
	if (rw_status(rwl) == RW_WRITE)
		return MUTEX_TRYLOCK_RECURSIVE;
	if (mutex_trylock(rwl))
		return MUTEX_TRYLOCK_SUCCESS;
	return MUTEX_TRYLOCK_FAILED;
}

int atomic_dec_and_mutex_lock(volatile int *, struct rwlock *);

static inline void
mutex_cleanup(struct rwlock **p)
{
	mutex_unlock(*p);
}

#define _guard(rwl) \
	mutex_lock(rwl); \
	struct rwlock *_guard_p __cleanup(mutex_cleanup) = rwl
#define guard(type) _guard

#endif
