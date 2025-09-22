//==- BasicValueFactory.h - Basic values for Path Sens analysis --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines BasicValueFactory, a class that manages the lifetime
//  of APSInt objects and symbolic constraints used by ExprEngine
//  and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BASICVALUEFACTORY_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BASICVALUEFACTORY_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstdint>
#include <utility>

namespace clang {

class CXXBaseSpecifier;

namespace ento {

class CompoundValData : public llvm::FoldingSetNode {
  QualType T;
  llvm::ImmutableList<SVal> L;

public:
  CompoundValData(QualType t, llvm::ImmutableList<SVal> l) : T(t), L(l) {
    assert(NonLoc::isCompoundType(t));
  }

  using iterator = llvm::ImmutableList<SVal>::iterator;

  iterator begin() const { return L.begin(); }
  iterator end() const { return L.end(); }

  QualType getType() const { return T; }

  static void Profile(llvm::FoldingSetNodeID& ID, QualType T,
                      llvm::ImmutableList<SVal> L);

  void Profile(llvm::FoldingSetNodeID& ID) { Profile(ID, T, L); }
};

class LazyCompoundValData : public llvm::FoldingSetNode {
  StoreRef store;
  const TypedValueRegion *region;

public:
  LazyCompoundValData(const StoreRef &st, const TypedValueRegion *r)
      : store(st), region(r) {
    assert(r);
    assert(NonLoc::isCompoundType(r->getValueType()));
  }

  /// It might return null.
  const void *getStore() const { return store.getStore(); }

  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const TypedValueRegion *getRegion() const { return region; }

  static void Profile(llvm::FoldingSetNodeID& ID,
                      const StoreRef &store,
                      const TypedValueRegion *region);

  void Profile(llvm::FoldingSetNodeID& ID) { Profile(ID, store, region); }
};

class PointerToMemberData : public llvm::FoldingSetNode {
  const NamedDecl *D;
  llvm::ImmutableList<const CXXBaseSpecifier *> L;

public:
  PointerToMemberData(const NamedDecl *D,
                      llvm::ImmutableList<const CXXBaseSpecifier *> L)
      : D(D), L(L) {}

  using iterator = llvm::ImmutableList<const CXXBaseSpecifier *>::iterator;

  iterator begin() const { return L.begin(); }
  iterator end() const { return L.end(); }

  static void Profile(llvm::FoldingSetNodeID &ID, const NamedDecl *D,
                      llvm::ImmutableList<const CXXBaseSpecifier *> L);

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, D, L); }

  /// It might return null.
  const NamedDecl *getDeclaratorDecl() const { return D; }

  llvm::ImmutableList<const CXXBaseSpecifier *> getCXXBaseList() const {
    return L;
  }
};

class BasicValueFactory {
  using APSIntSetTy =
      llvm::FoldingSet<llvm::FoldingSetNodeWrapper<llvm::APSInt>>;

  ASTContext &Ctx;
  llvm::BumpPtrAllocator& BPAlloc;

  APSIntSetTy APSIntSet;
  void *PersistentSVals = nullptr;
  void *PersistentSValPairs = nullptr;

  llvm::ImmutableList<SVal>::Factory SValListFactory;
  llvm::ImmutableList<const CXXBaseSpecifier *>::Factory CXXBaseListFactory;
  llvm::FoldingSet<CompoundValData>  CompoundValDataSet;
  llvm::FoldingSet<LazyCompoundValData> LazyCompoundValDataSet;
  llvm::FoldingSet<PointerToMemberData> PointerToMemberDataSet;

  // This is private because external clients should use the factory
  // method that takes a QualType.
  const llvm::APSInt& getValue(uint64_t X, unsigned BitWidth, bool isUnsigned);

public:
  BasicValueFactory(ASTContext &ctx, llvm::BumpPtrAllocator &Alloc)
      : Ctx(ctx), BPAlloc(Alloc), SValListFactory(Alloc),
        CXXBaseListFactory(Alloc) {}

  ~BasicValueFactory();

  ASTContext &getContext() const { return Ctx; }

  const llvm::APSInt& getValue(const llvm::APSInt& X);
  const llvm::APSInt& getValue(const llvm::APInt& X, bool isUnsigned);
  const llvm::APSInt& getValue(uint64_t X, QualType T);

  /// Returns the type of the APSInt used to store values of the given QualType.
  APSIntType getAPSIntType(QualType T) const {
    // For the purposes of the analysis and constraints, we treat atomics
    // as their underlying types.
    if (const AtomicType *AT = T->getAs<AtomicType>()) {
      T = AT->getValueType();
    }

    if (T->isIntegralOrEnumerationType() || Loc::isLocType(T)) {
      return APSIntType(Ctx.getIntWidth(T),
                        !T->isSignedIntegerOrEnumerationType());
    } else {
      // implicitly handle case of T->isFixedPointType()
      return APSIntType(Ctx.getIntWidth(T), T->isUnsignedFixedPointType());
    }

    llvm_unreachable("Unsupported type in getAPSIntType!");
  }

