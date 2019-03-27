/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Fabien Thomas
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
#include <sys/mutex.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include "hwpmc_soft.h"

/*
 * Software PMC support.
 */

#define	SOFT_CAPS (PMC_CAP_READ | PMC_CAP_WRITE | PMC_CAP_INTERRUPT | \
    PMC_CAP_USER | PMC_CAP_SYSTEM)

struct soft_descr {
	struct pmc_descr pm_descr;  /* "base class" */
};

static struct soft_descr soft_pmcdesc[SOFT_NPMCS] =
{
#define	SOFT_PMCDESCR(N)				\
	{						\
		.pm_descr =				\
		{					\
			.pd_name = #N,			\
			.pd_class = PMC_CLASS_SOFT,	\
			.pd_caps = SOFT_CAPS,		\
			.pd_width = 64			\
		},					\
	}

	SOFT_PMCDESCR(SOFT0),
	SOFT_PMCDESCR(SOFT1),
	SOFT_PMCDESCR(SOFT2),
	SOFT_PMCDESCR(SOFT3),
	SOFT_PMCDESCR(SOFT4),
	SOFT_PMCDESCR(SOFT5),
	SOFT_PMCDESCR(SOFT6),
	SOFT_PMCDESCR(SOFT7),
	SOFT_PMCDESCR(SOFT8),
	SOFT_PMCDESCR(SOFT9),
	SOFT_PMCDESCR(SOFT10),
	SOFT_PMCDESCR(SOFT11),
	SOFT_PMCDESCR(SOFT12),
	SOFT_PMCDESCR(SOFT13),
	SOFT_PMCDESCR(SOFT14),
	SOFT_PMCDESCR(SOFT15)
};

/*
 * Per-CPU data structure.
 */

struct soft_cpu {
	struct pmc_hw	soft_hw[SOFT_NPMCS];
	pmc_value_t	soft_values[SOFT_NPMCS];
};


static struct soft_cpu **soft_pcpu;

static int
soft_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	enum pmc_event ev;
	struct pmc_soft *ps;

	(void) cpu;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	if (a->pm_class != PMC_CLASS_SOFT)
		return (EINVAL);

	if ((pm->pm_caps & SOFT_CAPS) == 0)
		return (EINVAL);

	if ((pm->pm_caps & ~SOFT_CAPS) != 0)
		return (EPERM);

	ev = pm->pm_event;
	if ((int)ev < PMC_EV_SOFT_FIRST || (int)ev > PMC_EV_SOFT_LAST)
		return (EINVAL);

	/* Check if event is registered. */
	ps = pmc_soft_ev_acquire(ev);
	if (ps == NULL)
		return (EINVAL);
	pmc_soft_ev_release(ps);
	/* Module unload is protected by pmc SX lock. */
	if (ps->ps_alloc != NULL)
		ps->ps_alloc();

	return (0);
}

static int
soft_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	phw = &soft_pcpu[cpu]->soft_hw[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[soft,%d] pm=%p phw->pm=%p hwpmc not unconfigured", __LINE__,
	    pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return (0);
}

static int
soft_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	size_t copied;
	const struct soft_descr *pd;
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	phw = &soft_pcpu[cpu]->soft_hw[ri];
	pd  = &soft_pmcdesc[ri];

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
soft_get_config(int cpu, int ri, struct pmc **ppm)
{
	(void) ri;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	*ppm = soft_pcpu[cpu]->soft_hw[ri].phw_pmc;
	return (0);
}

static int
soft_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	int ri;
	struct pmc_cpu *pc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal cpu %d", __LINE__, cpu));
	KASSERT(soft_pcpu[cpu] != NULL, ("[soft,%d] null pcpu", __LINE__));

	free(soft_pcpu[cpu], M_PMC);
	soft_pcpu[cpu] = NULL;

	ri = md->pmd_classdep[PMC_CLASS_INDEX_SOFT].pcd_ri;

	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] ri=%d", __LINE__, ri));

	pc = pmc_pcpu[cpu];
	pc->pc_hwpmcs[ri] = NULL;

	return (0);
}

