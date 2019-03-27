/*
 * Copyright 2010-2015 Samy Al Bahra.
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

/*
 * (c) Copyright 2008, IBM Corporation.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is an implementation of hazard pointers as detailed in:
 *   http://www.research.ibm.com/people/m/michael/ieeetpds-2004.pdf
 *
 * This API provides a publishing mechanism that defers destruction of
 * hazard pointers until it is safe to do so. Preventing arbitrary re-use
 * protects against the ABA problem and provides safe memory reclamation.
 * The implementation was derived from the Hazard Pointers implementation
 * from the Amino CBBS project. It has been heavily modified for Concurrency
 * Kit.
 */

#include <ck_backoff.h>
#include <ck_cc.h>
#include <ck_hp.h>
#include <ck_pr.h>
#include <ck_stack.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>
#include <ck_stdlib.h>
#include <ck_string.h>

CK_STACK_CONTAINER(struct ck_hp_record, global_entry, ck_hp_record_container)
CK_STACK_CONTAINER(struct ck_hp_hazard, pending_entry, ck_hp_hazard_container)

void
ck_hp_init(struct ck_hp *state,
	   unsigned int degree,
	   unsigned int threshold,
	   ck_hp_destructor_t destroy)
{

	state->threshold = threshold;
	state->degree = degree;
	state->destroy = destroy;
	state->n_subscribers = 0;
	state->n_free = 0;
	ck_stack_init(&state->subscribers);
	ck_pr_fence_store();

	return;
}

void
ck_hp_set_threshold(struct ck_hp *state, unsigned int threshold)
{

	ck_pr_store_uint(&state->threshold, threshold);
	return;
}

struct ck_hp_record *
ck_hp_recycle(struct ck_hp *global)
{
	struct ck_hp_record *record;
	ck_stack_entry_t *entry;
	int state;

	if (ck_pr_load_uint(&global->n_free) == 0)
		return NULL;

	CK_STACK_FOREACH(&global->subscribers, entry) {
		record = ck_hp_record_container(entry);

		if (ck_pr_load_int(&record->state) == CK_HP_FREE) {
			ck_pr_fence_load();
			state = ck_pr_fas_int(&record->state, CK_HP_USED);
			if (state == CK_HP_FREE) {
				ck_pr_dec_uint(&global->n_free);
				return record;
			}
		}
	}

	return NULL;
}

void
ck_hp_unregister(struct ck_hp_record *entry)
{

	entry->n_pending = 0;
	entry->n_peak = 0;
	entry->n_reclamations = 0;
	ck_stack_init(&entry->pending);
	ck_pr_fence_store();
	ck_pr_store_int(&entry->state, CK_HP_FREE);
	ck_pr_inc_uint(&entry->global->n_free);
	return;
}

void
ck_hp_register(struct ck_hp *state,
    struct ck_hp_record *entry,
    void **pointers)
{

	entry->state = CK_HP_USED;
	entry->global = state;
	entry->pointers = pointers;
	entry->n_pending = 0;
	entry->n_peak = 0;
	entry->n_reclamations = 0;
	memset(pointers, 0, state->degree * sizeof(void *));
	ck_stack_init(&entry->pending);
	ck_pr_fence_store();
	ck_stack_push_upmc(&state->subscribers, &entry->global_entry);
	ck_pr_inc_uint(&state->n_subscribers);
	return;
}

static int
hazard_compare(const void *a, const void *b)
{
	void * const *x;
	void * const *y;

	x = a;
	y = b;
	return ((*x > *y) - (*x < *y));
}

CK_CC_INLINE static bool
ck_hp_member_scan(ck_stack_entry_t *entry, unsigned int degree, void *pointer)
{
	struct ck_hp_record *record;
	unsigned int i;
	void *hazard;

	do {
		record = ck_hp_record_container(entry);
		if (ck_pr_load_int(&record->state) == CK_HP_FREE)
			continue;

		if (ck_pr_load_ptr(&record->pointers) == NULL)
			continue;

		for (i = 0; i < degree; i++) {
			hazard = ck_pr_load_ptr(&record->pointers[i]);
			if (hazard == pointer)
				return (true);
		}
	} while ((entry = CK_STACK_NEXT(entry)) != NULL);

	return (false);
}

