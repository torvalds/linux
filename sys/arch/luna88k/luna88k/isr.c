/*	$OpenBSD: isr.c,v 1.12 2020/11/24 13:52:40 mpi Exp $	*/
/*	$NetBSD: isr.c,v 1.5 2000/07/09 08:08:20 nisimura Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Adam Glass, Gordon W. Ross, and Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Link and dispatch interrupts.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/vmmeter.h>
#include <sys/evcount.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>

#include <luna88k/luna88k/isr.h>

isr_autovec_list_t isr_autovec[NISRAUTOVEC];

void
isrinit()
{
	int i;

	/* Initialize the autovector lists. */
	for (i = 0; i < NISRAUTOVEC; ++i) {
		LIST_INIT(&isr_autovec[i]);
	}
}

/*
 * Establish an autovectored interrupt handler.
 * Called by driver attach functions.
 */
void
isrlink_autovec(int (*func)(void *), void *arg, int ipl, int priority,
    const char *name)
{
	struct isr_autovec *newisr, *curisr;
	isr_autovec_list_t *list;

#ifdef DIAGNOSTIC
	if (ipl < 0 || ipl >= NISRAUTOVEC)
		panic("isrlink_autovec: bad ipl %d", ipl);
#endif

	newisr = (struct isr_autovec *)malloc(sizeof(struct isr_autovec),
	    M_DEVBUF, M_NOWAIT);
	if (newisr == NULL)
		panic("isrlink_autovec: can't allocate space for isr");

	/* Fill in the new entry. */
	newisr->isr_func = func;
	newisr->isr_arg = arg;
	newisr->isr_ipl = ipl;
	newisr->isr_priority = priority;
	evcount_attach(&newisr->isr_count, name, &newisr->isr_ipl);

	/*
	 * Some devices are particularly sensitive to interrupt
	 * handling latency.  The SCC, for example, can lose many
	 * characters if its interrupt isn't handled with reasonable
	 * speed.
	 *
	 * To work around this problem, each device can give itself a
	 * "priority".  An unbuffered SCC would give itself a higher
	 * priority than a SCSI device, for example.
	 *
	 * This solution was originally developed for the hp300, which
	 * has a flat spl scheme (by necessity).  Thankfully, the
	 * MVME systems don't have this problem, though this may serve
	 * a useful purpose in any case.
	 */

	/*
	 * Get the appropriate ISR list.  If the list is empty, no
	 * additional work is necessary; we simply insert ourselves
	 * at the head of the list.
	 */
	list = &isr_autovec[ipl];
	if (LIST_EMPTY(list)) {
		LIST_INSERT_HEAD(list, newisr, isr_link);
		return;
	}

	/*
	 * A little extra work is required.  We traverse the list
	 * and place ourselves after any ISRs with our current (or
	 * higher) priority.
	 */
	for (curisr = LIST_FIRST(list); LIST_NEXT(curisr, isr_link) != NULL;
	    curisr = LIST_NEXT(curisr, isr_link)) {
		if (newisr->isr_priority > curisr->isr_priority) {
			LIST_INSERT_BEFORE(curisr, newisr, isr_link);
			return;
		}
	}

	/*
	 * We're the least important entry, it seems.  We just go
	 * on the end.
	 */
	LIST_INSERT_AFTER(curisr, newisr, isr_link);
}

/*
 * This is the dispatcher called by the low-level
 * assembly language autovectored interrupt routine.
 */
void
isrdispatch_autovec(int ipl)
{
	struct isr_autovec *isr;
	isr_autovec_list_t *list;
	int rc, handled = 0;
	static int straycount, unexpected;

#ifdef DIAGNOSTIC
	if (ipl < 0 || ipl >= NISRAUTOVEC)
		panic("isrdispatch_autovec: bad ipl %d", ipl);
#endif

	list = &isr_autovec[ipl];
	if (LIST_EMPTY(list)) {
		printf("isrdispatch_autovec: ipl %d unexpected\n", ipl);
		if (++unexpected > 10)
			panic("too many unexpected interrupts");
		return;
	}

	/* Give all the handlers a chance. */
	LIST_FOREACH(isr, list, isr_link) {
#ifdef MULTIPROCESSOR
		if (isr->isr_ipl < IPL_CLOCK)
			__mp_lock(&kernel_lock);
#endif
		rc = (*isr->isr_func)(isr->isr_arg);
#ifdef MULTIPROCESSOR
		if (isr->isr_ipl < IPL_CLOCK)
			__mp_unlock(&kernel_lock);
#endif
		if (rc != 0)
			isr->isr_count.ec_count++;
		handled |= rc;
	}

	if (handled)
		straycount = 0;
	else if (++straycount > 50)
		panic("isr_dispatch_autovec: too many stray interrupts");
	else
		printf("isrdispatch_autovec: stray level %d interrupt\n", ipl);
}
