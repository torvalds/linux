//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the primary interface to the instcombine pass. This pass
/// is suitable for use in the new pass manager. For a pass that works with the
/// legacy pass manager, use \c createInstructionCombiningPass().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H
#define LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

namespace llvm {

static constexpr unsigned InstCombineDefaultMaxIterations = 1;

struct InstCombineOptions {
  bool UseLoopInfo = false;
  // Verify that a fix point has been reached after MaxIterations.
  bool VerifyFixpoint = false;
  unsigned MaxIterations = InstCombineDefaultMaxIterations;

  InstCombineOptions() = default;

  InstCombineOptions &setUseLoopInfo(bool Value) {
    UseLoopInfo = Value;
    return *this;
  }

  InstCombineOptions &setVerifyFixpoint(bool Value) {
    VerifyFixpoint = Value;
    return *this;
  }

  InstCombineOptions &setMaxIterations(unsigned Value) {
    MaxIterations = Value;
    return *this;
  }
};

class InstCombinePass : public PassInfoMixin<InstCombinePass> {
private:
  InstructionWorklist Worklist;
  InstCombineOptions Options;

public:
  explicit InstCombinePass(InstCombineOptions Opts = {});
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// The legacy pass manager's instcombine pass.
///
/// This is a basic whole-function wrapper around the instcombine utility. It
/// will try to combine all instructions in the function.
class InstructionCombiningPass : public FunctionPass {
  InstructionWorklist Worklist;

public:
  static char ID; // Pass identification, replacement for typeid

  explicit InstructionCombiningPass();

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};

//===----------------------------------------------------------------------===//
//
// InstructionCombining - Combine instructions to form fewer, simple
// instructions. This pass does not modify the CFG, and has a tendency to make
// instructions dead, so a subsequent DCE pass is useful.
//
// This pass combines things like:
//    %Y = add int 1, %X
//    %Z = add int 1, %Y
// into:
//    %Z = add int 2, %X
//
FunctionPass *createInstructionCombiningPass();
}

#undef DEBUG_TYPE

#endif
