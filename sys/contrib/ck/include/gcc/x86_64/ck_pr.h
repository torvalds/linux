/*
 * Copyright 2009-2015 Samy Al Bahra.
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

#ifndef CK_PR_X86_64_H
#define CK_PR_X86_64_H

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

/*
 * Support for TSX extensions.
 */
#ifdef CK_MD_RTM_ENABLE
#include "ck_pr_rtm.h"
#endif

/* Minimum requirements for the CK_PR interface are met. */
#define CK_F_PR

#ifdef CK_MD_UMP
#define CK_PR_LOCK_PREFIX
#else
#define CK_PR_LOCK_PREFIX "lock "
#endif

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

#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__asm__ __volatile__(I ::: "memory");	\
	}

/* Atomic operations are always serializing. */
CK_PR_FENCE(atomic, "")
CK_PR_FENCE(atomic_store, "")
CK_PR_FENCE(atomic_load, "")
CK_PR_FENCE(store_atomic, "")
CK_PR_FENCE(load_atomic, "")

/* Traditional fence interface. */
CK_PR_FENCE(load, "lfence")
CK_PR_FENCE(load_store, "mfence")
CK_PR_FENCE(store, "sfence")
CK_PR_FENCE(store_load, "mfence")
CK_PR_FENCE(memory, "mfence")

/* Below are stdatomic-style fences. */

/*
 * Provides load-store and store-store ordering. However, Intel specifies that
 * the WC memory model is relaxed. It is likely an sfence *is* sufficient (in
 * particular, stores are not re-ordered with respect to prior loads and it is
 * really just the stores that are subject to re-ordering). However, we take
 * the conservative route as the manuals are too ambiguous for my taste.
 */
CK_PR_FENCE(release, "mfence")

/*
 * Provides load-load and load-store ordering. The lfence instruction ensures
 * all prior load operations are complete before any subsequent instructions
 * actually begin execution. However, the manual also ends up going to describe
 * WC memory as a relaxed model.
 */
CK_PR_FENCE(acquire, "mfence")

CK_PR_FENCE(acqrel, "mfence")
CK_PR_FENCE(lock, "mfence")
CK_PR_FENCE(unlock, "mfence")

#undef CK_PR_FENCE

/*
 * Read for ownership. Older compilers will generate the 32-bit
 * 3DNow! variant which is binary compatible with x86-64 variant
 * of prefetchw.
 */
#ifndef CK_F_PR_RFO
#define CK_F_PR_RFO
CK_CC_INLINE static void
ck_pr_rfo(const void *m)
{

	__asm__ __volatile__("prefetchw (%0)"
	    :
	    : "r" (m)
	    : "memory");

	return;
}
#endif /* CK_F_PR_RFO */

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

CK_PR_FAS(ptr, void, void *, char, "xchgq")

#define CK_PR_FAS_S(S, T, I) CK_PR_FAS(S, T, T, T, I)

#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_FAS_S(double, double, "xchgq")
#endif
CK_PR_FAS_S(char, char, "xchgb")
CK_PR_FAS_S(uint, unsigned int, "xchgl")
CK_PR_FAS_S(int, int, "xchgl")
CK_PR_FAS_S(64, uint64_t, "xchgq")
CK_PR_FAS_S(32, uint32_t, "xchgl")
CK_PR_FAS_S(16, uint16_t, "xchgw")
CK_PR_FAS_S(8,  uint8_t,  "xchgb")

#undef CK_PR_FAS_S
#undef CK_PR_FAS

/*
 * Atomic load-from-memory operations.
 */
#define CK_PR_LOAD(S, M, T, C, I)				\
	CK_CC_INLINE static T					\
	ck_pr_md_load_##S(const M *target)			\
	{							\
		T r;						\
		__asm__ __volatile__(I " %1, %0"		\
		    : "=q" (r)					\
		    : "m"  (*(const C *)target)			\
		    : "memory");				\
		return (r);					\
	}

