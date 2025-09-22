// $OpenBSD: bignum_sqr_8_16.S,v 1.4 2025/08/12 10:23:40 jsing Exp $
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
//    extern void bignum_sqr_8_16(uint64_t z[static 16], const uint64_t x[static 8]);
//
// Standard x86-64 ABI: RDI = z, RSI = x
// Microsoft x64 ABI:   RCX = z, RDX = x
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_sqr_8_16)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_sqr_8_16)
        .text

// These are actually right

#define z rdi
#define x rsi

// A zero register

#define zero rbp
#define zeroe ebp

// mulpadd i, j adds rdx * x[i] into the window  at the i+j point

.macro mulpadd arg1,arg2
        mulx    rcx, rax, [x+8*\arg1]
.if ((\arg1 + \arg2) % 8 == 0)
        adcx    r8, rax
        adox    r9, rcx
.elseif ((\arg1 + \arg2) % 8 == 1)
        adcx    r9, rax
        adox    r10, rcx
.elseif ((\arg1 + \arg2) % 8 == 2)
        adcx    r10, rax
        adox    r11, rcx
.elseif ((\arg1 + \arg2) % 8 == 3)
        adcx    r11, rax
        adox    r12, rcx
.elseif ((\arg1 + \arg2) % 8 == 4)
        adcx    r12, rax
        adox    r13, rcx
.elseif ((\arg1 + \arg2) % 8 == 5)
        adcx    r13, rax
        adox    r14, rcx
.elseif ((\arg1 + \arg2) % 8 == 6)
        adcx    r14, rax
        adox    r15, rcx
.elseif ((\arg1 + \arg2) % 8 == 7)
        adcx    r15, rax
        adox    r8, rcx
.endif

.endm

// mulpade i, j adds rdx * x[i] into the window at i+j
// but re-creates the top word assuming nothing to add there

.macro mulpade arg1,arg2
.if ((\arg1 + \arg2) % 8 == 0)
        mulx    r9, rax, [x+8*\arg1]
        adcx    r8, rax
        adox    r9, zero
.elseif ((\arg1 + \arg2) % 8 == 1)
        mulx    r10, rax, [x+8*\arg1]
        adcx    r9, rax
        adox    r10, zero
.elseif ((\arg1 + \arg2) % 8 == 2)
        mulx    r11, rax, [x+8*\arg1]
        adcx    r10, rax
        adox    r11, zero
.elseif ((\arg1 + \arg2) % 8 == 3)
        mulx    r12, rax, [x+8*\arg1]
        adcx    r11, rax
        adox    r12, zero
.elseif ((\arg1 + \arg2) % 8 == 4)
        mulx    r13, rax, [x+8*\arg1]
        adcx    r12, rax
        adox    r13, zero
.elseif ((\arg1 + \arg2) % 8 == 5)
        mulx    r14, rax, [x+8*\arg1]
        adcx    r13, rax
        adox    r14, zero
.elseif ((\arg1 + \arg2) % 8 == 6)
        mulx    r15, rax, [x+8*\arg1]
        adcx    r14, rax
        adox    r15, zero
.elseif ((\arg1 + \arg2) % 8 == 7)
        mulx    r8, rax, [x+8*\arg1]
        adcx    r15, rax
        adox    r8, zero
.endif

.endm

.macro diagonals

        xor     zeroe, zeroe

// Set initial window [r8..r10] + 2 wb = 10 + 20 + 30 + 40 + 50 + 60 + 70

        mov     rdx, [x]
        mulx    rax, r9, [x+8]
        mov     [z+8], r9
        mulx    rcx, r10, [x+16]
        adcx    r10, rax
        mov     [z+16], r10
        mulx    rax, r11, [x+24]
        adcx    r11, rcx
        mulx    rcx, r12, [x+32]
        adcx    r12, rax
        mulx    rax, r13, [x+40]
        adcx    r13, rcx
        mulx    rcx, r14, [x+48]
        adcx    r14, rax
        mulx    r8, r15, [x+56]
        adcx    r15, rcx
        adcx    r8, zero

