//===- PlaceSafepoints.h - Place GC Safepoints ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Place garbage collection safepoints at appropriate locations in the IR. This
// does not make relocation semantics or variable liveness explicit.  That's
// done by RewriteStatepointsForGC.
//
// Terminology:
// - A call is said to be "parseable" if there is a stack map generated for the
// return PC of the call.  A runtime can determine where values listed in the
// deopt arguments and (after RewriteStatepointsForGC) gc arguments are located
// on the stack when the code is suspended inside such a call.  Every parse
// point is represented by a call wrapped in an gc.statepoint intrinsic.
// - A "poll" is an explicit check in the generated code to determine if the
// runtime needs the generated code to cooperate by calling a helper routine
// and thus suspending its execution at a known state. The call to the helper
// routine will be parseable.  The (gc & runtime specific) logic of a poll is
// assumed to be provided in a function of the name "gc.safepoint_poll".
//
// We aim to insert polls such that running code can quickly be brought to a
// well defined state for inspection by the collector.  In the current
// implementation, this is done via the insertion of poll sites at method entry
// and the backedge of most loops.  We try to avoid inserting more polls than
// are necessary to ensure a finite period between poll sites.  This is not
// because the poll itself is expensive in the generated code; it's not.  Polls
// do tend to impact the optimizer itself in negative ways; we'd like to avoid
// perturbing the optimization of the method as much as we can.
//
// We also need to make most call sites parseable.  The callee might execute a
// poll (or otherwise be inspected by the GC).  If so, the entire stack
// (including the suspended frame of the current method) must be parseable.
//
// This pass will insert:
// - Call parse points ("call safepoints") for any call which may need to
// reach a safepoint during the execution of the callee function.
// - Backedge safepoint polls and entry safepoint polls to ensure that
// executing code reaches a safepoint poll in a finite amount of time.
//
// We do not currently support return statepoints, but adding them would not
// be hard.  They are not required for correctness - entry safepoints are an
// alternative - but some GCs may prefer them.  Patches welcome.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_PLACESAFEPOINTS_H
#define LLVM_TRANSFORMS_SCALAR_PLACESAFEPOINTS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetLibraryInfo;

class PlaceSafepointsPass : public PassInfoMixin<PlaceSafepointsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  bool runImpl(Function &F, const TargetLibraryInfo &TLI);

  void cleanup() {}

private:
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_PLACESAFEPOINTS_H
