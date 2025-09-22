//===- TypePromotion.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for type promotion.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TYPEPROMOTION_H
#define LLVM_CODEGEN_TYPEPROMOTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

class TypePromotionPass : public PassInfoMixin<TypePromotionPass> {
private:
  const TargetMachine *TM;

public:
  TypePromotionPass(const TargetMachine *TM): TM(TM) { }
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TYPEPROMOTION_H
