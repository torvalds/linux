/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, George V. Neville-Neil <gnn@freebsd.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/systm.h>

#include <machine/pmc_mdep.h>
#include <machine/md_var.h>
#include <machine/mips_opcode.h>
#include <machine/vmparam.h>

int mips_npmcs;

/*
 * Per-processor information.
 */
struct mips_cpu {
	struct pmc_hw	*pc_mipspmcs;
};

static struct mips_cpu **mips_pcpu;

#if defined(__mips_n64)
#	define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_XKPHYS_START))
#else
#	define	MIPS_IS_VALID_KERNELADDR(reg)	((((reg) & 3) == 0) && \
					((vm_offset_t)(reg) >= MIPS_KSEG0_START))
#endif

/*
 * We need some reasonable default to prevent backtrace code
 * from wandering too far
 */
#define	MAX_FUNCTION_SIZE 0x10000
#define	MAX_PROLOGUE_SIZE 0x100

static int
mips_allocate_pmc(int cpu, int ri, struct pmc *pm,
  const struct pmc_op_pmcallocate *a)
{
	enum pmc_event pe;
	uint32_t caps, config, counter;
	uint32_t event;
	int i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row index %d", __LINE__, ri));

	caps = a->pm_caps;
	if (a->pm_class != mips_pmc_spec.ps_cpuclass)
		return (EINVAL);
	pe = a->pm_ev;
	counter = MIPS_CTR_ALL;
	event = 0;
	for (i = 0; i < mips_event_codes_size; i++) {
		if (mips_event_codes[i].pe_ev == pe) {
			event = mips_event_codes[i].pe_code;
			counter =  mips_event_codes[i].pe_counter;
			break;
		}
	}

	if (i == mips_event_codes_size)
		return (EINVAL);

	if ((counter != MIPS_CTR_ALL) && (counter != ri))
		return (EINVAL);

	config = mips_get_perfctl(cpu, ri, event, caps);

	pm->pm_md.pm_mips_evsel = config;

	PMCDBG2(MDP,ALL,2,"mips-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}


static int
mips_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row index %d", __LINE__, ri));

	pm  = mips_pcpu[cpu]->pc_mipspmcs[ri].phw_pmc;
	tmp = mips_pmcn_read(ri);
	PMCDBG2(MDP,REA,2,"mips-read id=%d -> %jd", ri, tmp);

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = tmp - (1UL << (mips_pmc_spec.ps_counter_width - 1));
	else
		*v = tmp;

	return 0;
}

static int
mips_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row-index %d", __LINE__, ri));

	pm  = mips_pcpu[cpu]->pc_mipspmcs[ri].phw_pmc;

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = (1UL << (mips_pmc_spec.ps_counter_width - 1)) - v;
	
	PMCDBG3(MDP,WRI,1,"mips-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	mips_pmcn_write(ri, v);

	return 0;
}

static int
mips_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row-index %d", __LINE__, ri));

	phw = &mips_pcpu[cpu]->pc_mipspmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[mips,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
	    __LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return 0;
}

static int
mips_start_pmc(int cpu, int ri)
{
	uint32_t config;
        struct pmc *pm;
        struct pmc_hw *phw;

	phw    = &mips_pcpu[cpu]->pc_mipspmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_mips_evsel;

	/* Enable the PMC. */
	switch (ri) {
	case 0:
		mips_wr_perfcnt0(config);
		break;
	case 1:
		mips_wr_perfcnt2(config);
		break;
	default:
		break;
	}

	return 0;
}

static int
mips_stop_pmc(int cpu, int ri)
{
        struct pmc *pm;
        struct pmc_hw *phw;

	phw    = &mips_pcpu[cpu]->pc_mipspmcs[ri];
	pm     = phw->phw_pmc;

	/*
	 * Disable the PMCs.
	 *
	 * Clearing the entire register turns the counter off as well
	 * as removes the previously sampled event.
	 */
	switch (ri) {
	case 0:
		mips_wr_perfcnt0(0);
		break;
	case 1:
		mips_wr_perfcnt2(0);
		break;
	default:
		break;
	}
	return 0;
}

static int
mips_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row-index %d", __LINE__, ri));

	phw = &mips_pcpu[cpu]->pc_mipspmcs[ri];
	KASSERT(phw->phw_pmc == NULL,
	    ("[mips,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	return 0;
}

static int
mips_pmc_intr(struct trapframe *tf)
{
	int error;
	int retval, ri, cpu;
	struct pmc *pm;
	struct mips_cpu *pc;
	uint32_t r0, r2;
	pmc_value_t r;

	cpu = curcpu;
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] CPU %d out of range", __LINE__, cpu));

	retval = 0;
	pc = mips_pcpu[cpu];

	/* Stop PMCs without clearing the counter */
	r0 = mips_rd_perfcnt0();
	mips_wr_perfcnt0(r0 & ~(0x1f));
	r2 = mips_rd_perfcnt2();
	mips_wr_perfcnt2(r2 & ~(0x1f));

	for (ri = 0; ri < mips_npmcs; ri++) {
		pm = mips_pcpu[cpu]->pc_mipspmcs[ri].phw_pmc;
		if (pm == NULL)
			continue;
		if (! PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			continue;

		r = mips_pmcn_read(ri);

		/* If bit 31 is set, the counter has overflowed */
		if ((r & (1UL << (mips_pmc_spec.ps_counter_width - 1))) == 0)
			continue;

		retval = 1;
		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;
		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error) {
			/* Clear/disable the relevant counter */
			if (ri == 0)
				r0 = 0;
			else if (ri == 1)
				r2 = 0;
			mips_stop_pmc(cpu, ri);
		}

		/* Reload sampling count */
		mips_write_pmc(cpu, ri, pm->pm_sc.pm_reloadcount);
	}

	/*
	 * Re-enable the PMC counters where they left off.
	 *
	 * Any counter which overflowed will have its sample count
	 * reloaded in the loop above.
	 */
	mips_wr_perfcnt0(r0);
	mips_wr_perfcnt2(r2);

	return retval;
}

static int
mips_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	struct pmc_hw *phw;
	char mips_name[PMC_NAME_MAX];

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d], illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] row-index %d out of range", __LINE__, ri));

	phw = &mips_pcpu[cpu]->pc_mipspmcs[ri];
	snprintf(mips_name, sizeof(mips_name), "MIPS-%d", ri);
	if ((error = copystr(mips_name, pi->pm_name, PMC_NAME_MAX,
	    NULL)) != 0)
		return error;
	pi->pm_class = mips_pmc_spec.ps_cpuclass;
	if (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) {
		pi->pm_enabled = TRUE;
		*ppmc          = phw->phw_pmc;
	} else {
		pi->pm_enabled = FALSE;
		*ppmc	       = NULL;
	}

	return (0);
}

