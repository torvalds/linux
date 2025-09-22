//==- DependentDiagnostic.h - Dependently-generated diagnostics --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines interfaces for diagnostics which may or may
//  fire based on how a template is instantiated.
//
//  At the moment, the only consumer of this interface is access
//  control.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DEPENDENTDIAGNOSTIC_H
#define LLVM_CLANG_AST_DEPENDENTDIAGNOSTIC_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/Type.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include <cassert>
#include <iterator>

namespace clang {

class ASTContext;
class CXXRecordDecl;
class NamedDecl;

/// A dependently-generated diagnostic.
class DependentDiagnostic {
public:
  enum AccessNonce { Access = 0 };

  static DependentDiagnostic *Create(ASTContext &Context,
                                     DeclContext *Parent,
                                     AccessNonce _,
                                     SourceLocation Loc,
                                     bool IsMemberAccess,
                                     AccessSpecifier AS,
                                     NamedDecl *TargetDecl,
                                     CXXRecordDecl *NamingClass,
                                     QualType BaseObjectType,
                                     const PartialDiagnostic &PDiag) {
    DependentDiagnostic *DD = Create(Context, Parent, PDiag);
    DD->AccessData.Loc = Loc;
    DD->AccessData.IsMember = IsMemberAccess;
    DD->AccessData.Access = AS;
    DD->AccessData.TargetDecl = TargetDecl;
    DD->AccessData.NamingClass = NamingClass;
    DD->AccessData.BaseObjectType = BaseObjectType.getAsOpaquePtr();
    return DD;
  }

  unsigned getKind() const {
    return Access;
  }

  bool isAccessToMember() const {
    assert(getKind() == Access);
    return AccessData.IsMember;
  }

  AccessSpecifier getAccess() const {
    assert(getKind() == Access);
    return AccessSpecifier(AccessData.Access);
  }

  SourceLocation getAccessLoc() const {
    assert(getKind() == Access);
    return AccessData.Loc;
  }

  NamedDecl *getAccessTarget() const {
    assert(getKind() == Access);
    return AccessData.TargetDecl;
  }

  NamedDecl *getAccessNamingClass() const {
    assert(getKind() == Access);
    return AccessData.NamingClass;
  }

  QualType getAccessBaseObjectType() const {
    assert(getKind() == Access);
    return QualType::getFromOpaquePtr(AccessData.BaseObjectType);
  }

  const PartialDiagnostic &getDiagnostic() const {
    return Diag;
  }

private:
  friend class DeclContext::ddiag_iterator;
  friend class DependentStoredDeclsMap;

  DependentDiagnostic(const PartialDiagnostic &PDiag,
                      DiagnosticStorage *Storage)
      : Diag(PDiag, Storage) {}

  static DependentDiagnostic *Create(ASTContext &Context,
                                     DeclContext *Parent,
                                     const PartialDiagnostic &PDiag);

  DependentDiagnostic *NextDiagnostic;

  PartialDiagnostic Diag;

  struct {
    SourceLocation Loc;
    LLVM_PREFERRED_TYPE(AccessSpecifier)
    unsigned Access : 2;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsMember : 1;
    NamedDecl *TargetDecl;
    CXXRecordDecl *NamingClass;
    void *BaseObjectType;
  } AccessData;
};

/// An iterator over the dependent diagnostics in a dependent context.
class DeclContext::ddiag_iterator {
public:
  ddiag_iterator() = default;
  explicit ddiag_iterator(DependentDiagnostic *Ptr) : Ptr(Ptr) {}

  using value_type = DependentDiagnostic *;
  using reference = DependentDiagnostic *;
  using pointer = DependentDiagnostic *;
  using difference_type = int;
  using iterator_category = std::forward_iterator_tag;

  reference operator*() const { return Ptr; }

  ddiag_iterator &operator++() {
    assert(Ptr && "attempt to increment past end of diag list");
    Ptr = Ptr->NextDiagnostic;
    return *this;
  }

  ddiag_iterator operator++(int) {
    ddiag_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  bool operator==(ddiag_iterator Other) const {
    return Ptr == Other.Ptr;
  }

  bool operator!=(ddiag_iterator Other) const {
    return Ptr != Other.Ptr;
  }

  ddiag_iterator &operator+=(difference_type N) {
    assert(N >= 0 && "cannot rewind a DeclContext::ddiag_iterator");
    while (N--)
      ++*this;
    return *this;
  }

  ddiag_iterator operator+(difference_type N) const {
    ddiag_iterator tmp = *this;
    tmp += N;
    return tmp;
  }

private:
  DependentDiagnostic *Ptr = nullptr;
};

inline DeclContext::ddiag_range DeclContext::ddiags() const {
  assert(isDependentContext()
         && "cannot iterate dependent diagnostics of non-dependent context");
  const DependentStoredDeclsMap *Map
    = static_cast<DependentStoredDeclsMap*>(getPrimaryContext()->getLookupPtr());

  if (!Map)
    // Return an empty range using the always-end default constructor.
    return ddiag_range(ddiag_iterator(), ddiag_iterator());

  return ddiag_range(ddiag_iterator(Map->FirstDiagnostic), ddiag_iterator());
}

} // namespace clang

#endif // LLVM_CLANG_AST_DEPENDENTDIAGNOSTIC_H
