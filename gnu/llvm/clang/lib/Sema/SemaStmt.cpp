//===--- SemaStmt.cpp - Semantic Analysis for Statements ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for statements.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;
using namespace sema;

StmtResult Sema::ActOnExprStmt(ExprResult FE, bool DiscardedValue) {
  if (FE.isInvalid())
    return StmtError();

  FE = ActOnFinishFullExpr(FE.get(), FE.get()->getExprLoc(), DiscardedValue);
  if (FE.isInvalid())
    return StmtError();

  // C99 6.8.3p2: The expression in an expression statement is evaluated as a
  // void expression for its side effects.  Conversion to void allows any
  // operand, even incomplete types.

  // Same thing in for stmt first clause (when expr) and third clause.
  return StmtResult(FE.getAs<Stmt>());
}


StmtResult Sema::ActOnExprStmtError() {
  DiscardCleanupsInEvaluationContext();
  return StmtError();
}

StmtResult Sema::ActOnNullStmt(SourceLocation SemiLoc,
                               bool HasLeadingEmptyMacro) {
  return new (Context) NullStmt(SemiLoc, HasLeadingEmptyMacro);
}

StmtResult Sema::ActOnDeclStmt(DeclGroupPtrTy dg, SourceLocation StartLoc,
                               SourceLocation EndLoc) {
  DeclGroupRef DG = dg.get();

  // If we have an invalid decl, just return an error.
  if (DG.isNull()) return StmtError();

  return new (Context) DeclStmt(DG, StartLoc, EndLoc);
}

void Sema::ActOnForEachDeclStmt(DeclGroupPtrTy dg) {
  DeclGroupRef DG = dg.get();

  // If we don't have a declaration, or we have an invalid declaration,
  // just return.
  if (DG.isNull() || !DG.isSingleDecl())
    return;

  Decl *decl = DG.getSingleDecl();
  if (!decl || decl->isInvalidDecl())
    return;

  // Only variable declarations are permitted.
  VarDecl *var = dyn_cast<VarDecl>(decl);
  if (!var) {
    Diag(decl->getLocation(), diag::err_non_variable_decl_in_for);
    decl->setInvalidDecl();
    return;
  }

  // foreach variables are never actually initialized in the way that
  // the parser came up with.
  var->setInit(nullptr);

  // In ARC, we don't need to retain the iteration variable of a fast
  // enumeration loop.  Rather than actually trying to catch that
  // during declaration processing, we remove the consequences here.
  if (getLangOpts().ObjCAutoRefCount) {
    QualType type = var->getType();

    // Only do this if we inferred the lifetime.  Inferred lifetime
    // will show up as a local qualifier because explicit lifetime
    // should have shown up as an AttributedType instead.
    if (type.getLocalQualifiers().getObjCLifetime() == Qualifiers::OCL_Strong) {
      // Add 'const' and mark the variable as pseudo-strong.
      var->setType(type.withConst());
      var->setARCPseudoStrong(true);
    }
  }
}

/// Diagnose unused comparisons, both builtin and overloaded operators.
/// For '==' and '!=', suggest fixits for '=' or '|='.
///
/// Adding a cast to void (or other expression wrappers) will prevent the
/// warning from firing.
static bool DiagnoseUnusedComparison(Sema &S, const Expr *E) {
  SourceLocation Loc;
  bool CanAssign;
  enum { Equality, Inequality, Relational, ThreeWay } Kind;

  if (const BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (!Op->isComparisonOp())
      return false;

    if (Op->getOpcode() == BO_EQ)
      Kind = Equality;
    else if (Op->getOpcode() == BO_NE)
      Kind = Inequality;
    else if (Op->getOpcode() == BO_Cmp)
      Kind = ThreeWay;
    else {
      assert(Op->isRelationalOp());
      Kind = Relational;
    }
    Loc = Op->getOperatorLoc();
    CanAssign = Op->getLHS()->IgnoreParenImpCasts()->isLValue();
  } else if (const CXXOperatorCallExpr *Op = dyn_cast<CXXOperatorCallExpr>(E)) {
    switch (Op->getOperator()) {
    case OO_EqualEqual:
      Kind = Equality;
      break;
    case OO_ExclaimEqual:
      Kind = Inequality;
      break;
    case OO_Less:
    case OO_Greater:
    case OO_GreaterEqual:
    case OO_LessEqual:
      Kind = Relational;
      break;
    case OO_Spaceship:
      Kind = ThreeWay;
      break;
    default:
      return false;
    }

    Loc = Op->getOperatorLoc();
    CanAssign = Op->getArg(0)->IgnoreParenImpCasts()->isLValue();
  } else {
    // Not a typo-prone comparison.
    return false;
  }

  // Suppress warnings when the operator, suspicious as it may be, comes from
  // a macro expansion.
  if (S.SourceMgr.isMacroBodyExpansion(Loc))
    return false;

  S.Diag(Loc, diag::warn_unused_comparison)
    << (unsigned)Kind << E->getSourceRange();

  // If the LHS is a plausible entity to assign to, provide a fixit hint to
  // correct common typos.
  if (CanAssign) {
    if (Kind == Inequality)
      S.Diag(Loc, diag::note_inequality_comparison_to_or_assign)
        << FixItHint::CreateReplacement(Loc, "|=");
    else if (Kind == Equality)
      S.Diag(Loc, diag::note_equality_comparison_to_assign)
        << FixItHint::CreateReplacement(Loc, "=");
  }

  return true;
}

static bool DiagnoseNoDiscard(Sema &S, const WarnUnusedResultAttr *A,
                              SourceLocation Loc, SourceRange R1,
                              SourceRange R2, bool IsCtor) {
  if (!A)
    return false;
  StringRef Msg = A->getMessage();

  if (Msg.empty()) {
    if (IsCtor)
      return S.Diag(Loc, diag::warn_unused_constructor) << A << R1 << R2;
    return S.Diag(Loc, diag::warn_unused_result) << A << R1 << R2;
  }

  if (IsCtor)
    return S.Diag(Loc, diag::warn_unused_constructor_msg) << A << Msg << R1
                                                          << R2;
  return S.Diag(Loc, diag::warn_unused_result_msg) << A << Msg << R1 << R2;
}

void Sema::DiagnoseUnusedExprResult(const Stmt *S, unsigned DiagID) {
  if (const LabelStmt *Label = dyn_cast_or_null<LabelStmt>(S))
    return DiagnoseUnusedExprResult(Label->getSubStmt(), DiagID);

  const Expr *E = dyn_cast_or_null<Expr>(S);
  if (!E)
    return;

  // If we are in an unevaluated expression context, then there can be no unused
  // results because the results aren't expected to be used in the first place.
  if (isUnevaluatedContext())
    return;

  SourceLocation ExprLoc = E->IgnoreParenImpCasts()->getExprLoc();
  // In most cases, we don't want to warn if the expression is written in a
  // macro body, or if the macro comes from a system header. If the offending
  // expression is a call to a function with the warn_unused_result attribute,
  // we warn no matter the location. Because of the order in which the various
  // checks need to happen, we factor out the macro-related test here.
  bool ShouldSuppress =
      SourceMgr.isMacroBodyExpansion(ExprLoc) ||
      SourceMgr.isInSystemMacro(ExprLoc);

  const Expr *WarnExpr;
  SourceLocation Loc;
  SourceRange R1, R2;
  if (!E->isUnusedResultAWarning(WarnExpr, Loc, R1, R2, Context))
    return;

  // If this is a GNU statement expression expanded from a macro, it is probably
  // unused because it is a function-like macro that can be used as either an
  // expression or statement.  Don't warn, because it is almost certainly a
  // false positive.
  if (isa<StmtExpr>(E) && Loc.isMacroID())
    return;

  // Check if this is the UNREFERENCED_PARAMETER from the Microsoft headers.
  // That macro is frequently used to suppress "unused parameter" warnings,
  // but its implementation makes clang's -Wunused-value fire.  Prevent this.
  if (isa<ParenExpr>(E->IgnoreImpCasts()) && Loc.isMacroID()) {
    SourceLocation SpellLoc = Loc;
    if (findMacroSpelling(SpellLoc, "UNREFERENCED_PARAMETER"))
      return;
  }

  // Okay, we have an unused result.  Depending on what the base expression is,
  // we might want to make a more specific diagnostic.  Check for one of these
  // cases now.
  if (const FullExpr *Temps = dyn_cast<FullExpr>(E))
    E = Temps->getSubExpr();
  if (const CXXBindTemporaryExpr *TempExpr = dyn_cast<CXXBindTemporaryExpr>(E))
    E = TempExpr->getSubExpr();

  if (DiagnoseUnusedComparison(*this, E))
    return;

  E = WarnExpr;
  if (const auto *Cast = dyn_cast<CastExpr>(E))
    if (Cast->getCastKind() == CK_NoOp ||
        Cast->getCastKind() == CK_ConstructorConversion)
      E = Cast->getSubExpr()->IgnoreImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (E->getType()->isVoidType())
      return;

    if (DiagnoseNoDiscard(*this, cast_or_null<WarnUnusedResultAttr>(
                                     CE->getUnusedResultAttr(Context)),
                          Loc, R1, R2, /*isCtor=*/false))
      return;

    // If the callee has attribute pure, const, or warn_unused_result, warn with
    // a more specific message to make it clear what is happening. If the call
    // is written in a macro body, only warn if it has the warn_unused_result
    // attribute.
    if (const Decl *FD = CE->getCalleeDecl()) {
      if (ShouldSuppress)
        return;
      if (FD->hasAttr<PureAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "pure";
        return;
      }
      if (FD->hasAttr<ConstAttr>()) {
        Diag(Loc, diag::warn_unused_call) << R1 << R2 << "const";
        return;
      }
    }
  } else if (const auto *CE = dyn_cast<CXXConstructExpr>(E)) {
    if (const CXXConstructorDecl *Ctor = CE->getConstructor()) {
      const auto *A = Ctor->getAttr<WarnUnusedResultAttr>();
      A = A ? A : Ctor->getParent()->getAttr<WarnUnusedResultAttr>();
      if (DiagnoseNoDiscard(*this, A, Loc, R1, R2, /*isCtor=*/true))
        return;
    }
  } else if (const auto *ILE = dyn_cast<InitListExpr>(E)) {
    if (const TagDecl *TD = ILE->getType()->getAsTagDecl()) {

      if (DiagnoseNoDiscard(*this, TD->getAttr<WarnUnusedResultAttr>(), Loc, R1,
                            R2, /*isCtor=*/false))
        return;
    }
  } else if (ShouldSuppress)
    return;

  E = WarnExpr;
  if (const ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(E)) {
    if (getLangOpts().ObjCAutoRefCount && ME->isDelegateInitCall()) {
      Diag(Loc, diag::err_arc_unused_init_message) << R1;
      return;
    }
    const ObjCMethodDecl *MD = ME->getMethodDecl();
    if (MD) {
      if (DiagnoseNoDiscard(*this, MD->getAttr<WarnUnusedResultAttr>(), Loc, R1,
                            R2, /*isCtor=*/false))
        return;
    }
  } else if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    const Expr *Source = POE->getSyntacticForm();
    // Handle the actually selected call of an OpenMP specialized call.
    if (LangOpts.OpenMP && isa<CallExpr>(Source) &&
        POE->getNumSemanticExprs() == 1 &&
        isa<CallExpr>(POE->getSemanticExpr(0)))
      return DiagnoseUnusedExprResult(POE->getSemanticExpr(0), DiagID);
    if (isa<ObjCSubscriptRefExpr>(Source))
      DiagID = diag::warn_unused_container_subscript_expr;
    else if (isa<ObjCPropertyRefExpr>(Source))
      DiagID = diag::warn_unused_property_expr;
  } else if (const CXXFunctionalCastExpr *FC
                                       = dyn_cast<CXXFunctionalCastExpr>(E)) {
    const Expr *E = FC->getSubExpr();
    if (const CXXBindTemporaryExpr *TE = dyn_cast<CXXBindTemporaryExpr>(E))
      E = TE->getSubExpr();
    if (isa<CXXTemporaryObjectExpr>(E))
      return;
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(E))
      if (const CXXRecordDecl *RD = CE->getType()->getAsCXXRecordDecl())
        if (!RD->getAttr<WarnUnusedAttr>())
          return;
  }
  // Diagnose "(void*) blah" as a typo for "(void) blah".
  else if (const CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(E)) {
    TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
    QualType T = TI->getType();

    // We really do want to use the non-canonical type here.
    if (T == Context.VoidPtrTy) {
      PointerTypeLoc TL = TI->getTypeLoc().castAs<PointerTypeLoc>();

      Diag(Loc, diag::warn_unused_voidptr)
        << FixItHint::CreateRemoval(TL.getStarLoc());
      return;
    }
  }

  // Tell the user to assign it into a variable to force a volatile load if this
  // isn't an array.
  if (E->isGLValue() && E->getType().isVolatileQualified() &&
      !E->getType()->isArrayType()) {
    Diag(Loc, diag::warn_unused_volatile) << R1 << R2;
    return;
  }

  // Do not diagnose use of a comma operator in a SFINAE context because the
  // type of the left operand could be used for SFINAE, so technically it is
  // *used*.
  if (DiagID != diag::warn_unused_comma_left_operand || !isSFINAEContext())
    DiagIfReachable(Loc, S ? llvm::ArrayRef(S) : std::nullopt,
                    PDiag(DiagID) << R1 << R2);
}

void Sema::ActOnStartOfCompoundStmt(bool IsStmtExpr) {
  PushCompoundScope(IsStmtExpr);
}

void Sema::ActOnAfterCompoundStatementLeadingPragmas() {
  if (getCurFPFeatures().isFPConstrained()) {
    FunctionScopeInfo *FSI = getCurFunction();
    assert(FSI);
    FSI->setUsesFPIntrin();
  }
}

void Sema::ActOnFinishOfCompoundStmt() {
  PopCompoundScope();
}

sema::CompoundScopeInfo &Sema::getCurCompoundScope() const {
  return getCurFunction()->CompoundScopes.back();
}

