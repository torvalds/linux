/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if !defined(_KERNEL) && defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)mcount.c	8.1 (Berkeley) 6/4/93";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/gmon.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
void	bintr(void);
void	btrap(void);
void	eintr(void);
void	user(void);
#endif
#include <machine/atomic.h>

/*
 * mcount is called on entry to each function compiled with the profiling
 * switch set.  _mcount(), which is declared in a machine-dependent way
 * with _MCOUNT_DECL, does the actual work and is either inlined into a
 * C routine or called by an assembly stub.  In any case, this magic is
 * taken care of by the MCOUNT definition in <machine/profile.h>.
 *
 * _mcount updates data structures that represent traversals of the
 * program's call graph edges.  frompc and selfpc are the return
 * address and function address that represents the given call graph edge.
 *
 * Note: the original BSD code used the same variable (frompcindex) for
 * both frompcindex and frompc.  Any reasonable, modern compiler will
 * perform this optimization.
 */
/* _mcount; may be static, inline, etc */
_MCOUNT_DECL(uintfptr_t frompc, uintfptr_t selfpc)
{
#ifdef GUPROF
	u_int delta;
#endif
	fptrdiff_t frompci;
	u_short *frompcindex;
	struct tostruct *top, *prevtop;
	struct gmonparam *p;
	long toindex;
#ifdef _KERNEL
	MCOUNT_DECL(s)
#endif

	p = &_gmonparam;
#ifndef GUPROF			/* XXX */
	/*
	 * check that we are profiling
	 * and that we aren't recursively invoked.
	 */
	if (p->state != GMON_PROF_ON)
		return;
#endif
#ifdef _KERNEL
	MCOUNT_ENTER(s);
#else
	if (!atomic_cmpset_acq_int(&p->state, GMON_PROF_ON, GMON_PROF_BUSY))
		return;
#endif
	frompci = frompc - p->lowpc;

#ifdef _KERNEL
	/*
	 * When we are called from an exception handler, frompci may be
	 * for a user address.  Convert such frompci's to the index of
	 * user() to merge all user counts.
	 */
	if (frompci >= p->textsize) {
		if (frompci + p->lowpc
		    >= (uintfptr_t)(VM_MAXUSER_ADDRESS + UPAGES * PAGE_SIZE))
			goto done;
		frompci = (uintfptr_t)user - p->lowpc;
		if (frompci >= p->textsize)
		    goto done;
	}
#endif

#ifdef GUPROF
	if (p->state != GMON_PROF_HIRES)
		goto skip_guprof_stuff;
	/*
	 * Look at the clock and add the count of clock cycles since the
	 * clock was last looked at to a counter for frompc.  This
	 * solidifies the count for the function containing frompc and
	 * effectively starts another clock for the current function.
	 * The count for the new clock will be solidified when another
	 * function call is made or the function returns.
	 *
	 * We use the usual sampling counters since they can be located
	 * efficiently.  4-byte counters are usually necessary.
	 *
	 * There are many complications for subtracting the profiling
	 * overheads from the counts for normal functions and adding
	 * them to the counts for mcount(), mexitcount() and cputime().
	 * We attempt to handle fractional cycles, but the overheads
	 * are usually underestimated because they are calibrated for
	 * a simpler than usual setup.
	 */
	delta = cputime() - p->mcount_overhead;
	p->cputime_overhead_resid += p->cputime_overhead_frac;
	p->mcount_overhead_resid += p->mcount_overhead_frac;
	if ((int)delta < 0)
		*p->mcount_count += delta + p->mcount_overhead
				    - p->cputime_overhead;
	else if (delta != 0) {
		if (p->cputime_overhead_resid >= CALIB_SCALE) {
			p->cputime_overhead_resid -= CALIB_SCALE;
			++*p->cputime_count;
			--delta;
		}
		if (delta != 0) {
			if (p->mcount_overhead_resid >= CALIB_SCALE) {
				p->mcount_overhead_resid -= CALIB_SCALE;
				++*p->mcount_count;
				--delta;
			}
			KCOUNT(p, frompci) += delta;
		}
		*p->mcount_count += p->mcount_overhead_sub;
	}
	*p->cputime_count += p->cputime_overhead;
skip_guprof_stuff:
#endif /* GUPROF */

#ifdef _KERNEL
	/*
	 * When we are called from an exception handler, frompc is faked
	 * to be for where the exception occurred.  We've just solidified
	 * the count for there.  Now convert frompci to the index of btrap()
	 * for trap handlers and bintr() for interrupt handlers to make
	 * exceptions appear in the call graph as calls from btrap() and
	 * bintr() instead of calls from all over.
	 */
	if ((uintfptr_t)selfpc >= (uintfptr_t)btrap
	    && (uintfptr_t)selfpc < (uintfptr_t)eintr) {
		if ((uintfptr_t)selfpc >= (uintfptr_t)bintr)
			frompci = (uintfptr_t)bintr - p->lowpc;
		else
			frompci = (uintfptr_t)btrap - p->lowpc;
	}
#endif

	/*
	 * check that frompc is a reasonable pc value.
	 * for example:	signal catchers get called from the stack,
	 *		not from text space.  too bad.
	 */
	if (frompci >= p->textsize)
		goto done;

	frompcindex = &p->froms[frompci / (p->hashfraction * sizeof(*p->froms))];
	toindex = *frompcindex;
	if (toindex == 0) {
		/*
		 *	first time traversing this arc
		 */
		toindex = ++p->tos[0].link;
		if (toindex >= p->tolimit)
			/* halt further profiling */
			goto overflow;

		*frompcindex = toindex;
		top = &p->tos[toindex];
		top->selfpc = selfpc;
		top->count = 1;
		top->link = 0;
		goto done;
	}
	top = &p->tos[toindex];
	if (top->selfpc == selfpc) {
		/*
		 * arc at front of chain; usual case.
		 */
		top->count++;
		goto done;
	}
	/*
	 * have to go looking down chain for it.
	 * top points to what we are looking at,
	 * prevtop points to previous top.
	 * we know it is not at the head of the chain.
	 */
	for (; /* goto done */; ) {
		if (top->link == 0) {
			/*
			 * top is end of the chain and none of the chain
			 * had top->selfpc == selfpc.
			 * so we allocate a new tostruct
			 * and link it to the head of the chain.
			 */
			toindex = ++p->tos[0].link;
			if (toindex >= p->tolimit)
				goto overflow;

			top = &p->tos[toindex];
			top->selfpc = selfpc;
			top->count = 1;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}
		/*
		 * otherwise, check the next arc on the chain.
		 */
		prevtop = top;
		top = &p->tos[top->link];
		if (top->selfpc == selfpc) {
			/*
			 * there it is.
			 * increment its count
			 * move it to the head of the chain.
			 */
			top->count++;
			toindex = prevtop->link;
			prevtop->link = top->link;
			top->link = *frompcindex;
			*frompcindex = toindex;
			goto done;
		}

	}
done:
#ifdef _KERNEL
	MCOUNT_EXIT(s);
#else
	atomic_store_rel_int(&p->state, GMON_PROF_ON);
#endif
	return;
overflow:
	atomic_store_rel_int(&p->state, GMON_PROF_ERROR);
#ifdef _KERNEL
	MCOUNT_EXIT(s);
#endif
	return;
}

