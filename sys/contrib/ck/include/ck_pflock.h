/*
 * Copyright 2013 John Wittrock.
 * Copyright 2013-2015 Samy Al Bahra.
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

#ifndef CK_PFLOCK_H
#define CK_PFLOCK_H

/*
 * This is an implementation of phase-fair locks derived from the work
 * described in:
 *    Brandenburg, B. and Anderson, J. 2010. Spin-Based
 *    Reader-Writer Synchronization for Multiprocessor Real-Time Systems
 */

#include <ck_cc.h>
#include <ck_pr.h>

struct ck_pflock {
	uint32_t rin;
	uint32_t rout;
	uint32_t win;
	uint32_t wout;
};
typedef struct ck_pflock ck_pflock_t;

#define CK_PFLOCK_LSB   0xFFFFFFF0
#define CK_PFLOCK_RINC  0x100		/* Reader increment value. */
#define CK_PFLOCK_WBITS 0x3		/* Writer bits in reader. */
#define CK_PFLOCK_PRES  0x2		/* Writer present bit. */
#define CK_PFLOCK_PHID  0x1		/* Phase ID bit. */

#define CK_PFLOCK_INITIALIZER {0, 0, 0, 0}

CK_CC_INLINE static void
ck_pflock_init(struct ck_pflock *pf)
{

	pf->rin = 0;
	pf->rout = 0;
	pf->win = 0;
	pf->wout = 0;
	ck_pr_barrier();

	return;
}

CK_CC_INLINE static void
ck_pflock_write_unlock(ck_pflock_t *pf)
{

	ck_pr_fence_unlock();

	/* Migrate from write phase to read phase. */
	ck_pr_and_32(&pf->rin, CK_PFLOCK_LSB);

	/* Allow other writers to continue. */
	ck_pr_faa_32(&pf->wout, 1);
	return;
}

CK_CC_INLINE static void
ck_pflock_write_lock(ck_pflock_t *pf)
{
	uint32_t ticket;

	/* Acquire ownership of write-phase. */
	ticket = ck_pr_faa_32(&pf->win, 1);
	while (ck_pr_load_32(&pf->wout) != ticket)
		ck_pr_stall();

	/*
	 * Acquire ticket on read-side in order to allow them
	 * to flush. Indicates to any incoming reader that a
	 * write-phase is pending.
	 */
	ticket = ck_pr_faa_32(&pf->rin,
	    (ticket & CK_PFLOCK_PHID) | CK_PFLOCK_PRES);

	/* Wait for any pending readers to flush. */
	while (ck_pr_load_32(&pf->rout) != ticket)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_pflock_read_unlock(ck_pflock_t *pf)
{

	ck_pr_fence_unlock();
	ck_pr_faa_32(&pf->rout, CK_PFLOCK_RINC);
	return;
}

CK_CC_INLINE static void
ck_pflock_read_lock(ck_pflock_t *pf)
{
	uint32_t w;

	/*
	 * If no writer is present, then the operation has completed
	 * successfully.
	 */
	w = ck_pr_faa_32(&pf->rin, CK_PFLOCK_RINC) & CK_PFLOCK_WBITS;
	if (w == 0)
		goto leave;

	/* Wait for current write phase to complete. */
	while ((ck_pr_load_32(&pf->rin) & CK_PFLOCK_WBITS) == w)
		ck_pr_stall();

leave:
	/* Acquire semantics with respect to readers. */
	ck_pr_fence_lock();
	return;
}

#endif /* CK_PFLOCK_H */
