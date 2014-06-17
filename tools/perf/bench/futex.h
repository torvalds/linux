/*
 * Glibc independent futex library for testing kernel functionality.
 * Shamelessly stolen from Darren Hart <dvhltc@us.ibm.com>
 *    http://git.kernel.org/cgit/linux/kernel/git/dvhart/futextest.git/
 */

#ifndef _FUTEX_H
#define _FUTEX_H

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/futex.h>

/**
 * futex() - SYS_futex syscall wrapper
 * @uaddr:	address of first futex
 * @op:		futex op code
 * @val:	typically expected value of uaddr, but varies by op
 * @timeout:	typically an absolute struct timespec (except where noted
 *		otherwise). Overloaded by some ops
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
futex_wait(u_int32_t *uaddr, u_int32_t val, struct timespec *timeout, int opflags)
{
	return futex(uaddr, FUTEX_WAIT, val, timeout, NULL, 0, opflags);
}

/**
 * futex_wake() - wake one or more tasks blocked on uaddr
 * @nr_wake:	wake up to this many tasks
 */
static inline int
futex_wake(u_int32_t *uaddr, int nr_wake, int opflags)
{
	return futex(uaddr, FUTEX_WAKE, nr_wake, NULL, NULL, 0, opflags);
}

/**
* futex_cmp_requeue() - requeue tasks from uaddr to uaddr2
* @nr_wake:        wake up to this many tasks
* @nr_requeue:        requeue up to this many tasks
*/
static inline int
futex_cmp_requeue(u_int32_t *uaddr, u_int32_t val, u_int32_t *uaddr2, int nr_wake,
		 int nr_requeue, int opflags)
{
	return futex(uaddr, FUTEX_CMP_REQUEUE, nr_wake, nr_requeue, uaddr2,
		 val, opflags);
}

#endif /* _FUTEX_H */
