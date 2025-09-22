//==- DeadStoresChecker.cpp - Check for stores to dead variables -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a DeadStores, a flow-sensitive checker that looks for
//  stores to variables that are no longer live.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace ento;

namespace {

/// A simple visitor to record what VarDecls occur in EH-handling code.
class EHCodeVisitor : public RecursiveASTVisitor<EHCodeVisitor> {
public:
  bool inEH;
  llvm::DenseSet<const VarDecl *> &S;

  bool TraverseObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
    SaveAndRestore inFinally(inEH, true);
    return ::RecursiveASTVisitor<EHCodeVisitor>::TraverseObjCAtFinallyStmt(S);
  }

  bool TraverseObjCAtCatchStmt(ObjCAtCatchStmt *S) {
    SaveAndRestore inCatch(inEH, true);
    return ::RecursiveASTVisitor<EHCodeVisitor>::TraverseObjCAtCatchStmt(S);
  }

  bool TraverseCXXCatchStmt(CXXCatchStmt *S) {
    SaveAndRestore inCatch(inEH, true);
    return TraverseStmt(S->getHandlerBlock());
  }

  bool VisitDeclRefExpr(DeclRefExpr *DR) {
    if (inEH)
      if (const VarDecl *D = dyn_cast<VarDecl>(DR->getDecl()))
        S.insert(D);
    return true;
  }

  EHCodeVisitor(llvm::DenseSet<const VarDecl *> &S) :
  inEH(false), S(S) {}
};

// FIXME: Eventually migrate into its own file, and have it managed by
// AnalysisManager.
class ReachableCode {
  const CFG &cfg;
  llvm::BitVector reachable;
public:
  ReachableCode(const CFG &cfg)
    : cfg(cfg), reachable(cfg.getNumBlockIDs(), false) {}

  void computeReachableBlocks();

  bool isReachable(const CFGBlock *block) const {
    return reachable[block->getBlockID()];
  }
};
}

void ReachableCode::computeReachableBlocks() {
  if (!cfg.getNumBlockIDs())
    return;

  SmallVector<const CFGBlock*, 10> worklist;
  worklist.push_back(&cfg.getEntry());

  while (!worklist.empty()) {
    const CFGBlock *block = worklist.pop_back_val();
    llvm::BitVector::reference isReachable = reachable[block->getBlockID()];
    if (isReachable)
      continue;
    isReachable = true;

    for (const CFGBlock *succ : block->succs())
      if (succ)
        worklist.push_back(succ);
  }
}

static const Expr *
LookThroughTransitiveAssignmentsAndCommaOperators(const Expr *Ex) {
  while (Ex) {
    Ex = Ex->IgnoreParenCasts();
    const BinaryOperator *BO = dyn_cast<BinaryOperator>(Ex);
    if (!BO)
      break;
    BinaryOperatorKind Op = BO->getOpcode();
    if (Op == BO_Assign || Op == BO_Comma) {
      Ex = BO->getRHS();
      continue;
    }
    break;
  }
  return Ex;
}

namespace {
class DeadStoresChecker : public Checker<check::ASTCodeBody> {
public:
  bool ShowFixIts = false;
  bool WarnForDeadNestedAssignments = true;

  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const;
};

class DeadStoreObs : public LiveVariables::Observer {
  const CFG &cfg;
  ASTContext &Ctx;
  BugReporter& BR;
  const DeadStoresChecker *Checker;
  AnalysisDeclContext* AC;
  ParentMap& Parents;
  llvm::SmallPtrSet<const VarDecl*, 20> Escaped;
  std::unique_ptr<ReachableCode> reachableCode;
  const CFGBlock *currentBlock;
  std::unique_ptr<llvm::DenseSet<const VarDecl *>> InEH;

  enum DeadStoreKind { Standard, Enclosing, DeadIncrement, DeadInit };

public:
  DeadStoreObs(const CFG &cfg, ASTContext &ctx, BugReporter &br,
               const DeadStoresChecker *checker, AnalysisDeclContext *ac,
               ParentMap &parents,
               llvm::SmallPtrSet<const VarDecl *, 20> &escaped,
               bool warnForDeadNestedAssignments)
      : cfg(cfg), Ctx(ctx), BR(br), Checker(checker), AC(ac), Parents(parents),
        Escaped(escaped), currentBlock(nullptr) {}

  ~DeadStoreObs() override {}

