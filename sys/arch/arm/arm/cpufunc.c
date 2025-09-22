/*	$OpenBSD: cpufunc.c,v 1.58 2025/01/20 20:13:29 kettenis Exp $	*/
/*	$NetBSD: cpufunc.c,v 1.65 2003/11/05 12:53:15 scw Exp $	*/

/*
 * arm7tdmi support code Copyright (c) 2001 John Fremlin
 * arm8 support code Copyright (c) 1997 ARM Limited
 * arm8 support code Copyright (c) 1997 Causality Limited
 * arm9 support code Copyright (C) 2001 ARM Ltd
 * Copyright (c) 1997 Mark Brinicombe.
 * Copyright (c) 1997 Causality Limited
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Causality Limited.
 * 4. The name of Causality Limited may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CAUSALITY LIMITED ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CAUSALITY LIMITED BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpufuncs.c
 *
 * C functions for supporting CPU / MMU / TLB specific operations.
 *
 * Created      : 30/01/97
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/pmap.h>
#include <arm/cpuconf.h>

#if defined(PERFCTRS)
struct arm_pmc_funcs *arm_pmc;
#endif

/* PRIMARY CACHE VARIABLES */
int	arm_picache_size;
int	arm_picache_line_size;
int	arm_picache_ways;

int	arm_pdcache_size;	/* and unified */
int	arm_pdcache_line_size;
int	arm_pdcache_ways;

int	arm_pcache_type;
int	arm_pcache_unified;

int	arm_dcache_align;
int	arm_dcache_align_mask;

/* 1 == use cpu_sleep(), 0 == don't */
int cpu_do_powersave;

struct cpu_functions armv7_cpufuncs = {
	/* CPU functions */

	cpufunc_id,			/* id			*/
	cpufunc_nullop,			/* cpwait		*/

	/* MMU functions */

	cpufunc_control,		/* control		*/
	cpufunc_auxcontrol,		/* aux control		*/
	cpufunc_domains,		/* Domain		*/
	armv7_setttb,			/* Setttb		*/
	cpufunc_dfsr,			/* dfsr			*/
	cpufunc_dfar,			/* dfar			*/
	cpufunc_ifsr,			/* ifsr			*/
	cpufunc_ifar,			/* ifar			*/

	/* TLB functions */

	armv7_tlb_flushID,		/* tlb_flushID		*/
	armv7_tlb_flushID_SE,		/* tlb_flushID_SE	*/
	armv7_tlb_flushID,		/* tlb_flushI		*/
	armv7_tlb_flushID_SE,		/* tlb_flushI_SE	*/
	armv7_tlb_flushD,		/* tlb_flushD		*/
	armv7_tlb_flushD_SE,		/* tlb_flushD_SE	*/

	/* Cache operations */

	armv7_icache_sync_all,		/* icache_sync_all	*/
	armv7_icache_sync_range,	/* icache_sync_range	*/

	armv7_dcache_wbinv_all,		/* dcache_wbinv_all	*/
	armv7_dcache_wbinv_range,	/* dcache_wbinv_range	*/
	armv7_dcache_inv_range,		/* dcache_inv_range	*/
	armv7_dcache_wb_range,		/* dcache_wb_range	*/

	armv7_idcache_wbinv_all,	/* idcache_wbinv_all	*/
	armv7_idcache_wbinv_range,	/* idcache_wbinv_range	*/

	cpufunc_nullop,			/* sdcache_wbinv_all	*/
	(void *)cpufunc_nullop,		/* sdcache_wbinv_range	*/
	(void *)cpufunc_nullop,		/* sdcache_inv_range	*/
	(void *)cpufunc_nullop,		/* sdcache_wb_range	*/
	(void *)cpufunc_nullop,		/* sdcache_drain_writebuf */

	/* Other functions */

	cpufunc_nullop,			/* flush_prefetchbuf	*/
	armv7_drain_writebuf,		/* drain_writebuf	*/

	armv7_cpu_sleep,		/* sleep (wait for interrupt) */

	/* Soft functions */
	armv7_context_switch,		/* context_switch	*/
	armv7_setup			/* cpu setup		*/
};

/*
 * Global constants also used by locore.s
 */

struct cpu_functions cpufuncs;
u_int cputype;

int	arm_icache_min_line_size = 32;
int	arm_dcache_min_line_size = 32;
int	arm_idcache_min_line_size = 32;

void arm_get_cachetype_cp15v7 (void);
int	arm_dcache_l2_nsets;
int	arm_dcache_l2_assoc;
int	arm_dcache_l2_linesize;

/*
 * Base 2 logarithm of an int. returns 0 for 0 (yeye, I know).
 */
static int
log2(unsigned int i)
{
	int ret = 0;

	while (i >>= 1)
		ret++;

	return (ret);
}

