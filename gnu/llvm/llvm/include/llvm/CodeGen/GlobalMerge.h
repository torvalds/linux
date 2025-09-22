//===- llvm/CodeGen/GlobalMerge.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALMERGE_H
#define LLVM_CODEGEN_GLOBALMERGE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

struct GlobalMergeOptions {
  // FIXME: Infer the maximum possible offset depending on the actual users
  // (these max offsets are different for the users inside Thumb or ARM
  // functions), see the code that passes in the offset in the ARM backend
  // for more information.
  unsigned MaxOffset = 0;
  // The minimum size in bytes of each global that should considered in merging.
  unsigned MinSize = 0;
  bool GroupByUse = true;
  bool IgnoreSingleUse = true;
  bool MergeConst = false;
  /// Whether we should merge global variables that have external linkage.
  bool MergeExternal = true;
  /// Whether we should try to optimize for size only.
  /// Currently, this applies a dead simple heuristic: only consider globals
  /// used in minsize functions for merging.
  /// FIXME: This could learn about optsize, and be used in the cost model.
  bool SizeOnly = false;
};

// FIXME: This pass must run before AsmPrinterPass::doInitialization!
class GlobalMergePass : public PassInfoMixin<GlobalMergePass> {
  const TargetMachine *TM;
  GlobalMergeOptions Options;

public:
  GlobalMergePass(const TargetMachine *TM, GlobalMergeOptions Options)
      : TM(TM), Options(Options) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALMERGE_H
