/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Sunnorth

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#define ra r3
#define a0 r4
#define a1 r5
#define a2 r6
#define a3 r7
#define v0 r23

#define t0 r8
#define t1 r9
#define t2 r10
#define t3 r11
#define t4 r22

#ifndef __pic__
#if !defined(L_mulsi3) && !defined(L_divsi3)
       .text
       .global _flush_cache
_flush_cache:
        srli    r9, r5, 4
        mv      r8, r4
        mtsr    r9, sr0
1:
        cache   0xe, [r8, 0]            # write back invalid dcache
        addi    r8, 16
        bcnz    1b
        mfcr    r8, cr4
        bittst! r8, 0x3                 # if LDM is enable, write back LDM
        beq!    6f
        ldi     r10, 0
        cache   0xc, [r10, 0]
6:
        bittst! r8, 0x2                 # if LIM is enable, refill it
        beq!    7f
        cache   0x4, [r10, 0]
7:
        #nop!
        #nop!
        #nop!
        #nop!
        #nop!
        mv      r8, r4
        mtsr    r9, sr0
2:
        cache   0x2, [r8, 0]            # invalid unlock icache
        #nop!
        #nop!
        #nop!
        #nop!
        #nop!
        addi    r8, 16
        bcnz    2b
        br      r3
#endif

/* FUNCTION
   (U) INT32 v0 = __mulsi3 ((U) INT32 a0, (U) INT32 a1);
   REGISTERS:
        use     t0
        modify  a0
        a1      -> become 0
   NOTE:
   this seems to give better performance to just rotate and add.  */

#ifdef L_mulsi3
        .text
        .global __umulsi3
        .global __mulsi3
        /* signed multiplication (32x32)  */
        .ent    __mulsi3
__umulsi3:
__mulsi3:
        li      t1, 0
__mulsi3_loop:
        andri.c t0, a1, 1               # t0 = multiplier[0]
        srli    a1, a1, 1               # a1 /= 2
        beq     __mulsi3_loop2          # skip if (t0 == 0)
        add     t1, t1, a0              # add multiplicand
__mulsi3_loop2:
        slli    a0, a0, 1               # multiplicand mul 2
        cmpi.c  a1, 0
        bne     __mulsi3_loop
        mv      r4, t1
        br      ra
        .end    __mulsi3
#endif /* L_mulsi3 */

/* FUNCTION
   UINT32 (v0) = __udivsi3 (UINT32 (a0), UINT32 (a1));
   INT32 (v0) = __divsi3 (INT32 (a0),  INT32 (a1));
   UINT32 (v0) = __umodsi3 (UINT32 (a0), UINT32 (a1));
   INT32 (v0) = __modsi3 (INT32 (a0),  INT32 (a1));
   DESCRIPTION
   performs 32-bit division/modulo.
   REGISTERS
   used t0      bit-index
        t1
   modify a0    becomes remainer  */
#ifdef L_divsi3
        .text
        .global __udivsi3
        .global __umodsi3
        .global __divsi3
        .global __modsi3

        /* unsigned division  */
        .ent    __udivsi3
__udivsi3:
        li      t4, 0
        cmpi.c  a1, 0
        beq     __uds_exit
        li      t0, 1
        blt     __uds_ok
__uds_normalize:
        cmp.c   a0, a1
        bcc     __uds_ok
        slli    a1, a1, 1
        slli    t0, t0, 1
        cmpi.c  a1, 0
        bge     __uds_normalize
__uds_ok:
__uds_loop2:
        cmp.c   a0, a1
        bcc     __uds_loop3
        sub     a0, a0, a1
        or      t4, t4, t0
__uds_loop3:
        srli    t0, t0, 1
        srli    a1, a1, 1
        cmpi.c  t0, 0
        bne     __uds_loop2
__uds_exit:
        mv      a1, a0
        mv      r4, t4
        br      ra
        .end    __udivsi3

        /* unsigned modulus  */
        .ent    __umodsi3
__umodsi3:
        mv      t3, ra
        jl      __udivsi3
        mv      r4, a1
        br      t3
        .end    __umodsi3

        /* abs and div  */
        .ent    __orgsi3
__orgsi3:
        cmpi.c  a0, 0
        bge     __orgsi3_a0p
        neg     a0, a0
__orgsi3_a0p:
        cmpi.c  a1, 0
        bge     __udivsi3
        neg     a1, a1
        b       __udivsi3               # goto udivsi3
        .end    __orgsi3

        /* signed division  */
        .ent    __divsi3
__divsi3:
        mv      t3, ra
        xor     t2, a0, a1
        jl      __orgsi3
__divsi3_adjust:
        cmpi.c  t2, 0
        bge     __divsi3_exit
        neg     r4, r4
__divsi3_exit:
        br      t3
        .end    __divsi3

        /* signed modulus  */
        .ent    __modsi3
__modsi3:
        mv      t3, ra
        mv      t2, a0
        jl      __orgsi3
        mv      r4, a1
        b       __divsi3_adjust
        .end    __modsi3

#endif /* L_divsi3 */
#else /* -fPIC */
#if !defined(L_mulsi3) && !defined(L_divsi3)
        .set pic
        .text
        .global _flush_cache
_flush_cache:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        srli    r9, r5, 4
        mv      r8, r4
        mtsr    r9, sr0
