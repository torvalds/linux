//===- VerifyDiagnosticConsumer.h - Verifying Diagnostic Client -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_VERIFYDIAGNOSTICCONSUMER_H
#define LLVM_CLANG_FRONTEND_VERIFYDIAGNOSTICCONSUMER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace clang {

class FileEntry;
class LangOptions;
class SourceManager;
class TextDiagnosticBuffer;

/// VerifyDiagnosticConsumer - Create a diagnostic client which will use
/// markers in the input source to check that all the emitted diagnostics match
/// those expected. See clang/docs/InternalsManual.rst for details about how to
/// write tests to verify diagnostics.
///
class VerifyDiagnosticConsumer: public DiagnosticConsumer,
                                public CommentHandler {
public:
  /// Directive - Abstract class representing a parsed verify directive.
  ///
  class Directive {
  public:
    static std::unique_ptr<Directive>
    create(bool RegexKind, SourceLocation DirectiveLoc,
           SourceLocation DiagnosticLoc, bool MatchAnyFileAndLine,
           bool MatchAnyLine, StringRef Text, unsigned Min, unsigned Max);

  public:
    /// Constant representing n or more matches.
    static const unsigned MaxCount = std::numeric_limits<unsigned>::max();

    SourceLocation DirectiveLoc;
    SourceLocation DiagnosticLoc;
    const std::string Text;
    unsigned Min, Max;
    bool MatchAnyLine;
    bool MatchAnyFileAndLine; // `MatchAnyFileAndLine` implies `MatchAnyLine`.

    Directive(const Directive &) = delete;
    Directive &operator=(const Directive &) = delete;
    virtual ~Directive() = default;

    // Returns true if directive text is valid.
    // Otherwise returns false and populates E.
    virtual bool isValid(std::string &Error) = 0;

    // Returns true on match.
    virtual bool match(StringRef S) = 0;

  protected:
    Directive(SourceLocation DirectiveLoc, SourceLocation DiagnosticLoc,
              bool MatchAnyFileAndLine, bool MatchAnyLine, StringRef Text,
              unsigned Min, unsigned Max)
        : DirectiveLoc(DirectiveLoc), DiagnosticLoc(DiagnosticLoc), Text(Text),
          Min(Min), Max(Max), MatchAnyLine(MatchAnyLine || MatchAnyFileAndLine),
          MatchAnyFileAndLine(MatchAnyFileAndLine) {
      assert(!DirectiveLoc.isInvalid() && "DirectiveLoc is invalid!");
      assert((!DiagnosticLoc.isInvalid() || MatchAnyLine) &&
             "DiagnosticLoc is invalid!");
    }
  };

  using DirectiveList = std::vector<std::unique_ptr<Directive>>;

  /// ExpectedData - owns directive objects and deletes on destructor.
  struct ExpectedData {
    DirectiveList Errors;
    DirectiveList Warnings;
    DirectiveList Remarks;
    DirectiveList Notes;

    void Reset() {
      Errors.clear();
      Warnings.clear();
      Remarks.clear();
      Notes.clear();
    }
  };

  enum DirectiveStatus {
    HasNoDirectives,
    HasNoDirectivesReported,
    HasExpectedNoDiagnostics,
    HasOtherExpectedDirectives
  };

  class MarkerTracker;

private:
  DiagnosticsEngine &Diags;
  DiagnosticConsumer *PrimaryClient;
  std::unique_ptr<DiagnosticConsumer> PrimaryClientOwner;
  std::unique_ptr<TextDiagnosticBuffer> Buffer;
  std::unique_ptr<MarkerTracker> Markers;
  const Preprocessor *CurrentPreprocessor = nullptr;
  const LangOptions *LangOpts = nullptr;
  SourceManager *SrcManager = nullptr;
  unsigned ActiveSourceFiles = 0;
  DirectiveStatus Status;
  ExpectedData ED;

  void CheckDiagnostics();

  void setSourceManager(SourceManager &SM) {
    assert((!SrcManager || SrcManager == &SM) && "SourceManager changed!");
    SrcManager = &SM;
  }

  // These facilities are used for validation in debug builds.
  class UnparsedFileStatus {
    OptionalFileEntryRef File;
    bool FoundDirectives;

  public:
    UnparsedFileStatus(OptionalFileEntryRef File, bool FoundDirectives)
        : File(File), FoundDirectives(FoundDirectives) {}

    OptionalFileEntryRef getFile() const { return File; }
    bool foundDirectives() const { return FoundDirectives; }
  };

  using ParsedFilesMap = llvm::DenseMap<FileID, const FileEntry *>;
  using UnparsedFilesMap = llvm::DenseMap<FileID, UnparsedFileStatus>;

  ParsedFilesMap ParsedFiles;
  UnparsedFilesMap UnparsedFiles;

public:
  /// Create a new verifying diagnostic client, which will issue errors to
  /// the currently-attached diagnostic client when a diagnostic does not match
  /// what is expected (as indicated in the source file).
  VerifyDiagnosticConsumer(DiagnosticsEngine &Diags);
  ~VerifyDiagnosticConsumer() override;

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *PP) override;

  void EndSourceFile() override;

  enum ParsedStatus {
    /// File has been processed via HandleComment.
    IsParsed,

    /// File has diagnostics and may have directives.
    IsUnparsed,

    /// File has diagnostics but guaranteed no directives.
    IsUnparsedNoDirectives
  };

  /// Update lists of parsed and unparsed files.
  void UpdateParsedFileStatus(SourceManager &SM, FileID FID, ParsedStatus PS);

  bool HandleComment(Preprocessor &PP, SourceRange Comment) override;

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override;
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_VERIFYDIAGNOSTICCONSUMER_H
