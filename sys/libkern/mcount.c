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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/gmon.h>
#ifdef _KERNEL
#ifndef GUPROF
#include <sys/systm.h>
#endif
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#endif

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
	int delta;
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
	p->state = GMON_PROF_BUSY;
#endif

#ifdef _KERNEL
	/* De-relocate any addresses in a (single) trampoline. */
#ifdef MCOUNT_DETRAMP
	MCOUNT_DETRAMP(frompc);
	MCOUNT_DETRAMP(selfpc);
#endif
	/*
	 * When we are called from an exception handler, frompc may be
	 * a user address.  Convert such frompc's to some representation
	 * in kernel address space.
	 */
#ifdef MCOUNT_FROMPC_USER
	frompc = MCOUNT_FROMPC_USER(frompc);
#elif defined(MCOUNT_USERPC)
	/*
	 * For separate address spaces, we can only guess that addresses
	 * in the range known to us are actually kernel addresses.  Outside
	 * of this range, conerting to the user address is fail-safe.
	 */
	if (frompc < p->lowpc || frompc - p->lowpc >= p->textsize)
		frompc = MCOUNT_USERPC;
#endif
#endif /* _KERNEL */

	frompci = frompc - p->lowpc;
	if (frompci >= p->textsize)
		goto done;

#ifdef GUPROF
	if (p->state == GMON_PROF_HIRES) {
		/*
		 * Count the time since cputime() was previously called
		 * against `frompc'.  Compensate for overheads.
		 *
		 * cputime() sets its prev_count variable to the count when
		 * it is called.  This in effect starts a counter for
		 * the next period of execution (normally from now until 
		 * the next call to mcount() or mexitcount()).  We set
		 * cputime_bias to compensate for our own overhead.
		 *
		 * We use the usual sampling counters since they can be
		 * located efficiently.  4-byte counters are usually
		 * necessary.  gprof will add up the scattered counts
		 * just like it does for statistical profiling.  All
		 * counts are signed so that underflow in the subtractions
		 * doesn't matter much (negative counts are normally
		 * compensated for by larger counts elsewhere).  Underflow
		 * shouldn't occur, but may be caused by slightly wrong
		 * calibrations or from not clearing cputime_bias.
		 */
		delta = cputime() - cputime_bias - p->mcount_pre_overhead;
		cputime_bias = p->mcount_post_overhead;
		KCOUNT(p, frompci) += delta;
		*p->cputime_count += p->cputime_overhead;
		*p->mcount_count += p->mcount_overhead;
	}
#endif /* GUPROF */

#ifdef _KERNEL
	/*
	 * When we are called from an exception handler, frompc is faked
	 * to be for where the exception occurred.  We've just solidified
	 * the count for there.  Now convert frompci to an index that
	 * represents the kind of exception so that interruptions appear
	 * in the call graph as calls from those index instead of calls
	 * from all over.
	 */
	frompc = MCOUNT_FROMPC_INTR(selfpc);
	if ((frompc - p->lowpc) < p->textsize)
		frompci = frompc - p->lowpc;
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
	p->state = GMON_PROF_ON;
#endif
	return;
overflow:
	p->state = GMON_PROF_ERROR;
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
#ifdef MCOUNT_DETRAMP
	MCOUNT_DETRAMP(selfpc);
#endif
	selfpcdiff = selfpc - (uintfptr_t)p->lowpc;
	if (selfpcdiff < p->textsize) {
		int delta;

		/*
		 * Count the time since cputime() was previously called
		 * against `selfpc'.  Compensate for overheads.
		 */
		delta = cputime() - cputime_bias - p->mexitcount_pre_overhead;
		cputime_bias = p->mexitcount_post_overhead;
		KCOUNT(p, selfpcdiff) += delta;
		*p->cputime_count += p->cputime_overhead;
		*p->mexitcount_count += p->mexitcount_overhead;
	}
}

#ifndef __GNUCLIKE_ASM
#error "This file uses null asms to prevent timing loops being optimized away."
#endif

void
empty_loop(void)
{
	int i;

	for (i = 0; i < CALIB_SCALE; i++)
		__asm __volatile("");
}

void
nullfunc(void)
{
	__asm __volatile("");
}

void
nullfunc_loop(void)
{
	int i;

	for (i = 0; i < CALIB_SCALE; i++)
		nullfunc();
}
#endif /* GUPROF */
