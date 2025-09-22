/*
 * util/timeval_func.c - helpers to work with struct timeval values.
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains helpers to manipulate struct timeval values.
 */

#include "config.h"
#include "timeval_func.h"

/** subtract timers and the values do not overflow or become negative */
void
timeval_subtract(struct timeval* d, const struct timeval* end, const struct timeval* start)
{
#ifndef S_SPLINT_S
	time_t end_usec = end->tv_usec;
	d->tv_sec = end->tv_sec - start->tv_sec;
	if(end_usec < start->tv_usec) {
		end_usec += 1000000;
		d->tv_sec--;
	}
	d->tv_usec = end_usec - start->tv_usec;
#endif
}

/** add timers and the values do not overflow or become negative */
void
timeval_add(struct timeval* d, const struct timeval* add)
{
#ifndef S_SPLINT_S
	d->tv_sec += add->tv_sec;
	d->tv_usec += add->tv_usec;
	if(d->tv_usec >= 1000000 ) {
		d->tv_usec -= 1000000;
		d->tv_sec++;
	}
#endif
}

/** divide sum of timers to get average */
void
timeval_divide(struct timeval* avg, const struct timeval* sum, long long d)
{
#ifndef S_SPLINT_S
	long long leftover;
	if(d <= 0) {
		avg->tv_sec = 0;
		avg->tv_usec = 0;
		return;
	}
	avg->tv_sec = sum->tv_sec / d;
	avg->tv_usec = sum->tv_usec / d;
	/* handle fraction from seconds divide */
	leftover = sum->tv_sec - avg->tv_sec*d;
	if(leftover <= 0)
		leftover = 0;
	avg->tv_usec += (((long long)leftover)*((long long)1000000))/d;
	if(avg->tv_sec < 0)
		avg->tv_sec = 0;
	if(avg->tv_usec < 0)
		avg->tv_usec = 0;
#endif
}

/** histogram compare of time values */
int
timeval_smaller(const struct timeval* x, const struct timeval* y)
{
#ifndef S_SPLINT_S
	if(x->tv_sec < y->tv_sec)
		return 1;
	else if(x->tv_sec == y->tv_sec) {
		if(x->tv_usec <= y->tv_usec)
			return 1;
		else	return 0;
	}
	else	return 0;
#endif
}
