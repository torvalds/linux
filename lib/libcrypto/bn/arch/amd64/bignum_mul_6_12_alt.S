// $OpenBSD: bignum_mul_6_12_alt.S,v 1.4 2025/08/12 10:23:40 jsing Exp $
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
// Inputs x[6], y[6]; output z[12]
//
//    extern void bignum_mul_6_12_alt(uint64_t z[static 12],
//                                    const uint64_t x[static 6],
//                                    const uint64_t y[static 6]);
//
// Standard x86-64 ABI: RDI = z, RSI = x, RDX = y
// Microsoft x64 ABI:   RCX = z, RDX = x, R8 = y
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_mul_6_12_alt)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_mul_6_12_alt)
        .text

// These are actually right

#define z rdi
#define x rsi

// This is moved from rdx to free it for muls

#define y rcx

// Other variables used as a rotating 3-word window to add terms to

#define t0 r8
#define t1 r9
#define t2 r10

// Macro for the key "multiply and add to (c,h,l)" step

#define combadd(c,h,l,numa,numb)                \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, 0

// A minutely shorter form for when c = 0 initially

#define combadz(c,h,l,numa,numb)                \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     l, rax;                         \
        adc     h, rdx;                         \
        adc     c, c

// A short form where we don't expect a top carry

#define combads(h,l,numa,numb)                  \
        mov     rax, numa;                      \
        mul     QWORD PTR numb;                 \
        add     l, rax;                         \
        adc     h, rdx

S2N_BN_SYMBOL(bignum_mul_6_12_alt):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
#endif

// Copy y into a safe register to start with

        mov     y, rdx

// Result term 0

        mov     rax, [x]
        mul     QWORD PTR [y]

        mov     [z], rax
        mov     t0, rdx
        xor     t1, t1

// Result term 1

        xor     t2, t2
        combads(t1,t0,[x],[y+8])
        combadz(t2,t1,t0,[x+8],[y])
        mov     [z+8], t0

// Result term 2

        xor     t0, t0
        combadz(t0,t2,t1,[x],[y+16])
        combadd(t0,t2,t1,[x+8],[y+8])
        combadd(t0,t2,t1,[x+16],[y])
        mov     [z+16], t1

// Result term 3

        xor     t1, t1
        combadz(t1,t0,t2,[x],[y+24])
        combadd(t1,t0,t2,[x+8],[y+16])
        combadd(t1,t0,t2,[x+16],[y+8])
        combadd(t1,t0,t2,[x+24],[y])
        mov     [z+24], t2

// Result term 4

        xor     t2, t2
        combadz(t2,t1,t0,[x],[y+32])
        combadd(t2,t1,t0,[x+8],[y+24])
        combadd(t2,t1,t0,[x+16],[y+16])
        combadd(t2,t1,t0,[x+24],[y+8])
        combadd(t2,t1,t0,[x+32],[y])
        mov     [z+32], t0

// Result term 5

        xor     t0, t0
        combadz(t0,t2,t1,[x],[y+40])
        combadd(t0,t2,t1,[x+8],[y+32])
        combadd(t0,t2,t1,[x+16],[y+24])
        combadd(t0,t2,t1,[x+24],[y+16])
        combadd(t0,t2,t1,[x+32],[y+8])
        combadd(t0,t2,t1,[x+40],[y])
        mov     [z+40], t1

// Result term 6

        xor     t1, t1
        combadz(t1,t0,t2,[x+8],[y+40])
        combadd(t1,t0,t2,[x+16],[y+32])
        combadd(t1,t0,t2,[x+24],[y+24])
        combadd(t1,t0,t2,[x+32],[y+16])
        combadd(t1,t0,t2,[x+40],[y+8])
        mov     [z+48], t2

// Result term 7

        xor     t2, t2
        combadz(t2,t1,t0,[x+16],[y+40])
        combadd(t2,t1,t0,[x+24],[y+32])
        combadd(t2,t1,t0,[x+32],[y+24])
        combadd(t2,t1,t0,[x+40],[y+16])
        mov     [z+56], t0

// Result term 8

        xor     t0, t0
        combadz(t0,t2,t1,[x+24],[y+40])
        combadd(t0,t2,t1,[x+32],[y+32])
        combadd(t0,t2,t1,[x+40],[y+24])
        mov     [z+64], t1

// Result term 9

        xor     t1, t1
        combadz(t1,t0,t2,[x+32],[y+40])
        combadd(t1,t0,t2,[x+40],[y+32])
        mov     [z+72], t2

// Result term 10

        combads(t1,t0,[x+40],[y+40])
        mov     [z+80], t0

// Result term 11

        mov     [z+88], t1

// Return

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
