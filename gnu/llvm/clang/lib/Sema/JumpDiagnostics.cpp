//===--- JumpDiagnostics.cpp - Protected scope jump analysis ------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the JumpScopeChecker class, which is used to diagnose
// jumps that enter a protected scope in an invalid way.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/BitVector.h"
using namespace clang;

namespace {

/// JumpScopeChecker - This object is used by Sema to diagnose invalid jumps
/// into VLA and other protected scopes.  For example, this rejects:
///    goto L;
///    int a[n];
///  L:
///
/// We also detect jumps out of protected scopes when it's not possible to do
/// cleanups properly. Indirect jumps and ASM jumps can't do cleanups because
/// the target is unknown. Return statements with \c [[clang::musttail]] cannot
/// handle any cleanups due to the nature of a tail call.
class JumpScopeChecker {
  Sema &S;

  /// Permissive - True when recovering from errors, in which case precautions
  /// are taken to handle incomplete scope information.
  const bool Permissive;

  /// GotoScope - This is a record that we use to keep track of all of the
  /// scopes that are introduced by VLAs and other things that scope jumps like
  /// gotos.  This scope tree has nothing to do with the source scope tree,
  /// because you can have multiple VLA scopes per compound statement, and most
  /// compound statements don't introduce any scopes.
  struct GotoScope {
    /// ParentScope - The index in ScopeMap of the parent scope.  This is 0 for
    /// the parent scope is the function body.
    unsigned ParentScope;

    /// InDiag - The note to emit if there is a jump into this scope.
    unsigned InDiag;

    /// OutDiag - The note to emit if there is an indirect jump out
    /// of this scope.  Direct jumps always clean up their current scope
    /// in an orderly way.
    unsigned OutDiag;

    /// Loc - Location to emit the diagnostic.
    SourceLocation Loc;

    GotoScope(unsigned parentScope, unsigned InDiag, unsigned OutDiag,
              SourceLocation L)
      : ParentScope(parentScope), InDiag(InDiag), OutDiag(OutDiag), Loc(L) {}
  };

  SmallVector<GotoScope, 48> Scopes;
  llvm::DenseMap<Stmt*, unsigned> LabelAndGotoScopes;
  SmallVector<Stmt*, 16> Jumps;

  SmallVector<Stmt*, 4> IndirectJumps;
  SmallVector<LabelDecl *, 4> IndirectJumpTargets;
  SmallVector<AttributedStmt *, 4> MustTailStmts;

public:
  JumpScopeChecker(Stmt *Body, Sema &S);
private:
  void BuildScopeInformation(Decl *D, unsigned &ParentScope);
  void BuildScopeInformation(VarDecl *D, const BlockDecl *BDecl,
                             unsigned &ParentScope);
  void BuildScopeInformation(CompoundLiteralExpr *CLE, unsigned &ParentScope);
  void BuildScopeInformation(Stmt *S, unsigned &origParentScope);

  void VerifyJumps();
  void VerifyIndirectJumps();
  void VerifyMustTailStmts();
  void NoteJumpIntoScopes(ArrayRef<unsigned> ToScopes);
  void DiagnoseIndirectOrAsmJump(Stmt *IG, unsigned IGScope, LabelDecl *Target,
                                 unsigned TargetScope);
  void CheckJump(Stmt *From, Stmt *To, SourceLocation DiagLoc,
                 unsigned JumpDiag, unsigned JumpDiagWarning,
                 unsigned JumpDiagCXX98Compat);
  void CheckGotoStmt(GotoStmt *GS);
  const Attr *GetMustTailAttr(AttributedStmt *AS);

  unsigned GetDeepestCommonScope(unsigned A, unsigned B);
};
} // end anonymous namespace

#define CHECK_PERMISSIVE(x) (assert(Permissive || !(x)), (Permissive && (x)))

JumpScopeChecker::JumpScopeChecker(Stmt *Body, Sema &s)
    : S(s), Permissive(s.hasAnyUnrecoverableErrorsInThisFunction()) {
  // Add a scope entry for function scope.
  Scopes.push_back(GotoScope(~0U, ~0U, ~0U, SourceLocation()));

  // Build information for the top level compound statement, so that we have a
  // defined scope record for every "goto" and label.
  unsigned BodyParentScope = 0;
  BuildScopeInformation(Body, BodyParentScope);

  // Check that all jumps we saw are kosher.
  VerifyJumps();
  VerifyIndirectJumps();
  VerifyMustTailStmts();
}

/// GetDeepestCommonScope - Finds the innermost scope enclosing the
/// two scopes.
unsigned JumpScopeChecker::GetDeepestCommonScope(unsigned A, unsigned B) {
  while (A != B) {
    // Inner scopes are created after outer scopes and therefore have
    // higher indices.
    if (A < B) {
      assert(Scopes[B].ParentScope < B);
      B = Scopes[B].ParentScope;
    } else {
      assert(Scopes[A].ParentScope < A);
      A = Scopes[A].ParentScope;
    }
  }
  return A;
}

typedef std::pair<unsigned,unsigned> ScopePair;

