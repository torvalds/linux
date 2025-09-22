/*	$OpenBSD: w_clock_gettime.c,v 1.1 2020/07/06 13:33:06 pirofti Exp $ */
/*
 * Copyright (c) 2020 Paul Irofti <paul@irofti.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/timetc.h>

#include <time.h>

int
WRAP(clock_gettime)(clockid_t clock_id, struct timespec *tp)
{
	int rc = 0;
	struct timekeep *timekeep = _timekeep;

	if (timekeep == NULL || timekeep->tk_user == 0)
		return clock_gettime(clock_id, tp);

	switch (clock_id) {
	case CLOCK_REALTIME:
		rc = _nanotime(tp, timekeep);
		break;
	case CLOCK_UPTIME:
		rc = _nanoruntime(tp, timekeep);
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_BOOTTIME:
		rc = _nanouptime(tp, timekeep);
		break;
	default:
		return clock_gettime(clock_id, tp);
	}

	if (rc)
		return clock_gettime(clock_id, tp);

	return 0;
}
DEF_WRAP(clock_gettime);