CK_CC_INLINE static void *
ck_hp_member_cache(struct ck_hp *global, void **cache, unsigned int *n_hazards)
{
	struct ck_hp_record *record;
	ck_stack_entry_t *entry;
	unsigned int hazards = 0;
	unsigned int i;
	void *pointer;

	CK_STACK_FOREACH(&global->subscribers, entry) {
		record = ck_hp_record_container(entry);
		if (ck_pr_load_int(&record->state) == CK_HP_FREE)
			continue;

		if (ck_pr_load_ptr(&record->pointers) == NULL)
			continue;

		for (i = 0; i < global->degree; i++) {
			if (hazards > CK_HP_CACHE)
				break;

			pointer = ck_pr_load_ptr(&record->pointers[i]);
			if (pointer != NULL)
				cache[hazards++] = pointer;
		}
	}

	*n_hazards = hazards;
	return (entry);
}

void
ck_hp_reclaim(struct ck_hp_record *thread)
{
	struct ck_hp_hazard *hazard;
	struct ck_hp *global = thread->global;
	unsigned int n_hazards;
	void **cache, *marker, *match;
	ck_stack_entry_t *previous, *entry, *next;

	/* Store as many entries as possible in local array. */
	cache = thread->cache;
	marker = ck_hp_member_cache(global, cache, &n_hazards);

	/*
	 * In theory, there is an n such that (n * (log n) ** 2) < np.
	 */
	qsort(cache, n_hazards, sizeof(void *), hazard_compare);

	previous = NULL;
	CK_STACK_FOREACH_SAFE(&thread->pending, entry, next) {
		hazard = ck_hp_hazard_container(entry);
		match = bsearch(&hazard->pointer, cache, n_hazards,
				  sizeof(void *), hazard_compare);
		if (match != NULL) {
			previous = entry;
			continue;
		}

		if (marker != NULL &&
		    ck_hp_member_scan(marker, global->degree, hazard->pointer)) {
			previous = entry;
			continue;
		}

		thread->n_pending -= 1;

		/* Remove from the pending stack. */
		if (previous)
			CK_STACK_NEXT(previous) = CK_STACK_NEXT(entry);
		else
			CK_STACK_FIRST(&thread->pending) = CK_STACK_NEXT(entry);

		/* The entry is now safe to destroy. */
		global->destroy(hazard->data);
		thread->n_reclamations++;
	}

	return;
}

void
ck_hp_retire(struct ck_hp_record *thread,
    struct ck_hp_hazard *hazard,
    void *data,
    void *pointer)
{

	ck_pr_store_ptr(&hazard->pointer, pointer);
	ck_pr_store_ptr(&hazard->data, data);
	ck_stack_push_spnc(&thread->pending, &hazard->pending_entry);

	thread->n_pending += 1;
	if (thread->n_pending > thread->n_peak)
		thread->n_peak = thread->n_pending;

	return;
}

void
ck_hp_free(struct ck_hp_record *thread,
    struct ck_hp_hazard *hazard,
    void *data,
    void *pointer)
{
	struct ck_hp *global;

	global = ck_pr_load_ptr(&thread->global);
	ck_pr_store_ptr(&hazard->data, data);
	ck_pr_store_ptr(&hazard->pointer, pointer);
	ck_stack_push_spnc(&thread->pending, &hazard->pending_entry);

	thread->n_pending += 1;
	if (thread->n_pending > thread->n_peak)
		thread->n_peak = thread->n_pending;

	if (thread->n_pending >= global->threshold)
		ck_hp_reclaim(thread);

	return;
}

void
ck_hp_purge(struct ck_hp_record *thread)
{
	ck_backoff_t backoff = CK_BACKOFF_INITIALIZER;

	while (thread->n_pending > 0) {
		ck_hp_reclaim(thread);
		if (thread->n_pending > 0)
			ck_backoff_eb(&backoff);
	}

	return;
}
