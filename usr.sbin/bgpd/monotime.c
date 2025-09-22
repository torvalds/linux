/*	$OpenBSD: monotime.c,v 1.1 2025/02/20 19:47:31 claudio Exp $ */

/*
 * Copyright (c) 2025 Claudio Jeker <claudio@openbsd.org>
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
#include <time.h>

#include "monotime.h"

static inline monotime_t
monotime_from_ts(struct timespec *ts)
{
	monotime_t mt;

	mt = monotime_from_sec(ts->tv_sec);
	mt.monotime += ts->tv_nsec / (1000 * 1000 * 1000LL / MONOTIME_RES);
	return mt;
}

monotime_t
getmonotime(void)
{
	struct timespec	ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return monotime_clear();
	return monotime_from_ts(&ts);
}

time_t
monotime_to_time(monotime_t mt)
{
	mt = monotime_sub(getmonotime(), mt);
	return time(NULL) - monotime_to_sec(mt);
}

monotime_t
time_to_monotime(time_t t)
{
	time_t now = time(NULL);

	if (now < t)
		return monotime_clear();
	t = now - t;
	return monotime_sub(getmonotime(), monotime_from_sec(t));
}
