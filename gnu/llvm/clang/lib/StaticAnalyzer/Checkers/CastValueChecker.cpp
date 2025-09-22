//===- CastValueChecker - Model implementation of custom RTTIs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This defines CastValueChecker which models casts of custom RTTIs.
//
// TODO list:
// - It only allows one succesful cast between two types however in the wild
//   the object could be casted to multiple types.
// - It needs to check the most likely type information from the dynamic type
//   map to increase precision of dynamic casting.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclTemplate.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include <optional>
#include <utility>

using namespace clang;
using namespace ento;

namespace {
class CastValueChecker : public Checker<check::DeadSymbols, eval::Call> {
  enum class CallKind { Function, Method, InstanceOf };

  using CastCheck =
      std::function<void(const CastValueChecker *, const CallEvent &Call,
                         DefinedOrUnknownSVal, CheckerContext &)>;

public:
  // We have five cases to evaluate a cast:
  // 1) The parameter is non-null, the return value is non-null.
  // 2) The parameter is non-null, the return value is null.
  // 3) The parameter is null, the return value is null.
  // cast: 1;  dyn_cast: 1, 2;  cast_or_null: 1, 3;  dyn_cast_or_null: 1, 2, 3.
  //
  // 4) castAs: Has no parameter, the return value is non-null.
  // 5) getAs:  Has no parameter, the return value is null or non-null.
  //
  // We have two cases to check the parameter is an instance of the given type.
  // 1) isa:             The parameter is non-null, returns boolean.
  // 2) isa_and_nonnull: The parameter is null or non-null, returns boolean.
  bool evalCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;

private:
  // These are known in the LLVM project. The pairs are in the following form:
  // {{match-mode, {namespace, call}, argument-count}, {callback, kind}}
  const CallDescriptionMap<std::pair<CastCheck, CallKind>> CDM = {
      {{CDM::SimpleFunc, {"llvm", "cast"}, 1},
       {&CastValueChecker::evalCast, CallKind::Function}},
      {{CDM::SimpleFunc, {"llvm", "dyn_cast"}, 1},
       {&CastValueChecker::evalDynCast, CallKind::Function}},
      {{CDM::SimpleFunc, {"llvm", "cast_or_null"}, 1},
       {&CastValueChecker::evalCastOrNull, CallKind::Function}},
      {{CDM::SimpleFunc, {"llvm", "dyn_cast_or_null"}, 1},
       {&CastValueChecker::evalDynCastOrNull, CallKind::Function}},
      {{CDM::CXXMethod, {"clang", "castAs"}, 0},
       {&CastValueChecker::evalCastAs, CallKind::Method}},
      {{CDM::CXXMethod, {"clang", "getAs"}, 0},
       {&CastValueChecker::evalGetAs, CallKind::Method}},
      {{CDM::SimpleFunc, {"llvm", "isa"}, 1},
       {&CastValueChecker::evalIsa, CallKind::InstanceOf}},
      {{CDM::SimpleFunc, {"llvm", "isa_and_nonnull"}, 1},
       {&CastValueChecker::evalIsaAndNonNull, CallKind::InstanceOf}}};

  void evalCast(const CallEvent &Call, DefinedOrUnknownSVal DV,
                CheckerContext &C) const;
  void evalDynCast(const CallEvent &Call, DefinedOrUnknownSVal DV,
                   CheckerContext &C) const;
  void evalCastOrNull(const CallEvent &Call, DefinedOrUnknownSVal DV,
                      CheckerContext &C) const;
  void evalDynCastOrNull(const CallEvent &Call, DefinedOrUnknownSVal DV,
                         CheckerContext &C) const;
  void evalCastAs(const CallEvent &Call, DefinedOrUnknownSVal DV,
                  CheckerContext &C) const;
  void evalGetAs(const CallEvent &Call, DefinedOrUnknownSVal DV,
                 CheckerContext &C) const;
  void evalIsa(const CallEvent &Call, DefinedOrUnknownSVal DV,
               CheckerContext &C) const;
  void evalIsaAndNonNull(const CallEvent &Call, DefinedOrUnknownSVal DV,
                         CheckerContext &C) const;
};
} // namespace

