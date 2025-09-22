//===-- TransEmptyStatementsAndDealloc.cpp - Transformations to ARC mode --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// removeEmptyStatementsAndDealloc:
//
// Removes empty statements that are leftovers from previous transformations.
// e.g for
//
//  [x retain];
//
// removeRetainReleaseDealloc will leave an empty ";" that removeEmptyStatements
// will remove.
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

static bool isEmptyARCMTMacroStatement(NullStmt *S,
                                       std::vector<SourceLocation> &MacroLocs,
                                       ASTContext &Ctx) {
  if (!S->hasLeadingEmptyMacro())
    return false;

  SourceLocation SemiLoc = S->getSemiLoc();
  if (SemiLoc.isInvalid() || SemiLoc.isMacroID())
    return false;

  if (MacroLocs.empty())
    return false;

  SourceManager &SM = Ctx.getSourceManager();
  std::vector<SourceLocation>::iterator I = llvm::upper_bound(
      MacroLocs, SemiLoc, BeforeThanCompare<SourceLocation>(SM));
  --I;
  SourceLocation
      AfterMacroLoc = I->getLocWithOffset(getARCMTMacroName().size());
  assert(AfterMacroLoc.isFileID());

  if (AfterMacroLoc == SemiLoc)
    return true;

  SourceLocation::IntTy RelOffs = 0;
  if (!SM.isInSameSLocAddrSpace(AfterMacroLoc, SemiLoc, &RelOffs))
    return false;
  if (RelOffs < 0)
    return false;

  // We make the reasonable assumption that a semicolon after 100 characters
  // means that it is not the next token after our macro. If this assumption
  // fails it is not critical, we will just fail to clear out, e.g., an empty
  // 'if'.
  if (RelOffs - getARCMTMacroName().size() > 100)
    return false;

  SourceLocation AfterMacroSemiLoc = findSemiAfterLocation(AfterMacroLoc, Ctx);
  return AfterMacroSemiLoc == SemiLoc;
}

namespace {

/// Returns true if the statement became empty due to previous
/// transformations.
class EmptyChecker : public StmtVisitor<EmptyChecker, bool> {
  ASTContext &Ctx;
  std::vector<SourceLocation> &MacroLocs;

public:
  EmptyChecker(ASTContext &ctx, std::vector<SourceLocation> &macroLocs)
    : Ctx(ctx), MacroLocs(macroLocs) { }

  bool VisitNullStmt(NullStmt *S) {
    return isEmptyARCMTMacroStatement(S, MacroLocs, Ctx);
  }
  bool VisitCompoundStmt(CompoundStmt *S) {
    if (S->body_empty())
      return false; // was already empty, not because of transformations.
    for (auto *I : S->body())
      if (!Visit(I))
        return false;
    return true;
  }
  bool VisitIfStmt(IfStmt *S) {
    if (S->getConditionVariable())
      return false;
    Expr *condE = S->getCond();
    if (!condE)
      return false;
    if (hasSideEffects(condE, Ctx))
      return false;
    if (!S->getThen() || !Visit(S->getThen()))
      return false;
    return !S->getElse() || Visit(S->getElse());
  }
  bool VisitWhileStmt(WhileStmt *S) {
    if (S->getConditionVariable())
      return false;
    Expr *condE = S->getCond();
    if (!condE)
      return false;
    if (hasSideEffects(condE, Ctx))
      return false;
    if (!S->getBody())
      return false;
    return Visit(S->getBody());
  }
  bool VisitDoStmt(DoStmt *S) {
    Expr *condE = S->getCond();
    if (!condE)
      return false;
    if (hasSideEffects(condE, Ctx))
      return false;
    if (!S->getBody())
      return false;
    return Visit(S->getBody());
  }
  bool VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
    Expr *Exp = S->getCollection();
    if (!Exp)
      return false;
    if (hasSideEffects(Exp, Ctx))
      return false;
    if (!S->getBody())
      return false;
    return Visit(S->getBody());
  }
  bool VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
    if (!S->getSubStmt())
      return false;
    return Visit(S->getSubStmt());
  }
};

