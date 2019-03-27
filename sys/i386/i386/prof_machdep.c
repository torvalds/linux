/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 Bruce D. Evans.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef GUPROF
#include "opt_i586_guprof.h"
#include "opt_perfmon.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/eventhandler.h>
#include <sys/gmon.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <machine/clock.h>
#include <machine/perfmon.h>
#include <machine/timerreg.h>

#define	CPUTIME_CLOCK_UNINITIALIZED	0
#define	CPUTIME_CLOCK_I8254		1
#define	CPUTIME_CLOCK_TSC		2
#define	CPUTIME_CLOCK_I586_PMC		3
#define	CPUTIME_CLOCK_I8254_SHIFT	7

int	cputime_bias = 1;	/* initialize for locality of reference */

static int	cputime_clock = CPUTIME_CLOCK_UNINITIALIZED;
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
static u_int	cputime_clock_pmc_conf = I586_PMC_GUPROF;
static int	cputime_clock_pmc_init;
static struct gmonparam saved_gmp;
#endif
#if defined(I586_CPU) || defined(I686_CPU)
static int	cputime_prof_active;
#endif
#endif /* GUPROF */

#ifdef __GNUCLIKE_ASM
#if defined(SMP) && defined(GUPROF)
#define	MPLOCK "						\n\
	movl	$1,%edx						\n\
9:								\n\
	xorl	%eax,%eax					\n\
	lock 							\n\
	cmpxchgl %edx,mcount_lock				\n\
	jne	9b						\n"
#define	MPUNLOCK "movl	$0,mcount_lock				\n"
#else /* !(SMP && GUPROF) */
#define	MPLOCK
#define	MPUNLOCK
#endif /* SMP && GUPROF */

__asm("								\n\
GM_STATE	=	0					\n\
GMON_PROF_OFF	=	3					\n\
								\n\
	.text							\n\
	.p2align 4,0x90						\n\
	.globl	__mcount					\n\
	.type	__mcount,@function				\n\
__mcount:							\n\
	#							\n\
	# Check that we are profiling.  Do it early for speed.	\n\
	#							\n\
	cmpl	$GMON_PROF_OFF,_gmonparam+GM_STATE		\n\
	je	.mcount_exit					\n\
	#							\n\
	# __mcount is the same as [.]mcount except the caller	\n\
	# hasn't changed the stack except to call here, so the	\n\
	# caller's raddr is above our raddr.			\n\
	#							\n\
	movl	4(%esp),%edx					\n\
	jmp	.got_frompc					\n\
								\n\
	.p2align 4,0x90						\n\
	.globl	.mcount						\n\
.mcount:							\n\
	cmpl	$GMON_PROF_OFF,_gmonparam+GM_STATE		\n\
	je	.mcount_exit					\n\
	#							\n\
	# The caller's stack frame has already been built, so	\n\
	# %ebp is the caller's frame pointer.  The caller's	\n\
	# raddr is in the caller's frame following the caller's	\n\
	# caller's frame pointer.				\n\
	#							\n\
	movl	4(%ebp),%edx					\n\
.got_frompc:							\n\
	#							\n\
	# Our raddr is the caller's pc.				\n\
	#							\n\
	movl	(%esp),%eax					\n\
								\n\
	pushfl							\n\
	pushl	%eax						\n\
	pushl	%edx						\n\
	cli							\n"
	MPLOCK "						\n\
	call	mcount						\n"
	MPUNLOCK "						\n\
	addl	$8,%esp						\n\
	popfl							\n\
.mcount_exit:							\n\
	ret	$0						\n\
");

void	__mcount(void);
void	(*__mcountp)(void) = __mcount;
#else /* !__GNUCLIKE_ASM */
#error "this file needs to be ported to your compiler"
#endif /* __GNUCLIKE_ASM */

#ifdef GUPROF
/*
 * [.]mexitcount saves the return register(s), loads selfpc and calls
 * mexitcount(selfpc) to do the work.  Someday it should be in a machine
 * dependent file together with cputime(), __mcount and [.]mcount.  cputime()
 * can't just be put in machdep.c because it has to be compiled without -pg.
 */