CK_PR_LOAD(ptr, void, void *, char, "movq")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, T, I)

CK_PR_LOAD_S(char, char, "movb")
CK_PR_LOAD_S(uint, unsigned int, "movl")
CK_PR_LOAD_S(int, int, "movl")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_LOAD_S(double, double, "movq")
#endif
CK_PR_LOAD_S(64, uint64_t, "movq")
CK_PR_LOAD_S(32, uint32_t, "movl")
CK_PR_LOAD_S(16, uint16_t, "movw")
CK_PR_LOAD_S(8,  uint8_t,  "movb")

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

CK_CC_INLINE static void
ck_pr_load_64_2(const uint64_t target[2], uint64_t v[2])
{
	__asm__ __volatile__("movq %%rdx, %%rcx;"
			     "movq %%rax, %%rbx;"
			     CK_PR_LOCK_PREFIX "cmpxchg16b %2;"
				: "=a" (v[0]),
				  "=d" (v[1])
				: "m" (*(const uint64_t *)target)
				: "rbx", "rcx", "memory", "cc");
	return;
}

CK_CC_INLINE static void
ck_pr_load_ptr_2(const void *t, void *v)
{
	ck_pr_load_64_2(CK_CPP_CAST(const uint64_t *, t),
			CK_CPP_CAST(uint64_t *, v));
	return;
}

#define CK_PR_LOAD_2(S, W, T)							\
	CK_CC_INLINE static void						\
	ck_pr_md_load_##S##_##W(const T t[2], T v[2])				\
	{									\
		ck_pr_load_64_2((const uint64_t *)(const void *)t,		\
				(uint64_t *)(void *)v);				\
		return;								\
	}

CK_PR_LOAD_2(char, 16, char)
CK_PR_LOAD_2(int, 4, int)
CK_PR_LOAD_2(uint, 4, unsigned int)
CK_PR_LOAD_2(32, 4, uint32_t)
CK_PR_LOAD_2(16, 8, uint16_t)
CK_PR_LOAD_2(8, 16, uint8_t)

#undef CK_PR_LOAD_2

/*
 * Atomic store-to-memory operations.
 */
#define CK_PR_STORE_IMM(S, M, T, C, I, K)				\
	CK_CC_INLINE static void					\
	ck_pr_md_store_##S(M *target, T v)				\
	{								\
		__asm__ __volatile__(I " %1, %0"			\
					: "=m" (*(C *)target)		\
					: K "q" (v)			\
					: "memory");			\
		return;							\
	}

#define CK_PR_STORE(S, M, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I " %1, %0"		\
					: "=m" (*(C *)target)	\
					: "q" (v)		\
					: "memory");		\
		return;						\
	}

CK_PR_STORE_IMM(ptr, void, const void *, char, "movq", CK_CC_IMM_U32)
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_STORE(double, double, double, double, "movq")
#endif

#define CK_PR_STORE_S(S, T, I, K) CK_PR_STORE_IMM(S, T, T, T, I, K)

CK_PR_STORE_S(char, char, "movb", CK_CC_IMM_S32)
CK_PR_STORE_S(int, int, "movl", CK_CC_IMM_S32)
CK_PR_STORE_S(uint, unsigned int, "movl", CK_CC_IMM_U32)
CK_PR_STORE_S(64, uint64_t, "movq", CK_CC_IMM_U32)
CK_PR_STORE_S(32, uint32_t, "movl", CK_CC_IMM_U32)
CK_PR_STORE_S(16, uint16_t, "movw", CK_CC_IMM_U32)
CK_PR_STORE_S(8,  uint8_t, "movb", CK_CC_IMM_U32)

#undef CK_PR_STORE_S
#undef CK_PR_STORE_IMM
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

CK_PR_FAA(ptr, void, uintptr_t, char, "xaddq")

