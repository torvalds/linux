//=- AArch64CallingConv.td - Calling Conventions for AArch64 -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for AArch64 architecture.
//
//===----------------------------------------------------------------------===//

/// CCIfBigEndian - Match only if we're in big endian mode.
class CCIfBigEndian<CCAction A> :
  CCIf<"State.getMachineFunction().getDataLayout().isBigEndian()", A>;

class CCIfILP32<CCAction A> :
  CCIf<"State.getMachineFunction().getDataLayout().getPointerSize() == 4", A>;

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A>
    : CCIf<!strconcat("State.getMachineFunction()"
                      ".getSubtarget<AArch64Subtarget>().", F),
           A>;

//===----------------------------------------------------------------------===//
// ARM AAPCS64 Calling Convention
//===----------------------------------------------------------------------===//

defvar AArch64_Common = [
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32], CCBitConvertToType<v2i64>>,

  // Big endian vectors must be passed as if they were 1-element vectors so that
  // their lanes are in a consistent order.
  CCIfBigEndian<CCIfType<[v2i32, v2f32, v4i16, v4f16, v4bf16, v8i8],
                         CCBitConvertToType<f64>>>,
  CCIfBigEndian<CCIfType<[v2i64, v2f64, v4i32, v4f32, v8i16, v8f16, v8bf16, v16i8],
                         CCBitConvertToType<f128>>>,

  // In AAPCS, an SRet is passed in X8, not X0 like a normal pointer parameter.
  // However, on windows, in some circumstances, the SRet is passed in X0 or X1
  // instead.  The presence of the inreg attribute indicates that SRet is
  // passed in the alternative register (X0 or X1), not X8:
  // - X0 for non-instance methods.
  // - X1 for instance methods.

  // The "sret" attribute identifies indirect returns.
  // The "inreg" attribute identifies non-aggregate types.
  // The position of the "sret" attribute identifies instance/non-instance
  // methods.
  // "sret" on argument 0 means non-instance methods.
  // "sret" on argument 1 means instance methods.

  CCIfInReg<CCIfType<[i64],
    CCIfSRet<CCIfType<[i64], CCAssignToReg<[X0, X1]>>>>>,

  CCIfSRet<CCIfType<[i64], CCAssignToReg<[X8]>>>,

  // Put ByVal arguments directly on the stack. Minimum size and alignment of a
  // slot is 64-bit.
  CCIfByVal<CCPassByVal<8, 8>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToReg<[X20]>>>,

  // A SwiftError is passed in X21.
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[X21]>>>,

  // Pass SwiftAsync in an otherwise callee saved register so that it will be
  // preserved for normal function calls.
  CCIfSwiftAsync<CCIfType<[i64], CCAssignToReg<[X22]>>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Block">>,

  CCIfType<[nxv16i8, nxv8i16, nxv4i32, nxv2i64, nxv2f16, nxv4f16, nxv8f16,
            nxv2bf16, nxv4bf16, nxv8bf16, nxv2f32, nxv4f32, nxv2f64],
           CCAssignToReg<[Z0, Z1, Z2, Z3, Z4, Z5, Z6, Z7]>>,
  CCIfType<[nxv16i8, nxv8i16, nxv4i32, nxv2i64, nxv2f16, nxv4f16, nxv8f16,
            nxv2bf16, nxv4bf16, nxv8bf16, nxv2f32, nxv4f32, nxv2f64],
           CCPassIndirect<i64>>,

  CCIfType<[nxv1i1, nxv2i1, nxv4i1, nxv8i1, nxv16i1, aarch64svcount],
           CCAssignToReg<[P0, P1, P2, P3]>>,
  CCIfType<[nxv1i1, nxv2i1, nxv4i1, nxv8i1, nxv16i1, aarch64svcount],
           CCPassIndirect<i64>>,

  // Handle i1, i8, i16, i32, i64, f32, f64 and v2f64 by passing in registers,
  // up to eight each of GPR and FPR.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[W0, W1, W2, W3, W4, W5, W6, W7]>>,
  // i128 is split to two i64s, we can't fit half to register X7.
  CCIfType<[i64], CCIfSplit<CCAssignToRegWithShadow<[X0, X2, X4, X6],
                                                    [X0, X1, X3, X5]>>>,

  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStackWithShadow<8, 16, [X7]>>>,

  CCIfType<[i64], CCAssignToReg<[X0, X1, X2, X3, X4, X5, X6, X7]>>,
  CCIfType<[f16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[bf16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16, v4bf16],
           CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,

  // If more than will fit in registers, pass them on the stack instead.
  CCIfType<[i1, i8, i16, f16, bf16], CCAssignToStack<8, 8>>,
  CCIfType<[i32, f32], CCAssignToStack<8, 8>>,
  CCIfType<[i64, f64, v1f64, v2f32, v1i64, v2i32, v4i16, v8i8, v4f16, v4bf16],
           CCAssignToStack<8, 8>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToStack<16, 16>>
];