  bool isLive(const LiveVariables::LivenessValues &Live, const VarDecl *D) {
    if (Live.isLive(D))
      return true;
    // Lazily construct the set that records which VarDecls are in
    // EH code.
    if (!InEH.get()) {
      InEH.reset(new llvm::DenseSet<const VarDecl *>());
      EHCodeVisitor V(*InEH.get());
      V.TraverseStmt(AC->getBody());
    }
    // Treat all VarDecls that occur in EH code as being "always live"
    // when considering to suppress dead stores.  Frequently stores
    // are followed by reads in EH code, but we don't have the ability
    // to analyze that yet.
    return InEH->count(D);
  }

  bool isSuppressed(SourceRange R) {
    SourceManager &SMgr = Ctx.getSourceManager();
    SourceLocation Loc = R.getBegin();
    if (!Loc.isValid())
      return false;

    FileID FID = SMgr.getFileID(Loc);
    bool Invalid = false;
    StringRef Data = SMgr.getBufferData(FID, &Invalid);
    if (Invalid)
      return false;

    // Files autogenerated by DriverKit IIG contain some dead stores that
    // we don't want to report.
    if (Data.starts_with("/* iig"))
      return true;

    return false;
  }

  void Report(const VarDecl *V, DeadStoreKind dsk,
              PathDiagnosticLocation L, SourceRange R) {
    if (Escaped.count(V))
      return;

    // Compute reachable blocks within the CFG for trivial cases
    // where a bogus dead store can be reported because itself is unreachable.
    if (!reachableCode.get()) {
      reachableCode.reset(new ReachableCode(cfg));
      reachableCode->computeReachableBlocks();
    }

    if (!reachableCode->isReachable(currentBlock))
      return;

    if (isSuppressed(R))
      return;

    SmallString<64> buf;
    llvm::raw_svector_ostream os(buf);
    const char *BugType = nullptr;

    SmallVector<FixItHint, 1> Fixits;

    switch (dsk) {
      case DeadInit: {
        BugType = "Dead initialization";
        os << "Value stored to '" << *V
           << "' during its initialization is never read";

        ASTContext &ACtx = V->getASTContext();
        if (Checker->ShowFixIts) {
          if (V->getInit()->HasSideEffects(ACtx,
                                           /*IncludePossibleEffects=*/true)) {
            break;
          }
          SourceManager &SM = ACtx.getSourceManager();
          const LangOptions &LO = ACtx.getLangOpts();
          SourceLocation L1 =
              Lexer::findNextToken(
                  V->getTypeSourceInfo()->getTypeLoc().getEndLoc(),
                  SM, LO)->getEndLoc();
          SourceLocation L2 =
              Lexer::getLocForEndOfToken(V->getInit()->getEndLoc(), 1, SM, LO);
          Fixits.push_back(FixItHint::CreateRemoval({L1, L2}));
        }
        break;
      }

      case DeadIncrement:
        BugType = "Dead increment";
        [[fallthrough]];
      case Standard:
        if (!BugType) BugType = "Dead assignment";
        os << "Value stored to '" << *V << "' is never read";
        break;

      // eg.: f((x = foo()))
      case Enclosing:
        if (!Checker->WarnForDeadNestedAssignments)
          return;
        BugType = "Dead nested assignment";
        os << "Although the value stored to '" << *V
           << "' is used in the enclosing expression, the value is never "
              "actually read from '"
           << *V << "'";
        break;
    }

    BR.EmitBasicReport(AC->getDecl(), Checker, BugType, categories::UnusedCode,
                       os.str(), L, R, Fixits);
  }

  void CheckVarDecl(const VarDecl *VD, const Expr *Ex, const Expr *Val,
                    DeadStoreKind dsk,
                    const LiveVariables::LivenessValues &Live) {

    if (!VD->hasLocalStorage())
      return;
    // Reference types confuse the dead stores checker.  Skip them
    // for now.
    if (VD->getType()->getAs<ReferenceType>())
      return;

    if (!isLive(Live, VD) &&
        !(VD->hasAttr<UnusedAttr>() || VD->hasAttr<BlocksAttr>() ||
          VD->hasAttr<ObjCPreciseLifetimeAttr>())) {

      PathDiagnosticLocation ExLoc =
        PathDiagnosticLocation::createBegin(Ex, BR.getSourceManager(), AC);
      Report(VD, dsk, ExLoc, Val->getSourceRange());
    }
  }

