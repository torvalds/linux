//===-- xray_trampoline_hexagon.s -------------------------------*- ASM -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This implements the hexagon-specific assembler for the trampolines.
//
//===----------------------------------------------------------------------===//

#include "../builtins/assembly.h"
#include "../sanitizer_common/sanitizer_asm.h"

.macro SAVE_REGISTERS
memw(sp+#0)=r0
memw(sp+#4)=r1
memw(sp+#8)=r2
memw(sp+#12)=r3
memw(sp+#16)=r4
.endm
.macro RESTORE_REGISTERS
r0=memw(sp+#0)
r1=memw(sp+#4)
r2=memw(sp+#8)
r3=memw(sp+#12)
r4=memw(sp+#16)
.endm

.macro CALL_PATCHED_FUNC entry_type
	// if (xray::XRayPatchedFunctionE != NULL)
	//     xray::XRayPatchedFunctionE(FuncType);

	r8 = #ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)

	// The patched sled puts the function type
	// into r6.  Move it into r0 to pass it to
	// the patched function.
	{ r0 = r6
          r1 = \entry_type
          p0 = !cmp.eq(r8, #0)
	  if (p0) callr r8 }
.endm

	.text
	.globl ASM_SYMBOL(__xray_FunctionEntry)
	ASM_HIDDEN(__xray_FunctionEntry)
	ASM_TYPE_FUNCTION(__xray_FunctionEntry)
# LLVM-MCA-BEGIN __xray_FunctionEntry
ASM_SYMBOL(__xray_FunctionEntry):
	CFI_STARTPROC
	SAVE_REGISTERS

	CALL_PATCHED_FUNC #0  // XRayEntryType::ENTRY
.Ltmp0:
	RESTORE_REGISTERS
	// return
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionEntry)
	CFI_ENDPROC


	.globl ASM_SYMBOL(__xray_FunctionExit)
	ASM_HIDDEN(__xray_FunctionExit)
	ASM_TYPE_FUNCTION(__xray_FunctionExit)
# LLVM-MCA-BEGIN __xray_FunctionExit
ASM_SYMBOL(__xray_FunctionExit):
	CFI_STARTPROC
	SAVE_REGISTERS

	CALL_PATCHED_FUNC #1  // XRayEntryType::EXIT
.Ltmp1:
	RESTORE_REGISTERS
	// return
	jumpr r31
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionExit)
	CFI_ENDPROC


	.globl ASM_SYMBOL(__xray_FunctionTailExit)
	ASM_HIDDEN(__xray_FunctionTailExit)
	ASM_TYPE_FUNCTION(__xray_FunctionTailExit)
# LLVM-MCA-BEGIN __xray_FunctionTailExit
ASM_SYMBOL(__xray_FunctionTailExit):
	CFI_STARTPROC
	SAVE_REGISTERS

	CALL_PATCHED_FUNC #2  // XRayEntryType::TAIL
.Ltmp2:
	RESTORE_REGISTERS
	// return
	jumpr r31
# LLVM-MCA-END
	ASM_SIZE(__xray_FunctionTailExit)
	CFI_ENDPROC