let Entry = 1 in
def CC_AArch64_AAPCS : CallingConv<!listconcat(
  // The 'nest' parameter, if any, is passed in X18.
  // Darwin and Windows use X18 as the platform register and hence 'nest' isn't
  // currently supported there.
  [CCIfNest<CCAssignToReg<[X18]>>],
  AArch64_Common
)>;

let Entry = 1 in
def RetCC_AArch64_AAPCS : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32], CCBitConvertToType<v2i64>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Block">>,
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[X21]>>>,

  // Big endian vectors must be passed as if they were 1-element vectors so that
  // their lanes are in a consistent order.
  CCIfBigEndian<CCIfType<[v2i32, v2f32, v4i16, v4f16, v4bf16, v8i8],
                         CCBitConvertToType<f64>>>,
  CCIfBigEndian<CCIfType<[v2i64, v2f64, v4i32, v4f32, v8i16, v8f16, v8bf16, v16i8],
                         CCBitConvertToType<f128>>>,

  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[W0, W1, W2, W3, W4, W5, W6, W7]>>,
  CCIfType<[i64], CCAssignToReg<[X0, X1, X2, X3, X4, X5, X6, X7]>>,
  CCIfType<[f16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[bf16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16, v4bf16],
      CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[f128, v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
      CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,

  CCIfType<[nxv16i8, nxv8i16, nxv4i32, nxv2i64, nxv2f16, nxv4f16, nxv8f16,
            nxv2bf16, nxv4bf16, nxv8bf16, nxv2f32, nxv4f32, nxv2f64],
           CCAssignToReg<[Z0, Z1, Z2, Z3, Z4, Z5, Z6, Z7]>>,

  CCIfType<[nxv1i1, nxv2i1, nxv4i1, nxv8i1, nxv16i1, aarch64svcount],
           CCAssignToReg<[P0, P1, P2, P3]>>
]>;

let Entry = 1 in
def CC_AArch64_Win64PCS : CallingConv<AArch64_Common>;

// Vararg functions on windows pass floats in integer registers
let Entry = 1 in
def CC_AArch64_Win64_VarArg : CallingConv<[
  CCIfType<[f16, bf16], CCBitConvertToType<i16>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[f64], CCBitConvertToType<i64>>,
  CCDelegateTo<CC_AArch64_Win64PCS>
]>;

// Vararg functions on Arm64EC ABI use a different convention, using
// a stack layout compatible with the x64 calling convention.
let Entry = 1 in
def CC_AArch64_Arm64EC_VarArg : CallingConv<[
  // Convert small floating-point values to integer.
  CCIfType<[f16, bf16], CCBitConvertToType<i16>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[f64, v1f64, v1i64, v2f32, v2i32, v4i16, v4f16, v4bf16, v8i8, iPTR],
           CCBitConvertToType<i64>>,

  // Larger floating-point/vector values are passed indirectly.
  CCIfType<[f128, v2f64, v2i64, v4i32, v4f32, v8i16, v8f16, v8bf16, v16i8],
           CCPassIndirect<i64>>,
  CCIfType<[nxv16i8, nxv8i16, nxv4i32, nxv2i64, nxv2f16, nxv4f16, nxv8f16,
            nxv2bf16, nxv4bf16, nxv8bf16, nxv2f32, nxv4f32, nxv2f64],
           CCPassIndirect<i64>>,
  CCIfType<[nxv2i1, nxv4i1, nxv8i1, nxv16i1],
           CCPassIndirect<i64>>,

  // Handle SRet. See comment in CC_AArch64_AAPCS.
  CCIfInReg<CCIfType<[i64],
    CCIfSRet<CCIfType<[i64], CCAssignToReg<[X0, X1]>>>>>,
  CCIfSRet<CCIfType<[i64], CCAssignToReg<[X8]>>>,

  // Put ByVal arguments directly on the stack. Minimum size and alignment of a
  // slot is 64-bit. (Shouldn't normally come up; the Microsoft ABI doesn't
  // use byval.)
  CCIfByVal<CCPassByVal<8, 8>>,

  // Promote small integers to i32
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // Pass first four arguments in x0-x3.
  CCIfType<[i32], CCAssignToReg<[W0, W1, W2, W3]>>,
  CCIfType<[i64], CCAssignToReg<[X0, X1, X2, X3]>>,

  // Put remaining arguments on stack.
  CCIfType<[i32, i64], CCAssignToStack<8, 8>>,
]>;

// Arm64EC thunks use a calling convention that's precisely the x64 calling
// convention, except that the registers have different names, and the callee
// address is passed in X9.
let Entry = 1 in
def CC_AArch64_Arm64EC_Thunk : CallingConv<[
  // ARM64EC-specific: the InReg attribute can be used to access the x64 sp passed into entry thunks in x4 from the IR.
  CCIfInReg<CCIfType<[i64], CCAssignToReg<[X4]>>>,

  // Byval aggregates are passed by pointer
  CCIfByVal<CCPassIndirect<i64>>,

  // ARM64EC-specific: promote small integers to i32. (x86 only promotes i1,
  // but that would confuse ARM64 lowering code.)
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // The 'nest' parameter, if any, is passed in R10 (X4).
  CCIfNest<CCAssignToReg<[X4]>>,

  // A SwiftError is passed in R12 (X19).
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[X19]>>>,

  // Pass SwiftSelf in R13 (X20).
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToReg<[X20]>>>,

  // Pass SwiftAsync in an otherwise callee saved register so that calls to
  // normal functions don't need to save it somewhere.
  CCIfSwiftAsync<CCIfType<[i64], CCAssignToReg<[X21]>>>,

  // The 'CFGuardTarget' parameter, if any, is passed in RAX (R8).
  CCIfCFGuardTarget<CCAssignToReg<[X8]>>,

  // 128 bit vectors are passed by pointer
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v8f16, v4f32, v2f64], CCPassIndirect<i64>>,

  // 256 bit vectors are passed by pointer
  CCIfType<[v32i8, v16i16, v8i32, v4i64, v16f16, v8f32, v4f64], CCPassIndirect<i64>>,

  // 512 bit vectors are passed by pointer
  CCIfType<[v64i8, v32i16, v16i32, v32f16, v16f32, v8f64, v8i64], CCPassIndirect<i64>>,

  // Long doubles are passed by pointer
  CCIfType<[f80], CCPassIndirect<i64>>,

  // The first 4 MMX vector arguments are passed in GPRs.
  CCIfType<[x86mmx], CCBitConvertToType<i64>>,

  // The first 4 FP/Vector arguments are passed in XMM registers.
  CCIfType<[f16],
           CCAssignToRegWithShadow<[H0, H1, H2, H3],
                                   [X0, X1, X2, X3]>>,
  CCIfType<[f32],
           CCAssignToRegWithShadow<[S0, S1, S2, S3],
                                   [X0, X1, X2, X3]>>,
  CCIfType<[f64],
           CCAssignToRegWithShadow<[D0, D1, D2, D3],
                                   [X0, X1, X2, X3]>>,

  // The first 4 integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3],
                                          [Q0, Q1, Q2, Q3]>>,

  // Arm64EC thunks: the first argument is always a pointer to the destination
  // address, stored in x9.
  CCIfType<[i64], CCAssignToReg<[X9]>>,

  CCIfType<[i64], CCAssignToRegWithShadow<[X0, X1, X2, X3],
                                          [Q0, Q1, Q2, Q3]>>,

  // Integer/FP values get stored in stack slots that are 8 bytes in size and
  // 8-byte aligned if there are no more registers to hold them.
  CCIfType<[i8, i16, i32, i64, f16, f32, f64], CCAssignToStack<8, 8>>
]>;

