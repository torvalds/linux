/*
 * Copyright 2009-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
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

#ifndef CK_PR_H
#define CK_PR_H

#include <ck_cc.h>
#include <ck_limits.h>
#include <ck_md.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>

#ifndef CK_USE_CC_BUILTINS
#if defined(__x86_64__)
#include "gcc/x86_64/ck_pr.h"
#elif defined(__x86__)
#include "gcc/x86/ck_pr.h"
#elif defined(__sparcv9__)
#include "gcc/sparcv9/ck_pr.h"
#elif defined(__ppc64__)
#include "gcc/ppc64/ck_pr.h"
#elif defined(__s390x__)
#include "gcc/s390x/ck_pr.h"
#elif defined(__ppc__)
#include "gcc/ppc/ck_pr.h"
#elif defined(__arm__)
#if __ARM_ARCH >= 6
#include "gcc/arm/ck_pr.h"
#else
#include "gcc/arm/ck_pr_armv4.h"
#endif
#elif defined(__aarch64__)
#include "gcc/aarch64/ck_pr.h"
#elif !defined(__GNUC__)
#error Your platform is unsupported
#endif
#endif /* !CK_USE_CC_BUILTINS */

#if defined(__GNUC__)
#include "gcc/ck_pr.h"
#endif

#define CK_PR_FENCE_EMIT(T)			\
	CK_CC_INLINE static void		\
	ck_pr_fence_##T(void)			\
	{					\
		ck_pr_fence_strict_##T();	\
		return;				\
	}
#define CK_PR_FENCE_NOOP(T)			\
	CK_CC_INLINE static void		\
	ck_pr_fence_##T(void)			\
	{					\
		ck_pr_barrier();		\
		return;				\
	}

/*
 * None of the currently supported platforms allow for data-dependent
 * load ordering.
 */
CK_PR_FENCE_NOOP(load_depends)
#define ck_pr_fence_strict_load_depends ck_pr_fence_load_depends

/*
 * In memory models where atomic operations do not have serializing
 * effects, atomic read-modify-write operations are modeled as stores.
 */
#if defined(CK_MD_RMO)
/*
 * Only stores to the same location have a global
 * ordering.
 */
CK_PR_FENCE_EMIT(atomic)
CK_PR_FENCE_EMIT(atomic_load)
CK_PR_FENCE_EMIT(atomic_store)
CK_PR_FENCE_EMIT(store_atomic)
CK_PR_FENCE_EMIT(load_atomic)
CK_PR_FENCE_EMIT(load_store)
CK_PR_FENCE_EMIT(store_load)
CK_PR_FENCE_EMIT(load)
CK_PR_FENCE_EMIT(store)
CK_PR_FENCE_EMIT(memory)
CK_PR_FENCE_EMIT(acquire)
CK_PR_FENCE_EMIT(release)
CK_PR_FENCE_EMIT(acqrel)
CK_PR_FENCE_EMIT(lock)
CK_PR_FENCE_EMIT(unlock)
#elif defined(CK_MD_PSO)
/*
 * Anything can be re-ordered with respect to stores.
 * Otherwise, loads are executed in-order.
 */
CK_PR_FENCE_EMIT(atomic)
CK_PR_FENCE_NOOP(atomic_load)
CK_PR_FENCE_EMIT(atomic_store)
CK_PR_FENCE_EMIT(store_atomic)
CK_PR_FENCE_NOOP(load_atomic)
CK_PR_FENCE_EMIT(load_store)
CK_PR_FENCE_EMIT(store_load)
CK_PR_FENCE_NOOP(load)
CK_PR_FENCE_EMIT(store)
CK_PR_FENCE_EMIT(memory)
CK_PR_FENCE_EMIT(acquire)
CK_PR_FENCE_EMIT(release)
CK_PR_FENCE_EMIT(acqrel)
CK_PR_FENCE_EMIT(lock)
CK_PR_FENCE_EMIT(unlock)
#elif defined(CK_MD_TSO)
/*
 * Only loads are re-ordered and only with respect to
 * prior stores. Atomic operations are serializing.
 */
CK_PR_FENCE_NOOP(atomic)
CK_PR_FENCE_NOOP(atomic_load)
CK_PR_FENCE_NOOP(atomic_store)
CK_PR_FENCE_NOOP(store_atomic)
CK_PR_FENCE_NOOP(load_atomic)
CK_PR_FENCE_NOOP(load_store)
CK_PR_FENCE_EMIT(store_load)
CK_PR_FENCE_NOOP(load)
CK_PR_FENCE_NOOP(store)
CK_PR_FENCE_EMIT(memory)
CK_PR_FENCE_NOOP(acquire)
CK_PR_FENCE_NOOP(release)
CK_PR_FENCE_NOOP(acqrel)
CK_PR_FENCE_NOOP(lock)
CK_PR_FENCE_NOOP(unlock)
#else
#error "No memory model has been defined."
#endif /* CK_MD_TSO */

#undef CK_PR_FENCE_EMIT
#undef CK_PR_FENCE_NOOP

#ifndef CK_F_PR_RFO
#define CK_F_PR_RFO
CK_CC_INLINE static void
ck_pr_rfo(const void *m)
{

	(void)m;
	return;
}
#endif /* CK_F_PR_RFO */

#define CK_PR_STORE_SAFE(DST, VAL, TYPE)			\
    ck_pr_md_store_##TYPE(					\
        ((void)sizeof(*(DST) = (VAL)), (DST)),			\
        (VAL))

#define ck_pr_store_ptr(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), ptr)
#define ck_pr_store_char(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), char)
#ifndef CK_PR_DISABLE_DOUBLE
#define ck_pr_store_double(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), double)
#endif
#define ck_pr_store_uint(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), uint)
#define ck_pr_store_int(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), int)
#define ck_pr_store_32(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), 32)
#define ck_pr_store_16(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), 16)
#define ck_pr_store_8(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), 8)

#define ck_pr_store_ptr_unsafe(DST, VAL) ck_pr_md_store_ptr((DST), (VAL))

#ifdef CK_F_PR_LOAD_64
#define ck_pr_store_64(DST, VAL) CK_PR_STORE_SAFE((DST), (VAL), 64)
#endif /* CK_F_PR_LOAD_64 */

#define CK_PR_LOAD_PTR_SAFE(SRC) (CK_CC_TYPEOF(*(SRC), (void *)))ck_pr_md_load_ptr((SRC))
#define ck_pr_load_ptr(SRC) CK_PR_LOAD_PTR_SAFE((SRC))

