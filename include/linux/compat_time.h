/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPAT_TIME_H
#define _LINUX_COMPAT_TIME_H

#include <linux/types.h>
#include <linux/time64.h>

typedef s32		compat_time_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

extern int compat_get_timespec64(struct timespec64 *, const void __user *);
extern int compat_put_timespec64(const struct timespec64 *, void __user *);

#endif /* _LINUX_COMPAT_TIME_H */