// The native side of ARM64EC thunks
let Entry = 1 in
def CC_AArch64_Arm64EC_Thunk_Native : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[X9]>>,
  CCDelegateTo<CC_AArch64_AAPCS>
]>;

let Entry = 1 in
def RetCC_AArch64_Arm64EC_Thunk : CallingConv<[
  // The X86-Win64 calling convention always returns __m64 values in RAX.
  CCIfType<[x86mmx], CCBitConvertToType<i64>>,

  // Otherwise, everything is the same as 'normal' X86-64 C CC.

  // The X86-64 calling convention always returns FP values in XMM0.
  CCIfType<[f16], CCAssignToReg<[H0, H1]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1]>>,
  CCIfType<[f128], CCAssignToReg<[Q0, Q1]>>,

  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[X19]>>>,

  // Scalar values are returned in AX first, then DX.  For i8, the ABI
  // requires the values to be in AL and AH, however this code uses AL and DL
  // instead. This is because using AH for the second register conflicts with
  // the way LLVM does multiple return values -- a return of {i16,i8} would end
  // up in AX and AH, which overlap. Front-ends wishing to conform to the ABI
  // for functions that return two i8 values are currently expected to pack the
  // values into an i16 (which uses AX, and thus AL:AH).
  //
  // For code that doesn't care about the ABI, we allow returning more than two
  // integer values in registers.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[W8, W1, W0]>>,
  CCIfType<[i64], CCAssignToReg<[X8, X1, X0]>>,

  // Vector types are returned in XMM0 and XMM1, when they fit.  XMM2 and XMM3
  // can only be used by ABI non-compliant code. If the target doesn't have XMM
  // registers, it won't have vector types.
  CCIfType<[v16i8, v8i16, v4i32, v2i64, v8f16, v4f32, v2f64],
            CCAssignToReg<[Q0, Q1, Q2, Q3]>>
]>;

