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
 * Common code for handling Intel CPUs.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

static int
intel_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc;

	PMCDBG3(MDP,SWI,1, "pc=%p pp=%p enable-msr=%d", pc, pp,
	    pp->pp_flags & PMC_PP_ENABLE_MSR_ACCESS);

	/* allow the RDPMC instruction if needed */
	if (pp->pp_flags & PMC_PP_ENABLE_MSR_ACCESS)
		load_cr4(rcr4() | CR4_PCE);

	PMCDBG1(MDP,SWI,1, "cr4=0x%jx", (uintmax_t) rcr4());

	return 0;
}

static int
intel_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc;
	(void) pp;		/* can be NULL */

	PMCDBG3(MDP,SWO,1, "pc=%p pp=%p cr4=0x%jx", pc, pp,
	    (uintmax_t) rcr4());

	/* always turn off the RDPMC instruction */
	load_cr4(rcr4() & ~CR4_PCE);

	return 0;
}

struct pmc_mdep *
pmc_intel_initialize(void)
{
	struct pmc_mdep *pmc_mdep;
	enum pmc_cputype cputype;
	int error, model, nclasses, ncpus, stepping, verov;

	KASSERT(cpu_vendor_id == CPU_VENDOR_INTEL,
	    ("[intel,%d] Initializing non-intel processor", __LINE__));

	PMCDBG1(MDP,INI,0, "intel-initialize cpuid=0x%x", cpu_id);

	cputype = -1;
	nclasses = 2;
	error = 0;
	verov = 0;
	model = ((cpu_id & 0xF0000) >> 12) | ((cpu_id & 0xF0) >> 4);
	stepping = cpu_id & 0xF;

	snprintf(pmc_cpuid, sizeof(pmc_cpuid), "GenuineIntel-%d-%02X",
			 (cpu_id & 0xF00) >> 8, model);
	switch (cpu_id & 0xF00) {
	case 0x600:		/* Pentium Pro, Celeron, Pentium II & III */
		switch (model) {
		case 0xE:
			cputype = PMC_CPU_INTEL_CORE;
			break;
		case 0xF:
			/* Per Intel document 315338-020. */
			if (stepping == 0x7) {
				cputype = PMC_CPU_INTEL_CORE;
				verov = 1;
			} else {
				cputype = PMC_CPU_INTEL_CORE2;
				nclasses = 3;
			}
			break;
		case 0x17:
			cputype = PMC_CPU_INTEL_CORE2EXTREME;
			nclasses = 3;
			break;
		case 0x1C:	/* Per Intel document 320047-002. */
			cputype = PMC_CPU_INTEL_ATOM;
			nclasses = 3;
			break;
		case 0x1A:
		case 0x1E:	/*
				 * Per Intel document 253669-032 9/2009,
				 * pages A-2 and A-57
				 */
		case 0x1F:	/*
				 * Per Intel document 253669-032 9/2009,
				 * pages A-2 and A-57
				 */
			cputype = PMC_CPU_INTEL_COREI7;
			nclasses = 5;
			break;
		case 0x2E:
			cputype = PMC_CPU_INTEL_NEHALEM_EX;
			nclasses = 3;
			break;
		case 0x25:	/* Per Intel document 253669-033US 12/2009. */
		case 0x2C:	/* Per Intel document 253669-033US 12/2009. */
			cputype = PMC_CPU_INTEL_WESTMERE;
			nclasses = 5;
			break;
		case 0x2F:	/* Westmere-EX, seen in wild */
			cputype = PMC_CPU_INTEL_WESTMERE_EX;
			nclasses = 3;
			break;
		case 0x2A:	/* Per Intel document 253669-039US 05/2011. */
			cputype = PMC_CPU_INTEL_SANDYBRIDGE;
			nclasses = 5;
			break;
		case 0x2D:	/* Per Intel document 253669-044US 08/2012. */
			cputype = PMC_CPU_INTEL_SANDYBRIDGE_XEON;
			nclasses = 3;
			break;
		case 0x3A:	/* Per Intel document 253669-043US 05/2012. */
			cputype = PMC_CPU_INTEL_IVYBRIDGE;
			nclasses = 3;
			break;
		case 0x3E:	/* Per Intel document 325462-045US 01/2013. */
			cputype = PMC_CPU_INTEL_IVYBRIDGE_XEON;
			nclasses = 3;
			break;
			/* Skylake */
		case 0x4e:
		case 0x5e:
			/* Kabylake */
		case 0x8E:	/* Per Intel document 325462-063US July 2017. */
		case 0x9E:	/* Per Intel document 325462-063US July 2017. */
			cputype = PMC_CPU_INTEL_SKYLAKE;
			nclasses = 3;
			break;
		case 0x55:	/* SDM rev 63 */
			cputype = PMC_CPU_INTEL_SKYLAKE_XEON;
			nclasses = 3;
			break;
		case 0x3D:
		case 0x47:
			cputype = PMC_CPU_INTEL_BROADWELL;
			nclasses = 3;
			break;
		case 0x4f:
		case 0x56:
			cputype = PMC_CPU_INTEL_BROADWELL_XEON;
			nclasses = 3;
			break;
		case 0x3F:	/* Per Intel document 325462-045US 09/2014. */
		case 0x46:	/* Per Intel document 325462-045US 09/2014. */
			        /* Should 46 be XEON. probably its own? */
			cputype = PMC_CPU_INTEL_HASWELL_XEON;
			nclasses = 3;
			break;
		case 0x3C:	/* Per Intel document 325462-045US 01/2013. */
		case 0x45:	/* Per Intel document 325462-045US 09/2014. */
			cputype = PMC_CPU_INTEL_HASWELL;
			nclasses = 5;
			break;
		case 0x4D:      /* Per Intel document 330061-001 01/2014. */
			cputype = PMC_CPU_INTEL_ATOM_SILVERMONT;
			nclasses = 3;
			break;
		}
		break;
	}


	if ((int) cputype == -1) {
		printf("pmc: Unknown Intel CPU.\n");
		return (NULL);
	}

	/* Allocate base class and initialize machine dependent struct */
	pmc_mdep = pmc_mdep_alloc(nclasses);

	pmc_mdep->pmd_cputype	 = cputype;
	pmc_mdep->pmd_switch_in	 = intel_switch_in;
	pmc_mdep->pmd_switch_out = intel_switch_out;

	ncpus = pmc_cpu_max();
	error = pmc_tsc_initialize(pmc_mdep, ncpus);
	if (error)
		goto error;
	switch (cputype) {
		/*
		 * Intel Core, Core 2 and Atom processors.
		 */
	case PMC_CPU_INTEL_ATOM:
	case PMC_CPU_INTEL_ATOM_SILVERMONT:
	case PMC_CPU_INTEL_BROADWELL:
	case PMC_CPU_INTEL_BROADWELL_XEON:
	case PMC_CPU_INTEL_SKYLAKE_XEON:
	case PMC_CPU_INTEL_SKYLAKE:
	case PMC_CPU_INTEL_CORE:
	case PMC_CPU_INTEL_CORE2:
	case PMC_CPU_INTEL_CORE2EXTREME:
	case PMC_CPU_INTEL_COREI7:
	case PMC_CPU_INTEL_NEHALEM_EX:
	case PMC_CPU_INTEL_IVYBRIDGE:
	case PMC_CPU_INTEL_SANDYBRIDGE:
	case PMC_CPU_INTEL_WESTMERE:
	case PMC_CPU_INTEL_WESTMERE_EX:
	case PMC_CPU_INTEL_SANDYBRIDGE_XEON:
	case PMC_CPU_INTEL_IVYBRIDGE_XEON:
	case PMC_CPU_INTEL_HASWELL:
	case PMC_CPU_INTEL_HASWELL_XEON:
		error = pmc_core_initialize(pmc_mdep, ncpus, verov);
		break;

	default:
		KASSERT(0, ("[intel,%d] Unknown CPU type", __LINE__));
	}

	if (error) {
		pmc_tsc_finalize(pmc_mdep);
		goto error;
	}

	/*
	 * Init the uncore class.
	 */
	switch (cputype) {
		/*
		 * Intel Corei7 and Westmere processors.
		 */
	case PMC_CPU_INTEL_COREI7:
	case PMC_CPU_INTEL_HASWELL:
	case PMC_CPU_INTEL_SANDYBRIDGE:
	case PMC_CPU_INTEL_WESTMERE:
	case PMC_CPU_INTEL_BROADWELL:
		error = pmc_uncore_initialize(pmc_mdep, ncpus);
		break;
	default:
		break;
	}
  error:
	if (error) {
		pmc_mdep_free(pmc_mdep);
		pmc_mdep = NULL;
	}

	return (pmc_mdep);
}

