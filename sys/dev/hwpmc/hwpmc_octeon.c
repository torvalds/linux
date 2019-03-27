/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/systm.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/pmc_mdep.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-core.h>

#define	OCTEON_PMC_CAPS	(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

const struct mips_event_code_map mips_event_codes[] =
{
    { PMC_EV_OCTEON_CLK, MIPS_CTR_ALL, CVMX_CORE_PERF_CLK },
    { PMC_EV_OCTEON_ISSUE, MIPS_CTR_ALL, CVMX_CORE_PERF_ISSUE },
    { PMC_EV_OCTEON_RET, MIPS_CTR_ALL, CVMX_CORE_PERF_RET },
    { PMC_EV_OCTEON_NISSUE, MIPS_CTR_ALL, CVMX_CORE_PERF_NISSUE },
    { PMC_EV_OCTEON_SISSUE, MIPS_CTR_ALL, CVMX_CORE_PERF_SISSUE },
    { PMC_EV_OCTEON_DISSUE, MIPS_CTR_ALL, CVMX_CORE_PERF_DISSUE },
    { PMC_EV_OCTEON_IFI, MIPS_CTR_ALL, CVMX_CORE_PERF_IFI },
    { PMC_EV_OCTEON_BR, MIPS_CTR_ALL, CVMX_CORE_PERF_BR },
    { PMC_EV_OCTEON_BRMIS, MIPS_CTR_ALL, CVMX_CORE_PERF_BRMIS },
    { PMC_EV_OCTEON_J, MIPS_CTR_ALL, CVMX_CORE_PERF_J },
    { PMC_EV_OCTEON_JMIS, MIPS_CTR_ALL, CVMX_CORE_PERF_JMIS },
    { PMC_EV_OCTEON_REPLAY, MIPS_CTR_ALL, CVMX_CORE_PERF_REPLAY },
    { PMC_EV_OCTEON_IUNA, MIPS_CTR_ALL, CVMX_CORE_PERF_IUNA },
    { PMC_EV_OCTEON_TRAP, MIPS_CTR_ALL, CVMX_CORE_PERF_TRAP },
    { PMC_EV_OCTEON_UULOAD, MIPS_CTR_ALL, CVMX_CORE_PERF_UULOAD },
    { PMC_EV_OCTEON_UUSTORE, MIPS_CTR_ALL, CVMX_CORE_PERF_UUSTORE },
    { PMC_EV_OCTEON_ULOAD, MIPS_CTR_ALL, CVMX_CORE_PERF_ULOAD },
    { PMC_EV_OCTEON_USTORE, MIPS_CTR_ALL, CVMX_CORE_PERF_USTORE },
    { PMC_EV_OCTEON_EC, MIPS_CTR_ALL, CVMX_CORE_PERF_EC },
    { PMC_EV_OCTEON_MC, MIPS_CTR_ALL, CVMX_CORE_PERF_MC },
    { PMC_EV_OCTEON_CC, MIPS_CTR_ALL, CVMX_CORE_PERF_CC },
    { PMC_EV_OCTEON_CSRC, MIPS_CTR_ALL, CVMX_CORE_PERF_CSRC },
    { PMC_EV_OCTEON_CFETCH, MIPS_CTR_ALL, CVMX_CORE_PERF_CFETCH },
    { PMC_EV_OCTEON_CPREF, MIPS_CTR_ALL, CVMX_CORE_PERF_CPREF },
    { PMC_EV_OCTEON_ICA, MIPS_CTR_ALL, CVMX_CORE_PERF_ICA },
    { PMC_EV_OCTEON_II, MIPS_CTR_ALL, CVMX_CORE_PERF_II },
    { PMC_EV_OCTEON_IP, MIPS_CTR_ALL, CVMX_CORE_PERF_IP },
    { PMC_EV_OCTEON_CIMISS, MIPS_CTR_ALL, CVMX_CORE_PERF_CIMISS },
    { PMC_EV_OCTEON_WBUF, MIPS_CTR_ALL, CVMX_CORE_PERF_WBUF },
    { PMC_EV_OCTEON_WDAT, MIPS_CTR_ALL, CVMX_CORE_PERF_WDAT },
    { PMC_EV_OCTEON_WBUFLD, MIPS_CTR_ALL, CVMX_CORE_PERF_WBUFLD },
    { PMC_EV_OCTEON_WBUFFL, MIPS_CTR_ALL, CVMX_CORE_PERF_WBUFFL },
    { PMC_EV_OCTEON_WBUFTR, MIPS_CTR_ALL, CVMX_CORE_PERF_WBUFTR },
    { PMC_EV_OCTEON_BADD, MIPS_CTR_ALL, CVMX_CORE_PERF_BADD },
    { PMC_EV_OCTEON_BADDL2, MIPS_CTR_ALL, CVMX_CORE_PERF_BADDL2 },
    { PMC_EV_OCTEON_BFILL, MIPS_CTR_ALL, CVMX_CORE_PERF_BFILL },
    { PMC_EV_OCTEON_DDIDS, MIPS_CTR_ALL, CVMX_CORE_PERF_DDIDS },
    { PMC_EV_OCTEON_IDIDS, MIPS_CTR_ALL, CVMX_CORE_PERF_IDIDS },
    { PMC_EV_OCTEON_DIDNA, MIPS_CTR_ALL, CVMX_CORE_PERF_DIDNA },
    { PMC_EV_OCTEON_LDS, MIPS_CTR_ALL, CVMX_CORE_PERF_LDS },
    { PMC_EV_OCTEON_LMLDS, MIPS_CTR_ALL, CVMX_CORE_PERF_LMLDS },
    { PMC_EV_OCTEON_IOLDS, MIPS_CTR_ALL, CVMX_CORE_PERF_IOLDS },
    { PMC_EV_OCTEON_DMLDS, MIPS_CTR_ALL, CVMX_CORE_PERF_DMLDS },
    { PMC_EV_OCTEON_STS, MIPS_CTR_ALL, CVMX_CORE_PERF_STS },
    { PMC_EV_OCTEON_LMSTS, MIPS_CTR_ALL, CVMX_CORE_PERF_LMSTS },
    { PMC_EV_OCTEON_IOSTS, MIPS_CTR_ALL, CVMX_CORE_PERF_IOSTS },
    { PMC_EV_OCTEON_IOBDMA, MIPS_CTR_ALL, CVMX_CORE_PERF_IOBDMA },
    { PMC_EV_OCTEON_DTLB, MIPS_CTR_ALL, CVMX_CORE_PERF_DTLB },
    { PMC_EV_OCTEON_DTLBAD, MIPS_CTR_ALL, CVMX_CORE_PERF_DTLBAD },
    { PMC_EV_OCTEON_ITLB, MIPS_CTR_ALL, CVMX_CORE_PERF_ITLB },
    { PMC_EV_OCTEON_SYNC, MIPS_CTR_ALL, CVMX_CORE_PERF_SYNC },
    { PMC_EV_OCTEON_SYNCIOB, MIPS_CTR_ALL, CVMX_CORE_PERF_SYNCIOB },
    { PMC_EV_OCTEON_SYNCW, MIPS_CTR_ALL, CVMX_CORE_PERF_SYNCW },
};