  void CheckDeclRef(const DeclRefExpr *DR, const Expr *Val, DeadStoreKind dsk,
                    const LiveVariables::LivenessValues& Live) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl()))
      CheckVarDecl(VD, DR, Val, dsk, Live);
  }

  bool isIncrement(VarDecl *VD, const BinaryOperator* B) {
    if (B->isCompoundAssignmentOp())
      return true;

    const Expr *RHS = B->getRHS()->IgnoreParenCasts();
    const BinaryOperator* BRHS = dyn_cast<BinaryOperator>(RHS);

    if (!BRHS)
      return false;

    const DeclRefExpr *DR;

    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getLHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;

    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getRHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;

    return false;
  }

  void observeStmt(const Stmt *S, const CFGBlock *block,
                   const LiveVariables::LivenessValues &Live) override {

    currentBlock = block;

    // Skip statements in macros.
    if (S->getBeginLoc().isMacroID())
      return;

    // Only cover dead stores from regular assignments.  ++/-- dead stores
    // have never flagged a real bug.
    if (const BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {
      if (!B->isAssignmentOp()) return; // Skip non-assignments.

      if (DeclRefExpr *DR = dyn_cast<DeclRefExpr>(B->getLHS()))
        if (VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
          // Special case: check for assigning null to a pointer.
          //  This is a common form of defensive programming.
          const Expr *RHS =
              LookThroughTransitiveAssignmentsAndCommaOperators(B->getRHS());

          QualType T = VD->getType();
          if (T.isVolatileQualified())
            return;
          if (T->isPointerType() || T->isObjCObjectPointerType()) {
            if (RHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNull))
              return;
          }

          // Special case: self-assignments.  These are often used to shut up
          //  "unused variable" compiler warnings.
          if (const DeclRefExpr *RhsDR = dyn_cast<DeclRefExpr>(RHS))
            if (VD == dyn_cast<VarDecl>(RhsDR->getDecl()))
              return;

          // Otherwise, issue a warning.
          DeadStoreKind dsk = Parents.isConsumedExpr(B)
                              ? Enclosing
                              : (isIncrement(VD,B) ? DeadIncrement : Standard);

          CheckVarDecl(VD, DR, B->getRHS(), dsk, Live);
        }
    }
    else if (const UnaryOperator* U = dyn_cast<UnaryOperator>(S)) {
      if (!U->isIncrementOp() || U->isPrefix())
        return;

      const Stmt *parent = Parents.getParentIgnoreParenCasts(U);
      if (!parent || !isa<ReturnStmt>(parent))
        return;

      const Expr *Ex = U->getSubExpr()->IgnoreParenCasts();

      if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Ex))
        CheckDeclRef(DR, U, DeadIncrement, Live);
    }
    else if (const DeclStmt *DS = dyn_cast<DeclStmt>(S))
      // Iterate through the decls.  Warn if any initializers are complex
      // expressions that are not live (never used).
      for (const auto *DI : DS->decls()) {
        const auto *V = dyn_cast<VarDecl>(DI);

        if (!V)
          continue;

        if (V->hasLocalStorage()) {
          // Reference types confuse the dead stores checker.  Skip them
          // for now.
          if (V->getType()->getAs<ReferenceType>())
            return;

          if (const Expr *E = V->getInit()) {
            while (const FullExpr *FE = dyn_cast<FullExpr>(E))
              E = FE->getSubExpr();

            // Look through transitive assignments, e.g.:
            // int x = y = 0;
            E = LookThroughTransitiveAssignmentsAndCommaOperators(E);

            // Don't warn on C++ objects (yet) until we can show that their
            // constructors/destructors don't have side effects.
            if (isa<CXXConstructExpr>(E))
              return;

            // A dead initialization is a variable that is dead after it
            // is initialized.  We don't flag warnings for those variables
            // marked 'unused' or 'objc_precise_lifetime'.
            if (!isLive(Live, V) &&
                !V->hasAttr<UnusedAttr>() &&
                !V->hasAttr<ObjCPreciseLifetimeAttr>()) {
              // Special case: check for initializations with constants.
              //
              //  e.g. : int x = 0;
              //         struct A = {0, 1};
              //         struct B = {{0}, {1, 2}};
              //
              // If x is EVER assigned a new value later, don't issue
              // a warning.  This is because such initialization can be
              // due to defensive programming.
              if (isConstant(E))
                return;

              if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
                if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                  // Special case: check for initialization from constant
                  //  variables.
                  //
                  //  e.g. extern const int MyConstant;
                  //       int x = MyConstant;
                  //
                  if (VD->hasGlobalStorage() &&
                      VD->getType().isConstQualified())
                    return;
                  // Special case: check for initialization from scalar
                  //  parameters.  This is often a form of defensive
                  //  programming.  Non-scalars are still an error since
                  //  because it more likely represents an actual algorithmic
                  //  bug.
                  if (isa<ParmVarDecl>(VD) && VD->getType()->isScalarType())
                    return;
                }

              PathDiagnosticLocation Loc =
                PathDiagnosticLocation::create(V, BR.getSourceManager());
              Report(V, DeadInit, Loc, V->getInit()->getSourceRange());
            }
          }
        }
      }
  }