static int
soft_pcpu_init(struct pmc_mdep *md, int cpu)
{
	int first_ri, n;
	struct pmc_cpu *pc;
	struct soft_cpu *soft_pc;
	struct pmc_hw *phw;


	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal cpu %d", __LINE__, cpu));
	KASSERT(soft_pcpu, ("[soft,%d] null pcpu", __LINE__));
	KASSERT(soft_pcpu[cpu] == NULL, ("[soft,%d] non-null per-cpu",
	    __LINE__));

	soft_pc = malloc(sizeof(struct soft_cpu), M_PMC, M_WAITOK|M_ZERO);
	pc = pmc_pcpu[cpu];

	KASSERT(pc != NULL, ("[soft,%d] cpu %d null per-cpu", __LINE__, cpu));

	soft_pcpu[cpu] = soft_pc;
	phw = soft_pc->soft_hw;
	first_ri = md->pmd_classdep[PMC_CLASS_INDEX_SOFT].pcd_ri;

	for (n = 0; n < SOFT_NPMCS; n++, phw++) {
		phw->phw_state = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(n);
		phw->phw_pmc = NULL;
		pc->pc_hwpmcs[n + first_ri] = phw;
	}

	return (0);
}

static int
soft_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	const struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	phw = &soft_pcpu[cpu]->soft_hw[ri];
	pm  = phw->phw_pmc;

	KASSERT(pm != NULL,
	    ("[soft,%d] no owner for PHW [cpu%d,pmc%d]", __LINE__, cpu, ri));

	PMCDBG1(MDP,REA,1,"soft-read id=%d", ri);

	*v = soft_pcpu[cpu]->soft_values[ri];

	return (0);
}

static int
soft_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;
	const struct soft_descr *pd;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	pm = soft_pcpu[cpu]->soft_hw[ri].phw_pmc;
	pd = &soft_pmcdesc[ri];

	KASSERT(pm,
	    ("[soft,%d] cpu %d ri %d pmc not configured", __LINE__, cpu, ri));

	PMCDBG3(MDP,WRI,1, "soft-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	soft_pcpu[cpu]->soft_values[ri] = v;

	return (0);
}

static int
soft_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;
	enum pmc_event ev;
	struct pmc_soft *ps;

	(void) pmc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	phw = &soft_pcpu[cpu]->soft_hw[ri];

	KASSERT(phw->phw_pmc == NULL,
	    ("[soft,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	ev = pmc->pm_event;

	/* Check if event is registered. */
	ps = pmc_soft_ev_acquire(ev);
	KASSERT(ps != NULL,
	    ("[soft,%d] unregistered event %d", __LINE__, ev));
	pmc_soft_ev_release(ps);
	/* Module unload is protected by pmc SX lock. */
	if (ps->ps_release != NULL)
		ps->ps_release();
	return (0);
}

static int
soft_start_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct soft_cpu *pc;
	struct pmc_soft *ps;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	pc = soft_pcpu[cpu];
	pm = pc->soft_hw[ri].phw_pmc;

	KASSERT(pm,
	    ("[soft,%d] cpu %d ri %d pmc not configured", __LINE__, cpu, ri));

	ps = pmc_soft_ev_acquire(pm->pm_event);
	if (ps == NULL)
		return (EINVAL);
	atomic_add_int(&ps->ps_running, 1);
	pmc_soft_ev_release(ps);

	return (0);
}

static int
soft_stop_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct soft_cpu *pc;
	struct pmc_soft *ps;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[soft,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < SOFT_NPMCS,
	    ("[soft,%d] illegal row-index %d", __LINE__, ri));

	pc = soft_pcpu[cpu];
	pm = pc->soft_hw[ri].phw_pmc;

	KASSERT(pm,
	    ("[soft,%d] cpu %d ri %d pmc not configured", __LINE__, cpu, ri));

	ps = pmc_soft_ev_acquire(pm->pm_event);
	/* event unregistered ? */
	if (ps != NULL) {
		atomic_subtract_int(&ps->ps_running, 1);
		pmc_soft_ev_release(ps);
	}

	return (0);
}

