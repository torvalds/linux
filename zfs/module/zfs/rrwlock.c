/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/refcount.h>
#include <sys/rrwlock.h>

/*
 * This file contains the implementation of a re-entrant read
 * reader/writer lock (aka "rrwlock").
 *
 * This is a normal reader/writer lock with the additional feature
 * of allowing threads who have already obtained a read lock to
 * re-enter another read lock (re-entrant read) - even if there are
 * waiting writers.
 *
 * Callers who have not obtained a read lock give waiting writers priority.
 *
 * The rrwlock_t lock does not allow re-entrant writers, nor does it
 * allow a re-entrant mix of reads and writes (that is, it does not
 * allow a caller who has already obtained a read lock to be able to
 * then grab a write lock without first dropping all read locks, and
 * vice versa).
 *
 * The rrwlock_t uses tsd (thread specific data) to keep a list of
 * nodes (rrw_node_t), where each node keeps track of which specific
 * lock (rrw_node_t::rn_rrl) the thread has grabbed.  Since re-entering
 * should be rare, a thread that grabs multiple reads on the same rrwlock_t
 * will store multiple rrw_node_ts of the same 'rrn_rrl'. Nodes on the
 * tsd list can represent a different rrwlock_t.  This allows a thread
 * to enter multiple and unique rrwlock_ts for read locks at the same time.
 *
 * Since using tsd exposes some overhead, the rrwlock_t only needs to
 * keep tsd data when writers are waiting.  If no writers are waiting, then
 * a reader just bumps the anonymous read count (rr_anon_rcount) - no tsd
 * is needed.  Once a writer attempts to grab the lock, readers then
 * keep tsd data and bump the linked readers count (rr_linked_rcount).
 *
 * If there are waiting writers and there are anonymous readers, then a
 * reader doesn't know if it is a re-entrant lock. But since it may be one,
 * we allow the read to proceed (otherwise it could deadlock).  Since once
 * waiting writers are active, readers no longer bump the anonymous count,
 * the anonymous readers will eventually flush themselves out.  At this point,
 * readers will be able to tell if they are a re-entrant lock (have a
 * rrw_node_t entry for the lock) or not. If they are a re-entrant lock, then
 * we must let the proceed.  If they are not, then the reader blocks for the
 * waiting writers.  Hence, we do not starve writers.
 */

/* global key for TSD */
uint_t rrw_tsd_key;

typedef struct rrw_node {
	struct rrw_node *rn_next;
	rrwlock_t *rn_rrl;
	void *rn_tag;
} rrw_node_t;

static rrw_node_t *
rrn_find(rrwlock_t *rrl)
{
	rrw_node_t *rn;

	if (refcount_count(&rrl->rr_linked_rcount) == 0)
		return (NULL);

	for (rn = tsd_get(rrw_tsd_key); rn != NULL; rn = rn->rn_next) {
		if (rn->rn_rrl == rrl)
			return (rn);
	}
	return (NULL);
}

/*
 * Add a node to the head of the singly linked list.
 */
static void
rrn_add(rrwlock_t *rrl, void *tag)
{
	rrw_node_t *rn;

	rn = kmem_alloc(sizeof (*rn), KM_SLEEP);
	rn->rn_rrl = rrl;
	rn->rn_next = tsd_get(rrw_tsd_key);
	rn->rn_tag = tag;
	VERIFY(tsd_set(rrw_tsd_key, rn) == 0);
}

/*
 * If a node is found for 'rrl', then remove the node from this
 * thread's list and return TRUE; otherwise return FALSE.
 */
static boolean_t
rrn_find_and_remove(rrwlock_t *rrl, void *tag)
{
	rrw_node_t *rn;
	rrw_node_t *prev = NULL;

	if (refcount_count(&rrl->rr_linked_rcount) == 0)
		return (B_FALSE);

	for (rn = tsd_get(rrw_tsd_key); rn != NULL; rn = rn->rn_next) {
		if (rn->rn_rrl == rrl && rn->rn_tag == tag) {
			if (prev)
				prev->rn_next = rn->rn_next;
			else
				VERIFY(tsd_set(rrw_tsd_key, rn->rn_next) == 0);
			kmem_free(rn, sizeof (*rn));
			return (B_TRUE);
		}
		prev = rn;
	}
	return (B_FALSE);
}

