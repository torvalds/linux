/*	$OpenBSD: microtime.c,v 1.2 2022/07/23 22:58:51 cheloha Exp $ */
/*
 * Copyright (c) 2000 Poul-Henning Kamp <phk@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/timetc.h>

#include <time.h>

/*
 * Return the difference between the timehands' counter value now and what
 * was when we copied it to the timehands' offset_count.
 */
static inline int
tc_delta(struct timekeep *tk, u_int *delta)
{
	u_int tc;

	if (_tc_get_timecount(tk, &tc))
		return -1;
	*delta = (tc - tk->tk_offset_count) & tk->tk_counter_mask;
	return 0;
}

static int
binuptime(struct bintime *bt, struct timekeep *tk)
{
	u_int gen, delta;

	do {
		gen = tk->tk_generation;
		membar_consumer();
		if (tc_delta(tk, &delta))
			return -1;
		TIMECOUNT_TO_BINTIME(delta, tk->tk_scale, bt);
		bintimeadd(bt, &tk->tk_offset, bt);
		membar_consumer();
	} while (gen == 0 || gen != tk->tk_generation);

	return 0;
}

static int
binruntime(struct bintime *bt, struct timekeep *tk)
{
	u_int gen, delta;

	do {
		gen = tk->tk_generation;
		membar_consumer();
		if (tc_delta(tk, &delta))
			return -1;
		TIMECOUNT_TO_BINTIME(delta, tk->tk_scale, bt);
		bintimeadd(bt, &tk->tk_offset, bt);
		bintimesub(bt, &tk->tk_naptime, bt);
		membar_consumer();
	} while (gen == 0 || gen != tk->tk_generation);

	return 0;
}

static int
bintime(struct bintime *bt, struct timekeep *tk)
{
	u_int gen, delta;

	do {
		gen = tk->tk_generation;
		membar_consumer();
		if (tc_delta(tk, &delta))
			return -1;
		TIMECOUNT_TO_BINTIME(delta, tk->tk_scale, bt);
		bintimeadd(bt, &tk->tk_offset, bt);
		bintimeadd(bt, &tk->tk_boottime, bt);
		membar_consumer();
	} while (gen == 0 || gen != tk->tk_generation);

	return 0;
}

int
_microtime(struct timeval *tvp, struct timekeep *tk)
{
	struct bintime bt;

	if (bintime(&bt, tk))
		return -1;
	BINTIME_TO_TIMEVAL(&bt, tvp);
	return 0;
}

int
_nanotime(struct timespec *tsp, struct timekeep *tk)
{
	struct bintime bt;

	if (bintime(&bt, tk))
		return -1;
	BINTIME_TO_TIMESPEC(&bt, tsp);
	return 0;
}

int
_nanoruntime(struct timespec *ts, struct timekeep *tk)
{
	struct bintime bt;

	if (binruntime(&bt, tk))
		return -1;
	BINTIME_TO_TIMESPEC(&bt, ts);
	return 0;
}


int
_nanouptime(struct timespec *tsp, struct timekeep *tk)
{
	struct bintime bt;

	if (binuptime(&bt, tk))
		return -1;
	BINTIME_TO_TIMESPEC(&bt, tsp);
	return 0;
}
