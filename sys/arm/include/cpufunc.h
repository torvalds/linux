/*	$NetBSD: cpufunc.h,v 1.29 2003/09/06 09:08:35 rearnsha Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 * cpufunc.h
 *
 * Prototypes for cpu, mmu and tlb related functions.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUFUNC_H_
#define _MACHINE_CPUFUNC_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <machine/armreg.h>

static __inline void
breakpoint(void)
{
	__asm("udf        0xffff");
}

struct cpu_functions {

	/* CPU functions */
#if __ARM_ARCH < 6
	void	(*cf_cpwait)		(void);

	/* MMU functions */

	u_int	(*cf_control)		(u_int bic, u_int eor);
	void	(*cf_setttb)		(u_int ttb);

	/* TLB functions */

	void	(*cf_tlb_flushID)	(void);
	void	(*cf_tlb_flushID_SE)	(u_int va);
	void	(*cf_tlb_flushD)	(void);
	void	(*cf_tlb_flushD_SE)	(u_int va);

	/*
	 * Cache operations:
	 *
	 * We define the following primitives:
	 *
	 *	icache_sync_range	Synchronize I-cache range
	 *
	 *	dcache_wbinv_all	Write-back and Invalidate D-cache
	 *	dcache_wbinv_range	Write-back and Invalidate D-cache range
	 *	dcache_inv_range	Invalidate D-cache range
	 *	dcache_wb_range		Write-back D-cache range
	 *
	 *	idcache_wbinv_all	Write-back and Invalidate D-cache,
	 *				Invalidate I-cache
	 *	idcache_wbinv_range	Write-back and Invalidate D-cache,
	 *				Invalidate I-cache range
	 *
	 * Note that the ARM term for "write-back" is "clean".  We use
	 * the term "write-back" since it's a more common way to describe
	 * the operation.
	 *
	 * There are some rules that must be followed:
	 *
	 *	ID-cache Invalidate All:
	 *		Unlike other functions, this one must never write back.
	 *		It is used to intialize the MMU when it is in an unknown
	 *		state (such as when it may have lines tagged as valid
	 *		that belong to a previous set of mappings).
	 *
	 *	I-cache Sync range:
	 *		The goal is to synchronize the instruction stream,
	 *		so you may beed to write-back dirty D-cache blocks
	 *		first.  If a range is requested, and you can't
	 *		synchronize just a range, you have to hit the whole
	 *		thing.
	 *
	 *	D-cache Write-Back and Invalidate range:
	 *		If you can't WB-Inv a range, you must WB-Inv the
	 *		entire D-cache.
	 *
	 *	D-cache Invalidate:
	 *		If you can't Inv the D-cache, you must Write-Back
	 *		and Invalidate.  Code that uses this operation
	 *		MUST NOT assume that the D-cache will not be written
	 *		back to memory.
	 *
	 *	D-cache Write-Back:
	 *		If you can't Write-back without doing an Inv,
	 *		that's fine.  Then treat this as a WB-Inv.
	 *		Skipping the invalidate is merely an optimization.
	 *
	 *	All operations:
	 *		Valid virtual addresses must be passed to each
	 *		cache operation.
	 */
	void	(*cf_icache_sync_range)	(vm_offset_t, vm_size_t);

	void	(*cf_dcache_wbinv_all)	(void);
	void	(*cf_dcache_wbinv_range) (vm_offset_t, vm_size_t);
	void	(*cf_dcache_inv_range)	(vm_offset_t, vm_size_t);
	void	(*cf_dcache_wb_range)	(vm_offset_t, vm_size_t);

	void	(*cf_idcache_inv_all)	(void);
	void	(*cf_idcache_wbinv_all)	(void);
	void	(*cf_idcache_wbinv_range) (vm_offset_t, vm_size_t);
#endif
	void	(*cf_l2cache_wbinv_all) (void);
	void	(*cf_l2cache_wbinv_range) (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_inv_range)	  (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_wb_range)	  (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_drain_writebuf)	  (void);

	/* Other functions */

#if __ARM_ARCH < 6
	void	(*cf_drain_writebuf)	(void);
