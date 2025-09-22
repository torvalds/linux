//===- BasicValueFactory.cpp - Basic values for Path Sens analysis --------===//
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

#include "clang/StaticAnalyzer/Core/PathSensitive/BasicValueFactory.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace clang;
using namespace ento;

void CompoundValData::Profile(llvm::FoldingSetNodeID& ID, QualType T,
                              llvm::ImmutableList<SVal> L) {
  T.Profile(ID);
  ID.AddPointer(L.getInternalPointer());
}

void LazyCompoundValData::Profile(llvm::FoldingSetNodeID& ID,
                                  const StoreRef &store,
                                  const TypedValueRegion *region) {
  ID.AddPointer(store.getStore());
  ID.AddPointer(region);
}

void PointerToMemberData::Profile(
    llvm::FoldingSetNodeID &ID, const NamedDecl *D,
    llvm::ImmutableList<const CXXBaseSpecifier *> L) {
  ID.AddPointer(D);
  ID.AddPointer(L.getInternalPointer());
}

using SValData = std::pair<SVal, uintptr_t>;
using SValPair = std::pair<SVal, SVal>;

namespace llvm {

template<> struct FoldingSetTrait<SValData> {
  static inline void Profile(const SValData& X, llvm::FoldingSetNodeID& ID) {
    X.first.Profile(ID);
    ID.AddPointer( (void*) X.second);
  }
};

template<> struct FoldingSetTrait<SValPair> {
  static inline void Profile(const SValPair& X, llvm::FoldingSetNodeID& ID) {
    X.first.Profile(ID);
    X.second.Profile(ID);
  }
};

} // namespace llvm

using PersistentSValsTy =
    llvm::FoldingSet<llvm::FoldingSetNodeWrapper<SValData>>;

using PersistentSValPairsTy =
    llvm::FoldingSet<llvm::FoldingSetNodeWrapper<SValPair>>;

BasicValueFactory::~BasicValueFactory() {
  // Note that the dstor for the contents of APSIntSet will never be called,
  // so we iterate over the set and invoke the dstor for each APSInt.  This
  // frees an aux. memory allocated to represent very large constants.
  for (const auto &I : APSIntSet)
    I.getValue().~APSInt();

  delete (PersistentSValsTy*) PersistentSVals;
  delete (PersistentSValPairsTy*) PersistentSValPairs;
}

const llvm::APSInt& BasicValueFactory::getValue(const llvm::APSInt& X) {
  llvm::FoldingSetNodeID ID;
  void *InsertPos;

  using FoldNodeTy = llvm::FoldingSetNodeWrapper<llvm::APSInt>;

  X.Profile(ID);
  FoldNodeTy* P = APSIntSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!P) {
    P = new (BPAlloc) FoldNodeTy(X);
    APSIntSet.InsertNode(P, InsertPos);
  }

  return *P;
}

const llvm::APSInt& BasicValueFactory::getValue(const llvm::APInt& X,
                                                bool isUnsigned) {
  llvm::APSInt V(X, isUnsigned);
  return getValue(V);
}

const llvm::APSInt& BasicValueFactory::getValue(uint64_t X, unsigned BitWidth,
                                           bool isUnsigned) {
  llvm::APSInt V(BitWidth, isUnsigned);
  V = X;
  return getValue(V);
}

const llvm::APSInt& BasicValueFactory::getValue(uint64_t X, QualType T) {
  return getValue(getAPSIntType(T).getValue(X));
}

const CompoundValData*
BasicValueFactory::getCompoundValData(QualType T,
                                      llvm::ImmutableList<SVal> Vals) {
  llvm::FoldingSetNodeID ID;
  CompoundValData::Profile(ID, T, Vals);
  void *InsertPos;

  CompoundValData* D = CompoundValDataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!D) {
    D = new (BPAlloc) CompoundValData(T, Vals);
    CompoundValDataSet.InsertNode(D, InsertPos);
  }

  return D;
}

