/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_ATOMIC_H
#define __TOOLS_LINUX_ATOMIC_H

#include <asm/atomic.h>

void atomic_long_set(atomic_long_t *v, long i);

/* atomic_cmpxchg_relaxed */
#ifndef atomic_cmpxchg_relaxed
#define  atomic_cmpxchg_relaxed		atomic_cmpxchg
#define  atomic_cmpxchg_release         atomic_cmpxchg
#endif /* atomic_cmpxchg_relaxed */

static inline bool atomic_try_cmpxchg(atomic_t *ptr, int *oldp, int new)
{
	int ret, old = *oldp;

	ret = atomic_cmpxchg(ptr, old, new);
	if (ret != old)
		*oldp = ret;
	return ret == old;
}

static inline bool atomic_inc_unless_negative(atomic_t *v)
{
	int c = atomic_read(v);

	do {
		if (unlikely(c < 0))
			return false;
	} while (!atomic_try_cmpxchg(v, &c, c + 1));

	return true;
}

#endif /* __TOOLS_LINUX_ATOMIC_H */
