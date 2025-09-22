//===-- ARMCallingConv.td - Calling Conventions for ARM ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for ARM architecture.
//===----------------------------------------------------------------------===//

/// CCIfAlign - Match of the original alignment of the arg
class CCIfAlign<string Align, CCAction A>:
  CCIf<!strconcat("ArgFlags.getNonZeroOrigAlign() == ", Align), A>;

//===----------------------------------------------------------------------===//
// ARM APCS Calling Convention
//===----------------------------------------------------------------------===//
let Entry = 1 in
def CC_ARM_APCS : CallingConv<[

  // Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is passed in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  // f64 and v2f64 are passed in adjacent GPRs, possibly split onto the stack
  CCIfType<[f64, v2f64], CCCustom<"CC_ARM_APCS_Custom_f64">>,

  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,

  CCIfType<[i32], CCAssignToStack<4, 4>>,
  CCIfType<[f64], CCAssignToStack<8, 4>>,
  CCIfType<[v2f64], CCAssignToStack<16, 4>>
]>;

let Entry = 1 in
def RetCC_ARM_APCS : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is returned in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  CCIfType<[f64, v2f64], CCCustom<"RetCC_ARM_APCS_Custom_f64">>,

  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[R0, R2], [R1, R3]>>
]>;

//===----------------------------------------------------------------------===//
// ARM APCS Calling Convention for FastCC (when VFP2 or later is available)
//===----------------------------------------------------------------------===//
let Entry = 1 in
def FastCC_ARM_APCS : CallingConv<[
  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  CCIfType<[v2f64], CCAssignToReg<[Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7, S8,
                                 S9, S10, S11, S12, S13, S14, S15]>>,

  // CPRCs may be allocated to co-processor registers or the stack - they
  // may never be allocated to core registers.
  CCIfType<[f32], CCAssignToStackWithShadow<4, 4, [Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToStackWithShadow<8, 4, [Q0, Q1, Q2, Q3]>>,
  CCIfType<[v2f64], CCAssignToStackWithShadow<16, 4, [Q0, Q1, Q2, Q3]>>,

  CCDelegateTo<CC_ARM_APCS>
]>;

let Entry = 1 in
def RetFastCC_ARM_APCS : CallingConv<[
  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  CCIfType<[v2f64], CCAssignToReg<[Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7, S8,
                                 S9, S10, S11, S12, S13, S14, S15]>>,
  CCDelegateTo<RetCC_ARM_APCS>
]>;

//===----------------------------------------------------------------------===//
// ARM APCS Calling Convention for GHC
//===----------------------------------------------------------------------===//

let Entry = 1 in
def CC_ARM_APCS_GHC : CallingConv<[
  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  CCIfType<[v2f64], CCAssignToReg<[Q4, Q5]>>,
  CCIfType<[f64], CCAssignToReg<[D8, D9, D10, D11]>>,
  CCIfType<[f32], CCAssignToReg<[S16, S17, S18, S19, S20, S21, S22, S23]>>,

  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Pass in STG registers: Base, Sp, Hp, R1, R2, R3, R4, SpLim
  CCIfType<[i32], CCAssignToReg<[R4, R5, R6, R7, R8, R9, R10, R11]>>
]>;

//===----------------------------------------------------------------------===//
// ARM AAPCS (EABI) Calling Convention, common parts
//===----------------------------------------------------------------------===//

def CC_ARM_AAPCS_Common : CallingConv<[

  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i64/f64 is passed in even pairs of GPRs
  // i64 is 8-aligned i32 here, so we may need to eat R1 as a pad register
  // (and the same is true for f64 if VFP is not enabled)
  CCIfType<[i32], CCIfAlign<"8", CCAssignToRegWithShadow<[R0, R2], [R0, R1]>>>,
  CCIfType<[i32], CCIf<"ArgFlags.getNonZeroOrigAlign() != Align(8)",
                       CCAssignToReg<[R0, R1, R2, R3]>>>,

  CCIfType<[i32], CCIfAlign<"8", CCAssignToStackWithShadow<4, 8, [R0, R1, R2, R3]>>>,
  CCIfType<[i32], CCAssignToStackWithShadow<4, 4, [R0, R1, R2, R3]>>,
  CCIfType<[f16, bf16, f32], CCAssignToStackWithShadow<4, 4, [Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToStackWithShadow<8, 8, [Q0, Q1, Q2, Q3]>>,
  CCIfType<[v2f64], CCIfAlign<"16",
           CCAssignToStackWithShadow<16, 16, [Q0, Q1, Q2, Q3]>>>,
  CCIfType<[v2f64], CCAssignToStackWithShadow<16, 8, [Q0, Q1, Q2, Q3]>>
]>;

def RetCC_ARM_AAPCS_Common : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[R0, R2], [R1, R3]>>
]>;

//===----------------------------------------------------------------------===//
// ARM AAPCS (EABI) Calling Convention
//===----------------------------------------------------------------------===//

let Entry = 1 in
def CC_ARM_AAPCS : CallingConv<[
  // Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  // The 'nest' parameter, if any, is passed in R12.
  CCIfNest<CCAssignToReg<[R12]>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is passed in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  CCIfType<[f64, v2f64], CCCustom<"CC_ARM_AAPCS_Custom_f64">>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[f16, bf16], CCCustom<"CC_ARM_AAPCS_Custom_f16">>,
  CCDelegateTo<CC_ARM_AAPCS_Common>
]>;

let Entry = 1 in
def RetCC_ARM_AAPCS : CallingConv<[
  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is returned in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  CCIfType<[f64, v2f64], CCCustom<"RetCC_ARM_AAPCS_Custom_f64">>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[f16, bf16], CCCustom<"CC_ARM_AAPCS_Custom_f16">>,

  CCDelegateTo<RetCC_ARM_AAPCS_Common>
]>;

//===----------------------------------------------------------------------===//
// ARM AAPCS-VFP (EABI) Calling Convention
// Also used for FastCC (when VFP2 or later is available)
//===----------------------------------------------------------------------===//

let Entry = 1 in
def CC_ARM_AAPCS_VFP : CallingConv<[
  // Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is passed in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  // HFAs are passed in a contiguous block of registers, or on the stack
  CCIfConsecutiveRegs<CCCustom<"CC_ARM_AAPCS_Custom_Aggregate">>,

  CCIfType<[v2f64], CCAssignToReg<[Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7, S8,
                                 S9, S10, S11, S12, S13, S14, S15]>>,
  CCIfType<[f16, bf16], CCCustom<"CC_ARM_AAPCS_VFP_Custom_f16">>,
  CCDelegateTo<CC_ARM_AAPCS_Common>
]>;

let Entry = 1 in
def RetCC_ARM_AAPCS_VFP : CallingConv<[
  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v4f16, v4bf16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v8f16, v8bf16, v16i8, v4f32], CCBitConvertToType<v2f64>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i32], CCAssignToReg<[R10]>>>,

  // A SwiftError is returned in R8.
  CCIfSwiftError<CCIfType<[i32], CCAssignToReg<[R8]>>>,

  CCIfType<[v2f64], CCAssignToReg<[Q0, Q1, Q2, Q3]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7, S8,
                                 S9, S10, S11, S12, S13, S14, S15]>>,
  CCIfType<[f16, bf16], CCCustom<"CC_ARM_AAPCS_VFP_Custom_f16">>,
  CCDelegateTo<RetCC_ARM_AAPCS_Common>
]>;