// Windows Control Flow Guard checks take a single argument (the target function
// address) and have no return value.
let Entry = 1 in
def CC_AArch64_Win64_CFGuard_Check : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[X15]>>
]>;

let Entry = 1 in
def CC_AArch64_Arm64EC_CFGuard_Check : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[X11, X10, X9]>>
]>;

let Entry = 1 in
def RetCC_AArch64_Arm64EC_CFGuard_Check : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[X11]>>
]>;


// Darwin uses a calling convention which differs in only two ways
// from the standard one at this level:
//     + i128s (i.e. split i64s) don't need even registers.
//     + Stack slots are sized as needed rather than being at least 64-bit.
let Entry = 1 in
def CC_AArch64_DarwinPCS : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32, f128], CCBitConvertToType<v2i64>>,

  // An SRet is passed in X8, not X0 like a normal pointer parameter.
  CCIfSRet<CCIfType<[i64], CCAssignToReg<[X8]>>>,

  // Put ByVal arguments directly on the stack. Minimum size and alignment of a
  // slot is 64-bit.
  CCIfByVal<CCPassByVal<8, 8>>,

  // Pass SwiftSelf in a callee saved register.
  CCIfSwiftSelf<CCIfType<[i64], CCAssignToReg<[X20]>>>,

  // A SwiftError is passed in X21.
  CCIfSwiftError<CCIfType<[i64], CCAssignToReg<[X21]>>>,

  // Pass SwiftAsync in an otherwise callee saved register so that it will be
  // preserved for normal function calls.
  CCIfSwiftAsync<CCIfType<[i64], CCAssignToReg<[X22]>>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Block">>,

  // Handle i1, i8, i16, i32, i64, f32, f64 and v2f64 by passing in registers,
  // up to eight each of GPR and FPR.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[W0, W1, W2, W3, W4, W5, W6, W7]>>,
  // i128 is split to two i64s, we can't fit half to register X7.
  CCIfType<[i64],
           CCIfSplit<CCAssignToReg<[X0, X1, X2, X3, X4, X5, X6]>>>,
  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStackWithShadow<8, 16, [X7]>>>,

  CCIfType<[i64], CCAssignToReg<[X0, X1, X2, X3, X4, X5, X6, X7]>>,
  CCIfType<[f16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[bf16], CCAssignToReg<[H0, H1, H2, H3, H4, H5, H6, H7]>>,
  CCIfType<[f32], CCAssignToReg<[S0, S1, S2, S3, S4, S5, S6, S7]>>,
  CCIfType<[f64], CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16, v4bf16],
           CCAssignToReg<[D0, D1, D2, D3, D4, D5, D6, D7]>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToReg<[Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7]>>,

  // If more than will fit in registers, pass them on the stack instead.
  CCIf<"ValVT == MVT::i1 || ValVT == MVT::i8", CCAssignToStack<1, 1>>,
  CCIf<"ValVT == MVT::i16 || ValVT == MVT::f16 || ValVT == MVT::bf16",
  CCAssignToStack<2, 2>>,
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,

  // Re-demote pointers to 32-bits so we don't end up storing 64-bit
  // values and clobbering neighbouring stack locations. Not very pretty.
  CCIfPtr<CCIfILP32<CCTruncToType<i32>>>,
  CCIfPtr<CCIfILP32<CCAssignToStack<4, 4>>>,

  CCIfType<[i64, f64, v1f64, v2f32, v1i64, v2i32, v4i16, v8i8, v4f16, v4bf16],
           CCAssignToStack<8, 8>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToStack<16, 16>>
]>;

