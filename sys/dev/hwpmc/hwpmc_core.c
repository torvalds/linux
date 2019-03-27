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

/*
 * Intel Core PMCs.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#define	CORE_CPUID_REQUEST		0xA
#define	CORE_CPUID_REQUEST_SIZE		0x4
#define	CORE_CPUID_EAX			0x0
#define	CORE_CPUID_EBX			0x1
#define	CORE_CPUID_ECX			0x2
#define	CORE_CPUID_EDX			0x3

#define	IAF_PMC_CAPS			\
	(PMC_CAP_READ | PMC_CAP_WRITE | PMC_CAP_INTERRUPT | \
	 PMC_CAP_USER | PMC_CAP_SYSTEM)
#define	IAF_RI_TO_MSR(RI)		((RI) + (1 << 30))

#define	IAP_PMC_CAPS (PMC_CAP_INTERRUPT | PMC_CAP_USER | PMC_CAP_SYSTEM | \
    PMC_CAP_EDGE | PMC_CAP_THRESHOLD | PMC_CAP_READ | PMC_CAP_WRITE |	 \
    PMC_CAP_INVERT | PMC_CAP_QUALIFIER | PMC_CAP_PRECISE)

#define	EV_IS_NOTARCH		0
#define	EV_IS_ARCH_SUPP		1
#define	EV_IS_ARCH_NOTSUPP	-1

/*
 * "Architectural" events defined by Intel.  The values of these
 * symbols correspond to positions in the bitmask returned by
 * the CPUID.0AH instruction.
 */
enum core_arch_events {
	CORE_AE_BRANCH_INSTRUCTION_RETIRED	= 5,
	CORE_AE_BRANCH_MISSES_RETIRED		= 6,
	CORE_AE_INSTRUCTION_RETIRED		= 1,
	CORE_AE_LLC_MISSES			= 4,
	CORE_AE_LLC_REFERENCE			= 3,
	CORE_AE_UNHALTED_REFERENCE_CYCLES	= 2,
	CORE_AE_UNHALTED_CORE_CYCLES		= 0
};

static enum pmc_cputype	core_cputype;

struct core_cpu {
	volatile uint32_t	pc_resync;
	volatile uint32_t	pc_iafctrl;	/* Fixed function control. */
	volatile uint64_t	pc_globalctrl;	/* Global control register. */
	struct pmc_hw		pc_corepmcs[];
};

static struct core_cpu **core_pcpu;

static uint32_t core_architectural_events;
static uint64_t core_pmcmask;

static int core_iaf_ri;		/* relative index of fixed counters */
static int core_iaf_width;
static int core_iaf_npmc;

static int core_iap_width;
static int core_iap_npmc;
static int core_iap_wroffset;

static u_int pmc_alloc_refs;
static bool pmc_tsx_force_abort_set;

static int
core_pcpu_noop(struct pmc_mdep *md, int cpu)
{
	(void) md;
	(void) cpu;
	return (0);
}

static int
core_pcpu_init(struct pmc_mdep *md, int cpu)
{
	struct pmc_cpu *pc;
	struct core_cpu *cc;
	struct pmc_hw *phw;
	int core_ri, n, npmc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[iaf,%d] insane cpu number %d", __LINE__, cpu));

	PMCDBG1(MDP,INI,1,"core-init cpu=%d", cpu);

	core_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAP].pcd_ri;
	npmc = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAP].pcd_num;

	if (core_cputype != PMC_CPU_INTEL_CORE)
		npmc += md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAF].pcd_num;

	cc = malloc(sizeof(struct core_cpu) + npmc * sizeof(struct pmc_hw),
	    M_PMC, M_WAITOK | M_ZERO);

	core_pcpu[cpu] = cc;
	pc = pmc_pcpu[cpu];

	KASSERT(pc != NULL && cc != NULL,
	    ("[core,%d] NULL per-cpu structures cpu=%d", __LINE__, cpu));

	for (n = 0, phw = cc->pc_corepmcs; n < npmc; n++, phw++) {
		phw->phw_state 	  = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) |
		    PMC_PHW_INDEX_TO_STATE(n + core_ri);
		phw->phw_pmc	  = NULL;
		pc->pc_hwpmcs[n + core_ri]  = phw;
	}

	return (0);
}

static int
core_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	int core_ri, n, npmc;
	struct pmc_cpu *pc;
	struct core_cpu *cc;
	uint64_t msr = 0;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] insane cpu number (%d)", __LINE__, cpu));

	PMCDBG1(MDP,INI,1,"core-pcpu-fini cpu=%d", cpu);

	if ((cc = core_pcpu[cpu]) == NULL)
		return (0);

	core_pcpu[cpu] = NULL;

	pc = pmc_pcpu[cpu];

	KASSERT(pc != NULL, ("[core,%d] NULL per-cpu %d state", __LINE__,
		cpu));

	npmc = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAP].pcd_num;
	core_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAP].pcd_ri;

	for (n = 0; n < npmc; n++) {
		msr = rdmsr(IAP_EVSEL0 + n) & ~IAP_EVSEL_MASK;
		wrmsr(IAP_EVSEL0 + n, msr);
	}

	if (core_cputype != PMC_CPU_INTEL_CORE) {
		msr = rdmsr(IAF_CTRL) & ~IAF_CTRL_MASK;
		wrmsr(IAF_CTRL, msr);
		npmc += md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAF].pcd_num;
	}

	for (n = 0; n < npmc; n++)
		pc->pc_hwpmcs[n + core_ri] = NULL;

	free(cc, M_PMC);

	return (0);
}

