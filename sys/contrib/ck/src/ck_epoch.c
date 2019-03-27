/*
 * Copyright 2011-2015 Samy Al Bahra.
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
 * The implementation here is inspired from the work described in:
 *   Fraser, K. 2004. Practical Lock-Freedom. PhD Thesis, University
 *   of Cambridge Computing Laboratory.
 */

#include <ck_backoff.h>
#include <ck_cc.h>
#include <ck_epoch.h>
#include <ck_pr.h>
#include <ck_stack.h>
#include <ck_stdbool.h>
#include <ck_string.h>

/*
 * Only three distinct values are used for reclamation, but reclamation occurs
 * at e+2 rather than e+1. Any thread in a "critical section" would have
 * acquired some snapshot (e) of the global epoch value (e_g) and set an active
 * flag. Any hazardous references will only occur after a full memory barrier.
 * For example, assume an initial e_g value of 1, e value of 0 and active value
 * of 0.
 *
 * ck_epoch_begin(...)
 *   e = e_g
 *   active = 1
 *   memory_barrier();
 *
 * Any serialized reads may observe e = 0 or e = 1 with active = 0, or e = 0 or
 * e = 1 with active = 1. The e_g value can only go from 1 to 2 if every thread
 * has already observed the value of "1" (or the value we are incrementing
 * from). This guarantees us that for any given value e_g, any threads with-in
 * critical sections (referred to as "active" threads from here on) would have
 * an e value of e_g-1 or e_g. This also means that hazardous references may be
 * shared in both e_g-1 and e_g even if they are logically deleted in e_g.
 *
 * For example, assume all threads have an e value of e_g. Another thread may
 * increment to e_g to e_g+1. Older threads may have a reference to an object
 * which is only deleted in e_g+1. It could be that reader threads are
 * executing some hash table look-ups, while some other writer thread (which
 * causes epoch counter tick) actually deletes the same items that reader
 * threads are looking up (this writer thread having an e value of e_g+1).
 * This is possible if the writer thread re-observes the epoch after the
 * counter tick.
 *
 * Psuedo-code for writer:
 *   ck_epoch_begin()
 *   ht_delete(x)
 *   ck_epoch_end()
 *   ck_epoch_begin()
 *   ht_delete(x)
 *   ck_epoch_end()
 *
 * Psuedo-code for reader:
 *   for (;;) {
 *      x = ht_lookup(x)
 *      ck_pr_inc(&x->value);
 *   }
 *
 * Of course, it is also possible for references logically deleted at e_g-1 to
 * still be accessed at e_g as threads are "active" at the same time
 * (real-world time) mutating shared objects.
 *
 * Now, if the epoch counter is ticked to e_g+1, then no new hazardous
 * references could exist to objects logically deleted at e_g-1. The reason for
 * this is that at e_g+1, all epoch read-side critical sections started at
 * e_g-1 must have been completed. If any epoch read-side critical sections at
 * e_g-1 were still active, then we would never increment to e_g+1 (active != 0
 * ^ e != e_g).  Additionally, e_g may still have hazardous references to
 * objects logically deleted at e_g-1 which means objects logically deleted at
 * e_g-1 cannot be deleted at e_g+1 unless all threads have observed e_g+1
 * (since it is valid for active threads to be at e_g and threads at e_g still
 * require safe memory accesses).
 *
 * However, at e_g+2, all active threads must be either at e_g+1 or e_g+2.
 * Though e_g+2 may share hazardous references with e_g+1, and e_g+1 shares
 * hazardous references to e_g, no active threads are at e_g or e_g-1. This
 * means no hazardous references could exist to objects deleted at e_g-1 (at
 * e_g+2).
 *
 * To summarize these important points,
 *   1) Active threads will always have a value of e_g or e_g-1.
 *   2) Items that are logically deleted e_g or e_g-1 cannot be physically
 *      deleted.
 *   3) Objects logically deleted at e_g-1 can be physically destroyed at e_g+2
 *      or at e_g+1 if no threads are at e_g.
 *
 * Last but not least, if we are at e_g+2, then no active thread is at e_g
 * which means it is safe to apply modulo-3 arithmetic to e_g value in order to
 * re-use e_g to represent the e_g+3 state. This means it is sufficient to
 * represent e_g using only the values 0, 1 or 2. Every time a thread re-visits
 * a e_g (which can be determined with a non-empty deferral list) it can assume
 * objects in the e_g deferral list involved at least three e_g transitions and
 * are thus, safe, for physical deletion.
 *
 * Blocking semantics for epoch reclamation have additional restrictions.
 * Though we only require three deferral lists, reasonable blocking semantics
 * must be able to more gracefully handle bursty write work-loads which could
 * easily cause e_g wrap-around if modulo-3 arithmetic is used. This allows for
 * easy-to-trigger live-lock situations. The work-around to this is to not
 * apply modulo arithmetic to e_g but only to deferral list indexing.
 */
