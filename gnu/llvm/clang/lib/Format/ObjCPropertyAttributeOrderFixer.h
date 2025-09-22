//===--- ObjCPropertyAttributeOrderFixer.h ------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ObjCPropertyAttributeOrderFixer, a TokenAnalyzer that
/// adjusts the order of attributes in an ObjC `@property(...)` declaration,
/// depending on the style.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_OBJCPROPERTYATTRIBUTEORDERFIXER_H
#define LLVM_CLANG_LIB_FORMAT_OBJCPROPERTYATTRIBUTEORDERFIXER_H

#include "TokenAnalyzer.h"

namespace clang {
namespace format {

class ObjCPropertyAttributeOrderFixer : public TokenAnalyzer {
  llvm::StringMap<unsigned> SortOrderMap;

  void analyzeObjCPropertyDecl(const SourceManager &SourceMgr,
                               const AdditionalKeywords &Keywords,
                               tooling::Replacements &Fixes,
                               const FormatToken *Tok);

  void sortPropertyAttributes(const SourceManager &SourceMgr,
                              tooling::Replacements &Fixes,
                              const FormatToken *BeginTok,
                              const FormatToken *EndTok);

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override;

public:
  ObjCPropertyAttributeOrderFixer(const Environment &Env,
                                  const FormatStyle &Style);
};

} // end namespace format
} // end namespace clang

#endif
