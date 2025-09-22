//===-- Float2Int.h - Demote floating point ops to work on integers -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Float2Int pass, which aims to demote floating
// point operations to work on integers, where that is losslessly possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
#define LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class DominatorTree;
class Function;
class Instruction;
class LLVMContext;
class Type;
class Value;

class Float2IntPass : public PassInfoMixin<Float2IntPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, const DominatorTree &DT);

private:
  void findRoots(Function &F, const DominatorTree &DT);
  void seen(Instruction *I, ConstantRange R);
  ConstantRange badRange();
  ConstantRange unknownRange();
  ConstantRange validateRange(ConstantRange R);
  std::optional<ConstantRange> calcRange(Instruction *I);
  void walkBackwards();
  void walkForwards();
  bool validateAndTransform(const DataLayout &DL);
  Value *convert(Instruction *I, Type *ToTy);
  void cleanup();

  MapVector<Instruction *, ConstantRange> SeenInsts;
  SmallSetVector<Instruction *, 8> Roots;
  EquivalenceClasses<Instruction *> ECs;
  MapVector<Instruction *, Value *> ConvertedInsts;
  LLVMContext *Ctx;
};
}
#endif // LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
