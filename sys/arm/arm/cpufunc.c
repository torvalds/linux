/*	$NetBSD: cpufunc.c,v 1.65 2003/11/05 12:53:15 scw Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/disassem.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/uma.h>

#include <machine/cpufunc.h>

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

u_int	arm_cache_level;
u_int	arm_cache_type[14];
u_int	arm_cache_loc;

#if defined(CPU_ARM9E)
static void arm10_setup(void);
#endif
#ifdef CPU_MV_PJ4B
static void pj4bv7_setup(void);
#endif
#if defined(CPU_ARM1176)
static void arm11x6_setup(void);
#endif
#if defined(CPU_CORTEXA) || defined(CPU_KRAIT)
static void cortexa_setup(void);
#endif

#if defined(CPU_ARM9E)
struct cpu_functions armv5_ec_cpufuncs = {
	/* CPU functions */

	cpufunc_nullop,			/* cpwait		*/

	/* MMU functions */

	cpufunc_control,		/* control		*/
	armv5_ec_setttb,		/* Setttb		*/

	/* TLB functions */

	armv4_tlb_flushID,		/* tlb_flushID		*/
	arm9_tlb_flushID_SE,		/* tlb_flushID_SE	*/
	armv4_tlb_flushD,		/* tlb_flushD		*/
	armv4_tlb_flushD_SE,		/* tlb_flushD_SE	*/

	/* Cache operations */

	armv5_ec_icache_sync_range,	/* icache_sync_range	*/

	armv5_ec_dcache_wbinv_all,	/* dcache_wbinv_all	*/
	armv5_ec_dcache_wbinv_range,	/* dcache_wbinv_range	*/
	armv5_ec_dcache_inv_range,	/* dcache_inv_range	*/
	armv5_ec_dcache_wb_range,	/* dcache_wb_range	*/

	armv4_idcache_inv_all,		/* idcache_inv_all	*/
	armv5_ec_idcache_wbinv_all,	/* idcache_wbinv_all	*/
	armv5_ec_idcache_wbinv_range,	/* idcache_wbinv_range	*/

	cpufunc_nullop,                 /* l2cache_wbinv_all    */
	(void *)cpufunc_nullop,         /* l2cache_wbinv_range  */
      	(void *)cpufunc_nullop,         /* l2cache_inv_range    */
	(void *)cpufunc_nullop,         /* l2cache_wb_range     */
	(void *)cpufunc_nullop,         /* l2cache_drain_writebuf */

	/* Other functions */

	armv4_drain_writebuf,		/* drain_writebuf	*/

	(void *)cpufunc_nullop,		/* sleep		*/

	/* Soft functions */

	arm9_context_switch,		/* context_switch	*/

	arm10_setup			/* cpu setup		*/

};

struct cpu_functions sheeva_cpufuncs = {
	/* CPU functions */

	cpufunc_nullop,			/* cpwait		*/

	/* MMU functions */

	cpufunc_control,		/* control		*/
	sheeva_setttb,			/* Setttb		*/

	/* TLB functions */

	armv4_tlb_flushID,		/* tlb_flushID		*/
	arm9_tlb_flushID_SE,		/* tlb_flushID_SE	*/
	armv4_tlb_flushD,		/* tlb_flushD		*/
	armv4_tlb_flushD_SE,		/* tlb_flushD_SE	*/

	/* Cache operations */

	armv5_ec_icache_sync_range,	/* icache_sync_range	*/

	armv5_ec_dcache_wbinv_all,	/* dcache_wbinv_all	*/
	sheeva_dcache_wbinv_range,	/* dcache_wbinv_range	*/
	sheeva_dcache_inv_range,	/* dcache_inv_range	*/
	sheeva_dcache_wb_range,		/* dcache_wb_range	*/

	armv4_idcache_inv_all,		/* idcache_inv_all	*/
	armv5_ec_idcache_wbinv_all,	/* idcache_wbinv_all	*/
	sheeva_idcache_wbinv_range,	/* idcache_wbinv_all	*/

