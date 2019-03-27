/*
 * Copyright 2009-2016 Samy Al Bahra.
 * Copyright 2016 Olivier Houchard.
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

#ifndef CK_PR_ARM_H
#define CK_PR_ARM_H

#ifndef CK_PR_H
#error Do not include this file directly, use ck_pr.h
#endif

#include <ck_cc.h>
#include <ck_md.h>

#include <machine/armreg.h>

/*
 * armv4/v5 CPUs lack any instruction that would let us implement an atomic CAS
 * so we have to give atomicity by disabling interrupts. This only works in
 * the kernel, obviously.
 */
#define __with_interrupts_disabled(expr) \
	do {						\
		u_int cpsr_save, tmp;			\
							\
		__asm __volatile(			\
			"mrs  %0, cpsr;"		\
			"orr  %1, %0, %2;"		\
			"msr  cpsr_fsxc, %1;"		\
			: "=r" (cpsr_save), "=r" (tmp)	\
			: "I" (PSR_I | PSR_F)		\
		        : "cc" );		\
		(expr);				\
		 __asm __volatile(		\
			"msr  cpsr_fsxc, %0"	\
			: /* no output */	\
			: "r" (cpsr_save)	\
			: "cc" );		\
	} while(0)
/*
 * The following represent supported atomic operations.
 * These operations may be emulated.
 */
#include "ck_f_pr.h"

/*
 * Minimum interface requirement met.
 */
#define CK_F_PR

CK_CC_INLINE static void
ck_pr_stall(void)
{

	__asm__ __volatile__("" ::: "memory");
	return;
}

#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		I;					\
	}

/* 
 * ARM CPUs prior to armv6 didn't reorder instructions, and we don't
 * support any SMP system, so a compiler barrier should be enough for fences
 */
CK_PR_FENCE(atomic, ck_pr_stall())
CK_PR_FENCE(atomic_store, ck_pr_stall())
CK_PR_FENCE(atomic_load, ck_pr_stall())
CK_PR_FENCE(store_atomic, ck_pr_stall())
CK_PR_FENCE(load_atomic, ck_pr_stall())
CK_PR_FENCE(store, ck_pr_stall())
CK_PR_FENCE(store_load, ck_pr_stall())
CK_PR_FENCE(load, ck_pr_stall())
CK_PR_FENCE(load_store, ck_pr_stall())
CK_PR_FENCE(memory, ck_pr_stall())
CK_PR_FENCE(acquire, ck_pr_stall())
CK_PR_FENCE(release, ck_pr_stall())
CK_PR_FENCE(acqrel, ck_pr_stall())
CK_PR_FENCE(lock, ck_pr_stall())
CK_PR_FENCE(unlock, ck_pr_stall())

#undef CK_PR_FENCE

#define CK_PR_LOAD(S, M, T, C, I)				\
	CK_CC_INLINE static T					\
	ck_pr_md_load_##S(const M *target)			\
	{							\
		long r = 0;					\
		__asm__ __volatile__(I " %0, [%1];"		\
					: "=r" (r)		\
					: "r"  (target)		\
					: "memory");		\
		return ((T)r);					\
	}

CK_PR_LOAD(ptr, void, void *, uint32_t, "ldr")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, T, I)

CK_PR_LOAD_S(32, uint32_t, "ldr")
CK_PR_LOAD_S(16, uint16_t, "ldrh")
CK_PR_LOAD_S(8, uint8_t, "ldrb")
CK_PR_LOAD_S(uint, unsigned int, "ldr")
CK_PR_LOAD_S(int, int, "ldr")
CK_PR_LOAD_S(short, short, "ldrh")
CK_PR_LOAD_S(char, char, "ldrb")

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

#define CK_PR_STORE(S, M, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I " %1, [%0]"		\
					:			\
					: "r" (target),		\
					  "r" (v)		\
					: "memory");		\
		return;						\
	}

CK_PR_STORE(ptr, void, const void *, uint32_t, "str")

#define CK_PR_STORE_S(S, T, I) CK_PR_STORE(S, T, T, T, I)

CK_PR_STORE_S(32, uint32_t, "str")
CK_PR_STORE_S(16, uint16_t, "strh")
CK_PR_STORE_S(8, uint8_t, "strb")
CK_PR_STORE_S(uint, unsigned int, "str")
CK_PR_STORE_S(int, int, "str")
CK_PR_STORE_S(short, short, "strh")
CK_PR_STORE_S(char, char, "strb")

#undef CK_PR_STORE_S
#undef CK_PR_STORE

#define CK_PR_CAS(N, M, T)					\
CK_CC_INLINE static bool					\
ck_pr_cas_##N##_value(M *target, T compare, T set, M *value)	\
{								\
	bool ret;						\
	__with_interrupts_disabled(				\
	    {							\
	        *(T *)value = *(T *)target;			\
	        if (*(T *)target == compare) {			\
	    		*(T *)target = set;			\
	    		ret = true;				\
		} else						\
	    		ret = false;				\
	    }							\
	    );							\
	return ret;						\
}								\
CK_CC_INLINE static bool					\
ck_pr_cas_##N(M *target, T compare, T set)			\
{								\
	bool ret;						\
	__with_interrupts_disabled(				\
	    {							\
	        if (*(T *)target == compare) {			\
	    		*(T *)target = set;			\
	    		ret = true;				\
		} else						\
	    		ret = false;				\
	    }							\
	    );							\
	return ret;						\
}

CK_PR_CAS(ptr, void, void *)