#define CK_PR_LOAD_SAFE(SRC, TYPE) ck_pr_md_load_##TYPE((SRC))
#define ck_pr_load_char(SRC) CK_PR_LOAD_SAFE((SRC), char)
#ifndef CK_PR_DISABLE_DOUBLE
#define ck_pr_load_double(SRC) CK_PR_LOAD_SAFE((SRC), double)
#endif
#define ck_pr_load_uint(SRC) CK_PR_LOAD_SAFE((SRC), uint)
#define ck_pr_load_int(SRC) CK_PR_LOAD_SAFE((SRC), int)
#define ck_pr_load_32(SRC) CK_PR_LOAD_SAFE((SRC), 32)
#define ck_pr_load_16(SRC) CK_PR_LOAD_SAFE((SRC), 16)
#define ck_pr_load_8(SRC) CK_PR_LOAD_SAFE((SRC), 8)

#ifdef CK_F_PR_LOAD_64
#define ck_pr_load_64(SRC) CK_PR_LOAD_SAFE((SRC), 64)
#endif /* CK_F_PR_LOAD_64 */

#define CK_PR_BIN(K, S, M, T, P, C)					\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S(M *target, T value)				\
	{								\
		T previous;						\
		C punt;							\
		punt = ck_pr_md_load_##S(target);			\
		previous = (T)punt;					\
		while (ck_pr_cas_##S##_value(target,			\
					     (C)previous,		\
					     (C)(previous P value),	\
					     &previous) == false)	\
			ck_pr_stall();					\
									\
		return;							\
	}

#define CK_PR_BIN_S(K, S, T, P) CK_PR_BIN(K, S, T, T, P, T)

#if defined(CK_F_PR_LOAD_CHAR) && defined(CK_F_PR_CAS_CHAR_VALUE)

#ifndef CK_F_PR_ADD_CHAR
#define CK_F_PR_ADD_CHAR
CK_PR_BIN_S(add, char, char, +)
#endif /* CK_F_PR_ADD_CHAR */

#ifndef CK_F_PR_SUB_CHAR
#define CK_F_PR_SUB_CHAR
CK_PR_BIN_S(sub, char, char, -)
#endif /* CK_F_PR_SUB_CHAR */

#ifndef CK_F_PR_AND_CHAR
#define CK_F_PR_AND_CHAR
CK_PR_BIN_S(and, char, char, &)
#endif /* CK_F_PR_AND_CHAR */

#ifndef CK_F_PR_XOR_CHAR
#define CK_F_PR_XOR_CHAR
CK_PR_BIN_S(xor, char, char, ^)
#endif /* CK_F_PR_XOR_CHAR */

#ifndef CK_F_PR_OR_CHAR
#define CK_F_PR_OR_CHAR
CK_PR_BIN_S(or, char, char, |)
#endif /* CK_F_PR_OR_CHAR */

#endif /* CK_F_PR_LOAD_CHAR && CK_F_PR_CAS_CHAR_VALUE */

#if defined(CK_F_PR_LOAD_INT) && defined(CK_F_PR_CAS_INT_VALUE)

#ifndef CK_F_PR_ADD_INT
#define CK_F_PR_ADD_INT
CK_PR_BIN_S(add, int, int, +)
#endif /* CK_F_PR_ADD_INT */

#ifndef CK_F_PR_SUB_INT
#define CK_F_PR_SUB_INT
CK_PR_BIN_S(sub, int, int, -)
#endif /* CK_F_PR_SUB_INT */

#ifndef CK_F_PR_AND_INT
#define CK_F_PR_AND_INT
CK_PR_BIN_S(and, int, int, &)
#endif /* CK_F_PR_AND_INT */

#ifndef CK_F_PR_XOR_INT
#define CK_F_PR_XOR_INT
CK_PR_BIN_S(xor, int, int, ^)
#endif /* CK_F_PR_XOR_INT */

#ifndef CK_F_PR_OR_INT
#define CK_F_PR_OR_INT
CK_PR_BIN_S(or, int, int, |)
#endif /* CK_F_PR_OR_INT */

#endif /* CK_F_PR_LOAD_INT && CK_F_PR_CAS_INT_VALUE */

#if defined(CK_F_PR_LOAD_DOUBLE) && defined(CK_F_PR_CAS_DOUBLE_VALUE) && \
	    !defined(CK_PR_DISABLE_DOUBLE)

#ifndef CK_F_PR_ADD_DOUBLE
#define CK_F_PR_ADD_DOUBLE
CK_PR_BIN_S(add, double, double, +)
#endif /* CK_F_PR_ADD_DOUBLE */

#ifndef CK_F_PR_SUB_DOUBLE
#define CK_F_PR_SUB_DOUBLE
CK_PR_BIN_S(sub, double, double, -)
#endif /* CK_F_PR_SUB_DOUBLE */

#endif /* CK_F_PR_LOAD_DOUBLE && CK_F_PR_CAS_DOUBLE_VALUE && !CK_PR_DISABLE_DOUBLE */

#if defined(CK_F_PR_LOAD_UINT) && defined(CK_F_PR_CAS_UINT_VALUE)

#ifndef CK_F_PR_ADD_UINT
#define CK_F_PR_ADD_UINT
CK_PR_BIN_S(add, uint, unsigned int, +)
#endif /* CK_F_PR_ADD_UINT */

#ifndef CK_F_PR_SUB_UINT
#define CK_F_PR_SUB_UINT
CK_PR_BIN_S(sub, uint, unsigned int, -)
#endif /* CK_F_PR_SUB_UINT */

#ifndef CK_F_PR_AND_UINT
#define CK_F_PR_AND_UINT
CK_PR_BIN_S(and, uint, unsigned int, &)
#endif /* CK_F_PR_AND_UINT */

#ifndef CK_F_PR_XOR_UINT
#define CK_F_PR_XOR_UINT
CK_PR_BIN_S(xor, uint, unsigned int, ^)
#endif /* CK_F_PR_XOR_UINT */

#ifndef CK_F_PR_OR_UINT
#define CK_F_PR_OR_UINT
CK_PR_BIN_S(or, uint, unsigned int, |)
#endif /* CK_F_PR_OR_UINT */

#endif /* CK_F_PR_LOAD_UINT && CK_F_PR_CAS_UINT_VALUE */

#if defined(CK_F_PR_LOAD_PTR) && defined(CK_F_PR_CAS_PTR_VALUE)