	sheeva_l2cache_wbinv_all,	/* l2cache_wbinv_all    */
	sheeva_l2cache_wbinv_range,	/* l2cache_wbinv_range  */
	sheeva_l2cache_inv_range,	/* l2cache_inv_range    */
	sheeva_l2cache_wb_range,	/* l2cache_wb_range     */
	(void *)cpufunc_nullop,         /* l2cache_drain_writebuf */

	/* Other functions */

	armv4_drain_writebuf,		/* drain_writebuf	*/

	sheeva_cpu_sleep,		/* sleep		*/

	/* Soft functions */

	arm9_context_switch,		/* context_switch	*/

	arm10_setup			/* cpu setup		*/
};
#endif /* CPU_ARM9E */

#ifdef CPU_MV_PJ4B
struct cpu_functions pj4bv7_cpufuncs = {

	/* Cache operations */
	.cf_l2cache_wbinv_all = (void *)cpufunc_nullop,
	.cf_l2cache_wbinv_range = (void *)cpufunc_nullop,
	.cf_l2cache_inv_range = (void *)cpufunc_nullop,
	.cf_l2cache_wb_range = (void *)cpufunc_nullop,
	.cf_l2cache_drain_writebuf = (void *)cpufunc_nullop,

	/* Other functions */
	.cf_sleep = (void *)cpufunc_nullop,

	/* Soft functions */
	.cf_setup = pj4bv7_setup
};
#endif /* CPU_MV_PJ4B */

#if defined(CPU_ARM1176)
struct cpu_functions arm1176_cpufuncs = {

	/* Cache operations */
	.cf_l2cache_wbinv_all = (void *)cpufunc_nullop,
	.cf_l2cache_wbinv_range = (void *)cpufunc_nullop,
	.cf_l2cache_inv_range = (void *)cpufunc_nullop,
	.cf_l2cache_wb_range = (void *)cpufunc_nullop,
	.cf_l2cache_drain_writebuf = (void *)cpufunc_nullop,

	/* Other functions */
	.cf_sleep = arm11x6_sleep, 

	/* Soft functions */
	.cf_setup = arm11x6_setup
};
#endif /*CPU_ARM1176 */

#if defined(CPU_CORTEXA) || defined(CPU_KRAIT)
struct cpu_functions cortexa_cpufuncs = {

	/* Cache operations */

	/*
	 * Note: For CPUs using the PL310 the L2 ops are filled in when the
	 * L2 cache controller is actually enabled.
	 */
	.cf_l2cache_wbinv_all = cpufunc_nullop,
	.cf_l2cache_wbinv_range = (void *)cpufunc_nullop,
	.cf_l2cache_inv_range = (void *)cpufunc_nullop,
	.cf_l2cache_wb_range = (void *)cpufunc_nullop,
	.cf_l2cache_drain_writebuf = (void *)cpufunc_nullop,

	/* Other functions */
	.cf_sleep = armv7_cpu_sleep,

	/* Soft functions */
	.cf_setup = cortexa_setup
};
#endif /* CPU_CORTEXA || CPU_KRAIT */

/*
 * Global constants also used by locore.s
 */

struct cpu_functions cpufuncs;
u_int cputype;
#if __ARM_ARCH <= 5
u_int cpu_reset_needs_v4_MMU_disable;	/* flag used in locore-v4.s */
#endif

#if defined (CPU_ARM9E) ||	\
  defined(CPU_ARM1176) ||	\
  defined(CPU_MV_PJ4B) ||			\
  defined(CPU_CORTEXA) || defined(CPU_KRAIT)

static void get_cachetype_cp15(void);

/* Additional cache information local to this file.  Log2 of some of the
   above numbers.  */
static int	arm_dcache_l2_nsets;
static int	arm_dcache_l2_assoc;
static int	arm_dcache_l2_linesize;

