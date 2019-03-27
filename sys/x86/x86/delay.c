/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)clock.c	7.2 (Berkeley) 5/12/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Generic x86 routines to handle delay */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timetc.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/sched.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <x86/init.h>

static void
delay_tsc(int n)
{
	uint64_t end, now;

	/*
	 * Pin the current thread ensure correct behavior if the TSCs
	 * on different CPUs are not in sync.
	 */
	sched_pin();
	now = rdtsc();
	end = now + tsc_freq * n / 1000000;
	do {
		cpu_spinwait();
		now = rdtsc();
	} while (now < end);
	sched_unpin();
}

static int
delay_tc(int n)
{
	struct timecounter *tc;
	timecounter_get_t *func;
	uint64_t end, freq, now;
	u_int last, mask, u;

	/*
	 * Only use the TSC if it is P-state invariant.  If the TSC is
	 * not P-state invariant and the CPU is not running at the
	 * "full" P-state, then the TSC will increment at some rate
	 * less than tsc_freq and delay_tsc() will wait too long.
	 */
	if (tsc_is_invariant && tsc_freq != 0) {
		delay_tsc(n);
		return (1);
	}
	tc = timecounter;
	if (tc->tc_quality <= 0)
		return (0);
	func = tc->tc_get_timecount;
	mask = tc->tc_counter_mask;
	freq = tc->tc_frequency;
	now = 0;
	end = freq * n / 1000000;
	last = func(tc) & mask;
	do {
		cpu_spinwait();
		u = func(tc) & mask;
		if (u < last)
			now += mask - last + u + 1;
		else
			now += u - last;
		last = u;
	} while (now < end);
	return (1);
}

void
DELAY(int n)
{

	TSENTER();
	if (delay_tc(n)) {
		TSEXIT();
		return;
	}

	init_ops.early_delay(n);
	TSEXIT();
}

void
cpu_lock_delay(void)
{

	/*
	 * Use TSC to wait for a usec if present, otherwise fall back
	 * to reading from port 0x84.  We can't call into timecounters
	 * for this delay since timecounters might use spin locks.
	 *
	 * Note that unlike delay_tc(), this uses the TSC even if it
	 * is not P-state invariant.  For this function it is ok to
	 * wait even a few usecs.
	 */
	if (tsc_freq != 0)
		delay_tsc(1);
	else
		inb(0x84);
}
