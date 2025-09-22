//===--- Diagnostic.h - Framework for clang diagnostics tools --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
//  Structures supporting diagnostics and refactorings that span multiple
//  translation units. Indicate diagnostics reports and replacements
//  suggestions for the analyzed sources.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H
#define LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H

#include "Replacement.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {
namespace tooling {

/// Represents a range within a specific source file.
struct FileByteRange {
  FileByteRange() = default;

  FileByteRange(const SourceManager &Sources, CharSourceRange Range);

  std::string FilePath;
  unsigned FileOffset;
  unsigned Length;
};

/// Represents the diagnostic message with the error message associated
/// and the information on the location of the problem.
struct DiagnosticMessage {
  DiagnosticMessage(llvm::StringRef Message = "");

  /// Constructs a diagnostic message with anoffset to the diagnostic
  /// within the file where the problem occurred.
  ///
  /// \param Loc Should be a file location, it is not meaningful for a macro
  /// location.
  ///
  DiagnosticMessage(llvm::StringRef Message, const SourceManager &Sources,
                    SourceLocation Loc);

  std::string Message;
  std::string FilePath;
  unsigned FileOffset;

  /// Fixes for this diagnostic, grouped by file path.
  llvm::StringMap<Replacements> Fix;

  /// Extra source ranges associated with the note, in addition to the location
  /// of the Message itself.
  llvm::SmallVector<FileByteRange, 1> Ranges;
};

/// Represents the diagnostic with the level of severity and possible
/// fixes to be applied.
struct Diagnostic {
  enum Level {
    Remark = DiagnosticsEngine::Remark,
    Warning = DiagnosticsEngine::Warning,
    Error = DiagnosticsEngine::Error
  };

  Diagnostic() = default;

  Diagnostic(llvm::StringRef DiagnosticName, Level DiagLevel,
             StringRef BuildDirectory);

  Diagnostic(llvm::StringRef DiagnosticName, const DiagnosticMessage &Message,
             const SmallVector<DiagnosticMessage, 1> &Notes, Level DiagLevel,
             llvm::StringRef BuildDirectory);

  /// Name identifying the Diagnostic.
  std::string DiagnosticName;

  /// Message associated to the diagnostic.
  DiagnosticMessage Message;

  /// Potential notes about the diagnostic.
  SmallVector<DiagnosticMessage, 1> Notes;

  /// Diagnostic level. Can indicate either an error or a warning.
  Level DiagLevel;

  /// A build directory of the diagnostic source file.
  ///
  /// It's an absolute path which is `directory` field of the source file in
  /// compilation database. If users don't specify the compilation database
  /// directory, it is the current directory where clang-tidy runs.
  ///
  /// Note: it is empty in unittest.
  std::string BuildDirectory;
};

/// Collection of Diagnostics generated from a single translation unit.
struct TranslationUnitDiagnostics {
  /// Name of the main source for the translation unit.
  std::string MainSourceFile;
  std::vector<Diagnostic> Diagnostics;
};

/// Get the first fix to apply for this diagnostic.
/// \returns nullptr if no fixes are attached to the diagnostic.
const llvm::StringMap<Replacements> *selectFirstFix(const Diagnostic& D);

} // end namespace tooling
} // end namespace clang
#endif // LLVM_CLANG_TOOLING_CORE_DIAGNOSTIC_H
