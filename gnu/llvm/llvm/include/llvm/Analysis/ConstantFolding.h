//===-- ConstantFolding.h - Fold instructions into constants ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares routines for folding instructions into constants when all
// operands are constants, for example "sub i32 1, 0" -> "1".
//
// Also, to supplement the basic VMCore ConstantExpr simplifications,
// this file declares some additional folding routines that can make use of
// DataLayout information. These functions cannot go in VMCore due to library
// dependency issues.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CONSTANTFOLDING_H
#define LLVM_ANALYSIS_CONSTANTFOLDING_H

#include <stdint.h>

namespace llvm {

namespace Intrinsic {
using ID = unsigned;
}

class APInt;
template <typename T> class ArrayRef;
class CallBase;
class Constant;
class DSOLocalEquivalent;
class DataLayout;
class Function;
class GlobalValue;
class GlobalVariable;
class Instruction;
class TargetLibraryInfo;
class Type;

/// If this constant is a constant offset from a global, return the global and
/// the constant. Because of constantexprs, this function is recursive.
/// If the global is part of a dso_local_equivalent constant, return it through
/// `Equiv` if it is provided.
bool IsConstantOffsetFromGlobal(Constant *C, GlobalValue *&GV, APInt &Offset,
                                const DataLayout &DL,
                                DSOLocalEquivalent **DSOEquiv = nullptr);

/// ConstantFoldInstruction - Try to constant fold the specified instruction.
/// If successful, the constant result is returned, if not, null is returned.
/// Note that this fails if not all of the operands are constant.  Otherwise,
/// this function can only fail when attempting to fold instructions like loads
/// and stores, which have no constant expression form.
Constant *ConstantFoldInstruction(Instruction *I, const DataLayout &DL,
                                  const TargetLibraryInfo *TLI = nullptr);

/// ConstantFoldConstant - Fold the constant using the specified DataLayout.
/// This function always returns a non-null constant: Either the folding result,
/// or the original constant if further folding is not possible.
Constant *ConstantFoldConstant(const Constant *C, const DataLayout &DL,
                               const TargetLibraryInfo *TLI = nullptr);

/// ConstantFoldInstOperands - Attempt to constant fold an instruction with the
/// specified operands.  If successful, the constant result is returned, if not,
/// null is returned.  Note that this function can fail when attempting to
/// fold instructions like loads and stores, which have no constant expression
/// form.
///
/// In some cases, constant folding may return one value chosen from a set of
/// multiple legal return values. For example, the exact bit pattern of NaN
/// results is not guaranteed. Using such a result is usually only valid if
/// all uses of the original operation are replaced by the constant-folded
/// result. The \p AllowNonDeterministic parameter controls whether this is
/// allowed.
Constant *ConstantFoldInstOperands(Instruction *I, ArrayRef<Constant *> Ops,
                                   const DataLayout &DL,
                                   const TargetLibraryInfo *TLI = nullptr,
                                   bool AllowNonDeterministic = true);

/// Attempt to constant fold a compare instruction (icmp/fcmp) with the
/// specified operands. Returns null or a constant expression of the specified
/// operands on failure.
/// Denormal inputs may be flushed based on the denormal handling mode.
Constant *ConstantFoldCompareInstOperands(
    unsigned Predicate, Constant *LHS, Constant *RHS, const DataLayout &DL,
    const TargetLibraryInfo *TLI = nullptr, const Instruction *I = nullptr);

/// Attempt to constant fold a unary operation with the specified operand.
/// Returns null on failure.
Constant *ConstantFoldUnaryOpOperand(unsigned Opcode, Constant *Op,
                                     const DataLayout &DL);

/// Attempt to constant fold a binary operation with the specified operands.
/// Returns null or a constant expression of the specified operands on failure.
Constant *ConstantFoldBinaryOpOperands(unsigned Opcode, Constant *LHS,
                                       Constant *RHS, const DataLayout &DL);

/// Attempt to constant fold a floating point binary operation with the
/// specified operands, applying the denormal handling mod to the operands.
/// Returns null or a constant expression of the specified operands on failure.
Constant *ConstantFoldFPInstOperands(unsigned Opcode, Constant *LHS,
                                     Constant *RHS, const DataLayout &DL,
                                     const Instruction *I,
                                     bool AllowNonDeterministic = true);

/// Attempt to flush float point constant according to denormal mode set in the
/// instruction's parent function attributes. If so, return a zero with the
/// correct sign, otherwise return the original constant. Inputs and outputs to
/// floating point instructions can have their mode set separately, so the
/// direction is also needed.
///
/// If the calling function's "denormal-fp-math" input mode is "dynamic" for the
/// floating-point type, returns nullptr for denormal inputs.
Constant *FlushFPConstant(Constant *Operand, const Instruction *I,
                          bool IsOutput);

/// Attempt to constant fold a select instruction with the specified
/// operands. The constant result is returned if successful; if not, null is
/// returned.
Constant *ConstantFoldSelectInstruction(Constant *Cond, Constant *V1,
                                        Constant *V2);

/// Attempt to constant fold a cast with the specified operand.  If it
/// fails, it returns a constant expression of the specified operand.
Constant *ConstantFoldCastOperand(unsigned Opcode, Constant *C, Type *DestTy,
                                  const DataLayout &DL);

/// Constant fold a zext, sext or trunc, depending on IsSigned and whether the
/// DestTy is wider or narrower than C. Returns nullptr on failure.
Constant *ConstantFoldIntegerCast(Constant *C, Type *DestTy, bool IsSigned,
                                  const DataLayout &DL);

/// ConstantFoldInsertValueInstruction - Attempt to constant fold an insertvalue
/// instruction with the specified operands and indices.  The constant result is
/// returned if successful; if not, null is returned.
Constant *ConstantFoldInsertValueInstruction(Constant *Agg, Constant *Val,
                                             ArrayRef<unsigned> Idxs);

/// Attempt to constant fold an extractvalue instruction with the
/// specified operands and indices.  The constant result is returned if
/// successful; if not, null is returned.
Constant *ConstantFoldExtractValueInstruction(Constant *Agg,
                                              ArrayRef<unsigned> Idxs);

/// Attempt to constant fold an insertelement instruction with the
/// specified operands and indices.  The constant result is returned if
/// successful; if not, null is returned.
Constant *ConstantFoldInsertElementInstruction(Constant *Val,
                                               Constant *Elt,
                                               Constant *Idx);

/// Attempt to constant fold an extractelement instruction with the
/// specified operands and indices.  The constant result is returned if
/// successful; if not, null is returned.
Constant *ConstantFoldExtractElementInstruction(Constant *Val, Constant *Idx);

/// Attempt to constant fold a shufflevector instruction with the
/// specified operands and mask.  See class ShuffleVectorInst for a description
/// of the mask representation. The constant result is returned if successful;
/// if not, null is returned.
Constant *ConstantFoldShuffleVectorInstruction(Constant *V1, Constant *V2,
                                               ArrayRef<int> Mask);

/// Extract value of C at the given Offset reinterpreted as Ty. If bits past
/// the end of C are accessed, they are assumed to be poison.
Constant *ConstantFoldLoadFromConst(Constant *C, Type *Ty, const APInt &Offset,
                                    const DataLayout &DL);

/// Extract value of C reinterpreted as Ty. Same as previous API with zero
/// offset.
Constant *ConstantFoldLoadFromConst(Constant *C, Type *Ty,
                                    const DataLayout &DL);

/// Return the value that a load from C with offset Offset would produce if it
/// is constant and determinable. If this is not determinable, return null.
Constant *ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty, APInt Offset,
                                       const DataLayout &DL);

