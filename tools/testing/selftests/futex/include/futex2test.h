/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Futex2 library addons for futex tests
 *
 * Copyright 2021 Collabora Ltd.
 */
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

#ifndef FUTEX2_SIZE_U32
#define FUTEX2_SIZE_U32 0x02
#endif

#ifndef FUTEX_32
#define FUTEX_32 FUTEX2_SIZE_U32
#endif

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
	return syscall(__NR_futex_waitv, waiters, nr_waiters, flags, timo, clockid);
}
