/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Justin Hibbits
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

#include <sys/param.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/systm.h>

#include <machine/pmc_mdep.h>
#include <machine/spr.h>
#include <machine/cpu.h>

#include "hwpmc_powerpc.h"

#define PPC970_MAX_PMCS	8

/* MMCR0, PMC1 is 8 bytes in, PMC2 is 1 byte in. */
#define PPC970_SET_MMCR0_PMCSEL(r, x, i) \
	((r & ~(0x1f << (7 * (1 - i) + 1))) | (x << (7 * (1 - i) + 1)))
/* MMCR1 has 6 PMC*SEL items (PMC3->PMC8), in sequence. */
#define PPC970_SET_MMCR1_PMCSEL(r, x, i) \
	((r & ~(0x1f << (5 * (7 - i) + 2))) | (x << (5 * (7 - i) + 2)))

#define PPC970_PMC_HAS_OVERFLOWED(x) (ppc970_pmcn_read(x) & (0x1 << 31))

/* How PMC works on PPC970:
 *
 * Any PMC can count a direct event.  Indirect events are handled specially.
 * Direct events: As published.
 *
 * Encoding 00 000 -- Add byte lane bit counters
 *   MMCR1[24:31] -- select bit matching PMC being an adder.
 * Bus events:
 * PMCxSEL: 1x -- select from byte lane: 10 == lower lane (0/1), 11 == upper
 * lane (2/3).
 * PMCxSEL[2:4] -- bit in the byte lane selected.
 *
 * PMC[1,2,5,6] == lane 0/lane 2
 * PMC[3,4,7,8] == lane 1,3
 *
 *
 * Lanes:
 * Lane 0 -- TTM0(FPU,ISU,IFU,VPU)
 *           TTM1(IDU,ISU,STS)
 *           LSU0 byte 0
 *           LSU1 byte 0
 * Lane 1 -- TTM0
 *           TTM1
 *           LSU0 byte 1
 *           LSU1 byte 1
 * Lane 2 -- TTM0
 *           TTM1
 *           LSU0 byte 2
 *           LSU1 byte 2 or byte 6
 * Lane 3 -- TTM0
 *           TTM1
 *           LSU0 byte 3
 *           LSU1 byte 3 or byte 7
 *
 * Adders:
 *  Add byte lane for PMC (above), bit 0+4, 1+5, 2+6, 3+7
 */

struct pmc_ppc970_event {
	enum pmc_event pe_event;
	uint32_t pe_flags;
#define PMC_PPC970_FLAG_PMCS	0x000000ff
#define  PMC_PPC970_FLAG_PMC1	0x01
#define  PMC_PPC970_FLAG_PMC2	0x02
#define  PMC_PPC970_FLAG_PMC3	0x04
#define  PMC_PPC970_FLAG_PMC4	0x08
#define  PMC_PPC970_FLAG_PMC5	0x10
#define  PMC_PPC970_FLAG_PMC6	0x20
#define  PMC_PPC970_FLAG_PMC7	0x40
#define  PMC_PPC970_FLAG_PMC8	0x80
	uint32_t pe_code;
};

