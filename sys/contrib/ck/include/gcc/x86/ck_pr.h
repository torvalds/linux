/*
 * Copyright 2009-2015 Samy Al Bahra.
 * Copyright 2011 Devon H. O'Dell <devon.odell@gmail.com>
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
 */

#ifndef CK_PR_X86_H
#define CK_PR_X86_H

#ifndef CK_PR_H
#error Do not include this file directly, use ck_pr.h
#endif

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_stdint.h>

/*
 * The following represent supported atomic operations.
 * These operations may be emulated.
 */
#include "ck_f_pr.h"

/* Minimum requirements for the CK_PR interface are met. */
#define CK_F_PR

/*
 * Prevent speculative execution in busy-wait loops (P4 <=) or "predefined
 * delay".
 */
CK_CC_INLINE static void
ck_pr_stall(void)
{
	__asm__ __volatile__("pause" ::: "memory");
	return;
}

#ifdef CK_MD_UMP
#define CK_PR_LOCK_PREFIX
#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__asm__ __volatile__("" ::: "memory");	\
		return;					\
	}
#else
#define CK_PR_LOCK_PREFIX "lock "
#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__asm__ __volatile__(I ::: "memory");	\
		return;					\
	}
#endif /* CK_MD_UMP */

#if defined(CK_MD_SSE_DISABLE)
/* If SSE is disabled, then use atomic operations for serialization. */
#define CK_MD_X86_MFENCE "lock addl $0, (%%esp)"
#define CK_MD_X86_SFENCE CK_MD_X86_MFENCE
#define CK_MD_X86_LFENCE CK_MD_X86_MFENCE
#else
#define CK_MD_X86_SFENCE "sfence"
#define CK_MD_X86_LFENCE "lfence"
#define CK_MD_X86_MFENCE "mfence"
#endif /* !CK_MD_SSE_DISABLE */

CK_PR_FENCE(atomic, "")
CK_PR_FENCE(atomic_store, "")
CK_PR_FENCE(atomic_load, "")
CK_PR_FENCE(store_atomic, "")
CK_PR_FENCE(load_atomic, "")
CK_PR_FENCE(load, CK_MD_X86_LFENCE)
CK_PR_FENCE(load_store, CK_MD_X86_MFENCE)
CK_PR_FENCE(store, CK_MD_X86_SFENCE)
CK_PR_FENCE(store_load, CK_MD_X86_MFENCE)
CK_PR_FENCE(memory, CK_MD_X86_MFENCE)
CK_PR_FENCE(release, CK_MD_X86_MFENCE)
CK_PR_FENCE(acquire, CK_MD_X86_MFENCE)
CK_PR_FENCE(acqrel, CK_MD_X86_MFENCE)
CK_PR_FENCE(lock, CK_MD_X86_MFENCE)
CK_PR_FENCE(unlock, CK_MD_X86_MFENCE)

#undef CK_PR_FENCE

/*
 * Atomic fetch-and-store operations.
 */
#define CK_PR_FAS(S, M, T, C, I)				\
	CK_CC_INLINE static T					\
	ck_pr_fas_##S(M *target, T v)				\
	{							\
		__asm__ __volatile__(I " %0, %1"		\
					: "+m" (*(C *)target),	\
					  "+q" (v)		\
					:			\
					: "memory");		\
		return v;					\
	}

CK_PR_FAS(ptr, void, void *, char, "xchgl")

#define CK_PR_FAS_S(S, T, I) CK_PR_FAS(S, T, T, T, I)

CK_PR_FAS_S(char, char, "xchgb")
CK_PR_FAS_S(uint, unsigned int, "xchgl")
CK_PR_FAS_S(int, int, "xchgl")
CK_PR_FAS_S(32, uint32_t, "xchgl")
CK_PR_FAS_S(16, uint16_t, "xchgw")
CK_PR_FAS_S(8,  uint8_t,  "xchgb")

#undef CK_PR_FAS_S
#undef CK_PR_FAS

#define CK_PR_LOAD(S, M, T, C, I)					\
	CK_CC_INLINE static T						\
	ck_pr_md_load_##S(const M *target)				\
	{								\
		T r;							\
		__asm__ __volatile__(I " %1, %0"			\
					: "=q" (r)			\
					: "m"  (*(const C *)target)	\
					: "memory");			\
		return (r);						\
	}

CK_PR_LOAD(ptr, void, void *, char, "movl")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, T, I)

CK_PR_LOAD_S(char, char, "movb")
CK_PR_LOAD_S(uint, unsigned int, "movl")
CK_PR_LOAD_S(int, int, "movl")
CK_PR_LOAD_S(32, uint32_t, "movl")
CK_PR_LOAD_S(16, uint16_t, "movw")
CK_PR_LOAD_S(8,  uint8_t,  "movb")

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

