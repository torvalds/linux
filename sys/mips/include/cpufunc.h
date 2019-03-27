/*	$OpenBSD: pio.h,v 1.2 1998/09/15 10:50:12 pefo Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-4-Clause
 *
 * Copyright (c) 2002-2004 Juli Mallett.  All rights reserved.
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
 * Copyright (c) 1995-1999 Per Fogelstrom.  All rights reserved.
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
 *      This product includes software developed by Per Fogelstrom.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	JNPR: cpufunc.h,v 1.5 2007/08/09 11:23:32 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#include <sys/types.h>
#include <machine/cpuregs.h>

/* 
 * These functions are required by user-land atomi ops
 */ 

static __inline void
mips_barrier(void)
{
#if defined(CPU_CNMIPS) || defined(CPU_RMI) || defined(CPU_NLM)
	__compiler_membar();
#else
	__asm __volatile (".set noreorder\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  "nop\n\t"
			  ".set reorder\n\t"
			  : : : "memory");
#endif
}

static __inline void
mips_cp0_sync(void)
{
	__asm __volatile (__XSTRING(COP0_SYNC));
}

static __inline void
mips_wbflush(void)
{
#if defined(CPU_CNMIPS)
	__asm __volatile (".set noreorder\n\t"
			"syncw\n\t"
			".set reorder\n"
			: : : "memory");
#else	
	__asm __volatile ("sync" : : : "memory");
	mips_barrier();
#endif
}

static __inline void
breakpoint(void)
{
	__asm __volatile ("break");
}

#ifdef _KERNEL
/*
 * XXX
 * It would be nice to add variants that read/write register_t, to avoid some
 * ABI checks.
 */
#if defined(__mips_n32) || defined(__mips_n64)
#define	MIPS_RW64_COP0(n,r)					\
static __inline uint64_t					\
mips_rd_ ## n (void)						\
{								\
	int v0;							\
	__asm __volatile ("dmfc0 %[v0], $"__XSTRING(r)";"	\
			  : [v0] "=&r"(v0));			\
	mips_barrier();						\
	return (v0);						\
}								\
static __inline void						\
mips_wr_ ## n (uint64_t a0)					\
{								\
	__asm __volatile ("dmtc0 %[a0], $"__XSTRING(r)";"	\
			 __XSTRING(COP0_SYNC)";"		\
			 "nop;"					\
			 "nop;"					\
			 :					\
			 : [a0] "r"(a0));			\
	mips_barrier();						\
} struct __hack