void
rrw_init(rrwlock_t *rrl, boolean_t track_all)
{
	mutex_init(&rrl->rr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&rrl->rr_cv, NULL, CV_DEFAULT, NULL);
	rrl->rr_writer = NULL;
	refcount_create(&rrl->rr_anon_rcount);
	refcount_create(&rrl->rr_linked_rcount);
	rrl->rr_writer_wanted = B_FALSE;
	rrl->rr_track_all = track_all;
}

void
rrw_destroy(rrwlock_t *rrl)
{
	mutex_destroy(&rrl->rr_lock);
	cv_destroy(&rrl->rr_cv);
	ASSERT(rrl->rr_writer == NULL);
	refcount_destroy(&rrl->rr_anon_rcount);
	refcount_destroy(&rrl->rr_linked_rcount);
}

static void
rrw_enter_read_impl(rrwlock_t *rrl, boolean_t prio, void *tag)
{
	mutex_enter(&rrl->rr_lock);
#if !defined(DEBUG) && defined(_KERNEL)
	if (rrl->rr_writer == NULL && !rrl->rr_writer_wanted &&
	    !rrl->rr_track_all) {
		rrl->rr_anon_rcount.rc_count++;
		mutex_exit(&rrl->rr_lock);
		return;
	}
	DTRACE_PROBE(zfs__rrwfastpath__rdmiss);
#endif
	ASSERT(rrl->rr_writer != curthread);
	ASSERT(refcount_count(&rrl->rr_anon_rcount) >= 0);

	while (rrl->rr_writer != NULL || (rrl->rr_writer_wanted &&
	    refcount_is_zero(&rrl->rr_anon_rcount) && !prio &&
	    rrn_find(rrl) == NULL))
		cv_wait(&rrl->rr_cv, &rrl->rr_lock);

	if (rrl->rr_writer_wanted || rrl->rr_track_all) {
		/* may or may not be a re-entrant enter */
		rrn_add(rrl, tag);
		(void) refcount_add(&rrl->rr_linked_rcount, tag);
	} else {
		(void) refcount_add(&rrl->rr_anon_rcount, tag);
	}
	ASSERT(rrl->rr_writer == NULL);
	mutex_exit(&rrl->rr_lock);
}

void
rrw_enter_read(rrwlock_t *rrl, void *tag)
{
	rrw_enter_read_impl(rrl, B_FALSE, tag);
}

/*
 * take a read lock even if there are pending write lock requests. if we want
 * to take a lock reentrantly, but from different threads (that have a
 * relationship to each other), the normal detection mechanism to overrule
 * the pending writer does not work, so we have to give an explicit hint here.
 */
void
rrw_enter_read_prio(rrwlock_t *rrl, void *tag)
{
	rrw_enter_read_impl(rrl, B_TRUE, tag);
}


void
rrw_enter_write(rrwlock_t *rrl)
{
	mutex_enter(&rrl->rr_lock);
	ASSERT(rrl->rr_writer != curthread);

	while (refcount_count(&rrl->rr_anon_rcount) > 0 ||
	    refcount_count(&rrl->rr_linked_rcount) > 0 ||
	    rrl->rr_writer != NULL) {
		rrl->rr_writer_wanted = B_TRUE;
		cv_wait(&rrl->rr_cv, &rrl->rr_lock);
	}
	rrl->rr_writer_wanted = B_FALSE;
	rrl->rr_writer = curthread;
	mutex_exit(&rrl->rr_lock);
}

void
rrw_enter(rrwlock_t *rrl, krw_t rw, void *tag)
{
	if (rw == RW_READER)
		rrw_enter_read(rrl, tag);
	else
		rrw_enter_write(rrl);
}

void
rrw_exit(rrwlock_t *rrl, void *tag)
{
	mutex_enter(&rrl->rr_lock);
#if !defined(DEBUG) && defined(_KERNEL)
	if (!rrl->rr_writer && rrl->rr_linked_rcount.rc_count == 0) {
		rrl->rr_anon_rcount.rc_count--;
		if (rrl->rr_anon_rcount.rc_count == 0)
			cv_broadcast(&rrl->rr_cv);
		mutex_exit(&rrl->rr_lock);
		return;
	}
	DTRACE_PROBE(zfs__rrwfastpath__exitmiss);
#endif
	ASSERT(!refcount_is_zero(&rrl->rr_anon_rcount) ||
	    !refcount_is_zero(&rrl->rr_linked_rcount) ||
	    rrl->rr_writer != NULL);

	if (rrl->rr_writer == NULL) {
		int64_t count;
		if (rrn_find_and_remove(rrl, tag)) {
			count = refcount_remove(&rrl->rr_linked_rcount, tag);
		} else {
			ASSERT(!rrl->rr_track_all);
			count = refcount_remove(&rrl->rr_anon_rcount, tag);
		}
		if (count == 0)
			cv_broadcast(&rrl->rr_cv);
	} else {
		ASSERT(rrl->rr_writer == curthread);
		ASSERT(refcount_is_zero(&rrl->rr_anon_rcount) &&
		    refcount_is_zero(&rrl->rr_linked_rcount));
		rrl->rr_writer = NULL;
		cv_broadcast(&rrl->rr_cv);
	}
	mutex_exit(&rrl->rr_lock);
}

