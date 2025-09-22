/*	$OpenBSD: test_clock_gettime.c,v 1.1 2020/07/06 13:33:06 pirofti Exp $ */
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

#include <assert.h>
#include <time.h>

#define ASSERT_EQ(a, b) assert((a) == (b))

void
check()
{
	struct timespec tp = {0};

	ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &tp));
	ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &tp));
	ASSERT_EQ(0, clock_gettime(CLOCK_BOOTTIME, &tp));
	ASSERT_EQ(0, clock_gettime(CLOCK_UPTIME, &tp));


	ASSERT_EQ(0, clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp));
	ASSERT_EQ(0, clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp));

}

int main()
{
	check();
	return 0;
}