let Entry = 1 in
def CC_AArch64_DarwinPCS_VarArg : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32, f128], CCBitConvertToType<v2i64>>,

  CCIfConsecutiveRegs<CCCustom<"CC_AArch64_Custom_Stack_Block">>,

  // Handle all scalar types as either i64 or f64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[f16, bf16, f32], CCPromoteToType<f64>>,

  // Everything is on the stack.
  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfType<[i64], CCIfSplit<CCAssignToStack<8, 16>>>,
  CCIfType<[i64, f64, v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16, v4bf16],
           CCAssignToStack<8, 8>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToStack<16, 16>>
]>;

// In the ILP32 world, the minimum stack slot size is 4 bytes. Otherwise the
// same as the normal Darwin VarArgs handling.
let Entry = 1 in
def CC_AArch64_DarwinPCS_ILP32_VarArg : CallingConv<[
  CCIfType<[v2f32], CCBitConvertToType<v2i32>>,
  CCIfType<[v2f64, v4f32, f128], CCBitConvertToType<v2i64>>,

  // Handle all scalar types as either i32 or f32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f16, bf16], CCPromoteToType<f32>>,

  // Everything is on the stack.
  // i128 is split to two i64s, and its stack alignment is 16 bytes.
  CCIfPtr<CCIfILP32<CCTruncToType<i32>>>,
  CCIfType<[i32, f32], CCAssignToStack<4, 4>>,
  CCIfType<[i64], CCIfSplit<CCAssignToStack<8, 16>>>,
  CCIfType<[i64, f64, v1i64, v2i32, v4i16, v8i8, v1f64, v2f32, v4f16, v4bf16],
           CCAssignToStack<8, 8>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, v2f64, v8f16, v8bf16],
           CCAssignToStack<16, 16>>
]>;

//===----------------------------------------------------------------------===//
// ARM64 Calling Convention for GHC
//===----------------------------------------------------------------------===//

// This calling convention is specific to the Glasgow Haskell Compiler.
// The only documentation is the GHC source code, specifically the C header
// file:
//
//    https://github.com/ghc/ghc/blob/master/rts/include/stg/MachRegs.h
//
// which defines the registers for the Spineless Tagless G-Machine (STG) that
// GHC uses to implement lazy evaluation. The generic STG machine has a set of
// registers which are mapped to appropriate set of architecture specific
// registers for each CPU architecture.
//
// The STG Machine is documented here:
//
//    https://ghc.haskell.org/trac/ghc/wiki/Commentary/Compiler/GeneratedCode
//
// The AArch64 register mapping is defined in the following header file:
//
//    https://github.com/ghc/ghc/blob/master/rts/include/stg/MachRegs/arm64.h
//

let Entry = 1 in
def CC_AArch64_GHC : CallingConv<[
  CCIfType<[iPTR], CCBitConvertToType<i64>>,

  // Handle all vector types as either f64 or v2f64.
  CCIfType<[v1i64, v2i32, v4i16, v8i8, v2f32], CCBitConvertToType<f64>>,
  CCIfType<[v2i64, v4i32, v8i16, v16i8, v4f32, f128], CCBitConvertToType<v2f64>>,

  CCIfType<[v2f64], CCAssignToReg<[Q4, Q5]>>,
  CCIfType<[f32], CCAssignToReg<[S8, S9, S10, S11]>>,
  CCIfType<[f64], CCAssignToReg<[D12, D13, D14, D15]>>,

  // Promote i8/i16/i32 arguments to i64.
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,

  // Pass in STG registers: Base, Sp, Hp, R1, R2, R3, R4, R5, R6, SpLim
  CCIfType<[i64], CCAssignToReg<[X19, X20, X21, X22, X23, X24, X25, X26, X27, X28]>>
]>;

