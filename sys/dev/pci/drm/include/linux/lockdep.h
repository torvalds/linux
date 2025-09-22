/* Public domain. */

#ifndef _LINUX_LOCKDEP_H
#define _LINUX_LOCKDEP_H

#include <linux/smp.h>

struct lock_class_key {
};

struct pin_cookie {
};

#define might_lock(lock)
#define might_lock_nested(lock, subc)
#define lockdep_assert(c)		do {} while(0)
#define lockdep_assert_held(lock)	do { (void)(lock); } while(0)
#define lockdep_assert_held_once(lock)	do { (void)(lock); } while(0)
#define lockdep_assert_once(lock)	do { (void)(lock); } while(0)
#define lockdep_assert_not_held(lock)	do { (void)(lock); } while(0)
#define lockdep_assert_none_held_once()	do {} while(0)
#define lock_acquire(lock, a, b, c, d, e, f)
#define lock_release(lock, a)
#define lock_acquire_shared_recursive(lock, a, b, c, d)
#define lockdep_set_subclass(a, b)
#define lockdep_unpin_lock(a, b)
#define lockdep_set_class(a, b)
#define lockdep_init_map(a, b, c, d)
#define lockdep_set_class_and_name(a, b, c)
#define lockdep_is_held(lock)		0

#define mutex_acquire(a, b, c, d)
#define mutex_release(a, b)

#define SINGLE_DEPTH_NESTING		0

#define lockdep_pin_lock(lock)		\
({					\
	struct pin_cookie pc = {};	\
	pc;				\
})

#endif