// Windows Control Flow Guard checks take a single argument (the target function
// address) and have no return value.
let Entry = 1 in
def CC_ARM_Win32_CFGuard_Check : CallingConv<[
  CCIfType<[i32], CCAssignToReg<[R0]>>
]>;



//===----------------------------------------------------------------------===//
// Callee-saved register lists.
//===----------------------------------------------------------------------===//

def CSR_NoRegs : CalleeSavedRegs<(add)>;
def CSR_FPRegs : CalleeSavedRegs<(add (sequence "D%u", 0, 31))>;

def CSR_AAPCS : CalleeSavedRegs<(add LR, R11, R10, R9, R8, R7, R6, R5, R4,
                                     (sequence "D%u", 15, 8))>;

// The Windows Control Flow Guard Check function preserves the same registers as
// AAPCS, and also preserves all floating point registers.
def CSR_Win_AAPCS_CFGuard_Check : CalleeSavedRegs<(add LR, R11, R10, R9, R8, R7,
                                     R6, R5, R4, (sequence "D%u", 15, 0))>;

// R8 is used to pass swifterror, remove it from CSR.
def CSR_AAPCS_SwiftError : CalleeSavedRegs<(sub CSR_AAPCS, R8)>;

// R10 is used to pass swiftself, remove it from CSR.
def CSR_AAPCS_SwiftTail : CalleeSavedRegs<(sub CSR_AAPCS, R10)>;

