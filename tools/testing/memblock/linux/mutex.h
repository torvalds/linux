/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MUTEX_H
#define _MUTEX_H

#define DEFINE_MUTEX(name) int name

static inline void dummy_mutex_guard(int *name)
{
}

#define guard(mutex)	\
	dummy_##mutex##_guard

#endif /* _MUTEX_H */