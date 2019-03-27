/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008 Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

/* Support for the AMD K7 and later processors */

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#ifdef	HWPMC_DEBUG
enum pmc_class	amd_pmc_class;
#endif

/* AMD K7 & K8 PMCs */
struct amd_descr {
	struct pmc_descr pm_descr;  /* "base class" */
	uint32_t	pm_evsel;   /* address of EVSEL register */
	uint32_t	pm_perfctr; /* address of PERFCTR register */
};

static  struct amd_descr amd_pmcdesc[AMD_NPMCS] =
{
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_0,
	.pm_perfctr = AMD_PMC_PERFCTR_0
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_1,
	.pm_perfctr = AMD_PMC_PERFCTR_1
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_2,
	.pm_perfctr = AMD_PMC_PERFCTR_2
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_3,
	.pm_perfctr = AMD_PMC_PERFCTR_3
     },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_4,
	.pm_perfctr = AMD_PMC_PERFCTR_4
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_5,
	.pm_perfctr = AMD_PMC_PERFCTR_5
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_0,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_0
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_1,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_1
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_2,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_2
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_3,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_3
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_4,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_4
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_L3_5,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_L3_5
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_DF_0,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_DF_0
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_DF_1,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_DF_1
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_DF_2,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_DF_2
    },
    {
	.pm_descr =
	{
		.pd_name  = "",
		.pd_class = -1,
		.pd_caps  = AMD_PMC_CAPS,
		.pd_width = 48
	},
	.pm_evsel   = AMD_PMC_EVSEL_EP_DF_3,
	.pm_perfctr = AMD_PMC_PERFCTR_EP_DF_3
     }
};

struct amd_event_code_map {
	enum pmc_event	pe_ev;	 /* enum value */
	uint16_t	pe_code; /* encoded event mask */
	uint8_t		pe_mask; /* bits allowed in unit mask */
};

