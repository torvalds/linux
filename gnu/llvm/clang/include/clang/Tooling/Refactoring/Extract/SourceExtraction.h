//===--- SourceExtraction.cpp - Clang refactoring library -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_EXTRACT_SOURCEEXTRACTION_H
#define LLVM_CLANG_TOOLING_REFACTORING_EXTRACT_SOURCEEXTRACTION_H

#include "clang/Basic/LLVM.h"

namespace clang {

class LangOptions;
class SourceManager;
class SourceRange;
class Stmt;

namespace tooling {

/// Determines which semicolons should be inserted during extraction.
class ExtractionSemicolonPolicy {
public:
  bool isNeededInExtractedFunction() const {
    return IsNeededInExtractedFunction;
  }

  bool isNeededInOriginalFunction() const { return IsNeededInOriginalFunction; }

  /// Returns the semicolon insertion policy that is needed for extraction of
  /// the given statement from the given source range.
  static ExtractionSemicolonPolicy compute(const Stmt *S,
                                           SourceRange &ExtractedRange,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts);

private:
  ExtractionSemicolonPolicy(bool IsNeededInExtractedFunction,
                            bool IsNeededInOriginalFunction)
      : IsNeededInExtractedFunction(IsNeededInExtractedFunction),
        IsNeededInOriginalFunction(IsNeededInOriginalFunction) {}
  bool IsNeededInExtractedFunction;
  bool IsNeededInOriginalFunction;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_EXTRACT_SOURCEEXTRACTION_H
