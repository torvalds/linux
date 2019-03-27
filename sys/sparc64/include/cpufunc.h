/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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

#ifndef	_MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#include <machine/asi.h>
#include <machine/pstate.h>

struct thread;

/*
 * Membar operand macros for use in other macros when # is a special
 * character.  Keep these in sync with what the hardware expects.
 */
#define	C_Lookaside	(0)
#define	C_MemIssue	(1)
#define	C_Sync		(2)
#define	M_LoadLoad	(0)
#define	M_StoreLoad	(1)
#define	M_LoadStore	(2)
#define	M_StoreStore	(3)

#define	CMASK_SHIFT	(4)
#define	MMASK_SHIFT	(0)

#define	CMASK_GEN(bit)	((1 << (bit)) << CMASK_SHIFT)
#define	MMASK_GEN(bit)	((1 << (bit)) << MMASK_SHIFT)

#define	Lookaside	CMASK_GEN(C_Lookaside)
#define	MemIssue	CMASK_GEN(C_MemIssue)
#define	Sync		CMASK_GEN(C_Sync)
#define	LoadLoad	MMASK_GEN(M_LoadLoad)
#define	StoreLoad	MMASK_GEN(M_StoreLoad)
#define	LoadStore	MMASK_GEN(M_LoadStore)
#define	StoreStore	MMASK_GEN(M_StoreStore)

#define	casa(rs1, rs2, rd, asi) ({					\
	u_int __rd = (uint32_t)(rd);					\
	__asm __volatile("casa [%2] %3, %4, %0"				\
	    : "+r" (__rd), "=m" (*rs1)					\
	    : "r" (rs1), "n" (asi), "r" (rs2), "m" (*rs1));		\
	__rd;								\
})

#define	casxa(rs1, rs2, rd, asi) ({					\
	u_long __rd = (uint64_t)(rd);					\
	__asm __volatile("casxa [%2] %3, %4, %0"			\
	    : "+r" (__rd), "=m" (*rs1)					\
	    : "r" (rs1), "n" (asi), "r" (rs2), "m" (*rs1));		\
	__rd;								\
})

#define	flush(va) do {							\
	__asm __volatile("flush %0" : : "r" (va));			\
} while (0)

#define	flushw() do {							\
	__asm __volatile("flushw" : :);					\
} while (0)

#define	mov(val, reg) do {						\
	__asm __volatile("mov %0, %" __XSTRING(reg) : : "r" (val));	\
} while (0)

/* Generate ld*a/st*a functions for non-constant ASIs. */
#define	LDNC_GEN(tp, o)							\
	static __inline tp						\
	o ## _nc(caddr_t va, int asi)					\
	{								\
		tp r;							\
		__asm __volatile("wr %2, 0, %%asi;" #o " [%1] %%asi, %0"\
		    : "=r" (r) : "r" (va), "r" (asi));			\
		return (r);						\
	}

LDNC_GEN(u_char, lduba);
LDNC_GEN(u_short, lduha);
LDNC_GEN(u_int, lduwa);
LDNC_GEN(u_long, ldxa);

#define	LD_GENERIC(va, asi, op, type) ({				\
	type __r;							\
	__asm __volatile(#op " [%1] %2, %0"				\
	    : "=r" (__r) : "r" (va), "n" (asi));			\
	__r;								\
})

#define	lduba(va, asi)	LD_GENERIC(va, asi, lduba, u_char)
#define	lduha(va, asi)	LD_GENERIC(va, asi, lduha, u_short)
#define	lduwa(va, asi)	LD_GENERIC(va, asi, lduwa, u_int)
#define	ldxa(va, asi)	LD_GENERIC(va, asi, ldxa, u_long)

#define	STNC_GEN(tp, o)							\
	static __inline void						\
	o ## _nc(caddr_t va, int asi, tp val)				\
	{								\
		__asm __volatile("wr %2, 0, %%asi;" #o " %0, [%1] %%asi"\
		    : : "r" (val), "r" (va), "r" (asi));		\
	}

STNC_GEN(u_char, stba);
STNC_GEN(u_short, stha);
STNC_GEN(u_int, stwa);
STNC_GEN(u_long, stxa);

#define	ST_GENERIC(va, asi, val, op)					\
	__asm __volatile(#op " %0, [%1] %2"				\
	    : : "r" (val), "r" (va), "n" (asi));			\

#define	stba(va, asi, val)	ST_GENERIC(va, asi, val, stba)
#define	stha(va, asi, val)	ST_GENERIC(va, asi, val, stha)
#define	stwa(va, asi, val)	ST_GENERIC(va, asi, val, stwa)
#define	stxa(va, asi, val)	ST_GENERIC(va, asi, val, stxa)

