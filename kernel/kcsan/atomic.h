/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _KERNEL_KCSAN_ATOMIC_H
#define _KERNEL_KCSAN_ATOMIC_H

#include <linux/jiffies.h>
#include <linux/sched.h>

/*
 * Special rules for certain memory where concurrent conflicting accesses are
 * common, however, the current convention is to not mark them; returns true if
 * access to @ptr should be considered atomic. Called from slow-path.
 */
static bool kcsan_is_atomic_special(const volatile void *ptr)
{
	/* volatile globals that have been observed in data races. */
	return ptr == &jiffies || ptr == &current->state;
}

#endif /* _KERNEL_KCSAN_ATOMIC_H */