/*
 * If the lock was created with track_all, rrw_held(RW_READER) will return
 * B_TRUE iff the current thread has the lock for reader.  Otherwise it may
 * return B_TRUE if any thread has the lock for reader.
 */
boolean_t
rrw_held(rrwlock_t *rrl, krw_t rw)
{
	boolean_t held;

	mutex_enter(&rrl->rr_lock);
	if (rw == RW_WRITER) {
		held = (rrl->rr_writer == curthread);
	} else {
		held = (!refcount_is_zero(&rrl->rr_anon_rcount) ||
		    rrn_find(rrl) != NULL);
	}
	mutex_exit(&rrl->rr_lock);

	return (held);
}

void
rrw_tsd_destroy(void *arg)
{
	rrw_node_t *rn = arg;
	if (rn != NULL) {
		panic("thread %p terminating with rrw lock %p held",
		    (void *)curthread, (void *)rn->rn_rrl);
	}
}

/*
 * A reader-mostly lock implementation, tuning above reader-writer locks
 * for hightly parallel read acquisitions, while pessimizing writes.
 *
 * The idea is to split single busy lock into array of locks, so that
 * each reader can lock only one of them for read, depending on result
 * of simple hash function.  That proportionally reduces lock congestion.
 * Writer at the same time has to sequentially acquire write on all the locks.
 * That makes write acquisition proportionally slower, but in places where
 * it is used (filesystem unmount) performance is not critical.
 *
 * All the functions below are direct wrappers around functions above.
 */
void
rrm_init(rrmlock_t *rrl, boolean_t track_all)
{
	int i;

	for (i = 0; i < RRM_NUM_LOCKS; i++)
		rrw_init(&rrl->locks[i], track_all);
}

void
rrm_destroy(rrmlock_t *rrl)
{
	int i;

	for (i = 0; i < RRM_NUM_LOCKS; i++)
		rrw_destroy(&rrl->locks[i]);
}

void
rrm_enter(rrmlock_t *rrl, krw_t rw, void *tag)
{
	if (rw == RW_READER)
		rrm_enter_read(rrl, tag);
	else
		rrm_enter_write(rrl);
}

/*
 * This maps the current thread to a specific lock.  Note that the lock
 * must be released by the same thread that acquired it.  We do this
 * mapping by taking the thread pointer mod a prime number.  We examine
 * only the low 32 bits of the thread pointer, because 32-bit division
 * is faster than 64-bit division, and the high 32 bits have little
 * entropy anyway.
 */
#define	RRM_TD_LOCK()	(((uint32_t)(uintptr_t)(curthread)) % RRM_NUM_LOCKS)

void
rrm_enter_read(rrmlock_t *rrl, void *tag)
{
	rrw_enter_read(&rrl->locks[RRM_TD_LOCK()], tag);
}

void
rrm_enter_write(rrmlock_t *rrl)
{
	int i;

	for (i = 0; i < RRM_NUM_LOCKS; i++)
		rrw_enter_write(&rrl->locks[i]);
}

void
rrm_exit(rrmlock_t *rrl, void *tag)
{
	int i;

	if (rrl->locks[0].rr_writer == curthread) {
		for (i = 0; i < RRM_NUM_LOCKS; i++)
			rrw_exit(&rrl->locks[i], tag);
	} else {
		rrw_exit(&rrl->locks[RRM_TD_LOCK()], tag);
	}
}

boolean_t
rrm_held(rrmlock_t *rrl, krw_t rw)
{
	if (rw == RW_WRITER) {
		return (rrw_held(&rrl->locks[0], rw));
	} else {
		return (rrw_held(&rrl->locks[RRM_TD_LOCK()], rw));
	}
}