void
pmc_intel_finalize(struct pmc_mdep *md)
{
	pmc_tsc_finalize(md);

	switch (md->pmd_cputype) {
	case PMC_CPU_INTEL_ATOM:
	case PMC_CPU_INTEL_ATOM_SILVERMONT:
	case PMC_CPU_INTEL_BROADWELL:
	case PMC_CPU_INTEL_BROADWELL_XEON:
	case PMC_CPU_INTEL_SKYLAKE_XEON:
	case PMC_CPU_INTEL_SKYLAKE:
	case PMC_CPU_INTEL_CORE:
	case PMC_CPU_INTEL_CORE2:
	case PMC_CPU_INTEL_CORE2EXTREME:
	case PMC_CPU_INTEL_COREI7:
	case PMC_CPU_INTEL_NEHALEM_EX:
	case PMC_CPU_INTEL_HASWELL:
	case PMC_CPU_INTEL_HASWELL_XEON:
	case PMC_CPU_INTEL_IVYBRIDGE:
	case PMC_CPU_INTEL_SANDYBRIDGE:
	case PMC_CPU_INTEL_WESTMERE:
	case PMC_CPU_INTEL_WESTMERE_EX:
	case PMC_CPU_INTEL_SANDYBRIDGE_XEON:
	case PMC_CPU_INTEL_IVYBRIDGE_XEON:
		pmc_core_finalize(md);
		break;
	default:
		KASSERT(0, ("[intel,%d] unknown CPU type", __LINE__));
	}

	/*
	 * Uncore.
	 */
	switch (md->pmd_cputype) {
	case PMC_CPU_INTEL_BROADWELL:
	case PMC_CPU_INTEL_COREI7:
	case PMC_CPU_INTEL_HASWELL:
	case PMC_CPU_INTEL_SANDYBRIDGE:
	case PMC_CPU_INTEL_WESTMERE:
		pmc_uncore_finalize(md);
		break;
	default:
		break;
	}
}