StmtResult Sema::ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                                   ArrayRef<Stmt *> Elts, bool isStmtExpr) {
  const unsigned NumElts = Elts.size();

  // If we're in C mode, check that we don't have any decls after stmts.  If
  // so, emit an extension diagnostic in C89 and potentially a warning in later
  // versions.
  const unsigned MixedDeclsCodeID = getLangOpts().C99
                                        ? diag::warn_mixed_decls_code
                                        : diag::ext_mixed_decls_code;
  if (!getLangOpts().CPlusPlus && !Diags.isIgnored(MixedDeclsCodeID, L)) {
    // Note that __extension__ can be around a decl.
    unsigned i = 0;
    // Skip over all declarations.
    for (; i != NumElts && isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    // We found the end of the list or a statement.  Scan for another declstmt.
    for (; i != NumElts && !isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    if (i != NumElts) {
      Decl *D = *cast<DeclStmt>(Elts[i])->decl_begin();
      Diag(D->getLocation(), MixedDeclsCodeID);
    }
  }

  // Check for suspicious empty body (null statement) in `for' and `while'
  // statements.  Don't do anything for template instantiations, this just adds
  // noise.
  if (NumElts != 0 && !CurrentInstantiationScope &&
      getCurCompoundScope().HasEmptyLoopBodies) {
    for (unsigned i = 0; i != NumElts - 1; ++i)
      DiagnoseEmptyLoopBody(Elts[i], Elts[i + 1]);
  }

  // Calculate difference between FP options in this compound statement and in
  // the enclosing one. If this is a function body, take the difference against
  // default options. In this case the difference will indicate options that are
  // changed upon entry to the statement.
  FPOptions FPO = (getCurFunction()->CompoundScopes.size() == 1)
                      ? FPOptions(getLangOpts())
                      : getCurCompoundScope().InitialFPFeatures;
  FPOptionsOverride FPDiff = getCurFPFeatures().getChangesFrom(FPO);

  return CompoundStmt::Create(Context, Elts, FPDiff, L, R);
}

ExprResult
Sema::ActOnCaseExpr(SourceLocation CaseLoc, ExprResult Val) {
  if (!Val.get())
    return Val;

  if (DiagnoseUnexpandedParameterPack(Val.get()))
    return ExprError();

  // If we're not inside a switch, let the 'case' statement handling diagnose
  // this. Just clean up after the expression as best we can.
  if (getCurFunction()->SwitchStack.empty())
    return ActOnFinishFullExpr(Val.get(), Val.get()->getExprLoc(), false,
                               getLangOpts().CPlusPlus11);

  Expr *CondExpr =
      getCurFunction()->SwitchStack.back().getPointer()->getCond();
  if (!CondExpr)
    return ExprError();
  QualType CondType = CondExpr->getType();

  auto CheckAndFinish = [&](Expr *E) {
    if (CondType->isDependentType() || E->isTypeDependent())
      return ExprResult(E);

    if (getLangOpts().CPlusPlus11) {
      // C++11 [stmt.switch]p2: the constant-expression shall be a converted
      // constant expression of the promoted type of the switch condition.
      llvm::APSInt TempVal;
      return CheckConvertedConstantExpression(E, CondType, TempVal,
                                              CCEK_CaseValue);
    }

    ExprResult ER = E;
    if (!E->isValueDependent())
      ER = VerifyIntegerConstantExpression(E, AllowFold);
    if (!ER.isInvalid())
      ER = DefaultLvalueConversion(ER.get());
    if (!ER.isInvalid())
      ER = ImpCastExprToType(ER.get(), CondType, CK_IntegralCast);
    if (!ER.isInvalid())
      ER = ActOnFinishFullExpr(ER.get(), ER.get()->getExprLoc(), false);
    return ER;
  };

  ExprResult Converted = CorrectDelayedTyposInExpr(
      Val, /*InitDecl=*/nullptr, /*RecoverUncorrectedTypos=*/false,
      CheckAndFinish);
  if (Converted.get() == Val.get())
    Converted = CheckAndFinish(Val.get());
  return Converted;
}

StmtResult
Sema::ActOnCaseStmt(SourceLocation CaseLoc, ExprResult LHSVal,
                    SourceLocation DotDotDotLoc, ExprResult RHSVal,
                    SourceLocation ColonLoc) {
  assert((LHSVal.isInvalid() || LHSVal.get()) && "missing LHS value");
  assert((DotDotDotLoc.isInvalid() ? RHSVal.isUnset()
                                   : RHSVal.isInvalid() || RHSVal.get()) &&
         "missing RHS value");

  if (getCurFunction()->SwitchStack.empty()) {
    Diag(CaseLoc, diag::err_case_not_in_switch);
    return StmtError();
  }

  if (LHSVal.isInvalid() || RHSVal.isInvalid()) {
    getCurFunction()->SwitchStack.back().setInt(true);
    return StmtError();
  }

  if (LangOpts.OpenACC &&
      getCurScope()->isInOpenACCComputeConstructScope(Scope::SwitchScope)) {
    Diag(CaseLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*branch*/ 0 << /*into*/ 1;
    return StmtError();
  }

  auto *CS = CaseStmt::Create(Context, LHSVal.get(), RHSVal.get(),
                              CaseLoc, DotDotDotLoc, ColonLoc);
  getCurFunction()->SwitchStack.back().getPointer()->addSwitchCase(CS);
  return CS;
}

void Sema::ActOnCaseStmtBody(Stmt *S, Stmt *SubStmt) {
  cast<CaseStmt>(S)->setSubStmt(SubStmt);
}

StmtResult
Sema::ActOnDefaultStmt(SourceLocation DefaultLoc, SourceLocation ColonLoc,
                       Stmt *SubStmt, Scope *CurScope) {
  if (getCurFunction()->SwitchStack.empty()) {
    Diag(DefaultLoc, diag::err_default_not_in_switch);
    return SubStmt;
  }

  if (LangOpts.OpenACC &&
      getCurScope()->isInOpenACCComputeConstructScope(Scope::SwitchScope)) {
    Diag(DefaultLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*branch*/ 0 << /*into*/ 1;
    return StmtError();
  }

  DefaultStmt *DS = new (Context) DefaultStmt(DefaultLoc, ColonLoc, SubStmt);
  getCurFunction()->SwitchStack.back().getPointer()->addSwitchCase(DS);
  return DS;
}

StmtResult
Sema::ActOnLabelStmt(SourceLocation IdentLoc, LabelDecl *TheDecl,
                     SourceLocation ColonLoc, Stmt *SubStmt) {
  // If the label was multiply defined, reject it now.
  if (TheDecl->getStmt()) {
    Diag(IdentLoc, diag::err_redefinition_of_label) << TheDecl->getDeclName();
    Diag(TheDecl->getLocation(), diag::note_previous_definition);
    return SubStmt;
  }

  ReservedIdentifierStatus Status = TheDecl->isReserved(getLangOpts());
  if (isReservedInAllContexts(Status) &&
      !Context.getSourceManager().isInSystemHeader(IdentLoc))
    Diag(IdentLoc, diag::warn_reserved_extern_symbol)
        << TheDecl << static_cast<int>(Status);

  // If this label is in a compute construct scope, we need to make sure we
  // check gotos in/out.
  if (getCurScope()->isInOpenACCComputeConstructScope())
    setFunctionHasBranchProtectedScope();

  // Otherwise, things are good.  Fill in the declaration and return it.
  LabelStmt *LS = new (Context) LabelStmt(IdentLoc, TheDecl, SubStmt);
  TheDecl->setStmt(LS);
  if (!TheDecl->isGnuLocal()) {
    TheDecl->setLocStart(IdentLoc);
    if (!TheDecl->isMSAsmLabel()) {
      // Don't update the location of MS ASM labels.  These will result in
      // a diagnostic, and changing the location here will mess that up.
      TheDecl->setLocation(IdentLoc);
    }
  }
  return LS;
}

StmtResult Sema::BuildAttributedStmt(SourceLocation AttrsLoc,
                                     ArrayRef<const Attr *> Attrs,
                                     Stmt *SubStmt) {
  // FIXME: this code should move when a planned refactoring around statement
  // attributes lands.
  for (const auto *A : Attrs) {
    if (A->getKind() == attr::MustTail) {
      if (!checkAndRewriteMustTailAttr(SubStmt, *A)) {
        return SubStmt;
      }
      setFunctionHasMustTail();
    }
  }

  return AttributedStmt::Create(Context, AttrsLoc, Attrs, SubStmt);
}

StmtResult Sema::ActOnAttributedStmt(const ParsedAttributes &Attrs,
                                     Stmt *SubStmt) {
  SmallVector<const Attr *, 1> SemanticAttrs;
  ProcessStmtAttributes(SubStmt, Attrs, SemanticAttrs);
  if (!SemanticAttrs.empty())
    return BuildAttributedStmt(Attrs.Range.getBegin(), SemanticAttrs, SubStmt);
  // If none of the attributes applied, that's fine, we can recover by
  // returning the substatement directly instead of making an AttributedStmt
  // with no attributes on it.
  return SubStmt;
}

bool Sema::checkAndRewriteMustTailAttr(Stmt *St, const Attr &MTA) {
  ReturnStmt *R = cast<ReturnStmt>(St);
  Expr *E = R->getRetValue();

  if (CurContext->isDependentContext() || (E && E->isInstantiationDependent()))
    // We have to suspend our check until template instantiation time.
    return true;

  if (!checkMustTailAttr(St, MTA))
    return false;

  // FIXME: Replace Expr::IgnoreImplicitAsWritten() with this function.
  // Currently it does not skip implicit constructors in an initialization
  // context.
  auto IgnoreImplicitAsWritten = [](Expr *E) -> Expr * {
    return IgnoreExprNodes(E, IgnoreImplicitAsWrittenSingleStep,
                           IgnoreElidableImplicitConstructorSingleStep);
  };

  // Now that we have verified that 'musttail' is valid here, rewrite the
  // return value to remove all implicit nodes, but retain parentheses.
  R->setRetValue(IgnoreImplicitAsWritten(E));
  return true;
}

bool Sema::checkMustTailAttr(const Stmt *St, const Attr &MTA) {
  assert(!CurContext->isDependentContext() &&
         "musttail cannot be checked from a dependent context");

  // FIXME: Add Expr::IgnoreParenImplicitAsWritten() with this definition.
  auto IgnoreParenImplicitAsWritten = [](const Expr *E) -> const Expr * {
    return IgnoreExprNodes(const_cast<Expr *>(E), IgnoreParensSingleStep,
                           IgnoreImplicitAsWrittenSingleStep,
                           IgnoreElidableImplicitConstructorSingleStep);
  };

  const Expr *E = cast<ReturnStmt>(St)->getRetValue();
  const auto *CE = dyn_cast_or_null<CallExpr>(IgnoreParenImplicitAsWritten(E));

  if (!CE) {
    Diag(St->getBeginLoc(), diag::err_musttail_needs_call) << &MTA;
    return false;
  }

  if (const auto *EWC = dyn_cast<ExprWithCleanups>(E)) {
    if (EWC->cleanupsHaveSideEffects()) {
      Diag(St->getBeginLoc(), diag::err_musttail_needs_trivial_args) << &MTA;
      return false;
    }
  }

  // We need to determine the full function type (including "this" type, if any)
  // for both caller and callee.
  struct FuncType {
    enum {
      ft_non_member,
      ft_static_member,
      ft_non_static_member,
      ft_pointer_to_member,
    } MemberType = ft_non_member;

    QualType This;
    const FunctionProtoType *Func;
    const CXXMethodDecl *Method = nullptr;
  } CallerType, CalleeType;

  auto GetMethodType = [this, St, MTA](const CXXMethodDecl *CMD, FuncType &Type,
                                       bool IsCallee) -> bool {
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(CMD)) {
      Diag(St->getBeginLoc(), diag::err_musttail_structors_forbidden)
          << IsCallee << isa<CXXDestructorDecl>(CMD);
      if (IsCallee)
        Diag(CMD->getBeginLoc(), diag::note_musttail_structors_forbidden)
            << isa<CXXDestructorDecl>(CMD);
      Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
      return false;
    }
    if (CMD->isStatic())
      Type.MemberType = FuncType::ft_static_member;
    else {
      Type.This = CMD->getFunctionObjectParameterType();
      Type.MemberType = FuncType::ft_non_static_member;
    }
    Type.Func = CMD->getType()->castAs<FunctionProtoType>();
    return true;
  };

  const auto *CallerDecl = dyn_cast<FunctionDecl>(CurContext);

  // Find caller function signature.
  if (!CallerDecl) {
    int ContextType;
    if (isa<BlockDecl>(CurContext))
      ContextType = 0;
    else if (isa<ObjCMethodDecl>(CurContext))
      ContextType = 1;
    else
      ContextType = 2;
    Diag(St->getBeginLoc(), diag::err_musttail_forbidden_from_this_context)
        << &MTA << ContextType;
    return false;
  } else if (const auto *CMD = dyn_cast<CXXMethodDecl>(CurContext)) {
    // Caller is a class/struct method.
    if (!GetMethodType(CMD, CallerType, false))
      return false;
  } else {
    // Caller is a non-method function.
    CallerType.Func = CallerDecl->getType()->getAs<FunctionProtoType>();
  }

  const Expr *CalleeExpr = CE->getCallee()->IgnoreParens();
  const auto *CalleeBinOp = dyn_cast<BinaryOperator>(CalleeExpr);
  SourceLocation CalleeLoc = CE->getCalleeDecl()
                                 ? CE->getCalleeDecl()->getBeginLoc()
                                 : St->getBeginLoc();

  // Find callee function signature.
  if (const CXXMethodDecl *CMD =
          dyn_cast_or_null<CXXMethodDecl>(CE->getCalleeDecl())) {
    // Call is: obj.method(), obj->method(), functor(), etc.
    if (!GetMethodType(CMD, CalleeType, true))
      return false;
  } else if (CalleeBinOp && CalleeBinOp->isPtrMemOp()) {
    // Call is: obj->*method_ptr or obj.*method_ptr
    const auto *MPT =
        CalleeBinOp->getRHS()->getType()->castAs<MemberPointerType>();
    CalleeType.This = QualType(MPT->getClass(), 0);
    CalleeType.Func = MPT->getPointeeType()->castAs<FunctionProtoType>();
    CalleeType.MemberType = FuncType::ft_pointer_to_member;
  } else if (isa<CXXPseudoDestructorExpr>(CalleeExpr)) {
    Diag(St->getBeginLoc(), diag::err_musttail_structors_forbidden)
        << /* IsCallee = */ 1 << /* IsDestructor = */ 1;
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  } else {
    // Non-method function.
    CalleeType.Func =
        CalleeExpr->getType()->getPointeeType()->getAs<FunctionProtoType>();
  }

  // Both caller and callee must have a prototype (no K&R declarations).
  if (!CalleeType.Func || !CallerType.Func) {
    Diag(St->getBeginLoc(), diag::err_musttail_needs_prototype) << &MTA;
    if (!CalleeType.Func && CE->getDirectCallee()) {
      Diag(CE->getDirectCallee()->getBeginLoc(),
           diag::note_musttail_fix_non_prototype);
    }
    if (!CallerType.Func)
      Diag(CallerDecl->getBeginLoc(), diag::note_musttail_fix_non_prototype);
    return false;
  }

  // Caller and callee must have matching calling conventions.
  //
  // Some calling conventions are physically capable of supporting tail calls
  // even if the function types don't perfectly match. LLVM is currently too
  // strict to allow this, but if LLVM added support for this in the future, we
  // could exit early here and skip the remaining checks if the functions are
  // using such a calling convention.
  if (CallerType.Func->getCallConv() != CalleeType.Func->getCallConv()) {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl()))
      Diag(St->getBeginLoc(), diag::err_musttail_callconv_mismatch)
          << true << ND->getDeclName();
    else
      Diag(St->getBeginLoc(), diag::err_musttail_callconv_mismatch) << false;
    Diag(CalleeLoc, diag::note_musttail_callconv_mismatch)
        << FunctionType::getNameForCallConv(CallerType.Func->getCallConv())
        << FunctionType::getNameForCallConv(CalleeType.Func->getCallConv());
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  }

  if (CalleeType.Func->isVariadic() || CallerType.Func->isVariadic()) {
    Diag(St->getBeginLoc(), diag::err_musttail_no_variadic) << &MTA;
    return false;
  }

  const auto *CalleeDecl = CE->getCalleeDecl();
  if (CalleeDecl && CalleeDecl->hasAttr<CXX11NoReturnAttr>()) {
    Diag(St->getBeginLoc(), diag::err_musttail_no_return) << &MTA;
    return false;
  }

  // Caller and callee must match in whether they have a "this" parameter.
  if (CallerType.This.isNull() != CalleeType.This.isNull()) {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl())) {
      Diag(St->getBeginLoc(), diag::err_musttail_member_mismatch)
          << CallerType.MemberType << CalleeType.MemberType << true
          << ND->getDeclName();
      Diag(CalleeLoc, diag::note_musttail_callee_defined_here)
          << ND->getDeclName();
    } else
      Diag(St->getBeginLoc(), diag::err_musttail_member_mismatch)
          << CallerType.MemberType << CalleeType.MemberType << false;
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  }

  auto CheckTypesMatch = [this](FuncType CallerType, FuncType CalleeType,
                                PartialDiagnostic &PD) -> bool {
    enum {
      ft_different_class,
      ft_parameter_arity,
      ft_parameter_mismatch,
      ft_return_type,
    };

    auto DoTypesMatch = [this, &PD](QualType A, QualType B,
                                    unsigned Select) -> bool {
      if (!Context.hasSimilarType(A, B)) {
        PD << Select << A.getUnqualifiedType() << B.getUnqualifiedType();
        return false;
      }
      return true;
    };

    if (!CallerType.This.isNull() &&
        !DoTypesMatch(CallerType.This, CalleeType.This, ft_different_class))
      return false;

    if (!DoTypesMatch(CallerType.Func->getReturnType(),
                      CalleeType.Func->getReturnType(), ft_return_type))
      return false;

    if (CallerType.Func->getNumParams() != CalleeType.Func->getNumParams()) {
      PD << ft_parameter_arity << CallerType.Func->getNumParams()
         << CalleeType.Func->getNumParams();
      return false;
    }

    ArrayRef<QualType> CalleeParams = CalleeType.Func->getParamTypes();
    ArrayRef<QualType> CallerParams = CallerType.Func->getParamTypes();
    size_t N = CallerType.Func->getNumParams();
    for (size_t I = 0; I < N; I++) {
      if (!DoTypesMatch(CalleeParams[I], CallerParams[I],
                        ft_parameter_mismatch)) {
        PD << static_cast<int>(I) + 1;
        return false;
      }
    }

    return true;
  };

  PartialDiagnostic PD = PDiag(diag::note_musttail_mismatch);
  if (!CheckTypesMatch(CallerType, CalleeType, PD)) {
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl()))
      Diag(St->getBeginLoc(), diag::err_musttail_mismatch)
          << true << ND->getDeclName();
    else
      Diag(St->getBeginLoc(), diag::err_musttail_mismatch) << false;
    Diag(CalleeLoc, PD);
    Diag(MTA.getLocation(), diag::note_tail_call_required) << &MTA;
    return false;
  }

  return true;
}

namespace {
class CommaVisitor : public EvaluatedExprVisitor<CommaVisitor> {
  typedef EvaluatedExprVisitor<CommaVisitor> Inherited;
  Sema &SemaRef;
public:
  CommaVisitor(Sema &SemaRef) : Inherited(SemaRef.Context), SemaRef(SemaRef) {}
  void VisitBinaryOperator(BinaryOperator *E) {
    if (E->getOpcode() == BO_Comma)
      SemaRef.DiagnoseCommaOperator(E->getLHS(), E->getExprLoc());
    EvaluatedExprVisitor<CommaVisitor>::VisitBinaryOperator(E);
  }
};
}

StmtResult Sema::ActOnIfStmt(SourceLocation IfLoc,
                             IfStatementKind StatementKind,
                             SourceLocation LParenLoc, Stmt *InitStmt,
                             ConditionResult Cond, SourceLocation RParenLoc,
                             Stmt *thenStmt, SourceLocation ElseLoc,
                             Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  bool ConstevalOrNegatedConsteval =
      StatementKind == IfStatementKind::ConstevalNonNegated ||
      StatementKind == IfStatementKind::ConstevalNegated;

  Expr *CondExpr = Cond.get().second;
  assert((CondExpr || ConstevalOrNegatedConsteval) &&
         "If statement: missing condition");
  // Only call the CommaVisitor when not C89 due to differences in scope flags.
  if (CondExpr && (getLangOpts().C99 || getLangOpts().CPlusPlus) &&
      !Diags.isIgnored(diag::warn_comma_operator, CondExpr->getExprLoc()))
    CommaVisitor(*this).Visit(CondExpr);

  if (!ConstevalOrNegatedConsteval && !elseStmt)
    DiagnoseEmptyStmtBody(RParenLoc, thenStmt, diag::warn_empty_if_body);

  if (ConstevalOrNegatedConsteval ||
      StatementKind == IfStatementKind::Constexpr) {
    auto DiagnoseLikelihood = [&](const Stmt *S) {
      if (const Attr *A = Stmt::getLikelihoodAttr(S)) {
        Diags.Report(A->getLocation(),
                     diag::warn_attribute_has_no_effect_on_compile_time_if)
            << A << ConstevalOrNegatedConsteval << A->getRange();
        Diags.Report(IfLoc,
                     diag::note_attribute_has_no_effect_on_compile_time_if_here)
            << ConstevalOrNegatedConsteval
            << SourceRange(IfLoc, (ConstevalOrNegatedConsteval
                                       ? thenStmt->getBeginLoc()
                                       : LParenLoc)
                                      .getLocWithOffset(-1));
      }
    };
    DiagnoseLikelihood(thenStmt);
    DiagnoseLikelihood(elseStmt);
  } else {
    std::tuple<bool, const Attr *, const Attr *> LHC =
        Stmt::determineLikelihoodConflict(thenStmt, elseStmt);
    if (std::get<0>(LHC)) {
      const Attr *ThenAttr = std::get<1>(LHC);
      const Attr *ElseAttr = std::get<2>(LHC);
      Diags.Report(ThenAttr->getLocation(),
                   diag::warn_attributes_likelihood_ifstmt_conflict)
          << ThenAttr << ThenAttr->getRange();
      Diags.Report(ElseAttr->getLocation(), diag::note_conflicting_attribute)
          << ElseAttr << ElseAttr->getRange();
    }
  }

  if (ConstevalOrNegatedConsteval) {
    bool Immediate = ExprEvalContexts.back().Context ==
                     ExpressionEvaluationContext::ImmediateFunctionContext;
    if (CurContext->isFunctionOrMethod()) {
      const auto *FD =
          dyn_cast<FunctionDecl>(Decl::castFromDeclContext(CurContext));
      if (FD && FD->isImmediateFunction())
        Immediate = true;
    }
    if (isUnevaluatedContext() || Immediate)
      Diags.Report(IfLoc, diag::warn_consteval_if_always_true) << Immediate;
  }

  return BuildIfStmt(IfLoc, StatementKind, LParenLoc, InitStmt, Cond, RParenLoc,
                     thenStmt, ElseLoc, elseStmt);
}

StmtResult Sema::BuildIfStmt(SourceLocation IfLoc,
                             IfStatementKind StatementKind,
                             SourceLocation LParenLoc, Stmt *InitStmt,
                             ConditionResult Cond, SourceLocation RParenLoc,
                             Stmt *thenStmt, SourceLocation ElseLoc,
                             Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  if (StatementKind != IfStatementKind::Ordinary ||
      isa<ObjCAvailabilityCheckExpr>(Cond.get().second))
    setFunctionHasBranchProtectedScope();

  return IfStmt::Create(Context, IfLoc, StatementKind, InitStmt,
                        Cond.get().first, Cond.get().second, LParenLoc,
                        RParenLoc, thenStmt, ElseLoc, elseStmt);
}

namespace {
  struct CaseCompareFunctor {
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const llvm::APSInt &RHS) {
      return LHS.first < RHS;
    }
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS.first < RHS.first;
    }
    bool operator()(const llvm::APSInt &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS < RHS.first;
    }
  };
}

/// CmpCaseVals - Comparison predicate for sorting case values.
///
static bool CmpCaseVals(const std::pair<llvm::APSInt, CaseStmt*>& lhs,
                        const std::pair<llvm::APSInt, CaseStmt*>& rhs) {
  if (lhs.first < rhs.first)
    return true;

  if (lhs.first == rhs.first &&
      lhs.second->getCaseLoc() < rhs.second->getCaseLoc())
    return true;
  return false;
}

/// CmpEnumVals - Comparison predicate for sorting enumeration values.
///
static bool CmpEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl*>& lhs,
                        const std::pair<llvm::APSInt, EnumConstantDecl*>& rhs)
{
  return lhs.first < rhs.first;
}

/// EqEnumVals - Comparison preficate for uniqing enumeration values.
///
static bool EqEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl*>& lhs,
                       const std::pair<llvm::APSInt, EnumConstantDecl*>& rhs)
{
  return lhs.first == rhs.first;
}

/// GetTypeBeforeIntegralPromotion - Returns the pre-promotion type of
/// potentially integral-promoted expression @p expr.
static QualType GetTypeBeforeIntegralPromotion(const Expr *&E) {
  if (const auto *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr();
  while (const auto *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() != CK_IntegralCast) break;
    E = ImpCast->getSubExpr();
  }
  return E->getType();
}

ExprResult Sema::CheckSwitchCondition(SourceLocation SwitchLoc, Expr *Cond) {
  class SwitchConvertDiagnoser : public ICEConvertDiagnoser {
    Expr *Cond;

  public:
    SwitchConvertDiagnoser(Expr *Cond)
        : ICEConvertDiagnoser(/*AllowScopedEnumerations*/true, false, true),
          Cond(Cond) {}

    SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                         QualType T) override {
      return S.Diag(Loc, diag::err_typecheck_statement_requires_integer) << T;
    }

    SemaDiagnosticBuilder diagnoseIncomplete(
        Sema &S, SourceLocation Loc, QualType T) override {
      return S.Diag(Loc, diag::err_switch_incomplete_class_type)
               << T << Cond->getSourceRange();
    }

    SemaDiagnosticBuilder diagnoseExplicitConv(
        Sema &S, SourceLocation Loc, QualType T, QualType ConvTy) override {
      return S.Diag(Loc, diag::err_switch_explicit_conversion) << T << ConvTy;
    }

    SemaDiagnosticBuilder noteExplicitConv(
        Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_switch_conversion)
        << ConvTy->isEnumeralType() << ConvTy;
    }

    SemaDiagnosticBuilder diagnoseAmbiguous(Sema &S, SourceLocation Loc,
                                            QualType T) override {
      return S.Diag(Loc, diag::err_switch_multiple_conversions) << T;
    }

    SemaDiagnosticBuilder noteAmbiguous(
        Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_switch_conversion)
      << ConvTy->isEnumeralType() << ConvTy;
    }

    SemaDiagnosticBuilder diagnoseConversion(
        Sema &S, SourceLocation Loc, QualType T, QualType ConvTy) override {
      llvm_unreachable("conversion functions are permitted");
    }
  } SwitchDiagnoser(Cond);

  ExprResult CondResult =
      PerformContextualImplicitConversion(SwitchLoc, Cond, SwitchDiagnoser);
  if (CondResult.isInvalid())
    return ExprError();

  // FIXME: PerformContextualImplicitConversion doesn't always tell us if it
  // failed and produced a diagnostic.
  Cond = CondResult.get();
  if (!Cond->isTypeDependent() &&
      !Cond->getType()->isIntegralOrEnumerationType())
    return ExprError();

  // C99 6.8.4.2p5 - Integer promotions are performed on the controlling expr.
  return UsualUnaryConversions(Cond);
}

StmtResult Sema::ActOnStartOfSwitchStmt(SourceLocation SwitchLoc,
                                        SourceLocation LParenLoc,
                                        Stmt *InitStmt, ConditionResult Cond,
                                        SourceLocation RParenLoc) {
  Expr *CondExpr = Cond.get().second;
  assert((Cond.isInvalid() || CondExpr) && "switch with no condition");

  if (CondExpr && !CondExpr->isTypeDependent()) {
    // We have already converted the expression to an integral or enumeration
    // type, when we parsed the switch condition. There are cases where we don't
    // have an appropriate type, e.g. a typo-expr Cond was corrected to an
    // inappropriate-type expr, we just return an error.
    if (!CondExpr->getType()->isIntegralOrEnumerationType())
      return StmtError();
    if (CondExpr->isKnownToHaveBooleanValue()) {
      // switch(bool_expr) {...} is often a programmer error, e.g.
      //   switch(n && mask) { ... }  // Doh - should be "n & mask".
      // One can always use an if statement instead of switch(bool_expr).
      Diag(SwitchLoc, diag::warn_bool_switch_condition)
          << CondExpr->getSourceRange();
    }
  }

  setFunctionHasBranchIntoScope();

  auto *SS = SwitchStmt::Create(Context, InitStmt, Cond.get().first, CondExpr,
                                LParenLoc, RParenLoc);
  getCurFunction()->SwitchStack.push_back(
      FunctionScopeInfo::SwitchInfo(SS, false));
  return SS;
}

static void AdjustAPSInt(llvm::APSInt &Val, unsigned BitWidth, bool IsSigned) {
  Val = Val.extOrTrunc(BitWidth);
  Val.setIsSigned(IsSigned);
}

/// Check the specified case value is in range for the given unpromoted switch
/// type.
static void checkCaseValue(Sema &S, SourceLocation Loc, const llvm::APSInt &Val,
                           unsigned UnpromotedWidth, bool UnpromotedSign) {
  // In C++11 onwards, this is checked by the language rules.
  if (S.getLangOpts().CPlusPlus11)
    return;

  // If the case value was signed and negative and the switch expression is
  // unsigned, don't bother to warn: this is implementation-defined behavior.
  // FIXME: Introduce a second, default-ignored warning for this case?
  if (UnpromotedWidth < Val.getBitWidth()) {
    llvm::APSInt ConvVal(Val);
    AdjustAPSInt(ConvVal, UnpromotedWidth, UnpromotedSign);
    AdjustAPSInt(ConvVal, Val.getBitWidth(), Val.isSigned());
    // FIXME: Use different diagnostics for overflow  in conversion to promoted
    // type versus "switch expression cannot have this value". Use proper
    // IntRange checking rather than just looking at the unpromoted type here.
    if (ConvVal != Val)
      S.Diag(Loc, diag::warn_case_value_overflow) << toString(Val, 10)
                                                  << toString(ConvVal, 10);
  }
}

typedef SmallVector<std::pair<llvm::APSInt, EnumConstantDecl*>, 64> EnumValsTy;

/// Returns true if we should emit a diagnostic about this case expression not
/// being a part of the enum used in the switch controlling expression.
static bool ShouldDiagnoseSwitchCaseNotInEnum(const Sema &S,
                                              const EnumDecl *ED,
                                              const Expr *CaseExpr,
                                              EnumValsTy::iterator &EI,
                                              EnumValsTy::iterator &EIEnd,
                                              const llvm::APSInt &Val) {
  if (!ED->isClosed())
    return false;

  if (const DeclRefExpr *DRE =
          dyn_cast<DeclRefExpr>(CaseExpr->IgnoreParenImpCasts())) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      QualType VarType = VD->getType();
      QualType EnumType = S.Context.getTypeDeclType(ED);
      if (VD->hasGlobalStorage() && VarType.isConstQualified() &&
          S.Context.hasSameUnqualifiedType(EnumType, VarType))
        return false;
    }
  }

  if (ED->hasAttr<FlagEnumAttr>())
    return !S.IsValueInFlagEnum(ED, Val, false);

  while (EI != EIEnd && EI->first < Val)
    EI++;

  if (EI != EIEnd && EI->first == Val)
    return false;

  return true;
}

