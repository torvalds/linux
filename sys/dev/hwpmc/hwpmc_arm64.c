/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory with support from ARM Ltd.
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

static int arm64_npmcs;

struct arm64_event_code_map {
	enum pmc_event	pe_ev;
	uint8_t		pe_code;
};

/*
 * Per-processor information.
 */
struct arm64_cpu {
	struct pmc_hw   *pc_arm64pmcs;
};

static struct arm64_cpu **arm64_pcpu;

/*
 * Interrupt Enable Set Register
 */
static __inline void
arm64_interrupt_enable(uint32_t pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	WRITE_SPECIALREG(PMINTENSET_EL1, reg);

	isb();
}

/*
 * Interrupt Clear Set Register
 */
static __inline void
arm64_interrupt_disable(uint32_t pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	WRITE_SPECIALREG(PMINTENCLR_EL1, reg);

	isb();
}

/*
 * Counter Set Enable Register
 */
static __inline void
arm64_counter_enable(unsigned int pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	WRITE_SPECIALREG(PMCNTENSET_EL0, reg);

	isb();
}

/*
 * Counter Clear Enable Register
 */
static __inline void
arm64_counter_disable(unsigned int pmc)
{
	uint32_t reg;

	reg = (1 << pmc);
	WRITE_SPECIALREG(PMCNTENCLR_EL0, reg);

	isb();
}

/*
 * Performance Monitors Control Register
 */
static uint32_t
arm64_pmcr_read(void)
{
	uint32_t reg;

	reg = READ_SPECIALREG(PMCR_EL0);

	return (reg);
}

static void
arm64_pmcr_write(uint32_t reg)
{

	WRITE_SPECIALREG(PMCR_EL0, reg);

	isb();
}

/*
 * Performance Count Register N
 */
static uint32_t
arm64_pmcn_read(unsigned int pmc)
{

	KASSERT(pmc < arm64_npmcs, ("%s: illegal PMC number %d", __func__, pmc));

	WRITE_SPECIALREG(PMSELR_EL0, pmc);

	isb();

	return (READ_SPECIALREG(PMXEVCNTR_EL0));
}

static void
arm64_pmcn_write(unsigned int pmc, uint32_t reg)
{

	KASSERT(pmc < arm64_npmcs, ("%s: illegal PMC number %d", __func__, pmc));

	WRITE_SPECIALREG(PMSELR_EL0, pmc);
	WRITE_SPECIALREG(PMXEVCNTR_EL0, reg);

	isb();
}

static int
arm64_allocate_pmc(int cpu, int ri, struct pmc *pm,
  const struct pmc_op_pmcallocate *a)
{
	uint32_t caps, config;
	struct arm64_cpu *pac;
	enum pmc_event pe;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] illegal row index %d", __LINE__, ri));

	pac = arm64_pcpu[cpu];

	caps = a->pm_caps;
	if (a->pm_class != PMC_CLASS_ARMV8) {
		return (EINVAL);
	}
	pe = a->pm_ev;

	config = (pe & EVENT_ID_MASK);
	pm->pm_md.pm_arm64.pm_arm64_evsel = config;

	PMCDBG2(MDP, ALL, 2, "arm64-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}


static int
arm64_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	pmc_value_t tmp;
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] illegal row index %d", __LINE__, ri));

	pm  = arm64_pcpu[cpu]->pc_arm64pmcs[ri].phw_pmc;

	tmp = arm64_pmcn_read(ri);

	PMCDBG2(MDP, REA, 2, "arm64-read id=%d -> %jd", ri, tmp);
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = ARMV8_PERFCTR_VALUE_TO_RELOAD_COUNT(tmp);
	else
		*v = tmp;

	return 0;
}

static int
arm64_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] illegal row-index %d", __LINE__, ri));

	pm  = arm64_pcpu[cpu]->pc_arm64pmcs[ri].phw_pmc;

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = ARMV8_RELOAD_COUNT_TO_PERFCTR_VALUE(v);

	PMCDBG3(MDP, WRI, 1, "arm64-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	arm64_pmcn_write(ri, v);

	return 0;
}