// The order of callee-saved registers needs to match the order we actually push
// them in FrameLowering, because this order is what's used by
// PrologEpilogInserter to allocate frame index slots. So when R7 is the frame
// pointer, we use this ATPCS alternative.
def CSR_ATPCS_SplitPush : CalleeSavedRegs<(add LR, R7, R6, R5, R4,
                                               R11, R10, R9, R8,
                                               (sequence "D%u", 15, 8))>;

def CSR_Win_SplitFP : CalleeSavedRegs<(add R10, R9, R8, R7, R6, R5, R4,
                                               (sequence "D%u", 15, 8),
                                               LR, R11)>;

// R8 is used to pass swifterror, remove it from CSR.
def CSR_ATPCS_SplitPush_SwiftError : CalleeSavedRegs<(sub CSR_ATPCS_SplitPush,
                                                      R8)>;

// R10 is used to pass swifterror, remove it from CSR.
def CSR_ATPCS_SplitPush_SwiftTail : CalleeSavedRegs<(sub CSR_ATPCS_SplitPush,
                                                     R10)>;

// When enforcing an AAPCS compliant frame chain, R11 is used as the frame
// pointer even for Thumb targets, where split pushes are necessary.
// This AAPCS alternative makes sure the frame index slots match the push
// order in that case.
def CSR_AAPCS_SplitPush : CalleeSavedRegs<(add LR, R11,
                                               R7, R6, R5, R4,
                                               R10, R9, R8,
                                               (sequence "D%u", 15, 8))>;

// Constructors and destructors return 'this' in the ARM C++ ABI; since 'this'
// and the pointer return value are both passed in R0 in these cases, this can
// be partially modelled by treating R0 as a callee-saved register
// Only the resulting RegMask is used; the SaveList is ignored
def CSR_AAPCS_ThisReturn : CalleeSavedRegs<(add LR, R11, R10, R9, R8, R7, R6,
                                            R5, R4, (sequence "D%u", 15, 8),
                                            R0)>;

// iOS ABI deviates from ARM standard ABI. R9 is not a callee-saved register.
// Also save R7-R4 first to match the stack frame fixed spill areas.
def CSR_iOS : CalleeSavedRegs<(add LR, R7, R6, R5, R4, (sub CSR_AAPCS, R9))>;

// R8 is used to pass swifterror, remove it from CSR.
def CSR_iOS_SwiftError : CalleeSavedRegs<(sub CSR_iOS, R8)>;

// R10 is used to pass swiftself, remove it from CSR.
def CSR_iOS_SwiftTail : CalleeSavedRegs<(sub CSR_iOS, R10)>;

def CSR_iOS_ThisReturn : CalleeSavedRegs<(add LR, R7, R6, R5, R4,
                                         (sub CSR_AAPCS_ThisReturn, R9))>;

def CSR_iOS_TLSCall
    : CalleeSavedRegs<(add LR, SP, (sub(sequence "R%u", 12, 1), R9, R12),
                      (sequence "D%u", 31, 0))>;

// C++ TLS access function saves all registers except SP. Try to match
// the order of CSRs in CSR_iOS.
def CSR_iOS_CXX_TLS : CalleeSavedRegs<(add CSR_iOS, (sequence "R%u", 12, 1),
                                           (sequence "D%u", 31, 0))>;

// CSRs that are handled by prologue, epilogue.
def CSR_iOS_CXX_TLS_PE : CalleeSavedRegs<(add LR, R12, R11, R7, R5, R4)>;

// CSRs that are handled explicitly via copies.
def CSR_iOS_CXX_TLS_ViaCopy : CalleeSavedRegs<(sub CSR_iOS_CXX_TLS,
                                                   CSR_iOS_CXX_TLS_PE)>;

// The "interrupt" attribute is used to generate code that is acceptable in
// exception-handlers of various kinds. It makes us use a different return
// instruction (handled elsewhere) and affects which registers we must return to
// our "caller" in the same state as we receive them.

// For most interrupts, all registers except SP and LR are shared with
// user-space. We mark LR to be saved anyway, since this is what the ARM backend
// generally does rather than tracking its liveness as a normal register.
def CSR_GenericInt : CalleeSavedRegs<(add LR, (sequence "R%u", 12, 0))>;

// The fast interrupt handlers have more private state and get their own copies
// of R8-R12, in addition to SP and LR. As before, mark LR for saving too.

// FIXME: we mark R11 as callee-saved since it's often the frame-pointer, and
// current frame lowering expects to encounter it while processing callee-saved
// registers.
def CSR_FIQ : CalleeSavedRegs<(add LR, R11, (sequence "R%u", 7, 0))>;


