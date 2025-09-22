//===----- UninitializedPointee.cpp ------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions and methods for handling pointers and references
// to reduce the size and complexity of UninitializedObjectChecker.cpp.
//
// To read about command line options and documentation about how the checker
// works, refer to UninitializedObjectChecker.h.
//
//===----------------------------------------------------------------------===//

#include "UninitializedObject.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include <optional>

using namespace clang;
using namespace clang::ento;

namespace {

/// Represents a pointer or a reference field.
class LocField final : public FieldNode {
  /// We'll store whether the pointee or the pointer itself is uninitialited.
  const bool IsDereferenced;

public:
  LocField(const FieldRegion *FR, const bool IsDereferenced = true)
      : FieldNode(FR), IsDereferenced(IsDereferenced) {}

  void printNoteMsg(llvm::raw_ostream &Out) const override {
    if (IsDereferenced)
      Out << "uninitialized pointee ";
    else
      Out << "uninitialized pointer ";
  }

  void printPrefix(llvm::raw_ostream &Out) const override {}

  void printNode(llvm::raw_ostream &Out) const override {
    Out << getVariableName(getDecl());
  }

  void printSeparator(llvm::raw_ostream &Out) const override {
    if (getDecl()->getType()->isPointerType())
      Out << "->";
    else
      Out << '.';
  }
};

/// Represents a nonloc::LocAsInteger or void* field, that point to objects, but
/// needs to be casted back to its dynamic type for a correct note message.
class NeedsCastLocField final : public FieldNode {
  QualType CastBackType;

public:
  NeedsCastLocField(const FieldRegion *FR, const QualType &T)
      : FieldNode(FR), CastBackType(T) {}

  void printNoteMsg(llvm::raw_ostream &Out) const override {
    Out << "uninitialized pointee ";
  }

  void printPrefix(llvm::raw_ostream &Out) const override {
    // If this object is a nonloc::LocAsInteger.
    if (getDecl()->getType()->isIntegerType())
      Out << "reinterpret_cast";
    // If this pointer's dynamic type is different then it's static type.
    else
      Out << "static_cast";
    Out << '<' << CastBackType.getAsString() << ">(";
  }

  void printNode(llvm::raw_ostream &Out) const override {
    Out << getVariableName(getDecl()) << ')';
  }

  void printSeparator(llvm::raw_ostream &Out) const override { Out << "->"; }
};

/// Represents a Loc field that points to itself.
class CyclicLocField final : public FieldNode {

public:
  CyclicLocField(const FieldRegion *FR) : FieldNode(FR) {}

  void printNoteMsg(llvm::raw_ostream &Out) const override {
    Out << "object references itself ";
  }

  void printPrefix(llvm::raw_ostream &Out) const override {}

  void printNode(llvm::raw_ostream &Out) const override {
    Out << getVariableName(getDecl());
  }

  void printSeparator(llvm::raw_ostream &Out) const override {
    llvm_unreachable("CyclicLocField objects must be the last node of the "
                     "fieldchain!");
  }
};

} // end of anonymous namespace

// Utility function declarations.

struct DereferenceInfo {
  const TypedValueRegion *R;
  const bool NeedsCastBack;
  const bool IsCyclic;
  DereferenceInfo(const TypedValueRegion *R, bool NCB, bool IC)
      : R(R), NeedsCastBack(NCB), IsCyclic(IC) {}
};

/// Dereferences \p FR and returns with the pointee's region, and whether it
/// needs to be casted back to it's location type. If for whatever reason
/// dereferencing fails, returns std::nullopt.
static std::optional<DereferenceInfo> dereference(ProgramStateRef State,
                                                  const FieldRegion *FR);

/// Returns whether \p T can be (transitively) dereferenced to a void pointer
/// type (void*, void**, ...).
static bool isVoidPointer(QualType T);

//===----------------------------------------------------------------------===//
//                   Methods for FindUninitializedFields.
//===----------------------------------------------------------------------===//