static int
mips_get_config(int cpu, int ri, struct pmc **ppm)
{
	*ppm = mips_pcpu[cpu]->pc_mipspmcs[ri].phw_pmc;

	return 0;
}

/*
 * XXX don't know what we should do here.
 */
static int
mips_pmc_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{
	return 0;
}

static int
mips_pmc_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{
	return 0;
}

static int
mips_pcpu_init(struct pmc_mdep *md, int cpu)
{
	int first_ri, i;
	struct pmc_cpu *pc;
	struct mips_cpu *pac;
	struct pmc_hw  *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] wrong cpu number %d", __LINE__, cpu));
	PMCDBG1(MDP,INI,1,"mips-init cpu=%d", cpu);

	mips_pcpu[cpu] = pac = malloc(sizeof(struct mips_cpu), M_PMC,
	    M_WAITOK|M_ZERO);
	pac->pc_mipspmcs = malloc(sizeof(struct pmc_hw) * mips_npmcs,
	    M_PMC, M_WAITOK|M_ZERO);
	pc = pmc_pcpu[cpu];
	first_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_MIPS].pcd_ri;
	KASSERT(pc != NULL, ("[mips,%d] NULL per-cpu pointer", __LINE__));

	for (i = 0, phw = pac->pc_mipspmcs; i < mips_npmcs; i++, phw++) {
		phw->phw_state    = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(i);
		phw->phw_pmc      = NULL;
		pc->pc_hwpmcs[i + first_ri] = phw;
	}

	/*
	 * Clear the counter control register which has the effect
	 * of disabling counting.
	 */
	for (i = 0; i < mips_npmcs; i++)
		mips_pmcn_write(i, 0);

	return 0;
}

