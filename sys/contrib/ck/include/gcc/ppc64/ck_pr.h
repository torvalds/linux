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

#ifndef CK_PR_PPC64_H
#define CK_PR_PPC64_H

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
 * This bounces the hardware thread from low to medium
 * priority. I am unsure of the benefits of this approach
 * but it is used by the Linux kernel.
 */
CK_CC_INLINE static void
ck_pr_stall(void)
{

	__asm__ __volatile__("or 1, 1, 1;"
			     "or 2, 2, 2;" ::: "memory");
	return;
}

#define CK_PR_FENCE(T, I)				\
	CK_CC_INLINE static void			\
	ck_pr_fence_strict_##T(void)			\
	{						\
		__asm__ __volatile__(I ::: "memory");   \
	}

/*
 * These are derived from:
 *     http://www.ibm.com/developerworks/systems/articles/powerpc.html
 */
CK_PR_FENCE(atomic, "lwsync")
CK_PR_FENCE(atomic_store, "lwsync")
CK_PR_FENCE(atomic_load, "sync")
CK_PR_FENCE(store_atomic, "lwsync")
CK_PR_FENCE(load_atomic, "lwsync")
CK_PR_FENCE(store, "lwsync")
CK_PR_FENCE(store_load, "sync")
CK_PR_FENCE(load, "lwsync")
CK_PR_FENCE(load_store, "lwsync")
CK_PR_FENCE(memory, "sync")
CK_PR_FENCE(acquire, "lwsync")
CK_PR_FENCE(release, "lwsync")
CK_PR_FENCE(acqrel, "lwsync")
CK_PR_FENCE(lock, "lwsync")
CK_PR_FENCE(unlock, "lwsync")

#undef CK_PR_FENCE

#define CK_PR_LOAD(S, M, T, C, I)					\
	CK_CC_INLINE static T						\
	ck_pr_md_load_##S(const M *target)				\
	{								\
		T r;							\
		__asm__ __volatile__(I "%U1%X1 %0, %1"			\
					: "=r" (r)			\
					: "m"  (*(const C *)target)	\
					: "memory");			\
		return (r);						\
	}

CK_PR_LOAD(ptr, void, void *, uint64_t, "ld")

#define CK_PR_LOAD_S(S, T, I) CK_PR_LOAD(S, T, T, T, I)

CK_PR_LOAD_S(64, uint64_t, "ld")
CK_PR_LOAD_S(32, uint32_t, "lwz")
CK_PR_LOAD_S(16, uint16_t, "lhz")
CK_PR_LOAD_S(8, uint8_t, "lbz")
CK_PR_LOAD_S(uint, unsigned int, "lwz")
CK_PR_LOAD_S(int, int, "lwz")
CK_PR_LOAD_S(short, short, "lhz")
CK_PR_LOAD_S(char, char, "lbz")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_LOAD_S(double, double, "ld")
#endif

#undef CK_PR_LOAD_S
#undef CK_PR_LOAD

#define CK_PR_STORE(S, M, T, C, I)				\
	CK_CC_INLINE static void				\
	ck_pr_md_store_##S(M *target, T v)			\
	{							\
		__asm__ __volatile__(I "%U0%X0 %1, %0"		\
					: "=m" (*(C *)target)	\
					: "r" (v)		\
					: "memory");		\
		return;						\
	}

CK_PR_STORE(ptr, void, const void *, uint64_t, "std")

#define CK_PR_STORE_S(S, T, I) CK_PR_STORE(S, T, T, T, I)

CK_PR_STORE_S(64, uint64_t, "std")
CK_PR_STORE_S(32, uint32_t, "stw")
CK_PR_STORE_S(16, uint16_t, "sth")
CK_PR_STORE_S(8, uint8_t, "stb")
CK_PR_STORE_S(uint, unsigned int, "stw")
CK_PR_STORE_S(int, int, "stw")
CK_PR_STORE_S(short, short, "sth")
CK_PR_STORE_S(char, char, "stb")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_STORE_S(double, double, "std")
#endif

#undef CK_PR_STORE_S
#undef CK_PR_STORE

CK_CC_INLINE static bool
ck_pr_cas_64_value(uint64_t *target, uint64_t compare, uint64_t set, uint64_t *value)
{
	uint64_t previous;

        __asm__ __volatile__("1:"
			     "ldarx %0, 0, %1;"
			     "cmpd  0, %0, %3;"
			     "bne-  2f;"
			     "stdcx. %2, 0, %1;"
			     "bne-  1b;"
			     "2:"
                                : "=&r" (previous)
                                : "r"   (target),
				  "r"   (set),
                                  "r"   (compare)
                                : "memory", "cc");

        *value = previous;
        return (previous == compare);
}

