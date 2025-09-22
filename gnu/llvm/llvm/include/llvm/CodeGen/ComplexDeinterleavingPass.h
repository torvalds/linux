//===- ComplexDeinterleavingPass.h - Complex Deinterleaving Pass *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements generation of target-specific intrinsics to support
// handling of complex number arithmetic and deinterleaving.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H
#define LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

struct ComplexDeinterleavingPass
    : public PassInfoMixin<ComplexDeinterleavingPass> {
private:
  TargetMachine *TM;

public:
  ComplexDeinterleavingPass(TargetMachine *TM) : TM(TM) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

enum class ComplexDeinterleavingOperation {
  CAdd,
  CMulPartial,
  // The following 'operations' are used to represent internal states. Backends
  // are not expected to try and support these in any capacity.
  Deinterleave,
  Splat,
  Symmetric,
  ReductionPHI,
  ReductionOperation,
  ReductionSelect,
};

enum class ComplexDeinterleavingRotation {
  Rotation_0 = 0,
  Rotation_90 = 1,
  Rotation_180 = 2,
  Rotation_270 = 3,
};

} // namespace llvm

#endif // LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H
