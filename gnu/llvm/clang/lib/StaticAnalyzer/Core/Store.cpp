//===- Store.cpp - Interface for maps from Locations to Values ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defined the types Store and StoreManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BasicValueFactory.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <optional>

using namespace clang;
using namespace ento;

StoreManager::StoreManager(ProgramStateManager &stateMgr)
    : svalBuilder(stateMgr.getSValBuilder()), StateMgr(stateMgr),
      MRMgr(svalBuilder.getRegionManager()), Ctx(stateMgr.getContext()) {}

StoreRef StoreManager::enterStackFrame(Store OldStore,
                                       const CallEvent &Call,
                                       const StackFrameContext *LCtx) {
  StoreRef Store = StoreRef(OldStore, *this);

  SmallVector<CallEvent::FrameBindingTy, 16> InitialBindings;
  Call.getInitialStackFrameContents(LCtx, InitialBindings);

  for (const auto &I : InitialBindings)
    Store = Bind(Store.getStore(), I.first.castAs<Loc>(), I.second);

  return Store;
}

const ElementRegion *StoreManager::MakeElementRegion(const SubRegion *Base,
                                                     QualType EleTy,
                                                     uint64_t index) {
  NonLoc idx = svalBuilder.makeArrayIndex(index);
  return MRMgr.getElementRegion(EleTy, idx, Base, svalBuilder.getContext());
}

const ElementRegion *StoreManager::GetElementZeroRegion(const SubRegion *R,
                                                        QualType T) {
  NonLoc idx = svalBuilder.makeZeroArrayIndex();
  assert(!T.isNull());
  return MRMgr.getElementRegion(T, idx, R, Ctx);
}

