//===--- QualifierAlignmentFixer.h -------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares QualifierAlignmentFixer, a TokenAnalyzer that
/// enforces either east or west const depending on the style.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_QUALIFIERALIGNMENTFIXER_H
#define LLVM_CLANG_LIB_FORMAT_QUALIFIERALIGNMENTFIXER_H

#include "TokenAnalyzer.h"

namespace clang {
namespace format {

typedef std::function<std::pair<tooling::Replacements, unsigned>(
    const Environment &)>
    AnalyzerPass;

void addQualifierAlignmentFixerPasses(const FormatStyle &Style,
                                      SmallVectorImpl<AnalyzerPass> &Passes);

void prepareLeftRightOrderingForQualifierAlignmentFixer(
    const std::vector<std::string> &Order, std::vector<std::string> &LeftOrder,
    std::vector<std::string> &RightOrder,
    std::vector<tok::TokenKind> &Qualifiers);

// Is the Token a simple or qualifier type
bool isQualifierOrType(const FormatToken *Tok, const LangOptions &LangOpts);
bool isConfiguredQualifierOrType(const FormatToken *Tok,
                                 const std::vector<tok::TokenKind> &Qualifiers,
                                 const LangOptions &LangOpts);

// Is the Token likely a Macro
bool isPossibleMacro(const FormatToken *Tok);

class LeftRightQualifierAlignmentFixer : public TokenAnalyzer {
  std::string Qualifier;
  bool RightAlign;
  SmallVector<tok::TokenKind, 8> QualifierTokens;
  std::vector<tok::TokenKind> ConfiguredQualifierTokens;

public:
  LeftRightQualifierAlignmentFixer(
      const Environment &Env, const FormatStyle &Style,
      const std::string &Qualifier,
      const std::vector<tok::TokenKind> &ConfiguredQualifierTokens,
      bool RightAlign);

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override;

  static tok::TokenKind getTokenFromQualifier(const std::string &Qualifier);

  void fixQualifierAlignment(SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
                             FormatTokenLexer &Tokens,
                             tooling::Replacements &Fixes);

  const FormatToken *analyzeRight(const SourceManager &SourceMgr,
                                  const AdditionalKeywords &Keywords,
                                  tooling::Replacements &Fixes,
                                  const FormatToken *Tok,
                                  const std::string &Qualifier,
                                  tok::TokenKind QualifierType);

  const FormatToken *analyzeLeft(const SourceManager &SourceMgr,
                                 const AdditionalKeywords &Keywords,
                                 tooling::Replacements &Fixes,
                                 const FormatToken *Tok,
                                 const std::string &Qualifier,
                                 tok::TokenKind QualifierType);
};

} // end namespace format
} // end namespace clang

#endif
