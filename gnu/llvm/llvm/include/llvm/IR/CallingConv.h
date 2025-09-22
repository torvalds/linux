//===- llvm/CallingConv.h - LLVM Calling Conventions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines LLVM's set of calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CALLINGCONV_H
#define LLVM_IR_CALLINGCONV_H

namespace llvm {

/// CallingConv Namespace - This namespace contains an enum with a value for
/// the well-known calling conventions.
///
namespace CallingConv {

  /// LLVM IR allows to use arbitrary numbers as calling convention identifiers.
  using ID = unsigned;

  /// A set of enums which specify the assigned numeric values for known llvm
  /// calling conventions.
  /// LLVM Calling Convention Representation
  enum {
    /// The default llvm calling convention, compatible with C. This convention
    /// is the only one that supports varargs calls. As with typical C calling
    /// conventions, the callee/caller have to tolerate certain amounts of
    /// prototype mismatch.
    C = 0,

    // Generic LLVM calling conventions. None of these support varargs calls,
    // and all assume that the caller and callee prototype exactly match.

    /// Attempts to make calls as fast as possible (e.g. by passing things in
    /// registers).
    Fast = 8,

    /// Attempts to make code in the caller as efficient as possible under the
    /// assumption that the call is not commonly executed. As such, these calls
    /// often preserve all registers so that the call does not break any live
    /// ranges in the caller side.
    Cold = 9,

    /// Used by the Glasgow Haskell Compiler (GHC).
    GHC = 10,

    /// Used by the High-Performance Erlang Compiler (HiPE).
    HiPE = 11,

    /// OBSOLETED - Used for stack based JavaScript calls
    // WebKit_JS = 12,

    /// Used for dynamic register based calls (e.g. stackmap and patchpoint
    /// intrinsics).
    AnyReg = 13,

    /// Used for runtime calls that preserves most registers.
    PreserveMost = 14,

    /// Used for runtime calls that preserves (almost) all registers.
    PreserveAll = 15,

    /// Calling convention for Swift.
    Swift = 16,

    /// Used for access functions.
    CXX_FAST_TLS = 17,

    /// Attemps to make calls as fast as possible while guaranteeing that tail
    /// call optimization can always be performed.
    Tail = 18,

    /// Special calling convention on Windows for calling the Control Guard
    /// Check ICall funtion. The function takes exactly one argument (address of
    /// the target function) passed in the first argument register, and has no
    /// return value. All register values are preserved.
    CFGuard_Check = 19,

    /// This follows the Swift calling convention in how arguments are passed
    /// but guarantees tail calls will be made by making the callee clean up
    /// their stack.
    SwiftTail = 20,

    /// Used for runtime calls that preserves none general registers.
    PreserveNone = 21,

    /// This is the start of the target-specific calling conventions, e.g.
    /// fastcall and thiscall on X86.
    FirstTargetCC = 64,

    /// stdcall is mostly used by the Win32 API. It is basically the same as the
    /// C convention with the difference in that the callee is responsible for
    /// popping the arguments from the stack.
    X86_StdCall = 64,

    /// 'fast' analog of X86_StdCall. Passes first two arguments in ECX:EDX
    /// registers, others - via stack. Callee is responsible for stack cleaning.
    X86_FastCall = 65,

    /// ARM Procedure Calling Standard (obsolete, but still used on some
    /// targets).
    ARM_APCS = 66,

    /// ARM Architecture Procedure Calling Standard calling convention (aka
    /// EABI). Soft float variant.
    ARM_AAPCS = 67,

    /// Same as ARM_AAPCS, but uses hard floating point ABI.
    ARM_AAPCS_VFP = 68,

    /// Used for MSP430 interrupt routines.
    MSP430_INTR = 69,

    /// Similar to X86_StdCall. Passes first argument in ECX, others via stack.
    /// Callee is responsible for stack cleaning. MSVC uses this by default for
    /// methods in its ABI.
    X86_ThisCall = 70,

    /// Call to a PTX kernel. Passes all arguments in parameter space.
    PTX_Kernel = 71,

    /// Call to a PTX device function. Passes all arguments in register or
    /// parameter space.
    PTX_Device = 72,

    /// Used for SPIR non-kernel device functions. No lowering or expansion of
    /// arguments. Structures are passed as a pointer to a struct with the
    /// byval attribute. Functions can only call SPIR_FUNC and SPIR_KERNEL
    /// functions. Functions can only have zero or one return values. Variable
    /// arguments are not allowed, except for printf. How arguments/return
    /// values are lowered are not specified. Functions are only visible to the
    /// devices.
    SPIR_FUNC = 75,

    /// Used for SPIR kernel functions. Inherits the restrictions of SPIR_FUNC,
    /// except it cannot have non-void return values, it cannot have variable
    /// arguments, it can also be called by the host or it is externally
    /// visible.
    SPIR_KERNEL = 76,

    /// Used for Intel OpenCL built-ins.
    Intel_OCL_BI = 77,

