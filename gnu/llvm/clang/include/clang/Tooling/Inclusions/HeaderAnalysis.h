//===--- HeaderAnalysis.h -----------------------------------------*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_INCLUSIONS_HEADER_ANALYSIS_H
#define LLVM_CLANG_TOOLING_INCLUSIONS_HEADER_ANALYSIS_H

#include "clang/Basic/FileEntry.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

namespace clang {
class SourceManager;
class HeaderSearch;

namespace tooling {

/// Returns true if the given physical file is a self-contained header.
///
/// A header is considered self-contained if
//   - it has a proper header guard or has been #imported or contains #import(s)
//   - *and* it doesn't have a dont-include-me pattern.
///
/// This function can be expensive as it may scan the source code to find out
/// dont-include-me pattern heuristically.
bool isSelfContainedHeader(FileEntryRef FE, const SourceManager &SM,
                           const HeaderSearch &HeaderInfo);

/// This scans the given source code to see if it contains #import(s).
bool codeContainsImports(llvm::StringRef Code);

/// If Text begins an Include-What-You-Use directive, returns it.
/// Given "// IWYU pragma: keep", returns "keep".
/// Input is a null-terminated char* as provided by SM.getCharacterData().
/// (This should not be StringRef as we do *not* want to scan for its length).
/// For multi-line comments, we return only the first line.
std::optional<llvm::StringRef> parseIWYUPragma(const char *Text);

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_INCLUSIONS_HEADER_ANALYSIS_H