#ifndef CK_F_PR_ADD_PTR
#define CK_F_PR_ADD_PTR
CK_PR_BIN(add, ptr, void, uintptr_t, +, void *)
#endif /* CK_F_PR_ADD_PTR */

#ifndef CK_F_PR_SUB_PTR
#define CK_F_PR_SUB_PTR
CK_PR_BIN(sub, ptr, void, uintptr_t, -, void *)
#endif /* CK_F_PR_SUB_PTR */

#ifndef CK_F_PR_AND_PTR
#define CK_F_PR_AND_PTR
CK_PR_BIN(and, ptr, void, uintptr_t, &, void *)
#endif /* CK_F_PR_AND_PTR */

#ifndef CK_F_PR_XOR_PTR
#define CK_F_PR_XOR_PTR
CK_PR_BIN(xor, ptr, void, uintptr_t, ^, void *)
#endif /* CK_F_PR_XOR_PTR */

#ifndef CK_F_PR_OR_PTR
#define CK_F_PR_OR_PTR
CK_PR_BIN(or, ptr, void, uintptr_t, |, void *)
#endif /* CK_F_PR_OR_PTR */

#endif /* CK_F_PR_LOAD_PTR && CK_F_PR_CAS_PTR_VALUE */

#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_CAS_64_VALUE)

#ifndef CK_F_PR_ADD_64
#define CK_F_PR_ADD_64
CK_PR_BIN_S(add, 64, uint64_t, +)
#endif /* CK_F_PR_ADD_64 */

#ifndef CK_F_PR_SUB_64
#define CK_F_PR_SUB_64
CK_PR_BIN_S(sub, 64, uint64_t, -)
#endif /* CK_F_PR_SUB_64 */

#ifndef CK_F_PR_AND_64
#define CK_F_PR_AND_64
CK_PR_BIN_S(and, 64, uint64_t, &)
#endif /* CK_F_PR_AND_64 */

#ifndef CK_F_PR_XOR_64
#define CK_F_PR_XOR_64
CK_PR_BIN_S(xor, 64, uint64_t, ^)
#endif /* CK_F_PR_XOR_64 */

#ifndef CK_F_PR_OR_64
#define CK_F_PR_OR_64
CK_PR_BIN_S(or, 64, uint64_t, |)
#endif /* CK_F_PR_OR_64 */

#endif /* CK_F_PR_LOAD_64 && CK_F_PR_CAS_64_VALUE */

#if defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_CAS_32_VALUE)

#ifndef CK_F_PR_ADD_32
#define CK_F_PR_ADD_32
CK_PR_BIN_S(add, 32, uint32_t, +)
#endif /* CK_F_PR_ADD_32 */

#ifndef CK_F_PR_SUB_32
#define CK_F_PR_SUB_32
CK_PR_BIN_S(sub, 32, uint32_t, -)
#endif /* CK_F_PR_SUB_32 */

#ifndef CK_F_PR_AND_32
#define CK_F_PR_AND_32
CK_PR_BIN_S(and, 32, uint32_t, &)
#endif /* CK_F_PR_AND_32 */

#ifndef CK_F_PR_XOR_32
#define CK_F_PR_XOR_32
CK_PR_BIN_S(xor, 32, uint32_t, ^)
#endif /* CK_F_PR_XOR_32 */

#ifndef CK_F_PR_OR_32
#define CK_F_PR_OR_32
CK_PR_BIN_S(or, 32, uint32_t, |)
#endif /* CK_F_PR_OR_32 */

#endif /* CK_F_PR_LOAD_32 && CK_F_PR_CAS_32_VALUE */

#if defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_CAS_16_VALUE)

#ifndef CK_F_PR_ADD_16
#define CK_F_PR_ADD_16
CK_PR_BIN_S(add, 16, uint16_t, +)
#endif /* CK_F_PR_ADD_16 */

#ifndef CK_F_PR_SUB_16
#define CK_F_PR_SUB_16
CK_PR_BIN_S(sub, 16, uint16_t, -)
#endif /* CK_F_PR_SUB_16 */

#ifndef CK_F_PR_AND_16
#define CK_F_PR_AND_16
CK_PR_BIN_S(and, 16, uint16_t, &)
#endif /* CK_F_PR_AND_16 */

#ifndef CK_F_PR_XOR_16
#define CK_F_PR_XOR_16
CK_PR_BIN_S(xor, 16, uint16_t, ^)
#endif /* CK_F_PR_XOR_16 */

#ifndef CK_F_PR_OR_16
#define CK_F_PR_OR_16
CK_PR_BIN_S(or, 16, uint16_t, |)
#endif /* CK_F_PR_OR_16 */

#endif /* CK_F_PR_LOAD_16 && CK_F_PR_CAS_16_VALUE */

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_CAS_8_VALUE)

#ifndef CK_F_PR_ADD_8
#define CK_F_PR_ADD_8
CK_PR_BIN_S(add, 8, uint8_t, +)
#endif /* CK_F_PR_ADD_8 */

#ifndef CK_F_PR_SUB_8
#define CK_F_PR_SUB_8
CK_PR_BIN_S(sub, 8, uint8_t, -)
#endif /* CK_F_PR_SUB_8 */

#ifndef CK_F_PR_AND_8
#define CK_F_PR_AND_8
CK_PR_BIN_S(and, 8, uint8_t, &)
#endif /* CK_F_PR_AND_8 */

#ifndef CK_F_PR_XOR_8
#define CK_F_PR_XOR_8
CK_PR_BIN_S(xor, 8, uint8_t, ^)
#endif /* CK_F_PR_XOR_8 */

#ifndef CK_F_PR_OR_8
#define CK_F_PR_OR_8
CK_PR_BIN_S(or, 8, uint8_t, |)
#endif /* CK_F_PR_OR_8 */

#endif /* CK_F_PR_LOAD_8 && CK_F_PR_CAS_8_VALUE */

#undef CK_PR_BIN_S
#undef CK_PR_BIN

#define CK_PR_BTX(K, S, M, T, P, C, R)						   \
	CK_CC_INLINE static bool						   \
	ck_pr_##K##_##S(M *target, unsigned int offset)				   \
	{									   \
		T previous;							   \
		C punt;								   \
		punt = ck_pr_md_load_##S(target);				   \
		previous = (T)punt;						   \
		while (ck_pr_cas_##S##_value(target, (C)previous,		   \
			(C)(previous P (R ((T)1 << offset))), &previous) == false) \
				ck_pr_stall();					   \
		return ((previous >> offset) & 1);				   \
	}

#define CK_PR_BTX_S(K, S, T, P, R) CK_PR_BTX(K, S, T, T, P, T, R)

