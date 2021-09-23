/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FUTEX_H
#define _FUTEX_H

#include <asm/futex.h>

/*
 * Futex flags used to encode options to functions and preserve them across
 * restarts.
 */
#ifdef CONFIG_MMU
# define FLAGS_SHARED		0x01
#else
/*
 * NOMMU does not have per process address space. Let the compiler optimize
 * code away.
 */
# define FLAGS_SHARED		0x00
#endif
#define FLAGS_CLOCKRT		0x02
#define FLAGS_HAS_TIMEOUT	0x04

#ifdef CONFIG_HAVE_FUTEX_CMPXCHG
#define futex_cmpxchg_enabled 1
#else
extern int  __read_mostly futex_cmpxchg_enabled;
#endif

#ifdef CONFIG_FAIL_FUTEX
extern bool should_fail_futex(bool fshared);
#else
static inline bool should_fail_futex(bool fshared)
{
	return false;
}
#endif

extern int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags, u32
				 val, ktime_t *abs_time, u32 bitset, u32 __user
				 *uaddr2);

extern int futex_requeue(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_requeue,
			 u32 *cmpval, int requeue_pi);

extern int futex_wait(u32 __user *uaddr, unsigned int flags, u32 val,
		      ktime_t *abs_time, u32 bitset);

extern int futex_wake(u32 __user *uaddr, unsigned int flags, int nr_wake, u32 bitset);

extern int futex_wake_op(u32 __user *uaddr1, unsigned int flags,
			 u32 __user *uaddr2, int nr_wake, int nr_wake2, int op);

extern int futex_unlock_pi(u32 __user *uaddr, unsigned int flags);

extern int futex_lock_pi(u32 __user *uaddr, unsigned int flags, ktime_t *time, int trylock);

#endif /* _FUTEX_H */