/// GetDiagForGotoScopeDecl - If this decl induces a new goto scope, return a
/// diagnostic that should be emitted if control goes over it. If not, return 0.
static ScopePair GetDiagForGotoScopeDecl(Sema &S, const Decl *D) {
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    unsigned InDiag = 0;
    unsigned OutDiag = 0;

    if (VD->getType()->isVariablyModifiedType())
      InDiag = diag::note_protected_by_vla;

    if (VD->hasAttr<BlocksAttr>())
      return ScopePair(diag::note_protected_by___block,
                       diag::note_exits___block);

    if (VD->hasAttr<CleanupAttr>())
      return ScopePair(diag::note_protected_by_cleanup,
                       diag::note_exits_cleanup);

    if (VD->hasLocalStorage()) {
      switch (VD->getType().isDestructedType()) {
      case QualType::DK_objc_strong_lifetime:
        return ScopePair(diag::note_protected_by_objc_strong_init,
                         diag::note_exits_objc_strong);

      case QualType::DK_objc_weak_lifetime:
        return ScopePair(diag::note_protected_by_objc_weak_init,
                         diag::note_exits_objc_weak);

      case QualType::DK_nontrivial_c_struct:
        return ScopePair(diag::note_protected_by_non_trivial_c_struct_init,
                         diag::note_exits_dtor);

      case QualType::DK_cxx_destructor:
        OutDiag = diag::note_exits_dtor;
        break;

      case QualType::DK_none:
        break;
      }
    }

    const Expr *Init = VD->getInit();
    if (S.Context.getLangOpts().CPlusPlus && VD->hasLocalStorage() && Init &&
        !Init->containsErrors()) {
      // C++11 [stmt.dcl]p3:
      //   A program that jumps from a point where a variable with automatic
      //   storage duration is not in scope to a point where it is in scope
      //   is ill-formed unless the variable has scalar type, class type with
      //   a trivial default constructor and a trivial destructor, a
      //   cv-qualified version of one of these types, or an array of one of
      //   the preceding types and is declared without an initializer.

      // C++03 [stmt.dcl.p3:
      //   A program that jumps from a point where a local variable
      //   with automatic storage duration is not in scope to a point
      //   where it is in scope is ill-formed unless the variable has
      //   POD type and is declared without an initializer.

      InDiag = diag::note_protected_by_variable_init;

      // For a variable of (array of) class type declared without an
      // initializer, we will have call-style initialization and the initializer
      // will be the CXXConstructExpr with no intervening nodes.
      if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(Init)) {
        const CXXConstructorDecl *Ctor = CCE->getConstructor();
        if (Ctor->isTrivial() && Ctor->isDefaultConstructor() &&
            VD->getInitStyle() == VarDecl::CallInit) {
          if (OutDiag)
            InDiag = diag::note_protected_by_variable_nontriv_destructor;
          else if (!Ctor->getParent()->isPOD())
            InDiag = diag::note_protected_by_variable_non_pod;
          else
            InDiag = 0;
        }
      }
    }

    return ScopePair(InDiag, OutDiag);
  }

  if (const TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (TD->getUnderlyingType()->isVariablyModifiedType())
      return ScopePair(isa<TypedefDecl>(TD)
                           ? diag::note_protected_by_vla_typedef
                           : diag::note_protected_by_vla_type_alias,
                       0);
  }

  return ScopePair(0U, 0U);
}

/// Build scope information for a declaration that is part of a DeclStmt.
void JumpScopeChecker::BuildScopeInformation(Decl *D, unsigned &ParentScope) {
  // If this decl causes a new scope, push and switch to it.
  std::pair<unsigned,unsigned> Diags = GetDiagForGotoScopeDecl(S, D);
  if (Diags.first || Diags.second) {
    Scopes.push_back(GotoScope(ParentScope, Diags.first, Diags.second,
                               D->getLocation()));
    ParentScope = Scopes.size()-1;
  }

  // If the decl has an initializer, walk it with the potentially new
  // scope we just installed.
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    if (Expr *Init = VD->getInit())
      BuildScopeInformation(Init, ParentScope);
}

/// Build scope information for a captured block literal variables.
void JumpScopeChecker::BuildScopeInformation(VarDecl *D,
                                             const BlockDecl *BDecl,
                                             unsigned &ParentScope) {
  // exclude captured __block variables; there's no destructor
  // associated with the block literal for them.
  if (D->hasAttr<BlocksAttr>())
    return;
  QualType T = D->getType();
  QualType::DestructionKind destructKind = T.isDestructedType();
  if (destructKind != QualType::DK_none) {
    std::pair<unsigned,unsigned> Diags;
    switch (destructKind) {
      case QualType::DK_cxx_destructor:
        Diags = ScopePair(diag::note_enters_block_captures_cxx_obj,
                          diag::note_exits_block_captures_cxx_obj);
        break;
      case QualType::DK_objc_strong_lifetime:
        Diags = ScopePair(diag::note_enters_block_captures_strong,
                          diag::note_exits_block_captures_strong);
        break;
      case QualType::DK_objc_weak_lifetime:
        Diags = ScopePair(diag::note_enters_block_captures_weak,
                          diag::note_exits_block_captures_weak);
        break;
      case QualType::DK_nontrivial_c_struct:
        Diags = ScopePair(diag::note_enters_block_captures_non_trivial_c_struct,
                          diag::note_exits_block_captures_non_trivial_c_struct);
        break;
      case QualType::DK_none:
        llvm_unreachable("non-lifetime captured variable");
    }
    SourceLocation Loc = D->getLocation();
    if (Loc.isInvalid())
      Loc = BDecl->getLocation();
    Scopes.push_back(GotoScope(ParentScope,
                               Diags.first, Diags.second, Loc));
    ParentScope = Scopes.size()-1;
  }
}

