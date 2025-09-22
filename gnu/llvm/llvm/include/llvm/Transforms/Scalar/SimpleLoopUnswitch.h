//===- SimpleLoopUnswitch.h - Hoist loop-invariant control flow -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H
#define LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

class LPMUpdater;
class Loop;
class StringRef;
class raw_ostream;

struct ShouldRunExtraSimpleLoopUnswitch
    : public AnalysisInfoMixin<ShouldRunExtraSimpleLoopUnswitch> {
  static AnalysisKey Key;
  struct Result {
    bool invalidate(Loop &L, const PreservedAnalyses &PA,
                    LoopAnalysisManager::Invalidator &) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it remains preserved.
      auto PAC = PA.getChecker<ShouldRunExtraSimpleLoopUnswitch>();
      return !PAC.preservedWhenStateless();
    }
  };

  Result run(Loop &L, LoopAnalysisManager &AM,
             LoopStandardAnalysisResults &AR) {
    return Result();
  }

  static bool isRequired() { return true; }
};

struct ExtraSimpleLoopUnswitchPassManager : public LoopPassManager {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    auto PA = PreservedAnalyses::all();
    if (AM.getCachedResult<ShouldRunExtraSimpleLoopUnswitch>(L))
      PA.intersect(LoopPassManager::run(L, AM, AR, U));
    PA.abandon<ShouldRunExtraSimpleLoopUnswitch>();
    return PA;
  }

  static bool isRequired() { return true; }
};

/// This pass transforms loops that contain branches or switches on loop-
/// invariant conditions to have multiple loops. For example, it turns the left
/// into the right code:
///
///  for (...)                  if (lic)
///    A                          for (...)
///    if (lic)                     A; B; C
///      B                      else
///    C                          for (...)
///                                 A; C
///
/// This can increase the size of the code exponentially (doubling it every time
/// a loop is unswitched) so we only unswitch if the resultant code will be
/// smaller than a threshold.
///
/// This pass expects LICM to be run before it to hoist invariant conditions out
/// of the loop, to make the unswitching opportunity obvious.
///
/// There is a taxonomy of unswitching that we use to classify different forms
/// of this transformaiton:
///
/// - Trival unswitching: this is when the condition can be unswitched without
///   cloning any code from inside the loop. A non-trivial unswitch requires
///   code duplication.
///
/// - Full unswitching: this is when the branch or switch is completely moved
///   from inside the loop to outside the loop. Partial unswitching removes the
///   branch from the clone of the loop but must leave a (somewhat simplified)
///   branch in the original loop. While theoretically partial unswitching can
///   be done for switches, the requirements are extreme - we need the loop
///   invariant input to the switch to be sufficient to collapse to a single
///   successor in each clone.
///
/// This pass always does trivial, full unswitching for both branches and
/// switches. For branches, it also always does trivial, partial unswitching.
///
/// If enabled (via the constructor's `NonTrivial` parameter), this pass will
/// additionally do non-trivial, full unswitching for branches and switches, and
/// will do non-trivial, partial unswitching for branches.
///
/// Because partial unswitching of switches is extremely unlikely to be possible
/// in practice and significantly complicates the implementation, this pass does
/// not currently implement that in any mode.
class SimpleLoopUnswitchPass : public PassInfoMixin<SimpleLoopUnswitchPass> {
  bool NonTrivial;
  bool Trivial;

public:
  SimpleLoopUnswitchPass(bool NonTrivial = false, bool Trivial = true)
      : NonTrivial(NonTrivial), Trivial(Trivial) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H
