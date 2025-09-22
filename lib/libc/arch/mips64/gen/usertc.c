/*	$OpenBSD: usertc.c,v 1.3 2021/07/25 22:58:39 jca Exp $	*/
/*
 * Copyright (c) 2020 Visa Hankala
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

static inline u_int
get_cp0_count(void)
{
	uint32_t count;

	__asm volatile (
	"	.set	push\n"
	"	.set	mips64r2\n"
	"	rdhwr	%0, $2\n"
	"	.set	pop\n"
	: "=r" (count));

	return count;
}

static int
tc_get_timecount(struct timekeep *tk, u_int *tc)
{
	switch (tk->tk_user) {
	case TC_CP0_COUNT:
		*tc = get_cp0_count();
		return 0;
	}

	return -1;
}

int (*const _tc_get_timecount)(struct timekeep *, u_int *) = tc_get_timecount;