/*
 * Attempt to read from addr, val.  If a Data Access Error trap happens,
 * they return -1 and the contents of val is undefined.  A return of 0
 * means no trap happened, and the contents of val is valid.
 */
int fasword8(u_long asi, void *addr, uint8_t *val);
int fasword16(u_long asi, void *addr, uint16_t *val);
int fasword32(u_long asi, void *addr, uint32_t *val);

#define	membar(mask) do {						\
	__asm __volatile("membar %0" : : "n" (mask) : "memory");	\
} while (0)

#define	rd(name) ({							\
	uint64_t __sr;							\
	__asm __volatile("rd %%" #name ", %0" : "=r" (__sr) :);		\
	__sr;								\
})

#define	wr(name, val, xorval) do {					\
	__asm __volatile("wr %0, %1, %%" #name				\
	    : : "r" (val), "rI" (xorval));				\
} while (0)

#define	rdpr(name) ({							\
	uint64_t __pr;							\
	__asm __volatile("rdpr %%" #name", %0" : "=r" (__pr) :);	\
	__pr;								\
})

#define	wrpr(name, val, xorval) do {					\
	__asm __volatile("wrpr %0, %1, %%" #name			\
	    : : "r" (val), "rI" (xorval));				\
} while (0)

/*
 * Trick GAS/GCC into compiling access to TICK/(S)TICK_COMPARE independently
 * of the selected instruction set.
 */
#define	rdtickcmpr()			rd(asr23)
#define	rdstick()			rd(asr24)
#define	rdstickcmpr()			rd(asr25)
#define	wrtickcmpr(val, xorval)		wr(asr23, (val), (xorval))
#define	wrstick(val, xorval)		wr(asr24, (val), (xorval))
#define	wrstickcmpr(val, xorval)	wr(asr25, (val), (xorval))

/*
 * Macro intended to be used instead of wr(asr23, val, xorval) for writing to
 * the TICK_COMPARE register in order to avoid a bug in BlackBird CPUs that
 * can cause these writes to fail under certain conditions which in turn
 * causes the hardclock to stop.  The workaround is to read the TICK_COMPARE
 * register back immediately after writing to it with these two instructions
 * aligned to a quadword boundary in order to ensure that I$ misses won't
 * split them up.
 */
#define	wrtickcmpr_bbwar(val, xorval) ({				\
	__asm __volatile(						\
	"	ba,pt	%%xcc, 1f ;		"			\
	"	 nop	 ;			"			\
	"	.align	128 ;			"			\
	"1:	wr	%0, %1, %%asr23 ;	"			\
	"	rd	%%asr23, %%g0 ;		"			\
	: : "r" (val), "rI" (xorval));					\
})

static __inline void
breakpoint(void)
{

	__asm __volatile("ta %%xcc, 1" : :);
}

static __inline register_t
intr_disable(void)
{
	register_t s;

	s = rdpr(pstate);
	wrpr(pstate, s & ~PSTATE_IE, 0);
	return (s);
}
#define	intr_restore(s)	wrpr(pstate, (s), 0)

/*
 * In some places, it is required that the store is directly followed by a
 * membar #Sync.  Don't trust the compiler to not insert instructions in
 * between.  We also need to disable interrupts completely.
 */
#define	stxa_sync(va, asi, val) do {					\
	register_t s;							\
	s = intr_disable();						\
	__asm __volatile("stxa %0, [%1] %2; membar #Sync"		\
	    : : "r" (val), "r" (va), "n" (asi));			\
	intr_restore(s);						\
} while (0)

void ascopy(u_long asi, vm_offset_t src, vm_offset_t dst, size_t len);
void ascopyfrom(u_long sasi, vm_offset_t src, caddr_t dst, size_t len);
void ascopyto(caddr_t src, u_long dasi, vm_offset_t dst, size_t len);
void aszero(u_long asi, vm_offset_t dst, size_t len);

/*
 * Ultrasparc II doesn't implement popc in hardware.
 */
#if 0
#define	HAVE_INLINE_FFS
/*
 * See page 202 of the SPARC v9 Architecture Manual.
 */
static __inline int
ffs(int mask)
{
	int result;
	int neg;
	int tmp;

	__asm __volatile(
	"	neg	%3, %1 ;	"
	"	xnor	%3, %1, %2 ;	"
	"	popc	%2, %0 ;	"
	"	movrz	%3, %%g0, %0 ;	"
	: "=r" (result), "=r" (neg), "=r" (tmp) : "r" (mask));
	return (result);
}
#endif

#undef LDNC_GEN
#undef STNC_GEN

#endif /* !_MACHINE_CPUFUNC_H_ */
