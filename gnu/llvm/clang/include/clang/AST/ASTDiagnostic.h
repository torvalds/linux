//===--- ASTDiagnostic.h - Diagnostics for the AST library ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTDIAGNOSTIC_H
#define LLVM_CLANG_AST_ASTDIAGNOSTIC_H

#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticAST.h"

namespace clang {
  /// DiagnosticsEngine argument formatting function for diagnostics that
  /// involve AST nodes.
  ///
  /// This function formats diagnostic arguments for various AST nodes,
  /// including types, declaration names, nested name specifiers, and
  /// declaration contexts, into strings that can be printed as part of
  /// diagnostics. It is meant to be used as the argument to
  /// \c DiagnosticsEngine::SetArgToStringFn(), where the cookie is an \c
  /// ASTContext pointer.
  void FormatASTNodeDiagnosticArgument(
      DiagnosticsEngine::ArgumentKind Kind,
      intptr_t Val,
      StringRef Modifier,
      StringRef Argument,
      ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
      SmallVectorImpl<char> &Output,
      void *Cookie,
      ArrayRef<intptr_t> QualTypeVals);

  /// Returns a desugared version of the QualType, and marks ShouldAKA as true
  /// whenever we remove significant sugar from the type. Make sure ShouldAKA
  /// is initialized before passing it in.
  QualType desugarForDiagnostic(ASTContext &Context, QualType QT,
                                bool &ShouldAKA);
}  // end namespace clang

#endif
