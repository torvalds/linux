//===--------- Definition of the SanitizerCoverage class --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SanitizerCoverage is a simple code coverage implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERCOVERAGE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERCOVERAGE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Transforms/Instrumentation.h"

namespace llvm {
class Module;

/// This is the ModuleSanitizerCoverage pass used in the new pass manager. The
/// pass instruments functions for coverage, adds initialization calls to the
/// module for trace PC guards and 8bit counters if they are requested, and
/// appends globals to llvm.compiler.used.
class SanitizerCoveragePass : public PassInfoMixin<SanitizerCoveragePass> {
public:
  explicit SanitizerCoveragePass(
      SanitizerCoverageOptions Options = SanitizerCoverageOptions(),
      const std::vector<std::string> &AllowlistFiles =
          std::vector<std::string>(),
      const std::vector<std::string> &BlocklistFiles =
          std::vector<std::string>())
      : Options(Options) {
    if (AllowlistFiles.size() > 0)
      Allowlist = SpecialCaseList::createOrDie(AllowlistFiles,
                                               *vfs::getRealFileSystem());
    if (BlocklistFiles.size() > 0)
      Blocklist = SpecialCaseList::createOrDie(BlocklistFiles,
                                               *vfs::getRealFileSystem());
  }
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  SanitizerCoverageOptions Options;

  std::unique_ptr<SpecialCaseList> Allowlist;
  std::unique_ptr<SpecialCaseList> Blocklist;
};

} // namespace llvm

#endif