#define CK_EPOCH_GRACE 3U

/*
 * CK_EPOCH_LENGTH must be a power-of-2 (because (CK_EPOCH_LENGTH - 1) is used
 * as a mask, and it must be at least 3 (see comments above).
 */
#if (CK_EPOCH_LENGTH < 3 || (CK_EPOCH_LENGTH & (CK_EPOCH_LENGTH - 1)) != 0)
#error "CK_EPOCH_LENGTH must be a power of 2 and >= 3"
#endif

enum {
	CK_EPOCH_STATE_USED = 0,
	CK_EPOCH_STATE_FREE = 1
};

CK_STACK_CONTAINER(struct ck_epoch_record, record_next,
    ck_epoch_record_container)
CK_STACK_CONTAINER(struct ck_epoch_entry, stack_entry,
    ck_epoch_entry_container)

#define CK_EPOCH_SENSE_MASK	(CK_EPOCH_SENSE - 1)

bool
_ck_epoch_delref(struct ck_epoch_record *record,
    struct ck_epoch_section *section)
{
	struct ck_epoch_ref *current, *other;
	unsigned int i = section->bucket;

	current = &record->local.bucket[i];
	current->count--;

	if (current->count > 0)
		return false;

	/*
	 * If the current bucket no longer has any references, then
	 * determine whether we have already transitioned into a newer
	 * epoch. If so, then make sure to update our shared snapshot
	 * to allow for forward progress.
	 *
	 * If no other active bucket exists, then the record will go
	 * inactive in order to allow for forward progress.
	 */
	other = &record->local.bucket[(i + 1) & CK_EPOCH_SENSE_MASK];
	if (other->count > 0 &&
	    ((int)(current->epoch - other->epoch) < 0)) {
		/*
		 * The other epoch value is actually the newest,
		 * transition to it.
		 */
		ck_pr_store_uint(&record->epoch, other->epoch);
	}

	return true;
}

void
_ck_epoch_addref(struct ck_epoch_record *record,
    struct ck_epoch_section *section)
{
	struct ck_epoch *global = record->global;
	struct ck_epoch_ref *ref;
	unsigned int epoch, i;

	epoch = ck_pr_load_uint(&global->epoch);
	i = epoch & CK_EPOCH_SENSE_MASK;
	ref = &record->local.bucket[i];

	if (ref->count++ == 0) {
#ifndef CK_MD_TSO
		struct ck_epoch_ref *previous;

		/*
		 * The system has already ticked. If another non-zero bucket
		 * exists, make sure to order our observations with respect
		 * to it. Otherwise, it is possible to acquire a reference
		 * from the previous epoch generation.
		 *
		 * On TSO architectures, the monoticity of the global counter
		 * and load-{store, load} ordering are sufficient to guarantee
		 * this ordering.
		 */
		previous = &record->local.bucket[(i + 1) &
		    CK_EPOCH_SENSE_MASK];
		if (previous->count > 0)
			ck_pr_fence_acqrel();
#endif /* !CK_MD_TSO */

		/*
		 * If this is this is a new reference into the current
		 * bucket then cache the associated epoch value.
		 */
		ref->epoch = epoch;
	}

