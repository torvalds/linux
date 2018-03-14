/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPAT_TIME_H
#define _LINUX_COMPAT_TIME_H

#include <linux/types.h>

typedef s32		compat_time_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

#endif /* _LINUX_COMPAT_TIME_H */
