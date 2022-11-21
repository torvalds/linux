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

#endif /* __TOOLS_LINUX_ATOMIC_H */