#ifdef __GNUCLIKE_ASM
__asm("								\n\
	.text							\n\
#								\n\
# Dummy label to be seen when gprof -u hides [.]mexitcount.	\n\
#								\n\
	.p2align 4,0x90						\n\
	.globl	__mexitcount					\n\
	.type	__mexitcount,@function				\n\
__mexitcount:							\n\
	nop							\n\
								\n\
GMON_PROF_HIRES	=	4					\n\
								\n\
	.p2align 4,0x90						\n\
	.globl	.mexitcount					\n\
.mexitcount:							\n\
	cmpl	$GMON_PROF_HIRES,_gmonparam+GM_STATE		\n\
	jne	.mexitcount_exit				\n\
	pushl	%edx						\n\
	pushl	%eax						\n\
	movl	8(%esp),%eax					\n\
	pushfl							\n\
	pushl	%eax						\n\
	cli							\n"
	MPLOCK "						\n\
	call	mexitcount					\n"
	MPUNLOCK "						\n\
	addl	$4,%esp						\n\
	popfl							\n\
	popl	%eax						\n\
	popl	%edx						\n\
.mexitcount_exit:						\n\
	ret	$0						\n\
");
#endif /* __GNUCLIKE_ASM */

void	__mexitcount(void);
void	(*__mexitcountp)(void) = __mexitcount;

/*
 * Return the time elapsed since the last call.  The units are machine-
 * dependent.
 */
int
cputime()
{
	u_int count;
	int delta;
#if (defined(I586_CPU) || defined(I686_CPU)) && \
    defined(PERFMON) && defined(I586_PMC_GUPROF) && !defined(SMP)
	u_quad_t event_count;
#endif
	u_char high, low;
	static u_int prev_count;

#if defined(I586_CPU) || defined(I686_CPU)
	if (cputime_clock == CPUTIME_CLOCK_TSC) {
		/*
		 * Scale the TSC a little to make cputime()'s frequency
		 * fit in an int, assuming that the TSC frequency fits
		 * in a u_int.  Use a fixed scale since dynamic scaling
		 * would be slower and we can't really use the low bit
		 * of precision.
		 */
		count = (u_int)rdtsc() & ~1u;
		delta = (int)(count - prev_count) >> 1;
		prev_count = count;
		return (delta);
	}
#if defined(PERFMON) && defined(I586_PMC_GUPROF) && !defined(SMP)
	if (cputime_clock == CPUTIME_CLOCK_I586_PMC) {
		/*
		 * XXX permon_read() should be inlined so that the
		 * perfmon module doesn't need to be compiled with
		 * profiling disabled and so that it is fast.
		 */
		perfmon_read(0, &event_count);

		count = (u_int)event_count;
		delta = (int)(count - prev_count);
		prev_count = count;
		return (delta);
	}
#endif /* PERFMON && I586_PMC_GUPROF && !SMP */
#endif /* I586_CPU || I686_CPU */

	/*
	 * Read the current value of the 8254 timer counter 0.
	 */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	low = inb(TIMER_CNTR0);
	high = inb(TIMER_CNTR0);
	count = ((high << 8) | low) << CPUTIME_CLOCK_I8254_SHIFT;

	/*
	 * The timer counts down from TIMER_CNTR0_MAX to 0 and then resets.
	 * While profiling is enabled, this routine is called at least twice
	 * per timer reset (for mcounting and mexitcounting hardclock()),
	 * so at most one reset has occurred since the last call, and one
	 * has occurred iff the current count is larger than the previous
	 * count.  This allows counter underflow to be detected faster
	 * than in microtime().
	 */
	delta = prev_count - count;
	prev_count = count;
	if ((int) delta <= 0)
		return (delta + (i8254_max_count << CPUTIME_CLOCK_I8254_SHIFT));
	return (delta);
}

