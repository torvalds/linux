/*
 * Copyright 2011-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ck_barrier.h>
#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_spinlock.h>

#include "ck_internal.h"

void
ck_barrier_dissemination_init(struct ck_barrier_dissemination *barrier,
    struct ck_barrier_dissemination_flag **barrier_internal,
    unsigned int nthr)
{
	unsigned int i, j, k, size, offset;
	bool p = nthr & (nthr - 1);

	barrier->nthr = nthr;
	barrier->size = size = ck_internal_log(ck_internal_power_2(nthr));
	ck_pr_store_uint(&barrier->tid, 0);

	for (i = 0; i < nthr; ++i) {
		barrier[i].flags[0] = barrier_internal[i];
		barrier[i].flags[1] = barrier_internal[i] + size;
	}

	for (i = 0; i < nthr; ++i) {
		for (k = 0, offset = 1; k < size; ++k, offset <<= 1) {
			/*
			 * Determine the thread's partner, j, for the current round, k.
			 * Partners are chosen such that by the completion of the barrier,
			 * every thread has been directly (having one of its flag set) or
			 * indirectly (having one of its partners's flags set) signaled
			 * by every other thread in the barrier.
			 */
			if (p == false)
				j = (i + offset) & (nthr - 1);
			else
				j = (i + offset) % nthr;

			/* Set the thread's partner for round k. */
			barrier[i].flags[0][k].pflag = &barrier[j].flags[0][k].tflag;
			barrier[i].flags[1][k].pflag = &barrier[j].flags[1][k].tflag;

			/* Set the thread's flags to false. */
			barrier[i].flags[0][k].tflag = barrier[i].flags[1][k].tflag = 0;
		}
	}

	return;
}

void
ck_barrier_dissemination_subscribe(struct ck_barrier_dissemination *barrier,
    struct ck_barrier_dissemination_state *state)
{

	state->parity = 0;
	state->sense = ~0;
	state->tid = ck_pr_faa_uint(&barrier->tid, 1);
	return;
}

unsigned int
ck_barrier_dissemination_size(unsigned int nthr)
{

	return (ck_internal_log(ck_internal_power_2(nthr)) << 1);
}

void
ck_barrier_dissemination(struct ck_barrier_dissemination *barrier,
    struct ck_barrier_dissemination_state *state)
{
	unsigned int i;
	unsigned int size = barrier->size;

	for (i = 0; i < size; ++i) {
		unsigned int *pflag, *tflag;

		pflag = barrier[state->tid].flags[state->parity][i].pflag;
		tflag = &barrier[state->tid].flags[state->parity][i].tflag;

		/* Unblock current partner. */
		ck_pr_store_uint(pflag, state->sense);

		/* Wait until some other thread unblocks this one. */
		while (ck_pr_load_uint(tflag) != state->sense)
			ck_pr_stall();
	}

	/*
	 * Dissemination barriers use two sets of flags to prevent race conditions
	 * between successive calls to the barrier. Parity indicates which set will
	 * be used for the next barrier. They also use a sense reversal technique
	 * to avoid re-initialization of the flags for every two calls to the barrier.
	 */
	if (state->parity == 1)
		state->sense = ~state->sense;

	state->parity = 1 - state->parity;

	ck_pr_fence_acquire();
	return;
}