#define CK_PR_STORE(S, M, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I " %1, %0"		\
					: "=m" (*(C *)target)	\
					: CK_CC_IMM "q" (v)	\
					: "memory");		\
		return;						\
	}

CK_PR_STORE(ptr, void, const void *, char, "movl")

#define CK_PR_STORE_S(S, T, I) CK_PR_STORE(S, T, T, T, I)

CK_PR_STORE_S(char, char, "movb")
CK_PR_STORE_S(uint, unsigned int, "movl")
CK_PR_STORE_S(int, int, "movl")
CK_PR_STORE_S(32, uint32_t, "movl")
CK_PR_STORE_S(16, uint16_t, "movw")
CK_PR_STORE_S(8,  uint8_t, "movb")

#undef CK_PR_STORE_S
#undef CK_PR_STORE

/*
 * Atomic fetch-and-add operations.
 */
#define CK_PR_FAA(S, M, T, C, I)					\
	CK_CC_INLINE static T						\
	ck_pr_faa_##S(M *target, T d)					\
	{								\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %1, %0"	\
					: "+m" (*(C *)target),		\
					  "+q" (d)			\
					:				\
					: "memory", "cc");		\
		return (d);						\
	}

CK_PR_FAA(ptr, void, uintptr_t, char, "xaddl")

#define CK_PR_FAA_S(S, T, I) CK_PR_FAA(S, T, T, T, I)

CK_PR_FAA_S(char, char, "xaddb")
CK_PR_FAA_S(uint, unsigned int, "xaddl")
CK_PR_FAA_S(int, int, "xaddl")
CK_PR_FAA_S(32, uint32_t, "xaddl")
CK_PR_FAA_S(16, uint16_t, "xaddw")
CK_PR_FAA_S(8,  uint8_t,  "xaddb")

#undef CK_PR_FAA_S
#undef CK_PR_FAA

/*
 * Atomic store-only unary operations.
 */
#define CK_PR_UNARY(K, S, T, C, I)				\
	CK_PR_UNARY_R(K, S, T, C, I)				\
	CK_PR_UNARY_V(K, S, T, C, I)

#define CK_PR_UNARY_R(K, S, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_##K##_##S(T *target)				\
	{							\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %0"	\
					: "+m" (*(C *)target)	\
					:			\
					: "memory", "cc");	\
		return;						\
	}

#define CK_PR_UNARY_V(K, S, T, C, I)					\
	CK_CC_INLINE static bool					\
	ck_pr_##K##_##S##_is_zero(T *target)				\
	{								\
		bool ret;						\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %0; setz %1"	\
					: "+m" (*(C *)target),		\
					  "=rm" (ret)			\
					:				\
					: "memory", "cc");		\
		return ret;						\
	}

#define CK_PR_UNARY_S(K, S, T, I) CK_PR_UNARY(K, S, T, T, I)

#define CK_PR_GENERATE(K)				\
	CK_PR_UNARY(K, ptr, void, char, #K "l") 	\
	CK_PR_UNARY_S(K, char, char, #K "b")		\
	CK_PR_UNARY_S(K, int, int, #K "l")		\
	CK_PR_UNARY_S(K, uint, unsigned int, #K "l")	\
	CK_PR_UNARY_S(K, 32, uint32_t, #K "l")		\
	CK_PR_UNARY_S(K, 16, uint16_t, #K "w")		\
	CK_PR_UNARY_S(K, 8, uint8_t, #K "b")

CK_PR_GENERATE(inc)
CK_PR_GENERATE(dec)
CK_PR_GENERATE(neg)

/* not does not affect condition flags. */
#undef CK_PR_UNARY_V
#define CK_PR_UNARY_V(a, b, c, d, e)
CK_PR_GENERATE(not)

#undef CK_PR_GENERATE
#undef CK_PR_UNARY_S
#undef CK_PR_UNARY_V
#undef CK_PR_UNARY_R
#undef CK_PR_UNARY

/*
 * Atomic store-only binary operations.
 */
#define CK_PR_BINARY(K, S, M, T, C, I)					\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S(M *target, T d)					\
	{								\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %1, %0"	\
					: "+m" (*(C *)target)		\
					: CK_CC_IMM "q" (d)		\
					: "memory", "cc");		\
		return;							\
	}

#define CK_PR_BINARY_S(K, S, T, I) CK_PR_BINARY(K, S, T, T, T, I)

#define CK_PR_GENERATE(K)					\
	CK_PR_BINARY(K, ptr, void, uintptr_t, char, #K "l")	\
	CK_PR_BINARY_S(K, char, char, #K "b")			\
	CK_PR_BINARY_S(K, int, int, #K "l")			\
	CK_PR_BINARY_S(K, uint, unsigned int, #K "l")		\
	CK_PR_BINARY_S(K, 32, uint32_t, #K "l")			\
	CK_PR_BINARY_S(K, 16, uint16_t, #K "w")			\
	CK_PR_BINARY_S(K, 8, uint8_t, #K "b")

CK_PR_GENERATE(add)
CK_PR_GENERATE(sub)
CK_PR_GENERATE(and)
CK_PR_GENERATE(or)
CK_PR_GENERATE(xor)

#undef CK_PR_GENERATE
#undef CK_PR_BINARY_S
#undef CK_PR_BINARY

/*
 * Atomic compare and swap.
 */
#define CK_PR_CAS(S, M, T, C, I)						\
	CK_CC_INLINE static bool						\
	ck_pr_cas_##S(M *target, T compare, T set)				\
	{									\
		bool z;								\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %2, %0; setz %1"	\
					: "+m"  (*(C *)target),			\
					  "=a"  (z)				\
					: "q"   (set),				\
					  "a"   (compare)			\
					: "memory", "cc");			\
		return z;							\
	}

CK_PR_CAS(ptr, void, void *, char, "cmpxchgl")

#define CK_PR_CAS_S(S, T, I) CK_PR_CAS(S, T, T, T, I)

CK_PR_CAS_S(char, char, "cmpxchgb")
CK_PR_CAS_S(int, int, "cmpxchgl")
CK_PR_CAS_S(uint, unsigned int, "cmpxchgl")
CK_PR_CAS_S(32, uint32_t, "cmpxchgl")
CK_PR_CAS_S(16, uint16_t, "cmpxchgw")
CK_PR_CAS_S(8,  uint8_t,  "cmpxchgb")

#undef CK_PR_CAS_S
#undef CK_PR_CAS

/*
 * Compare and swap, set *v to old value of target.
 */
#define CK_PR_CAS_O(S, M, T, C, I, R)						\
	CK_CC_INLINE static bool						\
	ck_pr_cas_##S##_value(M *target, T compare, T set, M *v)		\
	{									\
		bool z;								\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX "cmpxchg" I " %3, %0;"	\
				     "mov %% " R ", %2;"			\
				     "setz %1;"					\
					: "+m"  (*(C *)target),			\
					  "=a"  (z),				\
					  "=m"  (*(C *)v)			\
					: "q"   (set),				\
					  "a"   (compare)			\
					: "memory", "cc");			\
		return (bool)z;							\
	}

CK_PR_CAS_O(ptr, void, void *, char, "l", "eax")

#define CK_PR_CAS_O_S(S, T, I, R)	\
	CK_PR_CAS_O(S, T, T, T, I, R)

CK_PR_CAS_O_S(char, char, "b", "al")
CK_PR_CAS_O_S(int, int, "l", "eax")
CK_PR_CAS_O_S(uint, unsigned int, "l", "eax")
CK_PR_CAS_O_S(32, uint32_t, "l", "eax")
CK_PR_CAS_O_S(16, uint16_t, "w", "ax")
CK_PR_CAS_O_S(8,  uint8_t,  "b", "al")

#undef CK_PR_CAS_O_S
#undef CK_PR_CAS_O

/*
 * Atomic bit test operations.
 */
#define CK_PR_BT(K, S, T, P, C, I)					\
	CK_CC_INLINE static bool					\
	ck_pr_##K##_##S(T *target, unsigned int b)			\
	{								\
		bool c;							\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I "; setc %1"	\
					: "+m" (*(C *)target),		\
					  "=q" (c)			\
					: "q"  ((P)b)			\
					: "memory", "cc");		\
		return (bool)c;						\
	}

#define CK_PR_BT_S(K, S, T, I) CK_PR_BT(K, S, T, T, T, I)

#define CK_PR_GENERATE(K)					\
	CK_PR_BT(K, ptr, void, uint32_t, char, #K "l %2, %0")	\
	CK_PR_BT_S(K, uint, unsigned int, #K "l %2, %0")	\
	CK_PR_BT_S(K, int, int, #K "l %2, %0")			\
	CK_PR_BT_S(K, 32, uint32_t, #K "l %2, %0")		\
	CK_PR_BT_S(K, 16, uint16_t, #K "w %w2, %0")

CK_PR_GENERATE(btc)
CK_PR_GENERATE(bts)
CK_PR_GENERATE(btr)

#undef CK_PR_GENERATE
#undef CK_PR_BT

#endif /* CK_PR_X86_H */