static bool isInfeasibleCast(const DynamicCastInfo *CastInfo,
                             bool CastSucceeds) {
  if (!CastInfo)
    return false;

  return CastSucceeds ? CastInfo->fails() : CastInfo->succeeds();
}

static const NoteTag *getNoteTag(CheckerContext &C,
                                 const DynamicCastInfo *CastInfo,
                                 QualType CastToTy, const Expr *Object,
                                 bool CastSucceeds, bool IsKnownCast) {
  std::string CastToName =
      CastInfo ? CastInfo->to()->getAsCXXRecordDecl()->getNameAsString()
               : CastToTy.getAsString();
  Object = Object->IgnoreParenImpCasts();

  return C.getNoteTag(
      [=]() -> std::string {
        SmallString<128> Msg;
        llvm::raw_svector_ostream Out(Msg);

        if (!IsKnownCast)
          Out << "Assuming ";

        if (const auto *DRE = dyn_cast<DeclRefExpr>(Object)) {
          Out << '\'' << DRE->getDecl()->getDeclName() << '\'';
        } else if (const auto *ME = dyn_cast<MemberExpr>(Object)) {
          Out << (IsKnownCast ? "Field '" : "field '")
              << ME->getMemberDecl()->getDeclName() << '\'';
        } else {
          Out << (IsKnownCast ? "The object" : "the object");
        }

        Out << ' ' << (CastSucceeds ? "is a" : "is not a") << " '" << CastToName
            << '\'';

        return std::string(Out.str());
      },
      /*IsPrunable=*/true);
}

static const NoteTag *getNoteTag(CheckerContext &C,
                                 SmallVector<QualType, 4> CastToTyVec,
                                 const Expr *Object,
                                 bool IsKnownCast) {
  Object = Object->IgnoreParenImpCasts();

  return C.getNoteTag(
      [=]() -> std::string {
        SmallString<128> Msg;
        llvm::raw_svector_ostream Out(Msg);

        if (!IsKnownCast)
          Out << "Assuming ";

        if (const auto *DRE = dyn_cast<DeclRefExpr>(Object)) {
          Out << '\'' << DRE->getDecl()->getNameAsString() << '\'';
        } else if (const auto *ME = dyn_cast<MemberExpr>(Object)) {
          Out << (IsKnownCast ? "Field '" : "field '")
              << ME->getMemberDecl()->getNameAsString() << '\'';
        } else {
          Out << (IsKnownCast ? "The object" : "the object");
        }
        Out << " is";

        bool First = true;
        for (QualType CastToTy: CastToTyVec) {
          std::string CastToName =
              CastToTy->getAsCXXRecordDecl()
                  ? CastToTy->getAsCXXRecordDecl()->getNameAsString()
                  : CastToTy.getAsString();
          Out << ' ' << ((CastToTyVec.size() == 1) ? "not" :
                         (First ? "neither" : "nor")) << " a '" << CastToName
              << '\'';
          First = false;
        }

        return std::string(Out.str());
      },
      /*IsPrunable=*/true);
}

//===----------------------------------------------------------------------===//
// Main logic to evaluate a cast.
//===----------------------------------------------------------------------===//

static QualType alignReferenceTypes(QualType toAlign, QualType alignTowards,
                                    ASTContext &ACtx) {
  if (alignTowards->isLValueReferenceType() &&
      alignTowards.isConstQualified()) {
    toAlign.addConst();
    return ACtx.getLValueReferenceType(toAlign);
  } else if (alignTowards->isLValueReferenceType())
    return ACtx.getLValueReferenceType(toAlign);
  else if (alignTowards->isRValueReferenceType())
    return ACtx.getRValueReferenceType(toAlign);

  llvm_unreachable("Must align towards a reference type!");
}

