/*
 * Copyright 2009-2016 Samy Al Bahra.
 * Copyright 2013-2016 Olivier Houchard.
 * Copyright 2016 Alexey Kopytov.
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

#ifndef CK_PR_AARCH64_LSE_H
#define CK_PR_AARCH64_LSE_H

#ifndef CK_PR_H
#error Do not include this file directly, use ck_pr.h
#endif

CK_CC_INLINE static bool
ck_pr_cas_64_2_value(uint64_t target[2], uint64_t compare[2], uint64_t set[2], uint64_t value[2])
{
        uint64_t tmp1;
        uint64_t tmp2;
        register uint64_t x0 __asm__ ("x0") = compare[0];
        register uint64_t x1 __asm__ ("x1") = compare[1];
        register uint64_t x2 __asm__ ("x2") = set[0];
        register uint64_t x3 __asm__ ("x3") = set[1];

        __asm__ __volatile__("casp %0, %1, %4, %5, [%6];"
                             "eor %2, %0, %7;"
                             "eor %3, %1, %8;"
                             "orr %2, %2, %3;"
                             : "+&r" (x0), "+&r" (x1), "=&r" (tmp1), "=&r" (tmp2)
                             : "r" (x2), "r" (x3), "r" (target), "r" (compare[0]), "r" (compare[1])
                             : "memory");

        value[0] = x0;
        value[1] = x1;

        return (!!tmp1);
}

CK_CC_INLINE static bool
ck_pr_cas_ptr_2_value(void *target, void *compare, void *set, void *value)
{
        return (ck_pr_cas_64_2_value(CK_CPP_CAST(uint64_t *, target),
                                   CK_CPP_CAST(uint64_t *, compare),
                                   CK_CPP_CAST(uint64_t *, set),
                                   CK_CPP_CAST(uint64_t *, value)));
}

CK_CC_INLINE static bool
ck_pr_cas_64_2(uint64_t target[2], uint64_t compare[2], uint64_t set[2])
{
        register uint64_t x0 __asm__ ("x0") = compare[0];
        register uint64_t x1 __asm__ ("x1") = compare[1];
        register uint64_t x2 __asm__ ("x2") = set[0];
        register uint64_t x3 __asm__ ("x3") = set[1];

        __asm__ __volatile__("casp %0, %1, %2, %3, [%4];"
                             "eor %0, %0, %5;"
                             "eor %1, %1, %6;"
                             "orr %0, %0, %1;"
                             : "+&r" (x0), "+&r" (x1)
                             : "r" (x2), "r" (x3), "r" (target), "r" (compare[0]), "r" (compare[1])
                             : "memory");

        return (!!x0);
}
CK_CC_INLINE static bool
ck_pr_cas_ptr_2(void *target, void *compare, void *set)
{
        return (ck_pr_cas_64_2(CK_CPP_CAST(uint64_t *, target),
                             CK_CPP_CAST(uint64_t *, compare),
                             CK_CPP_CAST(uint64_t *, set)));
}


#define CK_PR_CAS(N, M, T, W, R)					\
        CK_CC_INLINE static bool					\
        ck_pr_cas_##N##_value(M *target, T compare, T set, M *value)	\
        {								\
                  *(T *)value = compare;				\
                __asm__ __volatile__(					\
                                     "cas" W " %" R "0, %" R "2, [%1];"	\
                    : "+&r" (*(T *)value)				\
                    : "r"   (target),					\
                    "r"   (set)						\
                    : "memory");					\
                return (*(T *)value == compare);                        \
        }								\
        CK_CC_INLINE static bool					\
        ck_pr_cas_##N(M *target, T compare, T set)			\
        {								\
                T previous = compare;					\
                __asm__ __volatile__(					\
                                     "cas" W " %" R "0, %" R "2, [%1];"	\
                    : "+&r" (previous)					\
                    : "r"   (target),					\
                    "r"   (set)						\
                    : "memory");					\
                return (previous == compare);   			\
        }

CK_PR_CAS(ptr, void, void *, "", "")

#define CK_PR_CAS_S(N, M, W, R)	CK_PR_CAS(N, M, M, W, R)
CK_PR_CAS_S(64, uint64_t, "", "")
#ifndef CK_PR_DISABLE_DOUBLE
CK_PR_CAS_S(double, double, "", "")
#endif
CK_PR_CAS_S(32, uint32_t, "", "w")
CK_PR_CAS_S(uint, unsigned int, "", "w")
CK_PR_CAS_S(int, int, "", "w")
CK_PR_CAS_S(16, uint16_t, "h", "w")
CK_PR_CAS_S(8, uint8_t, "b", "w")
CK_PR_CAS_S(short, short, "h", "w")
CK_PR_CAS_S(char, char, "b", "w")


#undef CK_PR_CAS_S
#undef CK_PR_CAS

#define CK_PR_FAS(N, M, T, W, R)					\
        CK_CC_INLINE static T						\
        ck_pr_fas_##N(M *target, T v)					\
        {								\
                T previous;						\
                __asm__ __volatile__(					\
                                     "swp" W " %" R "2, %" R "0, [%1];"	\
                                        : "=&r" (previous)		\
                                        : "r"   (target),		\
                                          "r"   (v)			\
                                        : "memory");			\
                return (previous);					\
        }

CK_PR_FAS(64, uint64_t, uint64_t, "", "")
CK_PR_FAS(32, uint32_t, uint32_t, "", "w")
CK_PR_FAS(ptr, void, void *, "", "")
CK_PR_FAS(int, int, int, "", "w")
CK_PR_FAS(uint, unsigned int, unsigned int, "", "w")
CK_PR_FAS(16, uint16_t, uint16_t, "h", "w")
CK_PR_FAS(8, uint8_t, uint8_t, "b", "w")
CK_PR_FAS(short, short, short, "h", "w")
CK_PR_FAS(char, char, char, "b", "w")


#undef CK_PR_FAS

#define CK_PR_UNARY(O, N, M, T, I, W, R, S)			\
        CK_CC_INLINE static void				\
        ck_pr_##O##_##N(M *target)				\
        {							\
                __asm__ __volatile__(I ";"			\
                                     "st" S W " " R "0, [%0];"	\
                                        :			\
                                        : "r"   (target)	\
                                        : "x0", "memory");	\
                return;						\
        }

CK_PR_UNARY(inc, ptr, void, void *, "mov x0, 1", "", "x", "add")
CK_PR_UNARY(dec, ptr, void, void *, "mov x0, -1", "", "x", "add")
CK_PR_UNARY(not, ptr, void, void *, "mov x0, -1", "", "x", "eor")
CK_PR_UNARY(inc, 64, uint64_t, uint64_t, "mov x0, 1", "", "x", "add")
CK_PR_UNARY(dec, 64, uint64_t, uint64_t, "mov x0, -1", "", "x", "add")
CK_PR_UNARY(not, 64, uint64_t, uint64_t, "mov x0, -1", "", "x", "eor")

#define CK_PR_UNARY_S(S, T, W)					\
        CK_PR_UNARY(inc, S, T, T, "mov w0, 1", W, "w", "add")	\
        CK_PR_UNARY(dec, S, T, T, "mov w0, -1", W, "w", "add")	\
        CK_PR_UNARY(not, S, T, T, "mov w0, -1", W, "w", "eor")	\

CK_PR_UNARY_S(32, uint32_t, "")
CK_PR_UNARY_S(uint, unsigned int, "")
CK_PR_UNARY_S(int, int, "")
CK_PR_UNARY_S(16, uint16_t, "h")
CK_PR_UNARY_S(8, uint8_t, "b")
CK_PR_UNARY_S(short, short, "h")
CK_PR_UNARY_S(char, char, "b")

#undef CK_PR_UNARY_S
#undef CK_PR_UNARY

#define CK_PR_BINARY(O, N, M, T, S, W, R, I)			\
        CK_CC_INLINE static void				\
        ck_pr_##O##_##N(M *target, T delta)			\
        {							\
                __asm__ __volatile__(I ";"			\
                                     "st" S W " %" R "0, [%1];"	\
                                        : "+&r" (delta)		\
                                        : "r"   (target)	\
                                        : "memory");		\
                return;						\
        }

CK_PR_BINARY(and, ptr, void, uintptr_t, "clr", "", "", "mvn %0, %0")
CK_PR_BINARY(add, ptr, void, uintptr_t, "add", "", "", "")
CK_PR_BINARY(or, ptr, void, uintptr_t, "set", "", "", "")
CK_PR_BINARY(sub, ptr, void, uintptr_t, "add", "", "", "neg %0, %0")
CK_PR_BINARY(xor, ptr, void, uintptr_t, "eor", "", "", "")
CK_PR_BINARY(and, 64, uint64_t, uint64_t, "clr", "", "", "mvn %0, %0")
CK_PR_BINARY(add, 64, uint64_t, uint64_t, "add", "", "", "")
CK_PR_BINARY(or, 64, uint64_t, uint64_t, "set", "", "", "")
CK_PR_BINARY(sub, 64, uint64_t, uint64_t, "add", "", "", "neg %0, %0")
CK_PR_BINARY(xor, 64, uint64_t, uint64_t, "eor", "", "", "")

#define CK_PR_BINARY_S(S, T, W)						\
        CK_PR_BINARY(and, S, T, T, "clr", W, "w", "mvn %w0, %w0")	\
        CK_PR_BINARY(add, S, T, T, "add", W, "w", "")			\
        CK_PR_BINARY(or, S, T, T, "set", W, "w", "")			\
        CK_PR_BINARY(sub, S, T, T, "add", W, "w", "neg %w0, %w0")	\
        CK_PR_BINARY(xor, S, T, T, "eor", W, "w", "")

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

        __asm__ __volatile__(
                             "ldadd %2, %0, [%1];"
                                : "=r" (previous)
                                : "r"   (target),
                                  "r"   (delta)
                                : "memory");

        return (void *)(previous);
}

CK_CC_INLINE static uint64_t
ck_pr_faa_64(uint64_t *target, uint64_t delta)
{
        uint64_t previous;

        __asm__ __volatile__(
                             "ldadd %2, %0, [%1];"
                                : "=r" (previous)
                                : "r"   (target),
                                  "r"   (delta)
                                : "memory");

        return (previous);
}

#define CK_PR_FAA(S, T, W)						\
        CK_CC_INLINE static T						\
        ck_pr_faa_##S(T *target, T delta)				\
        {								\
                T previous;						\
                __asm__ __volatile__(					\
                                     "ldadd" W " %w2, %w0, [%1];"	\
                                        : "=r" (previous)		\
                                        : "r"   (target),		\
                                          "r"   (delta)			\
                                        : "memory");			\
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

#endif /* CK_PR_AARCH64_LSE_H */