void
arm_get_cachetype_cp15v7(void)
{
	uint32_t ctype;
	uint32_t cachereg;
	uint32_t cache_level_id;
	uint32_t sets;
	uint32_t sel, level;

	/* CTR - Cache Type Register */
	__asm volatile("mrc p15, 0, %0, c0, c0, 1"
		: "=r" (ctype));

	arm_dcache_min_line_size = 1 << (CPU_CT_DMINLINE(ctype) + 2);
	arm_icache_min_line_size = 1 << (CPU_CT_IMINLINE(ctype) + 2);
	arm_idcache_min_line_size =
	    min(arm_icache_min_line_size, arm_dcache_min_line_size);

	/* CLIDR - Cache Level ID Register */
	__asm volatile("mrc p15, 1, %0, c0, c0, 1"
		: "=r" (cache_level_id) :);
	cpu_drain_writebuf();

	/* L1 Cache available. */
	level = 0;
	if (cache_level_id & (0x7 << level)) {
		/* Unified cache. */
		if (cache_level_id & (0x4 << level))
			arm_pcache_unified = 1;

		/* Unified or data cache separate. */
		if (cache_level_id & (0x4 << level) ||
		    cache_level_id & (0x2 << level)) {
			sel = level << 1 | 0 << 0; /* L1 | unified/data cache */
			/* CSSELR - Cache Size Selection Register */
			__asm volatile("mcr p15, 2, %0, c0, c0, 0"
				:: "r" (sel));
			cpu_drain_writebuf();
			/* CCSIDR - Cache Size Identification Register */
			__asm volatile("mrc p15, 1, %0, c0, c0, 0"
			: "=r" (cachereg) :);
			cpu_drain_writebuf();
			sets = ((cachereg >> 13) & 0x7fff) + 1;
			arm_pdcache_line_size = 1 << ((cachereg & 0x7) + 4);
			arm_pdcache_ways = ((cachereg >> 3) & 0x3ff) + 1;
			arm_pdcache_size = arm_pdcache_line_size * arm_pdcache_ways * sets;
			switch (cachereg & 0xc0000000) {
			case 0x00000000:
				arm_pcache_type = 0;
				break;
			case 0x40000000:
			case 0xc0000000:
				arm_pcache_type = CPU_CT_CTYPE_WB1;
				break;
			case 0x80000000:
				arm_pcache_type = CPU_CT_CTYPE_WT;
				break;
			}
		}

		/* Instruction cache separate. */
		if (cache_level_id & (0x1 << level)) {
			sel = level << 1 | 1 << 0; /* L1 | instruction cache */
			/* CSSELR - Cache Size Selection Register */
			__asm volatile("mcr p15, 2, %0, c0, c0, 0"
				:: "r" (sel));
			cpu_drain_writebuf();
			/* CCSIDR - Cache Size Identification Register */
			__asm volatile("mrc p15, 1, %0, c0, c0, 0"
			: "=r" (cachereg) :);
			cpu_drain_writebuf();
			sets = ((cachereg >> 13) & 0x7fff) + 1;
			arm_picache_line_size = 1 << ((cachereg & 0x7) + 4);
			arm_picache_ways = ((cachereg >> 3) & 0x3ff) + 1;
			arm_picache_size = arm_picache_line_size * arm_picache_ways * sets;
		}
	}

	arm_dcache_align = arm_pdcache_line_size;
	arm_dcache_align_mask = arm_dcache_align - 1;

	arm_dcache_l2_nsets = arm_pdcache_size/arm_pdcache_ways/arm_pdcache_line_size;
	arm_dcache_l2_assoc = log2(arm_pdcache_ways);
	arm_dcache_l2_linesize = log2(arm_pdcache_line_size);
}

/* 
 */
void
armv7_idcache_wbinv_all(void)
{
	uint32_t arg;
	arg = 0;
	__asm volatile("mcr	p15, 0, r0, c7, c5, 0" :: "r" (arg));
	armv7_dcache_wbinv_all();
}

/* brute force cache flushing */
void
armv7_dcache_wbinv_all(void)
{
	int sets, ways, lvl;
	int nsets, nways;
	uint32_t wayincr, setincr;
	uint32_t wayval, setval;
	uint32_t word;

	nsets = arm_dcache_l2_nsets;
	nways = arm_pdcache_ways;

	setincr = armv7_dcache_sets_inc;
	wayincr = armv7_dcache_index_inc;

#if 0
	printf("l1 nsets %d nways %d wayincr %x setincr %x\n",
	    nsets, nways, wayincr, setincr);
#endif

	lvl = 0; /* L1 */
	setval = 0;
	for (sets = 0; sets < nsets; sets++)  {
		wayval = 0;
		for (ways = 0; ways < nways; ways++) {
			word = wayval | setval | lvl;

			/* Clean D cache SE with Set/Index */
			__asm volatile("mcr	p15, 0, %0, c7, c10, 2"
			    : : "r" (word));
			wayval += wayincr;
		}
		setval += setincr;
	}
	/* drain the write buffer */
	cpu_drain_writebuf();

	/* L2 cache flushing removed. Our current L2 caches are separate. */
}