    /// The C convention as specified in the x86-64 supplement to the System V
    /// ABI, used on most non-Windows systems.
    X86_64_SysV = 78,

    /// The C convention as implemented on Windows/x86-64 and AArch64. It
    /// differs from the more common \c X86_64_SysV convention in a number of
    /// ways, most notably in that XMM registers used to pass arguments are
    /// shadowed by GPRs, and vice versa. On AArch64, this is identical to the
    /// normal C (AAPCS) calling convention for normal functions, but floats are
    /// passed in integer registers to variadic functions.
    Win64 = 79,

    /// MSVC calling convention that passes vectors and vector aggregates in SSE
    /// registers.
    X86_VectorCall = 80,

    /// Placeholders for HHVM calling conventions (deprecated, removed).
    DUMMY_HHVM = 81,
    DUMMY_HHVM_C = 82,

    /// x86 hardware interrupt context. Callee may take one or two parameters,
    /// where the 1st represents a pointer to hardware context frame and the 2nd
    /// represents hardware error code, the presence of the later depends on the
    /// interrupt vector taken. Valid for both 32- and 64-bit subtargets.
    X86_INTR = 83,

    /// Used for AVR interrupt routines.
    AVR_INTR = 84,

    /// Used for AVR signal routines.
    AVR_SIGNAL = 85,

    /// Used for special AVR rtlib functions which have an "optimized"
    /// convention to preserve registers.
    AVR_BUILTIN = 86,

    /// Used for Mesa vertex shaders, or AMDPAL last shader stage before
    /// rasterization (vertex shader if tessellation and geometry are not in
    /// use, or otherwise copy shader if one is needed).
    AMDGPU_VS = 87,

    /// Used for Mesa/AMDPAL geometry shaders.
    AMDGPU_GS = 88,

    /// Used for Mesa/AMDPAL pixel shaders.
    AMDGPU_PS = 89,

    /// Used for Mesa/AMDPAL compute shaders.
    AMDGPU_CS = 90,

    /// Used for AMDGPU code object kernels.
    AMDGPU_KERNEL = 91,

    /// Register calling convention used for parameters transfer optimization
    X86_RegCall = 92,

    /// Used for Mesa/AMDPAL hull shaders (= tessellation control shaders).
    AMDGPU_HS = 93,

    /// Used for special MSP430 rtlib functions which have an "optimized"
    /// convention using additional registers.
    MSP430_BUILTIN = 94,

    /// Used for AMDPAL vertex shader if tessellation is in use.
    AMDGPU_LS = 95,

    /// Used for AMDPAL shader stage before geometry shader if geometry is in
    /// use. So either the domain (= tessellation evaluation) shader if
    /// tessellation is in use, or otherwise the vertex shader.
    AMDGPU_ES = 96,

    /// Used between AArch64 Advanced SIMD functions
    AArch64_VectorCall = 97,

    /// Used between AArch64 SVE functions
    AArch64_SVE_VectorCall = 98,

    /// For emscripten __invoke_* functions. The first argument is required to
    /// be the function ptr being indirectly called. The remainder matches the
    /// regular calling convention.
    WASM_EmscriptenInvoke = 99,

    /// Used for AMD graphics targets.
    AMDGPU_Gfx = 100,

    /// Used for M68k interrupt routines.
    M68k_INTR = 101,

    /// Preserve X0-X13, X19-X29, SP, Z0-Z31, P0-P15.
    AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0 = 102,

    /// Preserve X2-X15, X19-X29, SP, Z0-Z31, P0-P15.
    AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2 = 103,

    /// Used on AMDGPUs to give the middle-end more control over argument
    /// placement.
    AMDGPU_CS_Chain = 104,

    /// Used on AMDGPUs to give the middle-end more control over argument
    /// placement. Preserves active lane values for input VGPRs.
    AMDGPU_CS_ChainPreserve = 105,

    /// Used for M68k rtd-based CC (similar to X86's stdcall).
    M68k_RTD = 106,

    /// Used by GraalVM. Two additional registers are reserved.
    GRAAL = 107,

    /// Calling convention used in the ARM64EC ABI to implement calls between
    /// x64 code and thunks. This is basically the x64 calling convention using
    /// ARM64 register names. The first parameter is mapped to x9.
    ARM64EC_Thunk_X64 = 108,

    /// Calling convention used in the ARM64EC ABI to implement calls between
    /// ARM64 code and thunks. This is just the ARM64 calling convention,
    /// except that the first parameter is mapped to x9.
    ARM64EC_Thunk_Native = 109,

    /// Calling convention used for RISC-V V-extension.
    RISCV_VectorCall = 110,

    /// Preserve X1-X15, X19-X29, SP, Z0-Z31, P0-P15.
    AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1 = 111,

    /// The highest possible ID. Must be some 2^k - 1.
    MaxID = 1023
  };

} // end namespace CallingConv

} // end namespace llvm

#endif // LLVM_IR_CALLINGCONV_H