static struct pmc_ppc970_event ppc970_event_codes[] = {
	{PMC_EV_PPC970_INSTR_COMPLETED,
	    .pe_flags = PMC_PPC970_FLAG_PMCS,
	    .pe_code = 0x09
	},
	{PMC_EV_PPC970_MARKED_GROUP_DISPATCH,
		.pe_flags = PMC_PPC970_FLAG_PMC1,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_MARKED_STORE_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC1,
		.pe_code = 0x03
	},
	{PMC_EV_PPC970_GCT_EMPTY,
		.pe_flags = PMC_PPC970_FLAG_PMC1,
		.pe_code = 0x04
	},
	{PMC_EV_PPC970_RUN_CYCLES,
		.pe_flags = PMC_PPC970_FLAG_PMC1,
		.pe_code = 0x05
	},
	{PMC_EV_PPC970_OVERFLOW,
		.pe_flags = PMC_PPC970_FLAG_PMCS,
		.pe_code = 0x0a
	},
	{PMC_EV_PPC970_CYCLES,
		.pe_flags = PMC_PPC970_FLAG_PMCS,
		.pe_code = 0x0f
	},
	{PMC_EV_PPC970_THRESHOLD_TIMEOUT,
		.pe_flags = PMC_PPC970_FLAG_PMC2,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_GROUP_DISPATCH,
		.pe_flags = PMC_PPC970_FLAG_PMC2,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_BR_MARKED_INSTR_FINISH,
		.pe_flags = PMC_PPC970_FLAG_PMC2,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_GCT_EMPTY_BY_SRQ_FULL,
		.pe_flags = PMC_PPC970_FLAG_PMC2,
		.pe_code = 0xb
	},
	{PMC_EV_PPC970_STOP_COMPLETION,
		.pe_flags = PMC_PPC970_FLAG_PMC3,
		.pe_code = 0x1
	},
	{PMC_EV_PPC970_LSU_EMPTY,
		.pe_flags = PMC_PPC970_FLAG_PMC3,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_MARKED_STORE_WITH_INTR,
		.pe_flags = PMC_PPC970_FLAG_PMC3,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_CYCLES_IN_SUPER,
		.pe_flags = PMC_PPC970_FLAG_PMC3,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_VPU_MARKED_INSTR_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC3,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_FXU0_IDLE_FXU1_BUSY,
		.pe_flags = PMC_PPC970_FLAG_PMC4,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_SRQ_EMPTY,
		.pe_flags = PMC_PPC970_FLAG_PMC4,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_MARKED_GROUP_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC4,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_CR_MARKED_INSTR_FINISH,
		.pe_flags = PMC_PPC970_FLAG_PMC4,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_DISPATCH_SUCCESS,
		.pe_flags = PMC_PPC970_FLAG_PMC5,
		.pe_code = 0x1
	},
	{PMC_EV_PPC970_FXU0_IDLE_FXU1_IDLE,
		.pe_flags = PMC_PPC970_FLAG_PMC5,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_ONE_PLUS_INSTR_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC5,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_GROUP_MARKED_IDU,
		.pe_flags = PMC_PPC970_FLAG_PMC5,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_MARKED_GROUP_COMPLETE_TIMEOUT,
		.pe_flags = PMC_PPC970_FLAG_PMC5,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_FXU0_BUSY_FXU1_BUSY,
		.pe_flags = PMC_PPC970_FLAG_PMC6,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_MARKED_STORE_SENT_TO_STS,
		.pe_flags = PMC_PPC970_FLAG_PMC6,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_FXU_MARKED_INSTR_FINISHED,
		.pe_flags = PMC_PPC970_FLAG_PMC6,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_MARKED_GROUP_ISSUED,
		.pe_flags = PMC_PPC970_FLAG_PMC6,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_FXU0_BUSY_FXU1_IDLE,
		.pe_flags = PMC_PPC970_FLAG_PMC7,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_GROUP_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC7,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_FPU_MARKED_INSTR_COMPLETED,
		.pe_flags = PMC_PPC970_FLAG_PMC7,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_MARKED_INSTR_FINISH_ANY_UNIT,
		.pe_flags = PMC_PPC970_FLAG_PMC7,
		.pe_code = 0x5
	},
	{PMC_EV_PPC970_EXTERNAL_INTERRUPT,
		.pe_flags = PMC_PPC970_FLAG_PMC8,
		.pe_code = 0x2
	},
	{PMC_EV_PPC970_GROUP_DISPATCH_REJECT,
		.pe_flags = PMC_PPC970_FLAG_PMC8,
		.pe_code = 0x3
	},
	{PMC_EV_PPC970_LSU_MARKED_INSTR_FINISH,
		.pe_flags = PMC_PPC970_FLAG_PMC8,
		.pe_code = 0x4
	},
	{PMC_EV_PPC970_TIMEBASE_EVENT,
		.pe_flags = PMC_PPC970_FLAG_PMC8,
		.pe_code = 0x5
	},
#if 0
	{PMC_EV_PPC970_LSU_COMPLETION_STALL, },
	{PMC_EV_PPC970_FXU_COMPLETION_STALL, },
	{PMC_EV_PPC970_DCACHE_MISS_COMPLETION_STALL, },
	{PMC_EV_PPC970_FPU_COMPLETION_STALL, },
	{PMC_EV_PPC970_FXU_LONG_INSTR_COMPLETION_STALL, },
	{PMC_EV_PPC970_REJECT_COMPLETION_STALL, },
	{PMC_EV_PPC970_FPU_LONG_INSTR_COMPLETION_STALL, },
	{PMC_EV_PPC970_GCT_EMPTY_BY_ICACHE_MISS, },
	{PMC_EV_PPC970_REJECT_COMPLETION_STALL_ERAT_MISS, },
	{PMC_EV_PPC970_GCT_EMPTY_BY_BRANCH_MISS_PREDICT, },
#endif
};
static size_t ppc970_event_codes_size = nitems(ppc970_event_codes);