// Add in the next diagonal = 21 + 31 + 41 + 51 + 61 + 71 + 54

        xor     zeroe, zeroe
        mov     rdx, [x+8]
        mulpadd 2, 1
        mov     [z+24], r11
        mulpadd 3, 1
        mov     [z+32], r12
        mulpadd 4, 1
        mulpadd 5, 1
        mulpadd 6, 1
        mulpade 7, 1
        mov     rdx, [x+32]
        mulpade 5, 4
        adcx    r10, zero

// And the next one = 32 + 42 + 52 + 62 + 72 + 64 + 65

        xor     zeroe, zeroe
        mov     rdx, [x+16]
        mulpadd 3, 2
        mov     [z+40], r13
        mulpadd 4, 2
        mov     [z+48], r14
        mulpadd 5, 2
        mulpadd 6, 2
        mulpadd 7, 2
        mov     rdx, [x+48]
        mulpade 4, 6
        mulpade 5, 6
        adcx    r12, zero

// And the final one = 43 + 53 + 63 + 73 + 74 + 75 + 76

        xor     zeroe, zeroe
        mov     rdx, [x+24]
        mulpadd 4, 3
        mov     [z+56], r15
        mulpadd 5, 3
        mov     [z+64], r8
        mulpadd 6, 3
        mulpadd 7, 3
        mov     rdx, [x+56]
        mulpadd 4, 7
        mulpade 5, 7
        mulpade 6, 7
        adcx    r14, zero

// Double and add things; use z[1]..z[8] and thereafter the registers
// r9..r15 which haven't been written back yet

        xor     zeroe, zeroe
        mov     rdx, [x]
        mulx    rcx, rax, rdx
        mov     [z], rax
        mov     rax, [z+8]
        adcx    rax, rax
        adox    rax, rcx
        mov     [z+8], rax

        mov     rax, [z+16]
        mov     rdx, [x+8]
        mulx    rcx, rdx, rdx
        adcx    rax, rax
        adox    rax, rdx
        mov     [z+16], rax
        mov     rax, [z+24]
        adcx    rax, rax
        adox    rax, rcx
        mov     [z+24], rax

        mov     rax, [z+32]
        mov     rdx, [x+16]
        mulx    rcx, rdx, rdx
        adcx    rax, rax
        adox    rax, rdx
        mov     [z+32], rax
        mov     rax, [z+40]
        adcx    rax, rax
        adox    rax, rcx
        mov     [z+40], rax

        mov     rax, [z+48]
        mov     rdx, [x+24]
        mulx    rcx, rdx, rdx
        adcx    rax, rax
        adox    rax, rdx
        mov     [z+48], rax
        mov     rax, [z+56]
        adcx    rax, rax
        adox    rax, rcx
        mov     [z+56], rax

        mov     rax, [z+64]
        mov     rdx, [x+32]
        mulx    rcx, rdx, rdx
        adcx    rax, rax
        adox    rax, rdx
        mov     [z+64], rax
        adcx    r9, r9
        adox    r9, rcx
        mov     [z+72], r9

        mov     rdx, [x+40]
        mulx    rcx, rdx, rdx
        adcx    r10, r10
        adox    r10, rdx
        mov     [z+80], r10
        adcx    r11, r11
        adox    r11, rcx
        mov     [z+88], r11

        mov     rdx, [x+48]
        mulx    rcx, rdx, rdx
        adcx    r12, r12
        adox    r12, rdx
        mov     [z+96], r12
        adcx    r13, r13
        adox    r13, rcx
        mov     [z+104], r13

        mov     rdx, [x+56]
        mulx    r15, rdx, rdx
        adcx    r14, r14
        adox    r14, rdx
        mov     [z+112], r14
        adcx    r15, zero
        adox    r15, zero
        mov     [z+120], r15

.endm


S2N_BN_SYMBOL(bignum_sqr_8_16):
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
        push    r14
        push    r15

// Do the multiplication

        diagonals

// Real epilog

        pop     r15
        pop     r14
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