#if defined(CK_F_PR_LOAD_INT) && defined(CK_F_PR_CAS_INT_VALUE)

#ifndef CK_F_PR_BTC_INT
#define CK_F_PR_BTC_INT
CK_PR_BTX_S(btc, int, int, ^,)
#endif /* CK_F_PR_BTC_INT */

#ifndef CK_F_PR_BTR_INT
#define CK_F_PR_BTR_INT
CK_PR_BTX_S(btr, int, int, &, ~)
#endif /* CK_F_PR_BTR_INT */

#ifndef CK_F_PR_BTS_INT
#define CK_F_PR_BTS_INT
CK_PR_BTX_S(bts, int, int, |,)
#endif /* CK_F_PR_BTS_INT */

#endif /* CK_F_PR_LOAD_INT && CK_F_PR_CAS_INT_VALUE */

#if defined(CK_F_PR_LOAD_UINT) && defined(CK_F_PR_CAS_UINT_VALUE)

#ifndef CK_F_PR_BTC_UINT
#define CK_F_PR_BTC_UINT
CK_PR_BTX_S(btc, uint, unsigned int, ^,)
#endif /* CK_F_PR_BTC_UINT */

#ifndef CK_F_PR_BTR_UINT
#define CK_F_PR_BTR_UINT
CK_PR_BTX_S(btr, uint, unsigned int, &, ~)
#endif /* CK_F_PR_BTR_UINT */

#ifndef CK_F_PR_BTS_UINT
#define CK_F_PR_BTS_UINT
CK_PR_BTX_S(bts, uint, unsigned int, |,)
#endif /* CK_F_PR_BTS_UINT */

#endif /* CK_F_PR_LOAD_UINT && CK_F_PR_CAS_UINT_VALUE */

#if defined(CK_F_PR_LOAD_PTR) && defined(CK_F_PR_CAS_PTR_VALUE)

#ifndef CK_F_PR_BTC_PTR
#define CK_F_PR_BTC_PTR
CK_PR_BTX(btc, ptr, void, uintptr_t, ^, void *,)
#endif /* CK_F_PR_BTC_PTR */

#ifndef CK_F_PR_BTR_PTR
#define CK_F_PR_BTR_PTR
CK_PR_BTX(btr, ptr, void, uintptr_t, &, void *, ~)
#endif /* CK_F_PR_BTR_PTR */

#ifndef CK_F_PR_BTS_PTR
#define CK_F_PR_BTS_PTR
CK_PR_BTX(bts, ptr, void, uintptr_t, |, void *,)
#endif /* CK_F_PR_BTS_PTR */

#endif /* CK_F_PR_LOAD_PTR && CK_F_PR_CAS_PTR_VALUE */

#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_CAS_64_VALUE)

#ifndef CK_F_PR_BTC_64
#define CK_F_PR_BTC_64
CK_PR_BTX_S(btc, 64, uint64_t, ^,)
#endif /* CK_F_PR_BTC_64 */

#ifndef CK_F_PR_BTR_64
#define CK_F_PR_BTR_64
CK_PR_BTX_S(btr, 64, uint64_t, &, ~)
#endif /* CK_F_PR_BTR_64 */

#ifndef CK_F_PR_BTS_64
#define CK_F_PR_BTS_64
CK_PR_BTX_S(bts, 64, uint64_t, |,)
#endif /* CK_F_PR_BTS_64 */

#endif /* CK_F_PR_LOAD_64 && CK_F_PR_CAS_64_VALUE */

#if defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_CAS_32_VALUE)

#ifndef CK_F_PR_BTC_32
#define CK_F_PR_BTC_32
CK_PR_BTX_S(btc, 32, uint32_t, ^,)
#endif /* CK_F_PR_BTC_32 */

#ifndef CK_F_PR_BTR_32
#define CK_F_PR_BTR_32
CK_PR_BTX_S(btr, 32, uint32_t, &, ~)
#endif /* CK_F_PR_BTR_32 */

#ifndef CK_F_PR_BTS_32
#define CK_F_PR_BTS_32
CK_PR_BTX_S(bts, 32, uint32_t, |,)
#endif /* CK_F_PR_BTS_32 */

#endif /* CK_F_PR_LOAD_32 && CK_F_PR_CAS_32_VALUE */

#if defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_CAS_16_VALUE)

#ifndef CK_F_PR_BTC_16
#define CK_F_PR_BTC_16
CK_PR_BTX_S(btc, 16, uint16_t, ^,)
#endif /* CK_F_PR_BTC_16 */

#ifndef CK_F_PR_BTR_16
#define CK_F_PR_BTR_16
CK_PR_BTX_S(btr, 16, uint16_t, &, ~)
#endif /* CK_F_PR_BTR_16 */

#ifndef CK_F_PR_BTS_16
#define CK_F_PR_BTS_16
CK_PR_BTX_S(bts, 16, uint16_t, |,)
#endif /* CK_F_PR_BTS_16 */

#endif /* CK_F_PR_LOAD_16 && CK_F_PR_CAS_16_VALUE */

#undef CK_PR_BTX_S
#undef CK_PR_BTX

#define CK_PR_UNARY(K, X, S, M, T)					\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S(M *target)					\
	{								\
		ck_pr_##X##_##S(target, (T)1);				\
		return;							\
	}

#define CK_PR_UNARY_Z(K, S, M, T, P, C, Z)				\
	CK_CC_INLINE static bool					\
	ck_pr_##K##_##S##_is_zero(M *target)				\
	{								\
		T previous;						\
		C punt;							\
		punt = (C)ck_pr_md_load_##S(target);			\
		previous = (T)punt;					\
		while (ck_pr_cas_##S##_value(target,			\
					     (C)previous,		\
					     (C)(previous P 1),		\
					     &previous) == false)	\
			ck_pr_stall();					\
		return previous == (T)Z;				\
        }

#define CK_PR_UNARY_Z_STUB(K, S, M)					\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S##_zero(M *target, bool *zero)			\
	{								\
		*zero = ck_pr_##K##_##S##_is_zero(target);		\
		return;							\
	}

#define CK_PR_UNARY_S(K, X, S, M) CK_PR_UNARY(K, X, S, M, M)
#define CK_PR_UNARY_Z_S(K, S, M, P, Z)          \
        CK_PR_UNARY_Z(K, S, M, M, P, M, Z)      \
        CK_PR_UNARY_Z_STUB(K, S, M)

#if defined(CK_F_PR_LOAD_CHAR) && defined(CK_F_PR_CAS_CHAR_VALUE)