#endif

	void	(*cf_sleep)		(int mode);

#if __ARM_ARCH < 6
	/* Soft functions */

	void	(*cf_context_switch)	(void);
#endif

	void	(*cf_setup)		(void);
};

extern struct cpu_functions cpufuncs;
extern u_int cputype;

#if __ARM_ARCH < 6
#define	cpu_cpwait()		cpufuncs.cf_cpwait()

#define cpu_control(c, e)	cpufuncs.cf_control(c, e)
#define cpu_setttb(t)		cpufuncs.cf_setttb(t)

#define	cpu_tlb_flushID()	cpufuncs.cf_tlb_flushID()
#define	cpu_tlb_flushID_SE(e)	cpufuncs.cf_tlb_flushID_SE(e)
#define	cpu_tlb_flushD()	cpufuncs.cf_tlb_flushD()
#define	cpu_tlb_flushD_SE(e)	cpufuncs.cf_tlb_flushD_SE(e)

#define	cpu_icache_sync_range(a, s) cpufuncs.cf_icache_sync_range((a), (s))

#define	cpu_dcache_wbinv_all()	cpufuncs.cf_dcache_wbinv_all()
#define	cpu_dcache_wbinv_range(a, s) cpufuncs.cf_dcache_wbinv_range((a), (s))
#define	cpu_dcache_inv_range(a, s) cpufuncs.cf_dcache_inv_range((a), (s))
#define	cpu_dcache_wb_range(a, s) cpufuncs.cf_dcache_wb_range((a), (s))

#define	cpu_idcache_inv_all()	cpufuncs.cf_idcache_inv_all()
#define	cpu_idcache_wbinv_all()	cpufuncs.cf_idcache_wbinv_all()
#define	cpu_idcache_wbinv_range(a, s) cpufuncs.cf_idcache_wbinv_range((a), (s))
#endif

#define cpu_l2cache_wbinv_all()	cpufuncs.cf_l2cache_wbinv_all()
#define cpu_l2cache_wb_range(a, s) cpufuncs.cf_l2cache_wb_range((a), (s))
#define cpu_l2cache_inv_range(a, s) cpufuncs.cf_l2cache_inv_range((a), (s))
#define cpu_l2cache_wbinv_range(a, s) cpufuncs.cf_l2cache_wbinv_range((a), (s))
#define cpu_l2cache_drain_writebuf() cpufuncs.cf_l2cache_drain_writebuf()

#if __ARM_ARCH < 6
#define	cpu_drain_writebuf()	cpufuncs.cf_drain_writebuf()
#endif
#define cpu_sleep(m)		cpufuncs.cf_sleep(m)

#define cpu_setup()			cpufuncs.cf_setup()

int	set_cpufuncs		(void);
#define ARCHITECTURE_NOT_PRESENT	1	/* known but not configured */
#define ARCHITECTURE_NOT_SUPPORTED	2	/* not known */

void	cpufunc_nullop		(void);
u_int	cpufunc_control		(u_int clear, u_int bic);
void	cpu_domains		(u_int domains);

#if defined(CPU_ARM9E)
void	arm9_tlb_flushID_SE	(u_int va);
void	arm9_context_switch	(void);

u_int	sheeva_control_ext 		(u_int, u_int);
void	sheeva_cpu_sleep		(int);
void	sheeva_setttb			(u_int);
void	sheeva_dcache_wbinv_range	(vm_offset_t, vm_size_t);
void	sheeva_dcache_inv_range		(vm_offset_t, vm_size_t);
void	sheeva_dcache_wb_range		(vm_offset_t, vm_size_t);
void	sheeva_idcache_wbinv_range	(vm_offset_t, vm_size_t);

void	sheeva_l2cache_wbinv_range	(vm_offset_t, vm_size_t);
void	sheeva_l2cache_inv_range	(vm_offset_t, vm_size_t);
void	sheeva_l2cache_wb_range		(vm_offset_t, vm_size_t);
void	sheeva_l2cache_wbinv_all	(void);
#endif

