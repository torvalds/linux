// $OpenBSD: bignum_sqr_8_16_alt.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Input x[8]; output z[16]
//
//    extern void bignum_sqr_8_16_alt(uint64_t z[static 16],
//                                    const uint64_t x[static 8]);
//
// Standard x86-64 ABI: RDI = z, RSI = x
// Microsoft x64 ABI:   RCX = z, RDX = x
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_sqr_8_16_alt)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_sqr_8_16_alt)
        .text

// Input arguments

#define z rdi
#define x rsi

// Other variables used as a rotating 3-word window to add terms to

#define t0 r8
#define t1 r9
#define t2 r10

// Additional temporaries for local windows to share doublings

#define u0 rcx
#define u1 r11

// Macro for the key "multiply and add to (c,h,l)" step

#define combadd(c,h,l,numa,numb)                \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, 0

// Set up initial window (c,h,l) = numa * numb

#define combaddz(c,h,l,numa,numb)               \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        xor     c, c;                           \
        mov     l, rax;                         \
        mov     h, rdx

// Doubling step (c,h,l) = 2 * (c,hh,ll) + (0,h,l)

#define doubladd(c,h,l,hh,ll)                   \
        add     ll, ll;                         \
        adc     hh, hh;                         \
        adc     c, c;                           \
        add     l, ll;                          \
        adc     h, hh;                          \
        adc     c, 0

// Square term incorporation (c,h,l) += numba^2

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

// A version doubling directly before adding, for single non-square terms

#define combadd2(c,h,l,numa,numb)               \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     rax, rax;                       \
        adc     rdx, rdx;                       \
        adc     c, 0;                           \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, 0

S2N_BN_SYMBOL(bignum_sqr_8_16_alt):
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

        combaddz(t1,u1,u0,[x],[x+24])
        combadd(t1,u1,u0,[x+8],[x+16])
        doubladd(t1,t0,t2,u1,u0)
        mov     [z+24], t2

// Result term 4

        combaddz(t2,u1,u0,[x],[x+32])
        combadd(t2,u1,u0,[x+8],[x+24])
        doubladd(t2,t1,t0,u1,u0)
        combadd1(t2,t1,t0,[x+16])
        mov     [z+32], t0

// Result term 5

        combaddz(t0,u1,u0,[x],[x+40])
        combadd(t0,u1,u0,[x+8],[x+32])
        combadd(t0,u1,u0,[x+16],[x+24])
        doubladd(t0,t2,t1,u1,u0)
        mov     [z+40], t1

// Result term 6

        combaddz(t1,u1,u0,[x],[x+48])
        combadd(t1,u1,u0,[x+8],[x+40])
        combadd(t1,u1,u0,[x+16],[x+32])
        doubladd(t1,t0,t2,u1,u0)
        combadd1(t1,t0,t2,[x+24])
        mov     [z+48], t2

// Result term 7

        combaddz(t2,u1,u0,[x],[x+56])
        combadd(t2,u1,u0,[x+8],[x+48])
        combadd(t2,u1,u0,[x+16],[x+40])
        combadd(t2,u1,u0,[x+24],[x+32])
        doubladd(t2,t1,t0,u1,u0)
        mov     [z+56], t0

// Result term 8

        combaddz(t0,u1,u0,[x+8],[x+56])
        combadd(t0,u1,u0,[x+16],[x+48])
        combadd(t0,u1,u0,[x+24],[x+40])
        doubladd(t0,t2,t1,u1,u0)
        combadd1(t0,t2,t1,[x+32])
        mov     [z+64], t1

// Result term 9

        combaddz(t1,u1,u0,[x+16],[x+56])
        combadd(t1,u1,u0,[x+24],[x+48])
        combadd(t1,u1,u0,[x+32],[x+40])
        doubladd(t1,t0,t2,u1,u0)
        mov     [z+72], t2

// Result term 10

        combaddz(t2,u1,u0,[x+24],[x+56])
        combadd(t2,u1,u0,[x+32],[x+48])
        doubladd(t2,t1,t0,u1,u0)
        combadd1(t2,t1,t0,[x+40])
        mov     [z+80], t0

// Result term 11

        combaddz(t0,u1,u0,[x+32],[x+56])
        combadd(t0,u1,u0,[x+40],[x+48])
        doubladd(t0,t2,t1,u1,u0)
        mov     [z+88], t1

// Result term 12

        xor     t1, t1
        combadd2(t1,t0,t2,[x+40],[x+56])
        combadd1(t1,t0,t2,[x+48])
        mov     [z+96], t2

// Result term 13

        xor     t2, t2
        combadd2(t2,t1,t0,[x+48],[x+56])
        mov     [z+104], t0

// Result term 14

        combads(t2,t1,[x+56])
        mov     [z+112], t1

// Result term 15

        mov     [z+120], t2

// Return

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