static void addCastTransition(const CallEvent &Call, DefinedOrUnknownSVal DV,
                              CheckerContext &C, bool IsNonNullParam,
                              bool IsNonNullReturn,
                              bool IsCheckedCast = false) {
  ProgramStateRef State = C.getState()->assume(DV, IsNonNullParam);
  if (!State)
    return;

  const Expr *Object;
  QualType CastFromTy;
  QualType CastToTy = Call.getResultType();

  if (Call.getNumArgs() > 0) {
    Object = Call.getArgExpr(0);
    CastFromTy = Call.parameters()[0]->getType();
  } else {
    Object = cast<CXXInstanceCall>(&Call)->getCXXThisExpr();
    CastFromTy = Object->getType();
    if (CastToTy->isPointerType()) {
      if (!CastFromTy->isPointerType())
        return;
    } else {
      if (!CastFromTy->isReferenceType())
        return;

      CastFromTy = alignReferenceTypes(CastFromTy, CastToTy, C.getASTContext());
    }
  }

  const MemRegion *MR = DV.getAsRegion();
  const DynamicCastInfo *CastInfo =
      getDynamicCastInfo(State, MR, CastFromTy, CastToTy);

  // We assume that every checked cast succeeds.
  bool CastSucceeds = IsCheckedCast || CastFromTy == CastToTy;
  if (!CastSucceeds) {
    if (CastInfo)
      CastSucceeds = IsNonNullReturn && CastInfo->succeeds();
    else
      CastSucceeds = IsNonNullReturn;
  }

  // Check for infeasible casts.
  if (isInfeasibleCast(CastInfo, CastSucceeds)) {
    C.generateSink(State, C.getPredecessor());
    return;
  }

  // Store the type and the cast information.
  bool IsKnownCast = CastInfo || IsCheckedCast || CastFromTy == CastToTy;
  if (!IsKnownCast || IsCheckedCast)
    State = setDynamicTypeAndCastInfo(State, MR, CastFromTy, CastToTy,
                                      CastSucceeds);

  SVal V = CastSucceeds ? C.getSValBuilder().evalCast(DV, CastToTy, CastFromTy)
                        : C.getSValBuilder().makeNullWithType(CastToTy);
  C.addTransition(
      State->BindExpr(Call.getOriginExpr(), C.getLocationContext(), V, false),
      getNoteTag(C, CastInfo, CastToTy, Object, CastSucceeds, IsKnownCast));
}

static void addInstanceOfTransition(const CallEvent &Call,
                                    DefinedOrUnknownSVal DV,
                                    ProgramStateRef State, CheckerContext &C,
                                    bool IsInstanceOf) {
  const FunctionDecl *FD = Call.getDecl()->getAsFunction();
  QualType CastFromTy = Call.parameters()[0]->getType();
  SmallVector<QualType, 4> CastToTyVec;
  for (unsigned idx = 0; idx < FD->getTemplateSpecializationArgs()->size() - 1;
       ++idx) {
    TemplateArgument CastToTempArg =
      FD->getTemplateSpecializationArgs()->get(idx);
    switch (CastToTempArg.getKind()) {
    default:
      return;
    case TemplateArgument::Type:
      CastToTyVec.push_back(CastToTempArg.getAsType());
      break;
    case TemplateArgument::Pack:
      for (TemplateArgument ArgInPack: CastToTempArg.pack_elements())
        CastToTyVec.push_back(ArgInPack.getAsType());
      break;
    }
  }

  const MemRegion *MR = DV.getAsRegion();
  if (MR && CastFromTy->isReferenceType())
    MR = State->getSVal(DV.castAs<Loc>()).getAsRegion();

  bool Success = false;
  bool IsAnyKnown = false;
  for (QualType CastToTy: CastToTyVec) {
    if (CastFromTy->isPointerType())
      CastToTy = C.getASTContext().getPointerType(CastToTy);
    else if (CastFromTy->isReferenceType())
      CastToTy = alignReferenceTypes(CastToTy, CastFromTy, C.getASTContext());
    else
      return;

    const DynamicCastInfo *CastInfo =
      getDynamicCastInfo(State, MR, CastFromTy, CastToTy);

    bool CastSucceeds;
    if (CastInfo)
      CastSucceeds = IsInstanceOf && CastInfo->succeeds();
    else
      CastSucceeds = IsInstanceOf || CastFromTy == CastToTy;

    // Store the type and the cast information.
    bool IsKnownCast = CastInfo || CastFromTy == CastToTy;
    IsAnyKnown = IsAnyKnown || IsKnownCast;
    ProgramStateRef NewState = State;
    if (!IsKnownCast)
      NewState = setDynamicTypeAndCastInfo(State, MR, CastFromTy, CastToTy,
                                           IsInstanceOf);

    if (CastSucceeds) {
      Success = true;
      C.addTransition(
          NewState->BindExpr(Call.getOriginExpr(), C.getLocationContext(),
                             C.getSValBuilder().makeTruthVal(true)),
          getNoteTag(C, CastInfo, CastToTy, Call.getArgExpr(0), true,
                     IsKnownCast));
      if (IsKnownCast)
        return;
    } else if (CastInfo && CastInfo->succeeds()) {
      C.generateSink(NewState, C.getPredecessor());
      return;
    }
  }

  if (!Success) {
    C.addTransition(
        State->BindExpr(Call.getOriginExpr(), C.getLocationContext(),
                        C.getSValBuilder().makeTruthVal(false)),
        getNoteTag(C, CastToTyVec, Call.getArgExpr(0), IsAnyKnown));
  }
}