const struct amd_event_code_map amd_event_codes[] = {
#if	defined(__i386__)	/* 32 bit Athlon (K7) only */
	{ PMC_EV_K7_DC_ACCESSES, 		0x40, 0 },
	{ PMC_EV_K7_DC_MISSES,			0x41, 0 },
	{ PMC_EV_K7_DC_REFILLS_FROM_L2,		0x42, AMD_PMC_UNITMASK_MOESI },
	{ PMC_EV_K7_DC_REFILLS_FROM_SYSTEM,	0x43, AMD_PMC_UNITMASK_MOESI },
	{ PMC_EV_K7_DC_WRITEBACKS,		0x44, AMD_PMC_UNITMASK_MOESI },
	{ PMC_EV_K7_L1_DTLB_MISS_AND_L2_DTLB_HITS, 0x45, 0 },
	{ PMC_EV_K7_L1_AND_L2_DTLB_MISSES,	0x46, 0 },
	{ PMC_EV_K7_MISALIGNED_REFERENCES,	0x47, 0 },

	{ PMC_EV_K7_IC_FETCHES,			0x80, 0 },
	{ PMC_EV_K7_IC_MISSES,			0x81, 0 },

	{ PMC_EV_K7_L1_ITLB_MISSES,		0x84, 0 },
	{ PMC_EV_K7_L1_L2_ITLB_MISSES,		0x85, 0 },

	{ PMC_EV_K7_RETIRED_INSTRUCTIONS,	0xC0, 0 },
	{ PMC_EV_K7_RETIRED_OPS,		0xC1, 0 },
	{ PMC_EV_K7_RETIRED_BRANCHES,		0xC2, 0 },
	{ PMC_EV_K7_RETIRED_BRANCHES_MISPREDICTED, 0xC3, 0 },
	{ PMC_EV_K7_RETIRED_TAKEN_BRANCHES, 	0xC4, 0 },
	{ PMC_EV_K7_RETIRED_TAKEN_BRANCHES_MISPREDICTED, 0xC5, 0 },
	{ PMC_EV_K7_RETIRED_FAR_CONTROL_TRANSFERS, 0xC6, 0 },
	{ PMC_EV_K7_RETIRED_RESYNC_BRANCHES,	0xC7, 0 },
	{ PMC_EV_K7_INTERRUPTS_MASKED_CYCLES,	0xCD, 0 },
	{ PMC_EV_K7_INTERRUPTS_MASKED_WHILE_PENDING_CYCLES, 0xCE, 0 },
	{ PMC_EV_K7_HARDWARE_INTERRUPTS,	0xCF, 0 },
#endif

	{ PMC_EV_K8_FP_DISPATCHED_FPU_OPS,		0x00, 0x3F },
	{ PMC_EV_K8_FP_CYCLES_WITH_NO_FPU_OPS_RETIRED,	0x01, 0x00 },
	{ PMC_EV_K8_FP_DISPATCHED_FPU_FAST_FLAG_OPS,	0x02, 0x00 },

	{ PMC_EV_K8_LS_SEGMENT_REGISTER_LOAD, 		0x20, 0x7F },
	{ PMC_EV_K8_LS_MICROARCHITECTURAL_RESYNC_BY_SELF_MODIFYING_CODE,
	  						0x21, 0x00 },
	{ PMC_EV_K8_LS_MICROARCHITECTURAL_RESYNC_BY_SNOOP, 0x22, 0x00 },
	{ PMC_EV_K8_LS_BUFFER2_FULL,			0x23, 0x00 },
	{ PMC_EV_K8_LS_LOCKED_OPERATION,		0x24, 0x07 },
	{ PMC_EV_K8_LS_MICROARCHITECTURAL_LATE_CANCEL,	0x25, 0x00 },
	{ PMC_EV_K8_LS_RETIRED_CFLUSH_INSTRUCTIONS,	0x26, 0x00 },
	{ PMC_EV_K8_LS_RETIRED_CPUID_INSTRUCTIONS,	0x27, 0x00 },

	{ PMC_EV_K8_DC_ACCESS,				0x40, 0x00 },
	{ PMC_EV_K8_DC_MISS,				0x41, 0x00 },
	{ PMC_EV_K8_DC_REFILL_FROM_L2,			0x42, 0x1F },
	{ PMC_EV_K8_DC_REFILL_FROM_SYSTEM,		0x43, 0x1F },
	{ PMC_EV_K8_DC_COPYBACK,			0x44, 0x1F },
	{ PMC_EV_K8_DC_L1_DTLB_MISS_AND_L2_DTLB_HIT,	0x45, 0x00 },
	{ PMC_EV_K8_DC_L1_DTLB_MISS_AND_L2_DTLB_MISS,	0x46, 0x00 },
	{ PMC_EV_K8_DC_MISALIGNED_DATA_REFERENCE,	0x47, 0x00 },
	{ PMC_EV_K8_DC_MICROARCHITECTURAL_LATE_CANCEL,	0x48, 0x00 },
	{ PMC_EV_K8_DC_MICROARCHITECTURAL_EARLY_CANCEL, 0x49, 0x00 },
	{ PMC_EV_K8_DC_ONE_BIT_ECC_ERROR,		0x4A, 0x03 },
	{ PMC_EV_K8_DC_DISPATCHED_PREFETCH_INSTRUCTIONS, 0x4B, 0x07 },
	{ PMC_EV_K8_DC_DCACHE_ACCESSES_BY_LOCKS,	0x4C, 0x03 },

	{ PMC_EV_K8_BU_CPU_CLK_UNHALTED,		0x76, 0x00 },
	{ PMC_EV_K8_BU_INTERNAL_L2_REQUEST,		0x7D, 0x1F },
	{ PMC_EV_K8_BU_FILL_REQUEST_L2_MISS,		0x7E, 0x07 },
	{ PMC_EV_K8_BU_FILL_INTO_L2,			0x7F, 0x03 },

	{ PMC_EV_K8_IC_FETCH,				0x80, 0x00 },
	{ PMC_EV_K8_IC_MISS,				0x81, 0x00 },
	{ PMC_EV_K8_IC_REFILL_FROM_L2,			0x82, 0x00 },
	{ PMC_EV_K8_IC_REFILL_FROM_SYSTEM,		0x83, 0x00 },
	{ PMC_EV_K8_IC_L1_ITLB_MISS_AND_L2_ITLB_HIT,	0x84, 0x00 },
	{ PMC_EV_K8_IC_L1_ITLB_MISS_AND_L2_ITLB_MISS,	0x85, 0x00 },
	{ PMC_EV_K8_IC_MICROARCHITECTURAL_RESYNC_BY_SNOOP, 0x86, 0x00 },
	{ PMC_EV_K8_IC_INSTRUCTION_FETCH_STALL,		0x87, 0x00 },
	{ PMC_EV_K8_IC_RETURN_STACK_HIT,		0x88, 0x00 },
	{ PMC_EV_K8_IC_RETURN_STACK_OVERFLOW,		0x89, 0x00 },

	{ PMC_EV_K8_FR_RETIRED_X86_INSTRUCTIONS,	0xC0, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_UOPS,			0xC1, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_BRANCHES,		0xC2, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_BRANCHES_MISPREDICTED,	0xC3, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_TAKEN_BRANCHES,		0xC4, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_TAKEN_BRANCHES_MISPREDICTED, 0xC5, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_FAR_CONTROL_TRANSFERS,	0xC6, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_RESYNCS,			0xC7, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_NEAR_RETURNS,		0xC8, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_NEAR_RETURNS_MISPREDICTED, 0xC9, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_TAKEN_BRANCHES_MISPREDICTED_BY_ADDR_MISCOMPARE,
							0xCA, 0x00 },
	{ PMC_EV_K8_FR_RETIRED_FPU_INSTRUCTIONS,	0xCB, 0x0F },
	{ PMC_EV_K8_FR_RETIRED_FASTPATH_DOUBLE_OP_INSTRUCTIONS,
							0xCC, 0x07 },
	{ PMC_EV_K8_FR_INTERRUPTS_MASKED_CYCLES,	0xCD, 0x00 },
	{ PMC_EV_K8_FR_INTERRUPTS_MASKED_WHILE_PENDING_CYCLES, 0xCE, 0x00 },
	{ PMC_EV_K8_FR_TAKEN_HARDWARE_INTERRUPTS,	0xCF, 0x00 },

	{ PMC_EV_K8_FR_DECODER_EMPTY,			0xD0, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALLS,			0xD1, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_FROM_BRANCH_ABORT_TO_RETIRE,
							0xD2, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_FOR_SERIALIZATION, 0xD3, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_FOR_SEGMENT_LOAD,	0xD4, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_REORDER_BUFFER_IS_FULL,
							0xD5, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_RESERVATION_STATIONS_ARE_FULL,
							0xD6, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_FPU_IS_FULL,	0xD7, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_LS_IS_FULL,	0xD8, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_WAITING_FOR_ALL_TO_BE_QUIET,
							0xD9, 0x00 },
	{ PMC_EV_K8_FR_DISPATCH_STALL_WHEN_FAR_XFER_OR_RESYNC_BRANCH_PENDING,
							0xDA, 0x00 },
	{ PMC_EV_K8_FR_FPU_EXCEPTIONS,			0xDB, 0x0F },
	{ PMC_EV_K8_FR_NUMBER_OF_BREAKPOINTS_FOR_DR0,	0xDC, 0x00 },
	{ PMC_EV_K8_FR_NUMBER_OF_BREAKPOINTS_FOR_DR1,	0xDD, 0x00 },
	{ PMC_EV_K8_FR_NUMBER_OF_BREAKPOINTS_FOR_DR2,	0xDE, 0x00 },
	{ PMC_EV_K8_FR_NUMBER_OF_BREAKPOINTS_FOR_DR3,	0xDF, 0x00 },

	{ PMC_EV_K8_NB_MEMORY_CONTROLLER_PAGE_ACCESS_EVENT, 0xE0, 0x7 },
	{ PMC_EV_K8_NB_MEMORY_CONTROLLER_PAGE_TABLE_OVERFLOW, 0xE1, 0x00 },
	{ PMC_EV_K8_NB_MEMORY_CONTROLLER_DRAM_COMMAND_SLOTS_MISSED,
							0xE2, 0x00 },
	{ PMC_EV_K8_NB_MEMORY_CONTROLLER_TURNAROUND,	0xE3, 0x07 },
	{ PMC_EV_K8_NB_MEMORY_CONTROLLER_BYPASS_SATURATION, 0xE4, 0x0F },
	{ PMC_EV_K8_NB_SIZED_COMMANDS,			0xEB, 0x7F },
	{ PMC_EV_K8_NB_PROBE_RESULT,			0xEC, 0x0F },
	{ PMC_EV_K8_NB_HT_BUS0_BANDWIDTH,		0xF6, 0x0F },
	{ PMC_EV_K8_NB_HT_BUS1_BANDWIDTH,		0xF7, 0x0F },
	{ PMC_EV_K8_NB_HT_BUS2_BANDWIDTH,		0xF8, 0x0F }

};

