/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * time function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_TIME_H
#define _NOLIBC_TIME_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"

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

#endif /* _NOLIBC_TIME_H */