private:
  /// Return true if the given init list can be interpreted as constant
  bool isConstant(const InitListExpr *Candidate) const {
    // We consider init list to be constant if each member of the list can be
    // interpreted as constant.
    return llvm::all_of(Candidate->inits(), [this](const Expr *Init) {
      return isConstant(Init->IgnoreParenCasts());
    });
  }

  /// Return true if the given expression can be interpreted as constant
  bool isConstant(const Expr *E) const {
    // It looks like E itself is a constant
    if (E->isEvaluatable(Ctx))
      return true;

    // We should also allow defensive initialization of structs, i.e. { 0 }
    if (const auto *ILE = dyn_cast<InitListExpr>(E)) {
      return isConstant(ILE);
    }

    return false;
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver function to invoke the Dead-Stores checker on a CFG.
//===----------------------------------------------------------------------===//

namespace {
class FindEscaped {
public:
  llvm::SmallPtrSet<const VarDecl*, 20> Escaped;

  void operator()(const Stmt *S) {
    // Check for '&'. Any VarDecl whose address has been taken we treat as
    // escaped.
    // FIXME: What about references?
    if (auto *LE = dyn_cast<LambdaExpr>(S)) {
      findLambdaReferenceCaptures(LE);
      return;
    }

    const UnaryOperator *U = dyn_cast<UnaryOperator>(S);
    if (!U)
      return;
    if (U->getOpcode() != UO_AddrOf)
      return;

    const Expr *E = U->getSubExpr()->IgnoreParenCasts();
    if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E))
      if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl()))
        Escaped.insert(VD);
  }

  // Treat local variables captured by reference in C++ lambdas as escaped.
  void findLambdaReferenceCaptures(const LambdaExpr *LE)  {
    const CXXRecordDecl *LambdaClass = LE->getLambdaClass();
    llvm::DenseMap<const ValueDecl *, FieldDecl *> CaptureFields;
    FieldDecl *ThisCaptureField;
    LambdaClass->getCaptureFields(CaptureFields, ThisCaptureField);

    for (const LambdaCapture &C : LE->captures()) {
      if (!C.capturesVariable())
        continue;

      ValueDecl *VD = C.getCapturedVar();
      const FieldDecl *FD = CaptureFields[VD];
      if (!FD || !isa<VarDecl>(VD))
        continue;

      // If the capture field is a reference type, it is capture-by-reference.
      if (FD->getType()->isReferenceType())
        Escaped.insert(cast<VarDecl>(VD));
    }
  }
};
} // end anonymous namespace


//===----------------------------------------------------------------------===//
// DeadStoresChecker
//===----------------------------------------------------------------------===//

void DeadStoresChecker::checkASTCodeBody(const Decl *D, AnalysisManager &mgr,
                                         BugReporter &BR) const {

  // Don't do anything for template instantiations.
  // Proving that code in a template instantiation is "dead"
  // means proving that it is dead in all instantiations.
  // This same problem exists with -Wunreachable-code.
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isTemplateInstantiation())
      return;

  if (LiveVariables *L = mgr.getAnalysis<LiveVariables>(D)) {
    CFG &cfg = *mgr.getCFG(D);
    AnalysisDeclContext *AC = mgr.getAnalysisDeclContext(D);
    ParentMap &pmap = mgr.getParentMap(D);
    FindEscaped FS;
    cfg.VisitBlockStmts(FS);
    DeadStoreObs A(cfg, BR.getContext(), BR, this, AC, pmap, FS.Escaped,
                   WarnForDeadNestedAssignments);
    L->runOnAllBlocks(A);
  }
}

void ento::registerDeadStoresChecker(CheckerManager &Mgr) {
  auto *Chk = Mgr.registerChecker<DeadStoresChecker>();

  const AnalyzerOptions &AnOpts = Mgr.getAnalyzerOptions();
  Chk->WarnForDeadNestedAssignments =
      AnOpts.getCheckerBooleanOption(Chk, "WarnForDeadNestedAssignments");
  Chk->ShowFixIts =
      AnOpts.getCheckerBooleanOption(Chk, "ShowFixIts");
}

bool ento::shouldRegisterDeadStoresChecker(const CheckerManager &mgr) {
  return true;
}