int
pmc_soft_intr(struct pmckern_soft *ks)
{
	struct pmc *pm;
	struct soft_cpu *pc;
	int ri, processed, error, user_mode;

	KASSERT(ks->pm_cpu >= 0 && ks->pm_cpu < pmc_cpu_max(),
	    ("[soft,%d] CPU %d out of range", __LINE__, ks->pm_cpu));

	processed = 0;
	pc = soft_pcpu[ks->pm_cpu];

	for (ri = 0; ri < SOFT_NPMCS; ri++) {

		pm = pc->soft_hw[ri].phw_pmc;
		if (pm == NULL ||
		    pm->pm_state != PMC_STATE_RUNNING ||
		    pm->pm_event != ks->pm_ev) {
				continue;
		}

		processed = 1;
		if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			if ((pc->soft_values[ri]--) <= 0)
				pc->soft_values[ri] += pm->pm_sc.pm_reloadcount;
			else
				continue;
			user_mode = TRAPF_USERMODE(ks->pm_tf);
			error = pmc_process_interrupt(PMC_SR, pm, ks->pm_tf);
			if (error) {
				soft_stop_pmc(ks->pm_cpu, ri);
				continue;
			}

			if (user_mode) {
				/* If in user mode setup AST to process
				 * callchain out of interrupt context.
				 */
				curthread->td_flags |= TDF_ASTPENDING;
			}
		} else
			pc->soft_values[ri]++;
	}
	if (processed)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	return (processed);
}

void
pmc_soft_initialize(struct pmc_mdep *md)
{
	struct pmc_classdep *pcd;

	/* Add SOFT PMCs. */
	soft_pcpu = malloc(sizeof(struct soft_cpu *) * pmc_cpu_max(), M_PMC,
	    M_ZERO|M_WAITOK);

	pcd = &md->pmd_classdep[PMC_CLASS_INDEX_SOFT];

	pcd->pcd_caps	= SOFT_CAPS;
	pcd->pcd_class	= PMC_CLASS_SOFT;
	pcd->pcd_num	= SOFT_NPMCS;
	pcd->pcd_ri	= md->pmd_npmc;
	pcd->pcd_width	= 64;

	pcd->pcd_allocate_pmc = soft_allocate_pmc;
	pcd->pcd_config_pmc   = soft_config_pmc;
	pcd->pcd_describe     = soft_describe;
	pcd->pcd_get_config   = soft_get_config;
	pcd->pcd_get_msr      = NULL;
	pcd->pcd_pcpu_init    = soft_pcpu_init;
	pcd->pcd_pcpu_fini    = soft_pcpu_fini;
	pcd->pcd_read_pmc     = soft_read_pmc;
	pcd->pcd_write_pmc    = soft_write_pmc;
	pcd->pcd_release_pmc  = soft_release_pmc;
	pcd->pcd_start_pmc    = soft_start_pmc;
	pcd->pcd_stop_pmc     = soft_stop_pmc;

	md->pmd_npmc += SOFT_NPMCS;
}

void
pmc_soft_finalize(struct pmc_mdep *md)
{
#ifdef	INVARIANTS
	int i, ncpus;

	ncpus = pmc_cpu_max();
	for (i = 0; i < ncpus; i++)
		KASSERT(soft_pcpu[i] == NULL, ("[soft,%d] non-null pcpu cpu %d",
		    __LINE__, i));

	KASSERT(md->pmd_classdep[PMC_CLASS_INDEX_SOFT].pcd_class ==
	    PMC_CLASS_SOFT, ("[soft,%d] class mismatch", __LINE__));
#endif
	free(soft_pcpu, M_PMC);
	soft_pcpu = NULL;
}