static int
arm64_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP, CFG, 1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] illegal row-index %d", __LINE__, ri));

	phw = &arm64_pcpu[cpu]->pc_arm64pmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[arm64,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
	    __LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return 0;
}

static int
arm64_start_pmc(int cpu, int ri)
{
	struct pmc_hw *phw;
	uint32_t config;
	struct pmc *pm;

	phw    = &arm64_pcpu[cpu]->pc_arm64pmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_arm64.pm_arm64_evsel;

	/*
	 * Configure the event selection.
	 */
	WRITE_SPECIALREG(PMSELR_EL0, ri);
	WRITE_SPECIALREG(PMXEVTYPER_EL0, config);

	isb();

	/*
	 * Enable the PMC.
	 */
	arm64_interrupt_enable(ri);
	arm64_counter_enable(ri);

	return 0;
}

static int
arm64_stop_pmc(int cpu, int ri)
{
	struct pmc_hw *phw;
	struct pmc *pm;

	phw    = &arm64_pcpu[cpu]->pc_arm64pmcs[ri];
	pm     = phw->phw_pmc;

	/*
	 * Disable the PMCs.
	 */
	arm64_counter_disable(ri);
	arm64_interrupt_disable(ri);

	return 0;
}

static int
arm64_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] illegal row-index %d", __LINE__, ri));

	phw = &arm64_pcpu[cpu]->pc_arm64pmcs[ri];
	KASSERT(phw->phw_pmc == NULL,
	    ("[arm64,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	return 0;
}

static int
arm64_intr(struct trapframe *tf)
{
	struct arm64_cpu *pc;
	int retval, ri;
	struct pmc *pm;
	int error;
	int reg, cpu;

	cpu = curcpu;
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] CPU %d out of range", __LINE__, cpu));

	retval = 0;
	pc = arm64_pcpu[cpu];

	for (ri = 0; ri < arm64_npmcs; ri++) {
		pm = arm64_pcpu[cpu]->pc_arm64pmcs[ri].phw_pmc;
		if (pm == NULL)
			continue;
		if (!PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			continue;

		/* Check if counter is overflowed */
		reg = (1 << ri);
		if ((READ_SPECIALREG(PMOVSCLR_EL0) & reg) == 0)
			continue;
		/* Clear Overflow Flag */
		WRITE_SPECIALREG(PMOVSCLR_EL0, reg);

		isb();

		retval = 1; /* Found an interrupting PMC. */
		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error)
			arm64_stop_pmc(cpu, ri);

		/* Reload sampling count */
		arm64_write_pmc(cpu, ri, pm->pm_sc.pm_reloadcount);
	}

	return (retval);
}

static int
arm64_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	char arm64_name[PMC_NAME_MAX];
	struct pmc_hw *phw;
	int error;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d], illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < arm64_npmcs,
	    ("[arm64,%d] row-index %d out of range", __LINE__, ri));

	phw = &arm64_pcpu[cpu]->pc_arm64pmcs[ri];
	snprintf(arm64_name, sizeof(arm64_name), "ARMV8-%d", ri);
	if ((error = copystr(arm64_name, pi->pm_name, PMC_NAME_MAX,
	    NULL)) != 0)
		return (error);
	pi->pm_class = PMC_CLASS_ARMV8;
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
arm64_get_config(int cpu, int ri, struct pmc **ppm)
{

	*ppm = arm64_pcpu[cpu]->pc_arm64pmcs[ri].phw_pmc;

	return (0);
}

/*
 * XXX don't know what we should do here.
 */
static int
arm64_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{

	return (0);
}

static int
arm64_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{

	return (0);
}

