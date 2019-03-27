/*-
 * Copyright (c) 2014 Andrew Turner
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

#ifndef _MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

static __inline void
breakpoint(void)
{

	__asm("brk #0");
}

#ifdef _KERNEL

#include <machine/armreg.h>

void pan_enable(void);

static __inline register_t
dbg_disable(void)
{
	uint32_t ret;

	__asm __volatile(
	    "mrs %x0, daif   \n"
	    "msr daifset, #8 \n"
	    : "=&r" (ret));

	return (ret);
}

static __inline void
dbg_enable(void)
{

	__asm __volatile("msr daifclr, #8");
}

static __inline register_t
intr_disable(void)
{
	/* DAIF is a 32-bit register */
	uint32_t ret;

	__asm __volatile(
	    "mrs %x0, daif   \n"
	    "msr daifset, #2 \n"
	    : "=&r" (ret));

	return (ret);
}

static __inline void
intr_restore(register_t s)
{

	WRITE_SPECIALREG(daif, s);
}

static __inline void
intr_enable(void)
{

	__asm __volatile("msr daifclr, #2");
}

static __inline register_t
get_midr(void)
{
	uint64_t midr;

	midr = READ_SPECIALREG(midr_el1);

	return (midr);
}

static __inline register_t
get_mpidr(void)
{
	uint64_t mpidr;

	mpidr = READ_SPECIALREG(mpidr_el1);

	return (mpidr);
}

static __inline void
clrex(void)
{

	/*
	 * Ensure compiler barrier, otherwise the monitor clear might
	 * occur too late for us ?
	 */
	__asm __volatile("clrex" : : : "memory");
}

extern int64_t dcache_line_size;
extern int64_t icache_line_size;
extern int64_t idcache_line_size;
extern int64_t dczva_line_size;

#define	cpu_nullop()			arm64_nullop()
#define	cpufunc_nullop()		arm64_nullop()
#define	cpu_setttb(a)			arm64_setttb(a)

#define	cpu_tlb_flushID()		arm64_tlb_flushID()

#define	cpu_dcache_wbinv_range(a, s)	arm64_dcache_wbinv_range((a), (s))
#define	cpu_dcache_inv_range(a, s)	arm64_dcache_inv_range((a), (s))
#define	cpu_dcache_wb_range(a, s)	arm64_dcache_wb_range((a), (s))

#define	cpu_idcache_wbinv_range(a, s)	arm64_idcache_wbinv_range((a), (s))
#define	cpu_icache_sync_range(a, s)	arm64_icache_sync_range((a), (s))
#define cpu_icache_sync_range_checked(a, s) arm64_icache_sync_range_checked((a), (s))

void arm64_nullop(void);
void arm64_setttb(vm_offset_t);
void arm64_tlb_flushID(void);
void arm64_tlb_flushID_SE(vm_offset_t);
void arm64_icache_sync_range(vm_offset_t, vm_size_t);
int arm64_icache_sync_range_checked(vm_offset_t, vm_size_t);
void arm64_idcache_wbinv_range(vm_offset_t, vm_size_t);
void arm64_dcache_wbinv_range(vm_offset_t, vm_size_t);
void arm64_dcache_inv_range(vm_offset_t, vm_size_t);
void arm64_dcache_wb_range(vm_offset_t, vm_size_t);

#endif	/* _KERNEL */
#endif	/* _MACHINE_CPUFUNC_H_ */
