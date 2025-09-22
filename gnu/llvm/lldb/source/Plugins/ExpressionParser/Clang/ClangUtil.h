//===-- ClangUtil.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A collection of helper methods and data structures for manipulating clang
// types and decls.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGUTIL_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGUTIL_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/Type.h"

#include "lldb/Symbol/CompilerType.h"

namespace clang {
class TagDecl;
}

namespace lldb_private {
struct ClangUtil {
  static bool IsClangType(const CompilerType &ct);

  /// Returns the clang::Decl of the given CompilerDecl.
  /// CompilerDecl has to be valid and represent a clang::Decl.
  static clang::Decl *GetDecl(const CompilerDecl &decl);

  static clang::QualType GetQualType(const CompilerType &ct);

  static clang::QualType GetCanonicalQualType(const CompilerType &ct);

  static CompilerType RemoveFastQualifiers(const CompilerType &ct);

  static clang::TagDecl *GetAsTagDecl(const CompilerType &type);

  /// Returns a textual representation of the given Decl's AST. Does not
  /// deserialize any child nodes.
  static std::string DumpDecl(const clang::Decl *d);
  /// Returns a textual representation of the given type.
  static std::string ToString(const clang::Type *t);
  /// Returns a textual representation of the given CompilerType (assuming
  /// its underlying type is a Clang type).
  static std::string ToString(const CompilerType &c);
};
}

#endif