/*
 * Fixed function counters.
 */

static pmc_value_t
iaf_perfctr_value_to_reload_count(pmc_value_t v)
{

	/* If the PMC has overflowed, return a reload count of zero. */
	if ((v & (1ULL << (core_iaf_width - 1))) == 0)
		return (0);
	v &= (1ULL << core_iaf_width) - 1;
	return (1ULL << core_iaf_width) - v;
}

static pmc_value_t
iaf_reload_count_to_perfctr_value(pmc_value_t rlc)
{
	return (1ULL << core_iaf_width) - rlc;
}

static void
tweak_tsx_force_abort(void *arg)
{
	u_int val;

	val = (uintptr_t)arg;
	wrmsr(MSR_TSX_FORCE_ABORT, val);
}

static int
iaf_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	uint8_t ev, umask;
	uint32_t caps, flags, config;
	const struct pmc_md_iap_op_pmcallocate *iap;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU %d", __LINE__, cpu));

	PMCDBG2(MDP,ALL,1, "iaf-allocate ri=%d reqcaps=0x%x", ri, pm->pm_caps);

	if (ri < 0 || ri > core_iaf_npmc)
		return (EINVAL);

	caps = a->pm_caps;

	if (a->pm_class != PMC_CLASS_IAF ||
	    (caps & IAF_PMC_CAPS) != caps)
		return (EINVAL);

	iap = &a->pm_md.pm_iap;
	config = iap->pm_iap_config;
	ev = IAP_EVSEL_GET(config);
	umask = IAP_UMASK_GET(config);

	/* INST_RETIRED.ANY */
	if (ev == 0xC0 && ri != 0)
		return (EINVAL);
	/* CPU_CLK_UNHALTED.THREAD */
	if (ev == 0x3C && ri != 1)
		return (EINVAL);
	/* CPU_CLK_UNHALTED.REF */
	if (ev == 0x0 && umask == 0x3 && ri != 2)
		return (EINVAL);

	pmc_alloc_refs++;
	if ((cpu_stdext_feature3 & CPUID_STDEXT3_TSXFA) != 0 &&
	    !pmc_tsx_force_abort_set) {
		pmc_tsx_force_abort_set = true;
		smp_rendezvous(NULL, tweak_tsx_force_abort, NULL, (void *)1);
	}

	flags = 0;
	if (config & IAP_OS)
		flags |= IAF_OS;
	if (config & IAP_USR)
		flags |= IAF_USR;
	if (config & IAP_ANY)
		flags |= IAF_ANY;
	if (config & IAP_INT)
		flags |= IAF_PMI;

	if (caps & PMC_CAP_INTERRUPT)
		flags |= IAF_PMI;
	if (caps & PMC_CAP_SYSTEM)
		flags |= IAF_OS;
	if (caps & PMC_CAP_USER)
		flags |= IAF_USR;
	if ((caps & (PMC_CAP_USER | PMC_CAP_SYSTEM)) == 0)
		flags |= (IAF_OS | IAF_USR);

	pm->pm_md.pm_iaf.pm_iaf_ctrl = (flags << (ri * 4));

	PMCDBG1(MDP,ALL,2, "iaf-allocate config=0x%jx",
	    (uintmax_t) pm->pm_md.pm_iaf.pm_iaf_ctrl);

	return (0);
}

static int
iaf_config_pmc(int cpu, int ri, struct pmc *pm)
{
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU %d", __LINE__, cpu));

	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	PMCDBG3(MDP,CFG,1, "iaf-config cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(core_pcpu[cpu] != NULL, ("[core,%d] null per-cpu %d", __LINE__,
	    cpu));

	core_pcpu[cpu]->pc_corepmcs[ri + core_iaf_ri].phw_pmc = pm;

	return (0);
}

static int
iaf_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	struct pmc_hw *phw;
	char iaf_name[PMC_NAME_MAX];

	phw = &core_pcpu[cpu]->pc_corepmcs[ri + core_iaf_ri];

	(void) snprintf(iaf_name, sizeof(iaf_name), "IAF-%d", ri);
	if ((error = copystr(iaf_name, pi->pm_name, PMC_NAME_MAX,
	    NULL)) != 0)
		return (error);

	pi->pm_class = PMC_CLASS_IAF;

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
iaf_get_config(int cpu, int ri, struct pmc **ppm)
{
	*ppm = core_pcpu[cpu]->pc_corepmcs[ri + core_iaf_ri].phw_pmc;

	return (0);
}

static int
iaf_get_msr(int ri, uint32_t *msr)
{
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[iaf,%d] ri %d out of range", __LINE__, ri));

	*msr = IAF_RI_TO_MSR(ri);

	return (0);
}