const int amd_event_codes_size = nitems(amd_event_codes);

/*
 * Per-processor information
 */

struct amd_cpu {
	struct pmc_hw	pc_amdpmcs[AMD_NPMCS];
};

static struct amd_cpu **amd_pcpu;

/*
 * read a pmc register
 */

static int
amd_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	enum pmc_mode mode;
	const struct amd_descr *pd;
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));
	KASSERT(amd_pcpu[cpu],
	    ("[amd,%d] null per-cpu, cpu %d", __LINE__, cpu));

	pm = amd_pcpu[cpu]->pc_amdpmcs[ri].phw_pmc;
	pd = &amd_pmcdesc[ri];

	KASSERT(pm != NULL,
	    ("[amd,%d] No owner for HWPMC [cpu%d,pmc%d]", __LINE__,
		cpu, ri));

	mode = PMC_TO_MODE(pm);

	PMCDBG2(MDP,REA,1,"amd-read id=%d class=%d", ri, pd->pm_descr.pd_class);

#ifdef	HWPMC_DEBUG
	KASSERT(pd->pm_descr.pd_class == amd_pmc_class,
	    ("[amd,%d] unknown PMC class (%d)", __LINE__,
		pd->pm_descr.pd_class));
#endif

	tmp = rdmsr(pd->pm_perfctr); /* RDMSR serializes */
	PMCDBG2(MDP,REA,2,"amd-read (pre-munge) id=%d -> %jd", ri, tmp);
	if (PMC_IS_SAMPLING_MODE(mode)) {
		/* Sign extend 48 bit value to 64 bits. */
		tmp = (pmc_value_t) (((int64_t) tmp << 16) >> 16);
		tmp = AMD_PERFCTR_VALUE_TO_RELOAD_COUNT(tmp);
	}
	*v = tmp;

	PMCDBG2(MDP,REA,2,"amd-read (post-munge) id=%d -> %jd", ri, *v);

	return 0;
}