static void checkEnumTypesInSwitchStmt(Sema &S, const Expr *Cond,
                                       const Expr *Case) {
  QualType CondType = Cond->getType();
  QualType CaseType = Case->getType();

  const EnumType *CondEnumType = CondType->getAs<EnumType>();
  const EnumType *CaseEnumType = CaseType->getAs<EnumType>();
  if (!CondEnumType || !CaseEnumType)
    return;

  // Ignore anonymous enums.
  if (!CondEnumType->getDecl()->getIdentifier() &&
      !CondEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;
  if (!CaseEnumType->getDecl()->getIdentifier() &&
      !CaseEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;

  if (S.Context.hasSameUnqualifiedType(CondType, CaseType))
    return;

  S.Diag(Case->getExprLoc(), diag::warn_comparison_of_mixed_enum_types_switch)
      << CondType << CaseType << Cond->getSourceRange()
      << Case->getSourceRange();
}

StmtResult
Sema::ActOnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                            Stmt *BodyStmt) {
  SwitchStmt *SS = cast<SwitchStmt>(Switch);
  bool CaseListIsIncomplete = getCurFunction()->SwitchStack.back().getInt();
  assert(SS == getCurFunction()->SwitchStack.back().getPointer() &&
         "switch stack missing push/pop!");

  getCurFunction()->SwitchStack.pop_back();

  if (!BodyStmt) return StmtError();
  SS->setBody(BodyStmt, SwitchLoc);

  Expr *CondExpr = SS->getCond();
  if (!CondExpr) return StmtError();

  QualType CondType = CondExpr->getType();

  // C++ 6.4.2.p2:
  // Integral promotions are performed (on the switch condition).
  //
  // A case value unrepresentable by the original switch condition
  // type (before the promotion) doesn't make sense, even when it can
  // be represented by the promoted type.  Therefore we need to find
  // the pre-promotion type of the switch condition.
  const Expr *CondExprBeforePromotion = CondExpr;
  QualType CondTypeBeforePromotion =
      GetTypeBeforeIntegralPromotion(CondExprBeforePromotion);

  // Get the bitwidth of the switched-on value after promotions. We must
  // convert the integer case values to this width before comparison.
  bool HasDependentValue
    = CondExpr->isTypeDependent() || CondExpr->isValueDependent();
  unsigned CondWidth = HasDependentValue ? 0 : Context.getIntWidth(CondType);
  bool CondIsSigned = CondType->isSignedIntegerOrEnumerationType();

  // Get the width and signedness that the condition might actually have, for
  // warning purposes.
  // FIXME: Grab an IntRange for the condition rather than using the unpromoted
  // type.
  unsigned CondWidthBeforePromotion
    = HasDependentValue ? 0 : Context.getIntWidth(CondTypeBeforePromotion);
  bool CondIsSignedBeforePromotion
    = CondTypeBeforePromotion->isSignedIntegerOrEnumerationType();

  // Accumulate all of the case values in a vector so that we can sort them
  // and detect duplicates.  This vector contains the APInt for the case after
  // it has been converted to the condition type.
  typedef SmallVector<std::pair<llvm::APSInt, CaseStmt*>, 64> CaseValsTy;
  CaseValsTy CaseVals;

  // Keep track of any GNU case ranges we see.  The APSInt is the low value.
  typedef std::vector<std::pair<llvm::APSInt, CaseStmt*> > CaseRangesTy;
  CaseRangesTy CaseRanges;

  DefaultStmt *TheDefaultStmt = nullptr;

  bool CaseListIsErroneous = false;

  // FIXME: We'd better diagnose missing or duplicate default labels even
  // in the dependent case. Because default labels themselves are never
  // dependent.
  for (SwitchCase *SC = SS->getSwitchCaseList(); SC && !HasDependentValue;
       SC = SC->getNextSwitchCase()) {

    if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC)) {
      if (TheDefaultStmt) {
        Diag(DS->getDefaultLoc(), diag::err_multiple_default_labels_defined);
        Diag(TheDefaultStmt->getDefaultLoc(), diag::note_duplicate_case_prev);

        // FIXME: Remove the default statement from the switch block so that
        // we'll return a valid AST.  This requires recursing down the AST and
        // finding it, not something we are set up to do right now.  For now,
        // just lop the entire switch stmt out of the AST.
        CaseListIsErroneous = true;
      }
      TheDefaultStmt = DS;

    } else {
      CaseStmt *CS = cast<CaseStmt>(SC);

      Expr *Lo = CS->getLHS();

      if (Lo->isValueDependent()) {
        HasDependentValue = true;
        break;
      }

      // We already verified that the expression has a constant value;
      // get that value (prior to conversions).
      const Expr *LoBeforePromotion = Lo;
      GetTypeBeforeIntegralPromotion(LoBeforePromotion);
      llvm::APSInt LoVal = LoBeforePromotion->EvaluateKnownConstInt(Context);

      // Check the unconverted value is within the range of possible values of
      // the switch expression.
      checkCaseValue(*this, Lo->getBeginLoc(), LoVal, CondWidthBeforePromotion,
                     CondIsSignedBeforePromotion);

      // FIXME: This duplicates the check performed for warn_not_in_enum below.
      checkEnumTypesInSwitchStmt(*this, CondExprBeforePromotion,
                                 LoBeforePromotion);

      // Convert the value to the same width/sign as the condition.
      AdjustAPSInt(LoVal, CondWidth, CondIsSigned);

      // If this is a case range, remember it in CaseRanges, otherwise CaseVals.
      if (CS->getRHS()) {
        if (CS->getRHS()->isValueDependent()) {
          HasDependentValue = true;
          break;
        }
        CaseRanges.push_back(std::make_pair(LoVal, CS));
      } else
        CaseVals.push_back(std::make_pair(LoVal, CS));
    }
  }

  if (!HasDependentValue) {
    // If we don't have a default statement, check whether the
    // condition is constant.
    llvm::APSInt ConstantCondValue;
    bool HasConstantCond = false;
    if (!TheDefaultStmt) {
      Expr::EvalResult Result;
      HasConstantCond = CondExpr->EvaluateAsInt(Result, Context,
                                                Expr::SE_AllowSideEffects);
      if (Result.Val.isInt())
        ConstantCondValue = Result.Val.getInt();
      assert(!HasConstantCond ||
             (ConstantCondValue.getBitWidth() == CondWidth &&
              ConstantCondValue.isSigned() == CondIsSigned));
      Diag(SwitchLoc, diag::warn_switch_default);
    }
    bool ShouldCheckConstantCond = HasConstantCond;

    // Sort all the scalar case values so we can easily detect duplicates.
    llvm::stable_sort(CaseVals, CmpCaseVals);

    if (!CaseVals.empty()) {
      for (unsigned i = 0, e = CaseVals.size(); i != e; ++i) {
        if (ShouldCheckConstantCond &&
            CaseVals[i].first == ConstantCondValue)
          ShouldCheckConstantCond = false;

        if (i != 0 && CaseVals[i].first == CaseVals[i-1].first) {
          // If we have a duplicate, report it.
          // First, determine if either case value has a name
          StringRef PrevString, CurrString;
          Expr *PrevCase = CaseVals[i-1].second->getLHS()->IgnoreParenCasts();
          Expr *CurrCase = CaseVals[i].second->getLHS()->IgnoreParenCasts();
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(PrevCase)) {
            PrevString = DeclRef->getDecl()->getName();
          }
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(CurrCase)) {
            CurrString = DeclRef->getDecl()->getName();
          }
          SmallString<16> CaseValStr;
          CaseVals[i-1].first.toString(CaseValStr);

          if (PrevString == CurrString)
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case)
                << (PrevString.empty() ? CaseValStr.str() : PrevString);
          else
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case_differing_expr)
                << (PrevString.empty() ? CaseValStr.str() : PrevString)
                << (CurrString.empty() ? CaseValStr.str() : CurrString)
                << CaseValStr;

          Diag(CaseVals[i - 1].second->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          // FIXME: We really want to remove the bogus case stmt from the
          // substmt, but we have no way to do this right now.
          CaseListIsErroneous = true;
        }
      }
    }

    // Detect duplicate case ranges, which usually don't exist at all in
    // the first place.
    if (!CaseRanges.empty()) {
      // Sort all the case ranges by their low value so we can easily detect
      // overlaps between ranges.
      llvm::stable_sort(CaseRanges);

      // Scan the ranges, computing the high values and removing empty ranges.
      std::vector<llvm::APSInt> HiVals;
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &LoVal = CaseRanges[i].first;
        CaseStmt *CR = CaseRanges[i].second;
        Expr *Hi = CR->getRHS();

        const Expr *HiBeforePromotion = Hi;
        GetTypeBeforeIntegralPromotion(HiBeforePromotion);
        llvm::APSInt HiVal = HiBeforePromotion->EvaluateKnownConstInt(Context);

        // Check the unconverted value is within the range of possible values of
        // the switch expression.
        checkCaseValue(*this, Hi->getBeginLoc(), HiVal,
                       CondWidthBeforePromotion, CondIsSignedBeforePromotion);

        // Convert the value to the same width/sign as the condition.
        AdjustAPSInt(HiVal, CondWidth, CondIsSigned);

        // If the low value is bigger than the high value, the case is empty.
        if (LoVal > HiVal) {
          Diag(CR->getLHS()->getBeginLoc(), diag::warn_case_empty_range)
              << SourceRange(CR->getLHS()->getBeginLoc(), Hi->getEndLoc());
          CaseRanges.erase(CaseRanges.begin()+i);
          --i;
          --e;
          continue;
        }

        if (ShouldCheckConstantCond &&
            LoVal <= ConstantCondValue &&
            ConstantCondValue <= HiVal)
          ShouldCheckConstantCond = false;

        HiVals.push_back(HiVal);
      }

      // Rescan the ranges, looking for overlap with singleton values and other
      // ranges.  Since the range list is sorted, we only need to compare case
      // ranges with their neighbors.
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &CRLo = CaseRanges[i].first;
        llvm::APSInt &CRHi = HiVals[i];
        CaseStmt *CR = CaseRanges[i].second;

        // Check to see whether the case range overlaps with any
        // singleton cases.
        CaseStmt *OverlapStmt = nullptr;
        llvm::APSInt OverlapVal(32);

        // Find the smallest value >= the lower bound.  If I is in the
        // case range, then we have overlap.
        CaseValsTy::iterator I =
            llvm::lower_bound(CaseVals, CRLo, CaseCompareFunctor());
        if (I != CaseVals.end() && I->first < CRHi) {
          OverlapVal  = I->first;   // Found overlap with scalar.
          OverlapStmt = I->second;
        }

        // Find the smallest value bigger than the upper bound.
        I = std::upper_bound(I, CaseVals.end(), CRHi, CaseCompareFunctor());
        if (I != CaseVals.begin() && (I-1)->first >= CRLo) {
          OverlapVal  = (I-1)->first;      // Found overlap with scalar.
          OverlapStmt = (I-1)->second;
        }

        // Check to see if this case stmt overlaps with the subsequent
        // case range.
        if (i && CRLo <= HiVals[i-1]) {
          OverlapVal  = HiVals[i-1];       // Found overlap with range.
          OverlapStmt = CaseRanges[i-1].second;
        }

        if (OverlapStmt) {
          // If we have a duplicate, report it.
          Diag(CR->getLHS()->getBeginLoc(), diag::err_duplicate_case)
              << toString(OverlapVal, 10);
          Diag(OverlapStmt->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          // FIXME: We really want to remove the bogus case stmt from the
          // substmt, but we have no way to do this right now.
          CaseListIsErroneous = true;
        }
      }
    }

    // Complain if we have a constant condition and we didn't find a match.
    if (!CaseListIsErroneous && !CaseListIsIncomplete &&
        ShouldCheckConstantCond) {
      // TODO: it would be nice if we printed enums as enums, chars as
      // chars, etc.
      Diag(CondExpr->getExprLoc(), diag::warn_missing_case_for_condition)
        << toString(ConstantCondValue, 10)
        << CondExpr->getSourceRange();
    }

    // Check to see if switch is over an Enum and handles all of its
    // values.  We only issue a warning if there is not 'default:', but
    // we still do the analysis to preserve this information in the AST
    // (which can be used by flow-based analyes).
    //
    const EnumType *ET = CondTypeBeforePromotion->getAs<EnumType>();

    // If switch has default case, then ignore it.
    if (!CaseListIsErroneous && !CaseListIsIncomplete && !HasConstantCond &&
        ET && ET->getDecl()->isCompleteDefinition() &&
        !ET->getDecl()->enumerators().empty()) {
      const EnumDecl *ED = ET->getDecl();
      EnumValsTy EnumVals;

      // Gather all enum values, set their type and sort them,
      // allowing easier comparison with CaseVals.
      for (auto *EDI : ED->enumerators()) {
        llvm::APSInt Val = EDI->getInitVal();
        AdjustAPSInt(Val, CondWidth, CondIsSigned);
        EnumVals.push_back(std::make_pair(Val, EDI));
      }
      llvm::stable_sort(EnumVals, CmpEnumVals);
      auto EI = EnumVals.begin(), EIEnd =
        std::unique(EnumVals.begin(), EnumVals.end(), EqEnumVals);

      // See which case values aren't in enum.
      for (CaseValsTy::const_iterator CI = CaseVals.begin();
          CI != CaseVals.end(); CI++) {
        Expr *CaseExpr = CI->second->getLHS();
        if (ShouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              CI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;
      }

      // See which of case ranges aren't in enum
      EI = EnumVals.begin();
      for (CaseRangesTy::const_iterator RI = CaseRanges.begin();
          RI != CaseRanges.end(); RI++) {
        Expr *CaseExpr = RI->second->getLHS();
        if (ShouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              RI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;

        llvm::APSInt Hi =
          RI->second->getRHS()->EvaluateKnownConstInt(Context);
        AdjustAPSInt(Hi, CondWidth, CondIsSigned);

        CaseExpr = RI->second->getRHS();
        if (ShouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              Hi))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
            << CondTypeBeforePromotion;
      }

      // Check which enum vals aren't in switch
      auto CI = CaseVals.begin();
      auto RI = CaseRanges.begin();
      bool hasCasesNotInSwitch = false;

      SmallVector<DeclarationName,8> UnhandledNames;

      for (EI = EnumVals.begin(); EI != EIEnd; EI++) {
        // Don't warn about omitted unavailable EnumConstantDecls.
        switch (EI->second->getAvailability()) {
        case AR_Deprecated:
          // Omitting a deprecated constant is ok; it should never materialize.
        case AR_Unavailable:
          continue;

        case AR_NotYetIntroduced:
          // Partially available enum constants should be present. Note that we
          // suppress -Wunguarded-availability diagnostics for such uses.
        case AR_Available:
          break;
        }

        if (EI->second->hasAttr<UnusedAttr>())
          continue;

        // Drop unneeded case values
        while (CI != CaseVals.end() && CI->first < EI->first)
          CI++;

        if (CI != CaseVals.end() && CI->first == EI->first)
          continue;

        // Drop unneeded case ranges
        for (; RI != CaseRanges.end(); RI++) {
          llvm::APSInt Hi =
            RI->second->getRHS()->EvaluateKnownConstInt(Context);
          AdjustAPSInt(Hi, CondWidth, CondIsSigned);
          if (EI->first <= Hi)
            break;
        }

        if (RI == CaseRanges.end() || EI->first < RI->first) {
          hasCasesNotInSwitch = true;
          UnhandledNames.push_back(EI->second->getDeclName());
        }
      }

      if (TheDefaultStmt && UnhandledNames.empty() && ED->isClosedNonFlag())
        Diag(TheDefaultStmt->getDefaultLoc(), diag::warn_unreachable_default);

      // Produce a nice diagnostic if multiple values aren't handled.
      if (!UnhandledNames.empty()) {
        auto DB = Diag(CondExpr->getExprLoc(), TheDefaultStmt
                                                   ? diag::warn_def_missing_case
                                                   : diag::warn_missing_case)
                  << CondExpr->getSourceRange() << (int)UnhandledNames.size();

        for (size_t I = 0, E = std::min(UnhandledNames.size(), (size_t)3);
             I != E; ++I)
          DB << UnhandledNames[I];
      }

      if (!hasCasesNotInSwitch)
        SS->setAllEnumCasesCovered();
    }
  }

  if (BodyStmt)
    DiagnoseEmptyStmtBody(CondExpr->getEndLoc(), BodyStmt,
                          diag::warn_empty_switch_body);

  // FIXME: If the case list was broken is some way, we don't have a good system
  // to patch it up.  Instead, just return the whole substmt as broken.
  if (CaseListIsErroneous)
    return StmtError();

  return SS;
}

void
Sema::DiagnoseAssignmentEnum(QualType DstType, QualType SrcType,
                             Expr *SrcExpr) {
  if (Diags.isIgnored(diag::warn_not_in_enum_assignment, SrcExpr->getExprLoc()))
    return;

  if (const EnumType *ET = DstType->getAs<EnumType>())
    if (!Context.hasSameUnqualifiedType(SrcType, DstType) &&
        SrcType->isIntegerType()) {
      if (!SrcExpr->isTypeDependent() && !SrcExpr->isValueDependent() &&
          SrcExpr->isIntegerConstantExpr(Context)) {
        // Get the bitwidth of the enum value before promotions.
        unsigned DstWidth = Context.getIntWidth(DstType);
        bool DstIsSigned = DstType->isSignedIntegerOrEnumerationType();

        llvm::APSInt RhsVal = SrcExpr->EvaluateKnownConstInt(Context);
        AdjustAPSInt(RhsVal, DstWidth, DstIsSigned);
        const EnumDecl *ED = ET->getDecl();

        if (!ED->isClosed())
          return;

        if (ED->hasAttr<FlagEnumAttr>()) {
          if (!IsValueInFlagEnum(ED, RhsVal, true))
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
              << DstType.getUnqualifiedType();
        } else {
          typedef SmallVector<std::pair<llvm::APSInt, EnumConstantDecl *>, 64>
              EnumValsTy;
          EnumValsTy EnumVals;

          // Gather all enum values, set their type and sort them,
          // allowing easier comparison with rhs constant.
          for (auto *EDI : ED->enumerators()) {
            llvm::APSInt Val = EDI->getInitVal();
            AdjustAPSInt(Val, DstWidth, DstIsSigned);
            EnumVals.push_back(std::make_pair(Val, EDI));
          }
          if (EnumVals.empty())
            return;
          llvm::stable_sort(EnumVals, CmpEnumVals);
          EnumValsTy::iterator EIend =
              std::unique(EnumVals.begin(), EnumVals.end(), EqEnumVals);

          // See which values aren't in the enum.
          EnumValsTy::const_iterator EI = EnumVals.begin();
          while (EI != EIend && EI->first < RhsVal)
            EI++;
          if (EI == EIend || EI->first != RhsVal) {
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
                << DstType.getUnqualifiedType();
          }
        }
      }
    }
}

StmtResult Sema::ActOnWhileStmt(SourceLocation WhileLoc,
                                SourceLocation LParenLoc, ConditionResult Cond,
                                SourceLocation RParenLoc, Stmt *Body) {
  if (Cond.isInvalid())
    return StmtError();

  auto CondVal = Cond.get();
  CheckBreakContinueBinding(CondVal.second);

  if (CondVal.second &&
      !Diags.isIgnored(diag::warn_comma_operator, CondVal.second->getExprLoc()))
    CommaVisitor(*this).Visit(CondVal.second);

  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return WhileStmt::Create(Context, CondVal.first, CondVal.second, Body,
                           WhileLoc, LParenLoc, RParenLoc);
}

