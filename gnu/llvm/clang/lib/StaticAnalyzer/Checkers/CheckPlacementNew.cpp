//==- CheckPlacementNew.cpp - Check for placement new operation --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a check for misuse of the default placement new operator.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace ento;

namespace {
class PlacementNewChecker : public Checker<check::PreStmt<CXXNewExpr>> {
public:
  void checkPreStmt(const CXXNewExpr *NE, CheckerContext &C) const;

private:
  bool checkPlaceCapacityIsSufficient(const CXXNewExpr *NE,
                                      CheckerContext &C) const;

  bool checkPlaceIsAlignedProperly(const CXXNewExpr *NE,
                                   CheckerContext &C) const;

  // Returns the size of the target in a placement new expression.
  // E.g. in "new (&s) long" it returns the size of `long`.
  SVal getExtentSizeOfNewTarget(const CXXNewExpr *NE, CheckerContext &C,
                                bool &IsArray) const;
  // Returns the size of the place in a placement new expression.
  // E.g. in "new (&s) long" it returns the size of `s`.
  SVal getExtentSizeOfPlace(const CXXNewExpr *NE, CheckerContext &C) const;

  void emitBadAlignReport(const Expr *P, CheckerContext &C,
                          unsigned AllocatedTAlign,
                          unsigned StorageTAlign) const;
  unsigned getStorageAlign(CheckerContext &C, const ValueDecl *VD) const;

  void checkElementRegionAlign(const ElementRegion *R, CheckerContext &C,
                               const Expr *P, unsigned AllocatedTAlign) const;

  void checkFieldRegionAlign(const FieldRegion *R, CheckerContext &C,
                             const Expr *P, unsigned AllocatedTAlign) const;

  bool isVarRegionAlignedProperly(const VarRegion *R, CheckerContext &C,
                                  const Expr *P,
                                  unsigned AllocatedTAlign) const;

  BugType SBT{this, "Insufficient storage for placement new",
              categories::MemoryError};
  BugType ABT{this, "Bad align storage for placement new",
              categories::MemoryError};
};
} // namespace

SVal PlacementNewChecker::getExtentSizeOfPlace(const CXXNewExpr *NE,
                                               CheckerContext &C) const {
  const Expr *Place = NE->getPlacementArg(0);
  return getDynamicExtentWithOffset(C.getState(), C.getSVal(Place));
}

SVal PlacementNewChecker::getExtentSizeOfNewTarget(const CXXNewExpr *NE,
                                                   CheckerContext &C,
                                                   bool &IsArray) const {
  ProgramStateRef State = C.getState();
  SValBuilder &SvalBuilder = C.getSValBuilder();
  QualType ElementType = NE->getAllocatedType();
  ASTContext &AstContext = C.getASTContext();
  CharUnits TypeSize = AstContext.getTypeSizeInChars(ElementType);
  IsArray = false;
  if (NE->isArray()) {
    IsArray = true;
    const Expr *SizeExpr = *NE->getArraySize();
    SVal ElementCount = C.getSVal(SizeExpr);
    if (auto ElementCountNL = ElementCount.getAs<NonLoc>()) {
      // size in Bytes = ElementCountNL * TypeSize
      return SvalBuilder.evalBinOp(
          State, BO_Mul, *ElementCountNL,
          SvalBuilder.makeArrayIndex(TypeSize.getQuantity()),
          SvalBuilder.getArrayIndexType());
    }
  } else {
    // Create a concrete int whose size in bits and signedness is equal to
    // ArrayIndexType.
    llvm::APInt I(AstContext.getTypeSizeInChars(SvalBuilder.getArrayIndexType())
                          .getQuantity() *
                      C.getASTContext().getCharWidth(),
                  TypeSize.getQuantity());
    return SvalBuilder.makeArrayIndex(I.getZExtValue());
  }
  return UnknownVal();
}

