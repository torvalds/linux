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
 * Portions Copyright 2016-2018 Ruslan Bukin <br@bsdpad.com>
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
#include <machine/vmparam.h>
#include <machine/encoding.h>
#include <machine/riscvreg.h>
#include <machine/clock.h>
#include <machine/frame.h>
#include <machine/trap.h>
#include <vm/pmap.h>

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
	dtrace_invop_hdlr_t *hdlr, *prev;

	hdlr = dtrace_invop_hdlr;
	prev = NULL;

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

	(*func)(0, (uintptr_t)VM_MIN_KERNEL_ADDRESS);
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
	struct timespec curtime;

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

/* Function to handle DTrace traps during probes. See riscv/riscv/trap.c */
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
		case EXCP_FAULT_LOAD:
		case EXCP_FAULT_STORE:
		case EXCP_FAULT_FETCH:
			/* Flag a bad address. */
			cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
			cpu_core[curcpu].cpuc_dtrace_illval = 0;

			/*
			 * Offset the instruction pointer to the instruction
			 * following the one causing the fault.
			 */
			frame->tf_sepc += 4;

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
match_opcode(uint32_t insn, int match, int mask)  
{

	if (((insn ^ match) & mask) == 0)
		return (1);

	return (0);
}

static int
dtrace_invop_start(struct trapframe *frame)
{
	register_t *sp;
	uint32_t uimm;
	uint32_t imm;
	int invop;

	invop = dtrace_invop(frame->tf_sepc, frame, frame->tf_sepc);

	if (match_opcode(invop, (MATCH_SD | RS2_RA | RS1_SP),
	    (MASK_SD | RS2_MASK | RS1_MASK))) {
		/* Non-compressed store of ra to sp */
		imm = (invop >> 7) & 0x1f;
		imm |= ((invop >> 25) & 0x7f) << 5;
		sp = (register_t *)((uint8_t *)frame->tf_sp + imm);
		*sp = frame->tf_ra;
		frame->tf_sepc += INSN_SIZE;
		return (0);
	}

	if (match_opcode(invop, (MATCH_JALR | (X_RA << RS1_SHIFT)),
	    (MASK_JALR | RD_MASK | RS1_MASK | IMM_MASK))) {
		/* Non-compressed ret */
		frame->tf_sepc = frame->tf_ra;
		return (0);
	}

	if (match_opcode(invop, (MATCH_C_SDSP | RS2_C_RA),
	    (MASK_C_SDSP | RS2_C_MASK))) {
		/* 'C'-compressed store of ra to sp */
		uimm = ((invop >> 10) & 0x7) << 3;
		uimm |= ((invop >> 7) & 0x7) << 6;
		sp = (register_t *)((uint8_t *)frame->tf_sp + uimm);
		*sp = frame->tf_ra;
		frame->tf_sepc += INSN_C_SIZE;
		return (0);
	}

	if (match_opcode(invop, (MATCH_C_JR | (X_RA << RD_SHIFT)),
	    (MASK_C_JR | RD_MASK))) {
		/* 'C'-compressed ret */
		frame->tf_sepc = frame->tf_ra;
		return (0);
	}

	return (-1);
}

void
dtrace_invop_init(void)
{

	dtrace_invop_jump_addr = dtrace_invop_start;
}

void
dtrace_invop_uninit(void)
{

	dtrace_invop_jump_addr = 0;
}