1:
        cache   0xe, [r8, 0]            # write back invalid dcache
        addi    r8, 16
        bcnz    1b
        mfcr    r8, cr4
        bittst! r8, 0x3                 # if LDM is enable, write back LDM
        beq!    6f
        ldi     r10, 0
        cache   0xc, [r10, 0]
6:
        bittst! r8, 0x2                 # if LIM is enable, refill it
        beq!    7f
        cache   0x4, [r10, 0]
7:
        #nop!
        #nop!
        #nop!
        #nop!
        #nop!
        mv      r8, r4
        mtsr    r9, sr0
2:
        cache   0x2, [r8, 0]            # invalid unlock icache
        #nop!
        #nop!
        #nop!
        #nop!
        #nop!
        addi    r8, 16
        bcnz    2b
        .cprestore r0, 12               # pic used
        addi    r0, 8                   # pic used
        br      r3
#endif

/* FUNCTION
   (U) INT32 v0 = __mulsi3 ((U) INT32 a0, (U) INT32 a1);
   REGISTERS:
        use     t0
        modify  a0
        a1      -> become 0
   NOTE:
   this seems to give better performance to just rotate and add.  */

#ifdef L_mulsi3
        .set pic
        .text
        .global __umulsi3
        .global __mulsi3
        /* signed multiplication (32x32)  */
        .ent    __mulsi3
__umulsi3:
__mulsi3:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        li      t1, 0
__mulsi3_loop:
        andri.c t0, a1, 1               # t0 = multiplier[0]
        srli    a1, a1, 1               # a1 /= 2
        beq     __mulsi3_loop2          # skip if (t0 == 0)
        add     t1, t1, a0              # add multiplicand
__mulsi3_loop2:
        slli    a0, a0, 1               # multiplicand mul 2
        cmpi.c  a1, 0
        bne     __mulsi3_loop
        mv      r4, t1
        .cprestore r0, 12               # pic used
        addi    r0, 8                   # pic used
        br      ra
        .end    __mulsi3
#endif /* L_mulsi3 */

/* FUNCTION
   UINT32 (v0) = __udivsi3 (UINT32 (a0), UINT32 (a1));
   INT32 (v0) = __divsi3 (INT32 (a0),  INT32 (a1));
   UINT32 (v0) = __umodsi3 (UINT32 (a0), UINT32 (a1));
   INT32 (v0) = __modsi3 (INT32 (a0),  INT32 (a1));
   DESCRIPTION
   performs 32-bit division/modulo.
   REGISTERS
   used t0      bit-index
        t1
   modify a0    becomes remainer  */
#ifdef L_divsi3
        .set pic
        .text
        .global __udivsi3
        .global __umodsi3
        .global __divsi3
        .global __modsi3

        /* unsigned division  */
        .ent    __udivsi3
__udivsi3:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        li      t4, 0
        cmpi.c  a1, 0
        beq     __uds_exit
        li      t0, 1
        blt     __uds_ok
__uds_normalize:
        cmp.c   a0, a1
        bcc     __uds_ok
        slli    a1, a1, 1
        slli    t0, t0, 1
        cmpi.c  a1, 0
        bge     __uds_normalize
__uds_ok:
__uds_loop2:
        cmp.c   a0, a1
        bcc     __uds_loop3
        sub     a0, a0, a1
        or      t4, t4, t0
__uds_loop3:
        srli    t0, t0, 1
        srli    a1, a1, 1
        cmpi.c  t0, 0
        bne     __uds_loop2
__uds_exit:
        mv      a1, a0
        mv      r4, t4
        .cprestore r0, 12               # pic used
        addi    r0, 8                   # pic used
        br      ra
        .end    __udivsi3

        /* unsigned modulus  */
        .ent    __umodsi3
__umodsi3:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        li      t1, 0
        mv      t3, ra
#       jl      __udivsi3
        la      r29, __udivsi3
        brl     r29
        mv      r4, a1
        .cprestore r0, 12               # pic used
        addi    r0, 8                   # pic used
        br      t3
        .end    __umodsi3

        /* abs and div  */
        .ent    __orgsi3
__orgsi3:
        cmpi.c  a0, 0
        bge     __orgsi3_a0p
        neg     a0, a0
__orgsi3_a0p:
        cmpi.c  a1, 0
        bge     __udivsi3
        neg     a1, a1
        b       __udivsi3               # goto udivsi3
        .end    __orgsi3

        /* signed division  */
        .ent    __divsi3
__divsi3:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        mv      t3, ra
        xor     t2, a0, a1
#       jl      __orgsi3
        la      r29, __orgsi3
        brl     r29
__divsi3_adjust:
        cmpi.c  t2, 0
        bge     __divsi3_exit
        neg     r4, r4
__divsi3_exit:
        .cprestore r0, 12               # pic used
        addi    r0, 8                   # pic used
        br      t3
        .end    __divsi3

        /* signed modulus  */
        .ent    __modsi3
__modsi3:
        addi    r0, -8                  # pic used
        .cpload r29                     # pic used
        mv      t3, ra
        mv      t2, a0
#       jl      __orgsi3
        la      r29, __orgsi3
        brl     r29
        mv      r4, a1
        b       __divsi3_adjust
        .end    __modsi3

#endif /*L_divsi3 */
#endif