static int
arm64_pcpu_init(struct pmc_mdep *md, int cpu)
{
	struct arm64_cpu *pac;
	struct pmc_hw  *phw;
	struct pmc_cpu *pc;
	uint64_t pmcr;
	int first_ri;
	int i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[arm64,%d] wrong cpu number %d", __LINE__, cpu));
	PMCDBG1(MDP, INI, 1, "arm64-init cpu=%d", cpu);

	arm64_pcpu[cpu] = pac = malloc(sizeof(struct arm64_cpu), M_PMC,
	    M_WAITOK | M_ZERO);

	pac->pc_arm64pmcs = malloc(sizeof(struct pmc_hw) * arm64_npmcs,
	    M_PMC, M_WAITOK | M_ZERO);
	pc = pmc_pcpu[cpu];
	first_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_ARMV8].pcd_ri;
	KASSERT(pc != NULL, ("[arm64,%d] NULL per-cpu pointer", __LINE__));

	for (i = 0, phw = pac->pc_arm64pmcs; i < arm64_npmcs; i++, phw++) {
		phw->phw_state    = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(i);
		phw->phw_pmc      = NULL;
		pc->pc_hwpmcs[i + first_ri] = phw;
	}

	/* Enable unit */
	pmcr = arm64_pmcr_read();
	pmcr |= PMCR_E;
	arm64_pmcr_write(pmcr);

	return (0);
}

static int
arm64_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	uint32_t pmcr;

	pmcr = arm64_pmcr_read();
	pmcr &= ~PMCR_E;
	arm64_pmcr_write(pmcr);

	return (0);
}

struct pmc_mdep *
pmc_arm64_initialize()
{
	struct pmc_mdep *pmc_mdep;
	struct pmc_classdep *pcd;
	int idcode;
	int reg;

	reg = arm64_pmcr_read();
	arm64_npmcs = (reg & PMCR_N_MASK) >> PMCR_N_SHIFT;
	idcode = (reg & PMCR_IDCODE_MASK) >> PMCR_IDCODE_SHIFT;

	PMCDBG1(MDP, INI, 1, "arm64-init npmcs=%d", arm64_npmcs);

	/*
	 * Allocate space for pointers to PMC HW descriptors and for
	 * the MDEP structure used by MI code.
	 */
	arm64_pcpu = malloc(sizeof(struct arm64_cpu *) * pmc_cpu_max(),
		M_PMC, M_WAITOK | M_ZERO);

	/* Just one class */
	pmc_mdep = pmc_mdep_alloc(1);

	switch (idcode) {
	case PMCR_IDCODE_CORTEX_A57:
	case PMCR_IDCODE_CORTEX_A72:
		pmc_mdep->pmd_cputype = PMC_CPU_ARMV8_CORTEX_A57;
		break;
	default:
	case PMCR_IDCODE_CORTEX_A53:
		pmc_mdep->pmd_cputype = PMC_CPU_ARMV8_CORTEX_A53;
		break;
	}

	pcd = &pmc_mdep->pmd_classdep[PMC_MDEP_CLASS_INDEX_ARMV8];
	pcd->pcd_caps  = ARMV8_PMC_CAPS;
	pcd->pcd_class = PMC_CLASS_ARMV8;
	pcd->pcd_num   = arm64_npmcs;
	pcd->pcd_ri    = pmc_mdep->pmd_npmc;
	pcd->pcd_width = 32;

	pcd->pcd_allocate_pmc   = arm64_allocate_pmc;
	pcd->pcd_config_pmc     = arm64_config_pmc;
	pcd->pcd_pcpu_fini      = arm64_pcpu_fini;
	pcd->pcd_pcpu_init      = arm64_pcpu_init;
	pcd->pcd_describe       = arm64_describe;
	pcd->pcd_get_config     = arm64_get_config;
	pcd->pcd_read_pmc       = arm64_read_pmc;
	pcd->pcd_release_pmc    = arm64_release_pmc;
	pcd->pcd_start_pmc      = arm64_start_pmc;
	pcd->pcd_stop_pmc       = arm64_stop_pmc;
	pcd->pcd_write_pmc      = arm64_write_pmc;

	pmc_mdep->pmd_intr       = arm64_intr;
	pmc_mdep->pmd_switch_in  = arm64_switch_in;
	pmc_mdep->pmd_switch_out = arm64_switch_out;

	pmc_mdep->pmd_npmc   += arm64_npmcs;

	return (pmc_mdep);
}

void
pmc_arm64_finalize(struct pmc_mdep *md)
{

}