	section->bucket = i;
	return;
}

void
ck_epoch_init(struct ck_epoch *global)
{

	ck_stack_init(&global->records);
	global->epoch = 1;
	global->n_free = 0;
	ck_pr_fence_store();
	return;
}

struct ck_epoch_record *
ck_epoch_recycle(struct ck_epoch *global, void *ct)
{
	struct ck_epoch_record *record;
	ck_stack_entry_t *cursor;
	unsigned int state;

	if (ck_pr_load_uint(&global->n_free) == 0)
		return NULL;

	CK_STACK_FOREACH(&global->records, cursor) {
		record = ck_epoch_record_container(cursor);

		if (ck_pr_load_uint(&record->state) == CK_EPOCH_STATE_FREE) {
			/* Serialize with respect to deferral list clean-up. */
			ck_pr_fence_load();
			state = ck_pr_fas_uint(&record->state,
			    CK_EPOCH_STATE_USED);
			if (state == CK_EPOCH_STATE_FREE) {
				ck_pr_dec_uint(&global->n_free);
				ck_pr_store_ptr(&record->ct, ct);

				/*
				 * The context pointer is ordered by a
				 * subsequent protected section.
				 */
				return record;
			}
		}
	}

	return NULL;
}

void
ck_epoch_register(struct ck_epoch *global, struct ck_epoch_record *record,
    void *ct)
{
	size_t i;

	record->global = global;
	record->state = CK_EPOCH_STATE_USED;
	record->active = 0;
	record->epoch = 0;
	record->n_dispatch = 0;
	record->n_peak = 0;
	record->n_pending = 0;
	record->ct = ct;
	memset(&record->local, 0, sizeof record->local);

	for (i = 0; i < CK_EPOCH_LENGTH; i++)
		ck_stack_init(&record->pending[i]);

	ck_pr_fence_store();
	ck_stack_push_upmc(&global->records, &record->record_next);
	return;
}

void
ck_epoch_unregister(struct ck_epoch_record *record)
{
	struct ck_epoch *global = record->global;
	size_t i;

	record->active = 0;
	record->epoch = 0;
	record->n_dispatch = 0;
	record->n_peak = 0;
	record->n_pending = 0;
	memset(&record->local, 0, sizeof record->local);

	for (i = 0; i < CK_EPOCH_LENGTH; i++)
		ck_stack_init(&record->pending[i]);

	ck_pr_store_ptr(&record->ct, NULL);
	ck_pr_fence_store();
	ck_pr_store_uint(&record->state, CK_EPOCH_STATE_FREE);
	ck_pr_inc_uint(&global->n_free);
	return;
}

static struct ck_epoch_record *
ck_epoch_scan(struct ck_epoch *global,
    struct ck_epoch_record *cr,
    unsigned int epoch,
    bool *af)
{
	ck_stack_entry_t *cursor;

	if (cr == NULL) {
		cursor = CK_STACK_FIRST(&global->records);
		*af = false;
	} else {
		cursor = &cr->record_next;
		*af = true;
	}

	while (cursor != NULL) {
		unsigned int state, active;

		cr = ck_epoch_record_container(cursor);

		state = ck_pr_load_uint(&cr->state);
		if (state & CK_EPOCH_STATE_FREE) {
			cursor = CK_STACK_NEXT(cursor);
			continue;
		}

		active = ck_pr_load_uint(&cr->active);
		*af |= active;

		if (active != 0 && ck_pr_load_uint(&cr->epoch) != epoch)
			return cr;

		cursor = CK_STACK_NEXT(cursor);
	}

	return NULL;
}

