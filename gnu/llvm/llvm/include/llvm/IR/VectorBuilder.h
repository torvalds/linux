//===- llvm/VectorBuilder.h - Builder for VP Intrinsics ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the VectorBuilder class, which is used as a convenient way
// to create VP intrinsics as if they were LLVM instructions with a consistent
// and simplified interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VECTORBUILDER_H
#define LLVM_IR_VECTORBUILDER_H

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

namespace llvm {

class VectorBuilder {
public:
  enum class Behavior {
    // Abort if the requested VP intrinsic could not be created.
    // This is useful for strict consistency.
    ReportAndAbort = 0,

    // Return a default-initialized value if the requested VP intrinsic could
    // not be created.
    // This is useful for a defensive fallback to non-VP code.
    SilentlyReturnNone = 1,
  };

private:
  IRBuilderBase &Builder;
  Behavior ErrorHandling;

  // Explicit mask parameter.
  Value *Mask;
  // Explicit vector length parameter.
  Value *ExplicitVectorLength;
  // Compile-time vector length.
  ElementCount StaticVectorLength;

  // Get mask/evl value handles for the current configuration.
  Value &requestMask();
  Value &requestEVL();

  void handleError(const char *ErrorMsg) const;
  template <typename RetType>
  RetType returnWithError(const char *ErrorMsg) const {
    handleError(ErrorMsg);
    return RetType();
  }

  /// Helper function for creating VP intrinsic call.
  Value *createVectorInstructionImpl(Intrinsic::ID VPID, Type *ReturnTy,
                                     ArrayRef<Value *> VecOpArray,
                                     const Twine &Name = Twine());

public:
  VectorBuilder(IRBuilderBase &Builder,
                Behavior ErrorHandling = Behavior::ReportAndAbort)
      : Builder(Builder), ErrorHandling(ErrorHandling), Mask(nullptr),
        ExplicitVectorLength(nullptr),
        StaticVectorLength(ElementCount::getFixed(0)) {}

  Module &getModule() const;
  LLVMContext &getContext() const { return Builder.getContext(); }

  // All-true mask for the currently configured explicit vector length.
  Value *getAllTrueMask();

  VectorBuilder &setMask(Value *NewMask) {
    Mask = NewMask;
    return *this;
  }
  VectorBuilder &setEVL(Value *NewExplicitVectorLength) {
    ExplicitVectorLength = NewExplicitVectorLength;
    return *this;
  }
  VectorBuilder &setStaticVL(unsigned NewFixedVL) {
    StaticVectorLength = ElementCount::getFixed(NewFixedVL);
    return *this;
  }
  // TODO: setStaticVL(ElementCount) for scalable types.

  // Emit a VP intrinsic call that mimics a regular instruction.
  // This operation behaves according to the VectorBuilderBehavior.
  // \p Opcode      The functional instruction opcode of the emitted intrinsic.
  // \p ReturnTy    The return type of the operation.
  // \p VecOpArray  The operand list.
  Value *createVectorInstruction(unsigned Opcode, Type *ReturnTy,
                                 ArrayRef<Value *> VecOpArray,
                                 const Twine &Name = Twine());

  /// Emit a VP reduction intrinsic call for recurrence kind.
  /// \param RdxID       The intrinsic ID of llvm.vector.reduce.*
  /// \param ValTy       The type of operand which the reduction operation is
  ///                    performed.
  /// \param VecOpArray  The operand list.
  Value *createSimpleTargetReduction(Intrinsic::ID RdxID, Type *ValTy,
                                     ArrayRef<Value *> VecOpArray,
                                     const Twine &Name = Twine());
};

} // namespace llvm

#endif // LLVM_IR_VECTORBUILDER_H
