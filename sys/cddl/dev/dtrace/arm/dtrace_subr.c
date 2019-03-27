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
#include <machine/armreg.h>
#include <machine/clock.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <vm/pmap.h>

#define	DELAYBRANCH(x)	((int)(x) < 0)

#define	BIT_PC		15
#define	BIT_LR		14
#define	BIT_SP		13

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
dtrace_invop(uintptr_t addr, struct trapframe *frame, uintptr_t eax)
{
	dtrace_invop_hdlr_t *hdlr;
	int rval;

	for (hdlr = dtrace_invop_hdlr; hdlr != NULL; hdlr = hdlr->dtih_next)
		if ((rval = hdlr->dtih_func(addr, frame, eax)) != 0)
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
	printf("IMPLEMENT ME: dtrace_toxic_ranges\n");
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
	struct	timespec curtime;

	nanouptime(&curtime);

	return (curtime.tv_sec * 1000000000UL + curtime.tv_nsec);

}

uint64_t
dtrace_gethrestime(void)
{
	struct timespec current_time;

	dtrace_getnanotime(&current_time);

	return (current_time.tv_sec * 1000000000UL + current_time.tv_nsec);
}

/* Function to handle DTrace traps during probes. See amd64/amd64/trap.c */
int
dtrace_trap(struct trapframe *frame, u_int type)
{
	/*
	 * A trap can occur while DTrace executes a probe. Before
	 * executing the probe, DTrace blocks re-scheduling and sets
	 * a flag in its per-cpu flags to indicate that it doesn't
	 * want to fault. On returning from the probe, the no-fault
	 * flag is cleared and finally re-scheduling is enabled.
	 *
	 * Check if DTrace has enabled 'no-fault' mode:
	 *
	 */
	if ((cpu_core[curcpu].cpuc_dtrace_flags & CPU_DTRACE_NOFAULT) != 0) {
		/*
		 * There are only a couple of trap types that are expected.
		 * All the rest will be handled in the usual way.
		 */
		switch (type) {
		/* Page fault. */
		case FAULT_ALIGN:
			/* Flag a bad address. */
			cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
			cpu_core[curcpu].cpuc_dtrace_illval = 0;

			/*
			 * Offset the instruction pointer to the instruction
			 * following the one causing the fault.
			 */
			frame->tf_pc += sizeof(int);
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
	register_t *r0, *sp;
	int data, invop, reg, update_sp;

	invop = dtrace_invop(frame->tf_pc, frame, frame->tf_pc);
	switch (invop & DTRACE_INVOP_MASK) {
	case DTRACE_INVOP_PUSHM:
		sp = (register_t *)frame->tf_svc_sp;
		r0 = &frame->tf_r0;
		data = DTRACE_INVOP_DATA(invop);

		/*
		 * Store the pc, lr, and sp. These have their own
		 * entries in the struct.
		 */
		if (data & (1 << BIT_PC)) {
			sp--;
			*sp = frame->tf_pc;
		}
		if (data & (1 << BIT_LR)) {
			sp--;
			*sp = frame->tf_svc_lr;
		}
		if (data & (1 << BIT_SP)) {
			sp--;
			*sp = frame->tf_svc_sp;
		}

		/* Store the general registers */
		for (reg = 12; reg >= 0; reg--) {
			if (data & (1 << reg)) {
				sp--;
				*sp = r0[reg];
			}
		}

		/* Update the stack pointer and program counter to continue */
		frame->tf_svc_sp = (register_t)sp;
		frame->tf_pc += 4;
		break;
	case DTRACE_INVOP_POPM:
		sp = (register_t *)frame->tf_svc_sp;
		r0 = &frame->tf_r0;
		data = DTRACE_INVOP_DATA(invop);

		/* Read the general registers */
		for (reg = 0; reg <= 12; reg++) {
			if (data & (1 << reg)) {
				r0[reg] = *sp;
				sp++;
			}
		}

		/*
		 * Set the stack pointer. If we don't update it here we will
		 * need to update it at the end as the instruction would do
		 */
		update_sp = 1;
		if (data & (1 << BIT_SP)) {
			frame->tf_svc_sp = *sp;
			*sp++;
			update_sp = 0;
		}

		/* Update the link register, we need to use the correct copy */
		if (data & (1 << BIT_LR)) {
			frame->tf_svc_lr = *sp;
			*sp++;
		}
		/*
		 * And the program counter. If it's not in the list skip over
		 * it when we return so to not hit this again.
		 */
		if (data & (1 << BIT_PC)) {
			frame->tf_pc = *sp;
			*sp++;
		} else
			frame->tf_pc += 4;

		/* Update the stack pointer if we haven't already done so */
		if (update_sp)
			frame->tf_svc_sp = (register_t)sp;
		break;
	case DTRACE_INVOP_B:
		data = DTRACE_INVOP_DATA(invop) & 0x00ffffff;
		/* Sign extend the data */
		if ((data & (1 << 23)) != 0)
			data |= 0xff000000;
		/* The data is the number of 4-byte words to change the pc */
		data *= 4;
		data += 8;
		frame->tf_pc += data;
		break;
	default:
		return (-1);
		break;
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
