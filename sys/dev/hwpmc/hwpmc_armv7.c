/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/pmc_mdep.h>
#include <machine/cpu.h>

static int armv7_npmcs;

struct armv7_event_code_map {
	enum pmc_event	pe_ev;
	uint8_t		pe_code;
};

#define	PMC_EV_CPU_CYCLES	0xFF

/*
 * Per-processor information.
 */
struct armv7_cpu {
	struct pmc_hw   *pc_armv7pmcs;
};

static struct armv7_cpu **armv7_pcpu;

/*
 * Interrupt Enable Set Register
 */
static __inline void
armv7_interrupt_enable(uint32_t pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	cp15_pminten_set(reg);
}

/*
 * Interrupt Clear Set Register
 */
static __inline void
armv7_interrupt_disable(uint32_t pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	cp15_pminten_clr(reg);
}

/*
 * Counter Set Enable Register
 */
static __inline void
armv7_counter_enable(unsigned int pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	cp15_pmcnten_set(reg);
}

/*
 * Counter Clear Enable Register
 */
static __inline void
armv7_counter_disable(unsigned int pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	cp15_pmcnten_clr(reg);
}

/*
 * Performance Count Register N
 */
static uint32_t
armv7_pmcn_read(unsigned int pmc)
{

	KASSERT(pmc < armv7_npmcs, ("%s: illegal PMC number %d", __func__, pmc));

	cp15_pmselr_set(pmc);
	return (cp15_pmxevcntr_get());
}

static uint32_t
armv7_pmcn_write(unsigned int pmc, uint32_t reg)
{

	KASSERT(pmc < armv7_npmcs, ("%s: illegal PMC number %d", __func__, pmc));

	cp15_pmselr_set(pmc);
	cp15_pmxevcntr_set(reg);

	return (reg);
}

static int
armv7_allocate_pmc(int cpu, int ri, struct pmc *pm,
  const struct pmc_op_pmcallocate *a)
{
	struct armv7_cpu *pac;
	enum pmc_event pe;
	uint32_t config;
	uint32_t caps;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] illegal row index %d", __LINE__, ri));

	pac = armv7_pcpu[cpu];

	caps = a->pm_caps;
	if (a->pm_class != PMC_CLASS_ARMV7)
		return (EINVAL);
	pe = a->pm_ev;

	config = (pe & EVENT_ID_MASK);
	pm->pm_md.pm_armv7.pm_armv7_evsel = config;

	PMCDBG2(MDP, ALL, 2, "armv7-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}


static int
armv7_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	pmc_value_t tmp;
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] illegal row index %d", __LINE__, ri));

	pm  = armv7_pcpu[cpu]->pc_armv7pmcs[ri].phw_pmc;

	if (pm->pm_md.pm_armv7.pm_armv7_evsel == PMC_EV_CPU_CYCLES)
		tmp = (uint32_t)cp15_pmccntr_get();
	else
		tmp = armv7_pmcn_read(ri);
	tmp += 0x100000000llu * pm->pm_overflowcnt;

	PMCDBG2(MDP, REA, 2, "armv7-read id=%d -> %jd", ri, tmp);
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = ARMV7_PERFCTR_VALUE_TO_RELOAD_COUNT(tmp);
	else
		*v = tmp;

	return 0;
}

static int
armv7_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] illegal row-index %d", __LINE__, ri));

	pm  = armv7_pcpu[cpu]->pc_armv7pmcs[ri].phw_pmc;

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = ARMV7_RELOAD_COUNT_TO_PERFCTR_VALUE(v);
	
	PMCDBG3(MDP, WRI, 1, "armv7-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	if (pm->pm_md.pm_armv7.pm_armv7_evsel == PMC_EV_CPU_CYCLES)
		cp15_pmccntr_set(v);
	else
		armv7_pmcn_write(ri, v);

	return 0;
}

static int
armv7_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP, CFG, 1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] illegal row-index %d", __LINE__, ri));

	phw = &armv7_pcpu[cpu]->pc_armv7pmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[armv7,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
	    __LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return 0;
}