bool PlacementNewChecker::checkPlaceCapacityIsSufficient(
    const CXXNewExpr *NE, CheckerContext &C) const {
  bool IsArrayTypeAllocated;
  SVal SizeOfTarget = getExtentSizeOfNewTarget(NE, C, IsArrayTypeAllocated);
  SVal SizeOfPlace = getExtentSizeOfPlace(NE, C);
  const auto SizeOfTargetCI = SizeOfTarget.getAs<nonloc::ConcreteInt>();
  if (!SizeOfTargetCI)
    return true;
  const auto SizeOfPlaceCI = SizeOfPlace.getAs<nonloc::ConcreteInt>();
  if (!SizeOfPlaceCI)
    return true;

  if ((SizeOfPlaceCI->getValue() < SizeOfTargetCI->getValue()) ||
      (IsArrayTypeAllocated &&
       SizeOfPlaceCI->getValue() >= SizeOfTargetCI->getValue())) {
    if (ExplodedNode *N = C.generateErrorNode(C.getState())) {
      std::string Msg;
      // TODO: use clang constant
      if (IsArrayTypeAllocated &&
          SizeOfPlaceCI->getValue() > SizeOfTargetCI->getValue())
        Msg = std::string(llvm::formatv(
            "{0} bytes is possibly not enough for array allocation which "
            "requires {1} bytes. Current overhead requires the size of {2} "
            "bytes",
            SizeOfPlaceCI->getValue(), SizeOfTargetCI->getValue(),
            SizeOfPlaceCI->getValue() - SizeOfTargetCI->getValue()));
      else if (IsArrayTypeAllocated &&
               SizeOfPlaceCI->getValue() == SizeOfTargetCI->getValue())
        Msg = std::string(llvm::formatv(
            "Storage provided to placement new is only {0} bytes, "
            "whereas the allocated array type requires more space for "
            "internal needs",
            SizeOfPlaceCI->getValue(), SizeOfTargetCI->getValue()));
      else
        Msg = std::string(llvm::formatv(
            "Storage provided to placement new is only {0} bytes, "
            "whereas the allocated type requires {1} bytes",
            SizeOfPlaceCI->getValue(), SizeOfTargetCI->getValue()));

      auto R = std::make_unique<PathSensitiveBugReport>(SBT, Msg, N);
      bugreporter::trackExpressionValue(N, NE->getPlacementArg(0), *R);
      C.emitReport(std::move(R));

      return false;
    }
  }

  return true;
}

void PlacementNewChecker::emitBadAlignReport(const Expr *P, CheckerContext &C,
                                             unsigned AllocatedTAlign,
                                             unsigned StorageTAlign) const {
  ProgramStateRef State = C.getState();
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    std::string Msg(llvm::formatv("Storage type is aligned to {0} bytes but "
                                  "allocated type is aligned to {1} bytes",
                                  StorageTAlign, AllocatedTAlign));

    auto R = std::make_unique<PathSensitiveBugReport>(ABT, Msg, N);
    bugreporter::trackExpressionValue(N, P, *R);
    C.emitReport(std::move(R));
  }
}

unsigned PlacementNewChecker::getStorageAlign(CheckerContext &C,
                                              const ValueDecl *VD) const {
  unsigned StorageTAlign = C.getASTContext().getTypeAlign(VD->getType());
  if (unsigned SpecifiedAlignment = VD->getMaxAlignment())
    StorageTAlign = SpecifiedAlignment;

  return StorageTAlign / C.getASTContext().getCharWidth();
}

void PlacementNewChecker::checkElementRegionAlign(
    const ElementRegion *R, CheckerContext &C, const Expr *P,
    unsigned AllocatedTAlign) const {
  auto IsBaseRegionAlignedProperly = [this, R, &C, P,
                                      AllocatedTAlign]() -> bool {
    // Unwind nested ElementRegion`s to get the type.
    const MemRegion *SuperRegion = R;
    while (true) {
      if (SuperRegion->getKind() == MemRegion::ElementRegionKind) {
        SuperRegion = cast<SubRegion>(SuperRegion)->getSuperRegion();
        continue;
      }

      break;
    }

    const DeclRegion *TheElementDeclRegion = SuperRegion->getAs<DeclRegion>();
    if (!TheElementDeclRegion)
      return false;

    const DeclRegion *BaseDeclRegion = R->getBaseRegion()->getAs<DeclRegion>();
    if (!BaseDeclRegion)
      return false;

    unsigned BaseRegionAlign = 0;
    // We must use alignment TheElementDeclRegion if it has its own alignment
    // specifier
    if (TheElementDeclRegion->getDecl()->getMaxAlignment())
      BaseRegionAlign = getStorageAlign(C, TheElementDeclRegion->getDecl());
    else
      BaseRegionAlign = getStorageAlign(C, BaseDeclRegion->getDecl());

    if (AllocatedTAlign > BaseRegionAlign) {
      emitBadAlignReport(P, C, AllocatedTAlign, BaseRegionAlign);
      return false;
    }

    return true;
  };

  auto CheckElementRegionOffset = [this, R, &C, P, AllocatedTAlign]() -> void {
    RegionOffset TheOffsetRegion = R->getAsOffset();
    if (TheOffsetRegion.hasSymbolicOffset())
      return;

    unsigned Offset =
        TheOffsetRegion.getOffset() / C.getASTContext().getCharWidth();
    unsigned AddressAlign = Offset % AllocatedTAlign;
    if (AddressAlign != 0) {
      emitBadAlignReport(P, C, AllocatedTAlign, AddressAlign);
      return;
    }
  };

  if (IsBaseRegionAlignedProperly()) {
    CheckElementRegionOffset();
  }
}