const int mips_event_codes_size = nitems(mips_event_codes);

struct mips_pmc_spec mips_pmc_spec = {
	.ps_cpuclass = PMC_CLASS_OCTEON,
	.ps_cputype = PMC_CPU_MIPS_OCTEON,
	.ps_capabilities = OCTEON_PMC_CAPS,
	.ps_counter_width = 64
};

/*
 * Performance Count Register N
 */
uint64_t
mips_pmcn_read(unsigned int pmc)
{
	uint64_t reg = 0;

	KASSERT(pmc < mips_npmcs, ("[mips,%d] illegal PMC number %d",
				   __LINE__, pmc));

	/* The counter value is the next value after the control register. */
	switch (pmc) {
	case 0:
		CVMX_MF_COP0(reg, COP0_PERFVALUE0);
		break;
	case 1:
		CVMX_MF_COP0(reg, COP0_PERFVALUE1);
		break;
	default:
		return 0;
	}
	return (reg);
}

uint64_t
mips_pmcn_write(unsigned int pmc, uint64_t reg)
{

	KASSERT(pmc < mips_npmcs, ("[mips,%d] illegal PMC number %d",
				   __LINE__, pmc));

	switch (pmc) {
	case 0:
		CVMX_MT_COP0(reg, COP0_PERFVALUE0);
		break;
	case 1:
		CVMX_MT_COP0(reg, COP0_PERFVALUE1);
		break;
	default:
		return 0;
	}
	return (reg);
}

uint32_t
mips_get_perfctl(int cpu, int ri, uint32_t event, uint32_t caps)
{
	cvmx_core_perf_control_t control;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[mips,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < mips_npmcs,
	    ("[mips,%d] illegal row index %d", __LINE__, ri));

	control.s.event = event;

	if (caps & PMC_CAP_SYSTEM) {
		control.s.k = 1;
		control.s.s = 1;
		control.s.ex = 1;
	}

	if (caps & PMC_CAP_USER)
		control.s.u = 1;

	if ((caps & (PMC_CAP_USER | PMC_CAP_SYSTEM)) == 0) {
		control.s.k = 1;
		control.s.s = 1;
		control.s.u = 1;
		control.s.ex = 1;
	}

	if (caps & PMC_CAP_INTERRUPT)
		control.s.ie = 1;

	PMCDBG2(MDP,ALL,2,"mips-allocate ri=%d -> config=0x%x", ri,
	    control.u32);

	return (control.u32);
}