#ifndef CK_F_PR_INC_CHAR
#define CK_F_PR_INC_CHAR
CK_PR_UNARY_S(inc, add, char, char)
#endif /* CK_F_PR_INC_CHAR */

#ifndef CK_F_PR_INC_CHAR_ZERO
#define CK_F_PR_INC_CHAR_ZERO
CK_PR_UNARY_Z_S(inc, char, char, +, -1)
#else
CK_PR_UNARY_Z_STUB(inc, char, char)
#endif /* CK_F_PR_INC_CHAR_ZERO */

#ifndef CK_F_PR_DEC_CHAR
#define CK_F_PR_DEC_CHAR
CK_PR_UNARY_S(dec, sub, char, char)
#endif /* CK_F_PR_DEC_CHAR */

#ifndef CK_F_PR_DEC_CHAR_ZERO
#define CK_F_PR_DEC_CHAR_ZERO
CK_PR_UNARY_Z_S(dec, char, char, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, char, char)
#endif /* CK_F_PR_DEC_CHAR_ZERO */

#endif /* CK_F_PR_LOAD_CHAR && CK_F_PR_CAS_CHAR_VALUE */

#if defined(CK_F_PR_LOAD_INT) && defined(CK_F_PR_CAS_INT_VALUE)

#ifndef CK_F_PR_INC_INT
#define CK_F_PR_INC_INT
CK_PR_UNARY_S(inc, add, int, int)
#endif /* CK_F_PR_INC_INT */

#ifndef CK_F_PR_INC_INT_ZERO
#define CK_F_PR_INC_INT_ZERO
CK_PR_UNARY_Z_S(inc, int, int, +, -1)
#else
CK_PR_UNARY_Z_STUB(inc, int, int)
#endif /* CK_F_PR_INC_INT_ZERO */

#ifndef CK_F_PR_DEC_INT
#define CK_F_PR_DEC_INT
CK_PR_UNARY_S(dec, sub, int, int)
#endif /* CK_F_PR_DEC_INT */

#ifndef CK_F_PR_DEC_INT_ZERO
#define CK_F_PR_DEC_INT_ZERO
CK_PR_UNARY_Z_S(dec, int, int, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, int, int)
#endif /* CK_F_PR_DEC_INT_ZERO */

#endif /* CK_F_PR_LOAD_INT && CK_F_PR_CAS_INT_VALUE */

#if defined(CK_F_PR_LOAD_DOUBLE) && defined(CK_F_PR_CAS_DOUBLE_VALUE) && \
	    !defined(CK_PR_DISABLE_DOUBLE)

#ifndef CK_F_PR_INC_DOUBLE
#define CK_F_PR_INC_DOUBLE
CK_PR_UNARY_S(inc, add, double, double)
#endif /* CK_F_PR_INC_DOUBLE */

#ifndef CK_F_PR_DEC_DOUBLE
#define CK_F_PR_DEC_DOUBLE
CK_PR_UNARY_S(dec, sub, double, double)
#endif /* CK_F_PR_DEC_DOUBLE */

#endif /* CK_F_PR_LOAD_DOUBLE && CK_F_PR_CAS_DOUBLE_VALUE && !CK_PR_DISABLE_DOUBLE */

#if defined(CK_F_PR_LOAD_UINT) && defined(CK_F_PR_CAS_UINT_VALUE)

#ifndef CK_F_PR_INC_UINT
#define CK_F_PR_INC_UINT
CK_PR_UNARY_S(inc, add, uint, unsigned int)
#endif /* CK_F_PR_INC_UINT */

#ifndef CK_F_PR_INC_UINT_ZERO
#define CK_F_PR_INC_UINT_ZERO
CK_PR_UNARY_Z_S(inc, uint, unsigned int, +, UINT_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, uint, unsigned int)
#endif /* CK_F_PR_INC_UINT_ZERO */

#ifndef CK_F_PR_DEC_UINT
#define CK_F_PR_DEC_UINT
CK_PR_UNARY_S(dec, sub, uint, unsigned int)
#endif /* CK_F_PR_DEC_UINT */

#ifndef CK_F_PR_DEC_UINT_ZERO
#define CK_F_PR_DEC_UINT_ZERO
CK_PR_UNARY_Z_S(dec, uint, unsigned int, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, uint, unsigned int)
#endif /* CK_F_PR_DEC_UINT_ZERO */

#endif /* CK_F_PR_LOAD_UINT && CK_F_PR_CAS_UINT_VALUE */

#if defined(CK_F_PR_LOAD_PTR) && defined(CK_F_PR_CAS_PTR_VALUE)

#ifndef CK_F_PR_INC_PTR
#define CK_F_PR_INC_PTR
CK_PR_UNARY(inc, add, ptr, void, uintptr_t)
#endif /* CK_F_PR_INC_PTR */

#ifndef CK_F_PR_INC_PTR_ZERO
#define CK_F_PR_INC_PTR_ZERO
CK_PR_UNARY_Z(inc, ptr, void, uintptr_t, +, void *, UINT_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, ptr, void)
#endif /* CK_F_PR_INC_PTR_ZERO */

#ifndef CK_F_PR_DEC_PTR
#define CK_F_PR_DEC_PTR
CK_PR_UNARY(dec, sub, ptr, void, uintptr_t)
#endif /* CK_F_PR_DEC_PTR */

#ifndef CK_F_PR_DEC_PTR_ZERO
#define CK_F_PR_DEC_PTR_ZERO
CK_PR_UNARY_Z(dec, ptr, void, uintptr_t, -, void *, 1)
#else
CK_PR_UNARY_Z_STUB(dec, ptr, void)
#endif /* CK_F_PR_DEC_PTR_ZERO */

#endif /* CK_F_PR_LOAD_PTR && CK_F_PR_CAS_PTR_VALUE */

#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_CAS_64_VALUE)

#ifndef CK_F_PR_INC_64
#define CK_F_PR_INC_64
CK_PR_UNARY_S(inc, add, 64, uint64_t)
#endif /* CK_F_PR_INC_64 */

#ifndef CK_F_PR_INC_64_ZERO
#define CK_F_PR_INC_64_ZERO
CK_PR_UNARY_Z_S(inc, 64, uint64_t, +, UINT64_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, 64, uint64_t)
#endif /* CK_F_PR_INC_64_ZERO */

#ifndef CK_F_PR_DEC_64
#define CK_F_PR_DEC_64
CK_PR_UNARY_S(dec, sub, 64, uint64_t)
#endif /* CK_F_PR_DEC_64 */

