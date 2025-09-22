//===- ReduceOperandBundes.cpp - Specialized Delta Pass -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting operand bundes from calls.
//
//===----------------------------------------------------------------------===//

#include "ReduceOperandBundles.h"
#include "Delta.h"
#include "TestRunner.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace llvm {
class Module;
} // namespace llvm

using namespace llvm;

namespace {

/// Given ChunksToKeep, produce a map of calls and indexes of operand bundles
/// to be preserved for each call.
class OperandBundleRemapper : public InstVisitor<OperandBundleRemapper> {
  Oracle &O;

public:
  DenseMap<CallBase *, std::vector<unsigned>> CallsToRefine;

  explicit OperandBundleRemapper(Oracle &O) : O(O) {}

  /// So far only CallBase sub-classes can have operand bundles.
  /// Let's see which of the operand bundles of this call are to be kept.
  void visitCallBase(CallBase &Call) {
    if (!Call.hasOperandBundles())
      return; // No bundles to begin with.

    // Insert this call into map, we will likely want to rebuild it.
    auto &OperandBundlesToKeepIndexes = CallsToRefine[&Call];
    OperandBundlesToKeepIndexes.reserve(Call.getNumOperandBundles());

    // Enumerate every operand bundle on this call.
    for (unsigned BundleIndex : seq(Call.getNumOperandBundles()))
      if (O.shouldKeep()) // Should we keep this one?
        OperandBundlesToKeepIndexes.emplace_back(BundleIndex);
  }
};

struct OperandBundleCounter : public InstVisitor<OperandBundleCounter> {
  /// How many features (in this case, operand bundles) did we count, total?
  int OperandBundeCount = 0;

  /// So far only CallBase sub-classes can have operand bundles.
  void visitCallBase(CallBase &Call) {
    // Just accumulate the total number of operand bundles.
    OperandBundeCount += Call.getNumOperandBundles();
  }
};

} // namespace

static void maybeRewriteCallWithDifferentBundles(
    CallBase *OrigCall, ArrayRef<unsigned> OperandBundlesToKeepIndexes) {
  if (OperandBundlesToKeepIndexes.size() == OrigCall->getNumOperandBundles())
    return; // Not modifying operand bundles of this call after all.

  std::vector<OperandBundleDef> NewBundles;
  NewBundles.reserve(OperandBundlesToKeepIndexes.size());

  // Actually copy over the bundles that we want to keep.
  transform(OperandBundlesToKeepIndexes, std::back_inserter(NewBundles),
            [OrigCall](unsigned Index) {
              return OperandBundleDef(OrigCall->getOperandBundleAt(Index));
            });

  // Finally actually replace the bundles on the call.
  CallBase *NewCall = CallBase::Create(OrigCall, NewBundles, OrigCall);
  OrigCall->replaceAllUsesWith(NewCall);
  OrigCall->eraseFromParent();
}

/// Removes out-of-chunk operand bundles from calls.
static void extractOperandBundesFromModule(Oracle &O,
                                           ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();
  OperandBundleRemapper R(O);
  R.visit(Program);

  for (const auto &I : R.CallsToRefine)
    maybeRewriteCallWithDifferentBundles(I.first, I.second);
}

void llvm::reduceOperandBundesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractOperandBundesFromModule,
               "Reducing Operand Bundles");
}