StmtResult
Sema::ActOnDoStmt(SourceLocation DoLoc, Stmt *Body,
                  SourceLocation WhileLoc, SourceLocation CondLParen,
                  Expr *Cond, SourceLocation CondRParen) {
  assert(Cond && "ActOnDoStmt(): missing expression");

  CheckBreakContinueBinding(Cond);
  ExprResult CondResult = CheckBooleanCondition(DoLoc, Cond);
  if (CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.get();

  CondResult = ActOnFinishFullExpr(Cond, DoLoc, /*DiscardedValue*/ false);
  if (CondResult.isInvalid())
    return StmtError();
  Cond = CondResult.get();

  // Only call the CommaVisitor for C89 due to differences in scope flags.
  if (Cond && !getLangOpts().C99 && !getLangOpts().CPlusPlus &&
      !Diags.isIgnored(diag::warn_comma_operator, Cond->getExprLoc()))
    CommaVisitor(*this).Visit(Cond);

  return new (Context) DoStmt(Body, Cond, DoLoc, WhileLoc, CondRParen);
}

namespace {
  // Use SetVector since the diagnostic cares about the ordering of the Decl's.
  using DeclSetVector = llvm::SmallSetVector<VarDecl *, 8>;

  // This visitor will traverse a conditional statement and store all
  // the evaluated decls into a vector.  Simple is set to true if none
  // of the excluded constructs are used.
  class DeclExtractor : public EvaluatedExprVisitor<DeclExtractor> {
    DeclSetVector &Decls;
    SmallVectorImpl<SourceRange> &Ranges;
    bool Simple;
  public:
    typedef EvaluatedExprVisitor<DeclExtractor> Inherited;

    DeclExtractor(Sema &S, DeclSetVector &Decls,
                  SmallVectorImpl<SourceRange> &Ranges) :
        Inherited(S.Context),
        Decls(Decls),
        Ranges(Ranges),
        Simple(true) {}

    bool isSimple() { return Simple; }

    // Replaces the method in EvaluatedExprVisitor.
    void VisitMemberExpr(MemberExpr* E) {
      Simple = false;
    }

    // Any Stmt not explicitly listed will cause the condition to be marked
    // complex.
    void VisitStmt(Stmt *S) { Simple = false; }

    void VisitBinaryOperator(BinaryOperator *E) {
      Visit(E->getLHS());
      Visit(E->getRHS());
    }

    void VisitCastExpr(CastExpr *E) {
      Visit(E->getSubExpr());
    }

    void VisitUnaryOperator(UnaryOperator *E) {
      // Skip checking conditionals with derefernces.
      if (E->getOpcode() == UO_Deref)
        Simple = false;
      else
        Visit(E->getSubExpr());
    }

    void VisitConditionalOperator(ConditionalOperator *E) {
      Visit(E->getCond());
      Visit(E->getTrueExpr());
      Visit(E->getFalseExpr());
    }

    void VisitParenExpr(ParenExpr *E) {
      Visit(E->getSubExpr());
    }

    void VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
      Visit(E->getOpaqueValue()->getSourceExpr());
      Visit(E->getFalseExpr());
    }

    void VisitIntegerLiteral(IntegerLiteral *E) { }
    void VisitFloatingLiteral(FloatingLiteral *E) { }
    void VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) { }
    void VisitCharacterLiteral(CharacterLiteral *E) { }
    void VisitGNUNullExpr(GNUNullExpr *E) { }
    void VisitImaginaryLiteral(ImaginaryLiteral *E) { }

    void VisitDeclRefExpr(DeclRefExpr *E) {
      VarDecl *VD = dyn_cast<VarDecl>(E->getDecl());
      if (!VD) {
        // Don't allow unhandled Decl types.
        Simple = false;
        return;
      }

      Ranges.push_back(E->getSourceRange());

      Decls.insert(VD);
    }

  }; // end class DeclExtractor

  // DeclMatcher checks to see if the decls are used in a non-evaluated
  // context.
  class DeclMatcher : public EvaluatedExprVisitor<DeclMatcher> {
    DeclSetVector &Decls;
    bool FoundDecl;

  public:
    typedef EvaluatedExprVisitor<DeclMatcher> Inherited;

    DeclMatcher(Sema &S, DeclSetVector &Decls, Stmt *Statement) :
        Inherited(S.Context), Decls(Decls), FoundDecl(false) {
      if (!Statement) return;

      Visit(Statement);
    }

    void VisitReturnStmt(ReturnStmt *S) {
      FoundDecl = true;
    }

    void VisitBreakStmt(BreakStmt *S) {
      FoundDecl = true;
    }

    void VisitGotoStmt(GotoStmt *S) {
      FoundDecl = true;
    }

    void VisitCastExpr(CastExpr *E) {
      if (E->getCastKind() == CK_LValueToRValue)
        CheckLValueToRValueCast(E->getSubExpr());
      else
        Visit(E->getSubExpr());
    }

    void CheckLValueToRValueCast(Expr *E) {
      E = E->IgnoreParenImpCasts();

      if (isa<DeclRefExpr>(E)) {
        return;
      }

      if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
        Visit(CO->getCond());
        CheckLValueToRValueCast(CO->getTrueExpr());
        CheckLValueToRValueCast(CO->getFalseExpr());
        return;
      }

      if (BinaryConditionalOperator *BCO =
              dyn_cast<BinaryConditionalOperator>(E)) {
        CheckLValueToRValueCast(BCO->getOpaqueValue()->getSourceExpr());
        CheckLValueToRValueCast(BCO->getFalseExpr());
        return;
      }

      Visit(E);
    }

    void VisitDeclRefExpr(DeclRefExpr *E) {
      if (VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
        if (Decls.count(VD))
          FoundDecl = true;
    }

    void VisitPseudoObjectExpr(PseudoObjectExpr *POE) {
      // Only need to visit the semantics for POE.
      // SyntaticForm doesn't really use the Decal.
      for (auto *S : POE->semantics()) {
        if (auto *OVE = dyn_cast<OpaqueValueExpr>(S))
          // Look past the OVE into the expression it binds.
          Visit(OVE->getSourceExpr());
        else
          Visit(S);
      }
    }

    bool FoundDeclInUse() { return FoundDecl; }

  };  // end class DeclMatcher

  void CheckForLoopConditionalStatement(Sema &S, Expr *Second,
                                        Expr *Third, Stmt *Body) {
    // Condition is empty
    if (!Second) return;

    if (S.Diags.isIgnored(diag::warn_variables_not_in_loop_body,
                          Second->getBeginLoc()))
      return;

    PartialDiagnostic PDiag = S.PDiag(diag::warn_variables_not_in_loop_body);
    DeclSetVector Decls;
    SmallVector<SourceRange, 10> Ranges;
    DeclExtractor DE(S, Decls, Ranges);
    DE.Visit(Second);

    // Don't analyze complex conditionals.
    if (!DE.isSimple()) return;

    // No decls found.
    if (Decls.size() == 0) return;

    // Don't warn on volatile, static, or global variables.
    for (auto *VD : Decls)
      if (VD->getType().isVolatileQualified() || VD->hasGlobalStorage())
        return;

    if (DeclMatcher(S, Decls, Second).FoundDeclInUse() ||
        DeclMatcher(S, Decls, Third).FoundDeclInUse() ||
        DeclMatcher(S, Decls, Body).FoundDeclInUse())
      return;

    // Load decl names into diagnostic.
    if (Decls.size() > 4) {
      PDiag << 0;
    } else {
      PDiag << (unsigned)Decls.size();
      for (auto *VD : Decls)
        PDiag << VD->getDeclName();
    }

    for (auto Range : Ranges)
      PDiag << Range;

    S.Diag(Ranges.begin()->getBegin(), PDiag);
  }

  // If Statement is an incemement or decrement, return true and sets the
  // variables Increment and DRE.
  bool ProcessIterationStmt(Sema &S, Stmt* Statement, bool &Increment,
                            DeclRefExpr *&DRE) {
    if (auto Cleanups = dyn_cast<ExprWithCleanups>(Statement))
      if (!Cleanups->cleanupsHaveSideEffects())
        Statement = Cleanups->getSubExpr();

    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(Statement)) {
      switch (UO->getOpcode()) {
        default: return false;
        case UO_PostInc:
        case UO_PreInc:
          Increment = true;
          break;
        case UO_PostDec:
        case UO_PreDec:
          Increment = false;
          break;
      }
      DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr());
      return DRE;
    }

    if (CXXOperatorCallExpr *Call = dyn_cast<CXXOperatorCallExpr>(Statement)) {
      FunctionDecl *FD = Call->getDirectCallee();
      if (!FD || !FD->isOverloadedOperator()) return false;
      switch (FD->getOverloadedOperator()) {
        default: return false;
        case OO_PlusPlus:
          Increment = true;
          break;
        case OO_MinusMinus:
          Increment = false;
          break;
      }
      DRE = dyn_cast<DeclRefExpr>(Call->getArg(0));
      return DRE;
    }

    return false;
  }

  // A visitor to determine if a continue or break statement is a
  // subexpression.
  class BreakContinueFinder : public ConstEvaluatedExprVisitor<BreakContinueFinder> {
    SourceLocation BreakLoc;
    SourceLocation ContinueLoc;
    bool InSwitch = false;

  public:
    BreakContinueFinder(Sema &S, const Stmt* Body) :
        Inherited(S.Context) {
      Visit(Body);
    }

    typedef ConstEvaluatedExprVisitor<BreakContinueFinder> Inherited;

    void VisitContinueStmt(const ContinueStmt* E) {
      ContinueLoc = E->getContinueLoc();
    }

    void VisitBreakStmt(const BreakStmt* E) {
      if (!InSwitch)
        BreakLoc = E->getBreakLoc();
    }

    void VisitSwitchStmt(const SwitchStmt* S) {
      if (const Stmt *Init = S->getInit())
        Visit(Init);
      if (const Stmt *CondVar = S->getConditionVariableDeclStmt())
        Visit(CondVar);
      if (const Stmt *Cond = S->getCond())
        Visit(Cond);

      // Don't return break statements from the body of a switch.
      InSwitch = true;
      if (const Stmt *Body = S->getBody())
        Visit(Body);
      InSwitch = false;
    }

    void VisitForStmt(const ForStmt *S) {
      // Only visit the init statement of a for loop; the body
      // has a different break/continue scope.
      if (const Stmt *Init = S->getInit())
        Visit(Init);
    }

    void VisitWhileStmt(const WhileStmt *) {
      // Do nothing; the children of a while loop have a different
      // break/continue scope.
    }

    void VisitDoStmt(const DoStmt *) {
      // Do nothing; the children of a while loop have a different
      // break/continue scope.
    }

    void VisitCXXForRangeStmt(const CXXForRangeStmt *S) {
      // Only visit the initialization of a for loop; the body
      // has a different break/continue scope.
      if (const Stmt *Init = S->getInit())
        Visit(Init);
      if (const Stmt *Range = S->getRangeStmt())
        Visit(Range);
      if (const Stmt *Begin = S->getBeginStmt())
        Visit(Begin);
      if (const Stmt *End = S->getEndStmt())
        Visit(End);
    }

    void VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S) {
      // Only visit the initialization of a for loop; the body
      // has a different break/continue scope.
      if (const Stmt *Element = S->getElement())
        Visit(Element);
      if (const Stmt *Collection = S->getCollection())
        Visit(Collection);
    }

    bool ContinueFound() { return ContinueLoc.isValid(); }
    bool BreakFound() { return BreakLoc.isValid(); }
    SourceLocation GetContinueLoc() { return ContinueLoc; }
    SourceLocation GetBreakLoc() { return BreakLoc; }

  };  // end class BreakContinueFinder

  // Emit a warning when a loop increment/decrement appears twice per loop
  // iteration.  The conditions which trigger this warning are:
  // 1) The last statement in the loop body and the third expression in the
  //    for loop are both increment or both decrement of the same variable
  // 2) No continue statements in the loop body.
  void CheckForRedundantIteration(Sema &S, Expr *Third, Stmt *Body) {
    // Return when there is nothing to check.
    if (!Body || !Third) return;

    if (S.Diags.isIgnored(diag::warn_redundant_loop_iteration,
                          Third->getBeginLoc()))
      return;

    // Get the last statement from the loop body.
    CompoundStmt *CS = dyn_cast<CompoundStmt>(Body);
    if (!CS || CS->body_empty()) return;
    Stmt *LastStmt = CS->body_back();
    if (!LastStmt) return;

    bool LoopIncrement, LastIncrement;
    DeclRefExpr *LoopDRE, *LastDRE;

    if (!ProcessIterationStmt(S, Third, LoopIncrement, LoopDRE)) return;
    if (!ProcessIterationStmt(S, LastStmt, LastIncrement, LastDRE)) return;

    // Check that the two statements are both increments or both decrements
    // on the same variable.
    if (LoopIncrement != LastIncrement ||
        LoopDRE->getDecl() != LastDRE->getDecl()) return;

    if (BreakContinueFinder(S, Body).ContinueFound()) return;

    S.Diag(LastDRE->getLocation(), diag::warn_redundant_loop_iteration)
         << LastDRE->getDecl() << LastIncrement;
    S.Diag(LoopDRE->getLocation(), diag::note_loop_iteration_here)
         << LoopIncrement;
  }

} // end namespace


void Sema::CheckBreakContinueBinding(Expr *E) {
  if (!E || getLangOpts().CPlusPlus)
    return;
  BreakContinueFinder BCFinder(*this, E);
  Scope *BreakParent = CurScope->getBreakParent();
  if (BCFinder.BreakFound() && BreakParent) {
    if (BreakParent->getFlags() & Scope::SwitchScope) {
      Diag(BCFinder.GetBreakLoc(), diag::warn_break_binds_to_switch);
    } else {
      Diag(BCFinder.GetBreakLoc(), diag::warn_loop_ctrl_binds_to_inner)
          << "break";
    }
  } else if (BCFinder.ContinueFound() && CurScope->getContinueParent()) {
    Diag(BCFinder.GetContinueLoc(), diag::warn_loop_ctrl_binds_to_inner)
        << "continue";
  }
}

StmtResult Sema::ActOnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                              Stmt *First, ConditionResult Second,
                              FullExprArg third, SourceLocation RParenLoc,
                              Stmt *Body) {
  if (Second.isInvalid())
    return StmtError();

  if (!getLangOpts().CPlusPlus) {
    if (DeclStmt *DS = dyn_cast_or_null<DeclStmt>(First)) {
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      const Decl *NonVarSeen = nullptr;
      bool VarDeclSeen = false;
      for (auto *DI : DS->decls()) {
        if (VarDecl *VD = dyn_cast<VarDecl>(DI)) {
          VarDeclSeen = true;
          if (VD->isLocalVarDecl() && !VD->hasLocalStorage()) {
            Diag(DI->getLocation(), diag::err_non_local_variable_decl_in_for);
            DI->setInvalidDecl();
          }
        } else if (!NonVarSeen) {
          // Keep track of the first non-variable declaration we saw so that
          // we can diagnose if we don't see any variable declarations. This
          // covers a case like declaring a typedef, function, or structure
          // type rather than a variable.
          NonVarSeen = DI;
        }
      }
      // Diagnose if we saw a non-variable declaration but no variable
      // declarations.
      if (NonVarSeen && !VarDeclSeen)
        Diag(NonVarSeen->getLocation(), diag::err_non_variable_decl_in_for);
    }
  }

  CheckBreakContinueBinding(Second.get().second);
  CheckBreakContinueBinding(third.get());

  if (!Second.get().first)
    CheckForLoopConditionalStatement(*this, Second.get().second, third.get(),
                                     Body);
  CheckForRedundantIteration(*this, third.get(), Body);

  if (Second.get().second &&
      !Diags.isIgnored(diag::warn_comma_operator,
                       Second.get().second->getExprLoc()))
    CommaVisitor(*this).Visit(Second.get().second);

  Expr *Third  = third.release().getAs<Expr>();
  if (isa<NullStmt>(Body))
    getCurCompoundScope().setHasEmptyLoopBodies();

  return new (Context)
      ForStmt(Context, First, Second.get().second, Second.get().first, Third,
              Body, ForLoc, LParenLoc, RParenLoc);
}

StmtResult Sema::ActOnForEachLValueExpr(Expr *E) {
  // Reduce placeholder expressions here.  Note that this rejects the
  // use of pseudo-object l-values in this position.
  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid()) return StmtError();
  E = result.get();

  ExprResult FullExpr = ActOnFinishFullExpr(E, /*DiscardedValue*/ false);
  if (FullExpr.isInvalid())
    return StmtError();
  return StmtResult(static_cast<Stmt*>(FullExpr.get()));
}

/// Finish building a variable declaration for a for-range statement.
/// \return true if an error occurs.
static bool FinishForRangeVarDecl(Sema &SemaRef, VarDecl *Decl, Expr *Init,
                                  SourceLocation Loc, int DiagID) {
  if (Decl->getType()->isUndeducedType()) {
    ExprResult Res = SemaRef.CorrectDelayedTyposInExpr(Init);
    if (!Res.isUsable()) {
      Decl->setInvalidDecl();
      return true;
    }
    Init = Res.get();
  }

  // Deduce the type for the iterator variable now rather than leaving it to
  // AddInitializerToDecl, so we can produce a more suitable diagnostic.
  QualType InitType;
  if (!isa<InitListExpr>(Init) && Init->getType()->isVoidType()) {
    SemaRef.Diag(Loc, DiagID) << Init->getType();
  } else {
    TemplateDeductionInfo Info(Init->getExprLoc());
    TemplateDeductionResult Result = SemaRef.DeduceAutoType(
        Decl->getTypeSourceInfo()->getTypeLoc(), Init, InitType, Info);
    if (Result != TemplateDeductionResult::Success &&
        Result != TemplateDeductionResult::AlreadyDiagnosed)
      SemaRef.Diag(Loc, DiagID) << Init->getType();
  }

  if (InitType.isNull()) {
    Decl->setInvalidDecl();
    return true;
  }
  Decl->setType(InitType);

  // In ARC, infer lifetime.
  // FIXME: ARC may want to turn this into 'const __unsafe_unretained' if
  // we're doing the equivalent of fast iteration.
  if (SemaRef.getLangOpts().ObjCAutoRefCount &&
      SemaRef.ObjC().inferObjCARCLifetime(Decl))
    Decl->setInvalidDecl();

  SemaRef.AddInitializerToDecl(Decl, Init, /*DirectInit=*/false);
  SemaRef.FinalizeDeclaration(Decl);
  SemaRef.CurContext->addHiddenDecl(Decl);
  return false;
}

namespace {
// An enum to represent whether something is dealing with a call to begin()
// or a call to end() in a range-based for loop.
enum BeginEndFunction {
  BEF_begin,
  BEF_end
};

/// Produce a note indicating which begin/end function was implicitly called
/// by a C++11 for-range statement. This is often not obvious from the code,
/// nor from the diagnostics produced when analysing the implicit expressions
/// required in a for-range statement.
void NoteForRangeBeginEndFunction(Sema &SemaRef, Expr *E,
                                  BeginEndFunction BEF) {
  CallExpr *CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return;
  FunctionDecl *D = dyn_cast<FunctionDecl>(CE->getCalleeDecl());
  if (!D)
    return;
  SourceLocation Loc = D->getLocation();

  std::string Description;
  bool IsTemplate = false;
  if (FunctionTemplateDecl *FunTmpl = D->getPrimaryTemplate()) {
    Description = SemaRef.getTemplateArgumentBindingsText(
      FunTmpl->getTemplateParameters(), *D->getTemplateSpecializationArgs());
    IsTemplate = true;
  }

  SemaRef.Diag(Loc, diag::note_for_range_begin_end)
    << BEF << IsTemplate << Description << E->getType();
}

/// Build a variable declaration for a for-range statement.
VarDecl *BuildForRangeVarDecl(Sema &SemaRef, SourceLocation Loc,
                              QualType Type, StringRef Name) {
  DeclContext *DC = SemaRef.CurContext;
  IdentifierInfo *II = &SemaRef.PP.getIdentifierTable().get(Name);
  TypeSourceInfo *TInfo = SemaRef.Context.getTrivialTypeSourceInfo(Type, Loc);
  VarDecl *Decl = VarDecl::Create(SemaRef.Context, DC, Loc, Loc, II, Type,
                                  TInfo, SC_None);
  Decl->setImplicit();
  return Decl;
}

}

static bool ObjCEnumerationCollection(Expr *Collection) {
  return !Collection->isTypeDependent()
          && Collection->getType()->getAs<ObjCObjectPointerType>() != nullptr;
}

StmtResult Sema::ActOnCXXForRangeStmt(
    Scope *S, SourceLocation ForLoc, SourceLocation CoawaitLoc, Stmt *InitStmt,
    Stmt *First, SourceLocation ColonLoc, Expr *Range, SourceLocation RParenLoc,
    BuildForRangeKind Kind,
    ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps) {
  // FIXME: recover in order to allow the body to be parsed.
  if (!First)
    return StmtError();

  if (Range && ObjCEnumerationCollection(Range)) {
    // FIXME: Support init-statements in Objective-C++20 ranged for statement.
    if (InitStmt)
      return Diag(InitStmt->getBeginLoc(), diag::err_objc_for_range_init_stmt)
                 << InitStmt->getSourceRange();
    return ObjC().ActOnObjCForCollectionStmt(ForLoc, First, Range, RParenLoc);
  }

  DeclStmt *DS = dyn_cast<DeclStmt>(First);
  assert(DS && "first part of for range not a decl stmt");

  if (!DS->isSingleDecl()) {
    Diag(DS->getBeginLoc(), diag::err_type_defined_in_for_range);
    return StmtError();
  }

  // This function is responsible for attaching an initializer to LoopVar. We
  // must call ActOnInitializerError if we fail to do so.
  Decl *LoopVar = DS->getSingleDecl();
  if (LoopVar->isInvalidDecl() || !Range ||
      DiagnoseUnexpandedParameterPack(Range, UPPC_Expression)) {
    ActOnInitializerError(LoopVar);
    return StmtError();
  }

  // Build the coroutine state immediately and not later during template
  // instantiation
  if (!CoawaitLoc.isInvalid()) {
    if (!ActOnCoroutineBodyStart(S, CoawaitLoc, "co_await")) {
      ActOnInitializerError(LoopVar);
      return StmtError();
    }
  }

  // Build  auto && __range = range-init
  // Divide by 2, since the variables are in the inner scope (loop body).
  const auto DepthStr = std::to_string(S->getDepth() / 2);
  SourceLocation RangeLoc = Range->getBeginLoc();
  VarDecl *RangeVar = BuildForRangeVarDecl(*this, RangeLoc,
                                           Context.getAutoRRefDeductType(),
                                           std::string("__range") + DepthStr);
  if (FinishForRangeVarDecl(*this, RangeVar, Range, RangeLoc,
                            diag::err_for_range_deduction_failure)) {
    ActOnInitializerError(LoopVar);
    return StmtError();
  }

  // Claim the type doesn't contain auto: we've already done the checking.
  DeclGroupPtrTy RangeGroup =
      BuildDeclaratorGroup(MutableArrayRef<Decl *>((Decl **)&RangeVar, 1));
  StmtResult RangeDecl = ActOnDeclStmt(RangeGroup, RangeLoc, RangeLoc);
  if (RangeDecl.isInvalid()) {
    ActOnInitializerError(LoopVar);
    return StmtError();
  }

  StmtResult R = BuildCXXForRangeStmt(
      ForLoc, CoawaitLoc, InitStmt, ColonLoc, RangeDecl.get(),
      /*BeginStmt=*/nullptr, /*EndStmt=*/nullptr,
      /*Cond=*/nullptr, /*Inc=*/nullptr, DS, RParenLoc, Kind,
      LifetimeExtendTemps);
  if (R.isInvalid()) {
    ActOnInitializerError(LoopVar);
    return StmtError();
  }

  return R;
}

