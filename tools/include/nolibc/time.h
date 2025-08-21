/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * time function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_TIME_H
#define _NOLIBC_TIME_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"

#include <linux/signal.h>
#include <linux/time.h>

static __inline__
void __nolibc_timespec_user_to_kernel(const struct timespec *ts, struct __kernel_timespec *kts)
{
	kts->tv_sec = ts->tv_sec;
	kts->tv_nsec = ts->tv_nsec;
}

static __inline__
void __nolibc_timespec_kernel_to_user(const struct __kernel_timespec *kts, struct timespec *ts)
{
	ts->tv_sec = kts->tv_sec;
	ts->tv_nsec = kts->tv_nsec;
}

/*
 * int clock_getres(clockid_t clockid, struct timespec *res);
 * int clock_gettime(clockid_t clockid, struct timespec *tp);
 * int clock_settime(clockid_t clockid, const struct timespec *tp);
 * int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *rqtp,
 *                     struct timespec *rmtp)
 */

static __attribute__((unused))
int sys_clock_getres(clockid_t clockid, struct timespec *res)
{
#if defined(__NR_clock_getres)
	return my_syscall2(__NR_clock_getres, clockid, res);
#else
	struct __kernel_timespec kres;
	int ret;

	ret = my_syscall2(__NR_clock_getres_time64, clockid, &kres);
	if (res)
		__nolibc_timespec_kernel_to_user(&kres, res);
	return ret;
#endif
}

static __attribute__((unused))
int clock_getres(clockid_t clockid, struct timespec *res)
{
	return __sysret(sys_clock_getres(clockid, res));
}

static __attribute__((unused))
int sys_clock_gettime(clockid_t clockid, struct timespec *tp)
{
#if defined(__NR_clock_gettime)
	return my_syscall2(__NR_clock_gettime, clockid, tp);
#else
	struct __kernel_timespec ktp;
	int ret;

	ret = my_syscall2(__NR_clock_gettime64, clockid, &ktp);
	if (tp)
		__nolibc_timespec_kernel_to_user(&ktp, tp);
	return ret;
#endif
}

static __attribute__((unused))
int clock_gettime(clockid_t clockid, struct timespec *tp)
{
	return __sysret(sys_clock_gettime(clockid, tp));
}

static __attribute__((unused))
int sys_clock_settime(clockid_t clockid, struct timespec *tp)
{
#if defined(__NR_clock_settime)
	return my_syscall2(__NR_clock_settime, clockid, tp);
#elif defined(__NR_clock_settime64)
	struct __kernel_timespec ktp;

	__nolibc_timespec_user_to_kernel(tp, &ktp);
	return my_syscall2(__NR_clock_settime64, clockid, &ktp);
#else
	return __nolibc_enosys(__func__, clockid, tp);
#endif
}

static __attribute__((unused))
int clock_settime(clockid_t clockid, struct timespec *tp)
{
	return __sysret(sys_clock_settime(clockid, tp));
}

static __attribute__((unused))
int sys_clock_nanosleep(clockid_t clockid, int flags, const struct timespec *rqtp,
			struct timespec *rmtp)
{
#if defined(__NR_clock_nanosleep)
	return my_syscall4(__NR_clock_nanosleep, clockid, flags, rqtp, rmtp);
#elif defined(__NR_clock_nanosleep_time64)
	struct __kernel_timespec krqtp, krmtp;
	int ret;

	__nolibc_timespec_user_to_kernel(rqtp, &krqtp);
	ret = my_syscall4(__NR_clock_nanosleep_time64, clockid, flags, &krqtp, &krmtp);
	if (rmtp)
		__nolibc_timespec_kernel_to_user(&krmtp, rmtp);
	return ret;
#else
	return __nolibc_enosys(__func__, clockid, flags, rqtp, rmtp);
#endif
}

static __attribute__((unused))
int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *rqtp,
		    struct timespec *rmtp)
{
	/* Directly return a positive error number */
	return -sys_clock_nanosleep(clockid, flags, rqtp, rmtp);
}

static __inline__
double difftime(time_t time1, time_t time2)
{
	return time1 - time2;
}

static __inline__
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
	return __sysret(sys_clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp));
}


static __attribute__((unused))
time_t time(time_t *tptr)
{
	struct timeval tv;

	/* note, cannot fail here */
	sys_gettimeofday(&tv, NULL);

	if (tptr)
		*tptr = tv.tv_sec;
	return tv.tv_sec;
}


/*
 * int timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid);
 * int timer_gettime(timer_t timerid, struct itimerspec *curr_value);
 * int timer_settime(timer_t timerid, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
 */

static __attribute__((unused))
int sys_timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid)
{
	return my_syscall3(__NR_timer_create, clockid, evp, timerid);
}

static __attribute__((unused))
int timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid)
{
	return __sysret(sys_timer_create(clockid, evp, timerid));
}

static __attribute__((unused))
int sys_timer_delete(timer_t timerid)
{
	return my_syscall1(__NR_timer_delete, timerid);
}

static __attribute__((unused))
int timer_delete(timer_t timerid)
{
	return __sysret(sys_timer_delete(timerid));
}

static __attribute__((unused))
int sys_timer_gettime(timer_t timerid, struct itimerspec *curr_value)
{
#if defined(__NR_timer_gettime)
	return my_syscall2(__NR_timer_gettime, timerid, curr_value);
#elif defined(__NR_timer_gettime64)
	struct __kernel_itimerspec kcurr_value;
	int ret;

	ret = my_syscall2(__NR_timer_gettime64, timerid, &kcurr_value);
	__nolibc_timespec_kernel_to_user(&kcurr_value.it_interval, &curr_value->it_interval);
	__nolibc_timespec_kernel_to_user(&kcurr_value.it_value, &curr_value->it_value);
	return ret;
#else
	return __nolibc_enosys(__func__, timerid, curr_value);
#endif
}

static __attribute__((unused))
int timer_gettime(timer_t timerid, struct itimerspec *curr_value)
{
	return __sysret(sys_timer_gettime(timerid, curr_value));
}

static __attribute__((unused))
int sys_timer_settime(timer_t timerid, int flags,
		      const struct itimerspec *new_value, struct itimerspec *old_value)
{
#if defined(__NR_timer_settime)
	return my_syscall4(__NR_timer_settime, timerid, flags, new_value, old_value);
#elif defined(__NR_timer_settime64)
	struct __kernel_itimerspec knew_value, kold_value;
	int ret;

	__nolibc_timespec_user_to_kernel(&new_value->it_value, &knew_value.it_value);
	__nolibc_timespec_user_to_kernel(&new_value->it_interval, &knew_value.it_interval);
	ret = my_syscall4(__NR_timer_settime64, timerid, flags, &knew_value, &kold_value);
	if (old_value) {
		__nolibc_timespec_kernel_to_user(&kold_value.it_interval, &old_value->it_interval);
		__nolibc_timespec_kernel_to_user(&kold_value.it_value, &old_value->it_value);
	}
	return ret;
#else
	return __nolibc_enosys(__func__, timerid, flags, new_value, old_value);
#endif
}

static __attribute__((unused))
int timer_settime(timer_t timerid, int flags,
		  const struct itimerspec *new_value, struct itimerspec *old_value)
{
	return __sysret(sys_timer_settime(timerid, flags, new_value, old_value));
}

#endif /* _NOLIBC_TIME_H */
