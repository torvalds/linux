//===--- SValVisitor.h - Visitor for SVal subclasses ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SValVisitor, SymExprVisitor, and MemRegionVisitor
//  interfaces, and also FullSValVisitor, which visits all three hierarchies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALVISITOR_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALVISITOR_H

#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

namespace clang {

namespace ento {

/// SValVisitor - this class implements a simple visitor for SVal
/// subclasses.
template <typename ImplClass, typename RetTy = void> class SValVisitor {
  ImplClass &derived() { return *static_cast<ImplClass *>(this); }

public:
  RetTy Visit(SVal V) {
    // Dispatch to VisitFooVal for each FooVal.
    switch (V.getKind()) {
#define BASIC_SVAL(Id, Parent)                                                 \
  case SVal::Id##Kind:                                                         \
    return derived().Visit##Id(V.castAs<Id>());
#define LOC_SVAL(Id, Parent)                                                   \
  case SVal::Loc##Id##Kind:                                                    \
    return derived().Visit##Id(V.castAs<loc::Id>());
#define NONLOC_SVAL(Id, Parent)                                                \
  case SVal::NonLoc##Id##Kind:                                                 \
    return derived().Visit##Id(V.castAs<nonloc::Id>());
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
    }
    llvm_unreachable("Unknown SVal kind!");
  }

  // Dispatch to the more generic handler as a default implementation.
#define BASIC_SVAL(Id, Parent)                                                 \
  RetTy Visit##Id(Id V) { return derived().Visit##Parent(V.castAs<Id>()); }
#define ABSTRACT_SVAL(Id, Parent) BASIC_SVAL(Id, Parent)
#define LOC_SVAL(Id, Parent)                                                   \
  RetTy Visit##Id(loc::Id V) { return derived().VisitLoc(V.castAs<Loc>()); }
#define NONLOC_SVAL(Id, Parent)                                                \
  RetTy Visit##Id(nonloc::Id V) {                                              \
    return derived().VisitNonLoc(V.castAs<NonLoc>());                          \
  }
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"

  // Base case, ignore it. :)
  RetTy VisitSVal(SVal V) { return RetTy(); }
};

/// SymExprVisitor - this class implements a simple visitor for SymExpr
/// subclasses.
template <typename ImplClass, typename RetTy = void> class SymExprVisitor {
public:

#define DISPATCH(CLASS) \
    return static_cast<ImplClass *>(this)->Visit ## CLASS(cast<CLASS>(S))

  RetTy Visit(SymbolRef S) {
    // Dispatch to VisitSymbolFoo for each SymbolFoo.
    switch (S->getKind()) {
#define SYMBOL(Id, Parent) \
    case SymExpr::Id ## Kind: DISPATCH(Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"
    }
    llvm_unreachable("Unknown SymExpr kind!");
  }

  // If the implementation chooses not to implement a certain visit method, fall
  // back on visiting the superclass.
#define SYMBOL(Id, Parent) RetTy Visit ## Id(const Id *S) { DISPATCH(Parent); }
#define ABSTRACT_SYMBOL(Id, Parent) SYMBOL(Id, Parent)
#include "clang/StaticAnalyzer/Core/PathSensitive/Symbols.def"

  // Base case, ignore it. :)
  RetTy VisitSymExpr(SymbolRef S) { return RetTy(); }

#undef DISPATCH
};

/// MemRegionVisitor - this class implements a simple visitor for MemRegion
/// subclasses.
template <typename ImplClass, typename RetTy = void> class MemRegionVisitor {
public:

#define DISPATCH(CLASS) \
  return static_cast<ImplClass *>(this)->Visit ## CLASS(cast<CLASS>(R))

  RetTy Visit(const MemRegion *R) {
    // Dispatch to VisitFooRegion for each FooRegion.
    switch (R->getKind()) {
#define REGION(Id, Parent) case MemRegion::Id ## Kind: DISPATCH(Id);
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"
    }
    llvm_unreachable("Unknown MemRegion kind!");
  }

  // If the implementation chooses not to implement a certain visit method, fall
  // back on visiting the superclass.
#define REGION(Id, Parent) \
  RetTy Visit ## Id(const Id *R) { DISPATCH(Parent); }
#define ABSTRACT_REGION(Id, Parent) \
  REGION(Id, Parent)
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"

  // Base case, ignore it. :)
  RetTy VisitMemRegion(const MemRegion *R) { return RetTy(); }

#undef DISPATCH
};

/// FullSValVisitor - a convenient mixed visitor for all three:
/// SVal, SymExpr and MemRegion subclasses.
template <typename ImplClass, typename RetTy = void>
class FullSValVisitor : public SValVisitor<ImplClass, RetTy>,
                        public SymExprVisitor<ImplClass, RetTy>,
                        public MemRegionVisitor<ImplClass, RetTy> {
public:
  using SValVisitor<ImplClass, RetTy>::Visit;
  using SymExprVisitor<ImplClass, RetTy>::Visit;
  using MemRegionVisitor<ImplClass, RetTy>::Visit;
};

} // end namespace ento

} // end namespace clang

#endif