bool FindUninitializedFields::isDereferencableUninit(
    const FieldRegion *FR, FieldChainInfo LocalChain) {

  SVal V = State->getSVal(FR);

  assert((isDereferencableType(FR->getDecl()->getType()) ||
          isa<nonloc::LocAsInteger>(V)) &&
         "This method only checks dereferenceable objects!");

  if (V.isUnknown() || isa<loc::ConcreteInt>(V)) {
    IsAnyFieldInitialized = true;
    return false;
  }

  if (V.isUndef()) {
    return addFieldToUninits(
        LocalChain.add(LocField(FR, /*IsDereferenced*/ false)), FR);
  }

  if (!Opts.CheckPointeeInitialization) {
    IsAnyFieldInitialized = true;
    return false;
  }

  // At this point the pointer itself is initialized and points to a valid
  // location, we'll now check the pointee.
  std::optional<DereferenceInfo> DerefInfo = dereference(State, FR);
  if (!DerefInfo) {
    IsAnyFieldInitialized = true;
    return false;
  }

  if (DerefInfo->IsCyclic)
    return addFieldToUninits(LocalChain.add(CyclicLocField(FR)), FR);

  const TypedValueRegion *R = DerefInfo->R;
  const bool NeedsCastBack = DerefInfo->NeedsCastBack;

  QualType DynT = R->getLocationType();
  QualType PointeeT = DynT->getPointeeType();

  if (PointeeT->isStructureOrClassType()) {
    if (NeedsCastBack)
      return isNonUnionUninit(R, LocalChain.add(NeedsCastLocField(FR, DynT)));
    return isNonUnionUninit(R, LocalChain.add(LocField(FR)));
  }

  if (PointeeT->isUnionType()) {
    if (isUnionUninit(R)) {
      if (NeedsCastBack)
        return addFieldToUninits(LocalChain.add(NeedsCastLocField(FR, DynT)),
                                 R);
      return addFieldToUninits(LocalChain.add(LocField(FR)), R);
    } else {
      IsAnyFieldInitialized = true;
      return false;
    }
  }

  if (PointeeT->isArrayType()) {
    IsAnyFieldInitialized = true;
    return false;
  }

  assert((isPrimitiveType(PointeeT) || isDereferencableType(PointeeT)) &&
         "At this point FR must either have a primitive dynamic type, or it "
         "must be a null, undefined, unknown or concrete pointer!");

  SVal PointeeV = State->getSVal(R);

  if (isPrimitiveUninit(PointeeV)) {
    if (NeedsCastBack)
      return addFieldToUninits(LocalChain.add(NeedsCastLocField(FR, DynT)), R);
    return addFieldToUninits(LocalChain.add(LocField(FR)), R);
  }

  IsAnyFieldInitialized = true;
  return false;
}

//===----------------------------------------------------------------------===//
//                           Utility functions.
//===----------------------------------------------------------------------===//

static std::optional<DereferenceInfo> dereference(ProgramStateRef State,
                                                  const FieldRegion *FR) {

  llvm::SmallSet<const TypedValueRegion *, 5> VisitedRegions;

  SVal V = State->getSVal(FR);
  assert(V.getAsRegion() && "V must have an underlying region!");

  // If the static type of the field is a void pointer, or it is a
  // nonloc::LocAsInteger, we need to cast it back to the dynamic type before
  // dereferencing.
  bool NeedsCastBack =
      isVoidPointer(FR->getDecl()->getType()) || isa<nonloc::LocAsInteger>(V);

  // The region we'd like to acquire.
  const auto *R = V.getAsRegion()->getAs<TypedValueRegion>();
  if (!R)
    return std::nullopt;

  VisitedRegions.insert(R);

  // We acquire the dynamic type of R,
  QualType DynT = R->getLocationType();

  while (const MemRegion *Tmp = State->getSVal(R, DynT).getAsRegion()) {

    R = Tmp->getAs<TypedValueRegion>();
    if (!R)
      return std::nullopt;

    // We found a cyclic pointer, like int *ptr = (int *)&ptr.
    if (!VisitedRegions.insert(R).second)
      return DereferenceInfo{R, NeedsCastBack, /*IsCyclic*/ true};

    DynT = R->getLocationType();
    // In order to ensure that this loop terminates, we're also checking the
    // dynamic type of R, since type hierarchy is finite.
    if (isDereferencableType(DynT->getPointeeType()))
      break;
  }

  while (isa<CXXBaseObjectRegion>(R)) {
    NeedsCastBack = true;
    const auto *SuperR = dyn_cast<TypedValueRegion>(R->getSuperRegion());
    if (!SuperR)
      break;

    R = SuperR;
  }

  return DereferenceInfo{R, NeedsCastBack, /*IsCyclic*/ false};
}

static bool isVoidPointer(QualType T) {
  while (!T.isNull()) {
    if (T->isVoidPointerType())
      return true;
    T = T->getPointeeType();
  }
  return false;
}