static void
get_cachetype_cp15(void)
{
	u_int ctype, isize, dsize, cpuid;
	u_int clevel, csize, i, sel;
	u_int multiplier;
	u_char type;

	ctype = cp15_ctr_get();
	cpuid = cp15_midr_get();
	/*
	 * ...and thus spake the ARM ARM:
	 *
	 * If an <opcode2> value corresponding to an unimplemented or
	 * reserved ID register is encountered, the System Control
	 * processor returns the value of the main ID register.
	 */
	if (ctype == cpuid)
		goto out;

	if (CPU_CT_FORMAT(ctype) == CPU_CT_ARMV7) {
		__asm __volatile("mrc p15, 1, %0, c0, c0, 1"
		    : "=r" (clevel));
		arm_cache_level = clevel;
		arm_cache_loc = CPU_CLIDR_LOC(arm_cache_level);
		i = 0;
		while ((type = (clevel & 0x7)) && i < 7) {
			if (type == CACHE_DCACHE || type == CACHE_UNI_CACHE ||
			    type == CACHE_SEP_CACHE) {
				sel = i << 1;
				__asm __volatile("mcr p15, 2, %0, c0, c0, 0"
				    : : "r" (sel));
				__asm __volatile("mrc p15, 1, %0, c0, c0, 0"
				    : "=r" (csize));
				arm_cache_type[sel] = csize;
				arm_dcache_align = 1 <<
				    (CPUV7_CT_xSIZE_LEN(csize) + 4);
				arm_dcache_align_mask = arm_dcache_align - 1;
			}
			if (type == CACHE_ICACHE || type == CACHE_SEP_CACHE) {
				sel = (i << 1) | 1;
				__asm __volatile("mcr p15, 2, %0, c0, c0, 0"
				    : : "r" (sel));
				__asm __volatile("mrc p15, 1, %0, c0, c0, 0"
				    : "=r" (csize));
				arm_cache_type[sel] = csize;
			}
			i++;
			clevel >>= 3;
		}
	} else {
		if ((ctype & CPU_CT_S) == 0)
			arm_pcache_unified = 1;

		/*
		 * If you want to know how this code works, go read the ARM ARM.
		 */

		arm_pcache_type = CPU_CT_CTYPE(ctype);

		if (arm_pcache_unified == 0) {
			isize = CPU_CT_ISIZE(ctype);
			multiplier = (isize & CPU_CT_xSIZE_M) ? 3 : 2;
			arm_picache_line_size = 1U << (CPU_CT_xSIZE_LEN(isize) + 3);
			if (CPU_CT_xSIZE_ASSOC(isize) == 0) {
				if (isize & CPU_CT_xSIZE_M)
					arm_picache_line_size = 0; /* not present */
				else
					arm_picache_ways = 1;
			} else {
				arm_picache_ways = multiplier <<
				    (CPU_CT_xSIZE_ASSOC(isize) - 1);
			}
			arm_picache_size = multiplier << (CPU_CT_xSIZE_SIZE(isize) + 8);
		}

		dsize = CPU_CT_DSIZE(ctype);
		multiplier = (dsize & CPU_CT_xSIZE_M) ? 3 : 2;
		arm_pdcache_line_size = 1U << (CPU_CT_xSIZE_LEN(dsize) + 3);
		if (CPU_CT_xSIZE_ASSOC(dsize) == 0) {
			if (dsize & CPU_CT_xSIZE_M)
				arm_pdcache_line_size = 0; /* not present */
			else
				arm_pdcache_ways = 1;
		} else {
			arm_pdcache_ways = multiplier <<
			    (CPU_CT_xSIZE_ASSOC(dsize) - 1);
		}
		arm_pdcache_size = multiplier << (CPU_CT_xSIZE_SIZE(dsize) + 8);

		arm_dcache_align = arm_pdcache_line_size;

		arm_dcache_l2_assoc = CPU_CT_xSIZE_ASSOC(dsize) + multiplier - 2;
		arm_dcache_l2_linesize = CPU_CT_xSIZE_LEN(dsize) + 3;
		arm_dcache_l2_nsets = 6 + CPU_CT_xSIZE_SIZE(dsize) -
		    CPU_CT_xSIZE_ASSOC(dsize) - CPU_CT_xSIZE_LEN(dsize);

	out:
		arm_dcache_align_mask = arm_dcache_align - 1;
	}
}
#endif /* ARM9 || XSCALE */

