/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Glibc independent futex library for testing kernel functionality.
 *
 * AUTHOR
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2009-Nov-6: Initial version by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#ifndef _FUTEXTEST_H
#define _FUTEXTEST_H

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/futex.h>

typedef volatile u_int32_t futex_t;
#define FUTEX_INITIALIZER 0

/* Define the newer op codes if the system header file is not up to date. */
#ifndef FUTEX_WAIT_BITSET
#define FUTEX_WAIT_BITSET		9
#endif
#ifndef FUTEX_WAKE_BITSET
#define FUTEX_WAKE_BITSET		10
#endif
#ifndef FUTEX_WAIT_REQUEUE_PI
#define FUTEX_WAIT_REQUEUE_PI		11
#endif
#ifndef FUTEX_CMP_REQUEUE_PI
#define FUTEX_CMP_REQUEUE_PI		12
#endif
#ifndef FUTEX_WAIT_REQUEUE_PI_PRIVATE
#define FUTEX_WAIT_REQUEUE_PI_PRIVATE	(FUTEX_WAIT_REQUEUE_PI | \
					 FUTEX_PRIVATE_FLAG)
#endif
#ifndef FUTEX_REQUEUE_PI_PRIVATE
#define FUTEX_CMP_REQUEUE_PI_PRIVATE	(FUTEX_CMP_REQUEUE_PI | \
					 FUTEX_PRIVATE_FLAG)
#endif

/*
 * SYS_futex is expected from system C library, in glibc some 32-bit
 * architectures (e.g. RV32) are using 64-bit time_t, therefore it doesn't have
 * SYS_futex defined but just SYS_futex_time64. Define SYS_futex as
 * SYS_futex_time64 in this situation to ensure the compilation and the
 * compatibility.
 */
#if !defined(SYS_futex) && defined(SYS_futex_time64)
#define SYS_futex SYS_futex_time64
#endif

/*
 * On 32bit systems if we use "-D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64" or if
 * we are using a newer compiler then the size of the timestamps will be 64bit,
 * however, the SYS_futex will still point to the 32bit futex system call.
 */
#if __SIZEOF_POINTER__ == 4 && defined(SYS_futex_time64) && \
	defined(_TIME_BITS) && _TIME_BITS == 64
# undef SYS_futex
# define SYS_futex SYS_futex_time64
#endif

/**
 * futex() - SYS_futex syscall wrapper
 * @uaddr:	address of first futex
 * @op:		futex op code
 * @val:	typically expected value of uaddr, but varies by op
 * @timeout:	typically an absolute struct timespec (except where noted
 *              otherwise). Overloaded by some ops
 * @uaddr2:	address of second futex for some ops\
 * @val3:	varies by op
 * @opflags:	flags to be bitwise OR'd with op, such as FUTEX_PRIVATE_FLAG
 *
 * futex() is used by all the following futex op wrappers. It can also be
 * used for misuse and abuse testing. Generally, the specific op wrappers
 * should be used instead. It is a macro instead of an static inline function as
 * some of the types over overloaded (timeout is used for nr_requeue for
 * example).
 *
 * These argument descriptions are the defaults for all
 * like-named arguments in the following wrappers except where noted below.
 */
#define futex(uaddr, op, val, timeout, uaddr2, val3, opflags) \
	syscall(SYS_futex, uaddr, op | opflags, val, timeout, uaddr2, val3)

/**
 * futex_wait() - block on uaddr with optional timeout
 * @timeout:	relative timeout
 */
static inline int
futex_wait(futex_t *uaddr, futex_t val, struct timespec *timeout, int opflags)
{
	return futex(uaddr, FUTEX_WAIT, val, timeout, NULL, 0, opflags);
}

/**
 * futex_wake() - wake one or more tasks blocked on uaddr
 * @nr_wake:	wake up to this many tasks
 */
static inline int
futex_wake(futex_t *uaddr, int nr_wake, int opflags)
{
	return futex(uaddr, FUTEX_WAKE, nr_wake, NULL, NULL, 0, opflags);
}

/**
 * futex_wait_bitset() - block on uaddr with bitset
 * @bitset:	bitset to be used with futex_wake_bitset
 */
static inline int
futex_wait_bitset(futex_t *uaddr, futex_t val, struct timespec *timeout,
		  u_int32_t bitset, int opflags)
{
	return futex(uaddr, FUTEX_WAIT_BITSET, val, timeout, NULL, bitset,
		     opflags);
}

/**
 * futex_wake_bitset() - wake one or more tasks blocked on uaddr with bitset
 * @bitset:	bitset to compare with that used in futex_wait_bitset
 */
static inline int
futex_wake_bitset(futex_t *uaddr, int nr_wake, u_int32_t bitset, int opflags)
{
	return futex(uaddr, FUTEX_WAKE_BITSET, nr_wake, NULL, NULL, bitset,
		     opflags);
}

