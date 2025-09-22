/*	$OpenBSD: monotime.h,v 1.1 2025/02/20 19:47:31 claudio Exp $ */

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

/*
 * bgpd uses an internal microsecond time format.
 * To reduce errors this is wrapped into a struct and comes
 * with a bunch of helper functions.
 */
#define MONOTIME_RES	(1000 * 1000LL)

typedef struct monotime {
	long long	monotime;
} monotime_t;

monotime_t	getmonotime(void);
time_t		monotime_to_time(monotime_t);
monotime_t	time_to_monotime(time_t);

static inline monotime_t
monotime_clear(void)
{
	monotime_t mt = { 0 };
	return mt;
}

static inline int
monotime_valid(monotime_t mt)
{
	return mt.monotime > 0;
}

static inline int
monotime_cmp(monotime_t a, monotime_t b)
{
	if (a.monotime > b.monotime)
		return 1;
	if (a.monotime < b.monotime)
		return -1;
	return 0;
}

static inline monotime_t
monotime_add(monotime_t add1, monotime_t add2)
{
	monotime_t sum;
	sum.monotime = add1.monotime + add2.monotime;
	return sum;
}

static inline monotime_t
monotime_sub(monotime_t minu, monotime_t subt)
{
	monotime_t dif;
	dif.monotime = minu.monotime - subt.monotime;
	return dif;
}

static inline long long
monotime_to_msec(monotime_t mt)
{
	return mt.monotime / (MONOTIME_RES / 1000);
}

static inline long long
monotime_to_sec(monotime_t mt)
{
	return mt.monotime / MONOTIME_RES;
}

static inline monotime_t
monotime_from_sec(time_t sec)
{
	monotime_t mt;
	mt.monotime = sec;
	mt.monotime *= MONOTIME_RES;
	return mt;
}