CK_CC_INLINE static bool
ck_pr_cas_ptr_value(void *target, void *compare, void *set, void *value)
{
	void *previous;

        __asm__ __volatile__("1:"
			     "ldarx %0, 0, %1;"
			     "cmpd  0, %0, %3;"
			     "bne-  2f;"
			     "stdcx. %2, 0, %1;"
			     "bne-  1b;"
			     "2:"
                                : "=&r" (previous)
                                : "r"   (target),
				  "r"   (set),
                                  "r"   (compare)
                                : "memory", "cc");

        ck_pr_md_store_ptr(value, previous);
        return (previous == compare);
}

CK_CC_INLINE static bool
ck_pr_cas_64(uint64_t *target, uint64_t compare, uint64_t set)
{
	uint64_t previous;

        __asm__ __volatile__("1:"
			     "ldarx %0, 0, %1;"
			     "cmpd  0, %0, %3;"
			     "bne-  2f;"
			     "stdcx. %2, 0, %1;"
			     "bne-  1b;"
			     "2:"
                                : "=&r" (previous)
                                : "r"   (target),
				  "r"   (set),
                                  "r"   (compare)
                                : "memory", "cc");

        return (previous == compare);
}

CK_CC_INLINE static bool
ck_pr_cas_ptr(void *target, void *compare, void *set)
{
	void *previous;

        __asm__ __volatile__("1:"
			     "ldarx %0, 0, %1;"
			     "cmpd  0, %0, %3;"
			     "bne-  2f;"
			     "stdcx. %2, 0, %1;"
			     "bne-  1b;"
			     "2:"
                                : "=&r" (previous)
                                : "r"   (target),
				  "r"   (set),
                                  "r"   (compare)
                                : "memory", "cc");

        return (previous == compare);
}

#define CK_PR_CAS(N, T)							\
	CK_CC_INLINE static bool					\
	ck_pr_cas_##N##_value(T *target, T compare, T set, T *value)	\
	{								\
		T previous;						\
		__asm__ __volatile__("1:"				\
				     "lwarx %0, 0, %1;"			\
				     "cmpw  0, %0, %3;"			\
				     "bne-  2f;"			\
				     "stwcx. %2, 0, %1;"		\
				     "bne-  1b;"			\
				     "2:"				\
					: "=&r" (previous)		\
					: "r"   (target),		\
					  "r"   (set),			\
					  "r"   (compare)		\
					: "memory", "cc");		\
		*value = previous; 					\
		return (previous == compare);				\
	}								\
	CK_CC_INLINE static bool					\
	ck_pr_cas_##N(T *target, T compare, T set)			\
	{								\
		T previous;						\
		__asm__ __volatile__("1:"				\
				     "lwarx %0, 0, %1;"			\
				     "cmpw  0, %0, %3;"			\
				     "bne-  2f;"			\
				     "stwcx. %2, 0, %1;"		\
				     "bne-  1b;"			\
				     "2:"				\
					: "=&r" (previous)		\
					: "r"   (target),		\
					  "r"   (set),			\
					  "r"   (compare)		\
					: "memory", "cc");		\
		return (previous == compare);				\
	}

CK_PR_CAS(32, uint32_t)
CK_PR_CAS(uint, unsigned int)
CK_PR_CAS(int, int)

#undef CK_PR_CAS

#define CK_PR_FAS(N, M, T, W)					\
	CK_CC_INLINE static T					\
	ck_pr_fas_##N(M *target, T v)				\
	{							\
		T previous;					\
		__asm__ __volatile__("1:"			\
				     "l" W "arx %0, 0, %1;"	\
				     "st" W "cx. %2, 0, %1;"	\
				     "bne- 1b;"			\
					: "=&r" (previous)	\
					: "r"   (target),	\
					  "r"   (v)		\
					: "memory", "cc");	\
		return (previous);				\
	}

CK_PR_FAS(64, uint64_t, uint64_t, "d")
CK_PR_FAS(32, uint32_t, uint32_t, "w")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_FAS(double, double, double, "d")
#endif
CK_PR_FAS(ptr, void, void *, "d")
CK_PR_FAS(int, int, int, "w")
CK_PR_FAS(uint, unsigned int, unsigned int, "w")

#undef CK_PR_FAS

