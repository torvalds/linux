//===---------- IssueHash.h - Generate identification hashes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_ISSUEHASH_H
#define LLVM_CLANG_ANALYSIS_ISSUEHASH_H

#include "llvm/ADT/SmallString.h"

namespace clang {
class Decl;
class FullSourceLoc;
class LangOptions;

/// Returns an opaque identifier for a diagnostic.
///
/// This opaque identifier is intended to be stable even when the source code
/// is changed. It allows to track diagnostics in the long term, for example,
/// find which diagnostics are "new", maintain a database of suppressed
/// diagnostics etc.
///
/// We may introduce more variants of issue hashes in the future
/// but older variants will still be available for compatibility.
///
/// This hash is based on the following information:
///   - Name of the checker that emitted the diagnostic.
///   - Warning message.
///   - Name of the enclosing declaration.
///   - Contents of the line of code with the issue, excluding whitespace.
///   - Column number (but not the line number! - which makes it stable).
llvm::SmallString<32> getIssueHash(const FullSourceLoc &IssueLoc,
                                   llvm::StringRef CheckerName,
                                   llvm::StringRef WarningMessage,
                                   const Decl *IssueDecl,
                                   const LangOptions &LangOpts);

/// Get the unhashed string representation of the V1 issue hash.
/// When hashed, it becomes the actual issue hash. Useful for testing.
/// See GetIssueHashV1() for more information.
std::string getIssueString(const FullSourceLoc &IssueLoc,
                           llvm::StringRef CheckerName,
                           llvm::StringRef WarningMessage,
                           const Decl *IssueDecl, const LangOptions &LangOpts);
} // namespace clang

#endif
