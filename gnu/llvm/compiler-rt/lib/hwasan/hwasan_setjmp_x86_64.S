//===-- hwasan_setjmp_x86_64.S --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// setjmp interceptor for x86_64.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_asm.h"

#if HWASAN_WITH_INTERCEPTORS && defined(__x86_64__)
#include "sanitizer_common/sanitizer_platform.h"

// We want to save the context of the calling function.
// That requires
// 1) No modification of the return address by this function.
// 2) No modification of the stack pointer by this function.
// 3) (no modification of any other saved register, but that's not really going
// to occur, and hence isn't as much of a worry).
//
// There's essentially no way to ensure that the compiler will not modify the
// stack pointer when compiling a C function.
// Hence we have to write this function in assembly.
//
// TODO: Handle Intel CET.

.section .text
.file "hwasan_setjmp_x86_64.S"

.global ASM_WRAPPER_NAME(setjmp)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(setjmp))
ASM_WRAPPER_NAME(setjmp):
  CFI_STARTPROC
  _CET_ENDBR
  xorl %esi, %esi
  jmp	.Linterceptor_sigsetjmp
  CFI_ENDPROC
ASM_SIZE(ASM_WRAPPER_NAME(setjmp))

.global ASM_WRAPPER_NAME(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(sigsetjmp))
ASM_WRAPPER_NAME(sigsetjmp):
.Linterceptor_sigsetjmp:
  CFI_STARTPROC
  _CET_ENDBR

  // Save callee save registers.
  mov %rbx, (0*8)(%rdi)
  mov %rbp, (1*8)(%rdi)
  mov %r12, (2*8)(%rdi)
  mov %r13, (3*8)(%rdi)
  mov %r14, (4*8)(%rdi)
  mov %r15, (5*8)(%rdi)

  // Save SP as it was in caller's frame.
  lea 8(%rsp), %rdx
  mov %rdx, (6*8)(%rdi)

  // Save return address.
  mov (%rsp), %rax
  mov %rax, (7*8)(%rdi)

  jmp __sigjmp_save

  CFI_ENDPROC
ASM_SIZE(ASM_WRAPPER_NAME(sigsetjmp))

ASM_INTERCEPTOR_TRAMPOLINE(sigsetjmp)
ASM_TRAMPOLINE_ALIAS(__sigsetjmp, sigsetjmp)
ASM_INTERCEPTOR_TRAMPOLINE(setjmp)
ASM_TRAMPOLINE_ALIAS(_setjmp, setjmp)
#endif

// We do not need executable stack.
NO_EXEC_STACK_DIRECTIVE
