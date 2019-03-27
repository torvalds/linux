/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *
 *	@(#)subr_prof.c	8.3 (Berkeley) 9/23/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>

#ifdef GPROF
#include <sys/malloc.h>
#include <sys/gmon.h>
#undef MCOUNT

static MALLOC_DEFINE(M_GPROF, "gprof", "kernel profiling buffer");

static void kmstartup(void *);
SYSINIT(kmem, SI_SUB_KPROF, SI_ORDER_FIRST, kmstartup, NULL);

struct gmonparam _gmonparam = { GMON_PROF_OFF };

#ifdef GUPROF
void
nullfunc_loop_profiled()
{
	int i;

	for (i = 0; i < CALIB_SCALE; i++)
		nullfunc_profiled();
}

#define	nullfunc_loop_profiled_end	nullfunc_profiled	/* XXX */

void
nullfunc_profiled()
{
}
#endif /* GUPROF */

/*
 * Update the histograms to support extending the text region arbitrarily.
 * This is done slightly naively (no sparse regions), so will waste slight
 * amounts of memory, but will overall work nicely enough to allow profiling
 * of KLDs.
 */
void
kmupetext(uintfptr_t nhighpc)
{
	struct gmonparam np;	/* slightly large */
	struct gmonparam *p = &_gmonparam;
	char *cp;

	GIANT_REQUIRED;
	bcopy(p, &np, sizeof(*p));
	np.highpc = ROUNDUP(nhighpc, HISTFRACTION * sizeof(HISTCOUNTER));
	if (np.highpc <= p->highpc)
		return;
	np.textsize = np.highpc - p->lowpc;
	np.kcountsize = np.textsize / HISTFRACTION;
	np.hashfraction = HASHFRACTION;
	np.fromssize = np.textsize / HASHFRACTION;
	np.tolimit = np.textsize * ARCDENSITY / 100;
	if (np.tolimit < MINARCS)
		np.tolimit = MINARCS;
	else if (np.tolimit > MAXARCS)
		np.tolimit = MAXARCS;
	np.tossize = np.tolimit * sizeof(struct tostruct);
	cp = malloc(np.kcountsize + np.fromssize + np.tossize,
	    M_GPROF, M_WAITOK);
	/*
	 * Check for something else extending highpc while we slept.
	 */
	if (np.highpc <= p->highpc) {
		free(cp, M_GPROF);
		return;
	}
	np.tos = (struct tostruct *)cp;
	cp += np.tossize;
	np.kcount = (HISTCOUNTER *)cp;
	cp += np.kcountsize;
	np.froms = (u_short *)cp;
#ifdef GUPROF
	/* Reinitialize pointers to overhead counters. */
	np.cputime_count = &KCOUNT(&np, PC_TO_I(&np, cputime));
	np.mcount_count = &KCOUNT(&np, PC_TO_I(&np, mcount));
	np.mexitcount_count = &KCOUNT(&np, PC_TO_I(&np, mexitcount));
#endif
	critical_enter();
	bcopy(p->tos, np.tos, p->tossize);
	bzero((char *)np.tos + p->tossize, np.tossize - p->tossize);
	bcopy(p->kcount, np.kcount, p->kcountsize);
	bzero((char *)np.kcount + p->kcountsize, np.kcountsize -
	    p->kcountsize);
	bcopy(p->froms, np.froms, p->fromssize);
	bzero((char *)np.froms + p->fromssize, np.fromssize - p->fromssize);
	cp = (char *)p->tos;
	bcopy(&np, p, sizeof(*p));
	critical_exit();
	free(cp, M_GPROF);
}

static void
kmstartup(void *dummy)
{
	char *cp;
	struct gmonparam *p = &_gmonparam;
#ifdef GUPROF
	int cputime_overhead;
	int empty_loop_time;
	int i;
	int mcount_overhead;
	int mexitcount_overhead;
	int nullfunc_loop_overhead;
	int nullfunc_loop_profiled_time;
	uintfptr_t tmp_addr;
#endif

	/*
	 * Round lowpc and highpc to multiples of the density we're using
	 * so the rest of the scaling (here and in gprof) stays in ints.
	 */
	p->lowpc = ROUNDDOWN((u_long)btext, HISTFRACTION * sizeof(HISTCOUNTER));
	p->highpc = ROUNDUP((u_long)etext, HISTFRACTION * sizeof(HISTCOUNTER));
	p->textsize = p->highpc - p->lowpc;
	printf("Profiling kernel, textsize=%lu [%jx..%jx]\n",
	    p->textsize, (uintmax_t)p->lowpc, (uintmax_t)p->highpc);
	p->kcountsize = p->textsize / HISTFRACTION;
	p->hashfraction = HASHFRACTION;
	p->fromssize = p->textsize / HASHFRACTION;
	p->tolimit = p->textsize * ARCDENSITY / 100;
	if (p->tolimit < MINARCS)
		p->tolimit = MINARCS;
	else if (p->tolimit > MAXARCS)
		p->tolimit = MAXARCS;
	p->tossize = p->tolimit * sizeof(struct tostruct);
	cp = (char *)malloc(p->kcountsize + p->fromssize + p->tossize,
	    M_GPROF, M_WAITOK | M_ZERO);
	p->tos = (struct tostruct *)cp;
	cp += p->tossize;
	p->kcount = (HISTCOUNTER *)cp;
	cp += p->kcountsize;
	p->froms = (u_short *)cp;
	p->histcounter_type = FUNCTION_ALIGNMENT / HISTFRACTION * NBBY;

#ifdef GUPROF
	/* Signed counters. */
	p->histcounter_type = -p->histcounter_type;

	/* Initialize pointers to overhead counters. */
	p->cputime_count = &KCOUNT(p, PC_TO_I(p, cputime));
	p->mcount_count = &KCOUNT(p, PC_TO_I(p, mcount));
	p->mexitcount_count = &KCOUNT(p, PC_TO_I(p, mexitcount));

	/*
	 * Disable interrupts to avoid interference while we calibrate
	 * things.
	 */
	critical_enter();

	/*
	 * Determine overheads.
	 * XXX this needs to be repeated for each useful timer/counter.
	 */
	cputime_overhead = 0;
	startguprof(p);
	for (i = 0; i < CALIB_SCALE; i++)
		cputime_overhead += cputime();

	empty_loop();
	startguprof(p);
	empty_loop();
	empty_loop_time = cputime();

	nullfunc_loop_profiled();

	/*
	 * Start profiling.  There won't be any normal function calls since
	 * interrupts are disabled, but we will call the profiling routines
	 * directly to determine their overheads.
	 */
	p->state = GMON_PROF_HIRES;

	startguprof(p);
	nullfunc_loop_profiled();

	startguprof(p);
	for (i = 0; i < CALIB_SCALE; i++)
		MCOUNT_OVERHEAD(sys_profil);
	mcount_overhead = KCOUNT(p, PC_TO_I(p, sys_profil));

	startguprof(p);
	for (i = 0; i < CALIB_SCALE; i++)
		MEXITCOUNT_OVERHEAD();
	MEXITCOUNT_OVERHEAD_GETLABEL(tmp_addr);
	mexitcount_overhead = KCOUNT(p, PC_TO_I(p, tmp_addr));

	p->state = GMON_PROF_OFF;
	stopguprof(p);

	critical_exit();

	nullfunc_loop_profiled_time = 0;
	for (tmp_addr = (uintfptr_t)nullfunc_loop_profiled;
	     tmp_addr < (uintfptr_t)nullfunc_loop_profiled_end;
	     tmp_addr += HISTFRACTION * sizeof(HISTCOUNTER))
		nullfunc_loop_profiled_time += KCOUNT(p, PC_TO_I(p, tmp_addr));
#define CALIB_DOSCALE(count)	(((count) + CALIB_SCALE / 3) / CALIB_SCALE)
#define	c2n(count, freq)	((int)((count) * 1000000000LL / freq))
	printf("cputime %d, empty_loop %d, nullfunc_loop_profiled %d, mcount %d, mexitcount %d\n",
	       CALIB_DOSCALE(c2n(cputime_overhead, p->profrate)),
	       CALIB_DOSCALE(c2n(empty_loop_time, p->profrate)),
	       CALIB_DOSCALE(c2n(nullfunc_loop_profiled_time, p->profrate)),
	       CALIB_DOSCALE(c2n(mcount_overhead, p->profrate)),
	       CALIB_DOSCALE(c2n(mexitcount_overhead, p->profrate)));
	cputime_overhead -= empty_loop_time;
	mcount_overhead -= empty_loop_time;
	mexitcount_overhead -= empty_loop_time;

	/*-
	 * Profiling overheads are determined by the times between the
	 * following events:
	 *	MC1: mcount() is called
	 *	MC2: cputime() (called from mcount()) latches the timer
	 *	MC3: mcount() completes
	 *	ME1: mexitcount() is called
	 *	ME2: cputime() (called from mexitcount()) latches the timer
	 *	ME3: mexitcount() completes.
	 * The times between the events vary slightly depending on instruction
	 * combination and cache misses, etc.  Attempt to determine the
	 * minimum times.  These can be subtracted from the profiling times
	 * without much risk of reducing the profiling times below what they
	 * would be when profiling is not configured.  Abbreviate:
	 *	ab = minimum time between MC1 and MC3
	 *	a  = minimum time between MC1 and MC2
	 *	b  = minimum time between MC2 and MC3
	 *	cd = minimum time between ME1 and ME3
	 *	c  = minimum time between ME1 and ME2
	 *	d  = minimum time between ME2 and ME3.
	 * These satisfy the relations:
	 *	ab            <= mcount_overhead		(just measured)
	 *	a + b         <= ab
	 *	        cd    <= mexitcount_overhead		(just measured)
	 *	        c + d <= cd
	 *	a         + d <= nullfunc_loop_profiled_time	(just measured)
	 *	a >= 0, b >= 0, c >= 0, d >= 0.
	 * Assume that ab and cd are equal to the minimums.
	 */
	p->cputime_overhead = CALIB_DOSCALE(cputime_overhead);
	p->mcount_overhead = CALIB_DOSCALE(mcount_overhead - cputime_overhead);
	p->mexitcount_overhead = CALIB_DOSCALE(mexitcount_overhead
					       - cputime_overhead);
	nullfunc_loop_overhead = nullfunc_loop_profiled_time - empty_loop_time;
	p->mexitcount_post_overhead = CALIB_DOSCALE((mcount_overhead
						     - nullfunc_loop_overhead)
						    / 4);
	p->mexitcount_pre_overhead = p->mexitcount_overhead
				     + p->cputime_overhead
				     - p->mexitcount_post_overhead;
	p->mcount_pre_overhead = CALIB_DOSCALE(nullfunc_loop_overhead)
				 - p->mexitcount_post_overhead;
	p->mcount_post_overhead = p->mcount_overhead
				  + p->cputime_overhead
				  - p->mcount_pre_overhead;
	printf(
"Profiling overheads: mcount: %d+%d, %d+%d; mexitcount: %d+%d, %d+%d nsec\n",
	       c2n(p->cputime_overhead, p->profrate),
	       c2n(p->mcount_overhead, p->profrate),
	       c2n(p->mcount_pre_overhead, p->profrate),
	       c2n(p->mcount_post_overhead, p->profrate),
	       c2n(p->cputime_overhead, p->profrate),
	       c2n(p->mexitcount_overhead, p->profrate),
	       c2n(p->mexitcount_pre_overhead, p->profrate),
	       c2n(p->mexitcount_post_overhead, p->profrate));
	printf(
"Profiling overheads: mcount: %d+%d, %d+%d; mexitcount: %d+%d, %d+%d cycles\n",
	       p->cputime_overhead, p->mcount_overhead,
	       p->mcount_pre_overhead, p->mcount_post_overhead,
	       p->cputime_overhead, p->mexitcount_overhead,
	       p->mexitcount_pre_overhead, p->mexitcount_post_overhead);
#endif /* GUPROF */
}

/*
 * Return kernel profiling information.
 */
static int
sysctl_kern_prof(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *) arg1;
	u_int namelen = arg2;
	struct gmonparam *gp = &_gmonparam;
	int error;
	int state;

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case GPROF_STATE:
		state = gp->state;
		error = sysctl_handle_int(oidp, &state, 0, req);
		if (error)
			return (error);
		if (!req->newptr)
			return (0);
		if (state == GMON_PROF_OFF) {
			gp->state = state;
			PROC_LOCK(&proc0);
			stopprofclock(&proc0);
			PROC_UNLOCK(&proc0);
			stopguprof(gp);
		} else if (state == GMON_PROF_ON) {
			gp->state = GMON_PROF_OFF;
			stopguprof(gp);
			gp->profrate = profhz;
			PROC_LOCK(&proc0);
			startprofclock(&proc0);
			PROC_UNLOCK(&proc0);
			gp->state = state;
#ifdef GUPROF
		} else if (state == GMON_PROF_HIRES) {
			gp->state = GMON_PROF_OFF;
			PROC_LOCK(&proc0);
			stopprofclock(&proc0);
			PROC_UNLOCK(&proc0);
			startguprof(gp);
			gp->state = state;
#endif
		} else if (state != gp->state)
			return (EINVAL);
		return (0);
	case GPROF_COUNT:
		return (sysctl_handle_opaque(oidp, 
			gp->kcount, gp->kcountsize, req));
	case GPROF_FROMS:
		return (sysctl_handle_opaque(oidp, 
			gp->froms, gp->fromssize, req));
	case GPROF_TOS:
		return (sysctl_handle_opaque(oidp, 
			gp->tos, gp->tossize, req));
	case GPROF_GMONPARAM:
		return (sysctl_handle_opaque(oidp, gp, sizeof *gp, req));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

static SYSCTL_NODE(_kern, KERN_PROF, prof, CTLFLAG_RW, sysctl_kern_prof, "");
#endif /* GPROF */

/*
 * Profiling system call.
 *
 * The scale factor is a fixed point number with 16 bits of fraction, so that
 * 1.0 is represented as 0x10000.  A scale factor of 0 turns off profiling.
 */
#ifndef _SYS_SYSPROTO_H_
struct profil_args {
	caddr_t	samples;
	size_t	size;
	size_t	offset;
	u_int	scale;
};
#endif
/* ARGSUSED */
int
sys_profil(struct thread *td, struct profil_args *uap)
{
	struct uprof *upp;
	struct proc *p;

	if (uap->scale > (1 << 16))
		return (EINVAL);

	p = td->td_proc;
	if (uap->scale == 0) {
		PROC_LOCK(p);
		stopprofclock(p);
		PROC_UNLOCK(p);
		return (0);
	}
	PROC_LOCK(p);
	upp = &td->td_proc->p_stats->p_prof;
	PROC_PROFLOCK(p);
	upp->pr_off = uap->offset;
	upp->pr_scale = uap->scale;
	upp->pr_base = uap->samples;
	upp->pr_size = uap->size;
	PROC_PROFUNLOCK(p);
	startprofclock(p);
	PROC_UNLOCK(p);

	return (0);
}

/*
 * Scale is a fixed-point number with the binary point 16 bits
 * into the value, and is <= 1.0.  pc is at most 32 bits, so the
 * intermediate result is at most 48 bits.
 */
#define	PC_TO_INDEX(pc, prof) \
	((int)(((u_quad_t)((pc) - (prof)->pr_off) * \
	    (u_quad_t)((prof)->pr_scale)) >> 16) & ~1)

/*
 * Collect user-level profiling statistics; called on a profiling tick,
 * when a process is running in user-mode.  This routine may be called
 * from an interrupt context.  We perform the update with an AST
 * that will vector us to trap() with a context in which copyin and
 * copyout will work.  Trap will then call addupc_task().
 *
 * Note that we may (rarely) not get around to the AST soon enough, and
 * lose profile ticks when the next tick overwrites this one, but in this
 * case the system is overloaded and the profile is probably already
 * inaccurate.
 */
void
addupc_intr(struct thread *td, uintfptr_t pc, u_int ticks)
{
	struct uprof *prof;

	if (ticks == 0)
		return;
	prof = &td->td_proc->p_stats->p_prof;
	PROC_PROFLOCK(td->td_proc);
	if (pc < prof->pr_off || PC_TO_INDEX(pc, prof) >= prof->pr_size) {
		PROC_PROFUNLOCK(td->td_proc);
		return;			/* out of range; ignore */
	}

	PROC_PROFUNLOCK(td->td_proc);
	td->td_profil_addr = pc;
	td->td_profil_ticks = ticks;
	td->td_pflags |= TDP_OWEUPC;
	thread_lock(td);
	td->td_flags |= TDF_ASTPENDING;
	thread_unlock(td);
}

/*
 * Actually update the profiling statistics.  If the update fails, we
 * simply turn off profiling.
 */
void
addupc_task(struct thread *td, uintfptr_t pc, u_int ticks)
{
	struct proc *p = td->td_proc; 
	struct uprof *prof;
	caddr_t addr;
	u_int i;
	u_short v;
	int stop = 0;

	if (ticks == 0)
		return;

	PROC_LOCK(p);
	if (!(p->p_flag & P_PROFIL)) {
		PROC_UNLOCK(p);
		return;
	}
	p->p_profthreads++;
	prof = &p->p_stats->p_prof;
	PROC_PROFLOCK(p);
	if (pc < prof->pr_off ||
	    (i = PC_TO_INDEX(pc, prof)) >= prof->pr_size) {
		PROC_PROFUNLOCK(p);
		goto out;
	}

	addr = prof->pr_base + i;
	PROC_PROFUNLOCK(p);
	PROC_UNLOCK(p);
	if (copyin(addr, &v, sizeof(v)) == 0) {
		v += ticks;
		if (copyout(&v, addr, sizeof(v)) == 0) {
			PROC_LOCK(p);
			goto out;
		}
	}
	stop = 1;
	PROC_LOCK(p);

out:
	if (--p->p_profthreads == 0) {
		if (p->p_flag & P_STOPPROF) {
			wakeup(&p->p_profthreads);
			p->p_flag &= ~P_STOPPROF;
			stop = 0;
		}
	}
	if (stop)
		stopprofclock(p);
	PROC_UNLOCK(p);
}
