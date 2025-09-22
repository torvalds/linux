//===-- ClangExpressionHelper.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONHELPER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONHELPER_H

#include <map>
#include <string>
#include <vector>

#include "lldb/Expression/ExpressionTypeSystemHelper.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace clang {
class ASTConsumer;
}

namespace lldb_private {

class ClangExpressionDeclMap;

// ClangExpressionHelper
class ClangExpressionHelper
    : public llvm::RTTIExtends<ClangExpressionHelper,
                               ExpressionTypeSystemHelper> {
public:
  // LLVM RTTI support
  static char ID;

  /// Return the object that the parser should use when resolving external
  /// values.  May be NULL if everything should be self-contained.
  virtual ClangExpressionDeclMap *DeclMap() = 0;

  /// Return the object that the parser should allow to access ASTs.
  /// May be NULL if the ASTs do not need to be transformed.
  ///
  /// \param[in] passthrough
  ///     The ASTConsumer that the returned transformer should send
  ///     the ASTs to after transformation.
  virtual clang::ASTConsumer *
  ASTTransformer(clang::ASTConsumer *passthrough) = 0;

  virtual void CommitPersistentDecls() {}
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONHELPER_H