/// Create the initialization, compare, and increment steps for
/// the range-based for loop expression.
/// This function does not handle array-based for loops,
/// which are created in Sema::BuildCXXForRangeStmt.
///
/// \returns a ForRangeStatus indicating success or what kind of error occurred.
/// BeginExpr and EndExpr are set and FRS_Success is returned on success;
/// CandidateSet and BEF are set and some non-success value is returned on
/// failure.
static Sema::ForRangeStatus
BuildNonArrayForRange(Sema &SemaRef, Expr *BeginRange, Expr *EndRange,
                      QualType RangeType, VarDecl *BeginVar, VarDecl *EndVar,
                      SourceLocation ColonLoc, SourceLocation CoawaitLoc,
                      OverloadCandidateSet *CandidateSet, ExprResult *BeginExpr,
                      ExprResult *EndExpr, BeginEndFunction *BEF) {
  DeclarationNameInfo BeginNameInfo(
      &SemaRef.PP.getIdentifierTable().get("begin"), ColonLoc);
  DeclarationNameInfo EndNameInfo(&SemaRef.PP.getIdentifierTable().get("end"),
                                  ColonLoc);

  LookupResult BeginMemberLookup(SemaRef, BeginNameInfo,
                                 Sema::LookupMemberName);
  LookupResult EndMemberLookup(SemaRef, EndNameInfo, Sema::LookupMemberName);

  auto BuildBegin = [&] {
    *BEF = BEF_begin;
    Sema::ForRangeStatus RangeStatus =
        SemaRef.BuildForRangeBeginEndCall(ColonLoc, ColonLoc, BeginNameInfo,
                                          BeginMemberLookup, CandidateSet,
                                          BeginRange, BeginExpr);

    if (RangeStatus != Sema::FRS_Success) {
      if (RangeStatus == Sema::FRS_DiagnosticIssued)
        SemaRef.Diag(BeginRange->getBeginLoc(), diag::note_in_for_range)
            << ColonLoc << BEF_begin << BeginRange->getType();
      return RangeStatus;
    }
    if (!CoawaitLoc.isInvalid()) {
      // FIXME: getCurScope() should not be used during template instantiation.
      // We should pick up the set of unqualified lookup results for operator
      // co_await during the initial parse.
      *BeginExpr = SemaRef.ActOnCoawaitExpr(SemaRef.getCurScope(), ColonLoc,
                                            BeginExpr->get());
      if (BeginExpr->isInvalid())
        return Sema::FRS_DiagnosticIssued;
    }
    if (FinishForRangeVarDecl(SemaRef, BeginVar, BeginExpr->get(), ColonLoc,
                              diag::err_for_range_iter_deduction_failure)) {
      NoteForRangeBeginEndFunction(SemaRef, BeginExpr->get(), *BEF);
      return Sema::FRS_DiagnosticIssued;
    }
    return Sema::FRS_Success;
  };

  auto BuildEnd = [&] {
    *BEF = BEF_end;
    Sema::ForRangeStatus RangeStatus =
        SemaRef.BuildForRangeBeginEndCall(ColonLoc, ColonLoc, EndNameInfo,
                                          EndMemberLookup, CandidateSet,
                                          EndRange, EndExpr);
    if (RangeStatus != Sema::FRS_Success) {
      if (RangeStatus == Sema::FRS_DiagnosticIssued)
        SemaRef.Diag(EndRange->getBeginLoc(), diag::note_in_for_range)
            << ColonLoc << BEF_end << EndRange->getType();
      return RangeStatus;
    }
    if (FinishForRangeVarDecl(SemaRef, EndVar, EndExpr->get(), ColonLoc,
                              diag::err_for_range_iter_deduction_failure)) {
      NoteForRangeBeginEndFunction(SemaRef, EndExpr->get(), *BEF);
      return Sema::FRS_DiagnosticIssued;
    }
    return Sema::FRS_Success;
  };

  if (CXXRecordDecl *D = RangeType->getAsCXXRecordDecl()) {
    // - if _RangeT is a class type, the unqualified-ids begin and end are
    //   looked up in the scope of class _RangeT as if by class member access
    //   lookup (3.4.5), and if either (or both) finds at least one
    //   declaration, begin-expr and end-expr are __range.begin() and
    //   __range.end(), respectively;
    SemaRef.LookupQualifiedName(BeginMemberLookup, D);
    if (BeginMemberLookup.isAmbiguous())
      return Sema::FRS_DiagnosticIssued;

    SemaRef.LookupQualifiedName(EndMemberLookup, D);
    if (EndMemberLookup.isAmbiguous())
      return Sema::FRS_DiagnosticIssued;

    if (BeginMemberLookup.empty() != EndMemberLookup.empty()) {
      // Look up the non-member form of the member we didn't find, first.
      // This way we prefer a "no viable 'end'" diagnostic over a "i found
      // a 'begin' but ignored it because there was no member 'end'"
      // diagnostic.
      auto BuildNonmember = [&](
          BeginEndFunction BEFFound, LookupResult &Found,
          llvm::function_ref<Sema::ForRangeStatus()> BuildFound,
          llvm::function_ref<Sema::ForRangeStatus()> BuildNotFound) {
        LookupResult OldFound = std::move(Found);
        Found.clear();

        if (Sema::ForRangeStatus Result = BuildNotFound())
          return Result;

        switch (BuildFound()) {
        case Sema::FRS_Success:
          return Sema::FRS_Success;

        case Sema::FRS_NoViableFunction:
          CandidateSet->NoteCandidates(
              PartialDiagnosticAt(BeginRange->getBeginLoc(),
                                  SemaRef.PDiag(diag::err_for_range_invalid)
                                      << BeginRange->getType() << BEFFound),
              SemaRef, OCD_AllCandidates, BeginRange);
          [[fallthrough]];

        case Sema::FRS_DiagnosticIssued:
          for (NamedDecl *D : OldFound) {
            SemaRef.Diag(D->getLocation(),
                         diag::note_for_range_member_begin_end_ignored)
                << BeginRange->getType() << BEFFound;
          }
          return Sema::FRS_DiagnosticIssued;
        }
        llvm_unreachable("unexpected ForRangeStatus");
      };
      if (BeginMemberLookup.empty())
        return BuildNonmember(BEF_end, EndMemberLookup, BuildEnd, BuildBegin);
      return BuildNonmember(BEF_begin, BeginMemberLookup, BuildBegin, BuildEnd);
    }
  } else {
    // - otherwise, begin-expr and end-expr are begin(__range) and
    //   end(__range), respectively, where begin and end are looked up with
    //   argument-dependent lookup (3.4.2). For the purposes of this name
    //   lookup, namespace std is an associated namespace.
  }

  if (Sema::ForRangeStatus Result = BuildBegin())
    return Result;
  return BuildEnd();
}

/// Speculatively attempt to dereference an invalid range expression.
/// If the attempt fails, this function will return a valid, null StmtResult
/// and emit no diagnostics.
static StmtResult RebuildForRangeWithDereference(Sema &SemaRef, Scope *S,
                                                 SourceLocation ForLoc,
                                                 SourceLocation CoawaitLoc,
                                                 Stmt *InitStmt,
                                                 Stmt *LoopVarDecl,
                                                 SourceLocation ColonLoc,
                                                 Expr *Range,
                                                 SourceLocation RangeLoc,
                                                 SourceLocation RParenLoc) {
  // Determine whether we can rebuild the for-range statement with a
  // dereferenced range expression.
  ExprResult AdjustedRange;
  {
    Sema::SFINAETrap Trap(SemaRef);

    AdjustedRange = SemaRef.BuildUnaryOp(S, RangeLoc, UO_Deref, Range);
    if (AdjustedRange.isInvalid())
      return StmtResult();

    StmtResult SR = SemaRef.ActOnCXXForRangeStmt(
        S, ForLoc, CoawaitLoc, InitStmt, LoopVarDecl, ColonLoc,
        AdjustedRange.get(), RParenLoc, Sema::BFRK_Check);
    if (SR.isInvalid())
      return StmtResult();
  }

  // The attempt to dereference worked well enough that it could produce a valid
  // loop. Produce a fixit, and rebuild the loop with diagnostics enabled, in
  // case there are any other (non-fatal) problems with it.
  SemaRef.Diag(RangeLoc, diag::err_for_range_dereference)
    << Range->getType() << FixItHint::CreateInsertion(RangeLoc, "*");
  return SemaRef.ActOnCXXForRangeStmt(
      S, ForLoc, CoawaitLoc, InitStmt, LoopVarDecl, ColonLoc,
      AdjustedRange.get(), RParenLoc, Sema::BFRK_Rebuild);
}

StmtResult Sema::BuildCXXForRangeStmt(
    SourceLocation ForLoc, SourceLocation CoawaitLoc, Stmt *InitStmt,
    SourceLocation ColonLoc, Stmt *RangeDecl, Stmt *Begin, Stmt *End,
    Expr *Cond, Expr *Inc, Stmt *LoopVarDecl, SourceLocation RParenLoc,
    BuildForRangeKind Kind,
    ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps) {
  // FIXME: This should not be used during template instantiation. We should
  // pick up the set of unqualified lookup results for the != and + operators
  // in the initial parse.
  //
  // Testcase (accepts-invalid):
  //   template<typename T> void f() { for (auto x : T()) {} }
  //   namespace N { struct X { X begin(); X end(); int operator*(); }; }
  //   bool operator!=(N::X, N::X); void operator++(N::X);
  //   void g() { f<N::X>(); }
  Scope *S = getCurScope();

  DeclStmt *RangeDS = cast<DeclStmt>(RangeDecl);
  VarDecl *RangeVar = cast<VarDecl>(RangeDS->getSingleDecl());
  QualType RangeVarType = RangeVar->getType();

  DeclStmt *LoopVarDS = cast<DeclStmt>(LoopVarDecl);
  VarDecl *LoopVar = cast<VarDecl>(LoopVarDS->getSingleDecl());

  StmtResult BeginDeclStmt = Begin;
  StmtResult EndDeclStmt = End;
  ExprResult NotEqExpr = Cond, IncrExpr = Inc;

  if (RangeVarType->isDependentType()) {
    // The range is implicitly used as a placeholder when it is dependent.
    RangeVar->markUsed(Context);

    // Deduce any 'auto's in the loop variable as 'DependentTy'. We'll fill
    // them in properly when we instantiate the loop.
    if (!LoopVar->isInvalidDecl() && Kind != BFRK_Check) {
      if (auto *DD = dyn_cast<DecompositionDecl>(LoopVar))
        for (auto *Binding : DD->bindings())
          Binding->setType(Context.DependentTy);
      LoopVar->setType(SubstAutoTypeDependent(LoopVar->getType()));
    }
  } else if (!BeginDeclStmt.get()) {
    SourceLocation RangeLoc = RangeVar->getLocation();

    const QualType RangeVarNonRefType = RangeVarType.getNonReferenceType();

    ExprResult BeginRangeRef = BuildDeclRefExpr(RangeVar, RangeVarNonRefType,
                                                VK_LValue, ColonLoc);
    if (BeginRangeRef.isInvalid())
      return StmtError();

    ExprResult EndRangeRef = BuildDeclRefExpr(RangeVar, RangeVarNonRefType,
                                              VK_LValue, ColonLoc);
    if (EndRangeRef.isInvalid())
      return StmtError();

    QualType AutoType = Context.getAutoDeductType();
    Expr *Range = RangeVar->getInit();
    if (!Range)
      return StmtError();
    QualType RangeType = Range->getType();

    if (RequireCompleteType(RangeLoc, RangeType,
                            diag::err_for_range_incomplete_type))
      return StmtError();

    // P2718R0 - Lifetime extension in range-based for loops.
    if (getLangOpts().CPlusPlus23 && !LifetimeExtendTemps.empty()) {
      InitializedEntity Entity =
          InitializedEntity::InitializeVariable(RangeVar);
      for (auto *MTE : LifetimeExtendTemps)
        MTE->setExtendingDecl(RangeVar, Entity.allocateManglingNumber());
    }

    // Build auto __begin = begin-expr, __end = end-expr.
    // Divide by 2, since the variables are in the inner scope (loop body).
    const auto DepthStr = std::to_string(S->getDepth() / 2);
    VarDecl *BeginVar = BuildForRangeVarDecl(*this, ColonLoc, AutoType,
                                             std::string("__begin") + DepthStr);
    VarDecl *EndVar = BuildForRangeVarDecl(*this, ColonLoc, AutoType,
                                           std::string("__end") + DepthStr);

    // Build begin-expr and end-expr and attach to __begin and __end variables.
    ExprResult BeginExpr, EndExpr;
    if (const ArrayType *UnqAT = RangeType->getAsArrayTypeUnsafe()) {
      // - if _RangeT is an array type, begin-expr and end-expr are __range and
      //   __range + __bound, respectively, where __bound is the array bound. If
      //   _RangeT is an array of unknown size or an array of incomplete type,
      //   the program is ill-formed;

      // begin-expr is __range.
      BeginExpr = BeginRangeRef;
      if (!CoawaitLoc.isInvalid()) {
        BeginExpr = ActOnCoawaitExpr(S, ColonLoc, BeginExpr.get());
        if (BeginExpr.isInvalid())
          return StmtError();
      }
      if (FinishForRangeVarDecl(*this, BeginVar, BeginRangeRef.get(), ColonLoc,
                                diag::err_for_range_iter_deduction_failure)) {
        NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
        return StmtError();
      }

      // Find the array bound.
      ExprResult BoundExpr;
      if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(UnqAT))
        BoundExpr = IntegerLiteral::Create(
            Context, CAT->getSize(), Context.getPointerDiffType(), RangeLoc);
      else if (const VariableArrayType *VAT =
               dyn_cast<VariableArrayType>(UnqAT)) {
        // For a variably modified type we can't just use the expression within
        // the array bounds, since we don't want that to be re-evaluated here.
        // Rather, we need to determine what it was when the array was first
        // created - so we resort to using sizeof(vla)/sizeof(element).
        // For e.g.
        //  void f(int b) {
        //    int vla[b];
        //    b = -1;   <-- This should not affect the num of iterations below
        //    for (int &c : vla) { .. }
        //  }

        // FIXME: This results in codegen generating IR that recalculates the
        // run-time number of elements (as opposed to just using the IR Value
        // that corresponds to the run-time value of each bound that was
        // generated when the array was created.) If this proves too embarrassing
        // even for unoptimized IR, consider passing a magic-value/cookie to
        // codegen that then knows to simply use that initial llvm::Value (that
        // corresponds to the bound at time of array creation) within
        // getelementptr.  But be prepared to pay the price of increasing a
        // customized form of coupling between the two components - which  could
        // be hard to maintain as the codebase evolves.

        ExprResult SizeOfVLAExprR = ActOnUnaryExprOrTypeTraitExpr(
            EndVar->getLocation(), UETT_SizeOf,
            /*IsType=*/true,
            CreateParsedType(VAT->desugar(), Context.getTrivialTypeSourceInfo(
                                                 VAT->desugar(), RangeLoc))
                .getAsOpaquePtr(),
            EndVar->getSourceRange());
        if (SizeOfVLAExprR.isInvalid())
          return StmtError();

        ExprResult SizeOfEachElementExprR = ActOnUnaryExprOrTypeTraitExpr(
            EndVar->getLocation(), UETT_SizeOf,
            /*IsType=*/true,
            CreateParsedType(VAT->desugar(),
                             Context.getTrivialTypeSourceInfo(
                                 VAT->getElementType(), RangeLoc))
                .getAsOpaquePtr(),
            EndVar->getSourceRange());
        if (SizeOfEachElementExprR.isInvalid())
          return StmtError();

        BoundExpr =
            ActOnBinOp(S, EndVar->getLocation(), tok::slash,
                       SizeOfVLAExprR.get(), SizeOfEachElementExprR.get());
        if (BoundExpr.isInvalid())
          return StmtError();

      } else {
        // Can't be a DependentSizedArrayType or an IncompleteArrayType since
        // UnqAT is not incomplete and Range is not type-dependent.
        llvm_unreachable("Unexpected array type in for-range");
      }

      // end-expr is __range + __bound.
      EndExpr = ActOnBinOp(S, ColonLoc, tok::plus, EndRangeRef.get(),
                           BoundExpr.get());
      if (EndExpr.isInvalid())
        return StmtError();
      if (FinishForRangeVarDecl(*this, EndVar, EndExpr.get(), ColonLoc,
                                diag::err_for_range_iter_deduction_failure)) {
        NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
        return StmtError();
      }
    } else {
      OverloadCandidateSet CandidateSet(RangeLoc,
                                        OverloadCandidateSet::CSK_Normal);
      BeginEndFunction BEFFailure;
      ForRangeStatus RangeStatus = BuildNonArrayForRange(
          *this, BeginRangeRef.get(), EndRangeRef.get(), RangeType, BeginVar,
          EndVar, ColonLoc, CoawaitLoc, &CandidateSet, &BeginExpr, &EndExpr,
          &BEFFailure);

      if (Kind == BFRK_Build && RangeStatus == FRS_NoViableFunction &&
          BEFFailure == BEF_begin) {
        // If the range is being built from an array parameter, emit a
        // a diagnostic that it is being treated as a pointer.
        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Range)) {
          if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
            QualType ArrayTy = PVD->getOriginalType();
            QualType PointerTy = PVD->getType();
            if (PointerTy->isPointerType() && ArrayTy->isArrayType()) {
              Diag(Range->getBeginLoc(), diag::err_range_on_array_parameter)
                  << RangeLoc << PVD << ArrayTy << PointerTy;
              Diag(PVD->getLocation(), diag::note_declared_at);
              return StmtError();
            }
          }
        }

        // If building the range failed, try dereferencing the range expression
        // unless a diagnostic was issued or the end function is problematic.
        StmtResult SR = RebuildForRangeWithDereference(*this, S, ForLoc,
                                                       CoawaitLoc, InitStmt,
                                                       LoopVarDecl, ColonLoc,
                                                       Range, RangeLoc,
                                                       RParenLoc);
        if (SR.isInvalid() || SR.isUsable())
          return SR;
      }

      // Otherwise, emit diagnostics if we haven't already.
      if (RangeStatus == FRS_NoViableFunction) {
        Expr *Range = BEFFailure ? EndRangeRef.get() : BeginRangeRef.get();
        CandidateSet.NoteCandidates(
            PartialDiagnosticAt(Range->getBeginLoc(),
                                PDiag(diag::err_for_range_invalid)
                                    << RangeLoc << Range->getType()
                                    << BEFFailure),
            *this, OCD_AllCandidates, Range);
      }
      // Return an error if no fix was discovered.
      if (RangeStatus != FRS_Success)
        return StmtError();
    }

    assert(!BeginExpr.isInvalid() && !EndExpr.isInvalid() &&
           "invalid range expression in for loop");

    // C++11 [dcl.spec.auto]p7: BeginType and EndType must be the same.
    // C++1z removes this restriction.
    QualType BeginType = BeginVar->getType(), EndType = EndVar->getType();
    if (!Context.hasSameType(BeginType, EndType)) {
      Diag(RangeLoc, getLangOpts().CPlusPlus17
                         ? diag::warn_for_range_begin_end_types_differ
                         : diag::ext_for_range_begin_end_types_differ)
          << BeginType << EndType;
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
    }

    BeginDeclStmt =
        ActOnDeclStmt(ConvertDeclToDeclGroup(BeginVar), ColonLoc, ColonLoc);
    EndDeclStmt =
        ActOnDeclStmt(ConvertDeclToDeclGroup(EndVar), ColonLoc, ColonLoc);

    const QualType BeginRefNonRefType = BeginType.getNonReferenceType();
    ExprResult BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                           VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    ExprResult EndRef = BuildDeclRefExpr(EndVar, EndType.getNonReferenceType(),
                                         VK_LValue, ColonLoc);
    if (EndRef.isInvalid())
      return StmtError();

    // Build and check __begin != __end expression.
    NotEqExpr = ActOnBinOp(S, ColonLoc, tok::exclaimequal,
                           BeginRef.get(), EndRef.get());
    if (!NotEqExpr.isInvalid())
      NotEqExpr = CheckBooleanCondition(ColonLoc, NotEqExpr.get());
    if (!NotEqExpr.isInvalid())
      NotEqExpr =
          ActOnFinishFullExpr(NotEqExpr.get(), /*DiscardedValue*/ false);
    if (NotEqExpr.isInvalid()) {
      Diag(RangeLoc, diag::note_for_range_invalid_iterator)
        << RangeLoc << 0 << BeginRangeRef.get()->getType();
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      if (!Context.hasSameType(BeginType, EndType))
        NoteForRangeBeginEndFunction(*this, EndExpr.get(), BEF_end);
      return StmtError();
    }

    // Build and check ++__begin expression.
    BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    IncrExpr = ActOnUnaryOp(S, ColonLoc, tok::plusplus, BeginRef.get());
    if (!IncrExpr.isInvalid() && CoawaitLoc.isValid())
      // FIXME: getCurScope() should not be used during template instantiation.
      // We should pick up the set of unqualified lookup results for operator
      // co_await during the initial parse.
      IncrExpr = ActOnCoawaitExpr(S, CoawaitLoc, IncrExpr.get());
    if (!IncrExpr.isInvalid())
      IncrExpr = ActOnFinishFullExpr(IncrExpr.get(), /*DiscardedValue*/ false);
    if (IncrExpr.isInvalid()) {
      Diag(RangeLoc, diag::note_for_range_invalid_iterator)
        << RangeLoc << 2 << BeginRangeRef.get()->getType() ;
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      return StmtError();
    }

    // Build and check *__begin  expression.
    BeginRef = BuildDeclRefExpr(BeginVar, BeginRefNonRefType,
                                VK_LValue, ColonLoc);
    if (BeginRef.isInvalid())
      return StmtError();

    ExprResult DerefExpr = ActOnUnaryOp(S, ColonLoc, tok::star, BeginRef.get());
    if (DerefExpr.isInvalid()) {
      Diag(RangeLoc, diag::note_for_range_invalid_iterator)
        << RangeLoc << 1 << BeginRangeRef.get()->getType();
      NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
      return StmtError();
    }

    // Attach  *__begin  as initializer for VD. Don't touch it if we're just
    // trying to determine whether this would be a valid range.
    if (!LoopVar->isInvalidDecl() && Kind != BFRK_Check) {
      AddInitializerToDecl(LoopVar, DerefExpr.get(), /*DirectInit=*/false);
      if (LoopVar->isInvalidDecl() ||
          (LoopVar->getInit() && LoopVar->getInit()->containsErrors()))
        NoteForRangeBeginEndFunction(*this, BeginExpr.get(), BEF_begin);
    }
  }

  // Don't bother to actually allocate the result if we're just trying to
  // determine whether it would be valid.
  if (Kind == BFRK_Check)
    return StmtResult();

  // In OpenMP loop region loop control variable must be private. Perform
  // analysis of first part (if any).
  if (getLangOpts().OpenMP >= 50 && BeginDeclStmt.isUsable())
    OpenMP().ActOnOpenMPLoopInitialization(ForLoc, BeginDeclStmt.get());

  return new (Context) CXXForRangeStmt(
      InitStmt, RangeDS, cast_or_null<DeclStmt>(BeginDeclStmt.get()),
      cast_or_null<DeclStmt>(EndDeclStmt.get()), NotEqExpr.get(),
      IncrExpr.get(), LoopVarDS, /*Body=*/nullptr, ForLoc, CoawaitLoc,
      ColonLoc, RParenLoc);
}

