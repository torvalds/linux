/*	$OpenBSD: cpufunc.h,v 1.6 2023/08/21 20:17:30 miod Exp $	*/

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
 * $FreeBSD: head/sys/cpu/include/cpufunc.h 299683 2016-05-13 16:03:50Z andrew $
 */

#ifndef	_MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

static __inline void
breakpoint(void)
{
	__asm("ebreak");
}

#ifdef _KERNEL

#include <machine/riscvreg.h>

#define	rdcycle()			csr_read(cycle)
#define	rdtime()			csr_read(time)
#define	rdinstret()			csr_read(instret)
#define	rdhpmcounter(n)			csr_read(hpmcounter##n)

static __inline void
fence_i(void)
{
	__asm volatile("fence.i" ::: "memory");
}

static __inline void
sfence_vma(void)
{
	__asm volatile("sfence.vma" ::: "memory");
}

static __inline void
sfence_vma_page(uintptr_t addr)
{
	__asm volatile("sfence.vma %0"
			:
			: "r" (addr)
			: "memory");
}

// XXX ASIDs in riscv64 are only 16 bits.
static __inline void
sfence_vma_asid(uint64_t asid)
{
	__asm volatile("sfence.vma x0, %0"
			:
			: "r" (asid)
			: "memory");
}

static __inline void
sfence_vma_page_asid(uintptr_t addr, uint64_t asid)
{
	__asm volatile("sfence.vma %0, %1"
			 :
			 : "r" (addr), "r" (asid)
			 : "memory");
}

extern int64_t dcache_line_size;
extern int64_t icache_line_size;

extern void (*cpu_dcache_wbinv_range)(paddr_t, psize_t);
extern void (*cpu_dcache_inv_range)(paddr_t, psize_t);
extern void (*cpu_dcache_wb_range)(paddr_t, psize_t);

static __inline void
load_satp(uint64_t val)
{
	__asm volatile("csrw satp, %0" :: "r"(val));
}

#endif	/* _KERNEL */
#endif	/* _MACHINE_CPUFUNC_H_ */