#define CK_PR_UNARY(O, N, M, T, I, W)				\
	CK_CC_INLINE static void				\
	ck_pr_##O##_##N(M *target)				\
	{							\
		T previous;					\
		__asm__ __volatile__("1:"			\
				     "l" W "arx %0, 0, %1;"	\
				      I ";"			\
				     "st" W "cx. %0, 0, %1;"	\
				     "bne-  1b;"		\
					: "=&r" (previous)	\
					: "r"   (target)	\
					: "memory", "cc");	\
		return;						\
	}

CK_PR_UNARY(inc, ptr, void, void *, "addic %0, %0, 1", "d")
CK_PR_UNARY(dec, ptr, void, void *, "addic %0, %0, -1", "d")
CK_PR_UNARY(not, ptr, void, void *, "not %0, %0", "d")
CK_PR_UNARY(neg, ptr, void, void *, "neg %0, %0", "d")

#define CK_PR_UNARY_S(S, T, W)					\
	CK_PR_UNARY(inc, S, T, T, "addic %0, %0, 1", W)		\
	CK_PR_UNARY(dec, S, T, T, "addic %0, %0, -1", W)	\
	CK_PR_UNARY(not, S, T, T, "not %0, %0", W)		\
	CK_PR_UNARY(neg, S, T, T, "neg %0, %0", W)

CK_PR_UNARY_S(64, uint64_t, "d")
CK_PR_UNARY_S(32, uint32_t, "w")
CK_PR_UNARY_S(uint, unsigned int, "w")
CK_PR_UNARY_S(int, int, "w")

#undef CK_PR_UNARY_S
#undef CK_PR_UNARY

#define CK_PR_BINARY(O, N, M, T, I, W)				\
	CK_CC_INLINE static void				\
	ck_pr_##O##_##N(M *target, T delta)			\
	{							\
		T previous;					\
		__asm__ __volatile__("1:"			\
				     "l" W "arx %0, 0, %1;"	\
				      I " %0, %2, %0;"		\
				     "st" W "cx. %0, 0, %1;"	\
				     "bne-  1b;"		\
					: "=&r" (previous)	\
					: "r"   (target),	\
					  "r"   (delta)		\
					: "memory", "cc");	\
		return;						\
	}

CK_PR_BINARY(and, ptr, void, uintptr_t, "and", "d")
CK_PR_BINARY(add, ptr, void, uintptr_t, "add", "d")
CK_PR_BINARY(or, ptr, void, uintptr_t, "or", "d")
CK_PR_BINARY(sub, ptr, void, uintptr_t, "sub", "d")
CK_PR_BINARY(xor, ptr, void, uintptr_t, "xor", "d")

#define CK_PR_BINARY_S(S, T, W)			\
	CK_PR_BINARY(and, S, T, T, "and", W)	\
	CK_PR_BINARY(add, S, T, T, "add", W)	\
	CK_PR_BINARY(or, S, T, T, "or", W)	\
	CK_PR_BINARY(sub, S, T, T, "subf", W)	\
	CK_PR_BINARY(xor, S, T, T, "xor", W)

CK_PR_BINARY_S(64, uint64_t, "d")
CK_PR_BINARY_S(32, uint32_t, "w")
CK_PR_BINARY_S(uint, unsigned int, "w")
CK_PR_BINARY_S(int, int, "w")

#undef CK_PR_BINARY_S
#undef CK_PR_BINARY

CK_CC_INLINE static void *
ck_pr_faa_ptr(void *target, uintptr_t delta)
{
	uintptr_t previous, r;

	__asm__ __volatile__("1:"
			     "ldarx %0, 0, %2;"
			     "add %1, %3, %0;"
			     "stdcx. %1, 0, %2;"
			     "bne-  1b;"
				: "=&r" (previous),
				  "=&r" (r)
				: "r"   (target),
				  "r"   (delta)
				: "memory", "cc");

	return (void *)(previous);
}

#define CK_PR_FAA(S, T, W)						\
	CK_CC_INLINE static T						\
	ck_pr_faa_##S(T *target, T delta)				\
	{								\
		T previous, r;						\
		__asm__ __volatile__("1:"				\
				     "l" W "arx %0, 0, %2;"		\
				     "add %1, %3, %0;"			\
				     "st" W "cx. %1, 0, %2;"		\
				     "bne-  1b;"			\
					: "=&r" (previous),		\
					  "=&r" (r)			\
					: "r"   (target),		\
					  "r"   (delta)			\
					: "memory", "cc");		\
		return (previous);					\
	}

CK_PR_FAA(64, uint64_t, "d")
CK_PR_FAA(32, uint32_t, "w")
CK_PR_FAA(uint, unsigned int, "w")
CK_PR_FAA(int, int, "w")

#undef CK_PR_FAA

#endif /* CK_PR_PPC64_H */
