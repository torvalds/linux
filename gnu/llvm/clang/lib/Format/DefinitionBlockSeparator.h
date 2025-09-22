//===--- DefinitionBlockSeparator.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares DefinitionBlockSeparator, a TokenAnalyzer that inserts or
/// removes empty lines separating definition blocks like classes, structs,
/// functions, enums, and namespaces in between.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_DEFINITIONBLOCKSEPARATOR_H
#define LLVM_CLANG_LIB_FORMAT_DEFINITIONBLOCKSEPARATOR_H

#include "TokenAnalyzer.h"
#include "WhitespaceManager.h"

namespace clang {
namespace format {
class DefinitionBlockSeparator : public TokenAnalyzer {
public:
  DefinitionBlockSeparator(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override;

private:
  void separateBlocks(SmallVectorImpl<AnnotatedLine *> &Lines,
                      tooling::Replacements &Result, FormatTokenLexer &Tokens);
};
} // namespace format
} // namespace clang

#endif
