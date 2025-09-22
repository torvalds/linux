//===- FixItRewriter.h - Fix-It Rewriter Diagnostic Client ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a diagnostic client adaptor that performs rewrites as
// suggested by code modification hints attached to diagnostics. It
// then forwards any diagnostics to the adapted diagnostic client.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_FRONTEND_FIXITREWRITER_H
#define LLVM_CLANG_REWRITE_FRONTEND_FIXITREWRITER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Edit/EditedSource.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class LangOptions;
class SourceManager;

class FixItOptions {
public:
  FixItOptions() = default;
  virtual ~FixItOptions();

  /// This file is about to be rewritten. Return the name of the file
  /// that is okay to write to.
  ///
  /// \param fd out parameter for file descriptor. After the call it may be set
  /// to an open file descriptor for the returned filename, or it will be -1
  /// otherwise.
  virtual std::string RewriteFilename(const std::string &Filename, int &fd) = 0;

  /// True if files should be updated in place. RewriteFilename is only called
  /// if this is false.
  bool InPlace = false;

  /// Whether to abort fixing a file when not all errors could be fixed.
  bool FixWhatYouCan = false;

  /// Whether to only fix warnings and not errors.
  bool FixOnlyWarnings = false;

  /// If true, only pass the diagnostic to the actual diagnostic consumer
  /// if it is an error or a fixit was applied as part of the diagnostic.
  /// It basically silences warnings without accompanying fixits.
  bool Silent = false;
};

class FixItRewriter : public DiagnosticConsumer {
  /// The diagnostics machinery.
  DiagnosticsEngine &Diags;

  edit::EditedSource Editor;

  /// The rewriter used to perform the various code
  /// modifications.
  Rewriter Rewrite;

  /// The diagnostic client that performs the actual formatting
  /// of error messages.
  DiagnosticConsumer *Client;
  std::unique_ptr<DiagnosticConsumer> Owner;

  /// Turn an input path into an output path. NULL implies overwriting
  /// the original.
  FixItOptions *FixItOpts;

  /// The number of rewriter failures.
  unsigned NumFailures = 0;

  /// Whether the previous diagnostic was not passed to the consumer.
  bool PrevDiagSilenced = false;

public:
  /// Initialize a new fix-it rewriter.
  FixItRewriter(DiagnosticsEngine &Diags, SourceManager &SourceMgr,
                const LangOptions &LangOpts, FixItOptions *FixItOpts);

  /// Destroy the fix-it rewriter.
  ~FixItRewriter() override;

  /// Check whether there are modifications for a given file.
  bool IsModified(FileID ID) const {
    return Rewrite.getRewriteBufferFor(ID) != nullptr;
  }

  using iterator = Rewriter::buffer_iterator;

  // Iteration over files with changes.
  iterator buffer_begin() { return Rewrite.buffer_begin(); }
  iterator buffer_end() { return Rewrite.buffer_end(); }

  /// Write a single modified source file.
  ///
  /// \returns true if there was an error, false otherwise.
  bool WriteFixedFile(FileID ID, raw_ostream &OS);

  /// Write the modified source files.
  ///
  /// \returns true if there was an error, false otherwise.
  bool WriteFixedFiles(
    std::vector<std::pair<std::string, std::string>> *RewrittenFiles = nullptr);

  /// IncludeInDiagnosticCounts - This method (whose default implementation
  /// returns true) indicates whether the diagnostics handled by this
  /// DiagnosticConsumer should be included in the number of diagnostics
  /// reported by DiagnosticsEngine.
  bool IncludeInDiagnosticCounts() const override;

  /// HandleDiagnostic - Handle this diagnostic, reporting it to the user or
  /// capturing it to a log as needed.
  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override;

  /// Emit a diagnostic via the adapted diagnostic client.
  void Diag(SourceLocation Loc, unsigned DiagID);
};

} // namespace clang

#endif // LLVM_CLANG_REWRITE_FRONTEND_FIXITREWRITER_H