// Warn when the loop variable is a const reference that creates a copy.
// Suggest using the non-reference type for copies.  If a copy can be prevented
// suggest the const reference type that would do so.
// For instance, given "for (const &Foo : Range)", suggest
// "for (const Foo : Range)" to denote a copy is made for the loop.  If
// possible, also suggest "for (const &Bar : Range)" if this type prevents
// the copy altogether.
static void DiagnoseForRangeReferenceVariableCopies(Sema &SemaRef,
                                                    const VarDecl *VD,
                                                    QualType RangeInitType) {
  const Expr *InitExpr = VD->getInit();
  if (!InitExpr)
    return;

  QualType VariableType = VD->getType();

  if (auto Cleanups = dyn_cast<ExprWithCleanups>(InitExpr))
    if (!Cleanups->cleanupsHaveSideEffects())
      InitExpr = Cleanups->getSubExpr();

  const MaterializeTemporaryExpr *MTE =
      dyn_cast<MaterializeTemporaryExpr>(InitExpr);

  // No copy made.
  if (!MTE)
    return;

  const Expr *E = MTE->getSubExpr()->IgnoreImpCasts();

  // Searching for either UnaryOperator for dereference of a pointer or
  // CXXOperatorCallExpr for handling iterators.
  while (!isa<CXXOperatorCallExpr>(E) && !isa<UnaryOperator>(E)) {
    if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(E)) {
      E = CCE->getArg(0);
    } else if (const CXXMemberCallExpr *Call = dyn_cast<CXXMemberCallExpr>(E)) {
      const MemberExpr *ME = cast<MemberExpr>(Call->getCallee());
      E = ME->getBase();
    } else {
      const MaterializeTemporaryExpr *MTE = cast<MaterializeTemporaryExpr>(E);
      E = MTE->getSubExpr();
    }
    E = E->IgnoreImpCasts();
  }

  QualType ReferenceReturnType;
  if (isa<UnaryOperator>(E)) {
    ReferenceReturnType = SemaRef.Context.getLValueReferenceType(E->getType());
  } else {
    const CXXOperatorCallExpr *Call = cast<CXXOperatorCallExpr>(E);
    const FunctionDecl *FD = Call->getDirectCallee();
    QualType ReturnType = FD->getReturnType();
    if (ReturnType->isReferenceType())
      ReferenceReturnType = ReturnType;
  }

  if (!ReferenceReturnType.isNull()) {
    // Loop variable creates a temporary.  Suggest either to go with
    // non-reference loop variable to indicate a copy is made, or
    // the correct type to bind a const reference.
    SemaRef.Diag(VD->getLocation(),
                 diag::warn_for_range_const_ref_binds_temp_built_from_ref)
        << VD << VariableType << ReferenceReturnType;
    QualType NonReferenceType = VariableType.getNonReferenceType();
    NonReferenceType.removeLocalConst();
    QualType NewReferenceType =
        SemaRef.Context.getLValueReferenceType(E->getType().withConst());
    SemaRef.Diag(VD->getBeginLoc(), diag::note_use_type_or_non_reference)
        << NonReferenceType << NewReferenceType << VD->getSourceRange()
        << FixItHint::CreateRemoval(VD->getTypeSpecEndLoc());
  } else if (!VariableType->isRValueReferenceType()) {
    // The range always returns a copy, so a temporary is always created.
    // Suggest removing the reference from the loop variable.
    // If the type is a rvalue reference do not warn since that changes the
    // semantic of the code.
    SemaRef.Diag(VD->getLocation(), diag::warn_for_range_ref_binds_ret_temp)
        << VD << RangeInitType;
    QualType NonReferenceType = VariableType.getNonReferenceType();
    NonReferenceType.removeLocalConst();
    SemaRef.Diag(VD->getBeginLoc(), diag::note_use_non_reference_type)
        << NonReferenceType << VD->getSourceRange()
        << FixItHint::CreateRemoval(VD->getTypeSpecEndLoc());
  }
}

/// Determines whether the @p VariableType's declaration is a record with the
/// clang::trivial_abi attribute.
static bool hasTrivialABIAttr(QualType VariableType) {
  if (CXXRecordDecl *RD = VariableType->getAsCXXRecordDecl())
    return RD->hasAttr<TrivialABIAttr>();

  return false;
}

// Warns when the loop variable can be changed to a reference type to
// prevent a copy.  For instance, if given "for (const Foo x : Range)" suggest
// "for (const Foo &x : Range)" if this form does not make a copy.
static void DiagnoseForRangeConstVariableCopies(Sema &SemaRef,
                                                const VarDecl *VD) {
  const Expr *InitExpr = VD->getInit();
  if (!InitExpr)
    return;

  QualType VariableType = VD->getType();

  if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(InitExpr)) {
    if (!CE->getConstructor()->isCopyConstructor())
      return;
  } else if (const CastExpr *CE = dyn_cast<CastExpr>(InitExpr)) {
    if (CE->getCastKind() != CK_LValueToRValue)
      return;
  } else {
    return;
  }

  // Small trivially copyable types are cheap to copy. Do not emit the
  // diagnostic for these instances. 64 bytes is a common size of a cache line.
  // (The function `getTypeSize` returns the size in bits.)
  ASTContext &Ctx = SemaRef.Context;
  if (Ctx.getTypeSize(VariableType) <= 64 * 8 &&
      (VariableType.isTriviallyCopyConstructibleType(Ctx) ||
       hasTrivialABIAttr(VariableType)))
    return;

  // Suggest changing from a const variable to a const reference variable
  // if doing so will prevent a copy.
  SemaRef.Diag(VD->getLocation(), diag::warn_for_range_copy)
      << VD << VariableType;
  SemaRef.Diag(VD->getBeginLoc(), diag::note_use_reference_type)
      << SemaRef.Context.getLValueReferenceType(VariableType)
      << VD->getSourceRange()
      << FixItHint::CreateInsertion(VD->getLocation(), "&");
}

/// DiagnoseForRangeVariableCopies - Diagnose three cases and fixes for them.
/// 1) for (const foo &x : foos) where foos only returns a copy.  Suggest
///    using "const foo x" to show that a copy is made
/// 2) for (const bar &x : foos) where bar is a temporary initialized by bar.
///    Suggest either "const bar x" to keep the copying or "const foo& x" to
///    prevent the copy.
/// 3) for (const foo x : foos) where x is constructed from a reference foo.
///    Suggest "const foo &x" to prevent the copy.
static void DiagnoseForRangeVariableCopies(Sema &SemaRef,
                                           const CXXForRangeStmt *ForStmt) {
  if (SemaRef.inTemplateInstantiation())
    return;

  if (SemaRef.Diags.isIgnored(
          diag::warn_for_range_const_ref_binds_temp_built_from_ref,
          ForStmt->getBeginLoc()) &&
      SemaRef.Diags.isIgnored(diag::warn_for_range_ref_binds_ret_temp,
                              ForStmt->getBeginLoc()) &&
      SemaRef.Diags.isIgnored(diag::warn_for_range_copy,
                              ForStmt->getBeginLoc())) {
    return;
  }

  const VarDecl *VD = ForStmt->getLoopVariable();
  if (!VD)
    return;

  QualType VariableType = VD->getType();

  if (VariableType->isIncompleteType())
    return;

  const Expr *InitExpr = VD->getInit();
  if (!InitExpr)
    return;

  if (InitExpr->getExprLoc().isMacroID())
    return;

  if (VariableType->isReferenceType()) {
    DiagnoseForRangeReferenceVariableCopies(SemaRef, VD,
                                            ForStmt->getRangeInit()->getType());
  } else if (VariableType.isConstQualified()) {
    DiagnoseForRangeConstVariableCopies(SemaRef, VD);
  }
}

StmtResult Sema::FinishCXXForRangeStmt(Stmt *S, Stmt *B) {
  if (!S || !B)
    return StmtError();

  if (isa<ObjCForCollectionStmt>(S))
    return ObjC().FinishObjCForCollectionStmt(S, B);

  CXXForRangeStmt *ForStmt = cast<CXXForRangeStmt>(S);
  ForStmt->setBody(B);

  DiagnoseEmptyStmtBody(ForStmt->getRParenLoc(), B,
                        diag::warn_empty_range_based_for_body);

  DiagnoseForRangeVariableCopies(*this, ForStmt);

  return S;
}

StmtResult Sema::ActOnGotoStmt(SourceLocation GotoLoc,
                               SourceLocation LabelLoc,
                               LabelDecl *TheDecl) {
  setFunctionHasBranchIntoScope();

  // If this goto is in a compute construct scope, we need to make sure we check
  // gotos in/out.
  if (getCurScope()->isInOpenACCComputeConstructScope())
    setFunctionHasBranchProtectedScope();

  TheDecl->markUsed(Context);
  return new (Context) GotoStmt(TheDecl, GotoLoc, LabelLoc);
}

StmtResult
Sema::ActOnIndirectGotoStmt(SourceLocation GotoLoc, SourceLocation StarLoc,
                            Expr *E) {
  // Convert operand to void*
  if (!E->isTypeDependent()) {
    QualType ETy = E->getType();
    QualType DestTy = Context.getPointerType(Context.VoidTy.withConst());
    ExprResult ExprRes = E;
    AssignConvertType ConvTy =
      CheckSingleAssignmentConstraints(DestTy, ExprRes);
    if (ExprRes.isInvalid())
      return StmtError();
    E = ExprRes.get();
    if (DiagnoseAssignmentResult(ConvTy, StarLoc, DestTy, ETy, E, AA_Passing))
      return StmtError();
  }

  ExprResult ExprRes = ActOnFinishFullExpr(E, /*DiscardedValue*/ false);
  if (ExprRes.isInvalid())
    return StmtError();
  E = ExprRes.get();

  setFunctionHasIndirectGoto();

  // If this goto is in a compute construct scope, we need to make sure we
  // check gotos in/out.
  if (getCurScope()->isInOpenACCComputeConstructScope())
    setFunctionHasBranchProtectedScope();

  return new (Context) IndirectGotoStmt(GotoLoc, StarLoc, E);
}

static void CheckJumpOutOfSEHFinally(Sema &S, SourceLocation Loc,
                                     const Scope &DestScope) {
  if (!S.CurrentSEHFinally.empty() &&
      DestScope.Contains(*S.CurrentSEHFinally.back())) {
    S.Diag(Loc, diag::warn_jump_out_of_seh_finally);
  }
}

