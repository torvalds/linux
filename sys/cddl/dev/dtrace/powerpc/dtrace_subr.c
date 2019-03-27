/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 *
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/smp.h>
#include <sys/dtrace_impl.h>
#include <sys/dtrace_bsd.h>
#include <machine/clock.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <vm/pmap.h>

#define	DELAYBRANCH(x)	((int)(x) < 0)
		
extern dtrace_id_t	dtrace_probeid_error;
extern int (*dtrace_invop_jump_addr)(struct trapframe *);

extern void dtrace_getnanotime(struct timespec *tsp);

int dtrace_invop(uintptr_t, struct trapframe *, uintptr_t);
void dtrace_invop_init(void);
void dtrace_invop_uninit(void);

typedef struct dtrace_invop_hdlr {
	int (*dtih_func)(uintptr_t, struct trapframe *, uintptr_t);
	struct dtrace_invop_hdlr *dtih_next;
} dtrace_invop_hdlr_t;

dtrace_invop_hdlr_t *dtrace_invop_hdlr;

int
dtrace_invop(uintptr_t addr, struct trapframe *frame, uintptr_t arg0)
{
	dtrace_invop_hdlr_t *hdlr;
	int rval;

	for (hdlr = dtrace_invop_hdlr; hdlr != NULL; hdlr = hdlr->dtih_next)
		if ((rval = hdlr->dtih_func(addr, frame, arg0)) != 0)
			return (rval);

	return (0);
}

void
dtrace_invop_add(int (*func)(uintptr_t, struct trapframe *, uintptr_t))
{
	dtrace_invop_hdlr_t *hdlr;

	hdlr = kmem_alloc(sizeof (dtrace_invop_hdlr_t), KM_SLEEP);
	hdlr->dtih_func = func;
	hdlr->dtih_next = dtrace_invop_hdlr;
	dtrace_invop_hdlr = hdlr;
}

void
dtrace_invop_remove(int (*func)(uintptr_t, struct trapframe *, uintptr_t))
{
	dtrace_invop_hdlr_t *hdlr = dtrace_invop_hdlr, *prev = NULL;

	for (;;) {
		if (hdlr == NULL)
			panic("attempt to remove non-existent invop handler");

		if (hdlr->dtih_func == func)
			break;

		prev = hdlr;
		hdlr = hdlr->dtih_next;
	}

	if (prev == NULL) {
		ASSERT(dtrace_invop_hdlr == hdlr);
		dtrace_invop_hdlr = hdlr->dtih_next;
	} else {
		ASSERT(dtrace_invop_hdlr != hdlr);
		prev->dtih_next = hdlr->dtih_next;
	}

	kmem_free(hdlr, 0);
}


/*ARGSUSED*/
void
dtrace_toxic_ranges(void (*func)(uintptr_t base, uintptr_t limit))
{
	/*
	 * No toxic regions?
	 */
}

void
dtrace_xcall(processorid_t cpu, dtrace_xcall_t func, void *arg)
{
	cpuset_t cpus;

	if (cpu == DTRACE_CPUALL)
		cpus = all_cpus;
	else
		CPU_SETOF(cpu, &cpus);

	smp_rendezvous_cpus(cpus, smp_no_rendezvous_barrier, func,
			smp_no_rendezvous_barrier, arg);
}

static void
dtrace_sync_func(void)
{
}

void
dtrace_sync(void)
{
	dtrace_xcall(DTRACE_CPUALL, (dtrace_xcall_t)dtrace_sync_func, NULL);
}

static int64_t	tgt_cpu_tsc;
static int64_t	hst_cpu_tsc;
static int64_t	timebase_skew[MAXCPU];
static uint64_t	nsec_scale;

/* See below for the explanation of this macro. */
/* This is taken from the amd64 dtrace_subr, to provide a synchronized timer
 * between multiple processors in dtrace.  Since PowerPC Timebases can be much
 * lower than x86, the scale shift is 26 instead of 28, allowing for a 15.63MHz
 * timebase.
 */
#define SCALE_SHIFT	26

static void
dtrace_gethrtime_init_cpu(void *arg)
{
	uintptr_t cpu = (uintptr_t) arg;

	if (cpu == curcpu)
		tgt_cpu_tsc = mftb();
	else
		hst_cpu_tsc = mftb();
}

static void
dtrace_gethrtime_init(void *arg)
{
	struct pcpu *pc;
	uint64_t tb_f;
	cpuset_t map;
	int i;

	tb_f = cpu_tickrate();

	/*
	 * The following line checks that nsec_scale calculated below
	 * doesn't overflow 32-bit unsigned integer, so that it can multiply
	 * another 32-bit integer without overflowing 64-bit.
	 * Thus minimum supported Timebase frequency is 15.63MHz.
	 */
	KASSERT(tb_f > (NANOSEC >> (32 - SCALE_SHIFT)), ("Timebase frequency is too low"));

	/*
	 * We scale up NANOSEC/tb_f ratio to preserve as much precision
	 * as possible.
	 * 2^26 factor was chosen quite arbitrarily from practical
	 * considerations:
	 * - it supports TSC frequencies as low as 15.63MHz (see above);
	 */
	nsec_scale = ((uint64_t)NANOSEC << SCALE_SHIFT) / tb_f;

	/* The current CPU is the reference one. */
	sched_pin();
	timebase_skew[curcpu] = 0;
	CPU_FOREACH(i) {
		if (i == curcpu)
			continue;

		pc = pcpu_find(i);
		CPU_SETOF(PCPU_GET(cpuid), &map);
		CPU_SET(pc->pc_cpuid, &map);

		smp_rendezvous_cpus(map, NULL,
		    dtrace_gethrtime_init_cpu,
		    smp_no_rendezvous_barrier, (void *)(uintptr_t) i);

		timebase_skew[i] = tgt_cpu_tsc - hst_cpu_tsc;
	}
	sched_unpin();
}
#ifdef EARLY_AP_STARTUP
SYSINIT(dtrace_gethrtime_init, SI_SUB_DTRACE, SI_ORDER_ANY,
    dtrace_gethrtime_init, NULL);
