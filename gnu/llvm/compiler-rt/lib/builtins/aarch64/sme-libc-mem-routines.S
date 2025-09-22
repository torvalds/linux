// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Routines taken from libc/AOR_v20.02/string/aarch64

#include "../assembly.h"

#ifdef __aarch64__

#define L(l) .L ## l

//
//  __arm_sc_memcpy / __arm_sc_memmove
//

#define dstin    x0
#define src      x1
#define count    x2
#define dst      x3
#define srcend1  x4
#define dstend1  x5
#define A_l      x6
#define A_lw     w6
#define A_h      x7
#define B_l      x8
#define B_lw     w8
#define B_h      x9
#define C_l      x10
#define C_lw     w10
#define C_h      x11
#define D_l      x12
#define D_h      x13
#define E_l      x14
#define E_h      x15
#define F_l      x16
#define F_h      x17
#define G_l      count
#define G_h      dst
#define H_l      src
#define H_h      srcend1
#define tmp1     x14

/* This implementation handles overlaps and supports both memcpy and memmove
   from a single entry point.  It uses unaligned accesses and branchless
   sequences to keep the code small, simple and improve performance.

   Copies are split into 3 main cases: small copies of up to 32 bytes, medium
   copies of up to 128 bytes, and large copies.  The overhead of the overlap
   check is negligible since it is only required for large copies.

   Large copies use a software pipelined loop processing 64 bytes per iteration.
   The destination pointer is 16-byte aligned to minimize unaligned accesses.
   The loop tail is handled by always copying 64 bytes from the end.
*/

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_sc_memcpy)
        add     srcend1, src, count
        add     dstend1, dstin, count
        cmp     count, 128
        b.hi    L(copy_long)
        cmp     count, 32
        b.hi    L(copy32_128)

        /* Small copies: 0..32 bytes.  */
        cmp     count, 16
        b.lo    L(copy16)
        ldp     A_l, A_h, [src]
        ldp     D_l, D_h, [srcend1, -16]
        stp     A_l, A_h, [dstin]
        stp     D_l, D_h, [dstend1, -16]
        ret

        /* Copy 8-15 bytes.  */
L(copy16):
        tbz     count, 3, L(copy8)
        ldr     A_l, [src]
        ldr     A_h, [srcend1, -8]
        str     A_l, [dstin]
        str     A_h, [dstend1, -8]
        ret

        .p2align 3
        /* Copy 4-7 bytes.  */
L(copy8):
        tbz     count, 2, L(copy4)
        ldr     A_lw, [src]
        ldr     B_lw, [srcend1, -4]
        str     A_lw, [dstin]
        str     B_lw, [dstend1, -4]
        ret

        /* Copy 0..3 bytes using a branchless sequence.  */
L(copy4):
        cbz     count, L(copy0)
        lsr     tmp1, count, 1
        ldrb    A_lw, [src]
        ldrb    C_lw, [srcend1, -1]
        ldrb    B_lw, [src, tmp1]
        strb    A_lw, [dstin]
        strb    B_lw, [dstin, tmp1]
        strb    C_lw, [dstend1, -1]
L(copy0):
        ret

        .p2align 4
        /* Medium copies: 33..128 bytes.  */
L(copy32_128):
        ldp     A_l, A_h, [src]
        ldp     B_l, B_h, [src, 16]
        ldp     C_l, C_h, [srcend1, -32]
        ldp     D_l, D_h, [srcend1, -16]
        cmp     count, 64
        b.hi    L(copy128)
        stp     A_l, A_h, [dstin]
        stp     B_l, B_h, [dstin, 16]
        stp     C_l, C_h, [dstend1, -32]
        stp     D_l, D_h, [dstend1, -16]
        ret

        .p2align 4
        /* Copy 65..128 bytes.  */
L(copy128):
        ldp     E_l, E_h, [src, 32]
        ldp     F_l, F_h, [src, 48]
        cmp     count, 96
        b.ls    L(copy96)
        ldp     G_l, G_h, [srcend1, -64]
        ldp     H_l, H_h, [srcend1, -48]
        stp     G_l, G_h, [dstend1, -64]
        stp     H_l, H_h, [dstend1, -48]
