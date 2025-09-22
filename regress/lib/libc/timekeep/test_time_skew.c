/*	$OpenBSD: test_time_skew.c,v 1.1 2020/07/06 13:33:06 pirofti Exp $ */
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

#include <sys/time.h>

#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_NE(a, b) assert((a) != (b))

void
check()
{
         struct timespec tp1, tp2, tout;

         tout.tv_sec = 0;
         tout.tv_nsec = 100000;

         ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &tp1));

         nanosleep(&tout, NULL);

         ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &tp2));

         /* tp1 should never be larger than tp2 */
         ASSERT_NE(1, timespeccmp(&tp1, &tp2, >));
}

int
main(void)
{
	int i;

	for (i = 0; i < 1000; i++)
		check();

	return 0;
}
