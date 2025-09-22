// $OpenBSD: bignum_cmadd.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Multiply-add with single-word multiplier, z := z + c * y
// Inputs c, y[n]; outputs function return (carry-out) and z[k]
//
//    extern uint64_t bignum_cmadd(uint64_t k, uint64_t *z, uint64_t c, uint64_t n,
//                                 const uint64_t *y);
//
// Does the "z := z + c * y" operation where y is n digits, result z is p.
// Truncates the result in general.
//
// The return value is a high/carry word that is meaningful when p = n + 1, or
// more generally when n <= p and the result fits in p + 1 digits. In these
// cases it gives the top digit of the (p + 1)-digit result.
//
// Standard x86-64 ABI: RDI = k, RSI = z, RDX = c, RCX = n, R8 = y, returns RAX
// Microsoft x64 ABI:   RCX = k, RDX = z, R8 = c, R9 = n, [RSP+40] = y, returns RAX
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_cmadd)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_cmadd)
        .text

#define p rdi
#define z rsi
#define c r9
#define n rcx
#define x r8

#define i r10
#define h r11

#define r rbx

#define hshort r11d
#define ishort r10d



S2N_BN_SYMBOL(bignum_cmadd):
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

// Seems hard to avoid one more register

        push    rbx

// First clamp the input size n := min(p,n) since we can never need to read
// past the p'th term of the input to generate p-digit output.
// Subtract p := p - min(n,p) so it holds the size of the extra tail needed

        cmp     p, n
        cmovc   n, p
        sub     p, n

// Initialize high part h = 0; if n = 0 do nothing but return that zero

        xor     h, h
        test    n, n
        jz      bignum_cmadd_end

// Move c into a safer register as multiplies overwrite rdx

        mov     c, rdx

// Initialization of the loop: 2^64 * CF + [h,z_0'] = z_0 + c * x_0

        mov     rax, [x]
        mul     c
        add     [z], rax
        mov     h, rdx
        mov     ishort, 1
        dec     n
        jz      bignum_cmadd_hightail

// Main loop, where we always have CF + previous high part h to add in

bignum_cmadd_loop:
        adc     h, [z+8*i]
        sbb     r, r
        mov     rax, [x+8*i]
        mul     c
        sub     rdx, r
        add     rax, h
        mov     [z+8*i], rax
        mov     h, rdx
        inc     i
        dec     n
        jnz     bignum_cmadd_loop

bignum_cmadd_hightail:
        adc     h, 0

// Propagate the carry all the way to the end with h as extra carry word

bignum_cmadd_tail:
        test    p, p
        jz      bignum_cmadd_end

        add     [z+8*i], h
        mov     hshort, 0
        inc     i
        dec     p
        jz      bignum_cmadd_highend

bignum_cmadd_tloop:
        adc     [z+8*i], h
        inc     i
        dec     p
        jnz     bignum_cmadd_tloop

bignum_cmadd_highend:

        adc     h, 0

// Return the high/carry word

bignum_cmadd_end:
        mov     rax, h

        pop     rbx
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