static int
mips_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	return 0;
}

struct pmc_mdep *
pmc_mips_initialize()
{
	struct pmc_mdep *pmc_mdep;
	struct pmc_classdep *pcd;
	
	/*
	 * TODO: Use More bit of PerfCntlX register to detect actual 
	 * number of counters
	 */
	mips_npmcs = 2;
	
	PMCDBG1(MDP,INI,1,"mips-init npmcs=%d", mips_npmcs);

	/*
	 * Allocate space for pointers to PMC HW descriptors and for
	 * the MDEP structure used by MI code.
	 */
	mips_pcpu = malloc(sizeof(struct mips_cpu *) * pmc_cpu_max(), M_PMC,
			   M_WAITOK|M_ZERO);

	/* Just one class */
	pmc_mdep = pmc_mdep_alloc(1);

	pmc_mdep->pmd_cputype = mips_pmc_spec.ps_cputype;

	pcd = &pmc_mdep->pmd_classdep[PMC_MDEP_CLASS_INDEX_MIPS];
	pcd->pcd_caps  = mips_pmc_spec.ps_capabilities;
	pcd->pcd_class = mips_pmc_spec.ps_cpuclass;
	pcd->pcd_num   = mips_npmcs;
	pcd->pcd_ri    = pmc_mdep->pmd_npmc;
	pcd->pcd_width = mips_pmc_spec.ps_counter_width;

	pcd->pcd_allocate_pmc   = mips_allocate_pmc;
	pcd->pcd_config_pmc     = mips_config_pmc;
	pcd->pcd_pcpu_fini      = mips_pcpu_fini;
	pcd->pcd_pcpu_init      = mips_pcpu_init;
	pcd->pcd_describe       = mips_describe;
	pcd->pcd_get_config	= mips_get_config;
	pcd->pcd_read_pmc       = mips_read_pmc;
	pcd->pcd_release_pmc    = mips_release_pmc;
	pcd->pcd_start_pmc      = mips_start_pmc;
	pcd->pcd_stop_pmc       = mips_stop_pmc;
 	pcd->pcd_write_pmc      = mips_write_pmc;

	pmc_mdep->pmd_intr       = mips_pmc_intr;
	pmc_mdep->pmd_switch_in  = mips_pmc_switch_in;
	pmc_mdep->pmd_switch_out = mips_pmc_switch_out;
	
	pmc_mdep->pmd_npmc   += mips_npmcs;

	return (pmc_mdep);
}

void
pmc_mips_finalize(struct pmc_mdep *md)
{
	(void) md;
}

#ifdef	HWPMC_MIPS_BACKTRACE

