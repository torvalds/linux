//===- EntryExitInstrumenter.h - Function Entry/Exit Instrumentation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EntryExitInstrumenter pass - Instrument function entry/exit with calls to
// mcount(), @__cyg_profile_func_{enter,exit} and the like. There are two
// variants, intended to run pre- and post-inlining, respectively.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
#define LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct EntryExitInstrumenterPass
    : public PassInfoMixin<EntryExitInstrumenterPass> {
  EntryExitInstrumenterPass(bool PostInlining) : PostInlining(PostInlining) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  bool PostInlining;

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