let Entry = 1 in
def CC_AArch64_Preserve_None : CallingConv<[
  // VarArgs are only supported using the C calling convention.
  // This handles the non-variadic parameter case. Variadic parameters
  // are handled in CCAssignFnForCall.
  CCIfVarArg<CCIfSubtarget<"isTargetDarwin()", CCDelegateTo<CC_AArch64_DarwinPCS>>>,
  CCIfVarArg<CCIfSubtarget<"isTargetWindows()", CCDelegateTo<CC_AArch64_Win64PCS>>>,
  CCIfVarArg<CCDelegateTo<CC_AArch64_AAPCS>>,

  // We can pass arguments in all general registers, except:
  // - X8, used for sret
  // - X16/X17, used by the linker as IP0/IP1
  // - X18, the platform register
  // - X19, the base pointer
  // - X29, the frame pointer
  // - X30, the link register
  // General registers are not preserved with the exception of
  // FP, LR, and X18
  // Non-volatile registers are used first, so functions may call
  // normal functions without saving and reloading arguments.
  // X9 is assigned last as it is used in FrameLowering as the first
  // choice for a scratch register.
  CCIfType<[i32], CCAssignToReg<[W20, W21, W22, W23,
                                 W24, W25, W26, W27, W28,
                                 W0, W1, W2, W3, W4, W5,
                                 W6, W7, W10, W11,
                                 W12, W13, W14, W9]>>,
  CCIfType<[i64], CCAssignToReg<[X20, X21, X22, X23,
                                 X24, X25, X26, X27, X28,
                                 X0, X1, X2, X3, X4, X5,
                                 X6, X7, X10, X11,
                                 X12, X13, X14, X9]>>,

  // Windows uses X15 for stack allocation
  CCIf<"!State.getMachineFunction().getSubtarget<AArch64Subtarget>().isTargetWindows()",
    CCIfType<[i32], CCAssignToReg<[W15]>>>,
  CCIf<"!State.getMachineFunction().getSubtarget<AArch64Subtarget>().isTargetWindows()",
    CCIfType<[i64], CCAssignToReg<[X15]>>>,

  CCDelegateTo<CC_AArch64_AAPCS>
]>;

// The order of the callee-saves in this file is important, because the
// FrameLowering code will use this order to determine the layout the
// callee-save area in the stack frame. As can be observed below, Darwin
// requires the frame-record (LR, FP) to be at the top the callee-save area,
// whereas for other platforms they are at the bottom.

// FIXME: LR is only callee-saved in the sense that *we* preserve it and are
// presumably a callee to someone. External functions may not do so, but this
// is currently safe since BL has LR as an implicit-def and what happens after a
// tail call doesn't matter.
//
// It would be better to model its preservation semantics properly (create a
// vreg on entry, use it in RET & tail call generation; make that vreg def if we
// end up saving LR as part of a call frame). Watch this space...
def CSR_AArch64_AAPCS : CalleeSavedRegs<(add X19, X20, X21, X22, X23, X24,
                                           X25, X26, X27, X28, LR, FP,
                                           D8,  D9,  D10, D11,
                                           D12, D13, D14, D15)>;

// A variant for treating X18 as callee saved, when interfacing with
// code that needs X18 to be preserved.
def CSR_AArch64_AAPCS_X18 : CalleeSavedRegs<(add X18, CSR_AArch64_AAPCS)>;

// Win64 has unwinding codes for an (FP,LR) pair, save_fplr and save_fplr_x.
// We put FP before LR, so that frame lowering logic generates (FP,LR) pairs,
// and not (LR,FP) pairs.
def CSR_Win_AArch64_AAPCS : CalleeSavedRegs<(add X19, X20, X21, X22, X23, X24,
                                               X25, X26, X27, X28, FP, LR,
                                               D8, D9, D10, D11,
                                               D12, D13, D14, D15)>;

def CSR_Win_AArch64_AAPCS_SwiftError
    : CalleeSavedRegs<(sub CSR_Win_AArch64_AAPCS, X21)>;

def CSR_Win_AArch64_AAPCS_SwiftTail
    : CalleeSavedRegs<(sub CSR_Win_AArch64_AAPCS, X20, X22)>;

// The Control Flow Guard check call uses a custom calling convention that also
// preserves X0-X8 and Q0-Q7.
def CSR_Win_AArch64_CFGuard_Check : CalleeSavedRegs<(add CSR_Win_AArch64_AAPCS,
                                               (sequence "X%u", 0, 8),
                                               (sequence "Q%u", 0, 7))>;

// To match the x64 calling convention, Arm64EC thunks preserve q6-q15.
def CSR_Win_AArch64_Arm64EC_Thunk : CalleeSavedRegs<(add (sequence "Q%u", 6, 15),
                                                         X19, X20, X21, X22, X23, X24,
                                                         X25, X26, X27, X28, FP, LR)>;

