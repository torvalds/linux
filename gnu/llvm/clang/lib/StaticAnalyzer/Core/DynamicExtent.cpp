//===- DynamicExtent.cpp - Dynamic extent related APIs ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines APIs that track and query dynamic extent information.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

REGISTER_MAP_WITH_PROGRAMSTATE(DynamicExtentMap, const clang::ento::MemRegion *,
                               clang::ento::DefinedOrUnknownSVal)

namespace clang {
namespace ento {

DefinedOrUnknownSVal getDynamicExtent(ProgramStateRef State,
                                      const MemRegion *MR, SValBuilder &SVB) {
  MR = MR->StripCasts();

  if (const DefinedOrUnknownSVal *Size = State->get<DynamicExtentMap>(MR))
    if (auto SSize =
            SVB.convertToArrayIndex(*Size).getAs<DefinedOrUnknownSVal>())
      return *SSize;

  return MR->getMemRegionManager().getStaticSize(MR, SVB);
}

DefinedOrUnknownSVal getElementExtent(QualType Ty, SValBuilder &SVB) {
  return SVB.makeIntVal(SVB.getContext().getTypeSizeInChars(Ty).getQuantity(),
                        SVB.getArrayIndexType());
}

static DefinedOrUnknownSVal getConstantArrayElementCount(SValBuilder &SVB,
                                                         const MemRegion *MR) {
  MR = MR->StripCasts();

  const auto *TVR = MR->getAs<TypedValueRegion>();
  if (!TVR)
    return UnknownVal();

  if (const ConstantArrayType *CAT =
          SVB.getContext().getAsConstantArrayType(TVR->getValueType()))
    return SVB.makeIntVal(CAT->getSize(), /* isUnsigned = */ false);

  return UnknownVal();
}

static DefinedOrUnknownSVal
getDynamicElementCount(ProgramStateRef State, SVal Size,
                       DefinedOrUnknownSVal ElementSize) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();

  auto ElementCount =
      SVB.evalBinOp(State, BO_Div, Size, ElementSize, SVB.getArrayIndexType())
          .getAs<DefinedOrUnknownSVal>();
  return ElementCount.value_or(UnknownVal());
}

DefinedOrUnknownSVal getDynamicElementCount(ProgramStateRef State,
                                            const MemRegion *MR,
                                            SValBuilder &SVB,
                                            QualType ElementTy) {
  assert(MR != nullptr && "Not-null region expected");
  MR = MR->StripCasts();

  DefinedOrUnknownSVal ElementSize = getElementExtent(ElementTy, SVB);
  if (ElementSize.isZeroConstant())
    return getConstantArrayElementCount(SVB, MR);

  return getDynamicElementCount(State, getDynamicExtent(State, MR, SVB),
                                ElementSize);
}

SVal getDynamicExtentWithOffset(ProgramStateRef State, SVal BufV) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  const MemRegion *MRegion = BufV.getAsRegion();
  if (!MRegion)
    return UnknownVal();
  RegionOffset Offset = MRegion->getAsOffset();
  if (Offset.hasSymbolicOffset())
    return UnknownVal();
  const MemRegion *BaseRegion = MRegion->getBaseRegion();
  if (!BaseRegion)
    return UnknownVal();

  NonLoc OffsetInChars =
      SVB.makeArrayIndex(Offset.getOffset() / SVB.getContext().getCharWidth());
  DefinedOrUnknownSVal ExtentInBytes = getDynamicExtent(State, BaseRegion, SVB);

  return SVB.evalBinOp(State, BinaryOperator::Opcode::BO_Sub, ExtentInBytes,
                       OffsetInChars, SVB.getArrayIndexType());
}

DefinedOrUnknownSVal getDynamicElementCountWithOffset(ProgramStateRef State,
                                                      SVal BufV,
                                                      QualType ElementTy) {
  const MemRegion *MR = BufV.getAsRegion();
  if (!MR)
    return UnknownVal();

  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  DefinedOrUnknownSVal ElementSize = getElementExtent(ElementTy, SVB);
  if (ElementSize.isZeroConstant())
    return getConstantArrayElementCount(SVB, MR);

  return getDynamicElementCount(State, getDynamicExtentWithOffset(State, BufV),
                                ElementSize);
}

ProgramStateRef setDynamicExtent(ProgramStateRef State, const MemRegion *MR,
                                 DefinedOrUnknownSVal Size, SValBuilder &SVB) {
  MR = MR->StripCasts();

  if (Size.isUnknown())
    return State;

  return State->set<DynamicExtentMap>(MR->StripCasts(), Size);
}

} // namespace ento
} // namespace clang
