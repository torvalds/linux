// $OpenBSD: bignum_sqr.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Square z := x^2
// Input x[n]; output z[k]
//
//    extern void bignum_sqr(uint64_t k, uint64_t *z, uint64_t n, const uint64_t *x);
//
// Does the "z := x^2" operation where x is n digits and result z is k.
// Truncates the result in general unless k >= 2 * n
//
// Standard x86-64 ABI: RDI = k, RSI = z, RDX = n, RCX = x
// Microsoft x64 ABI:   RCX = k, RDX = z, R8 = n, R9 = x
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_sqr)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_sqr)
        .text

// First three are where arguments come in, but n is moved.

#define p rdi
#define z rsi
#define x rcx
#define n r8

// These are always local scratch since multiplier result is in these

#define a rax
#define d rdx

// Other variables

#define i rbx
#define ll rbp
#define hh r9
#define k r10
#define y r11
#define htop r12
#define l r13
#define h r14
#define c r15

// Short versions

#define llshort ebp

S2N_BN_SYMBOL(bignum_sqr):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
#endif

// We use too many registers, and also we need rax:rdx for multiplications

        push    rbx
        push    rbp
        push    r12
        push    r13
        push    r14
        push    r15
        mov     n, rdx

// If p = 0 the result is trivial and nothing needs doing

        test    p, p
        jz      bignum_sqr_end

// initialize (hh,ll) = 0

        xor     llshort, llshort
        xor     hh, hh

// Iterate outer loop from k = 0 ... k = p - 1 producing result digits

        xor     k, k

bignum_sqr_outerloop:

// First let bot = MAX 0 (k + 1 - n) and top = MIN (k + 1) n
// We want to accumulate all x[i] * x[k - i] for bot <= i < top
// For the optimization of squaring we avoid duplication and do
// 2 * x[i] * x[k - i] for i < htop, where htop = MIN ((k+1)/2) n
// Initialize i = bot; in fact just compute bot as i directly.

        xor     c, c
        lea     i, [k+1]
        mov     htop, i
        shr     htop, 1
        sub     i, n
        cmovc   i, c
        cmp     htop, n
        cmovnc  htop, n

// Initialize the three-part local sum (c,h,l); c was already done above

        xor     l, l
        xor     h, h

// If htop <= bot then main doubled part of the sum is empty

        cmp     i, htop
        jnc     bignum_sqr_nosumming

// Use a moving pointer for [y] = x[k-i] for the cofactor

        mov     a, k
        sub     a, i
        lea     y, [x+8*a]

// Do the main part of the sum x[i] * x[k - i] for 2 * i < k

bignum_sqr_innerloop:
        mov     a, [x+8*i]
        mul     QWORD PTR [y]
        add     l, a
        adc     h, d
        adc     c, 0
        sub     y, 8
        inc     i
        cmp     i, htop
        jc      bignum_sqr_innerloop

// Now double it

        add     l, l
        adc     h, h
        adc     c, c

// If k is even (which means 2 * i = k) and i < n add the extra x[i]^2 term

bignum_sqr_nosumming:
        test    k, 1
        jnz     bignum_sqr_innerend
        cmp     i, n
        jnc     bignum_sqr_innerend

        mov     a, [x+8*i]
        mul     a
        add     l, a
        adc     h, d
        adc     c, 0

// Now add the local sum into the global sum, store and shift

bignum_sqr_innerend:
        add     l, ll
        mov     [z+8*k], l
        adc     h, hh
        mov     ll, h
        adc     c, 0
        mov     hh, c

        inc     k
        cmp     k, p
        jc      bignum_sqr_outerloop

// Restore registers and return

bignum_sqr_end:
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
