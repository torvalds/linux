//== DynamicTypeChecker.cpp ------------------------------------ -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker looks for cases where the dynamic type of an object is unrelated
// to its static type. The type information utilized by this check is collected
// by the DynamicTypePropagation checker. This check does not report any type
// error for ObjC Generic types, in order to avoid duplicate erros from the
// ObjC Generics checker. This checker is not supposed to modify the program
// state, it is just the observer of the type information provided by other
// checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

namespace {
class DynamicTypeChecker : public Checker<check::PostStmt<ImplicitCastExpr>> {
  const BugType BT{this, "Dynamic and static type mismatch", "Type Error"};

  class DynamicTypeBugVisitor : public BugReporterVisitor {
  public:
    DynamicTypeBugVisitor(const MemRegion *Reg) : Reg(Reg) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Reg);
    }

    PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                     BugReporterContext &BRC,
                                     PathSensitiveBugReport &BR) override;

  private:
    // The tracked region.
    const MemRegion *Reg;
  };

  void reportTypeError(QualType DynamicType, QualType StaticType,
                       const MemRegion *Reg, const Stmt *ReportedNode,
                       CheckerContext &C) const;

public:
  void checkPostStmt(const ImplicitCastExpr *CE, CheckerContext &C) const;
};
}

void DynamicTypeChecker::reportTypeError(QualType DynamicType,
                                         QualType StaticType,
                                         const MemRegion *Reg,
                                         const Stmt *ReportedNode,
                                         CheckerContext &C) const {
  SmallString<192> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << "Object has a dynamic type '";
  QualType::print(DynamicType.getTypePtr(), Qualifiers(), OS, C.getLangOpts(),
                  llvm::Twine());
  OS << "' which is incompatible with static type '";
  QualType::print(StaticType.getTypePtr(), Qualifiers(), OS, C.getLangOpts(),
                  llvm::Twine());
  OS << "'";
  auto R = std::make_unique<PathSensitiveBugReport>(
      BT, OS.str(), C.generateNonFatalErrorNode());
  R->markInteresting(Reg);
  R->addVisitor(std::make_unique<DynamicTypeBugVisitor>(Reg));
  R->addRange(ReportedNode->getSourceRange());
  C.emitReport(std::move(R));
}

PathDiagnosticPieceRef DynamicTypeChecker::DynamicTypeBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC, PathSensitiveBugReport &) {
  ProgramStateRef State = N->getState();
  ProgramStateRef StatePrev = N->getFirstPred()->getState();

  DynamicTypeInfo TrackedType = getDynamicTypeInfo(State, Reg);
  DynamicTypeInfo TrackedTypePrev = getDynamicTypeInfo(StatePrev, Reg);
  if (!TrackedType.isValid())
    return nullptr;

  if (TrackedTypePrev.isValid() &&
      TrackedTypePrev.getType() == TrackedType.getType())
    return nullptr;

  // Retrieve the associated statement.
  const Stmt *S = N->getStmtForDiagnostics();
  if (!S)
    return nullptr;

  const LangOptions &LangOpts = BRC.getASTContext().getLangOpts();

  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << "Type '";
  QualType::print(TrackedType.getType().getTypePtr(), Qualifiers(), OS,
                  LangOpts, llvm::Twine());
  OS << "' is inferred from ";

  if (const auto *ExplicitCast = dyn_cast<ExplicitCastExpr>(S)) {
    OS << "explicit cast (from '";
    QualType::print(ExplicitCast->getSubExpr()->getType().getTypePtr(),
                    Qualifiers(), OS, LangOpts, llvm::Twine());
    OS << "' to '";
    QualType::print(ExplicitCast->getType().getTypePtr(), Qualifiers(), OS,
                    LangOpts, llvm::Twine());
    OS << "')";
  } else if (const auto *ImplicitCast = dyn_cast<ImplicitCastExpr>(S)) {
    OS << "implicit cast (from '";
    QualType::print(ImplicitCast->getSubExpr()->getType().getTypePtr(),
                    Qualifiers(), OS, LangOpts, llvm::Twine());
    OS << "' to '";
    QualType::print(ImplicitCast->getType().getTypePtr(), Qualifiers(), OS,
                    LangOpts, llvm::Twine());
    OS << "')";
  } else {
    OS << "this context";
  }

  // Generate the extra diagnostic.
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, OS.str(), true);
}

static bool hasDefinition(const ObjCObjectPointerType *ObjPtr) {
  const ObjCInterfaceDecl *Decl = ObjPtr->getInterfaceDecl();
  if (!Decl)
    return false;

  return Decl->getDefinition();
}

// TODO: consider checking explicit casts?
void DynamicTypeChecker::checkPostStmt(const ImplicitCastExpr *CE,
                                       CheckerContext &C) const {
  // TODO: C++ support.
  if (CE->getCastKind() != CK_BitCast)
    return;

  const MemRegion *Region = C.getSVal(CE).getAsRegion();
  if (!Region)
    return;

  ProgramStateRef State = C.getState();
  DynamicTypeInfo DynTypeInfo = getDynamicTypeInfo(State, Region);

  if (!DynTypeInfo.isValid())
    return;

  QualType DynType = DynTypeInfo.getType();
  QualType StaticType = CE->getType();

  const auto *DynObjCType = DynType->getAs<ObjCObjectPointerType>();
  const auto *StaticObjCType = StaticType->getAs<ObjCObjectPointerType>();

  if (!DynObjCType || !StaticObjCType)
    return;

  if (!hasDefinition(DynObjCType) || !hasDefinition(StaticObjCType))
    return;

  ASTContext &ASTCtxt = C.getASTContext();

  // Strip kindeofness to correctly detect subtyping relationships.
  DynObjCType = DynObjCType->stripObjCKindOfTypeAndQuals(ASTCtxt);
  StaticObjCType = StaticObjCType->stripObjCKindOfTypeAndQuals(ASTCtxt);

  // Specialized objects are handled by the generics checker.
  if (StaticObjCType->isSpecialized())
    return;

  if (ASTCtxt.canAssignObjCInterfaces(StaticObjCType, DynObjCType))
    return;

  if (DynTypeInfo.canBeASubClass() &&
      ASTCtxt.canAssignObjCInterfaces(DynObjCType, StaticObjCType))
    return;

  reportTypeError(DynType, StaticType, Region, CE, C);
}

void ento::registerDynamicTypeChecker(CheckerManager &mgr) {
  mgr.registerChecker<DynamicTypeChecker>();
}

bool ento::shouldRegisterDynamicTypeChecker(const CheckerManager &mgr) {
  return true;
}
