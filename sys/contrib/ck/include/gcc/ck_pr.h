/*
 * Copyright 2010 Samy Al Bahra.
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

#ifndef CK_PR_GCC_H
#define CK_PR_GCC_H

#ifndef CK_PR_H
#error Do not include this file directly, use ck_pr.h
#endif

#include <ck_cc.h>

CK_CC_INLINE static void
ck_pr_barrier(void)
{

	__asm__ __volatile__("" ::: "memory");
	return;
}

#ifndef CK_F_PR
#define CK_F_PR

#include <ck_stdbool.h>
#include <ck_stdint.h>

/*
 * The following represent supported atomic operations.
 * These operations may be emulated.
 */
#include "ck_f_pr.h"

#define CK_PR_ACCESS(x) (*(volatile __typeof__(x) *)&(x))

#define CK_PR_LOAD(S, M, T)		 			\
	CK_CC_INLINE static T					\
	ck_pr_md_load_##S(const M *target)			\
	{							\
		T r;						\
		ck_pr_barrier();				\
		r = CK_PR_ACCESS(*(const T *)target);		\
		ck_pr_barrier();				\
		return (r);					\
	}							\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		ck_pr_barrier();				\
		CK_PR_ACCESS(*(T *)target) = v;			\
		ck_pr_barrier();				\
		return;						\
	}

CK_CC_INLINE static void *
ck_pr_md_load_ptr(const void *target)
{
	void *r;

	ck_pr_barrier();
	r = CK_CC_DECONST_PTR(*(volatile void *const*)(target));
	ck_pr_barrier();

	return r;
}

CK_CC_INLINE static void
ck_pr_md_store_ptr(void *target, const void *v)
{

	ck_pr_barrier();
	*(volatile void **)target = CK_CC_DECONST_PTR(v);
	ck_pr_barrier();
	return;
}

#define CK_PR_LOAD_S(S, T) CK_PR_LOAD(S, T, T)

CK_PR_LOAD_S(char, char)
CK_PR_LOAD_S(uint, unsigned int)
CK_PR_LOAD_S(int, int)
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_LOAD_S(double, double)
#endif
CK_PR_LOAD_S(64, uint64_t)
CK_PR_LOAD_S(32, uint32_t)
CK_PR_LOAD_S(16, uint16_t)
CK_PR_LOAD_S(8,  uint8_t)

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

CK_CC_INLINE static void
ck_pr_stall(void)
{

	ck_pr_barrier();
}

/*
 * Load and store fences are equivalent to full fences in the GCC port.
 */
#define CK_PR_FENCE(T)					\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__sync_synchronize();			\
	}

CK_PR_FENCE(atomic)
CK_PR_FENCE(atomic_atomic)
CK_PR_FENCE(atomic_load)
CK_PR_FENCE(atomic_store)
CK_PR_FENCE(store_atomic)
CK_PR_FENCE(load_atomic)
CK_PR_FENCE(load)
CK_PR_FENCE(load_load)
CK_PR_FENCE(load_store)
CK_PR_FENCE(store)
CK_PR_FENCE(store_store)
CK_PR_FENCE(store_load)
CK_PR_FENCE(memory)
CK_PR_FENCE(acquire)
CK_PR_FENCE(release)
CK_PR_FENCE(acqrel)
CK_PR_FENCE(lock)
CK_PR_FENCE(unlock)

#undef CK_PR_FENCE

/*
 * Atomic compare and swap.
 */
#define CK_PR_CAS(S, M, T)							\
	CK_CC_INLINE static bool						\
	ck_pr_cas_##S(M *target, T compare, T set)				\
	{									\
		bool z;								\
		z = __sync_bool_compare_and_swap((T *)target, compare, set);	\
		return z;							\
	}

CK_PR_CAS(ptr, void, void *)

#define CK_PR_CAS_S(S, T) CK_PR_CAS(S, T, T)

CK_PR_CAS_S(char, char)
CK_PR_CAS_S(int, int)
CK_PR_CAS_S(uint, unsigned int)
CK_PR_CAS_S(64, uint64_t)
CK_PR_CAS_S(32, uint32_t)
CK_PR_CAS_S(16, uint16_t)
CK_PR_CAS_S(8,  uint8_t)

#undef CK_PR_CAS_S
#undef CK_PR_CAS

