//===- DynamicType.cpp - Dynamic type related APIs --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines APIs that track and query dynamic type information. This
//  information can be used to devirtualize calls during the symbolic execution
//  or do type checking.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include "clang/Basic/JsonSupport.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

/// The GDM component containing the dynamic type info. This is a map from a
/// symbol to its most likely type.
REGISTER_MAP_WITH_PROGRAMSTATE(DynamicTypeMap, const clang::ento::MemRegion *,
                               clang::ento::DynamicTypeInfo)

/// A set factory of dynamic cast informations.
REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(CastSet, clang::ento::DynamicCastInfo)

/// A map from symbols to cast informations.
REGISTER_MAP_WITH_PROGRAMSTATE(DynamicCastMap, const clang::ento::MemRegion *,
                               CastSet)

// A map from Class object symbols to the most likely pointed-to type.
REGISTER_MAP_WITH_PROGRAMSTATE(DynamicClassObjectMap, clang::ento::SymbolRef,
                               clang::ento::DynamicTypeInfo)

namespace clang {
namespace ento {

DynamicTypeInfo getDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR) {
  MR = MR->StripCasts();

  // Look up the dynamic type in the GDM.
  if (const DynamicTypeInfo *DTI = State->get<DynamicTypeMap>(MR))
    return *DTI;

  // Otherwise, fall back to what we know about the region.
  if (const auto *TR = dyn_cast<TypedRegion>(MR))
    return DynamicTypeInfo(TR->getLocationType(), /*CanBeSub=*/false);

  if (const auto *SR = dyn_cast<SymbolicRegion>(MR)) {
    SymbolRef Sym = SR->getSymbol();
    return DynamicTypeInfo(Sym->getType());
  }

  return {};
}

const DynamicTypeInfo *getRawDynamicTypeInfo(ProgramStateRef State,
                                             const MemRegion *MR) {
  return State->get<DynamicTypeMap>(MR);
}

static void unbox(QualType &Ty) {
  // FIXME: Why are we being fed references to pointers in the first place?
  while (Ty->isReferenceType() || Ty->isPointerType())
    Ty = Ty->getPointeeType();
  Ty = Ty.getCanonicalType().getUnqualifiedType();
}

const DynamicCastInfo *getDynamicCastInfo(ProgramStateRef State,
                                          const MemRegion *MR,
                                          QualType CastFromTy,
                                          QualType CastToTy) {
  const auto *Lookup = State->get<DynamicCastMap>().lookup(MR);
  if (!Lookup)
    return nullptr;

  unbox(CastFromTy);
  unbox(CastToTy);

  for (const DynamicCastInfo &Cast : *Lookup)
    if (Cast.equals(CastFromTy, CastToTy))
      return &Cast;

  return nullptr;
}

DynamicTypeInfo getClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym) {
  const DynamicTypeInfo *DTI = State->get<DynamicClassObjectMap>(Sym);
  return DTI ? *DTI : DynamicTypeInfo{};
}

ProgramStateRef setDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR,
                                   DynamicTypeInfo NewTy) {
  State = State->set<DynamicTypeMap>(MR->StripCasts(), NewTy);
  assert(State);
  return State;
}

ProgramStateRef setDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR,
                                   QualType NewTy, bool CanBeSubClassed) {
  return setDynamicTypeInfo(State, MR, DynamicTypeInfo(NewTy, CanBeSubClassed));
}

ProgramStateRef setDynamicTypeAndCastInfo(ProgramStateRef State,
                                          const MemRegion *MR,
                                          QualType CastFromTy,
                                          QualType CastToTy,
                                          bool CastSucceeds) {
  if (!MR)
    return State;

  if (CastSucceeds) {
    assert((CastToTy->isAnyPointerType() || CastToTy->isReferenceType()) &&
           "DynamicTypeInfo should always be a pointer.");
    State = State->set<DynamicTypeMap>(MR, CastToTy);
  }

  unbox(CastFromTy);
  unbox(CastToTy);

  DynamicCastInfo::CastResult ResultKind =
      CastSucceeds ? DynamicCastInfo::CastResult::Success
                   : DynamicCastInfo::CastResult::Failure;

  CastSet::Factory &F = State->get_context<CastSet>();

  const CastSet *TempSet = State->get<DynamicCastMap>(MR);
  CastSet Set = TempSet ? *TempSet : F.getEmptySet();

  Set = F.add(Set, {CastFromTy, CastToTy, ResultKind});
  State = State->set<DynamicCastMap>(MR, Set);

  assert(State);
  return State;
}

ProgramStateRef setClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym,
                                              DynamicTypeInfo NewTy) {
  State = State->set<DynamicClassObjectMap>(Sym, NewTy);
  return State;
}

ProgramStateRef setClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym, QualType NewTy,
                                              bool CanBeSubClassed) {
  return setClassObjectDynamicTypeInfo(State, Sym,
                                       DynamicTypeInfo(NewTy, CanBeSubClassed));
}

static bool isLive(SymbolReaper &SR, const MemRegion *MR) {
  return SR.isLiveRegion(MR);
}

static bool isLive(SymbolReaper &SR, SymbolRef Sym) { return SR.isLive(Sym); }

