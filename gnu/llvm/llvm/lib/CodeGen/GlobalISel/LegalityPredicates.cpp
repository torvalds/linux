//===- lib/CodeGen/GlobalISel/LegalizerPredicates.cpp - Predicates --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A library of predicate factories to use for LegalityPredicate.
//
//===----------------------------------------------------------------------===//

// Enable optimizations to work around MSVC debug mode bug in 32-bit:
// https://developercommunity.visualstudio.com/content/problem/1179643/msvc-copies-overaligned-non-trivially-copyable-par.html
// FIXME: Remove this when the issue is closed.
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_IX86)
// We have to disable runtime checks in order to enable optimizations. This is
// done for the entire file because the problem is actually observed in STL
// template functions.
#pragma runtime_checks("", off)
#pragma optimize("gs", on)
#endif

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

using namespace llvm;

LegalityPredicate LegalityPredicates::typeIs(unsigned TypeIdx, LLT Type) {
  return
      [=](const LegalityQuery &Query) { return Query.Types[TypeIdx] == Type; };
}

LegalityPredicate
LegalityPredicates::typeInSet(unsigned TypeIdx,
                              std::initializer_list<LLT> TypesInit) {
  SmallVector<LLT, 4> Types = TypesInit;
  return [=](const LegalityQuery &Query) {
    return llvm::is_contained(Types, Query.Types[TypeIdx]);
  };
}

LegalityPredicate LegalityPredicates::typePairInSet(
    unsigned TypeIdx0, unsigned TypeIdx1,
    std::initializer_list<std::pair<LLT, LLT>> TypesInit) {
  SmallVector<std::pair<LLT, LLT>, 4> Types = TypesInit;
  return [=](const LegalityQuery &Query) {
    std::pair<LLT, LLT> Match = {Query.Types[TypeIdx0], Query.Types[TypeIdx1]};
    return llvm::is_contained(Types, Match);
  };
}

LegalityPredicate LegalityPredicates::typePairAndMemDescInSet(
    unsigned TypeIdx0, unsigned TypeIdx1, unsigned MMOIdx,
    std::initializer_list<TypePairAndMemDesc> TypesAndMemDescInit) {
  SmallVector<TypePairAndMemDesc, 4> TypesAndMemDesc = TypesAndMemDescInit;
  return [=](const LegalityQuery &Query) {
    TypePairAndMemDesc Match = {Query.Types[TypeIdx0], Query.Types[TypeIdx1],
                                Query.MMODescrs[MMOIdx].MemoryTy,
                                Query.MMODescrs[MMOIdx].AlignInBits};
    return llvm::any_of(TypesAndMemDesc,
                        [=](const TypePairAndMemDesc &Entry) -> bool {
                          return Match.isCompatible(Entry);
                        });
  };
}

LegalityPredicate LegalityPredicates::isScalar(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx].isScalar();
  };
}

LegalityPredicate LegalityPredicates::isVector(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx].isVector();
  };
}

LegalityPredicate LegalityPredicates::isPointer(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx].isPointer();
  };
}

LegalityPredicate LegalityPredicates::isPointer(unsigned TypeIdx,
                                                unsigned AddrSpace) {
  return [=](const LegalityQuery &Query) {
    LLT Ty = Query.Types[TypeIdx];
    return Ty.isPointer() && Ty.getAddressSpace() == AddrSpace;
  };
}

LegalityPredicate LegalityPredicates::elementTypeIs(unsigned TypeIdx,
                                                    LLT EltTy) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isVector() && QueryTy.getElementType() == EltTy;
  };
}

LegalityPredicate LegalityPredicates::scalarNarrowerThan(unsigned TypeIdx,
                                                         unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && QueryTy.getSizeInBits() < Size;
  };
}

LegalityPredicate LegalityPredicates::scalarWiderThan(unsigned TypeIdx,
                                                      unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && QueryTy.getSizeInBits() > Size;
  };
}

LegalityPredicate LegalityPredicates::smallerThan(unsigned TypeIdx0,
                                                  unsigned TypeIdx1) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx0].getSizeInBits() <
           Query.Types[TypeIdx1].getSizeInBits();
  };
}

LegalityPredicate LegalityPredicates::largerThan(unsigned TypeIdx0,
                                                  unsigned TypeIdx1) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx0].getSizeInBits() >
           Query.Types[TypeIdx1].getSizeInBits();
  };
}

LegalityPredicate LegalityPredicates::scalarOrEltNarrowerThan(unsigned TypeIdx,
                                                              unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.getScalarSizeInBits() < Size;
  };
}

LegalityPredicate LegalityPredicates::scalarOrEltWiderThan(unsigned TypeIdx,
                                                           unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.getScalarSizeInBits() > Size;
  };
}

LegalityPredicate LegalityPredicates::scalarOrEltSizeNotPow2(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return !isPowerOf2_32(QueryTy.getScalarSizeInBits());
  };
}

LegalityPredicate LegalityPredicates::sizeNotMultipleOf(unsigned TypeIdx,
                                                        unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() && QueryTy.getSizeInBits() % Size != 0;
  };
}

LegalityPredicate LegalityPredicates::sizeNotPow2(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isScalar() &&
           !llvm::has_single_bit<uint32_t>(QueryTy.getSizeInBits());
  };
}

LegalityPredicate LegalityPredicates::sizeIs(unsigned TypeIdx, unsigned Size) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx].getSizeInBits() == Size;
  };
}

LegalityPredicate LegalityPredicates::sameSize(unsigned TypeIdx0,
                                               unsigned TypeIdx1) {
  return [=](const LegalityQuery &Query) {
    return Query.Types[TypeIdx0].getSizeInBits() ==
           Query.Types[TypeIdx1].getSizeInBits();
  };
}

LegalityPredicate LegalityPredicates::memSizeInBytesNotPow2(unsigned MMOIdx) {
  return [=](const LegalityQuery &Query) {
    return !llvm::has_single_bit<uint32_t>(
        Query.MMODescrs[MMOIdx].MemoryTy.getSizeInBytes());
  };
}

LegalityPredicate LegalityPredicates::memSizeNotByteSizePow2(unsigned MMOIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT MemTy = Query.MMODescrs[MMOIdx].MemoryTy;
    return !MemTy.isByteSized() ||
           !llvm::has_single_bit<uint32_t>(MemTy.getSizeInBytes());
  };
}

LegalityPredicate LegalityPredicates::numElementsNotPow2(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT QueryTy = Query.Types[TypeIdx];
    return QueryTy.isVector() && !isPowerOf2_32(QueryTy.getNumElements());
  };
}

LegalityPredicate LegalityPredicates::atomicOrderingAtLeastOrStrongerThan(
    unsigned MMOIdx, AtomicOrdering Ordering) {
  return [=](const LegalityQuery &Query) {
    return isAtLeastOrStrongerThan(Query.MMODescrs[MMOIdx].Ordering, Ordering);
  };
}
