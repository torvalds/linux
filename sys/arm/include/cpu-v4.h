/*-
 * Copyright 2016 Svatopluk Kraus <skra@FreeBSD.org>
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * $FreeBSD$
 */
#ifndef MACHINE_CPU_V4_H
#define MACHINE_CPU_V4_H

/* There are no user serviceable parts here, they may change without notice */
#ifndef _KERNEL
#error Only include this file in the kernel
#endif

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/sysreg.h>

#if __ARM_ARCH >= 6
#error Never include this file for ARMv6
#else

#define CPU_ASID_KERNEL 0

/*
 * Macros to generate CP15 (system control processor) read/write functions.
 */
#define _FX(s...) #s

#define _RF0(fname, aname...)						\
static __inline uint32_t						\
fname(void)								\
{									\
	uint32_t reg;							\
	__asm __volatile("mrc\t" _FX(aname): "=r" (reg));		\
	return(reg);							\
}

#define _R64F0(fname, aname)						\
static __inline uint64_t						\
fname(void)								\
{									\
	uint64_t reg;							\
	__asm __volatile("mrrc\t" _FX(aname): "=r" (reg));		\
	return(reg);							\
}

#define _WF0(fname, aname...)						\
static __inline void							\
fname(void)								\
{									\
	__asm __volatile("mcr\t" _FX(aname));				\
}

#define _WF1(fname, aname...)						\
static __inline void							\
fname(uint32_t reg)							\
{									\
	__asm __volatile("mcr\t" _FX(aname):: "r" (reg));		\
}


/*
 * Publicly accessible functions
 */


/* Various control registers */

_RF0(cp15_cpacr_get, CP15_CPACR(%0))
_WF1(cp15_cpacr_set, CP15_CPACR(%0))
_RF0(cp15_dfsr_get, CP15_DFSR(%0))
_RF0(cp15_ttbr_get, CP15_TTBR0(%0))
_RF0(cp15_dfar_get, CP15_DFAR(%0))
/* XScale */
_RF0(cp15_actlr_get, CP15_ACTLR(%0))
_WF1(cp15_actlr_set, CP15_ACTLR(%0))

/*CPU id registers */
_RF0(cp15_midr_get, CP15_MIDR(%0))
_RF0(cp15_ctr_get, CP15_CTR(%0))
_RF0(cp15_tcmtr_get, CP15_TCMTR(%0))
_RF0(cp15_tlbtr_get, CP15_TLBTR(%0))
_RF0(cp15_sctlr_get, CP15_SCTLR(%0))

#undef	_FX
#undef	_RF0
#undef	_WF0
#undef	_WF1


/*
 * armv4/5 compatibility shims.
 *
 * These functions provide armv4 cache maintenance using the new armv6 names.
 * Included here are just the functions actually used now in common code; it may
 * be necessary to add things here over time.
 *
 * The callers of the dcache functions expect these routines to handle address
 * and size values which are not aligned to cacheline boundaries; the armv4 and
 * armv5 asm code handles that.
 */

static __inline void
tlb_flush_all(void)
{
	cpu_tlb_flushID();
	cpu_cpwait();
}

static __inline void
icache_sync(vm_offset_t va, vm_size_t size)
{
	cpu_icache_sync_range(va, size);
}

static __inline void
dcache_inv_poc(vm_offset_t va, vm_paddr_t pa, vm_size_t size)
{

	cpu_dcache_inv_range(va, size);
#ifdef ARM_L2_PIPT
	cpu_l2cache_inv_range(pa, size);
#else
	cpu_l2cache_inv_range(va, size);
#endif
}

static __inline void
dcache_inv_poc_dma(vm_offset_t va, vm_paddr_t pa, vm_size_t size)
{

	/* See armv6 code, above, for why we do L2 before L1 in this case. */
#ifdef ARM_L2_PIPT
	cpu_l2cache_inv_range(pa, size);
#else
	cpu_l2cache_inv_range(va, size);
#endif
	cpu_dcache_inv_range(va, size);
}

static __inline void
dcache_wb_poc(vm_offset_t va, vm_paddr_t pa, vm_size_t size)
{

	cpu_dcache_wb_range(va, size);
#ifdef ARM_L2_PIPT
	cpu_l2cache_wb_range(pa, size);
#else
	cpu_l2cache_wb_range(va, size);
#endif
}

static __inline void
dcache_wbinv_poc_all(void)
{
	cpu_idcache_wbinv_all();
	cpu_l2cache_wbinv_all();
}

#endif /* _KERNEL */

#endif /* MACHINE_CPU_V4_H */
