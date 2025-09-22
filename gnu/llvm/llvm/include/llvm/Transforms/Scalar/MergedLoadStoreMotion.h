//===- MergedLoadStoreMotion.h - merge and hoist/sink load/stores ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//! \file
//! This pass performs merges of loads and stores on both sides of a
//  diamond (hammock). It hoists the loads and sinks the stores.
//
// The algorithm iteratively hoists two loads to the same address out of a
// diamond (hammock) and merges them into a single load in the header. Similar
// it sinks and merges two stores to the tail block (footer). The algorithm
// iterates over the instructions of one side of the diamond and attempts to
// find a matching load/store on the other side. It hoists / sinks when it
// thinks it safe to do so.  This optimization helps with eg. hiding load
// latencies, triggering if-conversion, and reducing static code size.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
#define LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;
struct MergedLoadStoreMotionOptions {
  bool SplitFooterBB;
  MergedLoadStoreMotionOptions(bool SplitFooterBB = false)
      : SplitFooterBB(SplitFooterBB) {}

  MergedLoadStoreMotionOptions &splitFooterBB(bool SFBB) {
    SplitFooterBB = SFBB;
    return *this;
  }
};

class MergedLoadStoreMotionPass
    : public PassInfoMixin<MergedLoadStoreMotionPass> {
  MergedLoadStoreMotionOptions Options;

public:
  MergedLoadStoreMotionPass()
      : MergedLoadStoreMotionPass(MergedLoadStoreMotionOptions()) {}
  MergedLoadStoreMotionPass(const MergedLoadStoreMotionOptions &PassOptions)
      : Options(PassOptions) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