const LazyCompoundValData*
BasicValueFactory::getLazyCompoundValData(const StoreRef &store,
                                          const TypedValueRegion *region) {
  llvm::FoldingSetNodeID ID;
  LazyCompoundValData::Profile(ID, store, region);
  void *InsertPos;

  LazyCompoundValData *D =
    LazyCompoundValDataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!D) {
    D = new (BPAlloc) LazyCompoundValData(store, region);
    LazyCompoundValDataSet.InsertNode(D, InsertPos);
  }

  return D;
}

const PointerToMemberData *BasicValueFactory::getPointerToMemberData(
    const NamedDecl *ND, llvm::ImmutableList<const CXXBaseSpecifier *> L) {
  llvm::FoldingSetNodeID ID;
  PointerToMemberData::Profile(ID, ND, L);
  void *InsertPos;

  PointerToMemberData *D =
      PointerToMemberDataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!D) {
    D = new (BPAlloc) PointerToMemberData(ND, L);
    PointerToMemberDataSet.InsertNode(D, InsertPos);
  }

  return D;
}

LLVM_ATTRIBUTE_UNUSED bool hasNoRepeatedElements(
    llvm::ImmutableList<const CXXBaseSpecifier *> BaseSpecList) {
  llvm::SmallPtrSet<QualType, 16> BaseSpecSeen;
  for (const CXXBaseSpecifier *BaseSpec : BaseSpecList) {
    QualType BaseType = BaseSpec->getType();
    // Check whether inserted
    if (!BaseSpecSeen.insert(BaseType).second)
      return false;
  }
  return true;
}

const PointerToMemberData *BasicValueFactory::accumCXXBase(
    llvm::iterator_range<CastExpr::path_const_iterator> PathRange,
    const nonloc::PointerToMember &PTM, const CastKind &kind) {
  assert((kind == CK_DerivedToBaseMemberPointer ||
          kind == CK_BaseToDerivedMemberPointer ||
          kind == CK_ReinterpretMemberPointer) &&
         "accumCXXBase called with wrong CastKind");
  nonloc::PointerToMember::PTMDataType PTMDT = PTM.getPTMData();
  const NamedDecl *ND = nullptr;
  llvm::ImmutableList<const CXXBaseSpecifier *> BaseSpecList;

  if (PTMDT.isNull() || PTMDT.is<const NamedDecl *>()) {
    if (PTMDT.is<const NamedDecl *>())
      ND = PTMDT.get<const NamedDecl *>();

    BaseSpecList = CXXBaseListFactory.getEmptyList();
  } else {
    const PointerToMemberData *PTMD = PTMDT.get<const PointerToMemberData *>();
    ND = PTMD->getDeclaratorDecl();

    BaseSpecList = PTMD->getCXXBaseList();
  }

  assert(hasNoRepeatedElements(BaseSpecList) &&
         "CXXBaseSpecifier list of PointerToMemberData must not have repeated "
         "elements");

  if (kind == CK_DerivedToBaseMemberPointer) {
    // Here we pop off matching CXXBaseSpecifiers from BaseSpecList.
    // Because, CK_DerivedToBaseMemberPointer comes from a static_cast and
    // serves to remove a matching implicit cast. Note that static_cast's that
    // are no-ops do not count since they produce an empty PathRange, a nice
    // thing about Clang AST.

    // Now we know that there are no repetitions in BaseSpecList.
    // So, popping the first element from it corresponding to each element in
    // PathRange is equivalent to only including elements that are in
    // BaseSpecList but not it PathRange
    auto ReducedBaseSpecList = CXXBaseListFactory.getEmptyList();
    for (const CXXBaseSpecifier *BaseSpec : BaseSpecList) {
      auto IsSameAsBaseSpec = [&BaseSpec](const CXXBaseSpecifier *I) -> bool {
        return BaseSpec->getType() == I->getType();
      };
      if (llvm::none_of(PathRange, IsSameAsBaseSpec))
        ReducedBaseSpecList =
            CXXBaseListFactory.add(BaseSpec, ReducedBaseSpecList);
    }

    return getPointerToMemberData(ND, ReducedBaseSpecList);
  }
  // FIXME: Reinterpret casts on member-pointers are not handled properly by
  // this code
  for (const CXXBaseSpecifier *I : llvm::reverse(PathRange))
    BaseSpecList = prependCXXBase(I, BaseSpecList);
  return getPointerToMemberData(ND, BaseSpecList);
}

