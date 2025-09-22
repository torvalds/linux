// $OpenBSD: bignum_modadd.S,v 1.4 2025/08/12 10:23:40 jsing Exp $
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
// Add modulo m, z := (x + y) mod m, assuming x and y reduced
// Inputs x[k], y[k], m[k]; output z[k]
//
//    extern void bignum_modadd(uint64_t k, uint64_t *z, const uint64_t *x,
//                              const uint64_t *y, const uint64_t *m);
//
// Standard x86-64 ABI: RDI = k, RSI = z, RDX = x, RCX = y, R8 = m
// Microsoft x64 ABI:   RCX = k, RDX = z, R8 = x, R9 = y, [RSP+40] = m
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_modadd)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_modadd)
        .text

#define k rdi
#define z rsi
#define x rdx
#define y rcx
#define m r8
#define i r9
#define j r10
#define a rax
#define c r11

S2N_BN_SYMBOL(bignum_modadd):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
        mov     r8, [rsp+56]
#endif

// If k = 0 do nothing

        test    k, k
        jz      bignum_modadd_end

// First just add (c::z) := x + y

        xor     c, c
        mov     j, k
        xor     i, i
bignum_modadd_addloop:
        mov     a, [x+8*i]
        adc     a, [y+8*i]
        mov     [z+8*i], a
        inc     i
        dec     j
        jnz     bignum_modadd_addloop
        adc     c, 0

// Now do a comparison subtraction (c::z) - m, recording mask for (c::z) >= m

        mov     j, k
        xor     i, i
bignum_modadd_cmploop:
        mov     a, [z+8*i]
        sbb     a, [m+8*i]
        inc     i
        dec     j
        jnz     bignum_modadd_cmploop
        sbb     c, 0
        not     c

// Now do a masked subtraction z := z - [c] * m

        xor     i, i
bignum_modadd_subloop:
        mov     a, [m+8*i]
        and     a, c
        neg     j
        sbb     [z+8*i], a
        sbb     j, j
        inc     i
        cmp     i, k
        jc      bignum_modadd_subloop

bignum_modadd_end:
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