/**
 * futex_lock_pi() - block on uaddr as a PI mutex
 * @detect:	whether (1) or not (0) to perform deadlock detection
 */
static inline int
futex_lock_pi(futex_t *uaddr, struct timespec *timeout, int detect,
	      int opflags)
{
	return futex(uaddr, FUTEX_LOCK_PI, detect, timeout, NULL, 0, opflags);
}

/**
 * futex_unlock_pi() - release uaddr as a PI mutex, waking the top waiter
 */
static inline int
futex_unlock_pi(futex_t *uaddr, int opflags)
{
	return futex(uaddr, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0, opflags);
}

/**
 * futex_wake_op() - FIXME: COME UP WITH A GOOD ONE LINE DESCRIPTION
 */
static inline int
futex_wake_op(futex_t *uaddr, futex_t *uaddr2, int nr_wake, int nr_wake2,
	      int wake_op, int opflags)
{
	return futex(uaddr, FUTEX_WAKE_OP, nr_wake, nr_wake2, uaddr2, wake_op,
		     opflags);
}

/**
 * futex_requeue() - requeue without expected value comparison, deprecated
 * @nr_wake:	wake up to this many tasks
 * @nr_requeue:	requeue up to this many tasks
 *
 * Due to its inherently racy implementation, futex_requeue() is deprecated in
 * favor of futex_cmp_requeue().
 */
static inline int
futex_requeue(futex_t *uaddr, futex_t *uaddr2, int nr_wake, int nr_requeue,
	      int opflags)
{
	return futex(uaddr, FUTEX_REQUEUE, nr_wake, nr_requeue, uaddr2, 0,
		     opflags);
}

/**
 * futex_cmp_requeue() - requeue tasks from uaddr to uaddr2
 * @nr_wake:	wake up to this many tasks
 * @nr_requeue:	requeue up to this many tasks
 */
static inline int
futex_cmp_requeue(futex_t *uaddr, futex_t val, futex_t *uaddr2, int nr_wake,
		  int nr_requeue, int opflags)
{
	return futex(uaddr, FUTEX_CMP_REQUEUE, nr_wake, nr_requeue, uaddr2,
		     val, opflags);
}

/**
 * futex_wait_requeue_pi() - block on uaddr and prepare to requeue to uaddr2
 * @uaddr:	non-PI futex source
 * @uaddr2:	PI futex target
 *
 * This is the first half of the requeue_pi mechanism. It shall always be
 * paired with futex_cmp_requeue_pi().
 */
static inline int
futex_wait_requeue_pi(futex_t *uaddr, futex_t val, futex_t *uaddr2,
		      struct timespec *timeout, int opflags)
{
	return futex(uaddr, FUTEX_WAIT_REQUEUE_PI, val, timeout, uaddr2, 0,
		     opflags);
}

/**
 * futex_cmp_requeue_pi() - requeue tasks from uaddr to uaddr2 (PI aware)
 * @uaddr:	non-PI futex source
 * @uaddr2:	PI futex target
 * @nr_wake:	wake up to this many tasks
 * @nr_requeue:	requeue up to this many tasks
 */
static inline int
futex_cmp_requeue_pi(futex_t *uaddr, futex_t val, futex_t *uaddr2, int nr_wake,
		     int nr_requeue, int opflags)
{
	return futex(uaddr, FUTEX_CMP_REQUEUE_PI, nr_wake, nr_requeue, uaddr2,
		     val, opflags);
}

/**
 * futex_cmpxchg() - atomic compare and exchange
 * @uaddr:	The address of the futex to be modified
 * @oldval:	The expected value of the futex
 * @newval:	The new value to try and assign the futex
 *
 * Implement cmpxchg using gcc atomic builtins.
 * http://gcc.gnu.org/onlinedocs/gcc-4.1.0/gcc/Atomic-Builtins.html
 *
 * Return the old futex value.
 */
static inline u_int32_t
futex_cmpxchg(futex_t *uaddr, u_int32_t oldval, u_int32_t newval)
{
	return __sync_val_compare_and_swap(uaddr, oldval, newval);
}

/**
 * futex_dec() - atomic decrement of the futex value
 * @uaddr:	The address of the futex to be modified
 *
 * Return the new futex value.
 */
static inline u_int32_t
futex_dec(futex_t *uaddr)
{
	return __sync_sub_and_fetch(uaddr, 1);
}

/**
 * futex_inc() - atomic increment of the futex value
 * @uaddr:	the address of the futex to be modified
 *
 * Return the new futex value.
 */
static inline u_int32_t
futex_inc(futex_t *uaddr)
{
	return __sync_add_and_fetch(uaddr, 1);
}

/**
 * futex_set() - atomic decrement of the futex value
 * @uaddr:	the address of the futex to be modified
 * @newval:	New value for the atomic_t
 *
 * Return the new futex value.
 */
static inline u_int32_t
futex_set(futex_t *uaddr, u_int32_t newval)
{
	*uaddr = newval;
	return newval;
}

#endif