static int
iaf_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	pm = core_pcpu[cpu]->pc_corepmcs[ri + core_iaf_ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] cpu %d ri %d(%d) pmc not configured", __LINE__, cpu,
		ri, ri + core_iaf_ri));

	tmp = rdpmc(IAF_RI_TO_MSR(ri));

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = iaf_perfctr_value_to_reload_count(tmp);
	else
		*v = tmp & ((1ULL << core_iaf_width) - 1);

	PMCDBG4(MDP,REA,1, "iaf-read cpu=%d ri=%d msr=0x%x -> v=%jx", cpu, ri,
	    IAF_RI_TO_MSR(ri), *v);

	return (0);
}

static int
iaf_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	PMCDBG3(MDP,REL,1, "iaf-release cpu=%d ri=%d pm=%p", cpu, ri, pmc);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	KASSERT(core_pcpu[cpu]->pc_corepmcs[ri + core_iaf_ri].phw_pmc == NULL,
	    ("[core,%d] PHW pmc non-NULL", __LINE__));

	MPASS(pmc_alloc_refs > 0);
	if (pmc_alloc_refs-- == 1 && pmc_tsx_force_abort_set) {
		pmc_tsx_force_abort_set = false;
		smp_rendezvous(NULL, tweak_tsx_force_abort, NULL, (void *)0);
	}

	return (0);
}

static int
iaf_start_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct core_cpu *iafc;
	uint64_t msr = 0;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	PMCDBG2(MDP,STA,1,"iaf-start cpu=%d ri=%d", cpu, ri);

	iafc = core_pcpu[cpu];
	pm = iafc->pc_corepmcs[ri + core_iaf_ri].phw_pmc;

	iafc->pc_iafctrl |= pm->pm_md.pm_iaf.pm_iaf_ctrl;

 	msr = rdmsr(IAF_CTRL) & ~IAF_CTRL_MASK;
 	wrmsr(IAF_CTRL, msr | (iafc->pc_iafctrl & IAF_CTRL_MASK));

	do {
		iafc->pc_resync = 0;
		iafc->pc_globalctrl |= (1ULL << (ri + IAF_OFFSET));
 		msr = rdmsr(IA_GLOBAL_CTRL) & ~IAF_GLOBAL_CTRL_MASK;
 		wrmsr(IA_GLOBAL_CTRL, msr | (iafc->pc_globalctrl &
 					     IAF_GLOBAL_CTRL_MASK));
	} while (iafc->pc_resync != 0);

	PMCDBG4(MDP,STA,1,"iafctrl=%x(%x) globalctrl=%jx(%jx)",
	    iafc->pc_iafctrl, (uint32_t) rdmsr(IAF_CTRL),
	    iafc->pc_globalctrl, rdmsr(IA_GLOBAL_CTRL));

	return (0);
}

static int
iaf_stop_pmc(int cpu, int ri)
{
	uint32_t fc;
	struct core_cpu *iafc;
	uint64_t msr = 0;

	PMCDBG2(MDP,STO,1,"iaf-stop cpu=%d ri=%d", cpu, ri);

	iafc = core_pcpu[cpu];

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	fc = (IAF_MASK << (ri * 4));

	iafc->pc_iafctrl &= ~fc;

	PMCDBG1(MDP,STO,1,"iaf-stop iafctrl=%x", iafc->pc_iafctrl);
 	msr = rdmsr(IAF_CTRL) & ~IAF_CTRL_MASK;
 	wrmsr(IAF_CTRL, msr | (iafc->pc_iafctrl & IAF_CTRL_MASK));

	do {
		iafc->pc_resync = 0;
		iafc->pc_globalctrl &= ~(1ULL << (ri + IAF_OFFSET));
 		msr = rdmsr(IA_GLOBAL_CTRL) & ~IAF_GLOBAL_CTRL_MASK;
 		wrmsr(IA_GLOBAL_CTRL, msr | (iafc->pc_globalctrl &
 					     IAF_GLOBAL_CTRL_MASK));
	} while (iafc->pc_resync != 0);

	PMCDBG4(MDP,STO,1,"iafctrl=%x(%x) globalctrl=%jx(%jx)",
	    iafc->pc_iafctrl, (uint32_t) rdmsr(IAF_CTRL),
	    iafc->pc_globalctrl, rdmsr(IA_GLOBAL_CTRL));

	return (0);
}

static int
iaf_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct core_cpu *cc;
	struct pmc *pm;
	uint64_t msr;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iaf_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	cc = core_pcpu[cpu];
	pm = cc->pc_corepmcs[ri + core_iaf_ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] cpu %d ri %d pmc not configured", __LINE__, cpu, ri));

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = iaf_reload_count_to_perfctr_value(v);

	/* Turn off fixed counters */
	msr = rdmsr(IAF_CTRL) & ~IAF_CTRL_MASK;
	wrmsr(IAF_CTRL, msr);

	wrmsr(IAF_CTR0 + ri, v & ((1ULL << core_iaf_width) - 1));

	/* Turn on fixed counters */
	msr = rdmsr(IAF_CTRL) & ~IAF_CTRL_MASK;
	wrmsr(IAF_CTRL, msr | (cc->pc_iafctrl & IAF_CTRL_MASK));

	PMCDBG6(MDP,WRI,1, "iaf-write cpu=%d ri=%d msr=0x%x v=%jx iafctrl=%jx "
	    "pmc=%jx", cpu, ri, IAF_RI_TO_MSR(ri), v,
	    (uintmax_t) rdmsr(IAF_CTRL),
	    (uintmax_t) rdpmc(IAF_RI_TO_MSR(ri)));

	return (0);
}


