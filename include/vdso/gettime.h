/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VDSO_GETTIME_H
#define _VDSO_GETTIME_H

#include <linux/types.h>

struct __kernel_timespec;
struct __kernel_old_timeval;
struct timezone;

#if !defined(CONFIG_64BIT) || defined(BUILD_VDSO32_64)
struct old_timespec32;
int __vdso_clock_getres(clockid_t clock, struct old_timespec32 *res);
int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts);
#else
int __vdso_clock_getres(clockid_t clock, struct __kernel_timespec *res);
int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts);
#endif

__kernel_old_time_t __vdso_time(__kernel_old_time_t *t);
int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
int __vdso_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts);

#endif
