// $OpenBSD: bignum_mul.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

// ----------------------------------------------------------------------------
// Multiply z := x * y
// Inputs x[m], y[n]; output z[k]
//
//    extern void bignum_mul(uint64_t k, uint64_t *z, uint64_t m, const uint64_t *x,
//                           uint64_t n, const uint64_t *y);
//
// Does the "z := x * y" operation where x is m digits, y is n, result z is k.
// Truncates the result in general unless k >= m + n
//
// Standard x86-64 ABI: RDI = k, RSI = z, RDX = m, RCX = x, R8 = n, R9 = y
// Microsoft x64 ABI:   RCX = k, RDX = z, R8 = m, R9 = x, [RSP+40] = n, [RSP+48] = y
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_mul)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_mul)
        .text

// These are actually right

#define p rdi
#define z rsi
#define n r8

// These are not

#define c r15
#define h r14
#define l r13
#define x r12
#define y r11
#define i rbx
#define k r10
#define m rbp

// These are always local scratch since multiplier result is in these

#define a rax
#define d rdx



S2N_BN_SYMBOL(bignum_mul):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
        mov     r8, [rsp+56]
        mov     r9, [rsp+64]
#endif

// We use too many registers, and also we need rax:rdx for multiplications

        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        mov     m, rdx

// If the result size is zero, do nothing
// Note that even if either or both inputs has size zero, we can't
// just give up because we at least need to zero the output array
// If we did a multiply-add variant, however, then we could

        test    p, p
        jz      bignum_mul_end

// Set initial 2-part sum to zero (we zero c inside the body)

        xor     h,h
        xor     l,l

// Otherwise do outer loop k = 0 ... k = p - 1

        xor     k, k

bignum_mul_outerloop:

// Zero our carry term first; we eventually want it and a zero is useful now
// Set a =  max 0 (k + 1 - n), i = min (k + 1) m
// This defines the range a <= j < i for the inner summation
// Note that since k < p < 2^64 we can assume k + 1 doesn't overflow
// And since we want to increment it anyway, we might as well do it now

        xor     c, c            // c = 0
        inc     k               // k = k + 1

        mov     a, k            // a = k + 1
        sub     a, n            // a = k + 1 - n
        cmovc   a, c            // a = max 0 (k + 1 - n)

        mov     i, m            // i = m
        cmp     k, m            // CF <=> k + 1 < m
        cmovc   i, k            // i = min (k + 1) m

// Turn i into a loop count, and skip things if it's <= 0
// Otherwise set up initial pointers x -> x0[a] and y -> y0[k - a]
// and then launch into the main inner loop, postdecrementing i

        mov     d, k
        sub     d, i
        sub     i, a
        jbe     bignum_mul_innerend
        lea     x,[rcx+8*a]
        lea     y,[r9+8*d-8]

bignum_mul_innerloop:
        mov     rax, [y+8*i]
        mul     QWORD PTR  [x]
        add     x, 8
        add     l, rax
        adc     h, rdx
        adc     c, 0
        dec     i
        jnz     bignum_mul_innerloop

bignum_mul_innerend:

        mov     [z], l
        mov     l, h
        mov     h, c
        add     z, 8

        cmp     k, p
        jc      bignum_mul_outerloop

bignum_mul_end:
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbp
        pop     rbx
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