/*
 * Write a PMC MSR.
 */

static int
amd_write_pmc(int cpu, int ri, pmc_value_t v)
{
	const struct amd_descr *pd;
	enum pmc_mode mode;
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));

	pm = amd_pcpu[cpu]->pc_amdpmcs[ri].phw_pmc;
	pd = &amd_pmcdesc[ri];

	KASSERT(pm != NULL,
	    ("[amd,%d] PMC not owned (cpu%d,pmc%d)", __LINE__,
		cpu, ri));

	mode = PMC_TO_MODE(pm);

#ifdef	HWPMC_DEBUG
	KASSERT(pd->pm_descr.pd_class == amd_pmc_class,
	    ("[amd,%d] unknown PMC class (%d)", __LINE__,
		pd->pm_descr.pd_class));
#endif

	/* use 2's complement of the count for sampling mode PMCs */
	if (PMC_IS_SAMPLING_MODE(mode))
		v = AMD_RELOAD_COUNT_TO_PERFCTR_VALUE(v);

	PMCDBG3(MDP,WRI,1,"amd-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	/* write the PMC value */
	wrmsr(pd->pm_perfctr, v);
	return 0;
}

/*
 * configure hardware pmc according to the configuration recorded in
 * pmc 'pm'.
 */

static int
amd_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));

	phw = &amd_pcpu[cpu]->pc_amdpmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[amd,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
		__LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;
	return 0;
}

/*
 * Retrieve a configured PMC pointer from hardware state.
 */

static int
amd_get_config(int cpu, int ri, struct pmc **ppm)
{
	*ppm = amd_pcpu[cpu]->pc_amdpmcs[ri].phw_pmc;

	return 0;
}

/*
 * Machine dependent actions taken during the context switch in of a
 * thread.
 */

static int
amd_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc;

	PMCDBG3(MDP,SWI,1, "pc=%p pp=%p enable-msr=%d", pc, pp,
	    (pp->pp_flags & PMC_PP_ENABLE_MSR_ACCESS) != 0);

	/* enable the RDPMC instruction if needed */
	if (pp->pp_flags & PMC_PP_ENABLE_MSR_ACCESS)
		load_cr4(rcr4() | CR4_PCE);

	return 0;
}

/*
 * Machine dependent actions taken during the context switch out of a
 * thread.
 */

static int
amd_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc;
	(void) pp;		/* can be NULL */

	PMCDBG3(MDP,SWO,1, "pc=%p pp=%p enable-msr=%d", pc, pp, pp ?
	    (pp->pp_flags & PMC_PP_ENABLE_MSR_ACCESS) == 1 : 0);

	/* always turn off the RDPMC instruction */
	load_cr4(rcr4() & ~CR4_PCE);

	return 0;
}

/*
 * Check if a given allocation is feasible.
 */

