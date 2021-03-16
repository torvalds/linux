// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   fs/ksmbd/netmisc.c
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Error mapping routines from Samba libsmb/errormap.c
 *   Copyright (C) Andrew Tridgell 2001
 */

#include "glob.h"
#include "smberr.h"
#include "nterr.h"
#include "time_wrappers.h"

/*
 * Convert the NT UTC (based 1601-01-01, in hundred nanosecond units)
 * into Unix UTC (based 1970-01-01, in seconds).
 */
struct timespec64 ksmbd_NTtimeToUnix(__le64 ntutc)
{
	struct timespec64 ts;

	/* Subtract the NTFS time offset, then convert to 1s intervals. */
	s64 t = le64_to_cpu(ntutc) - NTFS_TIME_OFFSET;
	u64 abs_t;

	/*
	 * Unfortunately can not use normal 64 bit division on 32 bit arch, but
	 * the alternative, do_div, does not work with negative numbers so have
	 * to special case them
	 */
	if (t < 0) {
		abs_t = -t;
		ts.tv_nsec = do_div(abs_t, 10000000) * 100;
		ts.tv_nsec = -ts.tv_nsec;
		ts.tv_sec = -abs_t;
	} else {
		abs_t = t;
		ts.tv_nsec = do_div(abs_t, 10000000) * 100;
		ts.tv_sec = abs_t;
	}

	return ts;
}
