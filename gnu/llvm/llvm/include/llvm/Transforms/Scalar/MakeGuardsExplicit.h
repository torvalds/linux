//===-- MakeGuardsExplicit.h - Turn guard intrinsics into guard branches --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the @llvm.experimental.guard intrinsic to the new form of
// guard represented as widenable explicit branch to the deopt block. The
// difference between this pass and LowerGuardIntrinsic is that after this pass
// the guard represented as intrinsic:
//
//   call void(i1, ...) @llvm.experimental.guard(i1 %old_cond) [ "deopt"() ]
//
// transforms to a guard represented as widenable explicit branch:
//
//   %widenable_cond = call i1 @llvm.experimental.widenable.condition()
//   br i1 (%old_cond & %widenable_cond), label %guarded, label %deopt
//
// Here:
//   - The semantics of @llvm.experimental.widenable.condition allows to replace
//     %widenable_cond with the construction (%widenable_cond & %any_other_cond)
//     without loss of correctness;
//   - %guarded is the lower part of old guard intrinsic's parent block split by
//     the intrinsic call;
//   - %deopt is a block containing a sole call to @llvm.experimental.deoptimize
//     intrinsic.
//
// Therefore, this branch preserves the property of widenability.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_SCALAR_MAKEGUARDSEXPLICIT_H
#define LLVM_TRANSFORMS_SCALAR_MAKEGUARDSEXPLICIT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct MakeGuardsExplicitPass : public PassInfoMixin<MakeGuardsExplicitPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_MAKEGUARDSEXPLICIT_H