/// Build scope information for compound literals of C struct types that are
/// non-trivial to destruct.
void JumpScopeChecker::BuildScopeInformation(CompoundLiteralExpr *CLE,
                                             unsigned &ParentScope) {
  unsigned InDiag = diag::note_enters_compound_literal_scope;
  unsigned OutDiag = diag::note_exits_compound_literal_scope;
  Scopes.push_back(GotoScope(ParentScope, InDiag, OutDiag, CLE->getExprLoc()));
  ParentScope = Scopes.size() - 1;
}

/// BuildScopeInformation - The statements from CI to CE are known to form a
/// coherent VLA scope with a specified parent node.  Walk through the
/// statements, adding any labels or gotos to LabelAndGotoScopes and recursively
/// walking the AST as needed.
void JumpScopeChecker::BuildScopeInformation(Stmt *S,
                                             unsigned &origParentScope) {
  // If this is a statement, rather than an expression, scopes within it don't
  // propagate out into the enclosing scope.  Otherwise we have to worry
  // about block literals, which have the lifetime of their enclosing statement.
  unsigned independentParentScope = origParentScope;
  unsigned &ParentScope = ((isa<Expr>(S) && !isa<StmtExpr>(S))
                            ? origParentScope : independentParentScope);

  unsigned StmtsToSkip = 0u;

  // If we found a label, remember that it is in ParentScope scope.
  switch (S->getStmtClass()) {
  case Stmt::AddrLabelExprClass:
    IndirectJumpTargets.push_back(cast<AddrLabelExpr>(S)->getLabel());
    break;

  case Stmt::ObjCForCollectionStmtClass: {
    auto *CS = cast<ObjCForCollectionStmt>(S);
    unsigned Diag = diag::note_protected_by_objc_fast_enumeration;
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope, Diag, 0, S->getBeginLoc()));
    BuildScopeInformation(CS->getBody(), NewParentScope);
    return;
  }

  case Stmt::IndirectGotoStmtClass:
    // "goto *&&lbl;" is a special case which we treat as equivalent
    // to a normal goto.  In addition, we don't calculate scope in the
    // operand (to avoid recording the address-of-label use), which
    // works only because of the restricted set of expressions which
    // we detect as constant targets.
    if (cast<IndirectGotoStmt>(S)->getConstantTarget())
      goto RecordJumpScope;

    LabelAndGotoScopes[S] = ParentScope;
    IndirectJumps.push_back(S);
    break;

  case Stmt::SwitchStmtClass:
    // Evaluate the C++17 init stmt and condition variable
    // before entering the scope of the switch statement.
    if (Stmt *Init = cast<SwitchStmt>(S)->getInit()) {
      BuildScopeInformation(Init, ParentScope);
      ++StmtsToSkip;
    }
    if (VarDecl *Var = cast<SwitchStmt>(S)->getConditionVariable()) {
      BuildScopeInformation(Var, ParentScope);
      ++StmtsToSkip;
    }
    goto RecordJumpScope;

  case Stmt::GCCAsmStmtClass:
    if (!cast<GCCAsmStmt>(S)->isAsmGoto())
      break;
    [[fallthrough]];

  case Stmt::GotoStmtClass:
  RecordJumpScope:
    // Remember both what scope a goto is in as well as the fact that we have
    // it.  This makes the second scan not have to walk the AST again.
    LabelAndGotoScopes[S] = ParentScope;
    Jumps.push_back(S);
    break;

  case Stmt::IfStmtClass: {
    IfStmt *IS = cast<IfStmt>(S);
    if (!(IS->isConstexpr() || IS->isConsteval() ||
          IS->isObjCAvailabilityCheck()))
      break;

    unsigned Diag = diag::note_protected_by_if_available;
    if (IS->isConstexpr())
      Diag = diag::note_protected_by_constexpr_if;
    else if (IS->isConsteval())
      Diag = diag::note_protected_by_consteval_if;

    if (VarDecl *Var = IS->getConditionVariable())
      BuildScopeInformation(Var, ParentScope);

    // Cannot jump into the middle of the condition.
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope, Diag, 0, IS->getBeginLoc()));

    if (!IS->isConsteval())
      BuildScopeInformation(IS->getCond(), NewParentScope);

    // Jumps into either arm of an 'if constexpr' are not allowed.
    NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope, Diag, 0, IS->getBeginLoc()));
    BuildScopeInformation(IS->getThen(), NewParentScope);
    if (Stmt *Else = IS->getElse()) {
      NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope, Diag, 0, IS->getBeginLoc()));
      BuildScopeInformation(Else, NewParentScope);
    }
    return;
  }

  case Stmt::CXXTryStmtClass: {
    CXXTryStmt *TS = cast<CXXTryStmt>(S);
    {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_cxx_try,
                                 diag::note_exits_cxx_try,
                                 TS->getSourceRange().getBegin()));
      if (Stmt *TryBlock = TS->getTryBlock())
        BuildScopeInformation(TryBlock, NewParentScope);
    }

    // Jump from the catch into the try is not allowed either.
    for (unsigned I = 0, E = TS->getNumHandlers(); I != E; ++I) {
      CXXCatchStmt *CS = TS->getHandler(I);
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_cxx_catch,
                                 diag::note_exits_cxx_catch,
                                 CS->getSourceRange().getBegin()));
      BuildScopeInformation(CS->getHandlerBlock(), NewParentScope);
    }
    return;
  }

  case Stmt::SEHTryStmtClass: {
    SEHTryStmt *TS = cast<SEHTryStmt>(S);
    {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_seh_try,
                                 diag::note_exits_seh_try,
                                 TS->getSourceRange().getBegin()));
      if (Stmt *TryBlock = TS->getTryBlock())
        BuildScopeInformation(TryBlock, NewParentScope);
    }

    // Jump from __except or __finally into the __try are not allowed either.
    if (SEHExceptStmt *Except = TS->getExceptHandler()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_seh_except,
                                 diag::note_exits_seh_except,
                                 Except->getSourceRange().getBegin()));
      BuildScopeInformation(Except->getBlock(), NewParentScope);
    } else if (SEHFinallyStmt *Finally = TS->getFinallyHandler()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_seh_finally,
                                 diag::note_exits_seh_finally,
                                 Finally->getSourceRange().getBegin()));
      BuildScopeInformation(Finally->getBlock(), NewParentScope);
    }

    return;
  }

  case Stmt::DeclStmtClass: {
    // If this is a declstmt with a VLA definition, it defines a scope from here
    // to the end of the containing context.
    DeclStmt *DS = cast<DeclStmt>(S);
    // The decl statement creates a scope if any of the decls in it are VLAs
    // or have the cleanup attribute.
    for (auto *I : DS->decls())
      BuildScopeInformation(I, origParentScope);
    return;
  }

  case Stmt::StmtExprClass: {
    // [GNU]
    // Jumping into a statement expression with goto or using
    // a switch statement outside the statement expression with
    // a case or default label inside the statement expression is not permitted.
    // Jumping out of a statement expression is permitted.
    StmtExpr *SE = cast<StmtExpr>(S);
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope,
                               diag::note_enters_statement_expression,
                               /*OutDiag=*/0, SE->getBeginLoc()));
    BuildScopeInformation(SE->getSubStmt(), NewParentScope);
    return;
  }

  case Stmt::ObjCAtTryStmtClass: {
    // Disallow jumps into any part of an @try statement by pushing a scope and
    // walking all sub-stmts in that scope.
    ObjCAtTryStmt *AT = cast<ObjCAtTryStmt>(S);
    // Recursively walk the AST for the @try part.
    {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_objc_try,
                                 diag::note_exits_objc_try,
                                 AT->getAtTryLoc()));
      if (Stmt *TryPart = AT->getTryBody())
        BuildScopeInformation(TryPart, NewParentScope);
    }

    // Jump from the catch to the finally or try is not valid.
    for (ObjCAtCatchStmt *AC : AT->catch_stmts()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_objc_catch,
                                 diag::note_exits_objc_catch,
                                 AC->getAtCatchLoc()));
      // @catches are nested and it isn't
      BuildScopeInformation(AC->getCatchBody(), NewParentScope);
    }

    // Jump from the finally to the try or catch is not valid.
    if (ObjCAtFinallyStmt *AF = AT->getFinallyStmt()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope,
                                 diag::note_protected_by_objc_finally,
                                 diag::note_exits_objc_finally,
                                 AF->getAtFinallyLoc()));
      BuildScopeInformation(AF, NewParentScope);
    }

    return;
  }

  case Stmt::ObjCAtSynchronizedStmtClass: {
    // Disallow jumps into the protected statement of an @synchronized, but
    // allow jumps into the object expression it protects.
    ObjCAtSynchronizedStmt *AS = cast<ObjCAtSynchronizedStmt>(S);
    // Recursively walk the AST for the @synchronized object expr, it is
    // evaluated in the normal scope.
    BuildScopeInformation(AS->getSynchExpr(), ParentScope);

    // Recursively walk the AST for the @synchronized part, protected by a new
    // scope.
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope,
                               diag::note_protected_by_objc_synchronized,
                               diag::note_exits_objc_synchronized,
                               AS->getAtSynchronizedLoc()));
    BuildScopeInformation(AS->getSynchBody(), NewParentScope);
    return;
  }

  case Stmt::ObjCAutoreleasePoolStmtClass: {
    // Disallow jumps into the protected statement of an @autoreleasepool.
    ObjCAutoreleasePoolStmt *AS = cast<ObjCAutoreleasePoolStmt>(S);
    // Recursively walk the AST for the @autoreleasepool part, protected by a
    // new scope.
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope,
                               diag::note_protected_by_objc_autoreleasepool,
                               diag::note_exits_objc_autoreleasepool,
                               AS->getAtLoc()));
    BuildScopeInformation(AS->getSubStmt(), NewParentScope);
    return;
  }

  case Stmt::ExprWithCleanupsClass: {
    // Disallow jumps past full-expressions that use blocks with
    // non-trivial cleanups of their captures.  This is theoretically
    // implementable but a lot of work which we haven't felt up to doing.
    ExprWithCleanups *EWC = cast<ExprWithCleanups>(S);
    for (unsigned i = 0, e = EWC->getNumObjects(); i != e; ++i) {
      if (auto *BDecl = EWC->getObject(i).dyn_cast<BlockDecl *>())
        for (const auto &CI : BDecl->captures()) {
          VarDecl *variable = CI.getVariable();
          BuildScopeInformation(variable, BDecl, origParentScope);
        }
      else if (auto *CLE = EWC->getObject(i).dyn_cast<CompoundLiteralExpr *>())
        BuildScopeInformation(CLE, origParentScope);
      else
        llvm_unreachable("unexpected cleanup object type");
    }
    break;
  }

  case Stmt::MaterializeTemporaryExprClass: {
    // Disallow jumps out of scopes containing temporaries lifetime-extended to
    // automatic storage duration.
    MaterializeTemporaryExpr *MTE = cast<MaterializeTemporaryExpr>(S);
    if (MTE->getStorageDuration() == SD_Automatic) {
      const Expr *ExtendedObject =
          MTE->getSubExpr()->skipRValueSubobjectAdjustments();
      if (ExtendedObject->getType().isDestructedType()) {
        Scopes.push_back(GotoScope(ParentScope, 0,
                                   diag::note_exits_temporary_dtor,
                                   ExtendedObject->getExprLoc()));
        origParentScope = Scopes.size()-1;
      }
    }
    break;
  }

  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::LabelStmtClass:
    LabelAndGotoScopes[S] = ParentScope;
    break;

  case Stmt::AttributedStmtClass: {
    AttributedStmt *AS = cast<AttributedStmt>(S);
    if (GetMustTailAttr(AS)) {
      LabelAndGotoScopes[AS] = ParentScope;
      MustTailStmts.push_back(AS);
    }
    break;
  }

  case Stmt::OpenACCComputeConstructClass: {
    unsigned NewParentScope = Scopes.size();
    OpenACCComputeConstruct *CC = cast<OpenACCComputeConstruct>(S);
    Scopes.push_back(GotoScope(
        ParentScope, diag::note_acc_branch_into_compute_construct,
        diag::note_acc_branch_out_of_compute_construct, CC->getBeginLoc()));
    BuildScopeInformation(CC->getStructuredBlock(), NewParentScope);
    return;
  }

  default:
    if (auto *ED = dyn_cast<OMPExecutableDirective>(S)) {
      if (!ED->isStandaloneDirective()) {
        unsigned NewParentScope = Scopes.size();
        Scopes.emplace_back(ParentScope,
                            diag::note_omp_protected_structured_block,
                            diag::note_omp_exits_structured_block,
                            ED->getStructuredBlock()->getBeginLoc());
        BuildScopeInformation(ED->getStructuredBlock(), NewParentScope);
        return;
      }
    }
    break;
  }

  for (Stmt *SubStmt : S->children()) {
    if (!SubStmt)
        continue;
    if (StmtsToSkip) {
      --StmtsToSkip;
      continue;
    }

    // Cases, labels, and defaults aren't "scope parents".  It's also
    // important to handle these iteratively instead of recursively in
    // order to avoid blowing out the stack.
    while (true) {
      Stmt *Next;
      if (SwitchCase *SC = dyn_cast<SwitchCase>(SubStmt))
        Next = SC->getSubStmt();
      else if (LabelStmt *LS = dyn_cast<LabelStmt>(SubStmt))
        Next = LS->getSubStmt();
      else
        break;

      LabelAndGotoScopes[SubStmt] = ParentScope;
      SubStmt = Next;
    }

    // Recursively walk the AST.
    BuildScopeInformation(SubStmt, ParentScope);
  }
}

