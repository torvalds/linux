// $OpenBSD: bignum_mul_6_12.S,v 1.4 2025/08/12 10:23:40 jsing Exp $
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
//    extern void bignum_mul_6_12(uint64_t z[static 12], const uint64_t x[static 6],
//                                const uint64_t y[static 6]);
//
// Standard x86-64 ABI: RDI = z, RSI = x, RDX = y
// Microsoft x64 ABI:   RCX = z, RDX = x, R8 = y
// ----------------------------------------------------------------------------

#include "s2n_bignum_internal.h"

        .intel_syntax noprefix
        S2N_BN_SYM_VISIBILITY_DIRECTIVE(bignum_mul_6_12)
        S2N_BN_SYM_PRIVACY_DIRECTIVE(bignum_mul_6_12)
        .text

// These are actually right

#define z rdi
#define x rsi

// Copied in or set up

#define y rcx

// A zero register

#define zero rbp
#define zeroe ebp

// Add in x[i] * rdx to the (i,i+1) position with the register window
// Would be nice to have conditional expressions reg[i], reg[i+1] ...

.macro mulpadd arg1,arg2
        mulx    rbx, rax, [x+8*\arg2]
.if ((\arg1 + \arg2) % 6 == 0)
        adcx    r8, rax
        adox    r9, rbx
.elseif ((\arg1 + \arg2) % 6 == 1)
        adcx    r9, rax
        adox    r10, rbx
.elseif ((\arg1 + \arg2) % 6 == 2)
        adcx    r10, rax
        adox    r11, rbx
.elseif ((\arg1 + \arg2) % 6 == 3)
        adcx    r11, rax
        adox    r12, rbx
.elseif ((\arg1 + \arg2) % 6 == 4)
        adcx    r12, rax
        adox    r13, rbx
.elseif ((\arg1 + \arg2) % 6 == 5)
        adcx    r13, rax
        adox    r8, rbx
.endif

.endm


// Add in the whole j'th row

.macro addrow arg1
        mov     rdx, [y+8*\arg1]
        xor     zeroe, zeroe

        mulpadd \arg1, 0

.if (\arg1 % 6 == 0)
        mov     [z+8*\arg1],r8
.elseif (\arg1 % 6 == 1)
        mov     [z+8*\arg1],r9
.elseif (\arg1 % 6 == 2)
        mov     [z+8*\arg1],r10
.elseif (\arg1 % 6 == 3)
        mov     [z+8*\arg1],r11
.elseif (\arg1 % 6 == 4)
        mov     [z+8*\arg1],r12
.elseif (\arg1 % 6 == 5)
        mov     [z+8*\arg1],r13
.endif

        mulpadd \arg1, 1
        mulpadd \arg1, 2
        mulpadd \arg1, 3
        mulpadd \arg1, 4

.if (\arg1 % 6 == 0)
        mulx    r8, rax, [x+40]
        adcx    r13, rax
        adox    r8, zero
        adcx    r8, zero
.elseif (\arg1 % 6 == 1)
        mulx    r9, rax, [x+40]
        adcx    r8, rax
        adox    r9, zero
        adcx    r9, zero
.elseif (\arg1 % 6 == 2)
        mulx    r10, rax, [x+40]
        adcx    r9, rax
        adox    r10, zero
        adcx    r10, zero
.elseif (\arg1 % 6 == 3)
        mulx    r11, rax, [x+40]
        adcx    r10, rax
        adox    r11, zero
        adcx    r11, zero
.elseif (\arg1 % 6 == 4)
        mulx    r12, rax, [x+40]
        adcx    r11, rax
        adox    r12, zero
        adcx    r12, zero
.elseif (\arg1 % 6 == 5)
        mulx    r13, rax, [x+40]
        adcx    r12, rax
        adox    r13, zero
        adcx    r13, zero
.endif

.endm



S2N_BN_SYMBOL(bignum_mul_6_12):
        _CET_ENDBR

#if WINDOWS_ABI
        push    rdi
        push    rsi
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
#endif

// Save more registers to play with

        push    rbp
        push    rbx
        push    r12
        push    r13

// Copy y into a safe register to start with

        mov     y, rdx

// Zero a register, which also makes sure we don't get a fake carry-in

        xor     zeroe, zeroe

// Do the zeroth row, which is a bit different
// Write back the zero-zero product and then accumulate
// r8,r13,r12,r11,r10,r9 as y[0] * x from 1..6

        mov     rdx, [y]

        mulx    r9, r8, [x]
        mov     [z], r8

        mulx    r10, rbx, [x+8]
        adcx    r9, rbx

        mulx    r11, rbx, [x+16]
        adcx    r10, rbx

        mulx    r12, rbx, [x+24]
        adcx    r11, rbx

        mulx    r13, rbx, [x+32]
        adcx    r12, rbx

        mulx    r8, rbx, [x+40]
        adcx    r13, rbx
        adcx    r8, zero

// Now all the other rows in a uniform pattern

        addrow  1
        addrow  2
        addrow  3
        addrow  4
        addrow  5

// Now write back the additional columns

        mov     [z+48], r8
        mov     [z+56], r9
        mov     [z+64], r10
        mov     [z+72], r11
        mov     [z+80], r12
        mov     [z+88], r13

// Restore registers and return

        pop     r13
        pop     r12
        pop     rbx
        pop     rbp

#if WINDOWS_ABI
        pop    rsi
        pop    rdi
#endif
        ret

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
