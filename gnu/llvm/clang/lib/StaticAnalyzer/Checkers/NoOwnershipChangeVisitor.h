//===--------------------------------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"

namespace clang {
namespace ento {

class NoOwnershipChangeVisitor : public NoStateChangeFuncVisitor {
protected:
  // The symbol whose (lack of) ownership change we are interested in.
  SymbolRef Sym;
  const CheckerBase &Checker;

  LLVM_DUMP_METHOD static std::string
  getFunctionName(const ExplodedNode *CallEnterN);

  /// Heuristically guess whether the callee intended to free the resource. This
  /// is done syntactically, because we are trying to argue about alternative
  /// paths of execution, and as a consequence we don't have path-sensitive
  /// information.
  virtual bool doesFnIntendToHandleOwnership(const Decl *Callee,
                                             ASTContext &ACtx) = 0;

  virtual bool hasResourceStateChanged(ProgramStateRef CallEnterState,
                                       ProgramStateRef CallExitEndState) = 0;

  bool wasModifiedInFunction(const ExplodedNode *CallEnterN,
                             const ExplodedNode *CallExitEndN) final;

  virtual PathDiagnosticPieceRef emitNote(const ExplodedNode *N) = 0;

  PathDiagnosticPieceRef maybeEmitNoteForObjCSelf(PathSensitiveBugReport &R,
                                                  const ObjCMethodCall &Call,
                                                  const ExplodedNode *N) final {
    // TODO: Implement.
    return nullptr;
  }

  PathDiagnosticPieceRef maybeEmitNoteForCXXThis(PathSensitiveBugReport &R,
                                                 const CXXConstructorCall &Call,
                                                 const ExplodedNode *N) final {
    // TODO: Implement.
    return nullptr;
  }

  // Set this to final, effectively dispatch to emitNote.
  PathDiagnosticPieceRef
  maybeEmitNoteForParameters(PathSensitiveBugReport &R, const CallEvent &Call,
                             const ExplodedNode *N) final;

public:
  using OwnerSet = llvm::SmallPtrSet<const MemRegion *, 8>;

private:
  OwnerSet getOwnersAtNode(const ExplodedNode *N);

public:
  NoOwnershipChangeVisitor(SymbolRef Sym, const CheckerBase *Checker)
      : NoStateChangeFuncVisitor(bugreporter::TrackingKind::Thorough), Sym(Sym),
        Checker(*Checker) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int Tag = 0;
    ID.AddPointer(&Tag);
    ID.AddPointer(Sym);
  }
};
} // namespace ento
} // namespace clang
