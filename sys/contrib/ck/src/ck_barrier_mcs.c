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
#include <ck_stdbool.h>

void
ck_barrier_mcs_init(struct ck_barrier_mcs *barrier, unsigned int nthr)
{
	unsigned int i, j;

	ck_pr_store_uint(&barrier->tid, 0);

	for (i = 0; i < nthr; ++i) {
		for (j = 0; j < 4; ++j) {
			/*
			 * If there are still threads that don't have parents,
			 * add it as a child.
			 */
			barrier[i].havechild[j] = ((i << 2) + j < nthr - 1) ? ~0 : 0;

			/*
			 * childnotready is initialized to havechild to ensure
			 * a thread does not wait for a child that does not exist.
			 */
			barrier[i].childnotready[j] = barrier[i].havechild[j];
		}

		/* The root thread does not have a parent. */
		barrier[i].parent = (i == 0) ?
		    &barrier[i].dummy :
		    &barrier[(i - 1) >> 2].childnotready[(i - 1) & 3];

		/* Leaf threads do not have any children. */
		barrier[i].children[0] = ((i << 1) + 1 >= nthr)	?
		    &barrier[i].dummy :
		    &barrier[(i << 1) + 1].parentsense;

		barrier[i].children[1] = ((i << 1) + 2 >= nthr)	?
		    &barrier[i].dummy :
		    &barrier[(i << 1) + 2].parentsense;

		barrier[i].parentsense = 0;
	}

	return;
}

void
ck_barrier_mcs_subscribe(struct ck_barrier_mcs *barrier, struct ck_barrier_mcs_state *state)
{

	state->sense = ~0;
	state->vpid = ck_pr_faa_uint(&barrier->tid, 1);
	return;
}

CK_CC_INLINE static bool
ck_barrier_mcs_check_children(unsigned int *childnotready)
{

	if (ck_pr_load_uint(&childnotready[0]) != 0)
		return false;
	if (ck_pr_load_uint(&childnotready[1]) != 0)
		return false;
	if (ck_pr_load_uint(&childnotready[2]) != 0)
		return false;
	if (ck_pr_load_uint(&childnotready[3]) != 0)
		return false;

	return true;
}

CK_CC_INLINE static void
ck_barrier_mcs_reinitialize_children(struct ck_barrier_mcs *node)
{

	ck_pr_store_uint(&node->childnotready[0], node->havechild[0]);
	ck_pr_store_uint(&node->childnotready[1], node->havechild[1]);
	ck_pr_store_uint(&node->childnotready[2], node->havechild[2]);
	ck_pr_store_uint(&node->childnotready[3], node->havechild[3]);
	return;
}

void
ck_barrier_mcs(struct ck_barrier_mcs *barrier,
    struct ck_barrier_mcs_state *state)
{

	/*
	 * Wait until all children have reached the barrier and are done waiting
	 * for their children.
	 */
	while (ck_barrier_mcs_check_children(barrier[state->vpid].childnotready) == false)
		ck_pr_stall();

	/* Reinitialize for next barrier. */
	ck_barrier_mcs_reinitialize_children(&barrier[state->vpid]);

	/* Inform parent thread and its children have arrived at the barrier. */
	ck_pr_store_uint(barrier[state->vpid].parent, 0);

	/* Wait until parent indicates all threads have arrived at the barrier. */
	if (state->vpid != 0) {
		while (ck_pr_load_uint(&barrier[state->vpid].parentsense) != state->sense)
			ck_pr_stall();
	}

	/* Inform children of successful barrier. */
	ck_pr_store_uint(barrier[state->vpid].children[0], state->sense);
	ck_pr_store_uint(barrier[state->vpid].children[1], state->sense);
	state->sense = ~state->sense;
	ck_pr_fence_memory();
	return;
}
