// $OpenBSD: bignum_sqr_4_8_alt.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Square, z := x^2
// Input x[4]; output z[8]
//
//    extern void bignum_sqr_4_8_alt(uint64_t z[static 8],
//                                   const uint64_t x[static 4]);
//
// Standard x86-64 ABI: RDI = z, RSI = x
// Microsoft x64 ABI:   RCX = z, RDX = x
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_sqr_4_8_alt)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_sqr_4_8_alt)
        .text

// Input arguments

#define z rdi
#define x rsi

// Other variables used as a rotating 3-word window to add terms to

#define t0 rcx
#define t1 r8
#define t2 r9

// Macro for the key "multiply and add to (c,h,l)" step, for square term

#define combadd1(c,h,l,numa)                    \
        mov     rax, numa;                      \
        mul     rax;                            \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, 0

// A short form where we don't expect a top carry

#define combads(h,l,numa)                       \
        mov     rax, numa;                      \
        mul     rax;                            \
        add     l, rax;                         \
        adc     h, rdx

// A version doubling before adding, for non-square terms

#define combadd2(c,h,l,numa,numb)               \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     rax, rax;                       \
        adc     rdx, rdx;                       \
        adc     c, 0;                           \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, 0

S2N_BN_SYMBOL(bignum_sqr_4_8_alt):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
#endif

// Result term 0

        mov     rax, [x]
        mul     rax

        mov     [z], rax
        mov     t0, rdx
        xor     t1, t1

// Result term 1

       xor     t2, t2
       combadd2(t2,t1,t0,[x],[x+8])
       mov     [z+8], t0

// Result term 2

        xor     t0, t0
        combadd1(t0,t2,t1,[x+8])
        combadd2(t0,t2,t1,[x],[x+16])
        mov     [z+16], t1

// Result term 3

        xor     t1, t1
        combadd2(t1,t0,t2,[x],[x+24])
        combadd2(t1,t0,t2,[x+8],[x+16])
        mov     [z+24], t2

// Result term 4

        xor     t2, t2
        combadd2(t2,t1,t0,[x+8],[x+24])
        combadd1(t2,t1,t0,[x+16])
        mov     [z+32], t0

// Result term 5

        xor     t0, t0
        combadd2(t0,t2,t1,[x+16],[x+24])
        mov     [z+40], t1

// Result term 6

        xor     t1, t1
        combads(t0,t2,[x+24])
        mov     [z+48], t2

// Result term 7

        mov     [z+56], t0

// Return

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