static int
amd_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	int i;
	uint64_t allowed_unitmask, caps, config, unitmask;
	enum pmc_event pe;
	const struct pmc_descr *pd;

	(void) cpu;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row index %d", __LINE__, ri));

	pd = &amd_pmcdesc[ri].pm_descr;

	/* check class match */
	if (pd->pd_class != a->pm_class)
		return EINVAL;

	caps = pm->pm_caps;

	PMCDBG2(MDP,ALL,1,"amd-allocate ri=%d caps=0x%x", ri, caps);

	if((ri >= 0 && ri < 6) && !(a->pm_md.pm_amd.pm_amd_sub_class == PMC_AMD_SUB_CLASS_CORE))
		return EINVAL;
	if((ri >= 6 && ri < 12) && !(a->pm_md.pm_amd.pm_amd_sub_class == PMC_AMD_SUB_CLASS_L3_CACHE))
		return EINVAL;
	if((ri >= 12 && ri < 16) && !(a->pm_md.pm_amd.pm_amd_sub_class == PMC_AMD_SUB_CLASS_DATA_FABRIC))
		return EINVAL;

	if ((pd->pd_caps & caps) != caps)
		return EPERM;
	if (strlen(pmc_cpuid) != 0) {
		pm->pm_md.pm_amd.pm_amd_evsel =
			a->pm_md.pm_amd.pm_amd_config;
		PMCDBG2(MDP,ALL,2,"amd-allocate ri=%d -> config=0x%x", ri, a->pm_md.pm_amd.pm_amd_config);
		return (0);
	}

	pe = a->pm_ev;

	/* map ev to the correct event mask code */
	config = allowed_unitmask = 0;
	for (i = 0; i < amd_event_codes_size; i++)
		if (amd_event_codes[i].pe_ev == pe) {
			config =
			    AMD_PMC_TO_EVENTMASK(amd_event_codes[i].pe_code);
			allowed_unitmask =
			    AMD_PMC_TO_UNITMASK(amd_event_codes[i].pe_mask);
			break;
		}
	if (i == amd_event_codes_size)
		return EINVAL;

	unitmask = a->pm_md.pm_amd.pm_amd_config & AMD_PMC_UNITMASK;
	if (unitmask & ~allowed_unitmask) /* disallow reserved bits */
		return EINVAL;

	if (unitmask && (caps & PMC_CAP_QUALIFIER))
		config |= unitmask;

	if (caps & PMC_CAP_THRESHOLD)
		config |= a->pm_md.pm_amd.pm_amd_config & AMD_PMC_COUNTERMASK;

	/* set at least one of the 'usr' or 'os' caps */
	if (caps & PMC_CAP_USER)
		config |= AMD_PMC_USR;
	if (caps & PMC_CAP_SYSTEM)
		config |= AMD_PMC_OS;
	if ((caps & (PMC_CAP_USER|PMC_CAP_SYSTEM)) == 0)
		config |= (AMD_PMC_USR|AMD_PMC_OS);

	if (caps & PMC_CAP_EDGE)
		config |= AMD_PMC_EDGE;
	if (caps & PMC_CAP_INVERT)
		config |= AMD_PMC_INVERT;
	if (caps & PMC_CAP_INTERRUPT)
		config |= AMD_PMC_INT;

	pm->pm_md.pm_amd.pm_amd_evsel = config; /* save config value */

	PMCDBG2(MDP,ALL,2,"amd-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}

/*
 * Release machine dependent state associated with a PMC.  This is a
 * no-op on this architecture.
 *
 */

/* ARGSUSED0 */
static int
amd_release_pmc(int cpu, int ri, struct pmc *pmc)
{
#ifdef	HWPMC_DEBUG
	const struct amd_descr *pd;
#endif
	struct pmc_hw *phw;

	(void) pmc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));

	phw = &amd_pcpu[cpu]->pc_amdpmcs[ri];

	KASSERT(phw->phw_pmc == NULL,
	    ("[amd,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

#ifdef	HWPMC_DEBUG
	pd = &amd_pmcdesc[ri];
	if (pd->pm_descr.pd_class == amd_pmc_class)
		KASSERT(AMD_PMC_IS_STOPPED(pd->pm_evsel),
		    ("[amd,%d] PMC %d released while active", __LINE__, ri));
#endif

	return 0;
}

/*
 * start a PMC.
 */

static int
amd_start_pmc(int cpu, int ri)
{
	uint64_t config;
	struct pmc *pm;
	struct pmc_hw *phw;
	const struct amd_descr *pd;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));

	phw = &amd_pcpu[cpu]->pc_amdpmcs[ri];
	pm  = phw->phw_pmc;
	pd = &amd_pmcdesc[ri];

	KASSERT(pm != NULL,
	    ("[amd,%d] starting cpu%d,pmc%d with null pmc record", __LINE__,
		cpu, ri));

	PMCDBG2(MDP,STA,1,"amd-start cpu=%d ri=%d", cpu, ri);

	KASSERT(AMD_PMC_IS_STOPPED(pd->pm_evsel),
	    ("[amd,%d] pmc%d,cpu%d: Starting active PMC \"%s\"", __LINE__,
	    ri, cpu, pd->pm_descr.pd_name));

	/* turn on the PMC ENABLE bit */
	config = pm->pm_md.pm_amd.pm_amd_evsel | AMD_PMC_ENABLE;

	PMCDBG1(MDP,STA,2,"amd-start config=0x%x", config);

	wrmsr(pd->pm_evsel, config);
	return 0;
}

/*
 * Stop a PMC.
 */

