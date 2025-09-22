/*	$OpenBSD: usertc.c,v 1.3 2023/02/05 13:37:51 kettenis Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/timetc.h>

static inline uint64_t
agtimer_readcnt64_sun50i(void)
{
	uint64_t val;
	int retry;

	__asm volatile("isb" ::: "memory");
	for (retry = 0; retry < 150; retry++) {
		__asm volatile("mrs %x0, CNTVCT_EL0" : "=r" (val));

		if (((val + 1) & 0x1ff) > 1)
			break;
	}

	return val;
}

static inline u_int
agtimer_get_timecount_default(struct timecounter *tc)
{
	uint64_t val;

	/*
	 * No need to work around Cortex-A73 errata 858921 since we
	 * only look at the low 32 bits here.
	 */
	__asm volatile("isb" ::: "memory");
	__asm volatile("mrs %x0, CNTVCT_EL0" : "=r" (val));
	return (val & 0xffffffff);
}

static inline u_int
agtimer_get_timecount_sun50i(struct timecounter *tc)
{
	return agtimer_readcnt64_sun50i();
}

static int
tc_get_timecount(struct timekeep *tk, u_int *tc)
{
	switch (tk->tk_user) {
	case TC_AGTIMER:
		*tc = agtimer_get_timecount_default(NULL);
		return 0;
	case TC_AGTIMER_SUN50I:
		*tc = agtimer_get_timecount_sun50i(NULL);
		return 0;
	}

	return -1;
}

int (*const _tc_get_timecount)(struct timekeep *, u_int *) = tc_get_timecount;
