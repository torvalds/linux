// $OpenBSD: bignum_sqr_4_8.S,v 1.4 2025/08/12 10:23:40 jsing Exp $
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
//    extern void bignum_sqr_4_8(uint64_t z[static 8], const uint64_t x[static 4]);
//
// Standard x86-64 ABI: RDI = z, RSI = x
// Microsoft x64 ABI:   RCX = z, RDX = x
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_sqr_4_8)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_sqr_4_8)
        .text

// These are actually right

#define z rdi
#define x rsi

// A zero register

#define zero rbp
#define zeroe ebp

// Other registers

#define d1 r8
#define d2 r9
#define d3 r10
#define d4 r11
#define d5 r12
#define d6 r13



S2N_BN_SYMBOL(bignum_sqr_4_8):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
#endif

// Save more registers to play with

        push    rbp
        push    r12
        push    r13

// Set up an initial window [d6;...d1] = [23;03;01]

        mov     rdx, [x]
        mulx    d2, d1, [x+8]
        mulx    d4, d3, [x+24]
        mov     rdx, [x+16]
        mulx    d6, d5, [x+24]

// Clear our zero register, and also initialize the flags for the carry chain

        xor     zeroe, zeroe

// Chain in the addition of 02 + 12 + 13 to that window (no carry-out possible)
// This gives all the "heterogeneous" terms of the squaring ready to double

        mulx    rcx, rax, [x]
        adcx    d2, rax
        adox    d3, rcx
        mulx    rcx, rax, [x+8]
        adcx    d3, rax
        adox    d4, rcx
        mov     rdx, [x+24]
        mulx    rcx, rax, [x+8]
        adcx    d4, rax
        adox    d5, rcx
        adcx    d5, zero
        adox    d6, zero
        adcx    d6, zero

// In principle this is otiose as CF and OF carries are absorbed at this point
// However it seems helpful for the OOO engine to be told it's a fresh start

        xor     zeroe, zeroe

// Double and add to the 00 + 11 + 22 + 33 terms
//
// We could use shift-double but this seems tidier and in larger squarings
// it was actually more efficient. I haven't experimented with this small
// case to see how much that matters. Note: the writeback here is sprinkled
// into the sequence in such a way that things still work if z = x, i.e. if
// the output overwrites the input buffer and beyond.

        mov     rdx, [x]
        mulx    rdx, rax, rdx
        mov     [z], rax
        adcx    d1, d1
        adox    d1, rdx
        mov     rdx, [x+8]
        mov     [z+8], d1
        mulx    rdx, rax, rdx
        adcx    d2, d2
        adox    d2, rax
        adcx    d3, d3
        adox    d3, rdx
        mov     rdx, [x+16]
        mov     [z+16], d2
        mulx    rdx, rax, rdx
        adcx    d4, d4
        adox    d4, rax
        adcx    d5, d5
        adox    d5, rdx
        mov     rdx, [x+24]
        mov     [z+24], d3
        mulx    rdx, rax, rdx
        mov     [z+32], d4
        adcx    d6, d6
        mov     [z+40], d5
        adox    d6, rax
        mov     [z+48], d6
        adcx    rdx, zero
        adox    rdx, zero
        mov     [z+56], rdx

// Restore saved registers and return

        pop     r13
        pop     r12
        pop     rbp

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