static int
amd_stop_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct pmc_hw *phw;
	const struct amd_descr *pd;
	uint64_t config;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] illegal row-index %d", __LINE__, ri));

	phw = &amd_pcpu[cpu]->pc_amdpmcs[ri];
	pm  = phw->phw_pmc;
	pd  = &amd_pmcdesc[ri];

	KASSERT(pm != NULL,
	    ("[amd,%d] cpu%d,pmc%d no PMC to stop", __LINE__,
		cpu, ri));
	KASSERT(!AMD_PMC_IS_STOPPED(pd->pm_evsel),
	    ("[amd,%d] PMC%d, CPU%d \"%s\" already stopped",
		__LINE__, ri, cpu, pd->pm_descr.pd_name));

	PMCDBG1(MDP,STO,1,"amd-stop ri=%d", ri);

	/* turn off the PMC ENABLE bit */
	config = pm->pm_md.pm_amd.pm_amd_evsel & ~AMD_PMC_ENABLE;
	wrmsr(pd->pm_evsel, config);
	return 0;
}

/*
 * Interrupt handler.  This function needs to return '1' if the
 * interrupt was this CPU's PMCs or '0' otherwise.  It is not allowed
 * to sleep or do anything a 'fast' interrupt handler is not allowed
 * to do.
 */

static int
amd_intr(struct trapframe *tf)
{
	int i, error, retval, cpu;
	uint64_t config, evsel, perfctr;
	struct pmc *pm;
	struct amd_cpu *pac;
	pmc_value_t v;

	cpu = curcpu;
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] out of range CPU %d", __LINE__, cpu));

	PMCDBG3(MDP,INT,1, "cpu=%d tf=%p um=%d", cpu, (void *) tf,
	    TRAPF_USERMODE(tf));

	retval = 0;

	pac = amd_pcpu[cpu];

	/*
	 * look for all PMCs that have interrupted:
	 * - look for a running, sampling PMC which has overflowed
	 *   and which has a valid 'struct pmc' association
	 *
	 * If found, we call a helper to process the interrupt.
	 *
	 * If multiple PMCs interrupt at the same time, the AMD64
	 * processor appears to deliver as many NMIs as there are
	 * outstanding PMC interrupts.  So we process only one NMI
	 * interrupt at a time.
	 */

	for (i = 0; retval == 0 && i < AMD_NPMCS; i++) {

		if ((pm = pac->pc_amdpmcs[i].phw_pmc) == NULL ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			continue;
		}

		if (!AMD_PMC_HAS_OVERFLOWED(i))
			continue;

		retval = 1;	/* Found an interrupting PMC. */

		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		/* Stop the PMC, reload count. */
		evsel   = AMD_PMC_EVSEL_0 + i;
		perfctr = AMD_PMC_PERFCTR_0 + i;
		v       = pm->pm_sc.pm_reloadcount;
		config  = rdmsr(evsel);

		KASSERT((config & ~AMD_PMC_ENABLE) ==
		    (pm->pm_md.pm_amd.pm_amd_evsel & ~AMD_PMC_ENABLE),
		    ("[amd,%d] config mismatch reg=0x%jx pm=0x%jx", __LINE__,
			 (uintmax_t)config, (uintmax_t)pm->pm_md.pm_amd.pm_amd_evsel));

		wrmsr(evsel, config & ~AMD_PMC_ENABLE);
		wrmsr(perfctr, AMD_RELOAD_COUNT_TO_PERFCTR_VALUE(v));

		/* Restart the counter if logging succeeded. */
		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error == 0)
			wrmsr(evsel, config);
	}

	if (retval)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	PMCDBG1(MDP,INT,2, "retval=%d", retval);
	return (retval);
}

/*
 * describe a PMC
 */
static int
amd_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	size_t copied;
	const struct amd_descr *pd;
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] row-index %d out of range", __LINE__, ri));

	phw = &amd_pcpu[cpu]->pc_amdpmcs[ri];
	pd  = &amd_pmcdesc[ri];

	if ((error = copystr(pd->pm_descr.pd_name, pi->pm_name,
		 PMC_NAME_MAX, &copied)) != 0)
		return error;

	pi->pm_class = pd->pm_descr.pd_class;

	if (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) {
		pi->pm_enabled = TRUE;
		*ppmc          = phw->phw_pmc;
	} else {
		pi->pm_enabled = FALSE;
		*ppmc          = NULL;
	}

	return 0;
}

/*
 * i386 specific entry points
 */

/*
 * return the MSR address of the given PMC.
 */

static int
amd_get_msr(int ri, uint32_t *msr)
{
	KASSERT(ri >= 0 && ri < AMD_NPMCS,
	    ("[amd,%d] ri %d out of range", __LINE__, ri));

	*msr = amd_pmcdesc[ri].pm_perfctr - AMD_PMC_PERFCTR_0;

	return (0);
}

/*
 * processor dependent initialization.
 */

