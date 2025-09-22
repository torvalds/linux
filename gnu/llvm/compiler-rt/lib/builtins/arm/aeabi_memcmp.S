//===-- aeabi_memcmp.S - EABI memcmp implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//  void __aeabi_memcmp(void *dest, void *src, size_t n) { memcmp(dest, src, n); }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_memcmp)
#ifdef USE_THUMB_1
        push    {r7, lr}
        bl      memcmp
        pop     {r7, pc}
#else
        b       memcmp
#endif
END_COMPILERRT_FUNCTION(__aeabi_memcmp)

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcmp4, __aeabi_memcmp)
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memcmp8, __aeabi_memcmp)

NO_EXEC_STACK_DIRECTIVE

