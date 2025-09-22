//===-- ObjCARC.h - ObjCARC Scalar Transformations --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the ObjCARC Scalar Transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_OBJCARC_H
#define LLVM_TRANSFORMS_OBJCARC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Pass;

//===----------------------------------------------------------------------===//
//
// ObjCARCContract - Late ObjC ARC cleanups.
//
Pass *createObjCARCContractPass();

struct ObjCARCOptPass : public PassInfoMixin<ObjCARCOptPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct ObjCARCContractPass : public PassInfoMixin<ObjCARCContractPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct ObjCARCAPElimPass : public PassInfoMixin<ObjCARCAPElimPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

struct ObjCARCExpandPass : public PassInfoMixin<ObjCARCExpandPass> {
  PreservedAnalyses run(Function &M, FunctionAnalysisManager &AM);
};

struct PAEvalPass : public PassInfoMixin<PAEvalPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // End llvm namespace

#endif