std::optional<const MemRegion *> StoreManager::castRegion(const MemRegion *R,
                                                          QualType CastToTy) {
  ASTContext &Ctx = StateMgr.getContext();

  // Handle casts to Objective-C objects.
  if (CastToTy->isObjCObjectPointerType())
    return R->StripCasts();

  if (CastToTy->isBlockPointerType()) {
    // FIXME: We may need different solutions, depending on the symbol
    // involved.  Blocks can be casted to/from 'id', as they can be treated
    // as Objective-C objects.  This could possibly be handled by enhancing
    // our reasoning of downcasts of symbolic objects.
    if (isa<CodeTextRegion, SymbolicRegion>(R))
      return R;

    // We don't know what to make of it.  Return a NULL region, which
    // will be interpreted as UnknownVal.
    return std::nullopt;
  }

  // Now assume we are casting from pointer to pointer. Other cases should
  // already be handled.
  QualType PointeeTy = CastToTy->getPointeeType();
  QualType CanonPointeeTy = Ctx.getCanonicalType(PointeeTy);
  CanonPointeeTy = CanonPointeeTy.getLocalUnqualifiedType();

  // Handle casts to void*.  We just pass the region through.
  if (CanonPointeeTy == Ctx.VoidTy)
    return R;

  const auto IsSameRegionType = [&Ctx](const MemRegion *R, QualType OtherTy) {
    if (const auto *TR = dyn_cast<TypedValueRegion>(R)) {
      QualType ObjTy = Ctx.getCanonicalType(TR->getValueType());
      if (OtherTy == ObjTy.getLocalUnqualifiedType())
        return true;
    }
    return false;
  };

  // Handle casts from compatible types.
  if (R->isBoundable() && IsSameRegionType(R, CanonPointeeTy))
    return R;

  // Process region cast according to the kind of the region being cast.
  switch (R->getKind()) {
    case MemRegion::CXXThisRegionKind:
    case MemRegion::CodeSpaceRegionKind:
    case MemRegion::StackLocalsSpaceRegionKind:
    case MemRegion::StackArgumentsSpaceRegionKind:
    case MemRegion::HeapSpaceRegionKind:
    case MemRegion::UnknownSpaceRegionKind:
    case MemRegion::StaticGlobalSpaceRegionKind:
    case MemRegion::GlobalInternalSpaceRegionKind:
    case MemRegion::GlobalSystemSpaceRegionKind:
    case MemRegion::GlobalImmutableSpaceRegionKind: {
      llvm_unreachable("Invalid region cast");
    }

    case MemRegion::FunctionCodeRegionKind:
    case MemRegion::BlockCodeRegionKind:
    case MemRegion::BlockDataRegionKind:
    case MemRegion::StringRegionKind:
      // FIXME: Need to handle arbitrary downcasts.
    case MemRegion::SymbolicRegionKind:
    case MemRegion::AllocaRegionKind:
    case MemRegion::CompoundLiteralRegionKind:
    case MemRegion::FieldRegionKind:
    case MemRegion::ObjCIvarRegionKind:
    case MemRegion::ObjCStringRegionKind:
    case MemRegion::NonParamVarRegionKind:
    case MemRegion::ParamVarRegionKind:
    case MemRegion::CXXTempObjectRegionKind:
    case MemRegion::CXXLifetimeExtendedObjectRegionKind:
    case MemRegion::CXXBaseObjectRegionKind:
    case MemRegion::CXXDerivedObjectRegionKind:
      return MakeElementRegion(cast<SubRegion>(R), PointeeTy);

    case MemRegion::ElementRegionKind: {
      // If we are casting from an ElementRegion to another type, the
      // algorithm is as follows:
      //
      // (1) Compute the "raw offset" of the ElementRegion from the
      //     base region.  This is done by calling 'getAsRawOffset()'.
      //
      // (2a) If we get a 'RegionRawOffset' after calling
      //      'getAsRawOffset()', determine if the absolute offset
      //      can be exactly divided into chunks of the size of the
      //      casted-pointee type.  If so, create a new ElementRegion with
      //      the pointee-cast type as the new ElementType and the index
      //      being the offset divded by the chunk size.  If not, create
      //      a new ElementRegion at offset 0 off the raw offset region.
      //
      // (2b) If we don't a get a 'RegionRawOffset' after calling
      //      'getAsRawOffset()', it means that we are at offset 0.
      //
      // FIXME: Handle symbolic raw offsets.

      const ElementRegion *elementR = cast<ElementRegion>(R);
      const RegionRawOffset &rawOff = elementR->getAsArrayOffset();
      const MemRegion *baseR = rawOff.getRegion();

      // If we cannot compute a raw offset, throw up our hands and return
      // a NULL MemRegion*.
      if (!baseR)
        return std::nullopt;

      CharUnits off = rawOff.getOffset();

      if (off.isZero()) {
        // Edge case: we are at 0 bytes off the beginning of baseR. We check to
        // see if the type we are casting to is the same as the type of the base
        // region. If so, just return the base region.
        if (IsSameRegionType(baseR, CanonPointeeTy))
          return baseR;
        // Otherwise, create a new ElementRegion at offset 0.
        return MakeElementRegion(cast<SubRegion>(baseR), PointeeTy);
      }

      // We have a non-zero offset from the base region.  We want to determine
      // if the offset can be evenly divided by sizeof(PointeeTy).  If so,
      // we create an ElementRegion whose index is that value.  Otherwise, we
      // create two ElementRegions, one that reflects a raw offset and the other
      // that reflects the cast.

      // Compute the index for the new ElementRegion.
      int64_t newIndex = 0;
      const MemRegion *newSuperR = nullptr;

      // We can only compute sizeof(PointeeTy) if it is a complete type.
      if (!PointeeTy->isIncompleteType()) {
        // Compute the size in **bytes**.
        CharUnits pointeeTySize = Ctx.getTypeSizeInChars(PointeeTy);
        if (!pointeeTySize.isZero()) {
          // Is the offset a multiple of the size?  If so, we can layer the
          // ElementRegion (with elementType == PointeeTy) directly on top of
          // the base region.
          if (off % pointeeTySize == 0) {
            newIndex = off / pointeeTySize;
            newSuperR = baseR;
          }
        }
      }

      if (!newSuperR) {
        // Create an intermediate ElementRegion to represent the raw byte.
        // This will be the super region of the final ElementRegion.
        newSuperR = MakeElementRegion(cast<SubRegion>(baseR), Ctx.CharTy,
                                      off.getQuantity());
      }

      return MakeElementRegion(cast<SubRegion>(newSuperR), PointeeTy, newIndex);
    }
  }

  llvm_unreachable("unreachable");
}

