//===--- Record.cpp - struct and class metadata for the VM ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Record.h"
#include "clang/AST/ASTContext.h"

using namespace clang;
using namespace clang::interp;

Record::Record(const RecordDecl *Decl, BaseList &&SrcBases,
               FieldList &&SrcFields, VirtualBaseList &&SrcVirtualBases,
               unsigned VirtualSize, unsigned BaseSize)
    : Decl(Decl), Bases(std::move(SrcBases)), Fields(std::move(SrcFields)),
      BaseSize(BaseSize), VirtualSize(VirtualSize), IsUnion(Decl->isUnion()) {
  for (Base &V : SrcVirtualBases)
    VirtualBases.push_back({ V.Decl, V.Offset + BaseSize, V.Desc, V.R });

  for (Base &B : Bases)
    BaseMap[B.Decl] = &B;
  for (Field &F : Fields)
    FieldMap[F.Decl] = &F;
  for (Base &V : VirtualBases)
    VirtualBaseMap[V.Decl] = &V;
}

const std::string Record::getName() const {
  std::string Ret;
  llvm::raw_string_ostream OS(Ret);
  Decl->getNameForDiagnostic(OS, Decl->getASTContext().getPrintingPolicy(),
                             /*Qualified=*/true);
  return Ret;
}

const Record::Field *Record::getField(const FieldDecl *FD) const {
  auto It = FieldMap.find(FD);
  assert(It != FieldMap.end() && "Missing field");
  return It->second;
}

const Record::Base *Record::getBase(const RecordDecl *FD) const {
  auto It = BaseMap.find(FD);
  assert(It != BaseMap.end() && "Missing base");
  return It->second;
}

const Record::Base *Record::getBase(QualType T) const {
  if (auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    return BaseMap.lookup(RD);
  }
  return nullptr;
}

const Record::Base *Record::getVirtualBase(const RecordDecl *FD) const {
  auto It = VirtualBaseMap.find(FD);
  assert(It != VirtualBaseMap.end() && "Missing virtual base");
  return It->second;
}