// AArch64 PCS for vector functions (VPCS)
// must (additionally) preserve full Q8-Q23 registers
def CSR_AArch64_AAVPCS : CalleeSavedRegs<(add X19, X20, X21, X22, X23, X24,
                                          X25, X26, X27, X28, LR, FP,
                                          (sequence "Q%u", 8, 23))>;

// Functions taking SVE arguments or returning an SVE type
// must (additionally) preserve full Z8-Z23 and predicate registers P4-P15
def CSR_AArch64_SVE_AAPCS : CalleeSavedRegs<(add (sequence "Z%u", 8, 23),
                                                 (sequence "P%u", 4, 15),
                                                 X19, X20, X21, X22, X23, X24,
                                                 X25, X26, X27, X28, LR, FP)>;

// SME ABI support routines such as __arm_tpidr2_save/restore preserve most registers.
def CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0
                          : CalleeSavedRegs<(add (sequence "Z%u", 0, 31),
                                                 (sequence "P%u", 0, 15),
                                                 (sequence "X%u", 0, 13),
                                                 (sequence "X%u",19, 28),
                                                 LR, FP)>;

// SME ABI support routines such as __arm_get_current_vg preserve most registers.
def CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1
                          : CalleeSavedRegs<(add (sequence "Z%u", 0, 31),
                                                 (sequence "P%u", 0, 15),
                                                 (sequence "X%u", 1, 15),
                                                 (sequence "X%u",19, 28),
                                                 LR, FP)>;

// SME ABI support routines __arm_sme_state preserves most registers.
def CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2
                          : CalleeSavedRegs<(add (sequence "Z%u", 0, 31),
                                                 (sequence "P%u", 0, 15),
                                                 (sequence "X%u", 2, 15),
                                                 (sequence "X%u",19, 28),
                                                 LR, FP)>;

// The SMSTART/SMSTOP instructions preserve only GPR registers.
def CSR_AArch64_SMStartStop : CalleeSavedRegs<(add (sequence "X%u", 0, 28),
                                                   LR, FP)>;

def CSR_AArch64_AAPCS_SwiftTail
    : CalleeSavedRegs<(sub CSR_AArch64_AAPCS, X20, X22)>;

// Constructors and destructors return 'this' in the iOS 64-bit C++ ABI; since
// 'this' and the pointer return value are both passed in X0 in these cases,
// this can be partially modelled by treating X0 as a callee-saved register;
// only the resulting RegMask is used; the SaveList is ignored
//
// (For generic ARM 64-bit ABI code, clang will not generate constructors or
// destructors with 'this' returns, so this RegMask will not be used in that
// case)
def CSR_AArch64_AAPCS_ThisReturn : CalleeSavedRegs<(add CSR_AArch64_AAPCS, X0)>;

def CSR_AArch64_AAPCS_SwiftError
    : CalleeSavedRegs<(sub CSR_AArch64_AAPCS, X21)>;

// The ELF stub used for TLS-descriptor access saves every feasible
// register. Only X0 and LR are clobbered.
def CSR_AArch64_TLS_ELF
    : CalleeSavedRegs<(add (sequence "X%u", 1, 28), FP,
                           (sequence "Q%u", 0, 31))>;

def CSR_AArch64_AllRegs
    : CalleeSavedRegs<(add (sequence "W%u", 0, 30), WSP,
                           (sequence "X%u", 0, 28), FP, LR, SP,
                           (sequence "B%u", 0, 31), (sequence "H%u", 0, 31),
                           (sequence "S%u", 0, 31), (sequence "D%u", 0, 31),
                           (sequence "Q%u", 0, 31))>;

def CSR_AArch64_NoRegs : CalleeSavedRegs<(add)>;

def CSR_AArch64_NoneRegs : CalleeSavedRegs<(add LR, FP)>;

def CSR_AArch64_RT_MostRegs :  CalleeSavedRegs<(add CSR_AArch64_AAPCS,
                                                (sequence "X%u", 9, 15))>;

def CSR_AArch64_RT_AllRegs :  CalleeSavedRegs<(add CSR_AArch64_RT_MostRegs,
                                                (sequence "Q%u", 8, 31))>;

def CSR_AArch64_StackProbe_Windows
    : CalleeSavedRegs<(add (sequence "X%u", 0, 15),
                           (sequence "X%u", 18, 28), FP, SP,
                           (sequence "Q%u", 0, 31))>;