StmtResult
Sema::ActOnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope) {
  Scope *S = CurScope->getContinueParent();
  if (!S) {
    // C99 6.8.6.2p1: A break shall appear only in or as a loop body.
    return StmtError(Diag(ContinueLoc, diag::err_continue_not_in_loop));
  }
  if (S->isConditionVarScope()) {
    // We cannot 'continue;' from within a statement expression in the
    // initializer of a condition variable because we would jump past the
    // initialization of that variable.
    return StmtError(Diag(ContinueLoc, diag::err_continue_from_cond_var_init));
  }

  // A 'continue' that would normally have execution continue on a block outside
  // of a compute construct counts as 'branching out of' the compute construct,
  // so diagnose here.
  if (S->isOpenACCComputeConstructScope())
    return StmtError(
        Diag(ContinueLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*branch*/ 0 << /*out of */ 0);

  CheckJumpOutOfSEHFinally(*this, ContinueLoc, *S);

  return new (Context) ContinueStmt(ContinueLoc);
}

StmtResult
Sema::ActOnBreakStmt(SourceLocation BreakLoc, Scope *CurScope) {
  Scope *S = CurScope->getBreakParent();
  if (!S) {
    // C99 6.8.6.3p1: A break shall appear only in or as a switch/loop body.
    return StmtError(Diag(BreakLoc, diag::err_break_not_in_loop_or_switch));
  }
  if (S->isOpenMPLoopScope())
    return StmtError(Diag(BreakLoc, diag::err_omp_loop_cannot_use_stmt)
                     << "break");

  // OpenACC doesn't allow 'break'ing from a compute construct, so diagnose if
  // we are trying to do so.  This can come in 2 flavors: 1-the break'able thing
  // (besides the compute construct) 'contains' the compute construct, at which
  // point the 'break' scope will be the compute construct.  Else it could be a
  // loop of some sort that has a direct parent of the compute construct.
  // However, a 'break' in a 'switch' marked as a compute construct doesn't
  // count as 'branch out of' the compute construct.
  if (S->isOpenACCComputeConstructScope() ||
      (S->isLoopScope() && S->getParent() &&
       S->getParent()->isOpenACCComputeConstructScope()))
    return StmtError(
        Diag(BreakLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*branch*/ 0 << /*out of */ 0);

  CheckJumpOutOfSEHFinally(*this, BreakLoc, *S);

  return new (Context) BreakStmt(BreakLoc);
}

Sema::NamedReturnInfo Sema::getNamedReturnInfo(Expr *&E,
                                               SimplerImplicitMoveMode Mode) {
  if (!E)
    return NamedReturnInfo();
  // - in a return statement in a function [where] ...
  // ... the expression is the name of a non-volatile automatic object ...
  const auto *DR = dyn_cast<DeclRefExpr>(E->IgnoreParens());
  if (!DR || DR->refersToEnclosingVariableOrCapture())
    return NamedReturnInfo();
  const auto *VD = dyn_cast<VarDecl>(DR->getDecl());
  if (!VD)
    return NamedReturnInfo();
  if (VD->getInit() && VD->getInit()->containsErrors())
    return NamedReturnInfo();
  NamedReturnInfo Res = getNamedReturnInfo(VD);
  if (Res.Candidate && !E->isXValue() &&
      (Mode == SimplerImplicitMoveMode::ForceOn ||
       (Mode != SimplerImplicitMoveMode::ForceOff &&
        getLangOpts().CPlusPlus23))) {
    E = ImplicitCastExpr::Create(Context, VD->getType().getNonReferenceType(),
                                 CK_NoOp, E, nullptr, VK_XValue,
                                 FPOptionsOverride());
  }
  return Res;
}

Sema::NamedReturnInfo Sema::getNamedReturnInfo(const VarDecl *VD) {
  NamedReturnInfo Info{VD, NamedReturnInfo::MoveEligibleAndCopyElidable};

  // C++20 [class.copy.elision]p3:
  // - in a return statement in a function with ...
  // (other than a function ... parameter)
  if (VD->getKind() == Decl::ParmVar)
    Info.S = NamedReturnInfo::MoveEligible;
  else if (VD->getKind() != Decl::Var)
    return NamedReturnInfo();

  // (other than ... a catch-clause parameter)
  if (VD->isExceptionVariable())
    Info.S = NamedReturnInfo::MoveEligible;

  // ...automatic...
  if (!VD->hasLocalStorage())
    return NamedReturnInfo();

  // We don't want to implicitly move out of a __block variable during a return
  // because we cannot assume the variable will no longer be used.
  if (VD->hasAttr<BlocksAttr>())
    return NamedReturnInfo();

  QualType VDType = VD->getType();
  if (VDType->isObjectType()) {
    // C++17 [class.copy.elision]p3:
    // ...non-volatile automatic object...
    if (VDType.isVolatileQualified())
      return NamedReturnInfo();
  } else if (VDType->isRValueReferenceType()) {
    // C++20 [class.copy.elision]p3:
    // ...either a non-volatile object or an rvalue reference to a non-volatile
    // object type...
    QualType VDReferencedType = VDType.getNonReferenceType();
    if (VDReferencedType.isVolatileQualified() ||
        !VDReferencedType->isObjectType())
      return NamedReturnInfo();
    Info.S = NamedReturnInfo::MoveEligible;
  } else {
    return NamedReturnInfo();
  }

  // Variables with higher required alignment than their type's ABI
  // alignment cannot use NRVO.
  if (!VD->hasDependentAlignment() && !VDType->isIncompleteType() &&
      Context.getDeclAlign(VD) > Context.getTypeAlignInChars(VDType))
    Info.S = NamedReturnInfo::MoveEligible;

  return Info;
}

const VarDecl *Sema::getCopyElisionCandidate(NamedReturnInfo &Info,
                                             QualType ReturnType) {
  if (!Info.Candidate)
    return nullptr;

  auto invalidNRVO = [&] {
    Info = NamedReturnInfo();
    return nullptr;
  };

  // If we got a non-deduced auto ReturnType, we are in a dependent context and
  // there is no point in allowing copy elision since we won't have it deduced
  // by the point the VardDecl is instantiated, which is the last chance we have
  // of deciding if the candidate is really copy elidable.
  if ((ReturnType->getTypeClass() == Type::TypeClass::Auto &&
       ReturnType->isCanonicalUnqualified()) ||
      ReturnType->isSpecificBuiltinType(BuiltinType::Dependent))
    return invalidNRVO();

  if (!ReturnType->isDependentType()) {
    // - in a return statement in a function with ...
    // ... a class return type ...
    if (!ReturnType->isRecordType())
      return invalidNRVO();

    QualType VDType = Info.Candidate->getType();
    // ... the same cv-unqualified type as the function return type ...
    // When considering moving this expression out, allow dissimilar types.
    if (!VDType->isDependentType() &&
        !Context.hasSameUnqualifiedType(ReturnType, VDType))
      Info.S = NamedReturnInfo::MoveEligible;
  }
  return Info.isCopyElidable() ? Info.Candidate : nullptr;
}

/// Verify that the initialization sequence that was picked for the
/// first overload resolution is permissible under C++98.
///
/// Reject (possibly converting) constructors not taking an rvalue reference,
/// or user conversion operators which are not ref-qualified.
static bool
VerifyInitializationSequenceCXX98(const Sema &S,
                                  const InitializationSequence &Seq) {
  const auto *Step = llvm::find_if(Seq.steps(), [](const auto &Step) {
    return Step.Kind == InitializationSequence::SK_ConstructorInitialization ||
           Step.Kind == InitializationSequence::SK_UserConversion;
  });
  if (Step != Seq.step_end()) {
    const auto *FD = Step->Function.Function;
    if (isa<CXXConstructorDecl>(FD)
            ? !FD->getParamDecl(0)->getType()->isRValueReferenceType()
            : cast<CXXMethodDecl>(FD)->getRefQualifier() == RQ_None)
      return false;
  }
  return true;
}

ExprResult Sema::PerformMoveOrCopyInitialization(
    const InitializedEntity &Entity, const NamedReturnInfo &NRInfo, Expr *Value,
    bool SupressSimplerImplicitMoves) {
  if (getLangOpts().CPlusPlus &&
      (!getLangOpts().CPlusPlus23 || SupressSimplerImplicitMoves) &&
      NRInfo.isMoveEligible()) {
    ImplicitCastExpr AsRvalue(ImplicitCastExpr::OnStack, Value->getType(),
                              CK_NoOp, Value, VK_XValue, FPOptionsOverride());
    Expr *InitExpr = &AsRvalue;
    auto Kind = InitializationKind::CreateCopy(Value->getBeginLoc(),
                                               Value->getBeginLoc());
    InitializationSequence Seq(*this, Entity, Kind, InitExpr);
    auto Res = Seq.getFailedOverloadResult();
    if ((Res == OR_Success || Res == OR_Deleted) &&
        (getLangOpts().CPlusPlus11 ||
         VerifyInitializationSequenceCXX98(*this, Seq))) {
      // Promote "AsRvalue" to the heap, since we now need this
      // expression node to persist.
      Value =
          ImplicitCastExpr::Create(Context, Value->getType(), CK_NoOp, Value,
                                   nullptr, VK_XValue, FPOptionsOverride());
      // Complete type-checking the initialization of the return type
      // using the constructor we found.
      return Seq.Perform(*this, Entity, Kind, Value);
    }
  }
  // Either we didn't meet the criteria for treating an lvalue as an rvalue,
  // above, or overload resolution failed. Either way, we need to try
  // (again) now with the return value expression as written.
  return PerformCopyInitialization(Entity, SourceLocation(), Value);
}

/// Determine whether the declared return type of the specified function
/// contains 'auto'.
static bool hasDeducedReturnType(FunctionDecl *FD) {
  const FunctionProtoType *FPT =
      FD->getTypeSourceInfo()->getType()->castAs<FunctionProtoType>();
  return FPT->getReturnType()->isUndeducedType();
}

StmtResult Sema::ActOnCapScopeReturnStmt(SourceLocation ReturnLoc,
                                         Expr *RetValExp,
                                         NamedReturnInfo &NRInfo,
                                         bool SupressSimplerImplicitMoves) {
  // If this is the first return we've seen, infer the return type.
  // [expr.prim.lambda]p4 in C++11; block literals follow the same rules.
  CapturingScopeInfo *CurCap = cast<CapturingScopeInfo>(getCurFunction());
  QualType FnRetType = CurCap->ReturnType;
  LambdaScopeInfo *CurLambda = dyn_cast<LambdaScopeInfo>(CurCap);
  if (CurLambda && CurLambda->CallOperator->getType().isNull())
    return StmtError();
  bool HasDeducedReturnType =
      CurLambda && hasDeducedReturnType(CurLambda->CallOperator);

  if (ExprEvalContexts.back().isDiscardedStatementContext() &&
      (HasDeducedReturnType || CurCap->HasImplicitReturnType)) {
    if (RetValExp) {
      ExprResult ER =
          ActOnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
      if (ER.isInvalid())
        return StmtError();
      RetValExp = ER.get();
    }
    return ReturnStmt::Create(Context, ReturnLoc, RetValExp,
                              /* NRVOCandidate=*/nullptr);
  }

  if (HasDeducedReturnType) {
    FunctionDecl *FD = CurLambda->CallOperator;
    // If we've already decided this lambda is invalid, e.g. because
    // we saw a `return` whose expression had an error, don't keep
    // trying to deduce its return type.
    if (FD->isInvalidDecl())
      return StmtError();
    // In C++1y, the return type may involve 'auto'.
    // FIXME: Blocks might have a return type of 'auto' explicitly specified.
    if (CurCap->ReturnType.isNull())
      CurCap->ReturnType = FD->getReturnType();

    AutoType *AT = CurCap->ReturnType->getContainedAutoType();
    assert(AT && "lost auto type from lambda return type");
    if (DeduceFunctionTypeFromReturnExpr(FD, ReturnLoc, RetValExp, AT)) {
      FD->setInvalidDecl();
      // FIXME: preserve the ill-formed return expression.
      return StmtError();
    }
    CurCap->ReturnType = FnRetType = FD->getReturnType();
  } else if (CurCap->HasImplicitReturnType) {
    // For blocks/lambdas with implicit return types, we check each return
    // statement individually, and deduce the common return type when the block
    // or lambda is completed.
    // FIXME: Fold this into the 'auto' codepath above.
    if (RetValExp && !isa<InitListExpr>(RetValExp)) {
      ExprResult Result = DefaultFunctionArrayLvalueConversion(RetValExp);
      if (Result.isInvalid())
        return StmtError();
      RetValExp = Result.get();

      // DR1048: even prior to C++14, we should use the 'auto' deduction rules
      // when deducing a return type for a lambda-expression (or by extension
      // for a block). These rules differ from the stated C++11 rules only in
      // that they remove top-level cv-qualifiers.
      if (!CurContext->isDependentContext())
        FnRetType = RetValExp->getType().getUnqualifiedType();
      else
        FnRetType = CurCap->ReturnType = Context.DependentTy;
    } else {
      if (RetValExp) {
        // C++11 [expr.lambda.prim]p4 bans inferring the result from an
        // initializer list, because it is not an expression (even
        // though we represent it as one). We still deduce 'void'.
        Diag(ReturnLoc, diag::err_lambda_return_init_list)
          << RetValExp->getSourceRange();
      }

      FnRetType = Context.VoidTy;
    }

    // Although we'll properly infer the type of the block once it's completed,
    // make sure we provide a return type now for better error recovery.
    if (CurCap->ReturnType.isNull())
      CurCap->ReturnType = FnRetType;
  }
  const VarDecl *NRVOCandidate = getCopyElisionCandidate(NRInfo, FnRetType);

  if (auto *CurBlock = dyn_cast<BlockScopeInfo>(CurCap)) {
    if (CurBlock->FunctionType->castAs<FunctionType>()->getNoReturnAttr()) {
      Diag(ReturnLoc, diag::err_noreturn_block_has_return_expr);
      return StmtError();
    }
  } else if (auto *CurRegion = dyn_cast<CapturedRegionScopeInfo>(CurCap)) {
    Diag(ReturnLoc, diag::err_return_in_captured_stmt) << CurRegion->getRegionName();
    return StmtError();
  } else {
    assert(CurLambda && "unknown kind of captured scope");
    if (CurLambda->CallOperator->getType()
            ->castAs<FunctionType>()
            ->getNoReturnAttr()) {
      Diag(ReturnLoc, diag::err_noreturn_lambda_has_return_expr);
      return StmtError();
    }
  }

  // Otherwise, verify that this result type matches the previous one.  We are
  // pickier with blocks than for normal functions because we don't have GCC
  // compatibility to worry about here.
  if (FnRetType->isDependentType()) {
    // Delay processing for now.  TODO: there are lots of dependent
    // types we can conclusively prove aren't void.
  } else if (FnRetType->isVoidType()) {
    if (RetValExp && !isa<InitListExpr>(RetValExp) &&
        !(getLangOpts().CPlusPlus &&
          (RetValExp->isTypeDependent() ||
           RetValExp->getType()->isVoidType()))) {
      if (!getLangOpts().CPlusPlus &&
          RetValExp->getType()->isVoidType())
        Diag(ReturnLoc, diag::ext_return_has_void_expr) << "literal" << 2;
      else {
        Diag(ReturnLoc, diag::err_return_block_has_expr);
        RetValExp = nullptr;
      }
    }
  } else if (!RetValExp) {
    return StmtError(Diag(ReturnLoc, diag::err_block_return_missing_expr));
  } else if (!RetValExp->isTypeDependent()) {
    // we have a non-void block with an expression, continue checking

    // C99 6.8.6.4p3(136): The return statement is not an assignment. The
    // overlap restriction of subclause 6.5.16.1 does not apply to the case of
    // function return.

    // In C++ the return statement is handled via a copy initialization.
    // the C version of which boils down to CheckSingleAssignmentConstraints.
    InitializedEntity Entity =
        InitializedEntity::InitializeResult(ReturnLoc, FnRetType);
    ExprResult Res = PerformMoveOrCopyInitialization(
        Entity, NRInfo, RetValExp, SupressSimplerImplicitMoves);
    if (Res.isInvalid()) {
      // FIXME: Cleanup temporaries here, anyway?
      return StmtError();
    }
    RetValExp = Res.get();
    CheckReturnValExpr(RetValExp, FnRetType, ReturnLoc);
  }

  if (RetValExp) {
    ExprResult ER =
        ActOnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
    if (ER.isInvalid())
      return StmtError();
    RetValExp = ER.get();
  }
  auto *Result =
      ReturnStmt::Create(Context, ReturnLoc, RetValExp, NRVOCandidate);

  // If we need to check for the named return value optimization,
  // or if we need to infer the return type,
  // save the return statement in our scope for later processing.
  if (CurCap->HasImplicitReturnType || NRVOCandidate)
    FunctionScopes.back()->Returns.push_back(Result);

  if (FunctionScopes.back()->FirstReturnLoc.isInvalid())
    FunctionScopes.back()->FirstReturnLoc = ReturnLoc;

  if (auto *CurBlock = dyn_cast<BlockScopeInfo>(CurCap);
      CurBlock && CurCap->HasImplicitReturnType && RetValExp &&
      RetValExp->containsErrors())
    CurBlock->TheDecl->setInvalidDecl();

  return Result;
}

namespace {
/// Marks all typedefs in all local classes in a type referenced.
///
/// In a function like
/// auto f() {
///   struct S { typedef int a; };
///   return S();
/// }
///
/// the local type escapes and could be referenced in some TUs but not in
/// others. Pretend that all local typedefs are always referenced, to not warn
/// on this. This isn't necessary if f has internal linkage, or the typedef
/// is private.
class LocalTypedefNameReferencer
    : public RecursiveASTVisitor<LocalTypedefNameReferencer> {
public:
  LocalTypedefNameReferencer(Sema &S) : S(S) {}
  bool VisitRecordType(const RecordType *RT);
private:
  Sema &S;
};
bool LocalTypedefNameReferencer::VisitRecordType(const RecordType *RT) {
  auto *R = dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!R || !R->isLocalClass() || !R->isLocalClass()->isExternallyVisible() ||
      R->isDependentType())
    return true;
  for (auto *TmpD : R->decls())
    if (auto *T = dyn_cast<TypedefNameDecl>(TmpD))
      if (T->getAccess() != AS_private || R->hasFriends())
        S.MarkAnyDeclReferenced(T->getLocation(), T, /*OdrUse=*/false);
  return true;
}
}

TypeLoc Sema::getReturnTypeLoc(FunctionDecl *FD) const {
  return FD->getTypeSourceInfo()
      ->getTypeLoc()
      .getAsAdjusted<FunctionProtoTypeLoc>()
      .getReturnLoc();
}

bool Sema::DeduceFunctionTypeFromReturnExpr(FunctionDecl *FD,
                                            SourceLocation ReturnLoc,
                                            Expr *RetExpr, const AutoType *AT) {
  // If this is the conversion function for a lambda, we choose to deduce its
  // type from the corresponding call operator, not from the synthesized return
  // statement within it. See Sema::DeduceReturnType.
  if (isLambdaConversionOperator(FD))
    return false;

  if (isa_and_nonnull<InitListExpr>(RetExpr)) {
    //  If the deduction is for a return statement and the initializer is
    //  a braced-init-list, the program is ill-formed.
    Diag(RetExpr->getExprLoc(),
         getCurLambda() ? diag::err_lambda_return_init_list
                        : diag::err_auto_fn_return_init_list)
        << RetExpr->getSourceRange();
    return true;
  }

  if (FD->isDependentContext()) {
    // C++1y [dcl.spec.auto]p12:
    //   Return type deduction [...] occurs when the definition is
    //   instantiated even if the function body contains a return
    //   statement with a non-type-dependent operand.
    assert(AT->isDeduced() && "should have deduced to dependent type");
    return false;
  }

  TypeLoc OrigResultType = getReturnTypeLoc(FD);
  //  In the case of a return with no operand, the initializer is considered
  //  to be void().
  CXXScalarValueInitExpr VoidVal(Context.VoidTy, nullptr, SourceLocation());
  if (!RetExpr) {
    // For a function with a deduced result type to return with omitted
    // expression, the result type as written must be 'auto' or
    // 'decltype(auto)', possibly cv-qualified or constrained, but not
    // ref-qualified.
    if (!OrigResultType.getType()->getAs<AutoType>()) {
      Diag(ReturnLoc, diag::err_auto_fn_return_void_but_not_auto)
          << OrigResultType.getType();
      return true;
    }
    RetExpr = &VoidVal;
  }

  QualType Deduced = AT->getDeducedType();
  {
    //  Otherwise, [...] deduce a value for U using the rules of template
    //  argument deduction.
    auto RetExprLoc = RetExpr->getExprLoc();
    TemplateDeductionInfo Info(RetExprLoc);
    SourceLocation TemplateSpecLoc;
    if (RetExpr->getType() == Context.OverloadTy) {
      auto FindResult = OverloadExpr::find(RetExpr);
      if (FindResult.Expression)
        TemplateSpecLoc = FindResult.Expression->getNameLoc();
    }
    TemplateSpecCandidateSet FailedTSC(TemplateSpecLoc);
    TemplateDeductionResult Res = DeduceAutoType(
        OrigResultType, RetExpr, Deduced, Info, /*DependentDeduction=*/false,
        /*IgnoreConstraints=*/false, &FailedTSC);
    if (Res != TemplateDeductionResult::Success && FD->isInvalidDecl())
      return true;
    switch (Res) {
    case TemplateDeductionResult::Success:
      break;
    case TemplateDeductionResult::AlreadyDiagnosed:
      return true;
    case TemplateDeductionResult::Inconsistent: {
      //  If a function with a declared return type that contains a placeholder
      //  type has multiple return statements, the return type is deduced for
      //  each return statement. [...] if the type deduced is not the same in
      //  each deduction, the program is ill-formed.
      const LambdaScopeInfo *LambdaSI = getCurLambda();
      if (LambdaSI && LambdaSI->HasImplicitReturnType)
        Diag(ReturnLoc, diag::err_typecheck_missing_return_type_incompatible)
            << Info.SecondArg << Info.FirstArg << true /*IsLambda*/;
      else
        Diag(ReturnLoc, diag::err_auto_fn_different_deductions)
            << (AT->isDecltypeAuto() ? 1 : 0) << Info.SecondArg
            << Info.FirstArg;
      return true;
    }
    default:
      Diag(RetExpr->getExprLoc(), diag::err_auto_fn_deduction_failure)
          << OrigResultType.getType() << RetExpr->getType();
      FailedTSC.NoteCandidates(*this, RetExprLoc);
      return true;
    }
  }

  // If a local type is part of the returned type, mark its fields as
  // referenced.
  LocalTypedefNameReferencer(*this).TraverseType(RetExpr->getType());

  // CUDA: Kernel function must have 'void' return type.
  if (getLangOpts().CUDA && FD->hasAttr<CUDAGlobalAttr>() &&
      !Deduced->isVoidType()) {
    Diag(FD->getLocation(), diag::err_kern_type_not_void_return)
        << FD->getType() << FD->getSourceRange();
    return true;
  }

  if (!FD->isInvalidDecl() && AT->getDeducedType() != Deduced)
    // Update all declarations of the function to have the deduced return type.
    Context.adjustDeducedFunctionResultType(FD, Deduced);

  return false;
}

StmtResult
Sema::ActOnReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                      Scope *CurScope) {
  // Correct typos, in case the containing function returns 'auto' and
  // RetValExp should determine the deduced type.
  ExprResult RetVal = CorrectDelayedTyposInExpr(
      RetValExp, nullptr, /*RecoverUncorrectedTypos=*/true);
  if (RetVal.isInvalid())
    return StmtError();

  if (getCurScope()->isInOpenACCComputeConstructScope())
    return StmtError(
        Diag(ReturnLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*return*/ 1 << /*out of */ 0);

  StmtResult R =
      BuildReturnStmt(ReturnLoc, RetVal.get(), /*AllowRecovery=*/true);
  if (R.isInvalid() || ExprEvalContexts.back().isDiscardedStatementContext())
    return R;

  VarDecl *VD =
      const_cast<VarDecl *>(cast<ReturnStmt>(R.get())->getNRVOCandidate());

  CurScope->updateNRVOCandidate(VD);

  CheckJumpOutOfSEHFinally(*this, ReturnLoc, *CurScope->getFnParent());

  return R;
}

static bool CheckSimplerImplicitMovesMSVCWorkaround(const Sema &S,
                                                    const Expr *E) {
  if (!E || !S.getLangOpts().CPlusPlus23 || !S.getLangOpts().MSVCCompat)
    return false;
  const Decl *D = E->getReferencedDeclOfCallee();
  if (!D || !S.SourceMgr.isInSystemHeader(D->getLocation()))
    return false;
  for (const DeclContext *DC = D->getDeclContext(); DC; DC = DC->getParent()) {
    if (DC->isStdNamespace())
      return true;
  }
  return false;
}

StmtResult Sema::BuildReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                                 bool AllowRecovery) {
  // Check for unexpanded parameter packs.
  if (RetValExp && DiagnoseUnexpandedParameterPack(RetValExp))
    return StmtError();

  // HACK: We suppress simpler implicit move here in msvc compatibility mode
  // just as a temporary work around, as the MSVC STL has issues with
  // this change.
  bool SupressSimplerImplicitMoves =
      CheckSimplerImplicitMovesMSVCWorkaround(*this, RetValExp);
  NamedReturnInfo NRInfo = getNamedReturnInfo(
      RetValExp, SupressSimplerImplicitMoves ? SimplerImplicitMoveMode::ForceOff
                                             : SimplerImplicitMoveMode::Normal);

  if (isa<CapturingScopeInfo>(getCurFunction()))
    return ActOnCapScopeReturnStmt(ReturnLoc, RetValExp, NRInfo,
                                   SupressSimplerImplicitMoves);

  QualType FnRetType;
  QualType RelatedRetType;
  const AttrVec *Attrs = nullptr;
  bool isObjCMethod = false;

  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    FnRetType = FD->getReturnType();
    if (FD->hasAttrs())
      Attrs = &FD->getAttrs();
    if (FD->isNoReturn())
      Diag(ReturnLoc, diag::warn_noreturn_function_has_return_expr) << FD;
    if (FD->isMain() && RetValExp)
      if (isa<CXXBoolLiteralExpr>(RetValExp))
        Diag(ReturnLoc, diag::warn_main_returns_bool_literal)
            << RetValExp->getSourceRange();
    if (FD->hasAttr<CmseNSEntryAttr>() && RetValExp) {
      if (const auto *RT = dyn_cast<RecordType>(FnRetType.getCanonicalType())) {
        if (RT->getDecl()->isOrContainsUnion())
          Diag(RetValExp->getBeginLoc(), diag::warn_cmse_nonsecure_union) << 1;
      }
    }
  } else if (ObjCMethodDecl *MD = getCurMethodDecl()) {
    FnRetType = MD->getReturnType();
    isObjCMethod = true;
    if (MD->hasAttrs())
      Attrs = &MD->getAttrs();
    if (MD->hasRelatedResultType() && MD->getClassInterface()) {
      // In the implementation of a method with a related return type, the
      // type used to type-check the validity of return statements within the
      // method body is a pointer to the type of the class being implemented.
      RelatedRetType = Context.getObjCInterfaceType(MD->getClassInterface());
      RelatedRetType = Context.getObjCObjectPointerType(RelatedRetType);
    }
  } else // If we don't have a function/method context, bail.
    return StmtError();

  if (RetValExp) {
    const auto *ATy = dyn_cast<ArrayType>(RetValExp->getType());
    if (ATy && ATy->getElementType().isWebAssemblyReferenceType()) {
      Diag(ReturnLoc, diag::err_wasm_table_art) << 1;
      return StmtError();
    }
  }

  // C++1z: discarded return statements are not considered when deducing a
  // return type.
  if (ExprEvalContexts.back().isDiscardedStatementContext() &&
      FnRetType->getContainedAutoType()) {
    if (RetValExp) {
      ExprResult ER =
          ActOnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
      if (ER.isInvalid())
        return StmtError();
      RetValExp = ER.get();
    }
    return ReturnStmt::Create(Context, ReturnLoc, RetValExp,
                              /* NRVOCandidate=*/nullptr);
  }

  // FIXME: Add a flag to the ScopeInfo to indicate whether we're performing
  // deduction.
  if (getLangOpts().CPlusPlus14) {
    if (AutoType *AT = FnRetType->getContainedAutoType()) {
      FunctionDecl *FD = cast<FunctionDecl>(CurContext);
      // If we've already decided this function is invalid, e.g. because
      // we saw a `return` whose expression had an error, don't keep
      // trying to deduce its return type.
      // (Some return values may be needlessly wrapped in RecoveryExpr).
      if (FD->isInvalidDecl() ||
          DeduceFunctionTypeFromReturnExpr(FD, ReturnLoc, RetValExp, AT)) {
        FD->setInvalidDecl();
        if (!AllowRecovery)
          return StmtError();
        // The deduction failure is diagnosed and marked, try to recover.
        if (RetValExp) {
          // Wrap return value with a recovery expression of the previous type.
          // If no deduction yet, use DependentTy.
          auto Recovery = CreateRecoveryExpr(
              RetValExp->getBeginLoc(), RetValExp->getEndLoc(), RetValExp,
              AT->isDeduced() ? FnRetType : QualType());
          if (Recovery.isInvalid())
            return StmtError();
          RetValExp = Recovery.get();
        } else {
          // Nothing to do: a ReturnStmt with no value is fine recovery.
        }
      } else {
        FnRetType = FD->getReturnType();
      }
    }
  }
  const VarDecl *NRVOCandidate = getCopyElisionCandidate(NRInfo, FnRetType);

  bool HasDependentReturnType = FnRetType->isDependentType();

  ReturnStmt *Result = nullptr;
  if (FnRetType->isVoidType()) {
    if (RetValExp) {
      if (auto *ILE = dyn_cast<InitListExpr>(RetValExp)) {
        // We simply never allow init lists as the return value of void
        // functions. This is compatible because this was never allowed before,
        // so there's no legacy code to deal with.
        NamedDecl *CurDecl = getCurFunctionOrMethodDecl();
        int FunctionKind = 0;
        if (isa<ObjCMethodDecl>(CurDecl))
          FunctionKind = 1;
        else if (isa<CXXConstructorDecl>(CurDecl))
          FunctionKind = 2;
        else if (isa<CXXDestructorDecl>(CurDecl))
          FunctionKind = 3;

        Diag(ReturnLoc, diag::err_return_init_list)
            << CurDecl << FunctionKind << RetValExp->getSourceRange();

        // Preserve the initializers in the AST.
        RetValExp = AllowRecovery
                        ? CreateRecoveryExpr(ILE->getLBraceLoc(),
                                             ILE->getRBraceLoc(), ILE->inits())
                              .get()
                        : nullptr;
      } else if (!RetValExp->isTypeDependent()) {
        // C99 6.8.6.4p1 (ext_ since GCC warns)
        unsigned D = diag::ext_return_has_expr;
        if (RetValExp->getType()->isVoidType()) {
          NamedDecl *CurDecl = getCurFunctionOrMethodDecl();
          if (isa<CXXConstructorDecl>(CurDecl) ||
              isa<CXXDestructorDecl>(CurDecl))
            D = diag::err_ctor_dtor_returns_void;
          else
            D = diag::ext_return_has_void_expr;
        }
        else {
          ExprResult Result = RetValExp;
          Result = IgnoredValueConversions(Result.get());
          if (Result.isInvalid())
            return StmtError();
          RetValExp = Result.get();
          RetValExp = ImpCastExprToType(RetValExp,
                                        Context.VoidTy, CK_ToVoid).get();
        }
        // return of void in constructor/destructor is illegal in C++.
        if (D == diag::err_ctor_dtor_returns_void) {
          NamedDecl *CurDecl = getCurFunctionOrMethodDecl();
          Diag(ReturnLoc, D) << CurDecl << isa<CXXDestructorDecl>(CurDecl)
                             << RetValExp->getSourceRange();
        }
        // return (some void expression); is legal in C++.
        else if (D != diag::ext_return_has_void_expr ||
                 !getLangOpts().CPlusPlus) {
          NamedDecl *CurDecl = getCurFunctionOrMethodDecl();

          int FunctionKind = 0;
          if (isa<ObjCMethodDecl>(CurDecl))
            FunctionKind = 1;
          else if (isa<CXXConstructorDecl>(CurDecl))
            FunctionKind = 2;
          else if (isa<CXXDestructorDecl>(CurDecl))
            FunctionKind = 3;

          Diag(ReturnLoc, D)
              << CurDecl << FunctionKind << RetValExp->getSourceRange();
        }
      }

      if (RetValExp) {
        ExprResult ER =
            ActOnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
        if (ER.isInvalid())
          return StmtError();
        RetValExp = ER.get();
      }
    }

    Result = ReturnStmt::Create(Context, ReturnLoc, RetValExp,
                                /* NRVOCandidate=*/nullptr);
  } else if (!RetValExp && !HasDependentReturnType) {
    FunctionDecl *FD = getCurFunctionDecl();

    if ((FD && FD->isInvalidDecl()) || FnRetType->containsErrors()) {
      // The intended return type might have been "void", so don't warn.
    } else if (getLangOpts().CPlusPlus11 && FD && FD->isConstexpr()) {
      // C++11 [stmt.return]p2
      Diag(ReturnLoc, diag::err_constexpr_return_missing_expr)
          << FD << FD->isConsteval();
      FD->setInvalidDecl();
    } else {
      // C99 6.8.6.4p1 (ext_ since GCC warns)
      // C90 6.6.6.4p4
      unsigned DiagID = getLangOpts().C99 ? diag::ext_return_missing_expr
                                          : diag::warn_return_missing_expr;
      // Note that at this point one of getCurFunctionDecl() or
      // getCurMethodDecl() must be non-null (see above).
      assert((getCurFunctionDecl() || getCurMethodDecl()) &&
             "Not in a FunctionDecl or ObjCMethodDecl?");
      bool IsMethod = FD == nullptr;
      const NamedDecl *ND =
          IsMethod ? cast<NamedDecl>(getCurMethodDecl()) : cast<NamedDecl>(FD);
      Diag(ReturnLoc, DiagID) << ND << IsMethod;
    }

    Result = ReturnStmt::Create(Context, ReturnLoc, /* RetExpr=*/nullptr,
                                /* NRVOCandidate=*/nullptr);
  } else {
    assert(RetValExp || HasDependentReturnType);
    QualType RetType = RelatedRetType.isNull() ? FnRetType : RelatedRetType;

    // C99 6.8.6.4p3(136): The return statement is not an assignment. The
    // overlap restriction of subclause 6.5.16.1 does not apply to the case of
    // function return.

    // In C++ the return statement is handled via a copy initialization,
    // the C version of which boils down to CheckSingleAssignmentConstraints.
    if (!HasDependentReturnType && !RetValExp->isTypeDependent()) {
      // we have a non-void function with an expression, continue checking
      InitializedEntity Entity =
          InitializedEntity::InitializeResult(ReturnLoc, RetType);
      ExprResult Res = PerformMoveOrCopyInitialization(
          Entity, NRInfo, RetValExp, SupressSimplerImplicitMoves);
      if (Res.isInvalid() && AllowRecovery)
        Res = CreateRecoveryExpr(RetValExp->getBeginLoc(),
                                 RetValExp->getEndLoc(), RetValExp, RetType);
      if (Res.isInvalid()) {
        // FIXME: Clean up temporaries here anyway?
        return StmtError();
      }
      RetValExp = Res.getAs<Expr>();

      // If we have a related result type, we need to implicitly
      // convert back to the formal result type.  We can't pretend to
      // initialize the result again --- we might end double-retaining
      // --- so instead we initialize a notional temporary.
      if (!RelatedRetType.isNull()) {
        Entity = InitializedEntity::InitializeRelatedResult(getCurMethodDecl(),
                                                            FnRetType);
        Res = PerformCopyInitialization(Entity, ReturnLoc, RetValExp);
        if (Res.isInvalid()) {
          // FIXME: Clean up temporaries here anyway?
          return StmtError();
        }
        RetValExp = Res.getAs<Expr>();
      }

      CheckReturnValExpr(RetValExp, FnRetType, ReturnLoc, isObjCMethod, Attrs,
                         getCurFunctionDecl());
    }

    if (RetValExp) {
      ExprResult ER =
          ActOnFinishFullExpr(RetValExp, ReturnLoc, /*DiscardedValue*/ false);
      if (ER.isInvalid())
        return StmtError();
      RetValExp = ER.get();
    }
    Result = ReturnStmt::Create(Context, ReturnLoc, RetValExp, NRVOCandidate);
  }

  // If we need to check for the named return value optimization, save the
  // return statement in our scope for later processing.
  if (Result->getNRVOCandidate())
    FunctionScopes.back()->Returns.push_back(Result);

  if (FunctionScopes.back()->FirstReturnLoc.isInvalid())
    FunctionScopes.back()->FirstReturnLoc = ReturnLoc;

  return Result;
}

