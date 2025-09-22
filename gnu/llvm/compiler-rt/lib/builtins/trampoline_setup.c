//===----- trampoline_setup.c - Implement __trampoline_setup -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

extern void __clear_cache(void *start, void *end);

// The ppc compiler generates calls to __trampoline_setup() when creating
// trampoline functions on the stack for use with nested functions.
// This function creates a custom 40-byte trampoline function on the stack
// which loads r11 with a pointer to the outer function's locals
// and then jumps to the target nested function.

#if __powerpc__ && !defined(__powerpc64__)
COMPILER_RT_ABI void __trampoline_setup(uint32_t *trampOnStack,
                                        int trampSizeAllocated,
                                        const void *realFunc, void *localsPtr) {
  // should never happen, but if compiler did not allocate
  // enough space on stack for the trampoline, abort
  if (trampSizeAllocated < 40)
    compilerrt_abort();

  // create trampoline
  trampOnStack[0] = 0x7c0802a6; // mflr r0
  trampOnStack[1] = 0x4800000d; // bl Lbase
  trampOnStack[2] = (uint32_t)realFunc;
  trampOnStack[3] = (uint32_t)localsPtr;
  trampOnStack[4] = 0x7d6802a6; // Lbase: mflr r11
  trampOnStack[5] = 0x818b0000; // lwz    r12,0(r11)
  trampOnStack[6] = 0x7c0803a6; // mtlr r0
  trampOnStack[7] = 0x7d8903a6; // mtctr r12
  trampOnStack[8] = 0x816b0004; // lwz    r11,4(r11)
  trampOnStack[9] = 0x4e800420; // bctr

  // clear instruction cache
  __clear_cache(trampOnStack, &trampOnStack[10]);
}
#endif // __powerpc__ && !defined(__powerpc64__)

// The AArch64 compiler generates calls to __trampoline_setup() when creating
// trampoline functions on the stack for use with nested functions.
// This function creates a custom 36-byte trampoline function on the stack
// which loads x18 with a pointer to the outer function's locals
// and then jumps to the target nested function.
// Note: x18 is a reserved platform register on Windows and macOS.

#if defined(__aarch64__) && defined(__ELF__)
COMPILER_RT_ABI void __trampoline_setup(uint32_t *trampOnStack,
                                        int trampSizeAllocated,
                                        const void *realFunc, void *localsPtr) {
  // This should never happen, but if compiler did not allocate
  // enough space on stack for the trampoline, abort.
  if (trampSizeAllocated < 36)
    compilerrt_abort();

  // create trampoline
  // Load realFunc into x17. mov/movk 16 bits at a time.
  trampOnStack[0] =
      0xd2800000u | ((((uint64_t)realFunc >> 0) & 0xffffu) << 5) | 0x11;
  trampOnStack[1] =
      0xf2a00000u | ((((uint64_t)realFunc >> 16) & 0xffffu) << 5) | 0x11;
  trampOnStack[2] =
      0xf2c00000u | ((((uint64_t)realFunc >> 32) & 0xffffu) << 5) | 0x11;
  trampOnStack[3] =
      0xf2e00000u | ((((uint64_t)realFunc >> 48) & 0xffffu) << 5) | 0x11;
  // Load localsPtr into x18
  trampOnStack[4] =
      0xd2800000u | ((((uint64_t)localsPtr >> 0) & 0xffffu) << 5) | 0x12;
  trampOnStack[5] =
      0xf2a00000u | ((((uint64_t)localsPtr >> 16) & 0xffffu) << 5) | 0x12;
  trampOnStack[6] =
      0xf2c00000u | ((((uint64_t)localsPtr >> 32) & 0xffffu) << 5) | 0x12;
  trampOnStack[7] =
      0xf2e00000u | ((((uint64_t)localsPtr >> 48) & 0xffffu) << 5) | 0x12;
  trampOnStack[8] = 0xd61f0220; // br x17

  // Clear instruction cache.
  __clear_cache(trampOnStack, &trampOnStack[9]);
}
#endif // defined(__aarch64__) && !defined(__APPLE__) && !defined(_WIN64)
