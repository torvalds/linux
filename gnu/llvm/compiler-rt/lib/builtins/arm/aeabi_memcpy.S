//===-- aeabi_memcpy.S - EABI memcpy implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//  void __aeabi_memcpy(void *dest, void *src, size_t n) { memcpy(dest, src, n); }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_memcpy)
#ifdef USE_THUMB_1
        push    {r7, lr}
        bl      memcpy
        pop     {r7, pc}
#else
        b       memcpy
#endif
END_COMPILERRT_FUNCTION(__aeabi_memcpy)

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcpy4, __aeabi_memcpy)
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcpy8, __aeabi_memcpy)

NO_EXEC_STACK_DIRECTIVE