#define CK_PR_FAA_S(S, T, I) CK_PR_FAA(S, T, T, T, I)

CK_PR_FAA_S(char, char, "xaddb")
CK_PR_FAA_S(uint, unsigned int, "xaddl")
CK_PR_FAA_S(int, int, "xaddl")
CK_PR_FAA_S(64, uint64_t, "xaddq")
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
	CK_PR_UNARY(K, ptr, void, char, #K "q") 	\
	CK_PR_UNARY_S(K, char, char, #K "b")		\
	CK_PR_UNARY_S(K, int, int, #K "l")		\
	CK_PR_UNARY_S(K, uint, unsigned int, #K "l")	\
	CK_PR_UNARY_S(K, 64, uint64_t, #K "q")		\
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
#define CK_PR_BINARY(K, S, M, T, C, I, O)				\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S(M *target, T d)					\
	{								\
		__asm__ __volatile__(CK_PR_LOCK_PREFIX I " %1, %0"	\
					: "+m" (*(C *)target)		\
					: O "q" (d)			\
					: "memory", "cc");		\
		return;							\
	}

#define CK_PR_BINARY_S(K, S, T, I, O) CK_PR_BINARY(K, S, T, T, T, I, O)

#define CK_PR_GENERATE(K)							\
	CK_PR_BINARY(K, ptr, void, uintptr_t, char, #K "q", CK_CC_IMM_U32)	\
	CK_PR_BINARY_S(K, char, char, #K "b", CK_CC_IMM_S32)			\
	CK_PR_BINARY_S(K, int, int, #K "l", CK_CC_IMM_S32)			\
	CK_PR_BINARY_S(K, uint, unsigned int, #K "l", CK_CC_IMM_U32)		\
	CK_PR_BINARY_S(K, 64, uint64_t, #K "q", CK_CC_IMM_U32)			\
	CK_PR_BINARY_S(K, 32, uint32_t, #K "l", CK_CC_IMM_U32)			\
	CK_PR_BINARY_S(K, 16, uint16_t, #K "w", CK_CC_IMM_U32)			\
	CK_PR_BINARY_S(K, 8, uint8_t, #K "b", CK_CC_IMM_U32)

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

CK_PR_CAS(ptr, void, void *, char, "cmpxchgq")

#define CK_PR_CAS_S(S, T, I) CK_PR_CAS(S, T, T, T, I)

CK_PR_CAS_S(char, char, "cmpxchgb")
CK_PR_CAS_S(int, int, "cmpxchgl")
CK_PR_CAS_S(uint, unsigned int, "cmpxchgl")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_CAS_S(double, double, "cmpxchgq")
#endif
CK_PR_CAS_S(64, uint64_t, "cmpxchgq")
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
		return z;							\
	}

CK_PR_CAS_O(ptr, void, void *, char, "q", "rax")

#define CK_PR_CAS_O_S(S, T, I, R)	\
	CK_PR_CAS_O(S, T, T, T, I, R)

CK_PR_CAS_O_S(char, char, "b", "al")
CK_PR_CAS_O_S(int, int, "l", "eax")
CK_PR_CAS_O_S(uint, unsigned int, "l", "eax")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_CAS_O_S(double, double, "q", "rax")
#endif
CK_PR_CAS_O_S(64, uint64_t, "q", "rax")
CK_PR_CAS_O_S(32, uint32_t, "l", "eax")
CK_PR_CAS_O_S(16, uint16_t, "w", "ax")
CK_PR_CAS_O_S(8,  uint8_t,  "b", "al")

#undef CK_PR_CAS_O_S
#undef CK_PR_CAS_O

/*
 * Contrary to C-interface, alignment requirements are that of uint64_t[2].
 */
CK_CC_INLINE static bool
ck_pr_cas_64_2(uint64_t target[2], uint64_t compare[2], uint64_t set[2])
{
	bool z;

	__asm__ __volatile__("movq 0(%4), %%rax;"
			     "movq 8(%4), %%rdx;"
			     CK_PR_LOCK_PREFIX "cmpxchg16b %0; setz %1"
				: "+m" (*target),
				  "=q" (z)
				: "b"  (set[0]),
				  "c"  (set[1]),
				  "q"  (compare)
				: "memory", "cc", "%rax", "%rdx");
	return z;
}

CK_CC_INLINE static bool
ck_pr_cas_ptr_2(void *t, void *c, void *s)
{
	return ck_pr_cas_64_2(CK_CPP_CAST(uint64_t *, t),
			      CK_CPP_CAST(uint64_t *, c),
			      CK_CPP_CAST(uint64_t *, s));
}

CK_CC_INLINE static bool
ck_pr_cas_64_2_value(uint64_t target[2],
		     uint64_t compare[2],
		     uint64_t set[2],
		     uint64_t v[2])
{
	bool z;

	__asm__ __volatile__(CK_PR_LOCK_PREFIX "cmpxchg16b %0;"
			     "setz %3"
				: "+m" (*target),
				  "=a" (v[0]),
				  "=d" (v[1]),
				  "=q" (z)
				: "a" (compare[0]),
				  "d" (compare[1]),
				  "b" (set[0]),
				  "c" (set[1])
				: "memory", "cc");
	return z;
}

CK_CC_INLINE static bool
ck_pr_cas_ptr_2_value(void *t, void *c, void *s, void *v)
{
	return ck_pr_cas_64_2_value(CK_CPP_CAST(uint64_t *,t),
				    CK_CPP_CAST(uint64_t *,c),
				    CK_CPP_CAST(uint64_t *,s),
				    CK_CPP_CAST(uint64_t *,v));
}

#define CK_PR_CAS_V(S, W, T)					\
CK_CC_INLINE static bool					\
ck_pr_cas_##S##_##W(T t[W], T c[W], T s[W])			\
{								\
	return ck_pr_cas_64_2((uint64_t *)(void *)t,		\
			      (uint64_t *)(void *)c,		\
			      (uint64_t *)(void *)s);		\
}								\
CK_CC_INLINE static bool					\
ck_pr_cas_##S##_##W##_value(T *t, T c[W], T s[W], T *v)		\
{								\
	return ck_pr_cas_64_2_value((uint64_t *)(void *)t,	\
				    (uint64_t *)(void *)c,	\
				    (uint64_t *)(void *)s,	\
				    (uint64_t *)(void *)v);	\
}

#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_CAS_V(double, 2, double)
#endif
CK_PR_CAS_V(char, 16, char)
CK_PR_CAS_V(int, 4, int)
CK_PR_CAS_V(uint, 4, unsigned int)
CK_PR_CAS_V(32, 4, uint32_t)
CK_PR_CAS_V(16, 8, uint16_t)
CK_PR_CAS_V(8, 16, uint8_t)

#undef CK_PR_CAS_V

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
		return c;						\
	}

#define CK_PR_BT_S(K, S, T, I) CK_PR_BT(K, S, T, T, T, I)

#define CK_PR_GENERATE(K)					\
	CK_PR_BT(K, ptr, void, uint64_t, char, #K "q %2, %0")	\
	CK_PR_BT_S(K, uint, unsigned int, #K "l %2, %0")	\
	CK_PR_BT_S(K, int, int, #K "l %2, %0")			\
	CK_PR_BT_S(K, 64, uint64_t, #K "q %2, %0")		\
	CK_PR_BT_S(K, 32, uint32_t, #K "l %2, %0")		\
	CK_PR_BT_S(K, 16, uint16_t, #K "w %w2, %0")

CK_PR_GENERATE(btc)
CK_PR_GENERATE(bts)
CK_PR_GENERATE(btr)

#undef CK_PR_GENERATE
#undef CK_PR_BT

#endif /* CK_PR_X86_64_H */

