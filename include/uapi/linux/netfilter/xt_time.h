/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_TIME_H
#define _XT_TIME_H 1

#include <linux/types.h>

struct xt_time_info {
	__u32 date_start;
	__u32 date_stop;
	__u32 daytime_start;
	__u32 daytime_stop;
	__u32 monthdays_match;
	__u8 weekdays_match;
	__u8 flags;
};

enum {
	/* Match against local time (instead of UTC) */
	XT_TIME_LOCAL_TZ = 1 << 0,

	/* treat timestart > timestop (e.g. 23:00-01:00) as single period */
	XT_TIME_CONTIGUOUS = 1 << 1,

	/* Shortcuts */
	XT_TIME_ALL_MONTHDAYS = 0xFFFFFFFE,
	XT_TIME_ALL_WEEKDAYS  = 0xFE,
	XT_TIME_MIN_DAYTIME   = 0,
	XT_TIME_MAX_DAYTIME   = 24 * 60 * 60 - 1,
};

#define XT_TIME_ALL_FLAGS (XT_TIME_LOCAL_TZ|XT_TIME_CONTIGUOUS)

#endif /* _XT_TIME_H */
