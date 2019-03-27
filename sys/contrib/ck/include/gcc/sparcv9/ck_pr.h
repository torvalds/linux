/*
 * Copyright 2009, 2010 Samy Al Bahra.
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

#ifndef CK_PR_SPARCV9_H
#define CK_PR_SPARCV9_H

#ifndef CK_PR_H
#error Do not include this file directly, use ck_pr.h
#endif

#include <ck_cc.h>
#include <ck_md.h>

/*
 * The following represent supported atomic operations.
 * These operations may be emulated.
 */
#include "ck_f_pr.h"

/*
 * Minimum interface requirement met.
 */
#define CK_F_PR

/*
 * Order loads at the least.
 */
CK_CC_INLINE static void
ck_pr_stall(void)
{

	__asm__ __volatile__("membar #LoadLoad" ::: "memory");
	return;
}

#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__asm__ __volatile__(I ::: "memory");   \
	}

/*
 * Atomic operations are treated as both load and store
 * operations on SPARCv9.
 */
CK_PR_FENCE(atomic, "membar #StoreStore")
CK_PR_FENCE(atomic_store, "membar #StoreStore")
CK_PR_FENCE(atomic_load, "membar #StoreLoad")
CK_PR_FENCE(store_atomic, "membar #StoreStore")
CK_PR_FENCE(load_atomic, "membar #LoadStore")
CK_PR_FENCE(store, "membar #StoreStore")
CK_PR_FENCE(store_load, "membar #StoreLoad")
CK_PR_FENCE(load, "membar #LoadLoad")
CK_PR_FENCE(load_store, "membar #LoadStore")
CK_PR_FENCE(memory, "membar #MemIssue")
CK_PR_FENCE(acquire, "membar #LoadLoad | #LoadStore")
CK_PR_FENCE(release, "membar #LoadStore | #StoreStore")
CK_PR_FENCE(acqrel, "membar #LoadLoad | #LoadStore | #StoreStore")
CK_PR_FENCE(lock, "membar #LoadLoad | #LoadStore | #StoreStore | #StoreLoad")
CK_PR_FENCE(unlock, "membar #LoadStore | #StoreStore")

#undef CK_PR_FENCE

#define CK_PR_LOAD(S, M, T, C, I)				\
	CK_CC_INLINE static T					\
	ck_pr_md_load_##S(const M *target)			\
	{							\
		T r;						\
		__asm__ __volatile__(I " [%1], %0"		\
					: "=&r" (r)		\
					: "r"   (target)	\
					: "memory");		\
		return (r);					\
	}

CK_PR_LOAD(ptr, void, void *, uint64_t, "ldx")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, T, I)

CK_PR_LOAD_S(64, uint64_t, "ldx")
CK_PR_LOAD_S(32, uint32_t, "lduw")
CK_PR_LOAD_S(uint, unsigned int, "lduw")
CK_PR_LOAD_S(double, double, "ldx")
CK_PR_LOAD_S(int, int, "ldsw")

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

#define CK_PR_STORE(S, M, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I " %0, [%1]"		\
					:			\
					: "r" (v),		\
					  "r" (target)		\
					: "memory");		\
		return;						\
	}

CK_PR_STORE(ptr, void, const void *, uint64_t, "stx")

#define CK_PR_STORE_S(S, T, I) CK_PR_STORE(S, T, T, T, I)

CK_PR_STORE_S(8, uint8_t, "stub")
CK_PR_STORE_S(64, uint64_t, "stx")
CK_PR_STORE_S(32, uint32_t, "stuw")
CK_PR_STORE_S(uint, unsigned int, "stuw")
CK_PR_STORE_S(double, double, "stx")
CK_PR_STORE_S(int, int, "stsw")

#undef CK_PR_STORE_S
#undef CK_PR_STORE

/* Use the appropriate address space for atomics within the FreeBSD kernel. */
#if defined(__FreeBSD__) && defined(_KERNEL)
#include <sys/cdefs.h>
#include <machine/atomic.h>
#define CK_PR_INS_CAS "casa"
#define CK_PR_INS_CASX "casxa"
#define CK_PR_INS_SWAP "swapa"
#define CK_PR_ASI_ATOMIC __XSTRING(__ASI_ATOMIC)
#else
#define CK_PR_INS_CAS "cas"
#define CK_PR_INS_CASX "casx"
#define CK_PR_INS_SWAP "swap"
#define CK_PR_ASI_ATOMIC ""
#endif

CK_CC_INLINE static bool
ck_pr_cas_64_value(uint64_t *target, uint64_t compare, uint64_t set, uint64_t *value)
{

	__asm__ __volatile__(CK_PR_INS_CASX " [%1] " CK_PR_ASI_ATOMIC ", %2, %0"
				: "+&r" (set)
				: "r"   (target),
				  "r"   (compare)
				: "memory");

	*value = set;
	return (compare == set);
}

CK_CC_INLINE static bool
ck_pr_cas_64(uint64_t *target, uint64_t compare, uint64_t set)
{

	__asm__ __volatile__(CK_PR_INS_CASX " [%1] " CK_PR_ASI_ATOMIC ", %2, %0"
				: "+&r" (set)
				: "r" (target),
				  "r" (compare)
				: "memory");

	return (compare == set);
}

CK_CC_INLINE static bool
ck_pr_cas_ptr(void *target, void *compare, void *set)
{

	return ck_pr_cas_64(target, (uint64_t)compare, (uint64_t)set);
}

CK_CC_INLINE static bool
ck_pr_cas_ptr_value(void *target, void *compare, void *set, void *previous)
{

	return ck_pr_cas_64_value(target, (uint64_t)compare, (uint64_t)set, previous);
}

#define CK_PR_CAS(N, T)							\
	CK_CC_INLINE static bool					\
	ck_pr_cas_##N##_value(T *target, T compare, T set, T *value)	\
	{								\
		__asm__ __volatile__(CK_PR_INS_CAS " [%1] " CK_PR_ASI_ATOMIC ", %2, %0" \
					: "+&r" (set)			\
					: "r"   (target),		\
					  "r"   (compare)		\
					: "memory");			\
		*value = set;						\
		return (compare == set);				\
	} 								\
	CK_CC_INLINE static bool					\
	ck_pr_cas_##N(T *target, T compare, T set)			\
	{								\
		__asm__ __volatile__(CK_PR_INS_CAS " [%1] " CK_PR_ASI_ATOMIC ", %2, %0" \
					: "+&r" (set)			\
					: "r" (target),			\
					  "r" (compare)			\
					: "memory");			\
		return (compare == set);				\
	}

CK_PR_CAS(32, uint32_t)
CK_PR_CAS(uint, unsigned int)
CK_PR_CAS(int, int)

#undef CK_PR_CAS

#define CK_PR_FAS(N, T)						\
	CK_CC_INLINE static T 					\
	ck_pr_fas_##N(T *target, T update)			\
	{							\
								\
		__asm__ __volatile__(CK_PR_INS_SWAP " [%1] " CK_PR_ASI_ATOMIC ", %0"		\
					: "+&r" (update)	\
					: "r"   (target)	\
					: "memory");		\
		return (update);				\
	}

CK_PR_FAS(int, int)
CK_PR_FAS(uint, unsigned int)
CK_PR_FAS(32, uint32_t)

#undef CK_PR_FAS

#undef CK_PR_INS_CAS
#undef CK_PR_INS_CASX
#undef CK_PR_INS_SWAP
#undef CK_PR_ASI_ATOMIC

#endif /* CK_PR_SPARCV9_H */