#else
SYSINIT(dtrace_gethrtime_init, SI_SUB_SMP, SI_ORDER_ANY, dtrace_gethrtime_init,
    NULL);
#endif

/*
 * DTrace needs a high resolution time function which can
 * be called from a probe context and guaranteed not to have
 * instrumented with probes itself.
 *
 * Returns nanoseconds since boot.
 */
uint64_t
dtrace_gethrtime()
{
	uint64_t timebase;
	uint32_t lo;
	uint32_t hi;

	/*
	 * We split timebase value into lower and higher 32-bit halves and separately
	 * scale them with nsec_scale, then we scale them down by 2^28
	 * (see nsec_scale calculations) taking into account 32-bit shift of
	 * the higher half and finally add.
	 */
	timebase = mftb() - timebase_skew[curcpu];
	lo = timebase;
	hi = timebase >> 32;
	return (((lo * nsec_scale) >> SCALE_SHIFT) +
	    ((hi * nsec_scale) << (32 - SCALE_SHIFT)));
}

uint64_t
dtrace_gethrestime(void)
{
	struct      timespec curtime;

	dtrace_getnanotime(&curtime);

	return (curtime.tv_sec * 1000000000UL + curtime.tv_nsec);
}

/* Function to handle DTrace traps during probes. See powerpc/powerpc/trap.c */
int
dtrace_trap(struct trapframe *frame, u_int type)
{
	uint16_t nofault;

	/*
	 * A trap can occur while DTrace executes a probe. Before
	 * executing the probe, DTrace blocks re-scheduling and sets
	 * a flag in its per-cpu flags to indicate that it doesn't
	 * want to fault. On returning from the probe, the no-fault
	 * flag is cleared and finally re-scheduling is enabled.
	 *
	 * Check if DTrace has enabled 'no-fault' mode:
	 */
	sched_pin();
	nofault = cpu_core[curcpu].cpuc_dtrace_flags & CPU_DTRACE_NOFAULT;
	sched_unpin();
	if (nofault) {
		KASSERT((frame->srr1 & PSL_EE) == 0, ("interrupts enabled"));
		/*
		 * There are only a couple of trap types that are expected.
		 * All the rest will be handled in the usual way.
		 */
		switch (type) {
		/* Page fault. */
		case EXC_DSI:
		case EXC_DSE:
			/* Flag a bad address. */
			cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
			cpu_core[curcpu].cpuc_dtrace_illval = frame->dar;

			/*
			 * Offset the instruction pointer to the instruction
			 * following the one causing the fault.
			 */
			frame->srr0 += sizeof(int);
			return (1);
		case EXC_ISI:
		case EXC_ISE:
			/* Flag a bad address. */
			cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
			cpu_core[curcpu].cpuc_dtrace_illval = frame->srr0;

			/*
			 * Offset the instruction pointer to the instruction
			 * following the one causing the fault.
			 */
			frame->srr0 += sizeof(int);
			return (1);
		default:
			/* Handle all other traps in the usual way. */
			break;
		}
	}

	/* Handle the trap in the usual way. */
	return (0);
}

void
dtrace_probe_error(dtrace_state_t *state, dtrace_epid_t epid, int which,
    int fault, int fltoffs, uintptr_t illval)
{

	dtrace_probe(dtrace_probeid_error, (uint64_t)(uintptr_t)state,
	    (uintptr_t)epid,
	    (uintptr_t)which, (uintptr_t)fault, (uintptr_t)fltoffs);
}

static int
dtrace_invop_start(struct trapframe *frame)
{

	switch (dtrace_invop(frame->srr0, frame, frame->fixreg[3])) {
	case DTRACE_INVOP_JUMP:
		break;
	case DTRACE_INVOP_BCTR:
		frame->srr0 = frame->ctr;
		break;
	case DTRACE_INVOP_BLR:
		frame->srr0 = frame->lr;
		break;
	case DTRACE_INVOP_MFLR_R0:
		frame->fixreg[0] = frame->lr;
		frame->srr0 = frame->srr0 + 4;
		break;
	default:
		return (-1);
	}
	return (0);
}

void dtrace_invop_init(void)
{
	dtrace_invop_jump_addr = dtrace_invop_start;
}

void dtrace_invop_uninit(void)
{
	dtrace_invop_jump_addr = 0;
}