static unsigned int
ck_epoch_dispatch(struct ck_epoch_record *record, unsigned int e, ck_stack_t *deferred)
{
	unsigned int epoch = e & (CK_EPOCH_LENGTH - 1);
	ck_stack_entry_t *head, *next, *cursor;
	unsigned int n_pending, n_peak;
	unsigned int i = 0;

	head = ck_stack_batch_pop_upmc(&record->pending[epoch]);
	for (cursor = head; cursor != NULL; cursor = next) {
		struct ck_epoch_entry *entry =
		    ck_epoch_entry_container(cursor);

		next = CK_STACK_NEXT(cursor);
		if (deferred != NULL)
			ck_stack_push_spnc(deferred, &entry->stack_entry);
		else
			entry->function(entry);

		i++;
	}

	n_peak = ck_pr_load_uint(&record->n_peak);
	n_pending = ck_pr_load_uint(&record->n_pending);

	/* We don't require accuracy around peak calculation. */
	if (n_pending > n_peak)
		ck_pr_store_uint(&record->n_peak, n_peak);

	if (i > 0) {
		ck_pr_add_uint(&record->n_dispatch, i);
		ck_pr_sub_uint(&record->n_pending, i);
	}

	return i;
}

/*
 * Reclaim all objects associated with a record.
 */
void
ck_epoch_reclaim(struct ck_epoch_record *record)
{
	unsigned int epoch;

	for (epoch = 0; epoch < CK_EPOCH_LENGTH; epoch++)
		ck_epoch_dispatch(record, epoch, NULL);

	return;
}

CK_CC_FORCE_INLINE static void
epoch_block(struct ck_epoch *global, struct ck_epoch_record *cr,
    ck_epoch_wait_cb_t *cb, void *ct)
{

	if (cb != NULL)
		cb(global, cr, ct);

	return;
}

/*
 * This function must not be called with-in read section.
 */
void
ck_epoch_synchronize_wait(struct ck_epoch *global,
    ck_epoch_wait_cb_t *cb, void *ct)
{
	struct ck_epoch_record *cr;
	unsigned int delta, epoch, goal, i;
	bool active;

	ck_pr_fence_memory();

	/*
	 * The observation of the global epoch must be ordered with respect to
	 * all prior operations. The re-ordering of loads is permitted given
	 * monoticity of global epoch counter.
	 *
	 * If UINT_MAX concurrent mutations were to occur then it is possible
	 * to encounter an ABA-issue. If this is a concern, consider tuning
	 * write-side concurrency.
	 */
	delta = epoch = ck_pr_load_uint(&global->epoch);
	goal = epoch + CK_EPOCH_GRACE;

	for (i = 0, cr = NULL; i < CK_EPOCH_GRACE - 1; cr = NULL, i++) {
		bool r;

		/*
		 * Determine whether all threads have observed the current
		 * epoch with respect to the updates on invocation.
		 */
		while (cr = ck_epoch_scan(global, cr, delta, &active),
		    cr != NULL) {
			unsigned int e_d;

			ck_pr_stall();

			/*
			 * Another writer may have already observed a grace
			 * period.
			 */
			e_d = ck_pr_load_uint(&global->epoch);
			if (e_d == delta) {
				epoch_block(global, cr, cb, ct);
				continue;
			}

			/*
			 * If the epoch has been updated, we may have already
			 * met our goal.
			 */
			delta = e_d;
			if ((goal > epoch) & (delta >= goal))
				goto leave;

			epoch_block(global, cr, cb, ct);

			/*
			 * If the epoch has been updated, then a grace period
			 * requires that all threads are observed idle at the
			 * same epoch.
			 */
			cr = NULL;
		}

		/*
		 * If we have observed all threads as inactive, then we assume
		 * we are at a grace period.
		 */
		if (active == false)
			break;

		/*
		 * Increment current epoch. CAS semantics are used to eliminate
		 * increment operations for synchronization that occurs for the
		 * same global epoch value snapshot.
		 *
		 * If we can guarantee there will only be one active barrier or
		 * epoch tick at a given time, then it is sufficient to use an
		 * increment operation. In a multi-barrier workload, however,
		 * it is possible to overflow the epoch value if we apply
		 * modulo-3 arithmetic.
		 */
		r = ck_pr_cas_uint_value(&global->epoch, delta, delta + 1,
		    &delta);

		/* Order subsequent thread active checks. */
		ck_pr_fence_atomic_load();

		/*
		 * If CAS has succeeded, then set delta to latest snapshot.
		 * Otherwise, we have just acquired latest snapshot.
		 */
		delta = delta + r;
	}

	/*
	 * A majority of use-cases will not require full barrier semantics.
	 * However, if non-temporal instructions are used, full barrier
	 * semantics are necessary.
	 */
leave:
	ck_pr_fence_memory();
	return;
}

