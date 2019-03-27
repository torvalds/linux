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

#ifndef CK_SPINLOCK_MCS_H
#define CK_SPINLOCK_MCS_H

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

#ifndef CK_F_SPINLOCK_MCS
#define CK_F_SPINLOCK_MCS

struct ck_spinlock_mcs {
	unsigned int locked;
	struct ck_spinlock_mcs *next;
};
typedef struct ck_spinlock_mcs * ck_spinlock_mcs_t;
typedef struct ck_spinlock_mcs ck_spinlock_mcs_context_t;

#define CK_SPINLOCK_MCS_INITIALIZER	    (NULL)

CK_CC_INLINE static void
ck_spinlock_mcs_init(struct ck_spinlock_mcs **queue)
{

	*queue = NULL;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_mcs_trylock(struct ck_spinlock_mcs **queue,
    struct ck_spinlock_mcs *node)
{
	bool r;

	node->locked = true;
	node->next = NULL;
	ck_pr_fence_store_atomic();

	r = ck_pr_cas_ptr(queue, NULL, node);
	ck_pr_fence_lock();
	return r;
}

CK_CC_INLINE static bool
ck_spinlock_mcs_locked(struct ck_spinlock_mcs **queue)
{
	bool r;

	r = ck_pr_load_ptr(queue) != NULL;
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_spinlock_mcs_lock(struct ck_spinlock_mcs **queue,
    struct ck_spinlock_mcs *node)
{
	struct ck_spinlock_mcs *previous;

	/*
	 * In the case that there is a successor, let them know they must
	 * wait for us to unlock.
	 */
	node->locked = true;
	node->next = NULL;
	ck_pr_fence_store_atomic();

	/*
	 * Swap current tail with current lock request. If the swap operation
	 * returns NULL, it means the queue was empty. If the queue was empty,
	 * then the operation is complete.
	 */
	previous = ck_pr_fas_ptr(queue, node);
	if (previous != NULL) {
		/*
		 * Let the previous lock holder know that we are waiting on
		 * them.
		 */
		ck_pr_store_ptr(&previous->next, node);
		while (ck_pr_load_uint(&node->locked) == true)
			ck_pr_stall();
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_mcs_unlock(struct ck_spinlock_mcs **queue,
    struct ck_spinlock_mcs *node)
{
	struct ck_spinlock_mcs *next;

	ck_pr_fence_unlock();

	next = ck_pr_load_ptr(&node->next);
	if (next == NULL) {
		/*
		 * If there is no request following us then it is a possibilty
		 * that we are the current tail. In this case, we may just
		 * mark the spinlock queue as empty.
		 */
		if (ck_pr_load_ptr(queue) == node &&
		    ck_pr_cas_ptr(queue, node, NULL) == true) {
			return;
		}

		/*
		 * If the node is not the current tail then a lock operation
		 * is in-progress. In this case, busy-wait until the queue is
		 * in a consistent state to wake up the incoming lock
		 * request.
		 */
		for (;;) {
			next = ck_pr_load_ptr(&node->next);
			if (next != NULL)
				break;

			ck_pr_stall();
		}
	}

	/* Allow the next lock operation to complete. */
	ck_pr_store_uint(&next->locked, false);
	return;
}
#endif /* CK_F_SPINLOCK_MCS */
#endif /* CK_SPINLOCK_MCS_H */