static int
pmc_next_frame(register_t *pc, register_t *sp)
{
	InstFmt i;
	uintptr_t va;
	uint32_t instr, mask;
	int more, stksize;
	register_t ra = 0;

	/* Jump here after a nonstandard (interrupt handler) frame */
	stksize = 0;

	/* check for bad SP: could foul up next frame */
	if (!MIPS_IS_VALID_KERNELADDR(*sp)) {
		goto error;
	}

	/* check for bad PC */
	if (!MIPS_IS_VALID_KERNELADDR(*pc)) {
		goto error;
	}

	/*
	 * Find the beginning of the current subroutine by scanning
	 * backwards from the current PC for the end of the previous
	 * subroutine.
	 */
	va = *pc - sizeof(int);
	while (1) {
		instr = *((uint32_t *)va);

		/* [d]addiu sp,sp,-X */
		if (((instr & 0xffff8000) == 0x27bd8000)
		    || ((instr & 0xffff8000) == 0x67bd8000))
			break;

		/* jr	ra */
		if (instr == 0x03e00008) {
			/* skip over branch-delay slot instruction */
			va += 2 * sizeof(int);
			break;
		}

		va -= sizeof(int);
	}

	/* skip over nulls which might separate .o files */
	while ((instr = *((uint32_t *)va)) == 0)
		va += sizeof(int);

	/* scan forwards to find stack size and any saved registers */
	stksize = 0;
	more = 3;
	mask = 0;
	for (; more; va += sizeof(int),
	    more = (more == 3) ? 3 : more - 1) {
		/* stop if hit our current position */
		if (va >= *pc)
			break;
		instr = *((uint32_t *)va);
		i.word = instr;
		switch (i.JType.op) {
		case OP_SPECIAL:
			switch (i.RType.func) {
			case OP_JR:
			case OP_JALR:
				more = 2;	/* stop after next instruction */
				break;

			case OP_SYSCALL:
			case OP_BREAK:
				more = 1;	/* stop now */
			}
			break;

		case OP_BCOND:
		case OP_J:
		case OP_JAL:
		case OP_BEQ:
		case OP_BNE:
		case OP_BLEZ:
		case OP_BGTZ:
			more = 2;	/* stop after next instruction */
			break;

		case OP_COP0:
		case OP_COP1:
		case OP_COP2:
		case OP_COP3:
			switch (i.RType.rs) {
			case OP_BCx:
			case OP_BCy:
				more = 2;	/* stop after next instruction */
			}
			break;

		case OP_SW:
		case OP_SD:
			/* 
			 * SP is being saved using S8(FP). Most likely it indicates
			 * that SP is modified in the function and we can't get
			 * its value safely without emulating code backward
			 * So just bail out on functions like this
			 */
			if ((i.IType.rs == 30) && (i.IType.rt = 29))
				return (-1);

			/* look for saved registers on the stack */
			if (i.IType.rs != 29)
				break;
			/* only restore the first one */
			if (mask & (1 << i.IType.rt))
				break;
			mask |= (1 << i.IType.rt);
			if (i.IType.rt == 31)
				ra = *((register_t *)(*sp + (short)i.IType.imm));
			break;

		case OP_ADDI:
		case OP_ADDIU:
		case OP_DADDI:
		case OP_DADDIU:
			/* look for stack pointer adjustment */
			if (i.IType.rs != 29 || i.IType.rt != 29)
				break;
			stksize = -((short)i.IType.imm);
		}
	}

	if (!MIPS_IS_VALID_KERNELADDR(ra))
		return (-1);

	*pc = ra;
	*sp += stksize;

	return (0);

error:
	return (-1);
}