static int
armv7_start_pmc(int cpu, int ri)
{
	struct pmc_hw *phw;
	uint32_t config;
	struct pmc *pm;

	phw    = &armv7_pcpu[cpu]->pc_armv7pmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_armv7.pm_armv7_evsel;

	pm->pm_overflowcnt = 0;

	/*
	 * Configure the event selection.
	 */
	if (config != PMC_EV_CPU_CYCLES) {
		cp15_pmselr_set(ri);
		cp15_pmxevtyper_set(config);
	} else
		ri = 31;

	/*
	 * Enable the PMC.
	 */
	armv7_interrupt_enable(ri);
	armv7_counter_enable(ri);

	return 0;
}

static int
armv7_stop_pmc(int cpu, int ri)
{
	struct pmc_hw *phw;
	struct pmc *pm;
	uint32_t config;

	phw    = &armv7_pcpu[cpu]->pc_armv7pmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_armv7.pm_armv7_evsel;
	if (config == PMC_EV_CPU_CYCLES)
		ri = 31;

	/*
	 * Disable the PMCs.
	 */
	armv7_counter_disable(ri);
	armv7_interrupt_disable(ri);

	return 0;
}

static int
armv7_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] illegal row-index %d", __LINE__, ri));

	phw = &armv7_pcpu[cpu]->pc_armv7pmcs[ri];
	KASSERT(phw->phw_pmc == NULL,
	    ("[armv7,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	return 0;
}

static int
armv7_intr(struct trapframe *tf)
{
	struct armv7_cpu *pc;
	int retval, ri;
	struct pmc *pm;
	int error;
	int reg, cpu;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] CPU %d out of range", __LINE__, cpu));

	retval = 0;
	cpu = curcpu;
	pc = armv7_pcpu[cpu];

	for (ri = 0; ri < armv7_npmcs; ri++) {
		pm = armv7_pcpu[cpu]->pc_armv7pmcs[ri].phw_pmc;
		if (pm == NULL)
			continue;

		/* Check if counter has overflowed */
		if (pm->pm_md.pm_armv7.pm_armv7_evsel == PMC_EV_CPU_CYCLES)
			reg = (1 << 31);
		else
			reg = (1 << ri);

		if ((cp15_pmovsr_get() & reg) == 0) {
			continue;
		}

		/* Clear Overflow Flag */
		cp15_pmovsr_set(reg);

		retval = 1; /* Found an interrupting PMC. */

		if (!PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			pm->pm_overflowcnt += 1;
			continue;
		}
		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error)
			armv7_stop_pmc(cpu, ri);

		/* Reload sampling count */
		armv7_write_pmc(cpu, ri, pm->pm_sc.pm_reloadcount);
	}

	return (retval);
}

static int
armv7_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	char armv7_name[PMC_NAME_MAX];
	struct pmc_hw *phw;
	int error;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d], illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < armv7_npmcs,
	    ("[armv7,%d] row-index %d out of range", __LINE__, ri));

	phw = &armv7_pcpu[cpu]->pc_armv7pmcs[ri];
	snprintf(armv7_name, sizeof(armv7_name), "ARMV7-%d", ri);
	if ((error = copystr(armv7_name, pi->pm_name, PMC_NAME_MAX,
	    NULL)) != 0)
		return error;
	pi->pm_class = PMC_CLASS_ARMV7;
	if (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) {
		pi->pm_enabled = TRUE;
		*ppmc = phw->phw_pmc;
	} else {
		pi->pm_enabled = FALSE;
		*ppmc = NULL;
	}

	return (0);
}

static int
armv7_get_config(int cpu, int ri, struct pmc **ppm)
{

	*ppm = armv7_pcpu[cpu]->pc_armv7pmcs[ri].phw_pmc;

	return 0;
}

/*
 * XXX don't know what we should do here.
 */
static int
armv7_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{

	return 0;
}

static int
armv7_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{

	return 0;
}