StmtResult
Sema::ActOnCXXCatchBlock(SourceLocation CatchLoc, Decl *ExDecl,
                         Stmt *HandlerBlock) {
  // There's nothing to test that ActOnExceptionDecl didn't already test.
  return new (Context)
      CXXCatchStmt(CatchLoc, cast_or_null<VarDecl>(ExDecl), HandlerBlock);
}

namespace {
class CatchHandlerType {
  QualType QT;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsPointer : 1;

  // This is a special constructor to be used only with DenseMapInfo's
  // getEmptyKey() and getTombstoneKey() functions.
  friend struct llvm::DenseMapInfo<CatchHandlerType>;
  enum Unique { ForDenseMap };
  CatchHandlerType(QualType QT, Unique) : QT(QT), IsPointer(false) {}

public:
  /// Used when creating a CatchHandlerType from a handler type; will determine
  /// whether the type is a pointer or reference and will strip off the top
  /// level pointer and cv-qualifiers.
  CatchHandlerType(QualType Q) : QT(Q), IsPointer(false) {
    if (QT->isPointerType())
      IsPointer = true;

    QT = QT.getUnqualifiedType();
    if (IsPointer || QT->isReferenceType())
      QT = QT->getPointeeType();
  }

  /// Used when creating a CatchHandlerType from a base class type; pretends the
  /// type passed in had the pointer qualifier, does not need to get an
  /// unqualified type.
  CatchHandlerType(QualType QT, bool IsPointer)
      : QT(QT), IsPointer(IsPointer) {}

  QualType underlying() const { return QT; }
  bool isPointer() const { return IsPointer; }

  friend bool operator==(const CatchHandlerType &LHS,
                         const CatchHandlerType &RHS) {
    // If the pointer qualification does not match, we can return early.
    if (LHS.IsPointer != RHS.IsPointer)
      return false;
    // Otherwise, check the underlying type without cv-qualifiers.
    return LHS.QT == RHS.QT;
  }
};
} // namespace

namespace llvm {
template <> struct DenseMapInfo<CatchHandlerType> {
  static CatchHandlerType getEmptyKey() {
    return CatchHandlerType(DenseMapInfo<QualType>::getEmptyKey(),
                       CatchHandlerType::ForDenseMap);
  }

  static CatchHandlerType getTombstoneKey() {
    return CatchHandlerType(DenseMapInfo<QualType>::getTombstoneKey(),
                       CatchHandlerType::ForDenseMap);
  }

  static unsigned getHashValue(const CatchHandlerType &Base) {
    return DenseMapInfo<QualType>::getHashValue(Base.underlying());
  }

  static bool isEqual(const CatchHandlerType &LHS,
                      const CatchHandlerType &RHS) {
    return LHS == RHS;
  }
};
}

namespace {
class CatchTypePublicBases {
  const llvm::DenseMap<QualType, CXXCatchStmt *> &TypesToCheck;

  CXXCatchStmt *FoundHandler;
  QualType FoundHandlerType;
  QualType TestAgainstType;

public:
  CatchTypePublicBases(const llvm::DenseMap<QualType, CXXCatchStmt *> &T,
                       QualType QT)
      : TypesToCheck(T), FoundHandler(nullptr), TestAgainstType(QT) {}

  CXXCatchStmt *getFoundHandler() const { return FoundHandler; }
  QualType getFoundHandlerType() const { return FoundHandlerType; }

  bool operator()(const CXXBaseSpecifier *S, CXXBasePath &) {
    if (S->getAccessSpecifier() == AccessSpecifier::AS_public) {
      QualType Check = S->getType().getCanonicalType();
      const auto &M = TypesToCheck;
      auto I = M.find(Check);
      if (I != M.end()) {
        // We're pretty sure we found what we need to find. However, we still
        // need to make sure that we properly compare for pointers and
        // references, to handle cases like:
        //
        // } catch (Base *b) {
        // } catch (Derived &d) {
        // }
        //
        // where there is a qualification mismatch that disqualifies this
        // handler as a potential problem.
        if (I->second->getCaughtType()->isPointerType() ==
                TestAgainstType->isPointerType()) {
          FoundHandler = I->second;
          FoundHandlerType = Check;
          return true;
        }
      }
    }
    return false;
  }
};
}

StmtResult Sema::ActOnCXXTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                                  ArrayRef<Stmt *> Handlers) {
  const llvm::Triple &T = Context.getTargetInfo().getTriple();
  const bool IsOpenMPGPUTarget =
      getLangOpts().OpenMPIsTargetDevice && (T.isNVPTX() || T.isAMDGCN());
  // Don't report an error if 'try' is used in system headers or in an OpenMP
  // target region compiled for a GPU architecture.
  if (!IsOpenMPGPUTarget && !getLangOpts().CXXExceptions &&
      !getSourceManager().isInSystemHeader(TryLoc) && !getLangOpts().CUDA) {
    // Delay error emission for the OpenMP device code.
    targetDiag(TryLoc, diag::err_exceptions_disabled) << "try";
  }

  // In OpenMP target regions, we assume that catch is never reached on GPU
  // targets.
  if (IsOpenMPGPUTarget)
    targetDiag(TryLoc, diag::warn_try_not_valid_on_target) << T.str();

  // Exceptions aren't allowed in CUDA device code.
  if (getLangOpts().CUDA)
    CUDA().DiagIfDeviceCode(TryLoc, diag::err_cuda_device_exceptions)
        << "try" << llvm::to_underlying(CUDA().CurrentTarget());

  if (getCurScope() && getCurScope()->isOpenMPSimdDirectiveScope())
    Diag(TryLoc, diag::err_omp_simd_region_cannot_use_stmt) << "try";

  sema::FunctionScopeInfo *FSI = getCurFunction();

  // C++ try is incompatible with SEH __try.
  if (!getLangOpts().Borland && FSI->FirstSEHTryLoc.isValid()) {
    Diag(TryLoc, diag::err_mixing_cxx_try_seh_try) << 0;
    Diag(FSI->FirstSEHTryLoc, diag::note_conflicting_try_here) << "'__try'";
  }

  const unsigned NumHandlers = Handlers.size();
  assert(!Handlers.empty() &&
         "The parser shouldn't call this if there are no handlers.");

  llvm::DenseMap<QualType, CXXCatchStmt *> HandledBaseTypes;
  llvm::DenseMap<CatchHandlerType, CXXCatchStmt *> HandledTypes;
  for (unsigned i = 0; i < NumHandlers; ++i) {
    CXXCatchStmt *H = cast<CXXCatchStmt>(Handlers[i]);

    // Diagnose when the handler is a catch-all handler, but it isn't the last
    // handler for the try block. [except.handle]p5. Also, skip exception
    // declarations that are invalid, since we can't usefully report on them.
    if (!H->getExceptionDecl()) {
      if (i < NumHandlers - 1)
        return StmtError(Diag(H->getBeginLoc(), diag::err_early_catch_all));
      continue;
    } else if (H->getExceptionDecl()->isInvalidDecl())
      continue;

    // Walk the type hierarchy to diagnose when this type has already been
    // handled (duplication), or cannot be handled (derivation inversion). We
    // ignore top-level cv-qualifiers, per [except.handle]p3
    CatchHandlerType HandlerCHT = H->getCaughtType().getCanonicalType();

    // We can ignore whether the type is a reference or a pointer; we need the
    // underlying declaration type in order to get at the underlying record
    // decl, if there is one.
    QualType Underlying = HandlerCHT.underlying();
    if (auto *RD = Underlying->getAsCXXRecordDecl()) {
      if (!RD->hasDefinition())
        continue;
      // Check that none of the public, unambiguous base classes are in the
      // map ([except.handle]p1). Give the base classes the same pointer
      // qualification as the original type we are basing off of. This allows
      // comparison against the handler type using the same top-level pointer
      // as the original type.
      CXXBasePaths Paths;
      Paths.setOrigin(RD);
      CatchTypePublicBases CTPB(HandledBaseTypes,
                                H->getCaughtType().getCanonicalType());
      if (RD->lookupInBases(CTPB, Paths)) {
        const CXXCatchStmt *Problem = CTPB.getFoundHandler();
        if (!Paths.isAmbiguous(
                CanQualType::CreateUnsafe(CTPB.getFoundHandlerType()))) {
          Diag(H->getExceptionDecl()->getTypeSpecStartLoc(),
               diag::warn_exception_caught_by_earlier_handler)
              << H->getCaughtType();
          Diag(Problem->getExceptionDecl()->getTypeSpecStartLoc(),
                diag::note_previous_exception_handler)
              << Problem->getCaughtType();
        }
      }
      // Strip the qualifiers here because we're going to be comparing this
      // type to the base type specifiers of a class, which are ignored in a
      // base specifier per [class.derived.general]p2.
      HandledBaseTypes[Underlying.getUnqualifiedType()] = H;
    }

    // Add the type the list of ones we have handled; diagnose if we've already
    // handled it.
    auto R = HandledTypes.insert(
        std::make_pair(H->getCaughtType().getCanonicalType(), H));
    if (!R.second) {
      const CXXCatchStmt *Problem = R.first->second;
      Diag(H->getExceptionDecl()->getTypeSpecStartLoc(),
           diag::warn_exception_caught_by_earlier_handler)
          << H->getCaughtType();
      Diag(Problem->getExceptionDecl()->getTypeSpecStartLoc(),
           diag::note_previous_exception_handler)
          << Problem->getCaughtType();
    }
  }

  FSI->setHasCXXTry(TryLoc);

  return CXXTryStmt::Create(Context, TryLoc, cast<CompoundStmt>(TryBlock),
                            Handlers);
}

StmtResult Sema::ActOnSEHTryBlock(bool IsCXXTry, SourceLocation TryLoc,
                                  Stmt *TryBlock, Stmt *Handler) {
  assert(TryBlock && Handler);

  sema::FunctionScopeInfo *FSI = getCurFunction();

  // SEH __try is incompatible with C++ try. Borland appears to support this,
  // however.
  if (!getLangOpts().Borland) {
    if (FSI->FirstCXXOrObjCTryLoc.isValid()) {
      Diag(TryLoc, diag::err_mixing_cxx_try_seh_try) << FSI->FirstTryType;
      Diag(FSI->FirstCXXOrObjCTryLoc, diag::note_conflicting_try_here)
          << (FSI->FirstTryType == sema::FunctionScopeInfo::TryLocIsCXX
                  ? "'try'"
                  : "'@try'");
    }
  }

  FSI->setHasSEHTry(TryLoc);

  // Reject __try in Obj-C methods, blocks, and captured decls, since we don't
  // track if they use SEH.
  DeclContext *DC = CurContext;
  while (DC && !DC->isFunctionOrMethod())
    DC = DC->getParent();
  FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(DC);
  if (FD)
    FD->setUsesSEHTry(true);
  else
    Diag(TryLoc, diag::err_seh_try_outside_functions);

  // Reject __try on unsupported targets.
  if (!Context.getTargetInfo().isSEHTrySupported())
    Diag(TryLoc, diag::err_seh_try_unsupported);

  return SEHTryStmt::Create(Context, IsCXXTry, TryLoc, TryBlock, Handler);
}

StmtResult Sema::ActOnSEHExceptBlock(SourceLocation Loc, Expr *FilterExpr,
                                     Stmt *Block) {
  assert(FilterExpr && Block);
  QualType FTy = FilterExpr->getType();
  if (!FTy->isIntegerType() && !FTy->isDependentType()) {
    return StmtError(
        Diag(FilterExpr->getExprLoc(), diag::err_filter_expression_integral)
        << FTy);
  }
  return SEHExceptStmt::Create(Context, Loc, FilterExpr, Block);
}

void Sema::ActOnStartSEHFinallyBlock() {
  CurrentSEHFinally.push_back(CurScope);
}

void Sema::ActOnAbortSEHFinallyBlock() {
  CurrentSEHFinally.pop_back();
}

StmtResult Sema::ActOnFinishSEHFinallyBlock(SourceLocation Loc, Stmt *Block) {
  assert(Block);
  CurrentSEHFinally.pop_back();
  return SEHFinallyStmt::Create(Context, Loc, Block);
}

StmtResult
Sema::ActOnSEHLeaveStmt(SourceLocation Loc, Scope *CurScope) {
  Scope *SEHTryParent = CurScope;
  while (SEHTryParent && !SEHTryParent->isSEHTryScope())
    SEHTryParent = SEHTryParent->getParent();
  if (!SEHTryParent)
    return StmtError(Diag(Loc, diag::err_ms___leave_not_in___try));
  CheckJumpOutOfSEHFinally(*this, Loc, *SEHTryParent);

  return new (Context) SEHLeaveStmt(Loc);
}

StmtResult Sema::BuildMSDependentExistsStmt(SourceLocation KeywordLoc,
                                            bool IsIfExists,
                                            NestedNameSpecifierLoc QualifierLoc,
                                            DeclarationNameInfo NameInfo,
                                            Stmt *Nested)
{
  return new (Context) MSDependentExistsStmt(KeywordLoc, IsIfExists,
                                             QualifierLoc, NameInfo,
                                             cast<CompoundStmt>(Nested));
}


StmtResult Sema::ActOnMSDependentExistsStmt(SourceLocation KeywordLoc,
                                            bool IsIfExists,
                                            CXXScopeSpec &SS,
                                            UnqualifiedId &Name,
                                            Stmt *Nested) {
  return BuildMSDependentExistsStmt(KeywordLoc, IsIfExists,
                                    SS.getWithLocInContext(Context),
                                    GetNameFromUnqualifiedId(Name),
                                    Nested);
}

RecordDecl*
Sema::CreateCapturedStmtRecordDecl(CapturedDecl *&CD, SourceLocation Loc,
                                   unsigned NumParams) {
  DeclContext *DC = CurContext;
  while (!(DC->isFunctionOrMethod() || DC->isRecord() || DC->isFileContext()))
    DC = DC->getParent();

  RecordDecl *RD = nullptr;
  if (getLangOpts().CPlusPlus)
    RD = CXXRecordDecl::Create(Context, TagTypeKind::Struct, DC, Loc, Loc,
                               /*Id=*/nullptr);
  else
    RD = RecordDecl::Create(Context, TagTypeKind::Struct, DC, Loc, Loc,
                            /*Id=*/nullptr);

  RD->setCapturedRecord();
  DC->addDecl(RD);
  RD->setImplicit();
  RD->startDefinition();

  assert(NumParams > 0 && "CapturedStmt requires context parameter");
  CD = CapturedDecl::Create(Context, CurContext, NumParams);
  DC->addDecl(CD);
  return RD;
}

static bool
buildCapturedStmtCaptureList(Sema &S, CapturedRegionScopeInfo *RSI,
                             SmallVectorImpl<CapturedStmt::Capture> &Captures,
                             SmallVectorImpl<Expr *> &CaptureInits) {
  for (const sema::Capture &Cap : RSI->Captures) {
    if (Cap.isInvalid())
      continue;

    // Form the initializer for the capture.
    ExprResult Init = S.BuildCaptureInit(Cap, Cap.getLocation(),
                                         RSI->CapRegionKind == CR_OpenMP);

    // FIXME: Bail out now if the capture is not used and the initializer has
    // no side-effects.

    // Create a field for this capture.
    FieldDecl *Field = S.BuildCaptureField(RSI->TheRecordDecl, Cap);

    // Add the capture to our list of captures.
    if (Cap.isThisCapture()) {
      Captures.push_back(CapturedStmt::Capture(Cap.getLocation(),
                                               CapturedStmt::VCK_This));
    } else if (Cap.isVLATypeCapture()) {
      Captures.push_back(
          CapturedStmt::Capture(Cap.getLocation(), CapturedStmt::VCK_VLAType));
    } else {
      assert(Cap.isVariableCapture() && "unknown kind of capture");

      if (S.getLangOpts().OpenMP && RSI->CapRegionKind == CR_OpenMP)
        S.OpenMP().setOpenMPCaptureKind(Field, Cap.getVariable(),
                                        RSI->OpenMPLevel);

      Captures.push_back(CapturedStmt::Capture(
          Cap.getLocation(),
          Cap.isReferenceCapture() ? CapturedStmt::VCK_ByRef
                                   : CapturedStmt::VCK_ByCopy,
          cast<VarDecl>(Cap.getVariable())));
    }
    CaptureInits.push_back(Init.get());
  }
  return false;
}

void Sema::ActOnCapturedRegionStart(SourceLocation Loc, Scope *CurScope,
                                    CapturedRegionKind Kind,
                                    unsigned NumParams) {
  CapturedDecl *CD = nullptr;
  RecordDecl *RD = CreateCapturedStmtRecordDecl(CD, Loc, NumParams);

  // Build the context parameter
  DeclContext *DC = CapturedDecl::castToDeclContext(CD);
  IdentifierInfo *ParamName = &Context.Idents.get("__context");
  QualType ParamType = Context.getPointerType(Context.getTagDeclType(RD));
  auto *Param =
      ImplicitParamDecl::Create(Context, DC, Loc, ParamName, ParamType,
                                ImplicitParamKind::CapturedContext);
  DC->addDecl(Param);

  CD->setContextParam(0, Param);

  // Enter the capturing scope for this captured region.
  PushCapturedRegionScope(CurScope, CD, RD, Kind);

  if (CurScope)
    PushDeclContext(CurScope, CD);
  else
    CurContext = CD;

  PushExpressionEvaluationContext(
      ExpressionEvaluationContext::PotentiallyEvaluated);
  ExprEvalContexts.back().InImmediateEscalatingFunctionContext = false;
}

void Sema::ActOnCapturedRegionStart(SourceLocation Loc, Scope *CurScope,
                                    CapturedRegionKind Kind,
                                    ArrayRef<CapturedParamNameType> Params,
                                    unsigned OpenMPCaptureLevel) {
  CapturedDecl *CD = nullptr;
  RecordDecl *RD = CreateCapturedStmtRecordDecl(CD, Loc, Params.size());

  // Build the context parameter
  DeclContext *DC = CapturedDecl::castToDeclContext(CD);
  bool ContextIsFound = false;
  unsigned ParamNum = 0;
  for (ArrayRef<CapturedParamNameType>::iterator I = Params.begin(),
                                                 E = Params.end();
       I != E; ++I, ++ParamNum) {
    if (I->second.isNull()) {
      assert(!ContextIsFound &&
             "null type has been found already for '__context' parameter");
      IdentifierInfo *ParamName = &Context.Idents.get("__context");
      QualType ParamType = Context.getPointerType(Context.getTagDeclType(RD))
                               .withConst()
                               .withRestrict();
      auto *Param =
          ImplicitParamDecl::Create(Context, DC, Loc, ParamName, ParamType,
                                    ImplicitParamKind::CapturedContext);
      DC->addDecl(Param);
      CD->setContextParam(ParamNum, Param);
      ContextIsFound = true;
    } else {
      IdentifierInfo *ParamName = &Context.Idents.get(I->first);
      auto *Param =
          ImplicitParamDecl::Create(Context, DC, Loc, ParamName, I->second,
                                    ImplicitParamKind::CapturedContext);
      DC->addDecl(Param);
      CD->setParam(ParamNum, Param);
    }
  }
  assert(ContextIsFound && "no null type for '__context' parameter");
  if (!ContextIsFound) {
    // Add __context implicitly if it is not specified.
    IdentifierInfo *ParamName = &Context.Idents.get("__context");
    QualType ParamType = Context.getPointerType(Context.getTagDeclType(RD));
    auto *Param =
        ImplicitParamDecl::Create(Context, DC, Loc, ParamName, ParamType,
                                  ImplicitParamKind::CapturedContext);
    DC->addDecl(Param);
    CD->setContextParam(ParamNum, Param);
  }
  // Enter the capturing scope for this captured region.
  PushCapturedRegionScope(CurScope, CD, RD, Kind, OpenMPCaptureLevel);

  if (CurScope)
    PushDeclContext(CurScope, CD);
  else
    CurContext = CD;

  PushExpressionEvaluationContext(
      ExpressionEvaluationContext::PotentiallyEvaluated);
}

void Sema::ActOnCapturedRegionError() {
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();
  PopDeclContext();
  PoppedFunctionScopePtr ScopeRAII = PopFunctionScopeInfo();
  CapturedRegionScopeInfo *RSI = cast<CapturedRegionScopeInfo>(ScopeRAII.get());

  RecordDecl *Record = RSI->TheRecordDecl;
  Record->setInvalidDecl();

  SmallVector<Decl*, 4> Fields(Record->fields());
  ActOnFields(/*Scope=*/nullptr, Record->getLocation(), Record, Fields,
              SourceLocation(), SourceLocation(), ParsedAttributesView());
}

StmtResult Sema::ActOnCapturedRegionEnd(Stmt *S) {
  // Leave the captured scope before we start creating captures in the
  // enclosing scope.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();
  PopDeclContext();
  PoppedFunctionScopePtr ScopeRAII = PopFunctionScopeInfo();
  CapturedRegionScopeInfo *RSI = cast<CapturedRegionScopeInfo>(ScopeRAII.get());

  SmallVector<CapturedStmt::Capture, 4> Captures;
  SmallVector<Expr *, 4> CaptureInits;
  if (buildCapturedStmtCaptureList(*this, RSI, Captures, CaptureInits))
    return StmtError();

  CapturedDecl *CD = RSI->TheCapturedDecl;
  RecordDecl *RD = RSI->TheRecordDecl;

  CapturedStmt *Res = CapturedStmt::Create(
      getASTContext(), S, static_cast<CapturedRegionKind>(RSI->CapRegionKind),
      Captures, CaptureInits, CD, RD);

  CD->setBody(Res->getCapturedStmt());
  RD->completeDefinition();

  return Res;
}