static void
iaf_initialize(struct pmc_mdep *md, int maxcpu, int npmc, int pmcwidth)
{
	struct pmc_classdep *pcd;

	KASSERT(md != NULL, ("[iaf,%d] md is NULL", __LINE__));

	PMCDBG0(MDP,INI,1, "iaf-initialize");

	pcd = &md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAF];

	pcd->pcd_caps	= IAF_PMC_CAPS;
	pcd->pcd_class	= PMC_CLASS_IAF;
	pcd->pcd_num	= npmc;
	pcd->pcd_ri	= md->pmd_npmc;
	pcd->pcd_width	= pmcwidth;

	pcd->pcd_allocate_pmc	= iaf_allocate_pmc;
	pcd->pcd_config_pmc	= iaf_config_pmc;
	pcd->pcd_describe	= iaf_describe;
	pcd->pcd_get_config	= iaf_get_config;
	pcd->pcd_get_msr	= iaf_get_msr;
	pcd->pcd_pcpu_fini	= core_pcpu_noop;
	pcd->pcd_pcpu_init	= core_pcpu_noop;
	pcd->pcd_read_pmc	= iaf_read_pmc;
	pcd->pcd_release_pmc	= iaf_release_pmc;
	pcd->pcd_start_pmc	= iaf_start_pmc;
	pcd->pcd_stop_pmc	= iaf_stop_pmc;
	pcd->pcd_write_pmc	= iaf_write_pmc;

	md->pmd_npmc	       += npmc;
}

/*
 * Intel programmable PMCs.
 */

/* Sub fields of UMASK that this event supports. */
#define	IAP_M_CORE		(1 << 0) /* Core specificity */
#define	IAP_M_AGENT		(1 << 1) /* Agent specificity */
#define	IAP_M_PREFETCH		(1 << 2) /* Prefetch */
#define	IAP_M_MESI		(1 << 3) /* MESI */
#define	IAP_M_SNOOPRESPONSE	(1 << 4) /* Snoop response */
#define	IAP_M_SNOOPTYPE		(1 << 5) /* Snoop type */
#define	IAP_M_TRANSITION	(1 << 6) /* Transition */

#define	IAP_F_CORE		(0x3 << 14) /* Core specificity */
#define	IAP_F_AGENT		(0x1 << 13) /* Agent specificity */
#define	IAP_F_PREFETCH		(0x3 << 12) /* Prefetch */
#define	IAP_F_MESI		(0xF <<  8) /* MESI */
#define	IAP_F_SNOOPRESPONSE	(0xB <<  8) /* Snoop response */
#define	IAP_F_SNOOPTYPE		(0x3 <<  8) /* Snoop type */
#define	IAP_F_TRANSITION	(0x1 << 12) /* Transition */

#define	IAP_PREFETCH_RESERVED	(0x2 << 12)
#define	IAP_CORE_THIS		(0x1 << 14)
#define	IAP_CORE_ALL		(0x3 << 14)
#define	IAP_F_CMASK		0xFF000000

static pmc_value_t
iap_perfctr_value_to_reload_count(pmc_value_t v)
{

	/* If the PMC has overflowed, return a reload count of zero. */
	if ((v & (1ULL << (core_iap_width - 1))) == 0)
		return (0);
	v &= (1ULL << core_iap_width) - 1;
	return (1ULL << core_iap_width) - v;
}

static pmc_value_t
iap_reload_count_to_perfctr_value(pmc_value_t rlc)
{
	return (1ULL << core_iap_width) - rlc;
}

static int
iap_pmc_has_overflowed(int ri)
{
	uint64_t v;

	/*
	 * We treat a Core (i.e., Intel architecture v1) PMC as has
	 * having overflowed if its MSB is zero.
	 */
	v = rdpmc(ri);
	return ((v & (1ULL << (core_iap_width - 1))) == 0);
}

static int
iap_event_corei7_ok_on_counter(uint8_t evsel, int ri)
{
	uint32_t mask;

	switch (evsel) {
		/*
		 * Events valid only on counter 0, 1.
		 */
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x51:
		case 0x63:
			mask = 0x3;
		break;

		default:
		mask = ~0;	/* Any row index is ok. */
	}

	return (mask & (1 << ri));
}

static int
iap_event_westmere_ok_on_counter(uint8_t evsel, int ri)
{
	uint32_t mask;

	switch (evsel) {
		/*
		 * Events valid only on counter 0.
		 */
		case 0x60:
		case 0xB3:
		mask = 0x1;
		break;

		/*
		 * Events valid only on counter 0, 1.
		 */
		case 0x4C:
		case 0x4E:
		case 0x51:
		case 0x63:
		mask = 0x3;
		break;

	default:
		mask = ~0;	/* Any row index is ok. */
	}

	return (mask & (1 << ri));
}

