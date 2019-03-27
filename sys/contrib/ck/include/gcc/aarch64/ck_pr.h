/*
 * Copyright 2009-2016 Samy Al Bahra.
 * Copyright 2013-2016 Olivier Houchard.
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

#ifndef CK_PR_AARCH64_H
#define CK_PR_AARCH64_H

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

CK_CC_INLINE static void
ck_pr_stall(void)
{

	__asm__ __volatile__("" ::: "memory");
	return;
}

#define CK_DMB_SY __asm __volatile("dmb ish" : : "r" (0) : "memory")
#define CK_DMB_LD __asm __volatile("dmb ishld" : : "r" (0) : "memory")
#define CK_DMB_ST __asm __volatile("dmb ishst" : : "r" (0) : "memory")

#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		I;					\
	}

CK_PR_FENCE(atomic, CK_DMB_ST)
CK_PR_FENCE(atomic_store, CK_DMB_ST)
CK_PR_FENCE(atomic_load, CK_DMB_SY)
CK_PR_FENCE(store_atomic, CK_DMB_ST)
CK_PR_FENCE(load_atomic, CK_DMB_SY)
CK_PR_FENCE(store, CK_DMB_ST)
CK_PR_FENCE(store_load, CK_DMB_SY)
CK_PR_FENCE(load, CK_DMB_LD)
CK_PR_FENCE(load_store, CK_DMB_SY)
CK_PR_FENCE(memory, CK_DMB_SY)
CK_PR_FENCE(acquire, CK_DMB_SY)
CK_PR_FENCE(release, CK_DMB_SY)
CK_PR_FENCE(acqrel, CK_DMB_SY)
CK_PR_FENCE(lock, CK_DMB_SY)
CK_PR_FENCE(unlock, CK_DMB_SY)

#undef CK_PR_FENCE

#undef CK_DMB_SI
#undef CK_DMB_LD
#undef CK_DMB_ST

#define CK_PR_LOAD(S, M, T, I)				\
	CK_CC_INLINE static T					\
	ck_pr_md_load_##S(const M *target)			\
	{							\
		long r = 0;					\
		__asm__ __volatile__(I " %w0, [%1];"		\
					: "=r" (r)		\
					: "r"  (target)		\
					: "memory");		\
		return ((T)r);					\
	}
#define CK_PR_LOAD_64(S, M, T, I)				\
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


CK_PR_LOAD_64(ptr, void, void *, "ldr")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, I)
#define CK_PR_LOAD_S_64(S, T, I) CK_PR_LOAD_64(S, T, T, I)

CK_PR_LOAD_S_64(64, uint64_t, "ldr")
CK_PR_LOAD_S(32, uint32_t, "ldr")
CK_PR_LOAD_S(16, uint16_t, "ldrh")
CK_PR_LOAD_S(8, uint8_t, "ldrb")
CK_PR_LOAD_S(uint, unsigned int, "ldr")
CK_PR_LOAD_S(int, int, "ldr")
CK_PR_LOAD_S(short, short, "ldrh")
CK_PR_LOAD_S(char, char, "ldrb")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_LOAD_S_64(double, double, "ldr")
#endif

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD_S_64
#undef CK_PR_LOAD
#undef CK_PR_LAOD_64

#define CK_PR_STORE(S, M, T, I)					\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I " %w1, [%0]"		\
					:			\
					: "r" (target),		\
					  "r" (v)		\
					: "memory");		\
		return;						\
	}
#define CK_PR_STORE_64(S, M, T, I)				\
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

CK_PR_STORE_64(ptr, void, const void *, "str")

#define CK_PR_STORE_S(S, T, I) CK_PR_STORE(S, T, T, I)
#define CK_PR_STORE_S_64(S, T, I) CK_PR_STORE_64(S, T, T, I)

CK_PR_STORE_S_64(64, uint64_t, "str")
CK_PR_STORE_S(32, uint32_t, "str")
CK_PR_STORE_S(16, uint16_t, "strh")
CK_PR_STORE_S(8, uint8_t, "strb")
CK_PR_STORE_S(uint, unsigned int, "str")
CK_PR_STORE_S(int, int, "str")
CK_PR_STORE_S(short, short, "strh")
CK_PR_STORE_S(char, char, "strb")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_STORE_S_64(double, double, "str")
#endif

#undef CK_PR_STORE_S
#undef CK_PR_STORE_S_64
#undef CK_PR_STORE
#undef CK_PR_STORE_64

#ifdef CK_MD_LSE_ENABLE
#include "ck_pr_lse.h"
#else
#include "ck_pr_llsc.h"
#endif

/*
 * ck_pr_neg_*() functions can only be implemented via LL/SC, as there are no
 * LSE alternatives.
 */
#define CK_PR_NEG(N, M, T, W, R)				\
        CK_CC_INLINE static void				\
        ck_pr_neg_##N(M *target)				\
        {							\
                T previous = 0;					\
                T tmp = 0;					\
                __asm__ __volatile__("1:"			\
                                     "ldxr" W " %" R "0, [%2];"	\
                                     "neg %" R "0, %" R "0;"	\
                                     "stxr" W " %w1, %" R "0, [%2];"	\
                                     "cbnz %w1, 1b;"		\
                                        : "=&r" (previous),	\
                                          "=&r" (tmp)		\
                                        : "r"   (target)	\
                                        : "memory", "cc");	\
                return;						\
        }

CK_PR_NEG(ptr, void, void *, "", "")
CK_PR_NEG(64, uint64_t, uint64_t, "", "")

#define CK_PR_NEG_S(S, T, W)					\
        CK_PR_NEG(S, T, T, W, "w")				\

CK_PR_NEG_S(32, uint32_t, "")
CK_PR_NEG_S(uint, unsigned int, "")
CK_PR_NEG_S(int, int, "")
CK_PR_NEG_S(16, uint16_t, "h")
CK_PR_NEG_S(8, uint8_t, "b")
CK_PR_NEG_S(short, short, "h")
CK_PR_NEG_S(char, char, "b")

#undef CK_PR_NEG_S
#undef CK_PR_NEG

#endif /* CK_PR_AARCH64_H */