/*
 * Cannot panic here as we may not have a console yet ...
 */

int
set_cpufuncs(void)
{
	cputype = cpufunc_id();
	cputype &= CPU_ID_CPU_MASK;

	/*
	 * NOTE: cpu_do_powersave defaults to off.  If we encounter a
	 * CPU type where we want to use it by default, then we set it.
	 */

	if ((cputype & CPU_ID_ARCH_MASK) == CPU_ID_ARCH_CPUID) {
		uint32_t mmfr0;

		__asm volatile("mrc p15, 0, %0, c0, c1, 4"
			: "=r" (mmfr0));

		switch (mmfr0 & ID_MMFR0_VMSA_MASK) {
		case VMSA_V7:
		case VMSA_V7_PXN:
		case VMSA_V7_LDT:
			cpufuncs = armv7_cpufuncs;
			/* V4 or higher */
			arm_get_cachetype_cp15v7();
			armv7_dcache_sets_inc = 1U << arm_dcache_l2_linesize;
			armv7_dcache_sets_max = (1U << (arm_dcache_l2_linesize +
			    arm_dcache_l2_nsets)) - armv7_dcache_sets_inc;
			armv7_dcache_index_inc = 1U << (32 -
			    arm_dcache_l2_assoc);
			armv7_dcache_index_max = 0U - armv7_dcache_index_inc;
			pmap_pte_init_armv7();

			/* Use powersave on this CPU. */
			cpu_do_powersave = 1;
			return 0;
		}
	}
	/*
	 * Bzzzz. And the answer was ...
	 */
	panic("No support for this CPU type (%08x) in kernel", cputype);
	return(ARCHITECTURE_NOT_PRESENT);
}

/*
 * CPU Setup code
 */

void
armv7_setup(void)
{
	uint32_t auxctrl, auxctrlmask;
	uint32_t cpuctrl, cpuctrlmask;
	uint32_t id_pfr1;

	auxctrl = auxctrlmask = 0;

	switch (cputype & CPU_ID_CORTEX_MASK) {
	case CPU_ID_CORTEX_A5:
	case CPU_ID_CORTEX_A9:
		/* Cache and TLB maintenance broadcast */
#ifdef notyet
		auxctrlmask |= CORTEXA9_AUXCTL_FW;
		auxctrl |= CORTEXA9_AUXCTL_FW;
#endif
		/* FALLTHROUGH */
	case CPU_ID_CORTEX_A7:
	case CPU_ID_CORTEX_A12:
	case CPU_ID_CORTEX_A15:
	case CPU_ID_CORTEX_A17:
		/* Set SMP to allow LDREX/STREX */
		auxctrlmask |= CORTEXA9_AUXCTL_SMP;
		auxctrl |= CORTEXA9_AUXCTL_SMP;
		break;
	}

	cpuctrlmask = CPU_CONTROL_MMU_ENABLE
	    | CPU_CONTROL_AFLT_ENABLE
	    | CPU_CONTROL_DC_ENABLE
	    | CPU_CONTROL_BPRD_ENABLE
	    | CPU_CONTROL_IC_ENABLE
	    | CPU_CONTROL_VECRELOC
	    | CPU_CONTROL_TRE
	    | CPU_CONTROL_AFE;

	cpuctrl = CPU_CONTROL_MMU_ENABLE
	    | CPU_CONTROL_DC_ENABLE
	    | CPU_CONTROL_BPRD_ENABLE
	    | CPU_CONTROL_IC_ENABLE
	    | CPU_CONTROL_AFE;

	if (vector_page == ARM_VECTORS_HIGH)
		cpuctrl |= CPU_CONTROL_VECRELOC;

	/*
	 * Check for the Virtualization Extensions and enable UWXN of
	 * those are included.
	 */
	__asm volatile("mrc p15, 0, %0, c0, c1, 1" : "=r"(id_pfr1));
	if ((id_pfr1 & 0x0000f000) == 0x00001000) {
		cpuctrlmask |= CPU_CONTROL_UWXN;
		cpuctrl |= CPU_CONTROL_UWXN;
	}

	/* Clear out the cache */
	cpu_idcache_wbinv_all();

	/*
	 * Set the auxiliary control register first, as the SMP bit
	 * needs to be set to 1 before the caches and the MMU are
	 * enabled.
	 */
	cpu_auxcontrol(auxctrlmask, auxctrl);

	/* Set the control register */
	cpu_control(cpuctrlmask, cpuctrl);

	/* And again. */
	cpu_idcache_wbinv_all();
}