static int
iap_event_sb_sbx_ib_ibx_ok_on_counter(uint8_t evsel, int ri)
{
	uint32_t mask;

	switch (evsel) {
		/* Events valid only on counter 0. */
    case 0xB7:
		mask = 0x1;
		break;
		/* Events valid only on counter 1. */
	case 0xC0:
		mask = 0x2;
		break;
		/* Events valid only on counter 2. */
	case 0x48:
	case 0xA2:
	case 0xA3:
		mask = 0x4;
		break;
		/* Events valid only on counter 3. */
	case 0xBB:
	case 0xCD:
		mask = 0x8;
		break;
	default:
		mask = ~0;	/* Any row index is ok. */
	}

	return (mask & (1 << ri));
}

static int
iap_event_ok_on_counter(uint8_t evsel, int ri)
{
	uint32_t mask;

	switch (evsel) {
		/*
		 * Events valid only on counter 0.
		 */
	case 0x10:
	case 0x14:
	case 0x18:
	case 0xB3:
	case 0xC1:
	case 0xCB:
		mask = (1 << 0);
		break;

		/*
		 * Events valid only on counter 1.
		 */
	case 0x11:
	case 0x12:
	case 0x13:
		mask = (1 << 1);
		break;

	default:
		mask = ~0;	/* Any row index is ok. */
	}

	return (mask & (1 << ri));
}

static int
iap_allocate_pmc(int cpu, int ri, struct pmc *pm,
    const struct pmc_op_pmcallocate *a)
{
	enum pmc_event map;
	uint8_t ev;
	uint32_t caps;
	const struct pmc_md_iap_op_pmcallocate *iap;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row-index value %d", __LINE__, ri));

	/* check requested capabilities */
	caps = a->pm_caps;
	if ((IAP_PMC_CAPS & caps) != caps)
		return (EPERM);
	map = 0;	/* XXX: silent GCC warning */
	iap = &a->pm_md.pm_iap;
	ev = IAP_EVSEL_GET(iap->pm_iap_config);

	switch (core_cputype) {
	case PMC_CPU_INTEL_COREI7:
	case PMC_CPU_INTEL_NEHALEM_EX:
		if (iap_event_corei7_ok_on_counter(ev, ri) == 0)
			return (EINVAL);
		break;
	case PMC_CPU_INTEL_SKYLAKE:
	case PMC_CPU_INTEL_SKYLAKE_XEON:
	case PMC_CPU_INTEL_BROADWELL:
	case PMC_CPU_INTEL_BROADWELL_XEON:
	case PMC_CPU_INTEL_SANDYBRIDGE:
	case PMC_CPU_INTEL_SANDYBRIDGE_XEON:
	case PMC_CPU_INTEL_IVYBRIDGE:
	case PMC_CPU_INTEL_IVYBRIDGE_XEON:
	case PMC_CPU_INTEL_HASWELL:
	case PMC_CPU_INTEL_HASWELL_XEON:
		if (iap_event_sb_sbx_ib_ibx_ok_on_counter(ev, ri) == 0)
			return (EINVAL);
		break;
	case PMC_CPU_INTEL_WESTMERE:
	case PMC_CPU_INTEL_WESTMERE_EX:
		if (iap_event_westmere_ok_on_counter(ev, ri) == 0)
			return (EINVAL);
		break;
	default:
		if (iap_event_ok_on_counter(ev, ri) == 0)
			return (EINVAL);
	}

	pm->pm_md.pm_iap.pm_iap_evsel = iap->pm_iap_config;
	return (0);
}

static int
iap_config_pmc(int cpu, int ri, struct pmc *pm)
{
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU %d", __LINE__, cpu));

	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	PMCDBG3(MDP,CFG,1, "iap-config cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(core_pcpu[cpu] != NULL, ("[core,%d] null per-cpu %d", __LINE__,
	    cpu));

	core_pcpu[cpu]->pc_corepmcs[ri].phw_pmc = pm;

	return (0);
}

static int
iap_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc)
{
	int error;
	struct pmc_hw *phw;
	char iap_name[PMC_NAME_MAX];

	phw = &core_pcpu[cpu]->pc_corepmcs[ri];

	(void) snprintf(iap_name, sizeof(iap_name), "IAP-%d", ri);
	if ((error = copystr(iap_name, pi->pm_name, PMC_NAME_MAX,
	    NULL)) != 0)
		return (error);

	pi->pm_class = PMC_CLASS_IAP;

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
iap_get_config(int cpu, int ri, struct pmc **ppm)
{
	*ppm = core_pcpu[cpu]->pc_corepmcs[ri].phw_pmc;

	return (0);
}

static int
iap_get_msr(int ri, uint32_t *msr)
{
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[iap,%d] ri %d out of range", __LINE__, ri));

	*msr = ri;

	return (0);
}

static int
iap_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	pm = core_pcpu[cpu]->pc_corepmcs[ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] cpu %d ri %d pmc not configured", __LINE__, cpu,
		ri));

	tmp = rdpmc(ri);
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = iap_perfctr_value_to_reload_count(tmp);
	else
		*v = tmp & ((1ULL << core_iap_width) - 1);

	PMCDBG4(MDP,REA,1, "iap-read cpu=%d ri=%d msr=0x%x -> v=%jx", cpu, ri,
	    IAP_PMC0 + ri, *v);

	return (0);
}