static int
armv7_pcpu_init(struct pmc_mdep *md, int cpu)
{
	struct armv7_cpu *pac;
	struct pmc_hw  *phw;
	struct pmc_cpu *pc;
	uint32_t pmnc;
	int first_ri;
	int i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[armv7,%d] wrong cpu number %d", __LINE__, cpu));
	PMCDBG1(MDP, INI, 1, "armv7-init cpu=%d", cpu);

	armv7_pcpu[cpu] = pac = malloc(sizeof(struct armv7_cpu), M_PMC,
	    M_WAITOK|M_ZERO);

	pac->pc_armv7pmcs = malloc(sizeof(struct pmc_hw) * armv7_npmcs,
	    M_PMC, M_WAITOK|M_ZERO);
	pc = pmc_pcpu[cpu];
	first_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_ARMV7].pcd_ri;
	KASSERT(pc != NULL, ("[armv7,%d] NULL per-cpu pointer", __LINE__));

	for (i = 0, phw = pac->pc_armv7pmcs; i < armv7_npmcs; i++, phw++) {
		phw->phw_state    = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(i);
		phw->phw_pmc      = NULL;
		pc->pc_hwpmcs[i + first_ri] = phw;
	}

	pmnc = 0xffffffff;
	cp15_pmcnten_clr(pmnc);
	cp15_pminten_clr(pmnc);
	cp15_pmovsr_set(pmnc);

	/* Enable unit */
	pmnc = cp15_pmcr_get();
	pmnc |= ARMV7_PMNC_ENABLE;
	cp15_pmcr_set(pmnc);

	return 0;
}

static int
armv7_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	uint32_t pmnc;

	pmnc = cp15_pmcr_get();
	pmnc &= ~ARMV7_PMNC_ENABLE;
	cp15_pmcr_set(pmnc);

	pmnc = 0xffffffff;
	cp15_pmcnten_clr(pmnc);
	cp15_pminten_clr(pmnc);
	cp15_pmovsr_set(pmnc);

	return 0;
}

struct pmc_mdep *
pmc_armv7_initialize()
{
	struct pmc_mdep *pmc_mdep;
	struct pmc_classdep *pcd;
	int idcode;
	int reg;

	reg = cp15_pmcr_get();
	armv7_npmcs = (reg >> ARMV7_PMNC_N_SHIFT) & \
				ARMV7_PMNC_N_MASK;
	idcode = (reg & ARMV7_IDCODE_MASK) >> ARMV7_IDCODE_SHIFT;

	PMCDBG1(MDP, INI, 1, "armv7-init npmcs=%d", armv7_npmcs);
	
	/*
	 * Allocate space for pointers to PMC HW descriptors and for
	 * the MDEP structure used by MI code.
	 */
	armv7_pcpu = malloc(sizeof(struct armv7_cpu *) * pmc_cpu_max(),
		M_PMC, M_WAITOK | M_ZERO);

	/* Just one class */
	pmc_mdep = pmc_mdep_alloc(1);

	switch (idcode) {
	case ARMV7_IDCODE_CORTEX_A9:
		pmc_mdep->pmd_cputype = PMC_CPU_ARMV7_CORTEX_A9;
		break;
	default:
	case ARMV7_IDCODE_CORTEX_A8:
		/*
		 * On A8 we implemented common events only,
		 * so use it for the rest of machines.
		 */
		pmc_mdep->pmd_cputype = PMC_CPU_ARMV7_CORTEX_A8;
		break;
	}

	pcd = &pmc_mdep->pmd_classdep[PMC_MDEP_CLASS_INDEX_ARMV7];
	pcd->pcd_caps  = ARMV7_PMC_CAPS;
	pcd->pcd_class = PMC_CLASS_ARMV7;
	pcd->pcd_num   = armv7_npmcs;
	pcd->pcd_ri    = pmc_mdep->pmd_npmc;
	pcd->pcd_width = 32;

	pcd->pcd_allocate_pmc   = armv7_allocate_pmc;
	pcd->pcd_config_pmc     = armv7_config_pmc;
	pcd->pcd_pcpu_fini      = armv7_pcpu_fini;
	pcd->pcd_pcpu_init      = armv7_pcpu_init;
	pcd->pcd_describe       = armv7_describe;
	pcd->pcd_get_config	= armv7_get_config;
	pcd->pcd_read_pmc       = armv7_read_pmc;
	pcd->pcd_release_pmc    = armv7_release_pmc;
	pcd->pcd_start_pmc      = armv7_start_pmc;
	pcd->pcd_stop_pmc       = armv7_stop_pmc;
	pcd->pcd_write_pmc      = armv7_write_pmc;

	pmc_mdep->pmd_intr       = armv7_intr;
	pmc_mdep->pmd_switch_in  = armv7_switch_in;
	pmc_mdep->pmd_switch_out = armv7_switch_out;
	
	pmc_mdep->pmd_npmc   += armv7_npmcs;

	return (pmc_mdep);
}

void
pmc_armv7_finalize(struct pmc_mdep *md)
{

}