template <typename MapTy>
static ProgramStateRef removeDeadImpl(ProgramStateRef State, SymbolReaper &SR) {
  const auto &Map = State->get<MapTy>();

  for (const auto &Elem : Map)
    if (!isLive(SR, Elem.first))
      State = State->remove<MapTy>(Elem.first);

  return State;
}

ProgramStateRef removeDeadTypes(ProgramStateRef State, SymbolReaper &SR) {
  return removeDeadImpl<DynamicTypeMap>(State, SR);
}

ProgramStateRef removeDeadCasts(ProgramStateRef State, SymbolReaper &SR) {
  return removeDeadImpl<DynamicCastMap>(State, SR);
}

ProgramStateRef removeDeadClassObjectTypes(ProgramStateRef State,
                                           SymbolReaper &SR) {
  return removeDeadImpl<DynamicClassObjectMap>(State, SR);
}

//===----------------------------------------------------------------------===//
//               Implementation of the 'printer-to-JSON' function
//===----------------------------------------------------------------------===//

static raw_ostream &printJson(const MemRegion *Region, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  return Out << "\"region\": \"" << Region << "\"";
}

static raw_ostream &printJson(const SymExpr *Symbol, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  return Out << "\"symbol\": \"" << Symbol << "\"";
}

static raw_ostream &printJson(const DynamicTypeInfo &DTI, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  Out << "\"dyn_type\": ";
  if (!DTI.isValid()) {
    Out << "null";
  } else {
    QualType ToPrint = DTI.getType();
    if (ToPrint->isAnyPointerType())
      ToPrint = ToPrint->getPointeeType();

    Out << '\"' << ToPrint << "\", \"sub_classable\": "
        << (DTI.canBeASubClass() ? "true" : "false");
  }
  return Out;
}

static raw_ostream &printJson(const DynamicCastInfo &DCI, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  return Out << "\"from\": \"" << DCI.from() << "\", \"to\": \"" << DCI.to()
             << "\", \"kind\": \"" << (DCI.succeeds() ? "success" : "fail")
             << "\"";
}

template <class T, class U>
static raw_ostream &printJson(const std::pair<T, U> &Pair, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  printJson(Pair.first, Out, NL, Space, IsDot) << ", ";
  return printJson(Pair.second, Out, NL, Space, IsDot);
}

template <class ContainerTy>
static raw_ostream &printJsonContainer(const ContainerTy &Container,
                                       raw_ostream &Out, const char *NL,
                                       unsigned int Space, bool IsDot) {
  if (Container.isEmpty()) {
    return Out << "null";
  }

  ++Space;
  Out << '[' << NL;
  for (auto I = Container.begin(); I != Container.end(); ++I) {
    const auto &Element = *I;

    Indent(Out, Space, IsDot) << "{ ";
    printJson(Element, Out, NL, Space, IsDot) << " }";

    if (std::next(I) != Container.end())
      Out << ',';
    Out << NL;
  }

  --Space;
  return Indent(Out, Space, IsDot) << "]";
}

static raw_ostream &printJson(const CastSet &Set, raw_ostream &Out,
                              const char *NL, unsigned int Space, bool IsDot) {
  Out << "\"casts\": ";
  return printJsonContainer(Set, Out, NL, Space, IsDot);
}

template <class MapTy>
static void printJsonImpl(raw_ostream &Out, ProgramStateRef State,
                          const char *Name, const char *NL, unsigned int Space,
                          bool IsDot, bool PrintEvenIfEmpty = true) {
  const auto &Map = State->get<MapTy>();
  if (Map.isEmpty() && !PrintEvenIfEmpty)
    return;

  Indent(Out, Space, IsDot) << "\"" << Name << "\": ";
  printJsonContainer(Map, Out, NL, Space, IsDot) << "," << NL;
}

static void printDynamicTypesJson(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, unsigned int Space,
                                  bool IsDot) {
  printJsonImpl<DynamicTypeMap>(Out, State, "dynamic_types", NL, Space, IsDot);
}

static void printDynamicCastsJson(raw_ostream &Out, ProgramStateRef State,
                                  const char *NL, unsigned int Space,
                                  bool IsDot) {
  printJsonImpl<DynamicCastMap>(Out, State, "dynamic_casts", NL, Space, IsDot);
}

static void printClassObjectDynamicTypesJson(raw_ostream &Out,
                                             ProgramStateRef State,
                                             const char *NL, unsigned int Space,
                                             bool IsDot) {
  // Let's print Class object type information only if we have something
  // meaningful to print.
  printJsonImpl<DynamicClassObjectMap>(Out, State, "class_object_types", NL,
                                       Space, IsDot,
                                       /*PrintEvenIfEmpty=*/false);
}

void printDynamicTypeInfoJson(raw_ostream &Out, ProgramStateRef State,
                              const char *NL, unsigned int Space, bool IsDot) {
  printDynamicTypesJson(Out, State, NL, Space, IsDot);
  printDynamicCastsJson(Out, State, NL, Space, IsDot);
  printClassObjectDynamicTypesJson(Out, State, NL, Space, IsDot);
}

} // namespace ento
} // namespace clang