/// VerifyJumps - Verify each element of the Jumps array to see if they are
/// valid, emitting diagnostics if not.
void JumpScopeChecker::VerifyJumps() {
  while (!Jumps.empty()) {
    Stmt *Jump = Jumps.pop_back_val();

    // With a goto,
    if (GotoStmt *GS = dyn_cast<GotoStmt>(Jump)) {
      // The label may not have a statement if it's coming from inline MS ASM.
      if (GS->getLabel()->getStmt()) {
        CheckJump(GS, GS->getLabel()->getStmt(), GS->getGotoLoc(),
                  diag::err_goto_into_protected_scope,
                  diag::ext_goto_into_protected_scope,
                  diag::warn_cxx98_compat_goto_into_protected_scope);
      }
      CheckGotoStmt(GS);
      continue;
    }

    // If an asm goto jumps to a different scope, things like destructors or
    // initializers might not be run which may be suprising to users. Perhaps
    // this behavior can be changed in the future, but today Clang will not
    // generate such code. Produce a diagnostic instead. See also the
    // discussion here: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=110728.
    if (auto *G = dyn_cast<GCCAsmStmt>(Jump)) {
      for (AddrLabelExpr *L : G->labels()) {
        LabelDecl *LD = L->getLabel();
        unsigned JumpScope = LabelAndGotoScopes[G];
        unsigned TargetScope = LabelAndGotoScopes[LD->getStmt()];
        if (JumpScope != TargetScope)
          DiagnoseIndirectOrAsmJump(G, JumpScope, LD, TargetScope);
      }
      continue;
    }

    // We only get indirect gotos here when they have a constant target.
    if (IndirectGotoStmt *IGS = dyn_cast<IndirectGotoStmt>(Jump)) {
      LabelDecl *Target = IGS->getConstantTarget();
      CheckJump(IGS, Target->getStmt(), IGS->getGotoLoc(),
                diag::err_goto_into_protected_scope,
                diag::ext_goto_into_protected_scope,
                diag::warn_cxx98_compat_goto_into_protected_scope);
      continue;
    }

    SwitchStmt *SS = cast<SwitchStmt>(Jump);
    for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
         SC = SC->getNextSwitchCase()) {
      if (CHECK_PERMISSIVE(!LabelAndGotoScopes.count(SC)))
        continue;
      SourceLocation Loc;
      if (CaseStmt *CS = dyn_cast<CaseStmt>(SC))
        Loc = CS->getBeginLoc();
      else if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC))
        Loc = DS->getBeginLoc();
      else
        Loc = SC->getBeginLoc();
      CheckJump(SS, SC, Loc, diag::err_switch_into_protected_scope, 0,
                diag::warn_cxx98_compat_switch_into_protected_scope);
    }
  }
}

