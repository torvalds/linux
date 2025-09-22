//===--- CommentBriefParser.h - Dumb comment parser -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a very simple Doxygen comment parser.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CLANG_AST_COMMENTBRIEFPARSER_H
#define LLVM_CLANG_AST_COMMENTBRIEFPARSER_H

#include "clang/AST/CommentLexer.h"

namespace clang {
namespace comments {

/// A very simple comment parser that extracts "a brief description".
///
/// Due to a variety of comment styles, it considers the following as "a brief
/// description", in order of priority:
/// \li a \or \\short command,
/// \li the first paragraph,
/// \li a \\result or \\return or \\returns paragraph.
class BriefParser {
  Lexer &L;

  const CommandTraits &Traits;

  /// Current lookahead token.
  Token Tok;

  SourceLocation ConsumeToken() {
    SourceLocation Loc = Tok.getLocation();
    L.lex(Tok);
    return Loc;
  }

public:
  BriefParser(Lexer &L, const CommandTraits &Traits);

  /// Return the best "brief description" we can find.
  std::string Parse();
};

} // end namespace comments
} // end namespace clang

#endif