/*
 * Compare and swap, set *v to old value of target.
 */
CK_CC_INLINE static bool
ck_pr_cas_ptr_value(void *target, void *compare, void *set, void *v)
{
	set = __sync_val_compare_and_swap((void **)target, compare, set);
	*(void **)v = set;
	return (set == compare);
}

#define CK_PR_CAS_O(S, T)						\
	CK_CC_INLINE static bool					\
	ck_pr_cas_##S##_value(T *target, T compare, T set, T *v)	\
	{								\
		set = __sync_val_compare_and_swap(target, compare, set);\
		*v = set;						\
		return (set == compare);				\
	}

CK_PR_CAS_O(char, char)
CK_PR_CAS_O(int, int)
CK_PR_CAS_O(uint, unsigned int)
CK_PR_CAS_O(64, uint64_t)
CK_PR_CAS_O(32, uint32_t)
CK_PR_CAS_O(16, uint16_t)
CK_PR_CAS_O(8,  uint8_t)

#undef CK_PR_CAS_O

/*
 * Atomic fetch-and-add operations.
 */
#define CK_PR_FAA(S, M, T)					\
	CK_CC_INLINE static T					\
	ck_pr_faa_##S(M *target, T d)				\
	{							\
		d = __sync_fetch_and_add((T *)target, d);	\
		return (d);					\
	}

CK_PR_FAA(ptr, void, void *)

#define CK_PR_FAA_S(S, T) CK_PR_FAA(S, T, T)

CK_PR_FAA_S(char, char)
CK_PR_FAA_S(uint, unsigned int)
CK_PR_FAA_S(int, int)
CK_PR_FAA_S(64, uint64_t)
CK_PR_FAA_S(32, uint32_t)
CK_PR_FAA_S(16, uint16_t)
CK_PR_FAA_S(8,  uint8_t)

#undef CK_PR_FAA_S
#undef CK_PR_FAA

/*
 * Atomic store-only binary operations.
 */
#define CK_PR_BINARY(K, S, M, T)				\
	CK_CC_INLINE static void				\
	ck_pr_##K##_##S(M *target, T d)				\
	{							\
		d = __sync_fetch_and_##K((T *)target, d);	\
		return;						\
	}

#define CK_PR_BINARY_S(K, S, T) CK_PR_BINARY(K, S, T, T)

#define CK_PR_GENERATE(K)			\
	CK_PR_BINARY(K, ptr, void, void *)	\
	CK_PR_BINARY_S(K, char, char)		\
	CK_PR_BINARY_S(K, int, int)		\
	CK_PR_BINARY_S(K, uint, unsigned int)	\
	CK_PR_BINARY_S(K, 64, uint64_t)		\
	CK_PR_BINARY_S(K, 32, uint32_t)		\
	CK_PR_BINARY_S(K, 16, uint16_t)		\
	CK_PR_BINARY_S(K, 8, uint8_t)

CK_PR_GENERATE(add)
CK_PR_GENERATE(sub)
CK_PR_GENERATE(and)
CK_PR_GENERATE(or)
CK_PR_GENERATE(xor)

#undef CK_PR_GENERATE
#undef CK_PR_BINARY_S
#undef CK_PR_BINARY

#define CK_PR_UNARY(S, M, T)			\
	CK_CC_INLINE static void		\
	ck_pr_inc_##S(M *target)		\
	{					\
		ck_pr_add_##S(target, (T)1);	\
		return;				\
	}					\
	CK_CC_INLINE static void		\
	ck_pr_dec_##S(M *target)		\
	{					\
		ck_pr_sub_##S(target, (T)1);	\
		return;				\
	}

#define CK_PR_UNARY_S(S, M) CK_PR_UNARY(S, M, M)

CK_PR_UNARY(ptr, void, void *)
CK_PR_UNARY_S(char, char)
CK_PR_UNARY_S(int, int)
CK_PR_UNARY_S(uint, unsigned int)
CK_PR_UNARY_S(64, uint64_t)
CK_PR_UNARY_S(32, uint32_t)
CK_PR_UNARY_S(16, uint16_t)
CK_PR_UNARY_S(8, uint8_t)

#undef CK_PR_UNARY_S
#undef CK_PR_UNARY
#endif /* !CK_F_PR */
#endif /* CK_PR_GCC_H */