static bool regionMatchesCXXRecordType(SVal V, QualType Ty) {
  const MemRegion *MR = V.getAsRegion();
  if (!MR)
    return true;

  const auto *TVR = dyn_cast<TypedValueRegion>(MR);
  if (!TVR)
    return true;

  const CXXRecordDecl *RD = TVR->getValueType()->getAsCXXRecordDecl();
  if (!RD)
    return true;

  const CXXRecordDecl *Expected = Ty->getPointeeCXXRecordDecl();
  if (!Expected)
    Expected = Ty->getAsCXXRecordDecl();

  return Expected->getCanonicalDecl() == RD->getCanonicalDecl();
}

SVal StoreManager::evalDerivedToBase(SVal Derived, const CastExpr *Cast) {
  // Early return to avoid doing the wrong thing in the face of
  // reinterpret_cast.
  if (!regionMatchesCXXRecordType(Derived, Cast->getSubExpr()->getType()))
    return UnknownVal();

  // Walk through the cast path to create nested CXXBaseRegions.
  SVal Result = Derived;
  for (const CXXBaseSpecifier *Base : Cast->path()) {
    Result = evalDerivedToBase(Result, Base->getType(), Base->isVirtual());
  }
  return Result;
}

SVal StoreManager::evalDerivedToBase(SVal Derived, const CXXBasePath &Path) {
  // Walk through the path to create nested CXXBaseRegions.
  SVal Result = Derived;
  for (const auto &I : Path)
    Result = evalDerivedToBase(Result, I.Base->getType(),
                               I.Base->isVirtual());
  return Result;
}

SVal StoreManager::evalDerivedToBase(SVal Derived, QualType BaseType,
                                     bool IsVirtual) {
  const MemRegion *DerivedReg = Derived.getAsRegion();
  if (!DerivedReg)
    return Derived;

  const CXXRecordDecl *BaseDecl = BaseType->getPointeeCXXRecordDecl();
  if (!BaseDecl)
    BaseDecl = BaseType->getAsCXXRecordDecl();
  assert(BaseDecl && "not a C++ object?");

  if (const auto *AlreadyDerivedReg =
          dyn_cast<CXXDerivedObjectRegion>(DerivedReg)) {
    if (const auto *SR =
            dyn_cast<SymbolicRegion>(AlreadyDerivedReg->getSuperRegion()))
      if (SR->getSymbol()->getType()->getPointeeCXXRecordDecl() == BaseDecl)
        return loc::MemRegionVal(SR);

    DerivedReg = AlreadyDerivedReg->getSuperRegion();
  }

  const MemRegion *BaseReg = MRMgr.getCXXBaseObjectRegion(
      BaseDecl, cast<SubRegion>(DerivedReg), IsVirtual);

  return loc::MemRegionVal(BaseReg);
}

/// Returns the static type of the given region, if it represents a C++ class
/// object.
///
/// This handles both fully-typed regions, where the dynamic type is known, and
/// symbolic regions, where the dynamic type is merely bounded (and even then,
/// only ostensibly!), but does not take advantage of any dynamic type info.
static const CXXRecordDecl *getCXXRecordType(const MemRegion *MR) {
  if (const auto *TVR = dyn_cast<TypedValueRegion>(MR))
    return TVR->getValueType()->getAsCXXRecordDecl();
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    return SR->getSymbol()->getType()->getPointeeCXXRecordDecl();
  return nullptr;
}