#if defined(CPU_CORTEXA) || defined(CPU_MV_PJ4B) || defined(CPU_KRAIT)
void	armv7_cpu_sleep			(int);
#endif
#if defined(CPU_MV_PJ4B)
void	pj4b_config			(void);
#endif

#if defined(CPU_ARM1176)
void    arm11x6_sleep                   (int);  /* no ref. for errata */
#endif

#if defined(CPU_ARM9E)
void	armv5_ec_setttb(u_int);

void	armv5_ec_icache_sync_range(vm_offset_t, vm_size_t);

void	armv5_ec_dcache_wbinv_all(void);
void	armv5_ec_dcache_wbinv_range(vm_offset_t, vm_size_t);
void	armv5_ec_dcache_inv_range(vm_offset_t, vm_size_t);
void	armv5_ec_dcache_wb_range(vm_offset_t, vm_size_t);

void	armv5_ec_idcache_wbinv_all(void);
void	armv5_ec_idcache_wbinv_range(vm_offset_t, vm_size_t);

void	armv4_tlb_flushID	(void);
void	armv4_tlb_flushD	(void);
void	armv4_tlb_flushD_SE	(u_int va);

void	armv4_drain_writebuf	(void);
void	armv4_idcache_inv_all	(void);
#endif

/*
 * Macros for manipulating CPU interrupts
 */
#if __ARM_ARCH < 6
#define	__ARM_INTR_BITS		(PSR_I | PSR_F)
#else
#define	__ARM_INTR_BITS		(PSR_I | PSR_F | PSR_A)
#endif

static __inline uint32_t
__set_cpsr(uint32_t bic, uint32_t eor)
{
	uint32_t	tmp, ret;

	__asm __volatile(
		"mrs     %0, cpsr\n"		/* Get the CPSR */
		"bic	 %1, %0, %2\n"		/* Clear bits */
		"eor	 %1, %1, %3\n"		/* XOR bits */
		"msr     cpsr_xc, %1\n"		/* Set the CPSR */
	: "=&r" (ret), "=&r" (tmp)
	: "r" (bic), "r" (eor) : "memory");

	return ret;
}

static __inline uint32_t
disable_interrupts(uint32_t mask)
{

	return (__set_cpsr(mask & __ARM_INTR_BITS, mask & __ARM_INTR_BITS));
}

static __inline uint32_t
enable_interrupts(uint32_t mask)
{

	return (__set_cpsr(mask & __ARM_INTR_BITS, 0));
}

static __inline uint32_t
restore_interrupts(uint32_t old_cpsr)
{

	return (__set_cpsr(__ARM_INTR_BITS, old_cpsr & __ARM_INTR_BITS));
}

static __inline register_t
intr_disable(void)
{

	return (disable_interrupts(PSR_I | PSR_F));
}

static __inline void
intr_restore(register_t s)
{

	restore_interrupts(s);
}
#undef __ARM_INTR_BITS

/*
 * Functions to manipulate cpu r13
 * (in arm/arm32/setstack.S)
 */

void set_stackptr	(u_int mode, u_int address);
u_int get_stackptr	(u_int mode);

/*
 * CPU functions from locore.S
 */

void cpu_reset		(void) __attribute__((__noreturn__));

/*
 * Cache info variables.
 */

/* PRIMARY CACHE VARIABLES */
extern int	arm_picache_size;
extern int	arm_picache_line_size;
extern int	arm_picache_ways;

extern int	arm_pdcache_size;	/* and unified */
extern int	arm_pdcache_line_size;
extern int	arm_pdcache_ways;

extern int	arm_pcache_type;
extern int	arm_pcache_unified;

extern int	arm_dcache_align;
extern int	arm_dcache_align_mask;

extern u_int	arm_cache_level;
extern u_int	arm_cache_loc;
extern u_int	arm_cache_type[14];

#else	/* !_KERNEL */

static __inline void
breakpoint(void)
{

	/*
	 * This matches the instruction used by GDB for software
	 * breakpoints.
	 */
	__asm("udf        0xfdee");
}

#endif	/* _KERNEL */
#endif	/* _MACHINE_CPUFUNC_H_ */

/* End of cpufunc.h */