/// VerifyIndirectJumps - Verify whether any possible indirect goto jump might
/// cross a protection boundary.  Unlike direct jumps, indirect goto jumps
/// count cleanups as protection boundaries: since there's no way to know where
/// the jump is going, we can't implicitly run the right cleanups the way we
/// can with direct jumps.  Thus, an indirect/asm jump is "trivial" if it
/// bypasses no initializations and no teardowns.  More formally, an
/// indirect/asm jump from A to B is trivial if the path out from A to DCA(A,B)
/// is trivial and the path in from DCA(A,B) to B is trivial, where DCA(A,B) is
/// the deepest common ancestor of A and B.  Jump-triviality is transitive but
/// asymmetric.
///
/// A path in is trivial if none of the entered scopes have an InDiag.
/// A path out is trivial is none of the exited scopes have an OutDiag.
///
/// Under these definitions, this function checks that the indirect
/// jump between A and B is trivial for every indirect goto statement A
/// and every label B whose address was taken in the function.
void JumpScopeChecker::VerifyIndirectJumps() {
  if (IndirectJumps.empty())
    return;
  // If there aren't any address-of-label expressions in this function,
  // complain about the first indirect goto.
  if (IndirectJumpTargets.empty()) {
    S.Diag(IndirectJumps[0]->getBeginLoc(),
           diag::err_indirect_goto_without_addrlabel);
    return;
  }
  // Collect a single representative of every scope containing an indirect
  // goto.  For most code bases, this substantially cuts down on the number of
  // jump sites we'll have to consider later.
  using JumpScope = std::pair<unsigned, Stmt *>;
  SmallVector<JumpScope, 32> JumpScopes;
  {
    llvm::DenseMap<unsigned, Stmt*> JumpScopesMap;
    for (Stmt *IG : IndirectJumps) {
      if (CHECK_PERMISSIVE(!LabelAndGotoScopes.count(IG)))
        continue;
      unsigned IGScope = LabelAndGotoScopes[IG];
      if (!JumpScopesMap.contains(IGScope))
        JumpScopesMap[IGScope] = IG;
    }
    JumpScopes.reserve(JumpScopesMap.size());
    for (auto &Pair : JumpScopesMap)
      JumpScopes.emplace_back(Pair);
  }

  // Collect a single representative of every scope containing a
  // label whose address was taken somewhere in the function.
  // For most code bases, there will be only one such scope.
  llvm::DenseMap<unsigned, LabelDecl*> TargetScopes;
  for (LabelDecl *TheLabel : IndirectJumpTargets) {
    if (CHECK_PERMISSIVE(!LabelAndGotoScopes.count(TheLabel->getStmt())))
      continue;
    unsigned LabelScope = LabelAndGotoScopes[TheLabel->getStmt()];
    if (!TargetScopes.contains(LabelScope))
      TargetScopes[LabelScope] = TheLabel;
  }

  // For each target scope, make sure it's trivially reachable from
  // every scope containing a jump site.
  //
  // A path between scopes always consists of exitting zero or more
  // scopes, then entering zero or more scopes.  We build a set of
  // of scopes S from which the target scope can be trivially
  // entered, then verify that every jump scope can be trivially
  // exitted to reach a scope in S.
  llvm::BitVector Reachable(Scopes.size(), false);
  for (auto [TargetScope, TargetLabel] : TargetScopes) {
    Reachable.reset();

    // Mark all the enclosing scopes from which you can safely jump
    // into the target scope.  'Min' will end up being the index of
    // the shallowest such scope.
    unsigned Min = TargetScope;
    while (true) {
      Reachable.set(Min);

      // Don't go beyond the outermost scope.
      if (Min == 0) break;

      // Stop if we can't trivially enter the current scope.
      if (Scopes[Min].InDiag) break;

      Min = Scopes[Min].ParentScope;
    }

    // Walk through all the jump sites, checking that they can trivially
    // reach this label scope.
    for (auto [JumpScope, JumpStmt] : JumpScopes) {
      unsigned Scope = JumpScope;
      // Walk out the "scope chain" for this scope, looking for a scope
      // we've marked reachable.  For well-formed code this amortizes
      // to O(JumpScopes.size() / Scopes.size()):  we only iterate
      // when we see something unmarked, and in well-formed code we
      // mark everything we iterate past.
      bool IsReachable = false;
      while (true) {
        if (Reachable.test(Scope)) {
          // If we find something reachable, mark all the scopes we just
          // walked through as reachable.
          for (unsigned S = JumpScope; S != Scope; S = Scopes[S].ParentScope)
            Reachable.set(S);
          IsReachable = true;
          break;
        }

        // Don't walk out if we've reached the top-level scope or we've
        // gotten shallower than the shallowest reachable scope.
        if (Scope == 0 || Scope < Min) break;

        // Don't walk out through an out-diagnostic.
        if (Scopes[Scope].OutDiag) break;

        Scope = Scopes[Scope].ParentScope;
      }

      // Only diagnose if we didn't find something.
      if (IsReachable) continue;

      DiagnoseIndirectOrAsmJump(JumpStmt, JumpScope, TargetLabel, TargetScope);
    }
  }
}

