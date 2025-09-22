// $OpenBSD: bignum_add.S,v 1.7 2025/08/11 14:13:56 jsing Exp $
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
// Add, z := x + y
// Inputs x[m], y[n]; outputs function return (carry-out) and z[p]
//
//    extern uint64_t bignum_add(uint64_t p, uint64_t *z, uint64_t m,
//                               const uint64_t *x, uint64_t n, const uint64_t *y);
//
// Does the z := x + y operation, truncating modulo p words in general and
// returning a top carry (0 or 1) in the p'th place, only adding the input
// words below p (as well as m and n respectively) to get the sum and carry.
//
// Standard x86-64 ABI: RDI = p, RSI = z, RDX = m, RCX = x, R8 = n, R9 = y, returns RAX
// Microsoft x64 ABI:   RCX = p, RDX = z, R8 = m, R9 = x, [RSP+40] = n, [RSP+48] = y, returns RAX
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_add)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_add)
        .text

#define p rdi
#define z rsi
#define m rdx
#define x rcx
#define n r8
#define y r9
#define i r10
#define a rax

#define ashort eax



S2N_BN_SYMBOL(bignum_add):
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

// Zero the main index counter for both branches

        xor     i, i

// First clamp the two input sizes m := min(p,m) and n := min(p,n) since
// we'll never need words past the p'th. Can now assume m <= p and n <= p.
// Then compare the modified m and n and branch accordingly

        cmp     p, m
        cmovc   m, p
        cmp     p, n
        cmovc   n, p
        cmp     m, n
        jc      bignum_add_ylonger

// The case where x is longer or of the same size (p >= m >= n)

        sub     p, m
        sub     m, n
        inc     m
        test    n, n
        jz      bignum_add_xtest
bignum_add_xmainloop:
        mov     a, [x+8*i]
        adc     a, [y+8*i]
        mov     [z+8*i],a
        inc     i
        dec     n
        jnz     bignum_add_xmainloop
        jmp     bignum_add_xtest
bignum_add_xtoploop:
        mov     a, [x+8*i]
        adc     a, 0
        mov     [z+8*i],a
        inc     i
bignum_add_xtest:
        dec     m
        jnz     bignum_add_xtoploop
        mov     ashort, 0
        adc     a, 0
        test    p, p
        jnz     bignum_add_tails
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

// The case where y is longer (p >= n > m)

bignum_add_ylonger:

        sub     p, n
        sub     n, m
        test    m, m
        jz      bignum_add_ytoploop
bignum_add_ymainloop:
        mov     a, [x+8*i]
        adc     a, [y+8*i]
        mov     [z+8*i],a
        inc     i
        dec     m
        jnz     bignum_add_ymainloop
bignum_add_ytoploop:
        mov     a, [y+8*i]
        adc     a, 0
        mov     [z+8*i],a
        inc     i
        dec     n
        jnz     bignum_add_ytoploop
        mov     ashort, 0
        adc     a, 0
        test    p, p
        jnz     bignum_add_tails
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

// Adding a non-trivial tail, when p > max(m,n)

bignum_add_tails:
        mov     [z+8*i],a
        xor     a, a
        jmp     bignum_add_tail
bignum_add_tailloop:
        mov     [z+8*i],a
bignum_add_tail:
        inc     i
        dec     p
        jnz     bignum_add_tailloop
#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
