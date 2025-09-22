/*	$OpenBSD: db_prof.c,v 1.5 2021/09/03 16:45:45 jasper Exp $	*/

/*-
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/exec_elf.h>
#include <sys/malloc.h>
#include <sys/gmon.h>

#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_sym.h>

#include "dt.h" /* for NDT */

#if NDT > 0
#include <dev/dt/dtvar.h>
#endif

extern int db_profile;			/* Allow dynamic profiling */
int db_prof_on;				/* Profiling state On/Off */

void dt_prov_kprobe_patch_all_entry(void);
void dt_prov_kprobe_depatch_all_entry(void);

vaddr_t db_get_probe_addr(struct trapframe *);
vaddr_t db_get_pc(struct trapframe *);

int
db_prof_enable(void)
{
#if NDT > 0
	if (!db_profile)
		return EPERM;

	dt_prov_kprobe_patch_all_entry();
	db_prof_on = 1;
	return 0;
#else
	return ENOENT;
#endif /* NDT > 0 */
}

void
db_prof_disable(void)
{
#if NDT > 0
	db_prof_on = 0;
	dt_prov_kprobe_depatch_all_entry();
#endif /* NDT > 0 */
}

/*
 * Equivalent to mcount(), must be called with interrupt disabled.
 */
void
db_prof_count(struct trapframe *frame)
{
	unsigned short *frompcindex;
	struct tostruct *top, *prevtop;
	struct gmonparam *p;
	long toindex;
	unsigned long frompc, selfpc;

	if ((p = curcpu()->ci_gmon) == NULL)
		return;

	/*
	 * check that we are profiling
	 * and that we aren't recursively invoked.
	 */
	if (p->state != GMON_PROF_ON)
		return;

	frompc = db_get_pc(frame);
	selfpc = db_get_probe_addr(frame);

	/*
	 * check that frompcindex is a reasonable pc value.
	 * for example:	signal catchers get called from the stack,
	 *		not from text space.  too bad.
	 */
	frompc -= p->lowpc;
	if (frompc > p->textsize)
		return;

#if (HASHFRACTION & (HASHFRACTION - 1)) == 0
	if (p->hashfraction == HASHFRACTION)
		frompcindex =
		    &p->froms[frompc / (HASHFRACTION * sizeof(*p->froms))];
	else
#endif
		frompcindex =
		    &p->froms[frompc / (p->hashfraction * sizeof(*p->froms))];
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
		return;
	}
	top = &p->tos[toindex];
	if (top->selfpc == selfpc) {
		/*
		 * arc at front of chain; usual case.
		 */
		top->count++;
		return;
	}
	/*
	 * have to go looking down chain for it.
	 * top points to what we are looking at,
	 * prevtop points to previous top.
	 * we know it is not at the head of the chain.
	 */
	for (; /* return */; ) {
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
			return;
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
			return;
		}
	}

overflow:
	p->state = GMON_PROF_ERROR;
}