L(copy96):
        stp     A_l, A_h, [dstin]
        stp     B_l, B_h, [dstin, 16]
        stp     E_l, E_h, [dstin, 32]
        stp     F_l, F_h, [dstin, 48]
        stp     C_l, C_h, [dstend1, -32]
        stp     D_l, D_h, [dstend1, -16]
        ret

        .p2align 4
        /* Copy more than 128 bytes.  */
L(copy_long):
        /* Use backwards copy if there is an overlap.  */
        sub     tmp1, dstin, src
        cbz     tmp1, L(copy0)
        cmp     tmp1, count
        b.lo    L(copy_long_backwards)

        /* Copy 16 bytes and then align dst to 16-byte alignment.  */

        ldp     D_l, D_h, [src]
        and     tmp1, dstin, 15
        bic     dst, dstin, 15
        sub     src, src, tmp1
        add     count, count, tmp1      /* Count is now 16 too large.  */
        ldp     A_l, A_h, [src, 16]
        stp     D_l, D_h, [dstin]
        ldp     B_l, B_h, [src, 32]
        ldp     C_l, C_h, [src, 48]
        ldp     D_l, D_h, [src, 64]!
        subs    count, count, 128 + 16  /* Test and readjust count.  */
        b.ls    L(copy64_from_end)
L(loop64):
        stp     A_l, A_h, [dst, 16]
        ldp     A_l, A_h, [src, 16]
        stp     B_l, B_h, [dst, 32]
        ldp     B_l, B_h, [src, 32]
        stp     C_l, C_h, [dst, 48]
        ldp     C_l, C_h, [src, 48]
        stp     D_l, D_h, [dst, 64]!
        ldp     D_l, D_h, [src, 64]!
        subs    count, count, 64
        b.hi    L(loop64)

        /* Write the last iteration and copy 64 bytes from the end.  */
L(copy64_from_end):
        ldp     E_l, E_h, [srcend1, -64]
        stp     A_l, A_h, [dst, 16]
        ldp     A_l, A_h, [srcend1, -48]
        stp     B_l, B_h, [dst, 32]
        ldp     B_l, B_h, [srcend1, -32]
        stp     C_l, C_h, [dst, 48]
        ldp     C_l, C_h, [srcend1, -16]
        stp     D_l, D_h, [dst, 64]
        stp     E_l, E_h, [dstend1, -64]
        stp     A_l, A_h, [dstend1, -48]
        stp     B_l, B_h, [dstend1, -32]
        stp     C_l, C_h, [dstend1, -16]
        ret

        .p2align 4

        /* Large backwards copy for overlapping copies.
           Copy 16 bytes and then align dst to 16-byte alignment.  */
L(copy_long_backwards):
        ldp     D_l, D_h, [srcend1, -16]
        and     tmp1, dstend1, 15
        sub     srcend1, srcend1, tmp1
        sub     count, count, tmp1
        ldp     A_l, A_h, [srcend1, -16]
        stp     D_l, D_h, [dstend1, -16]
        ldp     B_l, B_h, [srcend1, -32]
        ldp     C_l, C_h, [srcend1, -48]
        ldp     D_l, D_h, [srcend1, -64]!
        sub     dstend1, dstend1, tmp1
        subs    count, count, 128
        b.ls    L(copy64_from_start)

L(loop64_backwards):
        stp     A_l, A_h, [dstend1, -16]
        ldp     A_l, A_h, [srcend1, -16]
        stp     B_l, B_h, [dstend1, -32]
        ldp     B_l, B_h, [srcend1, -32]
        stp     C_l, C_h, [dstend1, -48]
        ldp     C_l, C_h, [srcend1, -48]
        stp     D_l, D_h, [dstend1, -64]!
        ldp     D_l, D_h, [srcend1, -64]!
        subs    count, count, 64
        b.hi    L(loop64_backwards)

        /* Write the last iteration and copy 64 bytes from the start.  */