void PlacementNewChecker::checkFieldRegionAlign(
    const FieldRegion *R, CheckerContext &C, const Expr *P,
    unsigned AllocatedTAlign) const {
  const MemRegion *BaseRegion = R->getBaseRegion();
  if (!BaseRegion)
    return;

  if (const VarRegion *TheVarRegion = BaseRegion->getAs<VarRegion>()) {
    if (isVarRegionAlignedProperly(TheVarRegion, C, P, AllocatedTAlign)) {
      // We've checked type align but, unless FieldRegion
      // offset is zero, we also need to check its own
      // align.
      RegionOffset Offset = R->getAsOffset();
      if (Offset.hasSymbolicOffset())
        return;

      int64_t OffsetValue =
          Offset.getOffset() / C.getASTContext().getCharWidth();
      unsigned AddressAlign = OffsetValue % AllocatedTAlign;
      if (AddressAlign != 0)
        emitBadAlignReport(P, C, AllocatedTAlign, AddressAlign);
    }
  }
}

bool PlacementNewChecker::isVarRegionAlignedProperly(
    const VarRegion *R, CheckerContext &C, const Expr *P,
    unsigned AllocatedTAlign) const {
  const VarDecl *TheVarDecl = R->getDecl();
  unsigned StorageTAlign = getStorageAlign(C, TheVarDecl);
  if (AllocatedTAlign > StorageTAlign) {
    emitBadAlignReport(P, C, AllocatedTAlign, StorageTAlign);

    return false;
  }

  return true;
}

bool PlacementNewChecker::checkPlaceIsAlignedProperly(const CXXNewExpr *NE,
                                                      CheckerContext &C) const {
  const Expr *Place = NE->getPlacementArg(0);

  QualType AllocatedT = NE->getAllocatedType();
  unsigned AllocatedTAlign = C.getASTContext().getTypeAlign(AllocatedT) /
                             C.getASTContext().getCharWidth();

  SVal PlaceVal = C.getSVal(Place);
  if (const MemRegion *MRegion = PlaceVal.getAsRegion()) {
    if (const ElementRegion *TheElementRegion = MRegion->getAs<ElementRegion>())
      checkElementRegionAlign(TheElementRegion, C, Place, AllocatedTAlign);
    else if (const FieldRegion *TheFieldRegion = MRegion->getAs<FieldRegion>())
      checkFieldRegionAlign(TheFieldRegion, C, Place, AllocatedTAlign);
    else if (const VarRegion *TheVarRegion = MRegion->getAs<VarRegion>())
      isVarRegionAlignedProperly(TheVarRegion, C, Place, AllocatedTAlign);
  }

  return true;
}

void PlacementNewChecker::checkPreStmt(const CXXNewExpr *NE,
                                       CheckerContext &C) const {
  // Check only the default placement new.
  if (!NE->getOperatorNew()->isReservedGlobalPlacementOperator())
    return;

  if (NE->getNumPlacementArgs() == 0)
    return;

  if (!checkPlaceCapacityIsSufficient(NE, C))
    return;

  checkPlaceIsAlignedProperly(NE, C);
}

void ento::registerPlacementNewChecker(CheckerManager &mgr) {
  mgr.registerChecker<PlacementNewChecker>();
}

bool ento::shouldRegisterPlacementNewChecker(const CheckerManager &mgr) {
  return true;
}
