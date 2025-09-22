//===- StmtIterator.cpp - Iterators for Statements ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines internal methods for StmtIterator.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>

using namespace clang;

// FIXME: Add support for dependent-sized array types in C++?
// Does it even make sense to build a CFG for an uninstantiated template?
static inline const VariableArrayType *FindVA(const Type* t) {
  while (const ArrayType *vt = dyn_cast<ArrayType>(t)) {
    if (const VariableArrayType *vat = dyn_cast<VariableArrayType>(vt))
      if (vat->getSizeExpr())
        return vat;

    t = vt->getElementType().getTypePtr();
  }

  return nullptr;
}

void StmtIteratorBase::NextVA() {
  assert(getVAPtr());

  const VariableArrayType *p = getVAPtr();
  p = FindVA(p->getElementType().getTypePtr());
  setVAPtr(p);

  if (p)
    return;

  if (inDeclGroup()) {
    if (VarDecl* VD = dyn_cast<VarDecl>(*DGI))
      if (VD->hasInit())
        return;

    NextDecl();
  }
  else {
    assert(inSizeOfTypeVA());
    RawVAPtr = 0;
  }
}

void StmtIteratorBase::NextDecl(bool ImmediateAdvance) {
  assert(getVAPtr() == nullptr);
  assert(inDeclGroup());

  if (ImmediateAdvance)
    ++DGI;

  for ( ; DGI != DGE; ++DGI)
    if (HandleDecl(*DGI))
      return;

  RawVAPtr = 0;
}

bool StmtIteratorBase::HandleDecl(Decl* D) {
  if (VarDecl* VD = dyn_cast<VarDecl>(D)) {
    if (const VariableArrayType* VAPtr = FindVA(VD->getType().getTypePtr())) {
      setVAPtr(VAPtr);
      return true;
    }

    if (VD->getInit())
      return true;
  }
  else if (TypedefNameDecl* TD = dyn_cast<TypedefNameDecl>(D)) {
    if (const VariableArrayType* VAPtr =
        FindVA(TD->getUnderlyingType().getTypePtr())) {
      setVAPtr(VAPtr);
      return true;
    }
  }
  else if (EnumConstantDecl* ECD = dyn_cast<EnumConstantDecl>(D)) {
    if (ECD->getInitExpr())
      return true;
  }

  return false;
}

StmtIteratorBase::StmtIteratorBase(Decl** dgi, Decl** dge)
    : DGI(dgi), RawVAPtr(DeclGroupMode), DGE(dge) {
  NextDecl(false);
}

StmtIteratorBase::StmtIteratorBase(const VariableArrayType* t)
    : DGI(nullptr), RawVAPtr(SizeOfTypeVAMode) {
  RawVAPtr |= reinterpret_cast<uintptr_t>(t);
}

Stmt*& StmtIteratorBase::GetDeclExpr() const {
  if (const VariableArrayType* VAPtr = getVAPtr()) {
    assert(VAPtr->SizeExpr);
    return const_cast<Stmt*&>(VAPtr->SizeExpr);
  }

  assert(inDeclGroup());
  VarDecl* VD = cast<VarDecl>(*DGI);
  return *VD->getInitAddress();
}