static int
pmc_next_uframe(register_t *pc, register_t *sp, register_t *ra)
{
	int offset, registers_on_stack;
	uint32_t opcode, mask;
	register_t function_start;
	int stksize;
	InstFmt i;

	registers_on_stack = 0;
	mask = 0;
	function_start = 0;
	offset = 0;
	stksize = 0;

	while (offset < MAX_FUNCTION_SIZE) {
		opcode = fuword32((void *)(*pc - offset));

		/* [d]addiu sp, sp, -X*/
		if (((opcode & 0xffff8000) == 0x27bd8000)
		    || ((opcode & 0xffff8000) == 0x67bd8000)) {
			function_start = *pc - offset;
			registers_on_stack = 1;
			break;
		}

		/* lui gp, X */
		if ((opcode & 0xffff8000) == 0x3c1c0000) {
			/*
			 * Function might start with this instruction
			 * Keep an eye on "jr ra" and sp correction
			 * with positive value further on
			 */
			function_start = *pc - offset;
		}

		if (function_start) {
			/*
			 * Stop looking further. Possible end of
			 * function instruction: it means there is no
			 * stack modifications, sp is unchanged
			 */

			/* [d]addiu sp,sp,X */
			if (((opcode & 0xffff8000) == 0x27bd0000)
			    || ((opcode & 0xffff8000) == 0x67bd0000))
				break;

			if (opcode == 0x03e00008)
				break;
		}

		offset += sizeof(int);
	}

	if (!function_start)
		return (-1);

	if (registers_on_stack) {
		offset = 0;
		while ((offset < MAX_PROLOGUE_SIZE)
		    && ((function_start + offset) < *pc)) {
			i.word = fuword32((void *)(function_start + offset));
			switch (i.JType.op) {
			case OP_SW:
				/* look for saved registers on the stack */
				if (i.IType.rs != 29)
					break;
				/* only restore the first one */
				if (mask & (1 << i.IType.rt))
					break;
				mask |= (1 << i.IType.rt);
				if (i.IType.rt == 31)
					*ra = fuword32((void *)(*sp + (short)i.IType.imm));
				break;

#if defined(__mips_n64)
			case OP_SD:
				/* look for saved registers on the stack */
				if (i.IType.rs != 29)
					break;
				/* only restore the first one */
				if (mask & (1 << i.IType.rt))
					break;
				mask |= (1 << i.IType.rt);
				/* ra */
				if (i.IType.rt == 31)
					*ra = fuword64((void *)(*sp + (short)i.IType.imm));
			break;
#endif

			case OP_ADDI:
			case OP_ADDIU:
			case OP_DADDI:
			case OP_DADDIU:
				/* look for stack pointer adjustment */
				if (i.IType.rs != 29 || i.IType.rt != 29)
					break;
				stksize = -((short)i.IType.imm);
			}

			offset += sizeof(int);
		}
	}

	/*
	 * We reached the end of backtrace
	 */
	if (*pc == *ra)
		return (-1);

	*pc = *ra;
	*sp += stksize;

	return (0);
}

#endif /* HWPMC_MIPS_BACKTRACE */

struct pmc_mdep *
pmc_md_initialize()
{
	return pmc_mips_initialize();
}

void
pmc_md_finalize(struct pmc_mdep *md)
{
	return pmc_mips_finalize(md);
}

int
pmc_save_kernel_callchain(uintptr_t *cc, int nframes,
    struct trapframe *tf)
{
	register_t pc, ra, sp;
	int frames = 0;

	pc = tf->pc;
	sp = tf->sp;
	ra = tf->ra;

	cc[frames++] = pc;

#ifdef	HWPMC_MIPS_BACKTRACE
	/*
	 * Unwind, and unwind, and unwind
	 */
	while (1) {
		if (frames >= nframes)
			break;

		if (pmc_next_frame(&pc, &sp) < 0)
			break;

		cc[frames++] = pc;
	}
#endif

	return (frames);
}

int
pmc_save_user_callchain(uintptr_t *cc, int nframes,
    struct trapframe *tf)
{
	register_t pc, ra, sp;
	int frames = 0;

	pc = tf->pc;
	sp = tf->sp;
	ra = tf->ra;

	cc[frames++] = pc;

#ifdef	HWPMC_MIPS_BACKTRACE

	/*
	 * Unwind, and unwind, and unwind
	 */
	while (1) {
		if (frames >= nframes)
			break;

		if (pmc_next_uframe(&pc, &sp, &ra) < 0)
			break;

		cc[frames++] = pc;
	}
#endif

	return (frames);
}