//===----------------------------------------------------------------------===//
// Evaluating cast, dyn_cast, cast_or_null, dyn_cast_or_null.
//===----------------------------------------------------------------------===//

static void evalNonNullParamNonNullReturn(const CallEvent &Call,
                                          DefinedOrUnknownSVal DV,
                                          CheckerContext &C,
                                          bool IsCheckedCast = false) {
  addCastTransition(Call, DV, C, /*IsNonNullParam=*/true,
                    /*IsNonNullReturn=*/true, IsCheckedCast);
}

static void evalNonNullParamNullReturn(const CallEvent &Call,
                                       DefinedOrUnknownSVal DV,
                                       CheckerContext &C) {
  addCastTransition(Call, DV, C, /*IsNonNullParam=*/true,
                    /*IsNonNullReturn=*/false);
}

static void evalNullParamNullReturn(const CallEvent &Call,
                                    DefinedOrUnknownSVal DV,
                                    CheckerContext &C) {
  if (ProgramStateRef State = C.getState()->assume(DV, false))
    C.addTransition(State->BindExpr(Call.getOriginExpr(),
                                    C.getLocationContext(),
                                    C.getSValBuilder().makeNullWithType(
                                        Call.getOriginExpr()->getType()),
                                    false),
                    C.getNoteTag("Assuming null pointer is passed into cast",
                                 /*IsPrunable=*/true));
}

void CastValueChecker::evalCast(const CallEvent &Call, DefinedOrUnknownSVal DV,
                                CheckerContext &C) const {
  evalNonNullParamNonNullReturn(Call, DV, C, /*IsCheckedCast=*/true);
}

void CastValueChecker::evalDynCast(const CallEvent &Call,
                                   DefinedOrUnknownSVal DV,
                                   CheckerContext &C) const {
  evalNonNullParamNonNullReturn(Call, DV, C);
  evalNonNullParamNullReturn(Call, DV, C);
}

void CastValueChecker::evalCastOrNull(const CallEvent &Call,
                                      DefinedOrUnknownSVal DV,
                                      CheckerContext &C) const {
  evalNonNullParamNonNullReturn(Call, DV, C);
  evalNullParamNullReturn(Call, DV, C);
}

void CastValueChecker::evalDynCastOrNull(const CallEvent &Call,
                                         DefinedOrUnknownSVal DV,
                                         CheckerContext &C) const {
  evalNonNullParamNonNullReturn(Call, DV, C);
  evalNonNullParamNullReturn(Call, DV, C);
  evalNullParamNullReturn(Call, DV, C);
}

//===----------------------------------------------------------------------===//
// Evaluating castAs, getAs.
//===----------------------------------------------------------------------===//

static void evalZeroParamNonNullReturn(const CallEvent &Call,
                                       DefinedOrUnknownSVal DV,
                                       CheckerContext &C,
                                       bool IsCheckedCast = false) {
  addCastTransition(Call, DV, C, /*IsNonNullParam=*/true,
                    /*IsNonNullReturn=*/true, IsCheckedCast);
}

static void evalZeroParamNullReturn(const CallEvent &Call,
                                    DefinedOrUnknownSVal DV,
                                    CheckerContext &C) {
  addCastTransition(Call, DV, C, /*IsNonNullParam=*/true,
                    /*IsNonNullReturn=*/false);
}

