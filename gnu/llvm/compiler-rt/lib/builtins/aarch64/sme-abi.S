// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// This patch implements the support routines for the SME ABI,
// described here:
//  https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#sme-support-routines

#include "../assembly.h"


#if !defined(__APPLE__)
#define TPIDR2_SYMBOL SYMBOL_NAME(__aarch64_has_sme_and_tpidr2_el0)
#define TPIDR2_SYMBOL_OFFSET :lo12:SYMBOL_NAME(__aarch64_has_sme_and_tpidr2_el0)
#define CPU_FEATS_SYMBOL SYMBOL_NAME(__aarch64_cpu_features)
#define CPU_FEATS_SYMBOL_OFFSET :lo12:SYMBOL_NAME(__aarch64_cpu_features)
#else
// MachO requires @page/@pageoff directives because the global is defined
// in a different file. Otherwise this file may fail to build.
#define TPIDR2_SYMBOL SYMBOL_NAME(__aarch64_has_sme_and_tpidr2_el0)@page
#define TPIDR2_SYMBOL_OFFSET SYMBOL_NAME(__aarch64_has_sme_and_tpidr2_el0)@pageoff
#define CPU_FEATS_SYMBOL SYMBOL_NAME(__aarch64_cpu_features)@page
#define CPU_FEATS_SYMBOL_OFFSET SYMBOL_NAME(__aarch64_cpu_features)@pageoff
#endif

.arch armv9-a+sme

