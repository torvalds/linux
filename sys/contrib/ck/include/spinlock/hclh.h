/*
 * Copyright 2013-2015 Olivier Houchard
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

#ifndef CK_SPINLOCK_HCLH_H
#define CK_SPINLOCK_HCLH_H

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

#ifndef CK_F_SPINLOCK_HCLH
#define CK_F_SPINLOCK_HCLH
struct ck_spinlock_hclh {
	unsigned int wait;
	unsigned int splice;
	int cluster_id;
	struct ck_spinlock_hclh *previous;
};
typedef struct ck_spinlock_hclh ck_spinlock_hclh_t;

CK_CC_INLINE static void
ck_spinlock_hclh_init(struct ck_spinlock_hclh **lock,
    struct ck_spinlock_hclh *unowned,
    int cluster_id)
{

	unowned->previous = NULL;
	unowned->wait = false;
	unowned->splice = false;
	unowned->cluster_id = cluster_id;
	*lock = unowned;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_hclh_locked(struct ck_spinlock_hclh **queue)
{
	struct ck_spinlock_hclh *head;
	bool r;

	head = ck_pr_load_ptr(queue);
	r = ck_pr_load_uint(&head->wait);
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_spinlock_hclh_lock(struct ck_spinlock_hclh **glob_queue,
    struct ck_spinlock_hclh **local_queue,
    struct ck_spinlock_hclh *thread)
{
	struct ck_spinlock_hclh *previous, *local_tail;

	/* Indicate to the next thread on queue that they will have to block. */
	thread->wait = true;
	thread->splice = false;
	thread->cluster_id = (*local_queue)->cluster_id;
	/* Make sure previous->previous doesn't appear to be NULL */
	thread->previous = *local_queue;

	/* Serialize with respect to update of local queue. */
	ck_pr_fence_store_atomic();

	/* Mark current request as last request. Save reference to previous request. */
	previous = ck_pr_fas_ptr(local_queue, thread);
	thread->previous = previous;

	/* Wait until previous thread from the local queue is done with lock. */
	ck_pr_fence_load();
	if (previous->previous != NULL) {
		while (ck_pr_load_uint(&previous->wait) == true &&
			ck_pr_load_int(&previous->cluster_id) == thread->cluster_id &&
			ck_pr_load_uint(&previous->splice) == false)
			ck_pr_stall();

		/* We're head of the global queue, we're done */
		if (ck_pr_load_int(&previous->cluster_id) == thread->cluster_id &&
				ck_pr_load_uint(&previous->splice) == false)
			return;
	}

	/* Now we need to splice the local queue into the global queue. */
	local_tail = ck_pr_load_ptr(local_queue);
	previous = ck_pr_fas_ptr(glob_queue, local_tail);

	ck_pr_store_uint(&local_tail->splice, true);

	/* Wait until previous thread from the global queue is done with lock. */
	while (ck_pr_load_uint(&previous->wait) == true)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_hclh_unlock(struct ck_spinlock_hclh **thread)
{
	struct ck_spinlock_hclh *previous;

	/*
	 * If there are waiters, they are spinning on the current node wait
	 * flag. The flag is cleared so that the successor may complete an
	 * acquisition. If the caller is pre-empted then the predecessor field
	 * may be updated by a successor's lock operation. In order to avoid
	 * this, save a copy of the predecessor before setting the flag.
	 */
	previous = thread[0]->previous;

	/* We have to pay this cost anyways, use it as a compiler barrier too. */
	ck_pr_fence_unlock();
	ck_pr_store_uint(&(*thread)->wait, false);

	/*
	 * Predecessor is guaranteed not to be spinning on previous request,
	 * so update caller to use previous structure. This allows successor
	 * all the time in the world to successfully read updated wait flag.
	 */
	*thread = previous;
	return;
}
#endif /* CK_F_SPINLOCK_HCLH */
#endif /* CK_SPINLOCK_HCLH_H */