/*
 * Actual definition of mcount function.  Defined in <machine/profile.h>,
 * which is included by <sys/gmon.h>.
 */
MCOUNT

#ifdef GUPROF
void
mexitcount(uintfptr_t selfpc)
{
	struct gmonparam *p;
	uintfptr_t selfpcdiff;

	p = &_gmonparam;
	selfpcdiff = selfpc - (uintfptr_t)p->lowpc;
	if (selfpcdiff < p->textsize) {
		u_int delta;

		/*
		 * Solidify the count for the current function.
		 */
		delta = cputime() - p->mexitcount_overhead;
		p->cputime_overhead_resid += p->cputime_overhead_frac;
		p->mexitcount_overhead_resid += p->mexitcount_overhead_frac;
		if ((int)delta < 0)
			*p->mexitcount_count += delta + p->mexitcount_overhead
						- p->cputime_overhead;
		else if (delta != 0) {
			if (p->cputime_overhead_resid >= CALIB_SCALE) {
				p->cputime_overhead_resid -= CALIB_SCALE;
				++*p->cputime_count;
				--delta;
			}
			if (delta != 0) {
				if (p->mexitcount_overhead_resid
				    >= CALIB_SCALE) {
					p->mexitcount_overhead_resid
					    -= CALIB_SCALE;
					++*p->mexitcount_count;
					--delta;
				}
				KCOUNT(p, selfpcdiff) += delta;
			}
			*p->mexitcount_count += p->mexitcount_overhead_sub;
		}
		*p->cputime_count += p->cputime_overhead;
	}
}
#endif /* GUPROF */
