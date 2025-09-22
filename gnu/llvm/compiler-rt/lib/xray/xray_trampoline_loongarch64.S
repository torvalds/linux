//===-- xray_trampoline_loongarch64.s ---------------------------*- ASM -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This implements the loongarch-specific assembler for the trampolines.
//
//===----------------------------------------------------------------------===//

#include "../sanitizer_common/sanitizer_asm.h"

#define FROM_0_TO_7 0,1,2,3,4,5,6,7
#define FROM_7_TO_0 7,6,5,4,3,2,1,0

.macro SAVE_ARG_REGISTERS
  .irp i,FROM_7_TO_0
    st.d  $a\i, $sp, (8 * 8 + 8 * \i)
  .endr
  .irp i,FROM_7_TO_0
    fst.d $f\i, $sp, (8 * \i)
  .endr
.endm

.macro RESTORE_ARG_REGISTERS
  .irp i,FROM_0_TO_7
    fld.d $f\i, $sp, (8 * \i)
  .endr
  .irp i,FROM_0_TO_7
    ld.d  $a\i, $sp, (8 * 8 + 8 * \i)
  .endr
.endm

.macro SAVE_RET_REGISTERS
  st.d    $a1, $sp, 24
  st.d    $a0, $sp, 16
  fst.d   $f1, $sp, 8
  fst.d   $f0, $sp, 0
.endm

.macro RESTORE_RET_REGISTERS
  fld.d   $f0, $sp, 0
  fld.d   $f1, $sp, 8
  ld.d    $a0, $sp, 16
  ld.d    $a1, $sp, 24
.endm

  .text
  .file "xray_trampoline_loongarch64.S"
  .globl ASM_SYMBOL(__xray_FunctionEntry)
  ASM_HIDDEN(__xray_FunctionEntry)
  .p2align 2
  ASM_TYPE_FUNCTION(__xray_FunctionEntry)
ASM_SYMBOL(__xray_FunctionEntry):
  .cfi_startproc
  // Save argument registers before doing any actual work.
  .cfi_def_cfa_offset 136
  addi.d  $sp, $sp, -136
  st.d    $ra, $sp, 128
  .cfi_offset 1, -8
  SAVE_ARG_REGISTERS

  la.got  $t2, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ld.d    $t2, $t2, 0

  beqz    $t2, FunctionEntry_restore

  // a1=0 means that we are tracing an entry event.
  move    $a1, $zero
  // Function ID is in t1 (the first parameter).
  move    $a0, $t1
  jirl    $ra, $t2, 0

FunctionEntry_restore:
  // Restore argument registers.
  RESTORE_ARG_REGISTERS
  ld.d    $ra, $sp, 128
  addi.d  $sp, $sp, 136
  ret
FunctionEntry_end:
  ASM_SIZE(__xray_FunctionEntry)
  .cfi_endproc

  .text
  .globl ASM_SYMBOL(__xray_FunctionExit)
  ASM_HIDDEN(__xray_FunctionExit)
  .p2align 2
  ASM_TYPE_FUNCTION(__xray_FunctionExit)
ASM_SYMBOL(__xray_FunctionExit):
  .cfi_startproc
  // Save return registers before doing any actual work.
  .cfi_def_cfa_offset 48
  addi.d  $sp, $sp, -48
  st.d    $ra, $sp, 40
  .cfi_offset 1, -8
  st.d    $fp, $sp, 32
  SAVE_RET_REGISTERS

  la.got  $t2, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ld.d    $t2, $t2, 0

  beqz    $t2, FunctionExit_restore

  // a1=1 means that we are tracing an exit event.
  li.w    $a1, 1
  // Function ID is in t1 (the first parameter).
  move    $a0, $t1
  jirl    $ra, $t2, 0

FunctionExit_restore:
  // Restore return registers.
  RESTORE_RET_REGISTERS
  ld.d    $fp, $sp, 32
  ld.d    $ra, $sp, 40
  addi.d  $sp, $sp, 48
  ret

FunctionExit_end:
  ASM_SIZE(__xray_FunctionExit)
  .cfi_endproc