L(copy64_from_start):
        ldp     G_l, G_h, [src, 48]
        stp     A_l, A_h, [dstend1, -16]
        ldp     A_l, A_h, [src, 32]
        stp     B_l, B_h, [dstend1, -32]
        ldp     B_l, B_h, [src, 16]
        stp     C_l, C_h, [dstend1, -48]
        ldp     C_l, C_h, [src]
        stp     D_l, D_h, [dstend1, -64]
        stp     G_l, G_h, [dstin, 48]
        stp     A_l, A_h, [dstin, 32]
        stp     B_l, B_h, [dstin, 16]
        stp     C_l, C_h, [dstin]
        ret
END_COMPILERRT_OUTLINE_FUNCTION(__arm_sc_memcpy)

DEFINE_COMPILERRT_FUNCTION_ALIAS(__arm_sc_memmove, __arm_sc_memcpy)


//
//  __arm_sc_memset
//

#define dstin    x0
#define val      x1
#define valw     w1
#define count    x2
#define dst      x3
#define dstend2  x4
#define zva_val  x5

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_sc_memset)
#ifdef __ARM_FEATURE_SVE
        mov     z0.b, valw
#else
        bfi valw, valw, #8, #8
        bfi valw, valw, #16, #16
        bfi val, val, #32, #32
        fmov d0, val
        fmov v0.d[1], val
#endif
        add     dstend2, dstin, count

        cmp     count, 96
        b.hi    L(set_long)
        cmp     count, 16
        b.hs    L(set_medium)
        mov     val, v0.D[0]

        /* Set 0..15 bytes.  */
        tbz     count, 3, 1f
        str     val, [dstin]
        str     val, [dstend2, -8]
        ret
        nop
1:      tbz     count, 2, 2f
        str     valw, [dstin]
        str     valw, [dstend2, -4]
        ret
2:      cbz     count, 3f
        strb    valw, [dstin]
        tbz     count, 1, 3f
        strh    valw, [dstend2, -2]
3:      ret

        /* Set 17..96 bytes.  */
L(set_medium):
        str     q0, [dstin]
        tbnz    count, 6, L(set96)
        str     q0, [dstend2, -16]
        tbz     count, 5, 1f
        str     q0, [dstin, 16]
        str     q0, [dstend2, -32]
1:      ret

        .p2align 4
        /* Set 64..96 bytes.  Write 64 bytes from the start and
           32 bytes from the end.  */
L(set96):
        str     q0, [dstin, 16]
        stp     q0, q0, [dstin, 32]
        stp     q0, q0, [dstend2, -32]
        ret

        .p2align 4
L(set_long):
        and     valw, valw, 255
        bic     dst, dstin, 15
        str     q0, [dstin]
        cmp     count, 160
        ccmp    valw, 0, 0, hs
        b.ne    L(no_zva)

#ifndef SKIP_ZVA_CHECK
        mrs     zva_val, dczid_el0
        and     zva_val, zva_val, 31
        cmp     zva_val, 4              /* ZVA size is 64 bytes.  */
        b.ne    L(no_zva)
#endif
        str     q0, [dst, 16]
        stp     q0, q0, [dst, 32]
        bic     dst, dst, 63
        sub     count, dstend2, dst      /* Count is now 64 too large.  */
        sub     count, count, 128       /* Adjust count and bias for loop.  */

        .p2align 4
L(zva_loop):
        add     dst, dst, 64
        dc      zva, dst
        subs    count, count, 64
        b.hi    L(zva_loop)
        stp     q0, q0, [dstend2, -64]
        stp     q0, q0, [dstend2, -32]
        ret

L(no_zva):
        sub     count, dstend2, dst      /* Count is 16 too large.  */
        sub     dst, dst, 16            /* Dst is biased by -32.  */
        sub     count, count, 64 + 16   /* Adjust count and bias for loop.  */
L(no_zva_loop):
        stp     q0, q0, [dst, 32]
        stp     q0, q0, [dst, 64]!
        subs    count, count, 64
        b.hi    L(no_zva_loop)
        stp     q0, q0, [dstend2, -64]
        stp     q0, q0, [dstend2, -32]
        ret
END_COMPILERRT_OUTLINE_FUNCTION(__arm_sc_memset)

#endif // __aarch64__
