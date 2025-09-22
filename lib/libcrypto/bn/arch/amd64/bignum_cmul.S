// $OpenBSD: bignum_cmul.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Multiply by a single word, z := c * y
// Inputs c, y[n]; outputs function return (carry-out) and z[k]
//
//    extern uint64_t bignum_cmul(uint64_t k, uint64_t *z, uint64_t c, uint64_t n,
//                                const uint64_t *y);
//
// Does the "z := c * y" operation where y is n digits, result z is p.
// Truncates the result in general unless p >= n + 1.
//
// The return value is a high/carry word that is meaningful when p >= n as
// giving the high part of the result. Since this is always zero if p > n,
// it is mainly of interest in the special case p = n, i.e. where the source
// and destination have the same nominal size, when it gives the extra word
// of the full result.
//
// Standard x86-64 ABI: RDI = k, RSI = z, RDX = c, RCX = n, R8 = y, returns RAX
// Microsoft x64 ABI:   RCX = k, RDX = z, R8 = c, R9 = n, [RSP+40] = y, returns RAX
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_cmul)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_cmul)
        .text

#define p rdi
#define z rsi
#define c r9
#define n rcx
#define x r8

#define i r10
#define h r11



S2N_BN_SYMBOL(bignum_cmul):
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

// First clamp the input size n := min(p,n) since we can never need to read
// past the p'th term of the input to generate p-digit output. Now we can
// assume that n <= p

        cmp     p, n
        cmovc   n, p

// Initialize current input/output pointer offset i and high part h.
// But then if n = 0 skip the multiplication and go to the tail part

        xor     h, h
        xor     i, i
        test    n, n
        jz      bignum_cmul_tail

// Move c into a safer register as multiplies overwrite rdx

        mov     c, rdx

// Initialization of the loop: [h,l] = c * x_0

        mov     rax, [x]
        mul     c
        mov     [z], rax
        mov     h, rdx
        inc     i
        cmp     i, n
        jz      bignum_cmul_tail

// Main loop doing the multiplications

bignum_cmul_loop:
        mov     rax, [x+8*i]
        mul     c
        add     rax, h
        adc     rdx, 0
        mov     [z+8*i], rax
        mov     h, rdx
        inc     i
        cmp     i, n
        jc      bignum_cmul_loop

// Add a tail when the destination is longer

bignum_cmul_tail:
        cmp     i, p
        jnc     bignum_cmul_end
        mov     [z+8*i], h
        xor     h, h
        inc     i
        cmp     i, p
        jnc     bignum_cmul_end

bignum_cmul_tloop:
        mov     [z+8*i], h
        inc     i
        cmp     i, p
        jc      bignum_cmul_tloop

// Return the high/carry word

bignum_cmul_end:
        mov     rax, h

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