static int
iap_release_pmc(int cpu, int ri, struct pmc *pm)
{
	(void) pm;

	PMCDBG3(MDP,REL,1, "iap-release cpu=%d ri=%d pm=%p", cpu, ri,
	    pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	KASSERT(core_pcpu[cpu]->pc_corepmcs[ri].phw_pmc
	    == NULL, ("[core,%d] PHW pmc non-NULL", __LINE__));

	return (0);
}

static int
iap_start_pmc(int cpu, int ri)
{
	struct pmc *pm;
	uint32_t evsel;
	struct core_cpu *cc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row-index %d", __LINE__, ri));

	cc = core_pcpu[cpu];
	pm = cc->pc_corepmcs[ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] starting cpu%d,ri%d with no pmc configured",
		__LINE__, cpu, ri));

	PMCDBG2(MDP,STA,1, "iap-start cpu=%d ri=%d", cpu, ri);

	evsel = pm->pm_md.pm_iap.pm_iap_evsel;

	PMCDBG4(MDP,STA,2, "iap-start/2 cpu=%d ri=%d evselmsr=0x%x evsel=0x%x",
	    cpu, ri, IAP_EVSEL0 + ri, evsel);

	/* Event specific configuration. */

	switch (IAP_EVSEL_GET(evsel)) {
	case 0xB7:
		wrmsr(IA_OFFCORE_RSP0, pm->pm_md.pm_iap.pm_iap_rsp);
		break;
	case 0xBB:
		wrmsr(IA_OFFCORE_RSP1, pm->pm_md.pm_iap.pm_iap_rsp);
		break;
	default:
		break;
	}

	wrmsr(IAP_EVSEL0 + ri, evsel | IAP_EN);

	if (core_cputype == PMC_CPU_INTEL_CORE)
		return (0);

	do {
		cc->pc_resync = 0;
		cc->pc_globalctrl |= (1ULL << ri);
		wrmsr(IA_GLOBAL_CTRL, cc->pc_globalctrl);
	} while (cc->pc_resync != 0);

	return (0);
}

static int
iap_stop_pmc(int cpu, int ri)
{
	struct pmc *pm;
	struct core_cpu *cc;
	uint64_t msr;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row index %d", __LINE__, ri));

	cc = core_pcpu[cpu];
	pm = cc->pc_corepmcs[ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] cpu%d ri%d no configured PMC to stop", __LINE__,
		cpu, ri));

	PMCDBG2(MDP,STO,1, "iap-stop cpu=%d ri=%d", cpu, ri);

	msr = rdmsr(IAP_EVSEL0 + ri) & ~IAP_EVSEL_MASK;
	wrmsr(IAP_EVSEL0 + ri, msr);	/* stop hw */

	if (core_cputype == PMC_CPU_INTEL_CORE)
		return (0);

	msr = 0;
	do {
		cc->pc_resync = 0;
		cc->pc_globalctrl &= ~(1ULL << ri);
		msr = rdmsr(IA_GLOBAL_CTRL) & ~IA_GLOBAL_CTRL_MASK;
		wrmsr(IA_GLOBAL_CTRL, cc->pc_globalctrl);
	} while (cc->pc_resync != 0);

	return (0);
}

static int
iap_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;
	struct core_cpu *cc;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[core,%d] illegal cpu value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < core_iap_npmc,
	    ("[core,%d] illegal row index %d", __LINE__, ri));

	cc = core_pcpu[cpu];
	pm = cc->pc_corepmcs[ri].phw_pmc;

	KASSERT(pm,
	    ("[core,%d] cpu%d ri%d no configured PMC to stop", __LINE__,
		cpu, ri));

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = iap_reload_count_to_perfctr_value(v);

	v &= (1ULL << core_iap_width) - 1;

	PMCDBG4(MDP,WRI,1, "iap-write cpu=%d ri=%d msr=0x%x v=%jx", cpu, ri,
	    IAP_PMC0 + ri, v);

	/*
	 * Write the new value to the counter (or it's alias).  The
	 * counter will be in a stopped state when the pcd_write()
	 * entry point is called.
	 */
	wrmsr(core_iap_wroffset + IAP_PMC0 + ri, v);
	return (0);
}


static void
iap_initialize(struct pmc_mdep *md, int maxcpu, int npmc, int pmcwidth,
    int flags)
{
	struct pmc_classdep *pcd;

	KASSERT(md != NULL, ("[iap,%d] md is NULL", __LINE__));

	PMCDBG0(MDP,INI,1, "iap-initialize");

	/* Remember the set of architectural events supported. */
	core_architectural_events = ~flags;

	pcd = &md->pmd_classdep[PMC_MDEP_CLASS_INDEX_IAP];

	pcd->pcd_caps	= IAP_PMC_CAPS;
	pcd->pcd_class	= PMC_CLASS_IAP;
	pcd->pcd_num	= npmc;
	pcd->pcd_ri	= md->pmd_npmc;
	pcd->pcd_width	= pmcwidth;

	pcd->pcd_allocate_pmc	= iap_allocate_pmc;
	pcd->pcd_config_pmc	= iap_config_pmc;
	pcd->pcd_describe	= iap_describe;
	pcd->pcd_get_config	= iap_get_config;
	pcd->pcd_get_msr	= iap_get_msr;
	pcd->pcd_pcpu_fini	= core_pcpu_fini;
	pcd->pcd_pcpu_init	= core_pcpu_init;
	pcd->pcd_read_pmc	= iap_read_pmc;
	pcd->pcd_release_pmc	= iap_release_pmc;
	pcd->pcd_start_pmc	= iap_start_pmc;
	pcd->pcd_stop_pmc	= iap_stop_pmc;
	pcd->pcd_write_pmc	= iap_write_pmc;

	md->pmd_npmc	       += npmc;
}

