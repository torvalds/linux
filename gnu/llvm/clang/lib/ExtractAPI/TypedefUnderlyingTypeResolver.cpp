//===- ExtractAPI/TypedefUnderlyingTypeResolver.cpp -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements UnderlyingTypeResolver.
///
//===----------------------------------------------------------------------===//

#include "clang/ExtractAPI/TypedefUnderlyingTypeResolver.h"
#include "clang/Basic/Module.h"
#include "clang/Index/USRGeneration.h"

using namespace clang;
using namespace extractapi;

const NamedDecl *
TypedefUnderlyingTypeResolver::getUnderlyingTypeDecl(QualType Type) const {
  const NamedDecl *TypeDecl = nullptr;

  const TypedefType *TypedefTy = Type->getAs<TypedefType>();
  if (TypedefTy)
    TypeDecl = TypedefTy->getDecl();
  if (const TagType *TagTy = Type->getAs<TagType>()) {
    TypeDecl = TagTy->getDecl();
  } else if (const ObjCInterfaceType *ObjCITy =
                 Type->getAs<ObjCInterfaceType>()) {
    TypeDecl = ObjCITy->getDecl();
  }

  if (TypeDecl && TypedefTy) {
    // if this is a typedef to another typedef, use the typedef's decl for the
    // USR - this will actually be in the output, unlike a typedef to an
    // anonymous decl
    const TypedefNameDecl *TypedefDecl = TypedefTy->getDecl();
    if (TypedefDecl->getUnderlyingType()->isTypedefNameType())
      TypeDecl = TypedefDecl;
  }

  return TypeDecl;
}

SymbolReference
TypedefUnderlyingTypeResolver::getSymbolReferenceForType(QualType Type,
                                                         APISet &API) const {
  std::string TypeName = Type.getAsString();
  SmallString<128> TypeUSR;
  const NamedDecl *TypeDecl = getUnderlyingTypeDecl(Type);
  const TypedefType *TypedefTy = Type->getAs<TypedefType>();
  StringRef OwningModuleName;

  if (TypeDecl) {
    if (!TypedefTy)
      TypeName = TypeDecl->getName().str();

    clang::index::generateUSRForDecl(TypeDecl, TypeUSR);
    if (auto *OwningModule = TypeDecl->getImportedOwningModule())
      OwningModuleName = OwningModule->Name;
  } else {
    clang::index::generateUSRForType(Type, Context, TypeUSR);
  }

  return API.createSymbolReference(TypeName, TypeUSR, OwningModuleName);
}

std::string TypedefUnderlyingTypeResolver::getUSRForType(QualType Type) const {
  SmallString<128> TypeUSR;
  const NamedDecl *TypeDecl = getUnderlyingTypeDecl(Type);

  if (TypeDecl)
    clang::index::generateUSRForDecl(TypeDecl, TypeUSR);
  else
    clang::index::generateUSRForType(Type, Context, TypeUSR);

  return std::string(TypeUSR);
}
