/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Futex2 library addons for futex tests
 *
 * Copyright 2021 Collabora Ltd.
 */
#include <linux/time_types.h>
#include <stdint.h>

#define u64_to_ptr(x) ((void *)(uintptr_t)(x))

#ifndef __NR_futex_waitv
#define __NR_futex_waitv 449
struct futex_waitv {
	__u64 val;
	__u64 uaddr;
	__u32 flags;
	__u32 __reserved;
};
#endif

#ifndef __NR_futex_wake
#define __NR_futex_wake 454
#endif

#ifndef __NR_futex_wait
#define __NR_futex_wait 455
#endif

#ifndef FUTEX2_SIZE_U32
#define FUTEX2_SIZE_U32 0x02
#endif

#ifndef FUTEX2_NUMA
#define FUTEX2_NUMA 0x04
#endif

#ifndef FUTEX2_MPOL
#define FUTEX2_MPOL 0x08
#endif

#ifndef FUTEX2_PRIVATE
#define FUTEX2_PRIVATE FUTEX_PRIVATE_FLAG
#endif

#ifndef FUTEX2_NO_NODE
#define FUTEX_NO_NODE (-1)
#endif

#ifndef FUTEX_32
#define FUTEX_32 FUTEX2_SIZE_U32
#endif

struct futex32_numa {
	futex_t futex;
	futex_t numa;
};

/**
 * futex_waitv - Wait at multiple futexes, wake on any
 * @waiters:    Array of waiters
 * @nr_waiters: Length of waiters array
 * @flags: Operation flags
 * @timo:  Optional timeout for operation
 */
static inline int futex_waitv(volatile struct futex_waitv *waiters, unsigned long nr_waiters,
			      unsigned long flags, struct timespec *timo, clockid_t clockid)
{
		struct __kernel_timespec ts = {
			.tv_sec = timo->tv_sec,
			.tv_nsec = timo->tv_nsec,
		};

		return syscall(__NR_futex_waitv, waiters, nr_waiters, flags, &ts, clockid);
}

/*
 * futex_wait() - block on uaddr with optional timeout
 * @val:	Expected value
 * @flags:	FUTEX2 flags
 * @timeout:	Relative timeout
 * @clockid:	Clock id for the timeout
 */
static inline int futex2_wait(void *uaddr, long val, unsigned int flags,
			      struct timespec *timeout, clockid_t clockid)
{
	return syscall(__NR_futex_wait, uaddr, val, ~0U, flags, timeout, clockid);
}

/*
 * futex2_wake() - Wake a number of futexes
 * @nr:		Number of threads to wake at most
 * @flags:	FUTEX2 flags
 */
static inline int futex2_wake(void *uaddr, int nr, unsigned int flags)
{
	return syscall(__NR_futex_wake, uaddr, ~0U, nr, flags);
}
