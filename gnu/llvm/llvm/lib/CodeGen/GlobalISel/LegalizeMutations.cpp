//===- lib/CodeGen/GlobalISel/LegalizerMutations.cpp - Mutations ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A library of mutation factories to use for LegalityMutation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

using namespace llvm;

LegalizeMutation LegalizeMutations::changeTo(unsigned TypeIdx, LLT Ty) {
  return
      [=](const LegalityQuery &Query) { return std::make_pair(TypeIdx, Ty); };
}

LegalizeMutation LegalizeMutations::changeTo(unsigned TypeIdx,
                                             unsigned FromTypeIdx) {
  return [=](const LegalityQuery &Query) {
    return std::make_pair(TypeIdx, Query.Types[FromTypeIdx]);
  };
}

LegalizeMutation LegalizeMutations::changeElementTo(unsigned TypeIdx,
                                                    unsigned FromTypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT OldTy = Query.Types[TypeIdx];
    const LLT NewTy = Query.Types[FromTypeIdx];
    return std::make_pair(TypeIdx, OldTy.changeElementType(NewTy));
  };
}

LegalizeMutation LegalizeMutations::changeElementTo(unsigned TypeIdx,
                                                    LLT NewEltTy) {
  return [=](const LegalityQuery &Query) {
    const LLT OldTy = Query.Types[TypeIdx];
    return std::make_pair(TypeIdx, OldTy.changeElementType(NewEltTy));
  };
}

LegalizeMutation LegalizeMutations::changeElementCountTo(unsigned TypeIdx,
                                                         unsigned FromTypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT OldTy = Query.Types[TypeIdx];
    const LLT NewTy = Query.Types[FromTypeIdx];
    ElementCount NewEltCount =
        NewTy.isVector() ? NewTy.getElementCount() : ElementCount::getFixed(1);
    return std::make_pair(TypeIdx, OldTy.changeElementCount(NewEltCount));
  };
}

LegalizeMutation LegalizeMutations::changeElementCountTo(unsigned TypeIdx,
                                                         LLT NewEltTy) {
  return [=](const LegalityQuery &Query) {
    const LLT OldTy = Query.Types[TypeIdx];
    ElementCount NewEltCount = NewEltTy.isVector() ? NewEltTy.getElementCount()
                                                   : ElementCount::getFixed(1);
    return std::make_pair(TypeIdx, OldTy.changeElementCount(NewEltCount));
  };
}

LegalizeMutation LegalizeMutations::changeElementSizeTo(unsigned TypeIdx,
                                                        unsigned FromTypeIdx) {
  return [=](const LegalityQuery &Query) {
    const LLT OldTy = Query.Types[TypeIdx];
    const LLT NewTy = Query.Types[FromTypeIdx];
    const LLT NewEltTy = LLT::scalar(NewTy.getScalarSizeInBits());
    return std::make_pair(TypeIdx, OldTy.changeElementType(NewEltTy));
  };
}

LegalizeMutation LegalizeMutations::widenScalarOrEltToNextPow2(unsigned TypeIdx,
                                                               unsigned Min) {
  return [=](const LegalityQuery &Query) {
    const LLT Ty = Query.Types[TypeIdx];
    unsigned NewEltSizeInBits =
        std::max(1u << Log2_32_Ceil(Ty.getScalarSizeInBits()), Min);
    return std::make_pair(TypeIdx, Ty.changeElementSize(NewEltSizeInBits));
  };
}

LegalizeMutation
LegalizeMutations::widenScalarOrEltToNextMultipleOf(unsigned TypeIdx,
                                                    unsigned Size) {
  return [=](const LegalityQuery &Query) {
    const LLT Ty = Query.Types[TypeIdx];
    unsigned NewEltSizeInBits = alignTo(Ty.getScalarSizeInBits(), Size);
    return std::make_pair(TypeIdx, Ty.changeElementSize(NewEltSizeInBits));
  };
}

LegalizeMutation LegalizeMutations::moreElementsToNextPow2(unsigned TypeIdx,
                                                           unsigned Min) {
  return [=](const LegalityQuery &Query) {
    const LLT VecTy = Query.Types[TypeIdx];
    unsigned NewNumElements =
        std::max(1u << Log2_32_Ceil(VecTy.getNumElements()), Min);
    return std::make_pair(
        TypeIdx, LLT::fixed_vector(NewNumElements, VecTy.getElementType()));
  };
}

LegalizeMutation LegalizeMutations::scalarize(unsigned TypeIdx) {
  return [=](const LegalityQuery &Query) {
    return std::make_pair(TypeIdx, Query.Types[TypeIdx].getElementType());
  };
}
