/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SECCOMP_TYPES_H
#define _LINUX_SECCOMP_TYPES_H

#include <linux/types.h>

#ifdef CONFIG_SECCOMP

struct seccomp_filter;
/**
 * struct seccomp - the state of a seccomp'ed process
 *
 * @mode:  indicates one of the valid values above for controlled
 *         system calls available to a process.
 * @filter_count: number of seccomp filters
 * @filter: must always point to a valid seccomp-filter or NULL as it is
 *          accessed without locking during system call entry.
 *
 *          @filter must only be accessed from the context of current as there
 *          is no read locking.
 */
struct seccomp {
	int mode;
	atomic_t filter_count;
	struct seccomp_filter *filter;
};

#else

struct seccomp { };
struct seccomp_filter { };

#endif

#endif /* _LINUX_SECCOMP_TYPES_H */