static int
amd_pcpu_init(struct pmc_mdep *md, int cpu)
{
	int classindex, first_ri, n;
	struct pmc_cpu *pc;
	struct amd_cpu *pac;
	struct pmc_hw  *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] insane cpu number %d", __LINE__, cpu));

	PMCDBG1(MDP,INI,1,"amd-init cpu=%d", cpu);

	amd_pcpu[cpu] = pac = malloc(sizeof(struct amd_cpu), M_PMC,
	    M_WAITOK|M_ZERO);

	/*
	 * Set the content of the hardware descriptors to a known
	 * state and initialize pointers in the MI per-cpu descriptor.
	 */
	pc = pmc_pcpu[cpu];
#if	defined(__amd64__)
	classindex = PMC_MDEP_CLASS_INDEX_K8;
#elif	defined(__i386__)
	classindex = md->pmd_cputype == PMC_CPU_AMD_K8 ?
	    PMC_MDEP_CLASS_INDEX_K8 : PMC_MDEP_CLASS_INDEX_K7;
#endif
	first_ri = md->pmd_classdep[classindex].pcd_ri;

	KASSERT(pc != NULL, ("[amd,%d] NULL per-cpu pointer", __LINE__));

	for (n = 0, phw = pac->pc_amdpmcs; n < AMD_NPMCS; n++, phw++) {
		phw->phw_state 	  = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(n);
		phw->phw_pmc	  = NULL;
		pc->pc_hwpmcs[n + first_ri]  = phw;
	}

	return (0);
}


/*
 * processor dependent cleanup prior to the KLD
 * being unloaded
 */

static int
amd_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	int classindex, first_ri, i;
	uint32_t evsel;
	struct pmc_cpu *pc;
	struct amd_cpu *pac;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[amd,%d] insane cpu number (%d)", __LINE__, cpu));

	PMCDBG1(MDP,INI,1,"amd-cleanup cpu=%d", cpu);

	/*
	 * First, turn off all PMCs on this CPU.
	 */
	for (i = 0; i < 4; i++) { /* XXX this loop is now not needed */
		evsel = rdmsr(AMD_PMC_EVSEL_0 + i);
		evsel &= ~AMD_PMC_ENABLE;
		wrmsr(AMD_PMC_EVSEL_0 + i, evsel);
	}

	/*
	 * Next, free up allocated space.
	 */
	if ((pac = amd_pcpu[cpu]) == NULL)
		return (0);

	amd_pcpu[cpu] = NULL;

#ifdef	HWPMC_DEBUG
	for (i = 0; i < AMD_NPMCS; i++) {
		KASSERT(pac->pc_amdpmcs[i].phw_pmc == NULL,
		    ("[amd,%d] CPU%d/PMC%d in use", __LINE__, cpu, i));
		KASSERT(AMD_PMC_IS_STOPPED(AMD_PMC_EVSEL_0 + i),
		    ("[amd,%d] CPU%d/PMC%d not stopped", __LINE__, cpu, i));
	}
#endif

	pc = pmc_pcpu[cpu];
	KASSERT(pc != NULL, ("[amd,%d] NULL per-cpu state", __LINE__));

#if	defined(__amd64__)
	classindex = PMC_MDEP_CLASS_INDEX_K8;
#elif	defined(__i386__)
	classindex = md->pmd_cputype == PMC_CPU_AMD_K8 ? PMC_MDEP_CLASS_INDEX_K8 :
	    PMC_MDEP_CLASS_INDEX_K7;
#endif
	first_ri = md->pmd_classdep[classindex].pcd_ri;

	/*
	 * Reset pointers in the MI 'per-cpu' state.
	 */
	for (i = 0; i < AMD_NPMCS; i++) {
		pc->pc_hwpmcs[i + first_ri] = NULL;
	}


	free(pac, M_PMC);

	return (0);
}

/*
 * Initialize ourselves.
 */