static pmc_value_t
ppc970_pmcn_read(unsigned int pmc)
{
	pmc_value_t val;

	switch (pmc) {
		case 0:
			val = mfspr(SPR_970PMC1);
			break;
		case 1:
			val = mfspr(SPR_970PMC2);
			break;
		case 2:
			val = mfspr(SPR_970PMC3);
			break;
		case 3:
			val = mfspr(SPR_970PMC4);
			break;
		case 4:
			val = mfspr(SPR_970PMC5);
			break;
		case 5:
			val = mfspr(SPR_970PMC6);
			break;
		case 6:
			val = mfspr(SPR_970PMC7);
			break;
		case 7:
			val = mfspr(SPR_970PMC8);
			break;
		default:
			panic("Invalid PMC number: %d\n", pmc);
	}

	return (val);
}

static void
ppc970_pmcn_write(unsigned int pmc, uint32_t val)
{
	switch (pmc) {
		case 0:
			mtspr(SPR_970PMC1, val);
			break;
		case 1:
			mtspr(SPR_970PMC2, val);
			break;
		case 2:
			mtspr(SPR_970PMC3, val);
			break;
		case 3:
			mtspr(SPR_970PMC4, val);
			break;
		case 4:
			mtspr(SPR_970PMC5, val);
			break;
		case 5:
			mtspr(SPR_970PMC6, val);
			break;
		case 6:
			mtspr(SPR_970PMC7, val);
			break;
		case 7:
			mtspr(SPR_970PMC8, val);
			break;
		default:
			panic("Invalid PMC number: %d\n", pmc);
	}
}

static int
ppc970_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < PPC970_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	phw = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[powerpc,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
	    __LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return 0;
}

static int
ppc970_set_pmc(int cpu, int ri, int config)
{
	struct pmc *pm;
	struct pmc_hw *phw;
	register_t pmc_mmcr;

	phw    = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	pm     = phw->phw_pmc;

	/*
	 * Disable the PMCs.
	 */
	switch (ri) {
	case 0:
	case 1:
		pmc_mmcr = mfspr(SPR_970MMCR0);
		pmc_mmcr = PPC970_SET_MMCR0_PMCSEL(pmc_mmcr, config, ri);
		mtspr(SPR_970MMCR0, pmc_mmcr);
		break;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		pmc_mmcr = mfspr(SPR_970MMCR1);
		pmc_mmcr = PPC970_SET_MMCR1_PMCSEL(pmc_mmcr, config, ri);
		mtspr(SPR_970MMCR1, pmc_mmcr);
		break;
	}
	return 0;
}

static int
ppc970_start_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct pmc_hw *phw;
	register_t pmc_mmcr;
	uint32_t config;
	int error;

	phw    = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_powerpc.pm_powerpc_evsel & ~POWERPC_PMC_ENABLE;

	error = ppc970_set_pmc(cpu, ri, config);
	
	/* The mask is inverted (enable is 1) compared to the flags in MMCR0, which
	 * are Freeze flags.
	 */
	config = ~pm->pm_md.pm_powerpc.pm_powerpc_evsel & POWERPC_PMC_ENABLE;

	pmc_mmcr = mfspr(SPR_970MMCR0);
	pmc_mmcr &= ~SPR_MMCR0_FC;
	pmc_mmcr |= config;
	mtspr(SPR_970MMCR0, pmc_mmcr);

	return 0;
}