#ifndef CK_F_PR_DEC_64_ZERO
#define CK_F_PR_DEC_64_ZERO
CK_PR_UNARY_Z_S(dec, 64, uint64_t, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, 64, uint64_t)
#endif /* CK_F_PR_DEC_64_ZERO */

#endif /* CK_F_PR_LOAD_64 && CK_F_PR_CAS_64_VALUE */

#if defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_CAS_32_VALUE)

#ifndef CK_F_PR_INC_32
#define CK_F_PR_INC_32
CK_PR_UNARY_S(inc, add, 32, uint32_t)
#endif /* CK_F_PR_INC_32 */

#ifndef CK_F_PR_INC_32_ZERO
#define CK_F_PR_INC_32_ZERO
CK_PR_UNARY_Z_S(inc, 32, uint32_t, +, UINT32_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, 32, uint32_t)
#endif /* CK_F_PR_INC_32_ZERO */

#ifndef CK_F_PR_DEC_32
#define CK_F_PR_DEC_32
CK_PR_UNARY_S(dec, sub, 32, uint32_t)
#endif /* CK_F_PR_DEC_32 */

#ifndef CK_F_PR_DEC_32_ZERO
#define CK_F_PR_DEC_32_ZERO
CK_PR_UNARY_Z_S(dec, 32, uint32_t, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, 32, uint32_t)
#endif /* CK_F_PR_DEC_32_ZERO */

#endif /* CK_F_PR_LOAD_32 && CK_F_PR_CAS_32_VALUE */

#if defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_CAS_16_VALUE)

#ifndef CK_F_PR_INC_16
#define CK_F_PR_INC_16
CK_PR_UNARY_S(inc, add, 16, uint16_t)
#endif /* CK_F_PR_INC_16 */

#ifndef CK_F_PR_INC_16_ZERO
#define CK_F_PR_INC_16_ZERO
CK_PR_UNARY_Z_S(inc, 16, uint16_t, +, UINT16_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, 16, uint16_t)
#endif /* CK_F_PR_INC_16_ZERO */

#ifndef CK_F_PR_DEC_16
#define CK_F_PR_DEC_16
CK_PR_UNARY_S(dec, sub, 16, uint16_t)
#endif /* CK_F_PR_DEC_16 */

#ifndef CK_F_PR_DEC_16_ZERO
#define CK_F_PR_DEC_16_ZERO
CK_PR_UNARY_Z_S(dec, 16, uint16_t, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, 16, uint16_t)
#endif /* CK_F_PR_DEC_16_ZERO */

#endif /* CK_F_PR_LOAD_16 && CK_F_PR_CAS_16_VALUE */

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_CAS_8_VALUE)

#ifndef CK_F_PR_INC_8
#define CK_F_PR_INC_8
CK_PR_UNARY_S(inc, add, 8, uint8_t)
#endif /* CK_F_PR_INC_8 */

#ifndef CK_F_PR_INC_8_ZERO
#define CK_F_PR_INC_8_ZERO
CK_PR_UNARY_Z_S(inc, 8, uint8_t, +, UINT8_MAX)
#else
CK_PR_UNARY_Z_STUB(inc, 8, uint8_t)
#endif /* CK_F_PR_INC_8_ZERO */

#ifndef CK_F_PR_DEC_8
#define CK_F_PR_DEC_8
CK_PR_UNARY_S(dec, sub, 8, uint8_t)
#endif /* CK_F_PR_DEC_8 */

#ifndef CK_F_PR_DEC_8_ZERO
#define CK_F_PR_DEC_8_ZERO
CK_PR_UNARY_Z_S(dec, 8, uint8_t, -, 1)
#else
CK_PR_UNARY_Z_STUB(dec, 8, uint8_t)
#endif /* CK_F_PR_DEC_8_ZERO */

#endif /* CK_F_PR_LOAD_8 && CK_F_PR_CAS_8_VALUE */

#undef CK_PR_UNARY_Z_S
#undef CK_PR_UNARY_S
#undef CK_PR_UNARY_Z
#undef CK_PR_UNARY

#define CK_PR_N(K, S, M, T, P, C)					\
	CK_CC_INLINE static void					\
	ck_pr_##K##_##S(M *target)					\
	{								\
		T previous;						\
		C punt;							\
		punt = (C)ck_pr_md_load_##S(target);			\
		previous = (T)punt;					\
		while (ck_pr_cas_##S##_value(target,			\
					     (C)previous,		\
					     (C)(P previous),		\
					     &previous) == false)	\
			ck_pr_stall();					\
									\
		return;							\
	}

#define CK_PR_N_Z(S, M, T, C)						\
	CK_CC_INLINE static void					\
	ck_pr_neg_##S##_zero(M *target, bool *zero)			\
	{								\
		T previous;						\
		C punt;							\
		punt = (C)ck_pr_md_load_##S(target);			\
		previous = (T)punt;					\
		while (ck_pr_cas_##S##_value(target,			\
					     (C)previous,		\
					     (C)(-previous),		\
					     &previous) == false)	\
			ck_pr_stall();					\
									\
		*zero = previous == 0;					\
		return;							\
	}

#define CK_PR_N_S(K, S, M, P)	CK_PR_N(K, S, M, M, P, M)
#define CK_PR_N_Z_S(S, M) 	CK_PR_N_Z(S, M, M, M)

#if defined(CK_F_PR_LOAD_CHAR) && defined(CK_F_PR_CAS_CHAR_VALUE)

#ifndef CK_F_PR_NOT_CHAR
#define CK_F_PR_NOT_CHAR
CK_PR_N_S(not, char, char, ~)
#endif /* CK_F_PR_NOT_CHAR */

#ifndef CK_F_PR_NEG_CHAR
#define CK_F_PR_NEG_CHAR
CK_PR_N_S(neg, char, char, -)
#endif /* CK_F_PR_NEG_CHAR */

#ifndef CK_F_PR_NEG_CHAR_ZERO
#define CK_F_PR_NEG_CHAR_ZERO
CK_PR_N_Z_S(char, char)
#endif /* CK_F_PR_NEG_CHAR_ZERO */

#endif /* CK_F_PR_LOAD_CHAR && CK_F_PR_CAS_CHAR_VALUE */

#if defined(CK_F_PR_LOAD_INT) && defined(CK_F_PR_CAS_INT_VALUE)

#ifndef CK_F_PR_NOT_INT
#define CK_F_PR_NOT_INT
CK_PR_N_S(not, int, int, ~)
#endif /* CK_F_PR_NOT_INT */

