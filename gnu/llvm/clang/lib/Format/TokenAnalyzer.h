//===--- TokenAnalyzer.h - Analyze Token Streams ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares an abstract TokenAnalyzer, and associated helper
/// classes. TokenAnalyzer can be extended to generate replacements based on
/// an annotated and pre-processed token stream.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_TOKENANALYZER_H
#define LLVM_CLANG_LIB_FORMAT_TOKENANALYZER_H

#include "AffectedRangeManager.h"
#include "FormatTokenLexer.h"
#include "TokenAnnotator.h"

namespace clang {
namespace format {

class Environment {
public:
  // This sets up an virtual file system with file \p FileName containing the
  // fragment \p Code. Assumes that \p Code starts at \p FirstStartColumn,
  // that the next lines of \p Code should start at \p NextStartColumn, and
  // that \p Code should end at \p LastStartColumn if it ends in newline.
  // See also the documentation of clang::format::internal::reformat.
  Environment(StringRef Code, StringRef FileName, unsigned FirstStartColumn = 0,
              unsigned NextStartColumn = 0, unsigned LastStartColumn = 0);

  FileID getFileID() const { return ID; }

  SourceManager &getSourceManager() const { return SM; }

  ArrayRef<CharSourceRange> getCharRanges() const { return CharRanges; }

  // Returns the column at which the fragment of code managed by this
  // environment starts.
  unsigned getFirstStartColumn() const { return FirstStartColumn; }

  // Returns the column at which subsequent lines of the fragment of code
  // managed by this environment should start.
  unsigned getNextStartColumn() const { return NextStartColumn; }

  // Returns the column at which the fragment of code managed by this
  // environment should end if it ends in a newline.
  unsigned getLastStartColumn() const { return LastStartColumn; }

  // Returns nullptr and prints a diagnostic to stderr if the environment
  // can't be created.
  static std::unique_ptr<Environment> make(StringRef Code, StringRef FileName,
                                           ArrayRef<tooling::Range> Ranges,
                                           unsigned FirstStartColumn = 0,
                                           unsigned NextStartColumn = 0,
                                           unsigned LastStartColumn = 0);

private:
  // This is only set if constructed from string.
  std::unique_ptr<SourceManagerForFile> VirtualSM;

  // This refers to either a SourceManager provided by users or VirtualSM
  // created for a single file.
  SourceManager &SM;
  FileID ID;

  SmallVector<CharSourceRange, 8> CharRanges;
  unsigned FirstStartColumn;
  unsigned NextStartColumn;
  unsigned LastStartColumn;
};

class TokenAnalyzer : public UnwrappedLineConsumer {
public:
  TokenAnalyzer(const Environment &Env, const FormatStyle &Style);

  std::pair<tooling::Replacements, unsigned>
  process(bool SkipAnnotation = false);

protected:
  virtual std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) = 0;

  void consumeUnwrappedLine(const UnwrappedLine &TheLine) override;

  void finishRun() override;

  FormatStyle Style;
  LangOptions LangOpts;
  // Stores Style, FileID and SourceManager etc.
  const Environment &Env;
  // AffectedRangeMgr stores ranges to be fixed.
  AffectedRangeManager AffectedRangeMgr;
  SmallVector<SmallVector<UnwrappedLine, 16>, 2> UnwrappedLines;
  encoding::Encoding Encoding;
};

} // end namespace format
} // end namespace clang

#endif