// Darwin variants of AAPCS.
// Darwin puts the frame-record at the top of the callee-save area.
def CSR_Darwin_AArch64_AAPCS : CalleeSavedRegs<(add LR, FP, X19, X20, X21, X22,
                                                X23, X24, X25, X26, X27, X28,
                                                D8,  D9,  D10, D11,
                                                D12, D13, D14, D15)>;

def CSR_Darwin_AArch64_AAVPCS : CalleeSavedRegs<(add LR, FP, X19, X20, X21,
                                                 X22, X23, X24, X25, X26, X27,
                                                 X28, (sequence "Q%u", 8, 23))>;

// For Windows calling convention on a non-windows OS, where X18 is treated
// as reserved, back up X18 when entering non-windows code (marked with the
// Windows calling convention) and restore when returning regardless of
// whether the individual function uses it - it might call other functions
// that clobber it.
def CSR_Darwin_AArch64_AAPCS_Win64
    : CalleeSavedRegs<(add CSR_Darwin_AArch64_AAPCS, X18)>;

def CSR_Darwin_AArch64_AAPCS_ThisReturn
    : CalleeSavedRegs<(add CSR_Darwin_AArch64_AAPCS, X0)>;

def CSR_Darwin_AArch64_AAPCS_SwiftError
    : CalleeSavedRegs<(sub CSR_Darwin_AArch64_AAPCS, X21)>;

def CSR_Darwin_AArch64_AAPCS_SwiftTail
    : CalleeSavedRegs<(sub CSR_Darwin_AArch64_AAPCS, X20, X22)>;

// The function used by Darwin to obtain the address of a thread-local variable
// guarantees more than a normal AAPCS function. x16 and x17 are used on the
// fast path for calculation, but other registers except X0 (argument/return)
// and LR (it is a call, after all) are preserved.
def CSR_Darwin_AArch64_TLS
    : CalleeSavedRegs<(add (sub (sequence "X%u", 1, 28), X16, X17),
                           FP,
                           (sequence "Q%u", 0, 31))>;

// We can only handle a register pair with adjacent registers, the register pair
// should belong to the same class as well. Since the access function on the
// fast path calls a function that follows CSR_Darwin_AArch64_TLS,
// CSR_Darwin_AArch64_CXX_TLS should be a subset of CSR_Darwin_AArch64_TLS.
def CSR_Darwin_AArch64_CXX_TLS
    : CalleeSavedRegs<(add CSR_Darwin_AArch64_AAPCS,
                           (sub (sequence "X%u", 1, 28), X9, X15, X16, X17, X18, X19),
                           (sequence "D%u", 0, 31))>;

// CSRs that are handled by prologue, epilogue.
def CSR_Darwin_AArch64_CXX_TLS_PE
    : CalleeSavedRegs<(add LR, FP)>;

// CSRs that are handled explicitly via copies.
def CSR_Darwin_AArch64_CXX_TLS_ViaCopy
    : CalleeSavedRegs<(sub CSR_Darwin_AArch64_CXX_TLS, LR, FP)>;

def CSR_Darwin_AArch64_RT_MostRegs
    : CalleeSavedRegs<(add CSR_Darwin_AArch64_AAPCS, (sequence "X%u", 9, 15))>;

def CSR_Darwin_AArch64_RT_AllRegs
    : CalleeSavedRegs<(add CSR_Darwin_AArch64_RT_MostRegs, (sequence "Q%u", 8, 31))>;

// Variants of the standard calling conventions for shadow call stack.
// These all preserve x18 in addition to any other registers.
def CSR_AArch64_NoRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_NoRegs, X18)>;
def CSR_AArch64_NoneRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_NoneRegs, X18)>;
def CSR_AArch64_AllRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AllRegs, X18)>;
def CSR_AArch64_AAPCS_SwiftError_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAPCS_SwiftError, X18)>;
def CSR_AArch64_RT_MostRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_RT_MostRegs, X18)>;
def CSR_AArch64_RT_AllRegs_SCS
    : CalleeSavedRegs<(add CSR_AArch64_RT_AllRegs, X18)>;
def CSR_AArch64_AAVPCS_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAVPCS, X18)>;
def CSR_AArch64_SVE_AAPCS_SCS
    : CalleeSavedRegs<(add CSR_AArch64_SVE_AAPCS, X18)>;
def CSR_AArch64_AAPCS_SCS
    : CalleeSavedRegs<(add CSR_AArch64_AAPCS, X18)>;
