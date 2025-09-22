#include "../builtins/assembly.h"

    .syntax unified
    .arch armv6t2
    .fpu vfpv2
    .code 32
    .global _ZN6__xray19XRayPatchedFunctionE

    @ Word-aligned function entry point
    .p2align 2
    @ Let C/C++ see the symbol
    .global __xray_FunctionEntry
    .hidden __xray_FunctionEntry
    @ It preserves all registers except r0, r12(ip), r14(lr) and r15(pc)
    @ Assume that "q" part of the floating-point registers is not used
    @   for passing parameters to C/C++ functions.
    .type __xray_FunctionEntry, %function
    @ In C++ it is void extern "C" __xray_FunctionEntry(uint32_t FuncId) with
    @   FuncId passed in r0 register.
__xray_FunctionEntry:
    PUSH {r1-r3,lr}
    @ Save floating-point parameters of the instrumented function
    VPUSH {d0-d7}
    MOVW r1, #:lower16:_ZN6__xray19XRayPatchedFunctionE - (. + 16)
    MOVT r1, #:upper16:_ZN6__xray19XRayPatchedFunctionE - (. + 12)
    LDR r2, [pc, r1]
    @ Handler address is nullptr if handler is not set
    CMP r2, #0
    BEQ FunctionEntry_restore
    @ Function ID is already in r0 (the first parameter).
    @ r1=0 means that we are tracing an entry event
    MOV r1, #0
    @ Call the handler with 2 parameters in r0 and r1
    BLX r2
FunctionEntry_restore:
    @ Restore floating-point parameters of the instrumented function
    VPOP {d0-d7}
    POP {r1-r3,pc}

    @ Word-aligned function entry point
    .p2align 2
    @ Let C/C++ see the symbol
	.global __xray_FunctionExit
	.hidden __xray_FunctionExit
	@ Assume that d1-d7 are not used for the return value.
    @ Assume that "q" part of the floating-point registers is not used for the
    @   return value in C/C++.
	.type __xray_FunctionExit, %function
	@ In C++ it is extern "C" void __xray_FunctionExit(uint32_t FuncId) with
    @   FuncId passed in r0 register.
__xray_FunctionExit:
    PUSH {r1-r3,lr}
    @ Save the floating-point return value of the instrumented function
    VPUSH {d0}
    @ Load the handler address
    MOVW r1, #:lower16:_ZN6__xray19XRayPatchedFunctionE - (. + 16)
    MOVT r1, #:upper16:_ZN6__xray19XRayPatchedFunctionE - (. + 12)
    LDR r2, [pc, r1]
    @ Handler address is nullptr if handler is not set
    CMP r2, #0
    BEQ FunctionExit_restore
    @ Function ID is already in r0 (the first parameter).
    @ 1 means that we are tracing an exit event
    MOV r1, #1
    @ Call the handler with 2 parameters in r0 and r1
    BLX r2
FunctionExit_restore:
    @ Restore the floating-point return value of the instrumented function
    VPOP {d0}
    POP {r1-r3,pc}

    @ Word-aligned function entry point
    .p2align 2
    @ Let C/C++ see the symbol
    .global __xray_FunctionTailExit
    .hidden __xray_FunctionTailExit
    @ It preserves all registers except r0, r12(ip), r14(lr) and r15(pc)
    @ Assume that "q" part of the floating-point registers is not used
    @   for passing parameters to C/C++ functions.
    .type __xray_FunctionTailExit, %function
    @ In C++ it is void extern "C" __xray_FunctionTailExit(uint32_t FuncId)
    @   with FuncId passed in r0 register.
__xray_FunctionTailExit:
    PUSH {r1-r3,lr}
    @ Save floating-point parameters of the instrumented function
    VPUSH {d0-d7}
    MOVW r1, #:lower16:_ZN6__xray19XRayPatchedFunctionE - (. + 16)
    MOVT r1, #:upper16:_ZN6__xray19XRayPatchedFunctionE - (. + 12)
    LDR r2, [pc, r1]
    @ Handler address is nullptr if handler is not set
    CMP r2, #0
    BEQ FunctionTailExit_restore
    @ Function ID is already in r0 (the first parameter).
    @ r1=2 means that we are tracing a tail exit event
    @ But before the logging part of XRay is ready, we pretend that here a
    @   normal function exit happens, so we give the handler code 1
    MOV r1, #1
    @ Call the handler with 2 parameters in r0 and r1
    BLX r2
FunctionTailExit_restore:
    @ Restore floating-point parameters of the instrumented function
    VPOP {d0-d7}
    POP {r1-r3,pc}

NO_EXEC_STACK_DIRECTIVE
