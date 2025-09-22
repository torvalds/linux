#include "../builtins/assembly.h"
#include "../sanitizer_common/sanitizer_asm.h"

.macro SAVE_REGISTERS
  stp x1, x2, [sp, #-16]!
  stp x3, x4, [sp, #-16]!
  stp x5, x6, [sp, #-16]!
  stp x7, x30, [sp, #-16]!
  stp q0, q1, [sp, #-32]!
  stp q2, q3, [sp, #-32]!
  stp q4, q5, [sp, #-32]!
  stp q6, q7, [sp, #-32]!
  // x8 is the indirect result register and needs to be preserved for the body of the function to use.
  stp x8, x0, [sp, #-16]!
.endm

.macro RESTORE_REGISTERS
  ldp x8, x0, [sp], #16
  ldp q6, q7, [sp], #32
  ldp q4, q5, [sp], #32
  ldp q2, q3, [sp], #32
  ldp q0, q1, [sp], #32
  ldp x7, x30, [sp], #16
  ldp x5, x6, [sp], #16
  ldp x3, x4, [sp], #16
  ldp x1, x2, [sp], #16
.endm

.text
.p2align 2
.global ASM_SYMBOL(__xray_FunctionEntry)
ASM_HIDDEN(__xray_FunctionEntry)
ASM_TYPE_FUNCTION(__xray_FunctionEntry)
ASM_SYMBOL(__xray_FunctionEntry):
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
  add x30, x30, #12
  // Push the registers which may be modified by the handler function.
  SAVE_REGISTERS

  // Load the handler function pointer.
  adrp x2, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ldr x2, [x2, #:lo12:ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)]
  cbz x2, 1f
  // Set w0 to the function ID (w17). Set x1 to XRayEntryType::ENTRY = 0.
  mov w0, w17
  mov x1, #0
  // Call the handler with 2 parameters.
  blr x2
1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_FunctionEntry)

.p2align 2
.global ASM_SYMBOL(__xray_FunctionExit)
ASM_HIDDEN(__xray_FunctionExit)
ASM_TYPE_FUNCTION(__xray_FunctionExit)
ASM_SYMBOL(__xray_FunctionExit):
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
  add x30, x30, #12
  SAVE_REGISTERS

  // Load the handler function pointer into x2.
  adrp x2, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ldr x2, [x2, #:lo12:ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)]
  cbz x2, 1f
  // Set w0 to the function ID (w17). Set x1 to XRayEntryType::EXIT = 1.
  mov w0, w17
  mov x1, #1
  // Call the handler with 2 parameters.
  blr x2
1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_FunctionExit)

.p2align 2
.global ASM_SYMBOL(__xray_FunctionTailExit)
ASM_HIDDEN(__xray_FunctionTailExit)
ASM_TYPE_FUNCTION(__xray_FunctionTailExit)
ASM_SYMBOL(__xray_FunctionTailExit):
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
  add x30, x30, #12
  // Save the registers which may be modified by the handler function.
  SAVE_REGISTERS
  // Load the handler function pointer into x2.
  adrp x2, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ldr x2, [x2, #:lo12:ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)]
  cbz x2, 1f
  // Set w0 to the function ID (w17). Set x1 to XRayEntryType::TAIL = 2.
  mov w0, w17
  mov x1, #2
  // Call the handler with 2 parameters.
  blr x2
1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_FunctionTailExit)

.p2align 2
.global ASM_SYMBOL(__xray_ArgLoggerEntry)
ASM_HIDDEN(__xray_ArgLoggerEntry)
ASM_TYPE_FUNCTION(__xray_ArgLoggerEntry)
ASM_SYMBOL(__xray_ArgLoggerEntry):
  add x30, x30, #12
  // Push the registers which may be modified by the handler function.
  SAVE_REGISTERS

  adrp x8, ASM_SYMBOL(_ZN6__xray13XRayArgLoggerE)
  ldr x8, [x8, #:lo12:ASM_SYMBOL(_ZN6__xray13XRayArgLoggerE)]
  cbnz x8, 2f

  // Load the handler function pointer.
  adrp x8, ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)
  ldr x8, [x8, #:lo12:ASM_SYMBOL(_ZN6__xray19XRayPatchedFunctionE)]
  cbz x8, 1f

2:
  mov x2, x0
  mov x1, #3  // XRayEntryType::LOG_ARGS_ENTRY
  mov w0, w17
  blr x8

1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_ArgLoggerEntry)

// __xray_*Event have default visibility so that they can be referenced by user
// DSOs that do not link against the runtime.
.global ASM_SYMBOL(__xray_CustomEvent)
ASM_TYPE_FUNCTION(__xray_CustomEvent)
ASM_SYMBOL(__xray_CustomEvent):
  SAVE_REGISTERS
  adrp x8, ASM_SYMBOL(_ZN6__xray22XRayPatchedCustomEventE)
  ldr x8, [x8, #:lo12:ASM_SYMBOL(_ZN6__xray22XRayPatchedCustomEventE)]
  cbz x8, 1f
  blr x8
1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_CustomEvent)

.global ASM_SYMBOL(__xray_TypedEvent)
ASM_TYPE_FUNCTION(__xray_TypedEvent)
ASM_SYMBOL(__xray_TypedEvent):
  SAVE_REGISTERS
  adrp x8, ASM_SYMBOL(_ZN6__xray21XRayPatchedTypedEventE)
  ldr x8, [x8, #:lo12:ASM_SYMBOL(_ZN6__xray21XRayPatchedTypedEventE)]
  cbz x8, 1f
  blr x8
1:
  RESTORE_REGISTERS
  ret
ASM_SIZE(__xray_TypedEvent)

NO_EXEC_STACK_DIRECTIVE