static int
ppc970_stop_pmc(int cpu, int ri)
{
	return ppc970_set_pmc(cpu, ri, PMC970N_NONE);
}

static int
ppc970_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < PPC970_MAX_PMCS,
	    ("[powerpc,%d] illegal row index %d", __LINE__, ri));

	pm  = powerpc_pcpu[cpu]->pc_ppcpmcs[ri].phw_pmc;
	KASSERT(pm,
	    ("[core,%d] cpu %d ri %d pmc not configured", __LINE__, cpu,
		ri));

	tmp = ppc970_pmcn_read(ri);
	PMCDBG2(MDP,REA,2,"ppc-read id=%d -> %jd", ri, tmp);
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = POWERPC_PERFCTR_VALUE_TO_RELOAD_COUNT(tmp);
	else
		*v = tmp;

	return 0;
}

static int
ppc970_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < PPC970_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	pm  = powerpc_pcpu[cpu]->pc_ppcpmcs[ri].phw_pmc;

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = POWERPC_RELOAD_COUNT_TO_PERFCTR_VALUE(v);
	
	PMCDBG3(MDP,WRI,1,"powerpc-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	ppc970_pmcn_write(ri, v);

	return 0;
}

static int
ppc970_intr(struct trapframe *tf)
{
	struct pmc *pm;
	struct powerpc_cpu *pac;
	uint32_t config;
	int i, error, retval, cpu;

	cpu = curcpu;
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] out of range CPU %d", __LINE__, cpu));

	PMCDBG3(MDP,INT,1, "cpu=%d tf=%p um=%d", cpu, (void *) tf,
	    TRAPF_USERMODE(tf));

	retval = 0;

	pac = powerpc_pcpu[cpu];

	/*
	 * look for all PMCs that have interrupted:
	 * - look for a running, sampling PMC which has overflowed
	 *   and which has a valid 'struct pmc' association
	 *
	 * If found, we call a helper to process the interrupt.
	 */

	config  = mfspr(SPR_970MMCR0) & ~SPR_MMCR0_FC;
	for (i = 0; i < PPC970_MAX_PMCS; i++) {
		if ((pm = pac->pc_ppcpmcs[i].phw_pmc) == NULL ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			continue;
		}

		if (!PPC970_PMC_HAS_OVERFLOWED(i))
			continue;

		retval = 1;	/* Found an interrupting PMC. */

		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error != 0)
			ppc970_stop_pmc(cpu, i);

		/* reload sampling count. */
		ppc970_write_pmc(cpu, i, pm->pm_sc.pm_reloadcount);
	}

	if (retval)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	/* Re-enable PERF exceptions. */
	if (retval)
		mtspr(SPR_970MMCR0, config | SPR_MMCR0_PMXE);

	return (retval);
}

static int
ppc970_pcpu_init(struct pmc_mdep *md, int cpu)
{
	struct pmc_cpu *pc;
	struct powerpc_cpu *pac;
	struct pmc_hw  *phw;
	int first_ri, i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] wrong cpu number %d", __LINE__, cpu));
	PMCDBG1(MDP,INI,1,"powerpc-init cpu=%d", cpu);

	powerpc_pcpu[cpu] = pac = malloc(sizeof(struct powerpc_cpu), M_PMC,
	    M_WAITOK|M_ZERO);
	pac->pc_ppcpmcs = malloc(sizeof(struct pmc_hw) * PPC970_MAX_PMCS,
	    M_PMC, M_WAITOK|M_ZERO);
	pac->pc_class = PMC_CLASS_PPC970;

	pc = pmc_pcpu[cpu];
	first_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_POWERPC].pcd_ri;
	KASSERT(pc != NULL, ("[powerpc,%d] NULL per-cpu pointer", __LINE__));

	for (i = 0, phw = pac->pc_ppcpmcs; i < PPC970_MAX_PMCS; i++, phw++) {
		phw->phw_state    = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(i);
		phw->phw_pmc      = NULL;
		pc->pc_hwpmcs[i + first_ri] = phw;
	}

	/* Clear the MMCRs, and set FC, to disable all PMCs. */
	/* 970 PMC is not counted when set to 0x08 */
	mtspr(SPR_970MMCR0, SPR_MMCR0_FC | SPR_MMCR0_PMXE |
	    SPR_MMCR0_FCECE | SPR_MMCR0_PMC1CE | SPR_MMCR0_PMCNCE |
	    SPR_970MMCR0_PMC1SEL(0x8) | SPR_970MMCR0_PMC2SEL(0x8));
	mtspr(SPR_970MMCR1, 0x4218420);

	return 0;
}