std::optional<SVal> StoreManager::evalBaseToDerived(SVal Base,
                                                    QualType TargetType) {
  const MemRegion *MR = Base.getAsRegion();
  if (!MR)
    return UnknownVal();

  // Assume the derived class is a pointer or a reference to a CXX record.
  TargetType = TargetType->getPointeeType();
  assert(!TargetType.isNull());
  const CXXRecordDecl *TargetClass = TargetType->getAsCXXRecordDecl();
  if (!TargetClass && !TargetType->isVoidType())
    return UnknownVal();

  // Drill down the CXXBaseObject chains, which represent upcasts (casts from
  // derived to base).
  while (const CXXRecordDecl *MRClass = getCXXRecordType(MR)) {
    // If found the derived class, the cast succeeds.
    if (MRClass == TargetClass)
      return loc::MemRegionVal(MR);

    // We skip over incomplete types. They must be the result of an earlier
    // reinterpret_cast, as one can only dynamic_cast between types in the same
    // class hierarchy.
    if (!TargetType->isVoidType() && MRClass->hasDefinition()) {
      // Static upcasts are marked as DerivedToBase casts by Sema, so this will
      // only happen when multiple or virtual inheritance is involved.
      CXXBasePaths Paths(/*FindAmbiguities=*/false, /*RecordPaths=*/true,
                         /*DetectVirtual=*/false);
      if (MRClass->isDerivedFrom(TargetClass, Paths))
        return evalDerivedToBase(loc::MemRegionVal(MR), Paths.front());
    }

    if (const auto *BaseR = dyn_cast<CXXBaseObjectRegion>(MR)) {
      // Drill down the chain to get the derived classes.
      MR = BaseR->getSuperRegion();
      continue;
    }

    // If this is a cast to void*, return the region.
    if (TargetType->isVoidType())
      return loc::MemRegionVal(MR);

    // Strange use of reinterpret_cast can give us paths we don't reason
    // about well, by putting in ElementRegions where we'd expect
    // CXXBaseObjectRegions. If it's a valid reinterpret_cast (i.e. if the
    // derived class has a zero offset from the base class), then it's safe
    // to strip the cast; if it's invalid, -Wreinterpret-base-class should
    // catch it. In the interest of performance, the analyzer will silently
    // do the wrong thing in the invalid case (because offsets for subregions
    // will be wrong).
    const MemRegion *Uncasted = MR->StripCasts(/*IncludeBaseCasts=*/false);
    if (Uncasted == MR) {
      // We reached the bottom of the hierarchy and did not find the derived
      // class. We must be casting the base to derived, so the cast should
      // fail.
      break;
    }

    MR = Uncasted;
  }

  // If we're casting a symbolic base pointer to a derived class, use
  // CXXDerivedObjectRegion to represent the cast. If it's a pointer to an
  // unrelated type, it must be a weird reinterpret_cast and we have to
  // be fine with ElementRegion. TODO: Should we instead make
  // Derived{TargetClass, Element{SourceClass, SR}}?
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR)) {
    QualType T = SR->getSymbol()->getType();
    const CXXRecordDecl *SourceClass = T->getPointeeCXXRecordDecl();
    if (TargetClass && SourceClass && TargetClass->isDerivedFrom(SourceClass))
      return loc::MemRegionVal(
          MRMgr.getCXXDerivedObjectRegion(TargetClass, SR));
    return loc::MemRegionVal(GetElementZeroRegion(SR, TargetType));
  }

  // We failed if the region we ended up with has perfect type info.
  if (isa<TypedValueRegion>(MR))
    return std::nullopt;

  return UnknownVal();
}

SVal StoreManager::getLValueFieldOrIvar(const Decl *D, SVal Base) {
  if (Base.isUnknownOrUndef())
    return Base;

  Loc BaseL = Base.castAs<Loc>();
  const SubRegion* BaseR = nullptr;

  switch (BaseL.getKind()) {
  case loc::MemRegionValKind:
    BaseR = cast<SubRegion>(BaseL.castAs<loc::MemRegionVal>().getRegion());
    break;

  case loc::GotoLabelKind:
    // These are anormal cases. Flag an undefined value.
    return UndefinedVal();

  case loc::ConcreteIntKind:
    // While these seem funny, this can happen through casts.
    // FIXME: What we should return is the field offset, not base. For example,
    //  add the field offset to the integer value.  That way things
    //  like this work properly:  &(((struct foo *) 0xa)->f)
    //  However, that's not easy to fix without reducing our abilities
    //  to catch null pointer dereference. Eg., ((struct foo *)0x0)->f = 7
    //  is a null dereference even though we're dereferencing offset of f
    //  rather than null. Coming up with an approach that computes offsets
    //  over null pointers properly while still being able to catch null
    //  dereferences might be worth it.
    return Base;

  default:
    llvm_unreachable("Unhandled Base.");
  }

  // NOTE: We must have this check first because ObjCIvarDecl is a subclass
  // of FieldDecl.
  if (const auto *ID = dyn_cast<ObjCIvarDecl>(D))
    return loc::MemRegionVal(MRMgr.getObjCIvarRegion(ID, BaseR));

  return loc::MemRegionVal(MRMgr.getFieldRegion(cast<FieldDecl>(D), BaseR));
}

