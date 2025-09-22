//===- Transforms/Instrumentation/MemorySanitizer.h - MSan Pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the memoy sanitizer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
class StringRef;
class raw_ostream;

struct MemorySanitizerOptions {
  MemorySanitizerOptions() : MemorySanitizerOptions(0, false, false, false){};
  MemorySanitizerOptions(int TrackOrigins, bool Recover, bool Kernel)
      : MemorySanitizerOptions(TrackOrigins, Recover, Kernel, false) {}
  MemorySanitizerOptions(int TrackOrigins, bool Recover, bool Kernel,
                         bool EagerChecks);
  bool Kernel;
  int TrackOrigins;
  bool Recover;
  bool EagerChecks;
};

/// A module pass for msan instrumentation.
///
/// Instruments functions to detect unitialized reads. This function pass
/// inserts calls to runtime library functions. If the functions aren't declared
/// yet, the pass inserts the declarations. Otherwise the existing globals are
/// used.
struct MemorySanitizerPass : public PassInfoMixin<MemorySanitizerPass> {
  MemorySanitizerPass(MemorySanitizerOptions Options) : Options(Options) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
  static bool isRequired() { return true; }

private:
  MemorySanitizerOptions Options;
};
}

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H */
