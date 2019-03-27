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
#include <ck_pr.h>

#include "ck_internal.h"

/*
 * This is a tournament barrier implementation. Threads are statically
 * assigned roles to perform for each round of the barrier. Winners
 * move on to the next round, while losers spin in their current rounds
 * on their own flags. During the last round, the champion of the tournament
 * sets the last flag that begins the wakeup process.
 */

enum {
	CK_BARRIER_TOURNAMENT_BYE,
	CK_BARRIER_TOURNAMENT_CHAMPION,
	CK_BARRIER_TOURNAMENT_DROPOUT,
	CK_BARRIER_TOURNAMENT_LOSER,
	CK_BARRIER_TOURNAMENT_WINNER
};

void
ck_barrier_tournament_subscribe(struct ck_barrier_tournament *barrier,
    struct ck_barrier_tournament_state *state)
{

	state->sense = ~0;
	state->vpid = ck_pr_faa_uint(&barrier->tid, 1);
	return;
}

void
ck_barrier_tournament_init(struct ck_barrier_tournament *barrier,
    struct ck_barrier_tournament_round **rounds,
    unsigned int nthr)
{
	unsigned int i, k, size, twok, twokm1, imod2k;

	ck_pr_store_uint(&barrier->tid, 0);
	barrier->size = size = ck_barrier_tournament_size(nthr);

	for (i = 0; i < nthr; ++i) {
		/* The first role is always CK_BARRIER_TOURNAMENT_DROPOUT. */
		rounds[i][0].flag = 0;
		rounds[i][0].role = CK_BARRIER_TOURNAMENT_DROPOUT;
		for (k = 1, twok = 2, twokm1 = 1; k < size; ++k, twokm1 = twok, twok <<= 1) {
			rounds[i][k].flag = 0;

			imod2k = i & (twok - 1);
			if (imod2k == 0) {
				if ((i + twokm1 < nthr) && (twok < nthr))
					rounds[i][k].role = CK_BARRIER_TOURNAMENT_WINNER;
				else if (i + twokm1 >= nthr)
					rounds[i][k].role = CK_BARRIER_TOURNAMENT_BYE;
			}

			if (imod2k == twokm1)
				rounds[i][k].role = CK_BARRIER_TOURNAMENT_LOSER;
			else if ((i == 0) && (twok >= nthr))
				rounds[i][k].role = CK_BARRIER_TOURNAMENT_CHAMPION;

			if (rounds[i][k].role == CK_BARRIER_TOURNAMENT_LOSER)
				rounds[i][k].opponent = &rounds[i - twokm1][k].flag;
			else if (rounds[i][k].role == CK_BARRIER_TOURNAMENT_WINNER ||
				 rounds[i][k].role == CK_BARRIER_TOURNAMENT_CHAMPION)
				rounds[i][k].opponent = &rounds[i + twokm1][k].flag;
		}
	}

	ck_pr_store_ptr(&barrier->rounds, rounds);
	return;
}

unsigned int
ck_barrier_tournament_size(unsigned int nthr)
{

	return (ck_internal_log(ck_internal_power_2(nthr)) + 1);
}

void
ck_barrier_tournament(struct ck_barrier_tournament *barrier,
    struct ck_barrier_tournament_state *state)
{
	struct ck_barrier_tournament_round **rounds = ck_pr_load_ptr(&barrier->rounds);
	int round = 1;

	if (barrier->size == 1)
		return;

	for (;; ++round) {
		switch (rounds[state->vpid][round].role) {
		case CK_BARRIER_TOURNAMENT_BYE:
			break;
		case CK_BARRIER_TOURNAMENT_CHAMPION:
			/*
			 * The CK_BARRIER_TOURNAMENT_CHAMPION waits until it wins the tournament; it then
			 * sets the final flag before the wakeup phase of the barrier.
			 */
			while (ck_pr_load_uint(&rounds[state->vpid][round].flag) != state->sense)
				ck_pr_stall();

			ck_pr_store_uint(rounds[state->vpid][round].opponent, state->sense);
			goto wakeup;
		case CK_BARRIER_TOURNAMENT_DROPOUT:
			/* NOTREACHED */
			break;
		case CK_BARRIER_TOURNAMENT_LOSER:
			/*
			 * CK_BARRIER_TOURNAMENT_LOSERs set the flags of their opponents and wait until
			 * their opponents release them after the tournament is over.
			 */
			ck_pr_store_uint(rounds[state->vpid][round].opponent, state->sense);
			while (ck_pr_load_uint(&rounds[state->vpid][round].flag) != state->sense)
				ck_pr_stall();

			goto wakeup;
		case CK_BARRIER_TOURNAMENT_WINNER:
			/*
			 * CK_BARRIER_TOURNAMENT_WINNERs wait until their current opponent sets their flag; they then
			 * continue to the next round of the tournament.
			 */
			while (ck_pr_load_uint(&rounds[state->vpid][round].flag) != state->sense)
				ck_pr_stall();
			break;
		}
	}

wakeup:
	for (round -= 1 ;; --round) {
		switch (rounds[state->vpid][round].role) {
		case CK_BARRIER_TOURNAMENT_BYE:
			break;
		case CK_BARRIER_TOURNAMENT_CHAMPION:
			/* NOTREACHED */
			break;
		case CK_BARRIER_TOURNAMENT_DROPOUT:
			goto leave;
			break;
		case CK_BARRIER_TOURNAMENT_LOSER:
			/* NOTREACHED */
			break;
		case CK_BARRIER_TOURNAMENT_WINNER:
			/*
			 * Winners inform their old opponents the tournament is over
			 * by setting their flags.
			 */
			ck_pr_store_uint(rounds[state->vpid][round].opponent, state->sense);
			break;
		}
	}

leave:
	ck_pr_fence_memory();
	state->sense = ~state->sense;
	return;
}