/*
 * Cannot panic here as we may not have a console yet ...
 */

int
set_cpufuncs(void)
{
	cputype = cp15_midr_get();
	cputype &= CPU_ID_CPU_MASK;

#if defined(CPU_ARM9E)
	if (cputype == CPU_ID_MV88FR131 || cputype == CPU_ID_MV88FR571_VD ||
	    cputype == CPU_ID_MV88FR571_41) {
		uint32_t sheeva_ctrl;

		sheeva_ctrl = (MV_DC_STREAM_ENABLE | MV_BTB_DISABLE |
		    MV_L2_ENABLE);
		/*
		 * Workaround for Marvell MV78100 CPU: Cache prefetch
		 * mechanism may affect the cache coherency validity,
		 * so it needs to be disabled.
		 *
		 * Refer to errata document MV-S501058-00C.pdf (p. 3.1
		 * L2 Prefetching Mechanism) for details.
		 */
		if (cputype == CPU_ID_MV88FR571_VD ||
		    cputype == CPU_ID_MV88FR571_41)
			sheeva_ctrl |= MV_L2_PREFETCH_DISABLE;

		sheeva_control_ext(0xffffffff & ~MV_WA_ENABLE, sheeva_ctrl);

		cpufuncs = sheeva_cpufuncs;
		get_cachetype_cp15();
		pmap_pte_init_generic();
		goto out;
	} else if (cputype == CPU_ID_ARM926EJS) {
		cpufuncs = armv5_ec_cpufuncs;
		get_cachetype_cp15();
		pmap_pte_init_generic();
		goto out;
	}
#endif /* CPU_ARM9E */
#if defined(CPU_ARM1176)
	if (cputype == CPU_ID_ARM1176JZS) {
		cpufuncs = arm1176_cpufuncs;
		get_cachetype_cp15();
		goto out;
	}
#endif /* CPU_ARM1176 */
#if defined(CPU_CORTEXA) || defined(CPU_KRAIT)
	switch(cputype & CPU_ID_SCHEME_MASK) {
	case CPU_ID_CORTEXA5:
	case CPU_ID_CORTEXA7:
	case CPU_ID_CORTEXA8:
	case CPU_ID_CORTEXA9:
	case CPU_ID_CORTEXA12:
	case CPU_ID_CORTEXA15:
	case CPU_ID_CORTEXA53:
	case CPU_ID_CORTEXA57:
	case CPU_ID_CORTEXA72:
	case CPU_ID_KRAIT300:
		cpufuncs = cortexa_cpufuncs;
		get_cachetype_cp15();
		goto out;
	default:
		break;
	}
#endif /* CPU_CORTEXA || CPU_KRAIT */

#if defined(CPU_MV_PJ4B)
	if (cputype == CPU_ID_MV88SV581X_V7 ||
	    cputype == CPU_ID_MV88SV584X_V7 ||
	    cputype == CPU_ID_ARM_88SV581X_V7) {
		cpufuncs = pj4bv7_cpufuncs;
		get_cachetype_cp15();
		goto out;
	}
#endif /* CPU_MV_PJ4B */

	/*
	 * Bzzzz. And the answer was ...
	 */
	panic("No support for this CPU type (%08x) in kernel", cputype);
	return(ARCHITECTURE_NOT_PRESENT);
out:
	uma_set_align(arm_dcache_align_mask);
	return (0);
}

/*
 * CPU Setup code
 */

