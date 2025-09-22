//=== CastSizeChecker.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CastSizeChecker checks when casting a malloc'ed symbolic region to type T,
// whether the size of the symbolic region is a multiple of the size of T.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CharUnits.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"

using namespace clang;
using namespace ento;

namespace {
class CastSizeChecker : public Checker< check::PreStmt<CastExpr> > {
  const BugType BT{this, "Cast region with wrong size."};

public:
  void checkPreStmt(const CastExpr *CE, CheckerContext &C) const;
};
}

/// Check if we are casting to a struct with a flexible array at the end.
/// \code
/// struct foo {
///   size_t len;
///   struct bar data[];
/// };
/// \endcode
/// or
/// \code
/// struct foo {
///   size_t len;
///   struct bar data[0];
/// }
/// \endcode
/// In these cases it is also valid to allocate size of struct foo + a multiple
/// of struct bar.
static bool evenFlexibleArraySize(ASTContext &Ctx, CharUnits RegionSize,
                                  CharUnits TypeSize, QualType ToPointeeTy) {
  const RecordType *RT = ToPointeeTy->getAs<RecordType>();
  if (!RT)
    return false;

  const RecordDecl *RD = RT->getDecl();
  RecordDecl::field_iterator Iter(RD->field_begin());
  RecordDecl::field_iterator End(RD->field_end());
  const FieldDecl *Last = nullptr;
  for (; Iter != End; ++Iter)
    Last = *Iter;
  assert(Last && "empty structs should already be handled");

  const Type *ElemType = Last->getType()->getArrayElementTypeNoTypeQual();
  CharUnits FlexSize;
  if (const ConstantArrayType *ArrayTy =
        Ctx.getAsConstantArrayType(Last->getType())) {
    FlexSize = Ctx.getTypeSizeInChars(ElemType);
    if (ArrayTy->getSize() == 1 && TypeSize > FlexSize)
      TypeSize -= FlexSize;
    else if (!ArrayTy->isZeroSize())
      return false;
  } else if (RD->hasFlexibleArrayMember()) {
    FlexSize = Ctx.getTypeSizeInChars(ElemType);
  } else {
    return false;
  }

  if (FlexSize.isZero())
    return false;

  CharUnits Left = RegionSize - TypeSize;
  if (Left.isNegative())
    return false;

  return Left % FlexSize == 0;
}

void CastSizeChecker::checkPreStmt(const CastExpr *CE,CheckerContext &C) const {
  const Expr *E = CE->getSubExpr();
  ASTContext &Ctx = C.getASTContext();
  QualType ToTy = Ctx.getCanonicalType(CE->getType());
  const PointerType *ToPTy = dyn_cast<PointerType>(ToTy.getTypePtr());

  if (!ToPTy)
    return;

  QualType ToPointeeTy = ToPTy->getPointeeType();

  // Only perform the check if 'ToPointeeTy' is a complete type.
  if (ToPointeeTy->isIncompleteType())
    return;

  ProgramStateRef state = C.getState();
  const MemRegion *R = C.getSVal(E).getAsRegion();
  if (!R)
    return;

  const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R);
  if (!SR)
    return;

  SValBuilder &svalBuilder = C.getSValBuilder();

  DefinedOrUnknownSVal Size = getDynamicExtent(state, SR, svalBuilder);
  const llvm::APSInt *SizeInt = svalBuilder.getKnownValue(state, Size);
  if (!SizeInt)
    return;

  CharUnits regionSize = CharUnits::fromQuantity(SizeInt->getZExtValue());
  CharUnits typeSize = C.getASTContext().getTypeSizeInChars(ToPointeeTy);

  // Ignore void, and a few other un-sizeable types.
  if (typeSize.isZero())
    return;

  if (regionSize % typeSize == 0)
    return;

  if (evenFlexibleArraySize(Ctx, regionSize, typeSize, ToPointeeTy))
    return;

  if (ExplodedNode *errorNode = C.generateErrorNode()) {
    constexpr llvm::StringLiteral Msg =
        "Cast a region whose size is not a multiple of the destination type "
        "size.";
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, errorNode);
    R->addRange(CE->getSourceRange());
    C.emitReport(std::move(R));
  }
}

void ento::registerCastSizeChecker(CheckerManager &mgr) {
  mgr.registerChecker<CastSizeChecker>();
}

bool ento::shouldRegisterCastSizeChecker(const CheckerManager &mgr) {
  // PR31226: C++ is more complicated than what this checker currently supports.
  // There are derived-to-base casts, there are different rules for 0-size
  // structures, no flexible arrays, etc.
  // FIXME: Disabled on C++ for now.
  const LangOptions &LO = mgr.getLangOpts();
  return !LO.CPlusPlus;
}
