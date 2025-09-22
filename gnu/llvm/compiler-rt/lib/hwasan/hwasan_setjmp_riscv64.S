//===-- hwasan_setjmp_riscv64.S -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
// setjmp interceptor for risc-v.
// HWAddressSanitizer runtime.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_asm.h"
#include "builtins/assembly.h"

#if HWASAN_WITH_INTERCEPTORS && defined(__riscv) && (__riscv_xlen == 64)
#include "sanitizer_common/sanitizer_platform.h"

// We want to save the context of the calling function.
// That requires
// 1) No modification of the link register by this function.
// 2) No modification of the stack pointer by this function.
// 3) (no modification of any other saved register, but that's not really going
// to occur, and hence isn't as much of a worry).
//
// There's essentially no way to ensure that the compiler will not modify the
// stack pointer when compiling a C function.
// Hence we have to write this function in assembly.

.section .text
.file "hwasan_setjmp_riscv64.S"

.global ASM_WRAPPER_NAME(setjmp)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(setjmp))
ASM_WRAPPER_NAME(setjmp):
  CFI_STARTPROC
  addi x11, x0, 0
  tail ASM_WRAPPER_NAME(sigsetjmp)
  CFI_ENDPROC
ASM_SIZE(ASM_WRAPPER_NAME(setjmp))

.global ASM_WRAPPER_NAME(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(sigsetjmp))
ASM_WRAPPER_NAME(sigsetjmp):
  CFI_STARTPROC
  sd    ra,   0<<3(x10)
  sd    s0,   1<<3(x10)
  sd    s1,   2<<3(x10)
  sd    s2,   3<<3(x10)
  sd    s3,   4<<3(x10)
  sd    s4,   5<<3(x10)
  sd    s5,   6<<3(x10)
  sd    s6,   7<<3(x10)
  sd    s7,   8<<3(x10)
  sd    s8,   9<<3(x10)
  sd    s9,   10<<3(x10)
  sd    s10,  11<<3(x10)
  sd    s11,  12<<3(x10)
  sd    sp,   13<<3(x10)
#if __riscv_float_abi_double
  fsd   fs0,  14<<3(x10)
  fsd   fs1,  15<<3(x10)
  fsd   fs2,  16<<3(x10)
  fsd   fs3,  17<<3(x10)
  fsd   fs4,  18<<3(x10)
  fsd   fs5,  19<<3(x10)
  fsd   fs6,  20<<3(x10)
  fsd   fs7,  21<<3(x10)
  fsd   fs8,  22<<3(x10)
  fsd   fs9,  23<<3(x10)
  fsd   fs10, 24<<3(x10)
  fsd   fs11, 25<<3(x10)
#elif __riscv_float_abi_soft
#else
# error "Unsupported case"
#endif
  // We always have the second argument to __sigjmp_save (savemask) set, since
  // the _setjmp function above has set it for us as `false`.
  // This function is defined in hwasan_interceptors.cc
  tail __sigjmp_save
  CFI_ENDPROC
ASM_SIZE(ASM_WRAPPER_NAME(sigsetjmp))

ASM_INTERCEPTOR_TRAMPOLINE(sigsetjmp)
ASM_TRAMPOLINE_ALIAS(__sigsetjmp, sigsetjmp)
ASM_INTERCEPTOR_TRAMPOLINE(setjmp)
ASM_TRAMPOLINE_ALIAS(_setjmp, setjmp)
#endif

// We do not need executable stack.
NO_EXEC_STACK_DIRECTIVE