const llvm::APSInt*
BasicValueFactory::evalAPSInt(BinaryOperator::Opcode Op,
                             const llvm::APSInt& V1, const llvm::APSInt& V2) {
  switch (Op) {
    default:
      llvm_unreachable("Invalid Opcode.");

    case BO_Mul:
      return &getValue( V1 * V2 );

    case BO_Div:
      if (V2 == 0) // Avoid division by zero
        return nullptr;
      return &getValue( V1 / V2 );

    case BO_Rem:
      if (V2 == 0) // Avoid division by zero
        return nullptr;
      return &getValue( V1 % V2 );

    case BO_Add:
      return &getValue( V1 + V2 );

    case BO_Sub:
      return &getValue( V1 - V2 );

    case BO_Shl: {
      // FIXME: This logic should probably go higher up, where we can
      // test these conditions symbolically.

      if (V2.isNegative() || V2.getBitWidth() > 64)
        return nullptr;

      uint64_t Amt = V2.getZExtValue();

      if (Amt >= V1.getBitWidth())
        return nullptr;

      return &getValue( V1.operator<<( (unsigned) Amt ));
    }

    case BO_Shr: {
      // FIXME: This logic should probably go higher up, where we can
      // test these conditions symbolically.

      if (V2.isNegative() || V2.getBitWidth() > 64)
        return nullptr;

      uint64_t Amt = V2.getZExtValue();

      if (Amt >= V1.getBitWidth())
        return nullptr;

      return &getValue( V1.operator>>( (unsigned) Amt ));
    }

    case BO_LT:
      return &getTruthValue( V1 < V2 );

    case BO_GT:
      return &getTruthValue( V1 > V2 );

    case BO_LE:
      return &getTruthValue( V1 <= V2 );

    case BO_GE:
      return &getTruthValue( V1 >= V2 );

    case BO_EQ:
      return &getTruthValue( V1 == V2 );

    case BO_NE:
      return &getTruthValue( V1 != V2 );

      // Note: LAnd, LOr, Comma are handled specially by higher-level logic.

    case BO_And:
      return &getValue( V1 & V2 );

    case BO_Or:
      return &getValue( V1 | V2 );

    case BO_Xor:
      return &getValue( V1 ^ V2 );
  }
}

const std::pair<SVal, uintptr_t>&
BasicValueFactory::getPersistentSValWithData(const SVal& V, uintptr_t Data) {
  // Lazily create the folding set.
  if (!PersistentSVals) PersistentSVals = new PersistentSValsTy();

  llvm::FoldingSetNodeID ID;
  void *InsertPos;
  V.Profile(ID);
  ID.AddPointer((void*) Data);

  PersistentSValsTy& Map = *((PersistentSValsTy*) PersistentSVals);

  using FoldNodeTy = llvm::FoldingSetNodeWrapper<SValData>;

  FoldNodeTy* P = Map.FindNodeOrInsertPos(ID, InsertPos);

  if (!P) {
    P = new (BPAlloc) FoldNodeTy(std::make_pair(V, Data));
    Map.InsertNode(P, InsertPos);
  }

  return P->getValue();
}

const std::pair<SVal, SVal>&
BasicValueFactory::getPersistentSValPair(const SVal& V1, const SVal& V2) {
  // Lazily create the folding set.
  if (!PersistentSValPairs) PersistentSValPairs = new PersistentSValPairsTy();

  llvm::FoldingSetNodeID ID;
  void *InsertPos;
  V1.Profile(ID);
  V2.Profile(ID);

  PersistentSValPairsTy& Map = *((PersistentSValPairsTy*) PersistentSValPairs);

  using FoldNodeTy = llvm::FoldingSetNodeWrapper<SValPair>;

  FoldNodeTy* P = Map.FindNodeOrInsertPos(ID, InsertPos);

  if (!P) {
    P = new (BPAlloc) FoldNodeTy(std::make_pair(V1, V2));
    Map.InsertNode(P, InsertPos);
  }

  return P->getValue();
}

const SVal* BasicValueFactory::getPersistentSVal(SVal X) {
  return &getPersistentSValWithData(X, 0).first;
}
