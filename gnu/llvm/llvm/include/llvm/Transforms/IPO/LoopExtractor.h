//===- LoopExtractor.h - Extract each loop into a new function ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pass wrapper around the ExtractLoop() scalar transformation to extract each
// top-level loop into its own new function. If the loop is the ONLY loop in a
// given function, it is not touched. This is a pass most useful for debugging
// via bugpoint.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_LOOPEXTRACTOR_H
#define LLVM_TRANSFORMS_IPO_LOOPEXTRACTOR_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct LoopExtractorPass : public PassInfoMixin<LoopExtractorPass> {
  LoopExtractorPass(unsigned NumLoops = ~0) : NumLoops(NumLoops) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  unsigned NumLoops;
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_LOOPEXTRACTOR_H