// Utility function which calls a system's abort() routine. Because the function
// is streaming-compatible it should disable streaming-SVE mode before calling
// abort(). Note that there is no need to preserve any state before the call,
// because the function does not return.
DEFINE_COMPILERRT_PRIVATE_FUNCTION(do_abort)
  .cfi_startproc
  .variant_pcs SYMBOL_NAME(do_abort)
  BTI_C
  stp  x29, x30, [sp, #-32]!
  cntd x0
  // Store VG to a stack location that we describe with .cfi_offset
  str x0, [sp, #16]
  .cfi_def_cfa_offset 32
  .cfi_offset w30, -24
  .cfi_offset w29, -32
  .cfi_offset 46, -16
  bl  __arm_sme_state
  tbz  x0, #0, 2f
1:
  smstop sm
2:
  // We can't make this into a tail-call because the unwinder would
  // need to restore the value of VG.
  bl  SYMBOL_NAME(abort)
  .cfi_endproc
END_COMPILERRT_FUNCTION(do_abort)

// __arm_sme_state fills the result registers based on a local
// that is set as part of the compiler-rt startup code.
//   __aarch64_has_sme_and_tpidr2_el0
DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_sme_state)
  .variant_pcs __arm_sme_state
  BTI_C
  mov x0, xzr
  mov x1, xzr

  adrp  x16, TPIDR2_SYMBOL
  ldrb w16, [x16, TPIDR2_SYMBOL_OFFSET]
  cbz w16, 1f
0:
  orr x0, x0, #0xC000000000000000
  mrs x16, SVCR
  bfxil x0, x16, #0, #2
  mrs x1, TPIDR2_EL0
1:
  ret
END_COMPILERRT_OUTLINE_FUNCTION(__arm_sme_state)

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_tpidr2_restore)
  .variant_pcs __arm_tpidr2_restore
  BTI_C
  // If TPIDR2_EL0 is nonnull, the subroutine aborts in some platform-specific
  // manner.
  mrs x14, TPIDR2_EL0
  cbnz  x14, 2f

  // If any of the reserved bytes in the first 16 bytes of BLK are nonzero,
  // the subroutine [..] aborts in some platform-defined manner.
  ldrh  w14, [x0, #10]
  cbnz  w14, 2f
  ldr w14, [x0, #12]
  cbnz  w14, 2f

  // If BLK.za_save_buffer is NULL, the subroutine does nothing.
  ldr x16, [x0]
  cbz x16, 1f

  // If BLK.num_za_save_slices is zero, the subroutine does nothing.
  ldrh  w14, [x0, #8]
  cbz x14, 1f

  mov x15, xzr
0:
  ldr za[w15,0], [x16]
  addsvl x16, x16, #1
  add x15, x15, #1
  cmp x14, x15
  b.ne  0b
1:
  ret
2:
  b  SYMBOL_NAME(do_abort)
END_COMPILERRT_OUTLINE_FUNCTION(__arm_tpidr2_restore)

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_tpidr2_save)
  .variant_pcs __arm_tpidr2_restore
  BTI_C
  // If the current thread does not have access to TPIDR2_EL0, the subroutine
  // does nothing.
  adrp  x14, TPIDR2_SYMBOL
  ldrb w14, [x14, TPIDR2_SYMBOL_OFFSET]
  cbz w14, 1f

  // If TPIDR2_EL0 is null, the subroutine does nothing.
  mrs x16, TPIDR2_EL0
  cbz x16, 1f

  // If any of the reserved bytes in the first 16 bytes of the TPIDR2 block are
  // nonzero, the subroutine [..] aborts in some platform-defined manner.
  ldrh  w14, [x16, #10]
  cbnz  w14, 2f
  ldr w14, [x16, #12]
  cbnz  w14, 2f

  // If num_za_save_slices is zero, the subroutine does nothing.
  ldrh  w14, [x16, #8]
  cbz x14, 1f

  // If za_save_buffer is NULL, the subroutine does nothing.
  ldr x16, [x16]
  cbz x16, 1f

  mov x15, xzr
0:
  str za[w15,0], [x16]
  addsvl x16, x16, #1
  add x15, x15, #1
  cmp x14, x15
  b.ne  0b
1:
  ret
2:
  b  SYMBOL_NAME(do_abort)
END_COMPILERRT_OUTLINE_FUNCTION(__arm_tpidr2_save)

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_za_disable)
  .variant_pcs __arm_tpidr2_restore
  BTI_C
  // If the current thread does not have access to SME, the subroutine does
  // nothing.
  adrp  x14, TPIDR2_SYMBOL
  ldrb w14, [x14, TPIDR2_SYMBOL_OFFSET]
  cbz w14, 0f

  // Otherwise, the subroutine behaves as if it did the following:
  // * Call __arm_tpidr2_save.
  stp x29, x30, [sp, #-16]!
  .cfi_def_cfa_offset 16
  mov x29, sp
  .cfi_def_cfa w29, 16
  .cfi_offset w30, -8
  .cfi_offset w29, -16
  bl  __arm_tpidr2_save

  // * Set TPIDR2_EL0 to null.
  msr TPIDR2_EL0, xzr

  // * Set PSTATE.ZA to 0.
  smstop za

  .cfi_def_cfa wsp, 16
  ldp x29, x30, [sp], #16
  .cfi_def_cfa_offset 0
  .cfi_restore w30
  .cfi_restore w29
0:
  ret
END_COMPILERRT_OUTLINE_FUNCTION(__arm_za_disable)

DEFINE_COMPILERRT_OUTLINE_FUNCTION_UNMANGLED(__arm_get_current_vg)
  .variant_pcs __arm_get_current_vg
  BTI_C

  stp     x29, x30, [sp, #-16]!
  .cfi_def_cfa_offset 16
  mov     x29, sp
  .cfi_def_cfa w29, 16
  .cfi_offset w30, -8
  .cfi_offset w29, -16
  adrp    x17, CPU_FEATS_SYMBOL
  ldr     w17, [x17, CPU_FEATS_SYMBOL_OFFSET]
  tbnz    w17, #30, 0f
  adrp    x16, TPIDR2_SYMBOL
  ldrb    w16, [x16, TPIDR2_SYMBOL_OFFSET]
  cbz     w16, 1f
0:
  mov     x18, x1
  bl      __arm_sme_state
  mov     x1, x18
  and     x17, x17, #0x40000000
  bfxil   x17, x0, #0, #1
  cbz     x17, 1f
  cntd    x0
  .cfi_def_cfa wsp, 16
  ldp     x29, x30, [sp], #16
  .cfi_def_cfa_offset 0
  .cfi_restore w30
  .cfi_restore w29
  ret
1:
  mov     x0, xzr
  .cfi_def_cfa wsp, 16
  ldp     x29, x30, [sp], #16
  .cfi_def_cfa_offset 0
  .cfi_restore w30
  .cfi_restore w29
  ret
END_COMPILERRT_OUTLINE_FUNCTION(__arm_get_current_vg)

NO_EXEC_STACK_DIRECTIVE

// GNU property note for BTI and PAC
GNU_PROPERTY_BTI_PAC
