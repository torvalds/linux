/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * timerfd definitions for NOLIBC
 * Copyright (C) 2025 Thomas Weißschuh <thomas.weissschuh@linutronix.de>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_TIMERFD_H
#define _NOLIBC_SYS_TIMERFD_H

#include "../sys.h"
#include "../time.h"

#include <linux/timerfd.h>


static __attribute__((unused))
int sys_timerfd_create(int clockid, int flags)
{
	return my_syscall2(__NR_timerfd_create, clockid, flags);
}

static __attribute__((unused))
int timerfd_create(int clockid, int flags)
{
	return __sysret(sys_timerfd_create(clockid, flags));
}


static __attribute__((unused))
int sys_timerfd_gettime(int fd, struct itimerspec *curr_value)
{
#if defined(__NR_timerfd_gettime)
	return my_syscall2(__NR_timerfd_gettime, fd, curr_value);
#else
	struct __kernel_itimerspec kcurr_value;
	int ret;

	ret = my_syscall2(__NR_timerfd_gettime64, fd, &kcurr_value);
	__nolibc_timespec_kernel_to_user(&kcurr_value.it_interval, &curr_value->it_interval);
	__nolibc_timespec_kernel_to_user(&kcurr_value.it_value, &curr_value->it_value);
	return ret;
#endif
}

static __attribute__((unused))
int timerfd_gettime(int fd, struct itimerspec *curr_value)
{
	return __sysret(sys_timerfd_gettime(fd, curr_value));
}


static __attribute__((unused))
int sys_timerfd_settime(int fd, int flags,
			const struct itimerspec *new_value, struct itimerspec *old_value)
{
#if defined(__NR_timerfd_settime)
	return my_syscall4(__NR_timerfd_settime, fd, flags, new_value, old_value);
#else
	struct __kernel_itimerspec knew_value, kold_value;
	int ret;

	__nolibc_timespec_user_to_kernel(&new_value->it_value, &knew_value.it_value);
	__nolibc_timespec_user_to_kernel(&new_value->it_interval, &knew_value.it_interval);
	ret = my_syscall4(__NR_timerfd_settime64, fd, flags, &knew_value, &kold_value);
	if (old_value) {
		__nolibc_timespec_kernel_to_user(&kold_value.it_interval, &old_value->it_interval);
		__nolibc_timespec_kernel_to_user(&kold_value.it_value, &old_value->it_value);
	}
	return ret;
#endif
}

static __attribute__((unused))
int timerfd_settime(int fd, int flags,
		    const struct itimerspec *new_value, struct itimerspec *old_value)
{
	return __sysret(sys_timerfd_settime(fd, flags, new_value, old_value));
}

#endif /* _NOLIBC_SYS_TIMERFD_H */