/// Return true if a particular error+note combination must be downgraded to a
/// warning in Microsoft mode.
static bool IsMicrosoftJumpWarning(unsigned JumpDiag, unsigned InDiagNote) {
  return (JumpDiag == diag::err_goto_into_protected_scope &&
         (InDiagNote == diag::note_protected_by_variable_init ||
          InDiagNote == diag::note_protected_by_variable_nontriv_destructor));
}

/// Return true if a particular note should be downgraded to a compatibility
/// warning in C++11 mode.
static bool IsCXX98CompatWarning(Sema &S, unsigned InDiagNote) {
  return S.getLangOpts().CPlusPlus11 &&
         InDiagNote == diag::note_protected_by_variable_non_pod;
}

/// Produce primary diagnostic for an indirect jump statement.
static void DiagnoseIndirectOrAsmJumpStmt(Sema &S, Stmt *Jump,
                                          LabelDecl *Target, bool &Diagnosed) {
  if (Diagnosed)
    return;
  bool IsAsmGoto = isa<GCCAsmStmt>(Jump);
  S.Diag(Jump->getBeginLoc(), diag::err_indirect_goto_in_protected_scope)
      << IsAsmGoto;
  S.Diag(Target->getStmt()->getIdentLoc(), diag::note_indirect_goto_target)
      << IsAsmGoto;
  Diagnosed = true;
}

