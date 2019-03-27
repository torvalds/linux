/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Joseph Koshy
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

#include <sys/param.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/systm.h>

#include <machine/specialreg.h>

/*
 * TSC support.
 */

#define	TSC_CAPS	PMC_CAP_READ

struct tsc_descr {
	struct pmc_descr pm_descr;  /* "base class" */
};

static struct tsc_descr tsc_pmcdesc[TSC_NPMCS] =
{
    {
	.pm_descr =
	{
		.pd_name  = "TSC",
		.pd_class = PMC_CLASS_TSC,
		.pd_caps  = TSC_CAPS,
		.pd_width = 64
	}
    }
};

/*
 * Per-CPU data structure for TSCs.
 */

struct tsc_cpu {
	struct pmc_hw	tc_hw;
};

static struct tsc_cpu **tsc_pcpu;

static int
tsc_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	(void) cpu;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < TSC_NPMCS,
	    ("[tsc,%d] illegal row index %d", __LINE__, ri));

	if (a->pm_class != PMC_CLASS_TSC)
		return (EINVAL);

	if ((pm->pm_caps & TSC_CAPS) == 0)
		return (EINVAL);

	if ((pm->pm_caps & ~TSC_CAPS) != 0)
		return (EPERM);

	if (a->pm_ev != PMC_EV_TSC_TSC ||
	    a->pm_mode != PMC_MODE_SC)
		return (EINVAL);

	return (0);
}

static int
tsc_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	phw = &tsc_pcpu[cpu]->tc_hw;

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[tsc,%d] pm=%p phw->pm=%p hwpmc not unconfigured", __LINE__,
	    pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return (0);
}

static int
tsc_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	size_t copied;
	const struct tsc_descr *pd;
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	phw = &tsc_pcpu[cpu]->tc_hw;
	pd  = &tsc_pmcdesc[ri];

	if ((error = copystr(pd->pm_descr.pd_name, pi->pm_name,
	    PMC_NAME_MAX, &copied)) != 0)
		return (error);

	pi->pm_class = pd->pm_descr.pd_class;

	if (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) {
		pi->pm_enabled = TRUE;
		*ppmc          = phw->phw_pmc;
	} else {
		pi->pm_enabled = FALSE;
		*ppmc          = NULL;
	}

	return (0);
}

static int
tsc_get_config(int cpu, int ri, struct pmc **ppm)
{
	(void) ri;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	*ppm = tsc_pcpu[cpu]->tc_hw.phw_pmc;

	return (0);
}

static int
tsc_get_msr(int ri, uint32_t *msr)
{
	(void) ri;

	KASSERT(ri >= 0 && ri < TSC_NPMCS,
	    ("[tsc,%d] ri %d out of range", __LINE__, ri));

	*msr = MSR_TSC;

	return (0);
}

static int
tsc_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	int ri;
	struct pmc_cpu *pc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal cpu %d", __LINE__, cpu));
	KASSERT(tsc_pcpu[cpu] != NULL, ("[tsc,%d] null pcpu", __LINE__));

	free(tsc_pcpu[cpu], M_PMC);
	tsc_pcpu[cpu] = NULL;

	ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_TSC].pcd_ri;

	pc = pmc_pcpu[cpu];
	pc->pc_hwpmcs[ri] = NULL;

	return (0);
}

static int
tsc_pcpu_init(struct pmc_mdep *md, int cpu)
{
	int ri;
	struct pmc_cpu *pc;
	struct tsc_cpu *tsc_pc;


	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal cpu %d", __LINE__, cpu));
	KASSERT(tsc_pcpu, ("[tsc,%d] null pcpu", __LINE__));
	KASSERT(tsc_pcpu[cpu] == NULL, ("[tsc,%d] non-null per-cpu",
	    __LINE__));

	tsc_pc = malloc(sizeof(struct tsc_cpu), M_PMC, M_WAITOK|M_ZERO);

	tsc_pc->tc_hw.phw_state = PMC_PHW_FLAG_IS_ENABLED |
	    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(0) |
	    PMC_PHW_FLAG_IS_SHAREABLE;

	tsc_pcpu[cpu] = tsc_pc;

	ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_TSC].pcd_ri;

	KASSERT(pmc_pcpu, ("[tsc,%d] null generic pcpu", __LINE__));

	pc = pmc_pcpu[cpu];

	KASSERT(pc, ("[tsc,%d] null generic per-cpu", __LINE__));

	pc->pc_hwpmcs[ri] = &tsc_pc->tc_hw;

	return (0);
}

