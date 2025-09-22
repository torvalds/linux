//===- ExtractAPI/TypedefUnderlyingTypeResolver.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the UnderlyingTypeResolver which is a helper type for
/// resolving the undelrying type for a given QualType and exposing that
/// information in various forms.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_UNDERLYING_TYPE_RESOLVER_H
#define LLVM_CLANG_UNDERLYING_TYPE_RESOLVER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/ExtractAPI/API.h"

#include <string>

namespace clang {
namespace extractapi {

struct TypedefUnderlyingTypeResolver {
  /// Gets the underlying type declaration.
  const NamedDecl *getUnderlyingTypeDecl(QualType Type) const;

  /// Get a SymbolReference for the given type.
  SymbolReference getSymbolReferenceForType(QualType Type, APISet &API) const;

  /// Get a USR for the given type.
  std::string getUSRForType(QualType Type) const;

  explicit TypedefUnderlyingTypeResolver(ASTContext &Context)
      : Context(Context) {}

private:
  ASTContext &Context;
};

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_UNDERLYING_TYPE_RESOLVER_H