#define	MIPS_RW64_COP0_SEL(n,r,s)				\
static __inline uint64_t					\
mips_rd_ ## n(void)						\
{								\
	int v0;							\
	__asm __volatile ("dmfc0 %[v0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			  : [v0] "=&r"(v0));			\
	mips_barrier();						\
	return (v0);						\
}								\
static __inline void						\
mips_wr_ ## n(uint64_t a0)					\
{								\
	__asm __volatile ("dmtc0 %[a0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			 __XSTRING(COP0_SYNC)";"		\
			 :					\
			 : [a0] "r"(a0));			\
	mips_barrier();						\
} struct __hack

#if defined(__mips_n64)
MIPS_RW64_COP0(excpc, MIPS_COP_0_EXC_PC);
MIPS_RW64_COP0(entryhi, MIPS_COP_0_TLB_HI);
MIPS_RW64_COP0(pagemask, MIPS_COP_0_TLB_PG_MASK);
MIPS_RW64_COP0_SEL(userlocal, MIPS_COP_0_USERLOCAL, 2);
#ifdef CPU_CNMIPS
MIPS_RW64_COP0_SEL(cvmcount, MIPS_COP_0_COUNT, 6);
MIPS_RW64_COP0_SEL(cvmctl, MIPS_COP_0_COUNT, 7);
MIPS_RW64_COP0_SEL(cvmmemctl, MIPS_COP_0_COMPARE, 7);
MIPS_RW64_COP0_SEL(icache_err, MIPS_COP_0_CACHE_ERR, 0);
MIPS_RW64_COP0_SEL(dcache_err, MIPS_COP_0_CACHE_ERR, 1);
#endif
#endif
#if defined(__mips_n64) || defined(__mips_n32) /* PHYSADDR_64_BIT */
MIPS_RW64_COP0(entrylo0, MIPS_COP_0_TLB_LO0);
MIPS_RW64_COP0(entrylo1, MIPS_COP_0_TLB_LO1);
#endif
MIPS_RW64_COP0(xcontext, MIPS_COP_0_TLB_XCONTEXT);

#undef	MIPS_RW64_COP0
#undef	MIPS_RW64_COP0_SEL
#endif

#define	MIPS_RW32_COP0(n,r)					\
static __inline uint32_t					\
mips_rd_ ## n (void)						\
{								\
	int v0;							\
	__asm __volatile ("mfc0 %[v0], $"__XSTRING(r)";"	\
			  : [v0] "=&r"(v0));			\
	mips_barrier();						\
	return (v0);						\
}								\
static __inline void						\
mips_wr_ ## n (uint32_t a0)					\
{								\
	__asm __volatile ("mtc0 %[a0], $"__XSTRING(r)";"	\
			 __XSTRING(COP0_SYNC)";"		\
			 "nop;"					\
			 "nop;"					\
			 :					\
			 : [a0] "r"(a0));			\
	mips_barrier();						\
} struct __hack

#define	MIPS_RW32_COP0_SEL(n,r,s)				\
static __inline uint32_t					\
mips_rd_ ## n(void)						\
{								\
	int v0;							\
	__asm __volatile ("mfc0 %[v0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			  : [v0] "=&r"(v0));			\
	mips_barrier();						\
	return (v0);						\
}								\
static __inline void						\
mips_wr_ ## n(uint32_t a0)					\
{								\
	__asm __volatile ("mtc0 %[a0], $"__XSTRING(r)", "__XSTRING(s)";"	\
			 __XSTRING(COP0_SYNC)";"		\
			 "nop;"					\
			 "nop;"					\
			 :					\
			 : [a0] "r"(a0));			\
	mips_barrier();						\
} struct __hack

#ifdef CPU_CNMIPS
static __inline void mips_sync_icache (void)
{
	__asm __volatile (
		".set push\n"
		".set mips64\n"
		".word 0x041f0000\n"		/* xxx ICACHE */
		"nop\n"
		".set pop\n"
		: : );
}
#endif

MIPS_RW32_COP0(compare, MIPS_COP_0_COMPARE);
MIPS_RW32_COP0(config, MIPS_COP_0_CONFIG);
MIPS_RW32_COP0_SEL(config1, MIPS_COP_0_CONFIG, 1);
MIPS_RW32_COP0_SEL(config2, MIPS_COP_0_CONFIG, 2);
MIPS_RW32_COP0_SEL(config3, MIPS_COP_0_CONFIG, 3);
#ifdef CPU_CNMIPS
MIPS_RW32_COP0_SEL(config4, MIPS_COP_0_CONFIG, 4);
#endif
#ifdef BERI_LARGE_TLB
MIPS_RW32_COP0_SEL(config5, MIPS_COP_0_CONFIG, 5);
#endif
#if defined(CPU_NLM) || defined(BERI_LARGE_TLB)
MIPS_RW32_COP0_SEL(config6, MIPS_COP_0_CONFIG, 6);
#endif
#if defined(CPU_NLM) || defined(CPU_MIPS1004K) || defined (CPU_MIPS74K) || \
    defined(CPU_MIPS24K)
MIPS_RW32_COP0_SEL(config7, MIPS_COP_0_CONFIG, 7);
#endif
MIPS_RW32_COP0(count, MIPS_COP_0_COUNT);
MIPS_RW32_COP0(index, MIPS_COP_0_TLB_INDEX);
MIPS_RW32_COP0(wired, MIPS_COP_0_TLB_WIRED);
MIPS_RW32_COP0(cause, MIPS_COP_0_CAUSE);
#if !defined(__mips_n64)
MIPS_RW32_COP0(excpc, MIPS_COP_0_EXC_PC);
#endif
MIPS_RW32_COP0(status, MIPS_COP_0_STATUS);
MIPS_RW32_COP0_SEL(cmgcrbase, 15, 3);

/* XXX: Some of these registers are specific to MIPS32. */
#if !defined(__mips_n64)
MIPS_RW32_COP0(entryhi, MIPS_COP_0_TLB_HI);
MIPS_RW32_COP0(pagemask, MIPS_COP_0_TLB_PG_MASK);
MIPS_RW32_COP0_SEL(userlocal, MIPS_COP_0_USERLOCAL, 2);
#endif
#ifdef CPU_NLM
MIPS_RW32_COP0_SEL(pagegrain, MIPS_COP_0_TLB_PG_MASK, 1);
#endif
#if !defined(__mips_n64) && !defined(__mips_n32) /* !PHYSADDR_64_BIT */
MIPS_RW32_COP0(entrylo0, MIPS_COP_0_TLB_LO0);
MIPS_RW32_COP0(entrylo1, MIPS_COP_0_TLB_LO1);
#endif
MIPS_RW32_COP0(prid, MIPS_COP_0_PRID);
MIPS_RW32_COP0_SEL(cinfo, MIPS_COP_0_PRID, 6);
MIPS_RW32_COP0_SEL(tinfo, MIPS_COP_0_PRID, 7);
/* XXX 64-bit?  */
MIPS_RW32_COP0_SEL(ebase, MIPS_COP_0_PRID, 1);

#if defined(CPU_MIPS24K) || defined(CPU_MIPS34K) ||		\
    defined(CPU_MIPS74K) || defined(CPU_MIPS1004K)  ||	\
    defined(CPU_MIPS1074K) || defined(CPU_INTERAPTIV) ||	\
    defined(CPU_PROAPTIV)
/* MIPS32/64 r2 intctl */
MIPS_RW32_COP0_SEL(intctl, MIPS_COP_0_INTCTL, 1);
#endif

#ifdef CPU_XBURST
MIPS_RW32_COP0_SEL(xburst_mbox0, MIPS_COP_0_XBURST_MBOX, 0);
MIPS_RW32_COP0_SEL(xburst_mbox1, MIPS_COP_0_XBURST_MBOX, 1);
MIPS_RW32_COP0_SEL(xburst_core_ctl, MIPS_COP_0_XBURST_C12, 2);
MIPS_RW32_COP0_SEL(xburst_core_sts, MIPS_COP_0_XBURST_C12, 3);
MIPS_RW32_COP0_SEL(xburst_reim, MIPS_COP_0_XBURST_C12, 4);
#endif
MIPS_RW32_COP0(watchlo, MIPS_COP_0_WATCH_LO);
MIPS_RW32_COP0_SEL(watchlo1, MIPS_COP_0_WATCH_LO, 1);
MIPS_RW32_COP0_SEL(watchlo2, MIPS_COP_0_WATCH_LO, 2);
MIPS_RW32_COP0_SEL(watchlo3, MIPS_COP_0_WATCH_LO, 3);
MIPS_RW32_COP0(watchhi, MIPS_COP_0_WATCH_HI);
MIPS_RW32_COP0_SEL(watchhi1, MIPS_COP_0_WATCH_HI, 1);
MIPS_RW32_COP0_SEL(watchhi2, MIPS_COP_0_WATCH_HI, 2);
MIPS_RW32_COP0_SEL(watchhi3, MIPS_COP_0_WATCH_HI, 3);

MIPS_RW32_COP0_SEL(perfcnt0, MIPS_COP_0_PERFCNT, 0);
MIPS_RW32_COP0_SEL(perfcnt1, MIPS_COP_0_PERFCNT, 1);
MIPS_RW32_COP0_SEL(perfcnt2, MIPS_COP_0_PERFCNT, 2);
MIPS_RW32_COP0_SEL(perfcnt3, MIPS_COP_0_PERFCNT, 3);
MIPS_RW32_COP0(hwrena, MIPS_COP_0_HWRENA);

#undef	MIPS_RW32_COP0
#undef	MIPS_RW32_COP0_SEL

static __inline register_t
intr_disable(void)
{
	register_t s;

	s = mips_rd_status();
	mips_wr_status(s & ~MIPS_SR_INT_IE);

	return (s & MIPS_SR_INT_IE);
}

static __inline register_t
intr_enable(void)
{
	register_t s;

	s = mips_rd_status();
	mips_wr_status(s | MIPS_SR_INT_IE);

	return (s);
}

static __inline void
intr_restore(register_t ie)
{
	if (ie == MIPS_SR_INT_IE) {
		intr_enable();
	}
}

static __inline uint32_t
set_intr_mask(uint32_t mask)
{
	uint32_t ostatus;

	ostatus = mips_rd_status();
	mask = (ostatus & ~MIPS_SR_INT_MASK) | (mask & MIPS_SR_INT_MASK);
	mips_wr_status(mask);
	return (ostatus);
}

static __inline uint32_t
get_intr_mask(void)
{

	return (mips_rd_status() & MIPS_SR_INT_MASK);
}

#endif /* _KERNEL */

#define	readb(va)	(*(volatile uint8_t *) (va))
#define	readw(va)	(*(volatile uint16_t *) (va))
#define	readl(va)	(*(volatile uint32_t *) (va))
#if !defined(__mips_o32)
#define	readq(a)	(*(volatile uint64_t *)(a))
#endif
 
#define	writeb(va, d)	(*(volatile uint8_t *) (va) = (d))
#define	writew(va, d)	(*(volatile uint16_t *) (va) = (d))
#define	writel(va, d)	(*(volatile uint32_t *) (va) = (d))
#if !defined(__mips_o32)
#define	writeq(va, d)	(*(volatile uint64_t *) (va) = (d))
#endif

#endif /* !_MACHINE_CPUFUNC_H_ */