#if defined(CPU_ARM9E)
static void
arm10_setup(void)
{
	int cpuctrl, cpuctrlmask;

	cpuctrl = CPU_CONTROL_MMU_ENABLE | CPU_CONTROL_SYST_ENABLE
	    | CPU_CONTROL_IC_ENABLE | CPU_CONTROL_DC_ENABLE
	    | CPU_CONTROL_WBUF_ENABLE | CPU_CONTROL_BPRD_ENABLE;
	cpuctrlmask = CPU_CONTROL_MMU_ENABLE | CPU_CONTROL_SYST_ENABLE
	    | CPU_CONTROL_IC_ENABLE | CPU_CONTROL_DC_ENABLE
	    | CPU_CONTROL_WBUF_ENABLE | CPU_CONTROL_ROM_ENABLE
	    | CPU_CONTROL_BEND_ENABLE | CPU_CONTROL_AFLT_ENABLE
	    | CPU_CONTROL_BPRD_ENABLE
	    | CPU_CONTROL_ROUNDROBIN | CPU_CONTROL_CPCLK;

#ifndef ARM32_DISABLE_ALIGNMENT_FAULTS
	cpuctrl |= CPU_CONTROL_AFLT_ENABLE;
#endif

#ifdef __ARMEB__
	cpuctrl |= CPU_CONTROL_BEND_ENABLE;
#endif

	/* Clear out the cache */
	cpu_idcache_wbinv_all();

	/* Now really make sure they are clean.  */
	__asm __volatile ("mcr\tp15, 0, r0, c7, c7, 0" : : );

	if (vector_page == ARM_VECTORS_HIGH)
		cpuctrl |= CPU_CONTROL_VECRELOC;

	/* Set the control register */
	cpu_control(0xffffffff, cpuctrl);

	/* And again. */
	cpu_idcache_wbinv_all();
}
#endif	/* CPU_ARM9E || CPU_ARM10 */

#if defined(CPU_ARM1176) \
 || defined(CPU_MV_PJ4B) \
 || defined(CPU_CORTEXA) || defined(CPU_KRAIT)
static __inline void
cpu_scc_setup_ccnt(void)
{
/* This is how you give userland access to the CCNT and PMCn
 * registers.
 * BEWARE! This gives write access also, which may not be what
 * you want!
 */
#ifdef _PMC_USER_READ_WRITE_
	/* Set PMUSERENR[0] to allow userland access */
	cp15_pmuserenr_set(1);
#endif
#if defined(CPU_ARM1176)
	/* Set PMCR[2,0] to enable counters and reset CCNT */
	cp15_pmcr_set(5);
#else
	/* Set up the PMCCNTR register as a cyclecounter:
	 * Set PMINTENCLR to 0xFFFFFFFF to block interrupts
	 * Set PMCR[2,0] to enable counters and reset CCNT
	 * Set PMCNTENSET to 0x80000000 to enable CCNT */
	cp15_pminten_clr(0xFFFFFFFF);
	cp15_pmcr_set(5);
	cp15_pmcnten_set(0x80000000);
#endif
}
#endif

#if defined(CPU_ARM1176)
static void
arm11x6_setup(void)
{
	uint32_t auxctrl, auxctrl_wax;
	uint32_t tmp, tmp2;
	uint32_t cpuid;

	cpuid = cp15_midr_get();

	auxctrl = 0;
	auxctrl_wax = ~0;

	/*
	 * Enable an errata workaround
	 */
	if ((cpuid & CPU_ID_CPU_MASK) == CPU_ID_ARM1176JZS) { /* ARM1176JZSr0 */
		auxctrl = ARM1176_AUXCTL_PHD;
		auxctrl_wax = ~ARM1176_AUXCTL_PHD;
	}

	tmp = cp15_actlr_get();
	tmp2 = tmp;
	tmp &= auxctrl_wax;
	tmp |= auxctrl;
	if (tmp != tmp2)
		cp15_actlr_set(tmp);

	cpu_scc_setup_ccnt();
}
#endif  /* CPU_ARM1176 */

#ifdef CPU_MV_PJ4B
static void
pj4bv7_setup(void)
{

	pj4b_config();
	cpu_scc_setup_ccnt();
}
#endif /* CPU_MV_PJ4B */

#if defined(CPU_CORTEXA) || defined(CPU_KRAIT)
static void
cortexa_setup(void)
{

	cpu_scc_setup_ccnt();
}
#endif  /* CPU_CORTEXA || CPU_KRAIT */
