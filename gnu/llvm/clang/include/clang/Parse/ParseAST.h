//===--- ParseAST.h - Define the ParseAST method ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the clang::ParseAST method.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_PARSEAST_H
#define LLVM_CLANG_PARSE_PARSEAST_H

#include "clang/Basic/LangOptions.h"

namespace clang {
  class Preprocessor;
  class ASTConsumer;
  class ASTContext;
  class CodeCompleteConsumer;
  class Sema;

  /// Parse the entire file specified, notifying the ASTConsumer as
  /// the file is parsed.
  ///
  /// This operation inserts the parsed decls into the translation
  /// unit held by Ctx.
  ///
  /// \param PrintStats Whether to print LLVM statistics related to parsing.
  /// \param TUKind The kind of translation unit being parsed.
  /// \param CompletionConsumer If given, an object to consume code completion
  /// results.
  /// \param SkipFunctionBodies Whether to skip parsing of function bodies.
  /// This option can be used, for example, to speed up searches for
  /// declarations/definitions when indexing.
  void ParseAST(Preprocessor &pp, ASTConsumer *C,
                ASTContext &Ctx, bool PrintStats = false,
                TranslationUnitKind TUKind = TU_Complete,
                CodeCompleteConsumer *CompletionConsumer = nullptr,
                bool SkipFunctionBodies = false);

  /// Parse the main file known to the preprocessor, producing an
  /// abstract syntax tree.
  void ParseAST(Sema &S, bool PrintStats = false,
                bool SkipFunctionBodies = false);

}  // end namespace clang

#endif