static int
tsc_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	enum pmc_mode mode;
	const struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal ri %d", __LINE__, ri));

	phw = &tsc_pcpu[cpu]->tc_hw;
	pm  = phw->phw_pmc;

	KASSERT(pm != NULL,
	    ("[tsc,%d] no owner for PHW [cpu%d,pmc%d]", __LINE__, cpu, ri));

	mode = PMC_TO_MODE(pm);

	KASSERT(mode == PMC_MODE_SC,
	    ("[tsc,%d] illegal pmc mode %d", __LINE__, mode));

	PMCDBG1(MDP,REA,1,"tsc-read id=%d", ri);

	*v = rdtsc();

	return (0);
}

static int
tsc_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	(void) pmc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0,
	    ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	phw = &tsc_pcpu[cpu]->tc_hw;

	KASSERT(phw->phw_pmc == NULL,
	    ("[tsc,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	/*
	 * Nothing to do.
	 */
	return (0);
}

static int
tsc_start_pmc(int cpu, int ri)
{
	(void) cpu;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	return (0);	/* TSCs are always running. */
}

static int
tsc_stop_pmc(int cpu, int ri)
{
	(void) cpu; (void) ri;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	return (0);	/* Cannot actually stop a TSC. */
}

static int
tsc_write_pmc(int cpu, int ri, pmc_value_t v)
{
	(void) cpu; (void) ri; (void) v;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[tsc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri == 0, ("[tsc,%d] illegal row-index %d", __LINE__, ri));

	/*
	 * The TSCs are used as timecounters by the kernel, so even
	 * though some i386 CPUs support writeable TSCs, we don't
	 * support writing changing TSC values through the HWPMC API.
	 */
	return (0);
}

int
pmc_tsc_initialize(struct pmc_mdep *md, int maxcpu)
{
	struct pmc_classdep *pcd;

	KASSERT(md != NULL, ("[tsc,%d] md is NULL", __LINE__));
	KASSERT(md->pmd_nclass >= 1, ("[tsc,%d] dubious md->nclass %d",
	    __LINE__, md->pmd_nclass));

	tsc_pcpu = malloc(sizeof(struct tsc_cpu *) * maxcpu, M_PMC,
	    M_ZERO|M_WAITOK);

	pcd = &md->pmd_classdep[PMC_MDEP_CLASS_INDEX_TSC];

	pcd->pcd_caps	= PMC_CAP_READ;
	pcd->pcd_class	= PMC_CLASS_TSC;
	pcd->pcd_num	= TSC_NPMCS;
	pcd->pcd_ri	= md->pmd_npmc;
	pcd->pcd_width	= 64;

	pcd->pcd_allocate_pmc = tsc_allocate_pmc;
	pcd->pcd_config_pmc   = tsc_config_pmc;
	pcd->pcd_describe     = tsc_describe;
	pcd->pcd_get_config   = tsc_get_config;
	pcd->pcd_get_msr      = tsc_get_msr;
	pcd->pcd_pcpu_init    = tsc_pcpu_init;
	pcd->pcd_pcpu_fini    = tsc_pcpu_fini;
	pcd->pcd_read_pmc     = tsc_read_pmc;
	pcd->pcd_release_pmc  = tsc_release_pmc;
	pcd->pcd_start_pmc    = tsc_start_pmc;
	pcd->pcd_stop_pmc     = tsc_stop_pmc;
	pcd->pcd_write_pmc    = tsc_write_pmc;

	md->pmd_npmc += TSC_NPMCS;

	return (0);
}

void
pmc_tsc_finalize(struct pmc_mdep *md)
{
#ifdef	INVARIANTS
	int i, ncpus;

	ncpus = pmc_cpu_max();
	for (i = 0; i < ncpus; i++)
		KASSERT(tsc_pcpu[i] == NULL, ("[tsc,%d] non-null pcpu cpu %d",
		    __LINE__, i));

	KASSERT(md->pmd_classdep[PMC_MDEP_CLASS_INDEX_TSC].pcd_class ==
	    PMC_CLASS_TSC, ("[tsc,%d] class mismatch", __LINE__));

#else
	(void) md;
#endif

	free(tsc_pcpu, M_PMC);
	tsc_pcpu = NULL;
}