#define CK_PR_CAS_S(N, T) CK_PR_CAS(N, T, T)
CK_PR_CAS_S(32, uint32_t)
CK_PR_CAS_S(uint, unsigned int)
CK_PR_CAS_S(int, int)
CK_PR_CAS_S(16, uint16_t)
CK_PR_CAS_S(8, uint8_t)
CK_PR_CAS_S(short, short)
CK_PR_CAS_S(char, char)

#undef CK_PR_CAS_S
#undef CK_PR_CAS

#define CK_PR_FAS(N, M, T, W)					\
	CK_CC_INLINE static T					\
	ck_pr_fas_##N(M *target, T v)				\
	{							\
		T previous = 0;					\
		__with_interrupts_disabled(			\
		    {						\
		    previous = *(T *)target;			\
		    *(T *)target = v;				\
		    }						\
		    );						\
		return (previous);				\
	}

CK_PR_FAS(32, uint32_t, uint32_t, "")
CK_PR_FAS(ptr, void, void *, "")
CK_PR_FAS(int, int, int, "")
CK_PR_FAS(uint, unsigned int, unsigned int, "")
CK_PR_FAS(16, uint16_t, uint16_t, "h")
CK_PR_FAS(8, uint8_t, uint8_t, "b")
CK_PR_FAS(short, short, short, "h")
CK_PR_FAS(char, char, char, "b")


#undef CK_PR_FAS

#define CK_PR_UNARY(O, N, M, T, I, W)				\
	CK_CC_INLINE static void				\
	ck_pr_##O##_##N(M *target)				\
	{							\
		__with_interrupts_disabled(			\
		    {						\
		    I;						\
		    }						\
		    );						\
		return;						\
	}

CK_PR_UNARY(inc, ptr, void, void *, (*(int *)target)++, "")
CK_PR_UNARY(dec, ptr, void, void *, (*(int *)target)--, "")
CK_PR_UNARY(not, ptr, void, void *, *(int *)target = !(*(int*)target), "")
CK_PR_UNARY(neg, ptr, void, void *, *(int *)target = -(*(int *)target), "")

#define CK_PR_UNARY_S(S, T, W)					\
	CK_PR_UNARY(inc, S, T, T, *target++, W)		\
	CK_PR_UNARY(dec, S, T, T, *target--, W)		\
	CK_PR_UNARY(not, S, T, T, *target = !*target, W)		\
	CK_PR_UNARY(neg, S, T, T, *target = -*target, W)		\

CK_PR_UNARY_S(32, uint32_t, "")
CK_PR_UNARY_S(uint, unsigned int, "")
CK_PR_UNARY_S(int, int, "")
CK_PR_UNARY_S(16, uint16_t, "h")
CK_PR_UNARY_S(8, uint8_t, "b")
CK_PR_UNARY_S(short, short, "h")
CK_PR_UNARY_S(char, char, "b")

#undef CK_PR_UNARY_S
#undef CK_PR_UNARY

#define CK_PR_BINARY(O, N, M, T, I, W)				\
	CK_CC_INLINE static void				\
	ck_pr_##O##_##N(M *target, T delta)			\
	{							\
		__with_interrupts_disabled(			\
		    {						\
		    I;						\
		    }						\
		    );						\
		return;						\
	}

CK_PR_BINARY(and, ptr, void, uintptr_t, *((uintptr_t *)target) &= delta, "")
CK_PR_BINARY(add, ptr, void, uintptr_t, *((uintptr_t *)target) += delta, "")
CK_PR_BINARY(or, ptr, void, uintptr_t, *((uintptr_t *)target) |= delta, "")
CK_PR_BINARY(sub, ptr, void, uintptr_t, *((uintptr_t *)target) -= delta, "")
CK_PR_BINARY(xor, ptr, void, uintptr_t, *((uintptr_t *)target) ^= delta, "")

#define CK_PR_BINARY_S(S, T, W)			\
	CK_PR_BINARY(and, S, T, T, *target &= delta, W)	\
	CK_PR_BINARY(add, S, T, T, *target += delta, W)	\
	CK_PR_BINARY(or, S, T, T, *target |= delta, W)	\
	CK_PR_BINARY(sub, S, T, T, *target -= delta, W)	\
	CK_PR_BINARY(xor, S, T, T, *target ^= delta, W)

CK_PR_BINARY_S(32, uint32_t, "")
CK_PR_BINARY_S(uint, unsigned int, "")
CK_PR_BINARY_S(int, int, "")
CK_PR_BINARY_S(16, uint16_t, "h")
CK_PR_BINARY_S(8, uint8_t, "b")
CK_PR_BINARY_S(short, short, "h")
CK_PR_BINARY_S(char, char, "b")

#undef CK_PR_BINARY_S
#undef CK_PR_BINARY

CK_CC_INLINE static void *
ck_pr_faa_ptr(void *target, uintptr_t delta)
{
	uintptr_t previous;
	__with_interrupts_disabled(
	{
	previous = *(uintptr_t *)target;
	*(uintptr_t *)target += delta;
	}
	);
	return (void *)(previous);
}

#define CK_PR_FAA(S, T, W)						\
	CK_CC_INLINE static T						\
	ck_pr_faa_##S(T *target, T delta)				\
	{								\
		T previous = 0;						\
		__with_interrupts_disabled(				\
		    {							\
		    previous = *target;					\
		    *target += delta;					\
		    }							\
		    );							\
		return (previous);					\
	}

CK_PR_FAA(32, uint32_t, "")
CK_PR_FAA(uint, unsigned int, "")
CK_PR_FAA(int, int, "")
CK_PR_FAA(16, uint16_t, "h")
CK_PR_FAA(8, uint8_t, "b")
CK_PR_FAA(short, short, "h")
CK_PR_FAA(char, char, "b")

#undef CK_PR_FAA

#undef __with_interrupts_disabled

#endif /* CK_PR_ARM_H */

