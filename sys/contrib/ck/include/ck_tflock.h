/*
 * Copyright 2014 Samy Al Bahra.
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

#ifndef CK_TFLOCK_TICKET_H
#define CK_TFLOCK_TICKET_H

/*
 * This is an implementation of task-fair locks derived from the work
 * described in:
 *	John M. Mellor-Crummey and Michael L. Scott. 1991.
 *	Scalable reader-writer synchronization for shared-memory
 *	multiprocessors. SIGPLAN Not. 26, 7 (April 1991), 106-113.
 */

#include <ck_cc.h>
#include <ck_pr.h>

struct ck_tflock_ticket {
	uint32_t request;
	uint32_t completion;
};
typedef struct ck_tflock_ticket ck_tflock_ticket_t;

#define CK_TFLOCK_TICKET_INITIALIZER { 0, 0 }

#define CK_TFLOCK_TICKET_RC_INCR	0x10000U	/* Read-side increment. */
#define CK_TFLOCK_TICKET_WC_INCR	0x1U		/* Write-side increment. */
#define CK_TFLOCK_TICKET_W_MASK		0xffffU		/* Write-side mask. */
#define CK_TFLOCK_TICKET_WC_TOPMSK	0x8000U		/* Write clear mask for overflow. */
#define CK_TFLOCK_TICKET_RC_TOPMSK	0x80000000U	/* Read clear mask for overflow. */

CK_CC_INLINE static uint32_t
ck_tflock_ticket_fca_32(uint32_t *target, uint32_t mask, uint32_t delta)
{
	uint32_t snapshot = ck_pr_load_32(target);
	uint32_t goal;

	for (;;) {
		goal = (snapshot & ~mask) + delta;
		if (ck_pr_cas_32_value(target, snapshot, goal, &snapshot) == true)
			break;

		ck_pr_stall();
	}

	return snapshot;
}

CK_CC_INLINE static void
ck_tflock_ticket_init(struct ck_tflock_ticket *pf)
{

	pf->request = pf->completion = 0;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static void
ck_tflock_ticket_write_lock(struct ck_tflock_ticket *lock)
{
	uint32_t previous;

	previous = ck_tflock_ticket_fca_32(&lock->request, CK_TFLOCK_TICKET_WC_TOPMSK,
	    CK_TFLOCK_TICKET_WC_INCR);
	ck_pr_fence_atomic_load();
	while (ck_pr_load_32(&lock->completion) != previous)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_tflock_ticket_write_unlock(struct ck_tflock_ticket *lock)
{

	ck_pr_fence_unlock();
	ck_tflock_ticket_fca_32(&lock->completion, CK_TFLOCK_TICKET_WC_TOPMSK,
	    CK_TFLOCK_TICKET_WC_INCR);
	return;
}

CK_CC_INLINE static void
ck_tflock_ticket_read_lock(struct ck_tflock_ticket *lock)
{
	uint32_t previous;

	previous = ck_tflock_ticket_fca_32(&lock->request,
	    CK_TFLOCK_TICKET_RC_TOPMSK, CK_TFLOCK_TICKET_RC_INCR) &
	    CK_TFLOCK_TICKET_W_MASK;

	ck_pr_fence_atomic_load();

	while ((ck_pr_load_32(&lock->completion) &
	    CK_TFLOCK_TICKET_W_MASK) != previous) {
		ck_pr_stall();
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_tflock_ticket_read_unlock(struct ck_tflock_ticket *lock)
{

	ck_pr_fence_unlock();
	ck_tflock_ticket_fca_32(&lock->completion, CK_TFLOCK_TICKET_RC_TOPMSK,
	    CK_TFLOCK_TICKET_RC_INCR);
	return;
}

#endif /* CK_TFLOCK_TICKET_H */