void
ck_epoch_synchronize(struct ck_epoch_record *record)
{

	ck_epoch_synchronize_wait(record->global, NULL, NULL);
	return;
}

void
ck_epoch_barrier(struct ck_epoch_record *record)
{

	ck_epoch_synchronize(record);
	ck_epoch_reclaim(record);
	return;
}

void
ck_epoch_barrier_wait(struct ck_epoch_record *record, ck_epoch_wait_cb_t *cb,
    void *ct)
{

	ck_epoch_synchronize_wait(record->global, cb, ct);
	ck_epoch_reclaim(record);
	return;
}

/*
 * It may be worth it to actually apply these deferral semantics to an epoch
 * that was observed at ck_epoch_call time. The problem is that the latter
 * would require a full fence.
 *
 * ck_epoch_call will dispatch to the latest epoch snapshot that was observed.
 * There are cases where it will fail to reclaim as early as it could. If this
 * becomes a problem, we could actually use a heap for epoch buckets but that
 * is far from ideal too.
 */
bool
ck_epoch_poll_deferred(struct ck_epoch_record *record, ck_stack_t *deferred)
{
	bool active;
	unsigned int epoch;
	struct ck_epoch_record *cr = NULL;
	struct ck_epoch *global = record->global;
	unsigned int n_dispatch;

	epoch = ck_pr_load_uint(&global->epoch);

	/* Serialize epoch snapshots with respect to global epoch. */
	ck_pr_fence_memory();

	/*
	 * At this point, epoch is the current global epoch value.
	 * There may or may not be active threads which observed epoch - 1.
	 * (ck_epoch_scan() will tell us that). However, there should be
	 * no active threads which observed epoch - 2.
	 *
	 * Note that checking epoch - 2 is necessary, as race conditions can
	 * allow another thread to increment the global epoch before this
	 * thread runs.
	 */
	n_dispatch = ck_epoch_dispatch(record, epoch - 2, deferred);

	cr = ck_epoch_scan(global, cr, epoch, &active);
	if (cr != NULL)
		return (n_dispatch > 0);

	/* We are at a grace period if all threads are inactive. */
	if (active == false) {
		record->epoch = epoch;
		for (epoch = 0; epoch < CK_EPOCH_LENGTH; epoch++)
			ck_epoch_dispatch(record, epoch, deferred);

		return true;
	}

	/*
	 * If an active thread exists, rely on epoch observation.
	 *
	 * All the active threads entered the epoch section during
	 * the current epoch. Therefore, we can now run the handlers
	 * for the immediately preceding epoch and attempt to
	 * advance the epoch if it hasn't been already.
	 */
	(void)ck_pr_cas_uint(&global->epoch, epoch, epoch + 1);

	ck_epoch_dispatch(record, epoch - 1, deferred);
	return true;
}

bool
ck_epoch_poll(struct ck_epoch_record *record)
{

	return ck_epoch_poll_deferred(record, NULL);
}
