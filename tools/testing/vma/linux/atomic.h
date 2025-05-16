/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#define atomic_t int32_t
#define atomic_inc(x) uatomic_inc(x)
#define atomic_read(x) uatomic_read(x)
#define atomic_set(x, y) uatomic_set(x, y)
#define U8_MAX UCHAR_MAX

#ifndef atomic_cmpxchg_relaxed
#define  atomic_cmpxchg_relaxed		uatomic_cmpxchg
#define  atomic_cmpxchg_release         uatomic_cmpxchg
#endif /* atomic_cmpxchg_relaxed */

#endif	/* _LINUX_ATOMIC_H */