void CastValueChecker::evalCastAs(const CallEvent &Call,
                                  DefinedOrUnknownSVal DV,
                                  CheckerContext &C) const {
  evalZeroParamNonNullReturn(Call, DV, C, /*IsCheckedCast=*/true);
}

void CastValueChecker::evalGetAs(const CallEvent &Call, DefinedOrUnknownSVal DV,
                                 CheckerContext &C) const {
  evalZeroParamNonNullReturn(Call, DV, C);
  evalZeroParamNullReturn(Call, DV, C);
}

//===----------------------------------------------------------------------===//
// Evaluating isa, isa_and_nonnull.
//===----------------------------------------------------------------------===//

void CastValueChecker::evalIsa(const CallEvent &Call, DefinedOrUnknownSVal DV,
                               CheckerContext &C) const {
  ProgramStateRef NonNullState, NullState;
  std::tie(NonNullState, NullState) = C.getState()->assume(DV);

  if (NonNullState) {
    addInstanceOfTransition(Call, DV, NonNullState, C, /*IsInstanceOf=*/true);
    addInstanceOfTransition(Call, DV, NonNullState, C, /*IsInstanceOf=*/false);
  }

  if (NullState) {
    C.generateSink(NullState, C.getPredecessor());
  }
}

void CastValueChecker::evalIsaAndNonNull(const CallEvent &Call,
                                         DefinedOrUnknownSVal DV,
                                         CheckerContext &C) const {
  ProgramStateRef NonNullState, NullState;
  std::tie(NonNullState, NullState) = C.getState()->assume(DV);

  if (NonNullState) {
    addInstanceOfTransition(Call, DV, NonNullState, C, /*IsInstanceOf=*/true);
    addInstanceOfTransition(Call, DV, NonNullState, C, /*IsInstanceOf=*/false);
  }

  if (NullState) {
    addInstanceOfTransition(Call, DV, NullState, C, /*IsInstanceOf=*/false);
  }
}

//===----------------------------------------------------------------------===//
// Main logic to evaluate a call.
//===----------------------------------------------------------------------===//

bool CastValueChecker::evalCall(const CallEvent &Call,
                                CheckerContext &C) const {
  const auto *Lookup = CDM.lookup(Call);
  if (!Lookup)
    return false;

  const CastCheck &Check = Lookup->first;
  CallKind Kind = Lookup->second;

  std::optional<DefinedOrUnknownSVal> DV;

  switch (Kind) {
  case CallKind::Function: {
    // We only model casts from pointers to pointers or from references
    // to references. Other casts are most likely specialized and we
    // cannot model them.
    QualType ParamT = Call.parameters()[0]->getType();
    QualType ResultT = Call.getResultType();
    if (!(ParamT->isPointerType() && ResultT->isPointerType()) &&
        !(ParamT->isReferenceType() && ResultT->isReferenceType())) {
      return false;
    }

    DV = Call.getArgSVal(0).getAs<DefinedOrUnknownSVal>();
    break;
  }
  case CallKind::InstanceOf: {
    // We need to obtain the only template argument to determinte the type.
    const FunctionDecl *FD = Call.getDecl()->getAsFunction();
    if (!FD || !FD->getTemplateSpecializationArgs())
      return false;

    DV = Call.getArgSVal(0).getAs<DefinedOrUnknownSVal>();
    break;
  }
  case CallKind::Method:
    const auto *InstanceCall = dyn_cast<CXXInstanceCall>(&Call);
    if (!InstanceCall)
      return false;

    DV = InstanceCall->getCXXThisVal().getAs<DefinedOrUnknownSVal>();
    break;
  }

  if (!DV)
    return false;

  Check(this, Call, *DV, C);
  return true;
}

void CastValueChecker::checkDeadSymbols(SymbolReaper &SR,
                                        CheckerContext &C) const {
  C.addTransition(removeDeadCasts(C.getState(), SR));
}

void ento::registerCastValueChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<CastValueChecker>();
}

bool ento::shouldRegisterCastValueChecker(const CheckerManager &mgr) {
  return true;
}