static int
core_intr(struct trapframe *tf)
{
	pmc_value_t v;
	struct pmc *pm;
	struct core_cpu *cc;
	int error, found_interrupt, ri;
	uint64_t msr;

	PMCDBG3(MDP,INT, 1, "cpu=%d tf=0x%p um=%d", curcpu, (void *) tf,
	    TRAPF_USERMODE(tf));

	found_interrupt = 0;
	cc = core_pcpu[curcpu];

	for (ri = 0; ri < core_iap_npmc; ri++) {

		if ((pm = cc->pc_corepmcs[ri].phw_pmc) == NULL ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			continue;

		if (!iap_pmc_has_overflowed(ri))
			continue;

		found_interrupt = 1;

		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);

		v = pm->pm_sc.pm_reloadcount;
		v = iap_reload_count_to_perfctr_value(v);

		/*
		 * Stop the counter, reload it but only restart it if
		 * the PMC is not stalled.
		 */
		msr = rdmsr(IAP_EVSEL0 + ri) & ~IAP_EVSEL_MASK;
		wrmsr(IAP_EVSEL0 + ri, msr);
		wrmsr(core_iap_wroffset + IAP_PMC0 + ri, v);

		if (error)
			continue;

		wrmsr(IAP_EVSEL0 + ri, msr | (pm->pm_md.pm_iap.pm_iap_evsel |
					      IAP_EN));
	}

	if (found_interrupt)
		lapic_reenable_pmc();

	if (found_interrupt)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	return (found_interrupt);
}

static int
core2_intr(struct trapframe *tf)
{
	int error, found_interrupt, n, cpu;
	uint64_t flag, intrstatus, intrenable, msr;
	struct pmc *pm;
	struct core_cpu *cc;
	pmc_value_t v;

	cpu = curcpu;
	PMCDBG3(MDP,INT, 1, "cpu=%d tf=0x%p um=%d", cpu, (void *) tf,
	    TRAPF_USERMODE(tf));

	/*
	 * The IA_GLOBAL_STATUS (MSR 0x38E) register indicates which
	 * PMCs have a pending PMI interrupt.  We take a 'snapshot' of
	 * the current set of interrupting PMCs and process these
	 * after stopping them.
	 */
	intrstatus = rdmsr(IA_GLOBAL_STATUS);
	intrenable = intrstatus & core_pmcmask;

	PMCDBG2(MDP,INT, 1, "cpu=%d intrstatus=%jx", cpu,
	    (uintmax_t) intrstatus);

	found_interrupt = 0;
	cc = core_pcpu[cpu];

	KASSERT(cc != NULL, ("[core,%d] null pcpu", __LINE__));

	cc->pc_globalctrl &= ~intrenable;
	cc->pc_resync = 1;	/* MSRs now potentially out of sync. */

	/*
	 * Stop PMCs and clear overflow status bits.
	 */
	msr = rdmsr(IA_GLOBAL_CTRL) & ~IA_GLOBAL_CTRL_MASK;
	wrmsr(IA_GLOBAL_CTRL, msr);
	wrmsr(IA_GLOBAL_OVF_CTRL, intrenable |
	    IA_GLOBAL_STATUS_FLAG_OVFBUF |
	    IA_GLOBAL_STATUS_FLAG_CONDCHG);

	/*
	 * Look for interrupts from fixed function PMCs.
	 */
	for (n = 0, flag = (1ULL << IAF_OFFSET); n < core_iaf_npmc;
	     n++, flag <<= 1) {

		if ((intrstatus & flag) == 0)
			continue;

		found_interrupt = 1;

		pm = cc->pc_corepmcs[n + core_iaf_ri].phw_pmc;
		if (pm == NULL || pm->pm_state != PMC_STATE_RUNNING ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);

		if (error)
			intrenable &= ~flag;

		v = iaf_reload_count_to_perfctr_value(pm->pm_sc.pm_reloadcount);

		/* Reload sampling count. */
		wrmsr(IAF_CTR0 + n, v);

		PMCDBG4(MDP,INT, 1, "iaf-intr cpu=%d error=%d v=%jx(%jx)", curcpu,
		    error, (uintmax_t) v, (uintmax_t) rdpmc(IAF_RI_TO_MSR(n)));
	}

	/*
	 * Process interrupts from the programmable counters.
	 */
	for (n = 0, flag = 1; n < core_iap_npmc; n++, flag <<= 1) {
		if ((intrstatus & flag) == 0)
			continue;

		found_interrupt = 1;

		pm = cc->pc_corepmcs[n].phw_pmc;
		if (pm == NULL || pm->pm_state != PMC_STATE_RUNNING ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			continue;

		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error)
			intrenable &= ~flag;

		v = iap_reload_count_to_perfctr_value(pm->pm_sc.pm_reloadcount);

		PMCDBG3(MDP,INT, 1, "iap-intr cpu=%d error=%d v=%jx", cpu, error,
		    (uintmax_t) v);

		/* Reload sampling count. */
		wrmsr(core_iap_wroffset + IAP_PMC0 + n, v);
	}

	/*
	 * Reenable all non-stalled PMCs.
	 */
	PMCDBG2(MDP,INT, 1, "cpu=%d intrenable=%jx", cpu,
	    (uintmax_t) intrenable);

	cc->pc_globalctrl |= intrenable;

	wrmsr(IA_GLOBAL_CTRL, cc->pc_globalctrl & IA_GLOBAL_CTRL_MASK);

	PMCDBG5(MDP,INT, 1, "cpu=%d fixedctrl=%jx globalctrl=%jx status=%jx "
	    "ovf=%jx", cpu, (uintmax_t) rdmsr(IAF_CTRL),
	    (uintmax_t) rdmsr(IA_GLOBAL_CTRL),
	    (uintmax_t) rdmsr(IA_GLOBAL_STATUS),
	    (uintmax_t) rdmsr(IA_GLOBAL_OVF_CTRL));

	if (found_interrupt)
		lapic_reenable_pmc();

	if (found_interrupt)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	return (found_interrupt);
}