#ifndef CK_F_PR_NEG_INT
#define CK_F_PR_NEG_INT
CK_PR_N_S(neg, int, int, -)
#endif /* CK_F_PR_NEG_INT */

#ifndef CK_F_PR_NEG_INT_ZERO
#define CK_F_PR_NEG_INT_ZERO
CK_PR_N_Z_S(int, int)
#endif /* CK_F_PR_NEG_INT_ZERO */

#endif /* CK_F_PR_LOAD_INT && CK_F_PR_CAS_INT_VALUE */

#if defined(CK_F_PR_LOAD_DOUBLE) && defined(CK_F_PR_CAS_DOUBLE_VALUE) && \
	    !defined(CK_PR_DISABLE_DOUBLE)

#ifndef CK_F_PR_NEG_DOUBLE
#define CK_F_PR_NEG_DOUBLE
CK_PR_N_S(neg, double, double, -)
#endif /* CK_F_PR_NEG_DOUBLE */

#endif /* CK_F_PR_LOAD_DOUBLE && CK_F_PR_CAS_DOUBLE_VALUE && !CK_PR_DISABLE_DOUBLE */

#if defined(CK_F_PR_LOAD_UINT) && defined(CK_F_PR_CAS_UINT_VALUE)

#ifndef CK_F_PR_NOT_UINT
#define CK_F_PR_NOT_UINT
CK_PR_N_S(not, uint, unsigned int, ~)
#endif /* CK_F_PR_NOT_UINT */

#ifndef CK_F_PR_NEG_UINT
#define CK_F_PR_NEG_UINT
CK_PR_N_S(neg, uint, unsigned int, -)
#endif /* CK_F_PR_NEG_UINT */

#ifndef CK_F_PR_NEG_UINT_ZERO
#define CK_F_PR_NEG_UINT_ZERO
CK_PR_N_Z_S(uint, unsigned int)
#endif /* CK_F_PR_NEG_UINT_ZERO */

#endif /* CK_F_PR_LOAD_UINT && CK_F_PR_CAS_UINT_VALUE */

#if defined(CK_F_PR_LOAD_PTR) && defined(CK_F_PR_CAS_PTR_VALUE)

#ifndef CK_F_PR_NOT_PTR
#define CK_F_PR_NOT_PTR
CK_PR_N(not, ptr, void, uintptr_t, ~, void *)
#endif /* CK_F_PR_NOT_PTR */

#ifndef CK_F_PR_NEG_PTR
#define CK_F_PR_NEG_PTR
CK_PR_N(neg, ptr, void, uintptr_t, -, void *)
#endif /* CK_F_PR_NEG_PTR */

#ifndef CK_F_PR_NEG_PTR_ZERO
#define CK_F_PR_NEG_PTR_ZERO
CK_PR_N_Z(ptr, void, uintptr_t, void *)
#endif /* CK_F_PR_NEG_PTR_ZERO */

#endif /* CK_F_PR_LOAD_PTR && CK_F_PR_CAS_PTR_VALUE */

#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_CAS_64_VALUE)

#ifndef CK_F_PR_NOT_64
#define CK_F_PR_NOT_64
CK_PR_N_S(not, 64, uint64_t, ~)
#endif /* CK_F_PR_NOT_64 */

#ifndef CK_F_PR_NEG_64
#define CK_F_PR_NEG_64
CK_PR_N_S(neg, 64, uint64_t, -)
#endif /* CK_F_PR_NEG_64 */

#ifndef CK_F_PR_NEG_64_ZERO
#define CK_F_PR_NEG_64_ZERO
CK_PR_N_Z_S(64, uint64_t)
#endif /* CK_F_PR_NEG_64_ZERO */

#endif /* CK_F_PR_LOAD_64 && CK_F_PR_CAS_64_VALUE */

#if defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_CAS_32_VALUE)

#ifndef CK_F_PR_NOT_32
#define CK_F_PR_NOT_32
CK_PR_N_S(not, 32, uint32_t, ~)
#endif /* CK_F_PR_NOT_32 */

#ifndef CK_F_PR_NEG_32
#define CK_F_PR_NEG_32
CK_PR_N_S(neg, 32, uint32_t, -)
#endif /* CK_F_PR_NEG_32 */

#ifndef CK_F_PR_NEG_32_ZERO
#define CK_F_PR_NEG_32_ZERO
CK_PR_N_Z_S(32, uint32_t)
#endif /* CK_F_PR_NEG_32_ZERO */

#endif /* CK_F_PR_LOAD_32 && CK_F_PR_CAS_32_VALUE */

#if defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_CAS_16_VALUE)

#ifndef CK_F_PR_NOT_16
#define CK_F_PR_NOT_16
CK_PR_N_S(not, 16, uint16_t, ~)
#endif /* CK_F_PR_NOT_16 */

#ifndef CK_F_PR_NEG_16
#define CK_F_PR_NEG_16
CK_PR_N_S(neg, 16, uint16_t, -)
#endif /* CK_F_PR_NEG_16 */

#ifndef CK_F_PR_NEG_16_ZERO
#define CK_F_PR_NEG_16_ZERO
CK_PR_N_Z_S(16, uint16_t)
#endif /* CK_F_PR_NEG_16_ZERO */

#endif /* CK_F_PR_LOAD_16 && CK_F_PR_CAS_16_VALUE */

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_CAS_8_VALUE)

#ifndef CK_F_PR_NOT_8
#define CK_F_PR_NOT_8
CK_PR_N_S(not, 8, uint8_t, ~)
#endif /* CK_F_PR_NOT_8 */

#ifndef CK_F_PR_NEG_8
#define CK_F_PR_NEG_8
CK_PR_N_S(neg, 8, uint8_t, -)
#endif /* CK_F_PR_NEG_8 */

#ifndef CK_F_PR_NEG_8_ZERO
#define CK_F_PR_NEG_8_ZERO
CK_PR_N_Z_S(8, uint8_t)
#endif /* CK_F_PR_NEG_8_ZERO */

#endif /* CK_F_PR_LOAD_8 && CK_F_PR_CAS_8_VALUE */

#undef CK_PR_N_Z_S
#undef CK_PR_N_S
#undef CK_PR_N_Z
#undef CK_PR_N

