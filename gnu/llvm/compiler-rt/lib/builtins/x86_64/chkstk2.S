// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "../assembly.h"

#ifdef __x86_64__

// _chkstk (_alloca) routine - probe stack between %rsp and (%rsp-%rax) in 4k increments,
// then decrement %rsp by %rax.  Preserves all registers except %rsp and flags.
// This routine is windows specific
// http://msdn.microsoft.com/en-us/library/ms648426.aspx

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__alloca)
        mov    %rcx,%rax        // x64 _alloca is a normal function with parameter in rcx
        // fallthrough
DEFINE_COMPILERRT_FUNCTION(___chkstk)
        push   %rcx
        cmp    $0x1000,%rax
        lea    16(%rsp),%rcx     // rsp before calling this routine -> rcx
        jb     1f
2:
        sub    $0x1000,%rcx
        test   %rcx,(%rcx)
        sub    $0x1000,%rax
        cmp    $0x1000,%rax
        ja     2b
1:
        sub    %rax,%rcx
        test   %rcx,(%rcx)

        lea    8(%rsp),%rax     // load pointer to the return address into rax
        mov    %rcx,%rsp        // install the new top of stack pointer into rsp
        mov    -8(%rax),%rcx    // restore rcx
        push   (%rax)           // push return address onto the stack
        sub    %rsp,%rax        // restore the original value in rax
        ret
END_COMPILERRT_FUNCTION(___chkstk)
END_COMPILERRT_FUNCTION(__alloca)

#endif // __x86_64__