SVal StoreManager::getLValueIvar(const ObjCIvarDecl *decl, SVal base) {
  return getLValueFieldOrIvar(decl, base);
}

SVal StoreManager::getLValueElement(QualType elementType, NonLoc Offset,
                                    SVal Base) {

  // Special case, if index is 0, return the same type as if
  // this was not an array dereference.
  if (Offset.isZeroConstant()) {
    QualType BT = Base.getType(this->Ctx);
    if (!BT.isNull() && !elementType.isNull()) {
      QualType PointeeTy = BT->getPointeeType();
      if (!PointeeTy.isNull() &&
          PointeeTy.getCanonicalType() == elementType.getCanonicalType())
        return Base;
    }
  }

  // If the base is an unknown or undefined value, just return it back.
  // FIXME: For absolute pointer addresses, we just return that value back as
  //  well, although in reality we should return the offset added to that
  //  value. See also the similar FIXME in getLValueFieldOrIvar().
  if (Base.isUnknownOrUndef() || isa<loc::ConcreteInt>(Base))
    return Base;

  if (isa<loc::GotoLabel>(Base))
    return UnknownVal();

  const SubRegion *BaseRegion =
      Base.castAs<loc::MemRegionVal>().getRegionAs<SubRegion>();

  // Pointer of any type can be cast and used as array base.
  const auto *ElemR = dyn_cast<ElementRegion>(BaseRegion);

  // Convert the offset to the appropriate size and signedness.
  auto Off = svalBuilder.convertToArrayIndex(Offset).getAs<NonLoc>();
  if (!Off) {
    // Handle cases when LazyCompoundVal is used for an array index.
    // Such case is possible if code does:
    //   char b[4];
    //   a[__builtin_bitcast(int, b)];
    // Return UnknownVal, since we cannot model it.
    return UnknownVal();
  }

  Offset = Off.value();

  if (!ElemR) {
    // If the base region is not an ElementRegion, create one.
    // This can happen in the following example:
    //
    //   char *p = __builtin_alloc(10);
    //   p[1] = 8;
    //
    //  Observe that 'p' binds to an AllocaRegion.
    return loc::MemRegionVal(MRMgr.getElementRegion(elementType, Offset,
                                                    BaseRegion, Ctx));
  }

  SVal BaseIdx = ElemR->getIndex();

  if (!isa<nonloc::ConcreteInt>(BaseIdx))
    return UnknownVal();

  const llvm::APSInt &BaseIdxI =
      BaseIdx.castAs<nonloc::ConcreteInt>().getValue();

  // Only allow non-integer offsets if the base region has no offset itself.
  // FIXME: This is a somewhat arbitrary restriction. We should be using
  // SValBuilder here to add the two offsets without checking their types.
  if (!isa<nonloc::ConcreteInt>(Offset)) {
    if (isa<ElementRegion>(BaseRegion->StripCasts()))
      return UnknownVal();

    return loc::MemRegionVal(MRMgr.getElementRegion(
        elementType, Offset, cast<SubRegion>(ElemR->getSuperRegion()), Ctx));
  }

  const llvm::APSInt& OffI = Offset.castAs<nonloc::ConcreteInt>().getValue();
  assert(BaseIdxI.isSigned());

  // Compute the new index.
  nonloc::ConcreteInt NewIdx(svalBuilder.getBasicValueFactory().getValue(BaseIdxI +
                                                                    OffI));

  // Construct the new ElementRegion.
  const SubRegion *ArrayR = cast<SubRegion>(ElemR->getSuperRegion());
  return loc::MemRegionVal(MRMgr.getElementRegion(elementType, NewIdx, ArrayR,
                                                  Ctx));
}

StoreManager::BindingsHandler::~BindingsHandler() = default;

bool StoreManager::FindUniqueBinding::HandleBinding(StoreManager& SMgr,
                                                    Store store,
                                                    const MemRegion* R,
                                                    SVal val) {
  SymbolRef SymV = val.getAsLocSymbol();
  if (!SymV || SymV != Sym)
    return true;

  if (Binding) {
    First = false;
    return false;
  }
  else
    Binding = R;

  return true;
}