int
pmc_core_initialize(struct pmc_mdep *md, int maxcpu, int version_override)
{
	int cpuid[CORE_CPUID_REQUEST_SIZE];
	int ipa_version, flags, nflags;

	do_cpuid(CORE_CPUID_REQUEST, cpuid);

	ipa_version = (version_override > 0) ? version_override :
	    cpuid[CORE_CPUID_EAX] & 0xFF;
	core_cputype = md->pmd_cputype;

	PMCDBG3(MDP,INI,1,"core-init cputype=%d ncpu=%d ipa-version=%d",
	    core_cputype, maxcpu, ipa_version);

	if (ipa_version < 1 || ipa_version > 4 ||
	    (core_cputype != PMC_CPU_INTEL_CORE && ipa_version == 1)) {
		/* Unknown PMC architecture. */
		printf("hwpc_core: unknown PMC architecture: %d\n",
		    ipa_version);
		return (EPROGMISMATCH);
	}

	core_iap_wroffset = 0;
	if (cpu_feature2 & CPUID2_PDCM) {
		if (rdmsr(IA32_PERF_CAPABILITIES) & PERFCAP_FW_WRITE) {
			PMCDBG0(MDP, INI, 1,
			    "core-init full-width write supported");
			core_iap_wroffset = IAP_A_PMC0 - IAP_PMC0;
		} else
			PMCDBG0(MDP, INI, 1,
			    "core-init full-width write NOT supported");
	} else
		PMCDBG0(MDP, INI, 1, "core-init pdcm not supported");

	core_pmcmask = 0;

	/*
	 * Initialize programmable counters.
	 */
	core_iap_npmc = (cpuid[CORE_CPUID_EAX] >> 8) & 0xFF;
	core_iap_width = (cpuid[CORE_CPUID_EAX] >> 16) & 0xFF;

	core_pmcmask |= ((1ULL << core_iap_npmc) - 1);

	nflags = (cpuid[CORE_CPUID_EAX] >> 24) & 0xFF;
	flags = cpuid[CORE_CPUID_EBX] & ((1 << nflags) - 1);

	iap_initialize(md, maxcpu, core_iap_npmc, core_iap_width, flags);

	/*
	 * Initialize fixed function counters, if present.
	 */
	if (core_cputype != PMC_CPU_INTEL_CORE) {
		core_iaf_ri = core_iap_npmc;
		core_iaf_npmc = cpuid[CORE_CPUID_EDX] & 0x1F;
		core_iaf_width = (cpuid[CORE_CPUID_EDX] >> 5) & 0xFF;

		iaf_initialize(md, maxcpu, core_iaf_npmc, core_iaf_width);
		core_pmcmask |= ((1ULL << core_iaf_npmc) - 1) << IAF_OFFSET;
	}

	PMCDBG2(MDP,INI,1,"core-init pmcmask=0x%jx iafri=%d", core_pmcmask,
	    core_iaf_ri);

	core_pcpu = malloc(sizeof(*core_pcpu) * maxcpu, M_PMC,
	    M_ZERO | M_WAITOK);

	/*
	 * Choose the appropriate interrupt handler.
	 */
	if (ipa_version == 1)
		md->pmd_intr = core_intr;
	else
		md->pmd_intr = core2_intr;

	md->pmd_pcpu_fini = NULL;
	md->pmd_pcpu_init = NULL;

	return (0);
}

void
pmc_core_finalize(struct pmc_mdep *md)
{
	PMCDBG0(MDP,INI,1, "core-finalize");

	free(core_pcpu, M_PMC);
	core_pcpu = NULL;
}
