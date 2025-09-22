//===-- ClangUtil.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A collection of helper methods and data structures for manipulating clang
// types and decls.
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

using namespace clang;
using namespace lldb_private;

bool ClangUtil::IsClangType(const CompilerType &ct) {
  // Invalid types are never Clang types.
  if (!ct)
    return false;

  if (!ct.GetTypeSystem().dyn_cast_or_null<TypeSystemClang>())
    return false;

  if (!ct.GetOpaqueQualType())
    return false;

  return true;
}

clang::Decl *ClangUtil::GetDecl(const CompilerDecl &decl) {
  assert(llvm::isa<TypeSystemClang>(decl.GetTypeSystem()));
  return static_cast<clang::Decl *>(decl.GetOpaqueDecl());
}

QualType ClangUtil::GetQualType(const CompilerType &ct) {
  // Make sure we have a clang type before making a clang::QualType
  if (!IsClangType(ct))
    return QualType();

  return QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
}

QualType ClangUtil::GetCanonicalQualType(const CompilerType &ct) {
  if (!IsClangType(ct))
    return QualType();

  return GetQualType(ct).getCanonicalType();
}

CompilerType ClangUtil::RemoveFastQualifiers(const CompilerType &ct) {
  if (!IsClangType(ct))
    return ct;

  QualType qual_type(GetQualType(ct));
  qual_type.removeLocalFastQualifiers();
  return CompilerType(ct.GetTypeSystem(), qual_type.getAsOpaquePtr());
}

clang::TagDecl *ClangUtil::GetAsTagDecl(const CompilerType &type) {
  clang::QualType qual_type = ClangUtil::GetCanonicalQualType(type);
  if (qual_type.isNull())
    return nullptr;

  return qual_type->getAsTagDecl();
}

std::string ClangUtil::DumpDecl(const clang::Decl *d) {
  if (!d)
    return "nullptr";

  std::string result;
  llvm::raw_string_ostream stream(result);
  bool deserialize = false;
  d->dump(stream, deserialize);

  stream.flush();
  return result;
}

std::string ClangUtil::ToString(const clang::Type *t) {
  return clang::QualType(t, 0).getAsString();
}

std::string ClangUtil::ToString(const CompilerType &c) {
  return ClangUtil::GetQualType(c).getAsString();
}
