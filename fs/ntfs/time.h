/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * time.h - NTFS time conversion functions.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_TIME_H
#define _LINUX_NTFS_TIME_H

#include <linux/time.h>		/* For current_kernel_time(). */
#include <asm/div64.h>		/* For do_div(). */

#include "endian.h"

#define NTFS_TIME_OFFSET ((s64)(369 * 365 + 89) * 24 * 3600 * 10000000)

/**
 * utc2ntfs - convert Linux UTC time to NTFS time
 * @ts:		Linux UTC time to convert to NTFS time
 *
 * Convert the Linux UTC time @ts to its corresponding NTFS time and return
 * that in little endian format.
 *
 * Linux stores time in a struct timespec64 consisting of a time64_t tv_sec
 * and a long tv_nsec where tv_sec is the number of 1-second intervals since
 * 1st January 1970, 00:00:00 UTC and tv_nsec is the number of 1-nano-second
 * intervals since the value of tv_sec.
 *
 * NTFS uses Microsoft's standard time format which is stored in a s64 and is
 * measured as the number of 100-nano-second intervals since 1st January 1601,
 * 00:00:00 UTC.
 */
static inline sle64 utc2ntfs(const struct timespec64 ts)
{
	/*
	 * Convert the seconds to 100ns intervals, add the nano-seconds
	 * converted to 100ns intervals, and then add the NTFS time offset.
	 */
	return cpu_to_sle64((s64)ts.tv_sec * 10000000 + ts.tv_nsec / 100 +
			NTFS_TIME_OFFSET);
}

/**
 * get_current_ntfs_time - get the current time in little endian NTFS format
 *
 * Get the current time from the Linux kernel, convert it to its corresponding
 * NTFS time and return that in little endian format.
 */
static inline sle64 get_current_ntfs_time(void)
{
	struct timespec64 ts;

	ktime_get_coarse_real_ts64(&ts);
	return utc2ntfs(ts);
}

/**
 * ntfs2utc - convert NTFS time to Linux time
 * @time:	NTFS time (little endian) to convert to Linux UTC
 *
 * Convert the little endian NTFS time @time to its corresponding Linux UTC
 * time and return that in cpu format.
 *
 * Linux stores time in a struct timespec64 consisting of a time64_t tv_sec
 * and a long tv_nsec where tv_sec is the number of 1-second intervals since
 * 1st January 1970, 00:00:00 UTC and tv_nsec is the number of 1-nano-second
 * intervals since the value of tv_sec.
 *
 * NTFS uses Microsoft's standard time format which is stored in a s64 and is
 * measured as the number of 100 nano-second intervals since 1st January 1601,
 * 00:00:00 UTC.
 */
static inline struct timespec64 ntfs2utc(const sle64 time)
{
	struct timespec64 ts;

	/* Subtract the NTFS time offset. */
	u64 t = (u64)(sle64_to_cpu(time) - NTFS_TIME_OFFSET);
	/*
	 * Convert the time to 1-second intervals and the remainder to
	 * 1-nano-second intervals.
	 */
	ts.tv_nsec = do_div(t, 10000000) * 100;
	ts.tv_sec = t;
	return ts;
}

#endif /* _LINUX_NTFS_TIME_H */