/// Return the value that a load from C would produce if it is constant and
/// determinable. If this is not determinable, return null.
Constant *ConstantFoldLoadFromConstPtr(Constant *C, Type *Ty,
                                       const DataLayout &DL);

/// If C is a uniform value where all bits are the same (either all zero, all
/// ones, all undef or all poison), return the corresponding uniform value in
/// the new type. If the value is not uniform or the result cannot be
/// represented, return null.
Constant *ConstantFoldLoadFromUniformValue(Constant *C, Type *Ty,
                                           const DataLayout &DL);

/// canConstantFoldCallTo - Return true if its even possible to fold a call to
/// the specified function.
bool canConstantFoldCallTo(const CallBase *Call, const Function *F);

/// ConstantFoldCall - Attempt to constant fold a call to the specified function
/// with the specified arguments, returning null if unsuccessful.
Constant *ConstantFoldCall(const CallBase *Call, Function *F,
                           ArrayRef<Constant *> Operands,
                           const TargetLibraryInfo *TLI = nullptr,
                           bool AllowNonDeterministic = true);

Constant *ConstantFoldBinaryIntrinsic(Intrinsic::ID ID, Constant *LHS,
                                      Constant *RHS, Type *Ty,
                                      Instruction *FMFSource);

/// ConstantFoldLoadThroughBitcast - try to cast constant to destination type
/// returning null if unsuccessful. Can cast pointer to pointer or pointer to
/// integer and vice versa if their sizes are equal.
Constant *ConstantFoldLoadThroughBitcast(Constant *C, Type *DestTy,
                                         const DataLayout &DL);

/// Check whether the given call has no side-effects.
/// Specifically checks for math routimes which sometimes set errno.
bool isMathLibCallNoop(const CallBase *Call, const TargetLibraryInfo *TLI);

Constant *ReadByteArrayFromGlobal(const GlobalVariable *GV, uint64_t Offset);
}

#endif