static int
sysctl_machdep_cputime_clock(SYSCTL_HANDLER_ARGS)
{
	int clock;
	int error;
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
	int event;
	struct pmc pmc;
#endif

	clock = cputime_clock;
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
	if (clock == CPUTIME_CLOCK_I586_PMC) {
		pmc.pmc_val = cputime_clock_pmc_conf;
		clock += pmc.pmc_event;
	}
#endif
	error = sysctl_handle_opaque(oidp, &clock, sizeof clock, req);
	if (error == 0 && req->newptr != NULL) {
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
		if (clock >= CPUTIME_CLOCK_I586_PMC) {
			event = clock - CPUTIME_CLOCK_I586_PMC;
			if (event >= 256)
				return (EINVAL);
			pmc.pmc_num = 0;
			pmc.pmc_event = event;
			pmc.pmc_unit = 0;
			pmc.pmc_flags = PMCF_E | PMCF_OS | PMCF_USR;
			pmc.pmc_mask = 0;
			cputime_clock_pmc_conf = pmc.pmc_val;
			cputime_clock = CPUTIME_CLOCK_I586_PMC;
		} else
#endif
		{
			if (clock < 0 || clock >= CPUTIME_CLOCK_I586_PMC)
				return (EINVAL);
			cputime_clock = clock;
		}
	}
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, cputime_clock, CTLTYPE_INT | CTLFLAG_RW,
	    0, sizeof(u_int), sysctl_machdep_cputime_clock, "I", "");

/*
 * The start and stop routines need not be here since we turn off profiling
 * before calling them.  They are here for convenience.
 */

void
startguprof(gp)
	struct gmonparam *gp;
{
#if defined(I586_CPU) || defined(I686_CPU)
	uint64_t freq;

	freq = atomic_load_acq_64(&tsc_freq);
	if (cputime_clock == CPUTIME_CLOCK_UNINITIALIZED) {
		if (freq != 0 && mp_ncpus == 1)
			cputime_clock = CPUTIME_CLOCK_TSC;
		else
			cputime_clock = CPUTIME_CLOCK_I8254;
	}
	if (cputime_clock == CPUTIME_CLOCK_TSC) {
		gp->profrate = freq >> 1;
		cputime_prof_active = 1;
	} else
		gp->profrate = i8254_freq << CPUTIME_CLOCK_I8254_SHIFT;
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
	if (cputime_clock == CPUTIME_CLOCK_I586_PMC) {
		if (perfmon_avail() &&
		    perfmon_setup(0, cputime_clock_pmc_conf) == 0) {
			if (perfmon_start(0) != 0)
				perfmon_fini(0);
			else {
				/* XXX 1 event == 1 us. */
				gp->profrate = 1000000;

				saved_gmp = *gp;

				/* Zap overheads.  They are invalid. */
				gp->cputime_overhead = 0;
				gp->mcount_overhead = 0;
				gp->mcount_post_overhead = 0;
				gp->mcount_pre_overhead = 0;
				gp->mexitcount_overhead = 0;
				gp->mexitcount_post_overhead = 0;
				gp->mexitcount_pre_overhead = 0;

				cputime_clock_pmc_init = TRUE;
			}
		}
	}
#endif /* PERFMON && I586_PMC_GUPROF */
#else /* !(I586_CPU || I686_CPU) */
	if (cputime_clock == CPUTIME_CLOCK_UNINITIALIZED)
		cputime_clock = CPUTIME_CLOCK_I8254;
	gp->profrate = i8254_freq << CPUTIME_CLOCK_I8254_SHIFT;
#endif /* I586_CPU || I686_CPU */
	cputime_bias = 0;
	cputime();
}

void
stopguprof(gp)
	struct gmonparam *gp;
{
#if defined(PERFMON) && defined(I586_PMC_GUPROF)
	if (cputime_clock_pmc_init) {
		*gp = saved_gmp;
		perfmon_fini(0);
		cputime_clock_pmc_init = FALSE;
	}
#endif
#if defined(I586_CPU) || defined(I686_CPU)
	if (cputime_clock == CPUTIME_CLOCK_TSC)
		cputime_prof_active = 0;
#endif
}

#if defined(I586_CPU) || defined(I686_CPU)
/* If the cpu frequency changed while profiling, report a warning. */
static void
tsc_freq_changed(void *arg, const struct cf_level *level, int status)
{

	/*
	 * If there was an error during the transition or
	 * TSC is P-state invariant, don't do anything.
	 */
	if (status != 0 || tsc_is_invariant)
		return;
	if (cputime_prof_active && cputime_clock == CPUTIME_CLOCK_TSC)
		printf("warning: cpu freq changed while profiling active\n");
}

EVENTHANDLER_DEFINE(cpufreq_post_change, tsc_freq_changed, NULL,
    EVENTHANDLER_PRI_ANY);
#endif /* I586_CPU || I686_CPU */

#endif /* GUPROF */