static int
ppc970_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	register_t mmcr0 = mfspr(SPR_MMCR0);

	mmcr0 |= SPR_MMCR0_FC;
	mmcr0 &= ~SPR_MMCR0_PMXE;
	mtspr(SPR_MMCR0, mmcr0);

	free(powerpc_pcpu[cpu]->pc_ppcpmcs, M_PMC);
	free(powerpc_pcpu[cpu], M_PMC);

	return 0;
}

static int
ppc970_allocate_pmc(int cpu, int ri, struct pmc *pm,
  const struct pmc_op_pmcallocate *a)
{
	enum pmc_event pe;
	uint32_t caps, config = 0, counter = 0;
	int i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < PPC970_MAX_PMCS,
	    ("[powerpc,%d] illegal row index %d", __LINE__, ri));

	caps = a->pm_caps;

	pe = a->pm_ev;

	if (pe < PMC_EV_PPC970_FIRST || pe > PMC_EV_PPC970_LAST)
		return (EINVAL);

	for (i = 0; i < ppc970_event_codes_size; i++) {
		if (ppc970_event_codes[i].pe_event == pe) {
			config = ppc970_event_codes[i].pe_code;
			counter =  ppc970_event_codes[i].pe_flags;
			break;
		}
	}
	if (i == ppc970_event_codes_size)
		return (EINVAL);

	if ((counter & (1 << ri)) == 0)
		return (EINVAL);

	if (caps & PMC_CAP_SYSTEM)
		config |= POWERPC_PMC_KERNEL_ENABLE;
	if (caps & PMC_CAP_USER)
		config |= POWERPC_PMC_USER_ENABLE;
	if ((caps & (PMC_CAP_USER | PMC_CAP_SYSTEM)) == 0)
		config |= POWERPC_PMC_ENABLE;

	pm->pm_md.pm_powerpc.pm_powerpc_evsel = config;

	PMCDBG2(MDP,ALL,2,"powerpc-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}

static int
ppc970_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < PPC970_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	phw = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	KASSERT(phw->phw_pmc == NULL,
	    ("[powerpc,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	return 0;
}

int
pmc_ppc970_initialize(struct pmc_mdep *pmc_mdep)
{
	struct pmc_classdep *pcd;
	
	pmc_mdep->pmd_cputype = PMC_CPU_PPC_970;

	pcd = &pmc_mdep->pmd_classdep[PMC_MDEP_CLASS_INDEX_POWERPC];
	pcd->pcd_caps  = POWERPC_PMC_CAPS;
	pcd->pcd_class = PMC_CLASS_PPC970;
	pcd->pcd_num   = PPC970_MAX_PMCS;
	pcd->pcd_ri    = pmc_mdep->pmd_npmc;
	pcd->pcd_width = 32;

	pcd->pcd_allocate_pmc   = ppc970_allocate_pmc;
	pcd->pcd_config_pmc     = ppc970_config_pmc;
	pcd->pcd_pcpu_fini      = ppc970_pcpu_fini;
	pcd->pcd_pcpu_init      = ppc970_pcpu_init;
	pcd->pcd_describe       = powerpc_describe;
	pcd->pcd_get_config     = powerpc_get_config;
	pcd->pcd_read_pmc       = ppc970_read_pmc;
	pcd->pcd_release_pmc    = ppc970_release_pmc;
	pcd->pcd_start_pmc      = ppc970_start_pmc;
	pcd->pcd_stop_pmc       = ppc970_stop_pmc;
 	pcd->pcd_write_pmc      = ppc970_write_pmc;

	pmc_mdep->pmd_npmc     += PPC970_MAX_PMCS;
	pmc_mdep->pmd_intr      = ppc970_intr;

	return (0);
}