  /// Convert - Create a new persistent APSInt with the same value as 'From'
  ///  but with the bitwidth and signedness of 'To'.
  const llvm::APSInt &Convert(const llvm::APSInt& To,
                              const llvm::APSInt& From) {
    APSIntType TargetType(To);
    if (TargetType == APSIntType(From))
      return From;

    return getValue(TargetType.convert(From));
  }

  const llvm::APSInt &Convert(QualType T, const llvm::APSInt &From) {
    APSIntType TargetType = getAPSIntType(T);
    return Convert(TargetType, From);
  }

  const llvm::APSInt &Convert(APSIntType TargetType, const llvm::APSInt &From) {
    if (TargetType == APSIntType(From))
      return From;

    return getValue(TargetType.convert(From));
  }

  const llvm::APSInt &getIntValue(uint64_t X, bool isUnsigned) {
    QualType T = isUnsigned ? Ctx.UnsignedIntTy : Ctx.IntTy;
    return getValue(X, T);
  }

  const llvm::APSInt &getMaxValue(const llvm::APSInt &v) {
    return getValue(APSIntType(v).getMaxValue());
  }

  const llvm::APSInt &getMinValue(const llvm::APSInt &v) {
    return getValue(APSIntType(v).getMinValue());
  }

  const llvm::APSInt &getMaxValue(QualType T) {
    return getMaxValue(getAPSIntType(T));
  }

  const llvm::APSInt &getMinValue(QualType T) {
    return getMinValue(getAPSIntType(T));
  }

  const llvm::APSInt &getMaxValue(APSIntType T) {
    return getValue(T.getMaxValue());
  }

  const llvm::APSInt &getMinValue(APSIntType T) {
    return getValue(T.getMinValue());
  }

  const llvm::APSInt &Add1(const llvm::APSInt &V) {
    llvm::APSInt X = V;
    ++X;
    return getValue(X);
  }

  const llvm::APSInt &Sub1(const llvm::APSInt &V) {
    llvm::APSInt X = V;
    --X;
    return getValue(X);
  }

  const llvm::APSInt &getZeroWithTypeSize(QualType T) {
    assert(T->isScalarType());
    return getValue(0, Ctx.getTypeSize(T), true);
  }

  const llvm::APSInt &getTruthValue(bool b, QualType T) {
    return getValue(b ? 1 : 0, Ctx.getIntWidth(T),
                    T->isUnsignedIntegerOrEnumerationType());
  }

  const llvm::APSInt &getTruthValue(bool b) {
    return getTruthValue(b, Ctx.getLogicalOperationType());
  }

  const CompoundValData *getCompoundValData(QualType T,
                                            llvm::ImmutableList<SVal> Vals);

  const LazyCompoundValData *getLazyCompoundValData(const StoreRef &store,
                                            const TypedValueRegion *region);

  const PointerToMemberData *
  getPointerToMemberData(const NamedDecl *ND,
                         llvm::ImmutableList<const CXXBaseSpecifier *> L);

  llvm::ImmutableList<SVal> getEmptySValList() {
    return SValListFactory.getEmptyList();
  }

  llvm::ImmutableList<SVal> prependSVal(SVal X, llvm::ImmutableList<SVal> L) {
    return SValListFactory.add(X, L);
  }

  llvm::ImmutableList<const CXXBaseSpecifier *> getEmptyCXXBaseList() {
    return CXXBaseListFactory.getEmptyList();
  }

  llvm::ImmutableList<const CXXBaseSpecifier *> prependCXXBase(
      const CXXBaseSpecifier *CBS,
      llvm::ImmutableList<const CXXBaseSpecifier *> L) {
    return CXXBaseListFactory.add(CBS, L);
  }

  const PointerToMemberData *
  accumCXXBase(llvm::iterator_range<CastExpr::path_const_iterator> PathRange,
               const nonloc::PointerToMember &PTM, const clang::CastKind &kind);

  const llvm::APSInt* evalAPSInt(BinaryOperator::Opcode Op,
                                     const llvm::APSInt& V1,
                                     const llvm::APSInt& V2);

  const std::pair<SVal, uintptr_t>&
  getPersistentSValWithData(const SVal& V, uintptr_t Data);

  const std::pair<SVal, SVal>&
  getPersistentSValPair(const SVal& V1, const SVal& V2);

  const SVal* getPersistentSVal(SVal X);
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BASICVALUEFACTORY_H