#define CK_PR_FAA(S, M, T, C)						\
	CK_CC_INLINE static C						\
	ck_pr_faa_##S(M *target, T delta)				\
	{								\
		T previous;						\
		C punt;							\
		punt = (C)ck_pr_md_load_##S(target);			\
		previous = (T)punt;					\
		while (ck_pr_cas_##S##_value(target,			\
					     (C)previous,		\
					     (C)(previous + delta),	\
					     &previous) == false)	\
			ck_pr_stall();					\
									\
		return ((C)previous);					\
	}

#define CK_PR_FAS(S, M, C)						\
	CK_CC_INLINE static C						\
	ck_pr_fas_##S(M *target, C update)				\
	{								\
		C previous;						\
		previous = ck_pr_md_load_##S(target);			\
		while (ck_pr_cas_##S##_value(target,			\
					     previous,			\
					     update,			\
					     &previous) == false)	\
			ck_pr_stall();					\
									\
		return (previous);					\
	}

#define CK_PR_FAA_S(S, M) CK_PR_FAA(S, M, M, M)
#define CK_PR_FAS_S(S, M) CK_PR_FAS(S, M, M)

#if defined(CK_F_PR_LOAD_CHAR) && defined(CK_F_PR_CAS_CHAR_VALUE)

#ifndef CK_F_PR_FAA_CHAR
#define CK_F_PR_FAA_CHAR
CK_PR_FAA_S(char, char)
#endif /* CK_F_PR_FAA_CHAR */

#ifndef CK_F_PR_FAS_CHAR
#define CK_F_PR_FAS_CHAR
CK_PR_FAS_S(char, char)
#endif /* CK_F_PR_FAS_CHAR */

#endif /* CK_F_PR_LOAD_CHAR && CK_F_PR_CAS_CHAR_VALUE */

#if defined(CK_F_PR_LOAD_INT) && defined(CK_F_PR_CAS_INT_VALUE)

#ifndef CK_F_PR_FAA_INT
#define CK_F_PR_FAA_INT
CK_PR_FAA_S(int, int)
#endif /* CK_F_PR_FAA_INT */

#ifndef CK_F_PR_FAS_INT
#define CK_F_PR_FAS_INT
CK_PR_FAS_S(int, int)
#endif /* CK_F_PR_FAS_INT */

#endif /* CK_F_PR_LOAD_INT && CK_F_PR_CAS_INT_VALUE */

#if defined(CK_F_PR_LOAD_DOUBLE) && defined(CK_F_PR_CAS_DOUBLE_VALUE) && \
	    !defined(CK_PR_DISABLE_DOUBLE)

#ifndef CK_F_PR_FAA_DOUBLE
#define CK_F_PR_FAA_DOUBLE
CK_PR_FAA_S(double, double)
#endif /* CK_F_PR_FAA_DOUBLE */

#ifndef CK_F_PR_FAS_DOUBLE
#define CK_F_PR_FAS_DOUBLE
CK_PR_FAS_S(double, double)
#endif /* CK_F_PR_FAS_DOUBLE */

#endif /* CK_F_PR_LOAD_DOUBLE && CK_F_PR_CAS_DOUBLE_VALUE && !CK_PR_DISABLE_DOUBLE */

#if defined(CK_F_PR_LOAD_UINT) && defined(CK_F_PR_CAS_UINT_VALUE)

#ifndef CK_F_PR_FAA_UINT
#define CK_F_PR_FAA_UINT
CK_PR_FAA_S(uint, unsigned int)
#endif /* CK_F_PR_FAA_UINT */

#ifndef CK_F_PR_FAS_UINT
#define CK_F_PR_FAS_UINT
CK_PR_FAS_S(uint, unsigned int)
#endif /* CK_F_PR_FAS_UINT */

#endif /* CK_F_PR_LOAD_UINT && CK_F_PR_CAS_UINT_VALUE */

#if defined(CK_F_PR_LOAD_PTR) && defined(CK_F_PR_CAS_PTR_VALUE)

#ifndef CK_F_PR_FAA_PTR
#define CK_F_PR_FAA_PTR
CK_PR_FAA(ptr, void, uintptr_t, void *)
#endif /* CK_F_PR_FAA_PTR */

#ifndef CK_F_PR_FAS_PTR
#define CK_F_PR_FAS_PTR
CK_PR_FAS(ptr, void, void *)
#endif /* CK_F_PR_FAS_PTR */

#endif /* CK_F_PR_LOAD_PTR && CK_F_PR_CAS_PTR_VALUE */

#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_CAS_64_VALUE)

#ifndef CK_F_PR_FAA_64
#define CK_F_PR_FAA_64
CK_PR_FAA_S(64, uint64_t)
#endif /* CK_F_PR_FAA_64 */

#ifndef CK_F_PR_FAS_64
#define CK_F_PR_FAS_64
CK_PR_FAS_S(64, uint64_t)
#endif /* CK_F_PR_FAS_64 */

#endif /* CK_F_PR_LOAD_64 && CK_F_PR_CAS_64_VALUE */

#if defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_CAS_32_VALUE)

#ifndef CK_F_PR_FAA_32
#define CK_F_PR_FAA_32
CK_PR_FAA_S(32, uint32_t)
#endif /* CK_F_PR_FAA_32 */

#ifndef CK_F_PR_FAS_32
#define CK_F_PR_FAS_32
CK_PR_FAS_S(32, uint32_t)
#endif /* CK_F_PR_FAS_32 */

#endif /* CK_F_PR_LOAD_32 && CK_F_PR_CAS_32_VALUE */

#if defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_CAS_16_VALUE)

#ifndef CK_F_PR_FAA_16
#define CK_F_PR_FAA_16
CK_PR_FAA_S(16, uint16_t)
#endif /* CK_F_PR_FAA_16 */

#ifndef CK_F_PR_FAS_16
#define CK_F_PR_FAS_16
CK_PR_FAS_S(16, uint16_t)
#endif /* CK_F_PR_FAS_16 */

#endif /* CK_F_PR_LOAD_16 && CK_F_PR_CAS_16_VALUE */

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_CAS_8_VALUE)

#ifndef CK_F_PR_FAA_8
#define CK_F_PR_FAA_8
CK_PR_FAA_S(8, uint8_t)
#endif /* CK_F_PR_FAA_8 */

#ifndef CK_F_PR_FAS_8
#define CK_F_PR_FAS_8
CK_PR_FAS_S(8, uint8_t)
#endif /* CK_F_PR_FAS_8 */

#endif /* CK_F_PR_LOAD_8 && CK_F_PR_CAS_8_VALUE */

#undef CK_PR_FAA_S
#undef CK_PR_FAS_S
#undef CK_PR_FAA
#undef CK_PR_FAS

#endif /* CK_PR_H */
