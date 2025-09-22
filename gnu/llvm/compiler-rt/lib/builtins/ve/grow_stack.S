// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../assembly.h"

// grow_stack routine
// This routine is VE specific
// https://www.nec.com/en/global/prod/hpc/aurora/document/VE-ABI_v1.1.pdf

// destroy %s62 and %s63 only

#ifdef __ve__

.text
.p2align        4
DEFINE_COMPILERRT_FUNCTION(__ve_grow_stack)
        subu.l          %sp, %sp, %s0           # sp -= alloca size
        and             %sp, -16, %sp           # align sp
        brge.l.t        %sp, %sl, 1f
        ld              %s63, 0x18(,%tp)        # load param area
        lea             %s62, 0x13b             # syscall # of grow
        shm.l           %s62, 0x0(%s63)         # stored at addr:0
        shm.l           %sl, 0x8(%s63)          # old limit at addr:8
        shm.l           %sp, 0x10(%s63)         # new limit at addr:16
        monc
1:
        b.l             (,%lr)
END_COMPILERRT_FUNCTION(__ve_grow_stack)

#endif // __ve__