/// Produce note diagnostics for a jump into a protected scope.
void JumpScopeChecker::NoteJumpIntoScopes(ArrayRef<unsigned> ToScopes) {
  if (CHECK_PERMISSIVE(ToScopes.empty()))
    return;
  for (unsigned I = 0, E = ToScopes.size(); I != E; ++I)
    if (Scopes[ToScopes[I]].InDiag)
      S.Diag(Scopes[ToScopes[I]].Loc, Scopes[ToScopes[I]].InDiag);
}

/// Diagnose an indirect jump which is known to cross scopes.
void JumpScopeChecker::DiagnoseIndirectOrAsmJump(Stmt *Jump, unsigned JumpScope,
                                                 LabelDecl *Target,
                                                 unsigned TargetScope) {
  if (CHECK_PERMISSIVE(JumpScope == TargetScope))
    return;

  unsigned Common = GetDeepestCommonScope(JumpScope, TargetScope);
  bool Diagnosed = false;

  // Walk out the scope chain until we reach the common ancestor.
  for (unsigned I = JumpScope; I != Common; I = Scopes[I].ParentScope)
    if (Scopes[I].OutDiag) {
      DiagnoseIndirectOrAsmJumpStmt(S, Jump, Target, Diagnosed);
      S.Diag(Scopes[I].Loc, Scopes[I].OutDiag);
    }

  SmallVector<unsigned, 10> ToScopesCXX98Compat;

  // Now walk into the scopes containing the label whose address was taken.
  for (unsigned I = TargetScope; I != Common; I = Scopes[I].ParentScope)
    if (IsCXX98CompatWarning(S, Scopes[I].InDiag))
      ToScopesCXX98Compat.push_back(I);
    else if (Scopes[I].InDiag) {
      DiagnoseIndirectOrAsmJumpStmt(S, Jump, Target, Diagnosed);
      S.Diag(Scopes[I].Loc, Scopes[I].InDiag);
    }

  // Diagnose this jump if it would be ill-formed in C++98.
  if (!Diagnosed && !ToScopesCXX98Compat.empty()) {
    bool IsAsmGoto = isa<GCCAsmStmt>(Jump);
    S.Diag(Jump->getBeginLoc(),
           diag::warn_cxx98_compat_indirect_goto_in_protected_scope)
        << IsAsmGoto;
    S.Diag(Target->getStmt()->getIdentLoc(), diag::note_indirect_goto_target)
        << IsAsmGoto;
    NoteJumpIntoScopes(ToScopesCXX98Compat);
  }
}

