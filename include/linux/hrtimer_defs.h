/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HRTIMER_DEFS_H
#define _LINUX_HRTIMER_DEFS_H

#include <linux/ktime.h>

#ifdef CONFIG_HIGH_RES_TIMERS

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
# define HIGH_RES_NSEC		1
# define KTIME_HIGH_RES		(HIGH_RES_NSEC)
# define MONOTONIC_RES_NSEC	HIGH_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_HIGH_RES

#else

# define MONOTONIC_RES_NSEC	LOW_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_LOW_RES

#endif

#endif