struct pmc_mdep *
pmc_amd_initialize(void)
{
	int classindex, error, i, ncpus;
	struct pmc_classdep *pcd;
	enum pmc_cputype cputype;
	struct pmc_mdep *pmc_mdep;
	enum pmc_class class;
	int model;
	char *name;

	/*
	 * The presence of hardware performance counters on the AMD
	 * Athlon, Duron or later processors, is _not_ indicated by
	 * any of the processor feature flags set by the 'CPUID'
	 * instruction, so we only check the 'instruction family'
	 * field returned by CPUID for instruction family >= 6.
	 */

	name = NULL;
	model = ((cpu_id & 0xF0000) >> 12) | ((cpu_id & 0xF0) >> 4);
	if (CPUID_TO_FAMILY(cpu_id) == 0x17)
		snprintf(pmc_cpuid, sizeof(pmc_cpuid), "AuthenticAMD-%d-%02X",
				 CPUID_TO_FAMILY(cpu_id), model);

	switch (cpu_id & 0xF00) {
#if	defined(__i386__)
	case 0x600:		/* Athlon(tm) processor */
		classindex = PMC_MDEP_CLASS_INDEX_K7;
		cputype = PMC_CPU_AMD_K7;
		class = PMC_CLASS_K7;
		name = "K7";
		break;
#endif
	case 0xF00:		/* Athlon64/Opteron processor */
		classindex = PMC_MDEP_CLASS_INDEX_K8;
		cputype = PMC_CPU_AMD_K8;
		class = PMC_CLASS_K8;
		name = "K8";
		break;

	default:
		(void) printf("pmc: Unknown AMD CPU %x %d-%d.\n", cpu_id, (cpu_id & 0xF00) >> 8, model);
		return NULL;
	}

#ifdef	HWPMC_DEBUG
	amd_pmc_class = class;
#endif

	/*
	 * Allocate space for pointers to PMC HW descriptors and for
	 * the MDEP structure used by MI code.
	 */
	amd_pcpu = malloc(sizeof(struct amd_cpu *) * pmc_cpu_max(), M_PMC,
	    M_WAITOK|M_ZERO);

	/*
	 * These processors have two classes of PMCs: the TSC and
	 * programmable PMCs.
	 */
	pmc_mdep = pmc_mdep_alloc(2);

	pmc_mdep->pmd_cputype = cputype;

	ncpus = pmc_cpu_max();

	/* Initialize the TSC. */
	error = pmc_tsc_initialize(pmc_mdep, ncpus);
	if (error)
		goto error;

	/* Initialize AMD K7 and K8 PMC handling. */
	pcd = &pmc_mdep->pmd_classdep[classindex];

	pcd->pcd_caps		= AMD_PMC_CAPS;
	pcd->pcd_class		= class;
	pcd->pcd_num		= AMD_NPMCS;
	pcd->pcd_ri		= pmc_mdep->pmd_npmc;
	pcd->pcd_width		= 48;

	/* fill in the correct pmc name and class */
	for (i = 0; i < AMD_NPMCS; i++) {
		(void) snprintf(amd_pmcdesc[i].pm_descr.pd_name,
		    sizeof(amd_pmcdesc[i].pm_descr.pd_name), "%s-%d",
		    name, i);
		amd_pmcdesc[i].pm_descr.pd_class = class;
	}

	pcd->pcd_allocate_pmc	= amd_allocate_pmc;
	pcd->pcd_config_pmc	= amd_config_pmc;
	pcd->pcd_describe	= amd_describe;
	pcd->pcd_get_config	= amd_get_config;
	pcd->pcd_get_msr	= amd_get_msr;
	pcd->pcd_pcpu_fini	= amd_pcpu_fini;
	pcd->pcd_pcpu_init	= amd_pcpu_init;
	pcd->pcd_read_pmc	= amd_read_pmc;
	pcd->pcd_release_pmc	= amd_release_pmc;
	pcd->pcd_start_pmc	= amd_start_pmc;
	pcd->pcd_stop_pmc	= amd_stop_pmc;
	pcd->pcd_write_pmc	= amd_write_pmc;

	pmc_mdep->pmd_pcpu_init = NULL;
	pmc_mdep->pmd_pcpu_fini = NULL;
	pmc_mdep->pmd_intr	= amd_intr;
	pmc_mdep->pmd_switch_in = amd_switch_in;
	pmc_mdep->pmd_switch_out = amd_switch_out;

	pmc_mdep->pmd_npmc     += AMD_NPMCS;

	PMCDBG0(MDP,INI,0,"amd-initialize");

	return (pmc_mdep);

  error:
	if (error) {
		free(pmc_mdep, M_PMC);
		pmc_mdep = NULL;
	}

	return (NULL);
}

/*
 * Finalization code for AMD CPUs.
 */

void
pmc_amd_finalize(struct pmc_mdep *md)
{
#if	defined(INVARIANTS)
	int classindex, i, ncpus, pmcclass;
#endif

	pmc_tsc_finalize(md);

	KASSERT(amd_pcpu != NULL, ("[amd,%d] NULL per-cpu array pointer",
	    __LINE__));

#if	defined(INVARIANTS)
	switch (md->pmd_cputype) {
#if	defined(__i386__)
	case PMC_CPU_AMD_K7:
		classindex = PMC_MDEP_CLASS_INDEX_K7;
		pmcclass = PMC_CLASS_K7;
		break;
#endif
	default:
		classindex = PMC_MDEP_CLASS_INDEX_K8;
		pmcclass = PMC_CLASS_K8;
	}

	KASSERT(md->pmd_classdep[classindex].pcd_class == pmcclass,
	    ("[amd,%d] pmc class mismatch", __LINE__));

	ncpus = pmc_cpu_max();

	for (i = 0; i < ncpus; i++)
		KASSERT(amd_pcpu[i] == NULL, ("[amd,%d] non-null pcpu",
		    __LINE__));
#endif

	free(amd_pcpu, M_PMC);
	amd_pcpu = NULL;
}