class EmptyStatementsRemover :
                            public RecursiveASTVisitor<EmptyStatementsRemover> {
  MigrationPass &Pass;

public:
  EmptyStatementsRemover(MigrationPass &pass) : Pass(pass) { }

  bool TraverseStmtExpr(StmtExpr *E) {
    CompoundStmt *S = E->getSubStmt();
    for (CompoundStmt::body_iterator
           I = S->body_begin(), E = S->body_end(); I != E; ++I) {
      if (I != E - 1)
        check(*I);
      TraverseStmt(*I);
    }
    return true;
  }

  bool VisitCompoundStmt(CompoundStmt *S) {
    for (auto *I : S->body())
      check(I);
    return true;
  }

  ASTContext &getContext() { return Pass.Ctx; }

private:
  void check(Stmt *S) {
    if (!S) return;
    if (EmptyChecker(Pass.Ctx, Pass.ARCMTMacroLocs).Visit(S)) {
      Transaction Trans(Pass.TA);
      Pass.TA.removeStmt(S);
    }
  }
};

} // anonymous namespace

static bool isBodyEmpty(CompoundStmt *body, ASTContext &Ctx,
                        std::vector<SourceLocation> &MacroLocs) {
  for (auto *I : body->body())
    if (!EmptyChecker(Ctx, MacroLocs).Visit(I))
      return false;

  return true;
}

static void cleanupDeallocOrFinalize(MigrationPass &pass) {
  ASTContext &Ctx = pass.Ctx;
  TransformActions &TA = pass.TA;
  DeclContext *DC = Ctx.getTranslationUnitDecl();
  Selector FinalizeSel =
      Ctx.Selectors.getNullarySelector(&pass.Ctx.Idents.get("finalize"));

  typedef DeclContext::specific_decl_iterator<ObjCImplementationDecl>
    impl_iterator;
  for (impl_iterator I = impl_iterator(DC->decls_begin()),
                     E = impl_iterator(DC->decls_end()); I != E; ++I) {
    ObjCMethodDecl *DeallocM = nullptr;
    ObjCMethodDecl *FinalizeM = nullptr;
    for (auto *MD : I->instance_methods()) {
      if (!MD->hasBody())
        continue;

      if (MD->getMethodFamily() == OMF_dealloc) {
        DeallocM = MD;
      } else if (MD->isInstanceMethod() && MD->getSelector() == FinalizeSel) {
        FinalizeM = MD;
      }
    }

    if (DeallocM) {
      if (isBodyEmpty(DeallocM->getCompoundBody(), Ctx, pass.ARCMTMacroLocs)) {
        Transaction Trans(TA);
        TA.remove(DeallocM->getSourceRange());
      }

      if (FinalizeM) {
        Transaction Trans(TA);
        TA.remove(FinalizeM->getSourceRange());
      }

    } else if (FinalizeM) {
      if (isBodyEmpty(FinalizeM->getCompoundBody(), Ctx, pass.ARCMTMacroLocs)) {
        Transaction Trans(TA);
        TA.remove(FinalizeM->getSourceRange());
      } else {
        Transaction Trans(TA);
        TA.replaceText(FinalizeM->getSelectorStartLoc(), "finalize", "dealloc");
      }
    }
  }
}

void trans::removeEmptyStatementsAndDeallocFinalize(MigrationPass &pass) {
  EmptyStatementsRemover(pass).TraverseDecl(pass.Ctx.getTranslationUnitDecl());

  cleanupDeallocOrFinalize(pass);

  for (unsigned i = 0, e = pass.ARCMTMacroLocs.size(); i != e; ++i) {
    Transaction Trans(pass.TA);
    pass.TA.remove(pass.ARCMTMacroLocs[i]);
  }
}