/// CheckJump - Validate that the specified jump statement is valid: that it is
/// jumping within or out of its current scope, not into a deeper one.
void JumpScopeChecker::CheckJump(Stmt *From, Stmt *To, SourceLocation DiagLoc,
                               unsigned JumpDiagError, unsigned JumpDiagWarning,
                                 unsigned JumpDiagCXX98Compat) {
  if (CHECK_PERMISSIVE(!LabelAndGotoScopes.count(From)))
    return;
  if (CHECK_PERMISSIVE(!LabelAndGotoScopes.count(To)))
    return;

  unsigned FromScope = LabelAndGotoScopes[From];
  unsigned ToScope = LabelAndGotoScopes[To];

  // Common case: exactly the same scope, which is fine.
  if (FromScope == ToScope) return;

  // Warn on gotos out of __finally blocks.
  if (isa<GotoStmt>(From) || isa<IndirectGotoStmt>(From)) {
    // If FromScope > ToScope, FromScope is more nested and the jump goes to a
    // less nested scope.  Check if it crosses a __finally along the way.
    for (unsigned I = FromScope; I > ToScope; I = Scopes[I].ParentScope) {
      if (Scopes[I].InDiag == diag::note_protected_by_seh_finally) {
        S.Diag(From->getBeginLoc(), diag::warn_jump_out_of_seh_finally);
        break;
      } else if (Scopes[I].InDiag ==
                 diag::note_omp_protected_structured_block) {
        S.Diag(From->getBeginLoc(), diag::err_goto_into_protected_scope);
        S.Diag(To->getBeginLoc(), diag::note_omp_exits_structured_block);
        break;
      } else if (Scopes[I].InDiag ==
                 diag::note_acc_branch_into_compute_construct) {
        S.Diag(From->getBeginLoc(), diag::err_goto_into_protected_scope);
        S.Diag(Scopes[I].Loc, diag::note_acc_branch_out_of_compute_construct);
        return;
      }
    }
  }

  unsigned CommonScope = GetDeepestCommonScope(FromScope, ToScope);

  // It's okay to jump out from a nested scope.
  if (CommonScope == ToScope) return;

  // Pull out (and reverse) any scopes we might need to diagnose skipping.
  SmallVector<unsigned, 10> ToScopesCXX98Compat;
  SmallVector<unsigned, 10> ToScopesError;
  SmallVector<unsigned, 10> ToScopesWarning;
  for (unsigned I = ToScope; I != CommonScope; I = Scopes[I].ParentScope) {
    if (S.getLangOpts().MSVCCompat && JumpDiagWarning != 0 &&
        IsMicrosoftJumpWarning(JumpDiagError, Scopes[I].InDiag))
      ToScopesWarning.push_back(I);
    else if (IsCXX98CompatWarning(S, Scopes[I].InDiag))
      ToScopesCXX98Compat.push_back(I);
    else if (Scopes[I].InDiag)
      ToScopesError.push_back(I);
  }

  // Handle warnings.
  if (!ToScopesWarning.empty()) {
    S.Diag(DiagLoc, JumpDiagWarning);
    NoteJumpIntoScopes(ToScopesWarning);
    assert(isa<LabelStmt>(To));
    LabelStmt *Label = cast<LabelStmt>(To);
    Label->setSideEntry(true);
  }

  // Handle errors.
  if (!ToScopesError.empty()) {
    S.Diag(DiagLoc, JumpDiagError);
    NoteJumpIntoScopes(ToScopesError);
  }

  // Handle -Wc++98-compat warnings if the jump is well-formed.
  if (ToScopesError.empty() && !ToScopesCXX98Compat.empty()) {
    S.Diag(DiagLoc, JumpDiagCXX98Compat);
    NoteJumpIntoScopes(ToScopesCXX98Compat);
  }
}

void JumpScopeChecker::CheckGotoStmt(GotoStmt *GS) {
  if (GS->getLabel()->isMSAsmLabel()) {
    S.Diag(GS->getGotoLoc(), diag::err_goto_ms_asm_label)
        << GS->getLabel()->getIdentifier();
    S.Diag(GS->getLabel()->getLocation(), diag::note_goto_ms_asm_label)
        << GS->getLabel()->getIdentifier();
  }
}

void JumpScopeChecker::VerifyMustTailStmts() {
  for (AttributedStmt *AS : MustTailStmts) {
    for (unsigned I = LabelAndGotoScopes[AS]; I; I = Scopes[I].ParentScope) {
      if (Scopes[I].OutDiag) {
        S.Diag(AS->getBeginLoc(), diag::err_musttail_scope);
        S.Diag(Scopes[I].Loc, Scopes[I].OutDiag);
      }
    }
  }
}

const Attr *JumpScopeChecker::GetMustTailAttr(AttributedStmt *AS) {
  ArrayRef<const Attr *> Attrs = AS->getAttrs();
  const auto *Iter =
      llvm::find_if(Attrs, [](const Attr *A) { return isa<MustTailAttr>(A); });
  return Iter != Attrs.end() ? *Iter : nullptr;
}

void Sema::DiagnoseInvalidJumps(Stmt *Body) {
  (void)JumpScopeChecker(Body, *this);
}
