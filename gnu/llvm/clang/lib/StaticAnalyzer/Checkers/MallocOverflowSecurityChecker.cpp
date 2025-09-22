// MallocOverflowSecurityChecker.cpp - Check for malloc overflows -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker detects a common memory allocation security flaw.
// Suppose 'unsigned int n' comes from an untrusted source. If the
// code looks like 'malloc (n * 4)', and an attacker can make 'n' be
// say MAX_UINT/4+2, then instead of allocating the correct 'n' 4-byte
// elements, this will actually allocate only two because of overflow.
// Then when the rest of the program attempts to store values past the
// second element, these values will actually overwrite other items in
// the heap, probably allowing the attacker to execute arbitrary code.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <utility>

using namespace clang;
using namespace ento;
using llvm::APSInt;

namespace {
struct MallocOverflowCheck {
  const CallExpr *call;
  const BinaryOperator *mulop;
  const Expr *variable;
  APSInt maxVal;

  MallocOverflowCheck(const CallExpr *call, const BinaryOperator *m,
                      const Expr *v, APSInt val)
      : call(call), mulop(m), variable(v), maxVal(std::move(val)) {}
};

class MallocOverflowSecurityChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &mgr,
                        BugReporter &BR) const;

  void CheckMallocArgument(
      SmallVectorImpl<MallocOverflowCheck> &PossibleMallocOverflows,
      const CallExpr *TheCall, ASTContext &Context) const;

  void OutputPossibleOverflows(
    SmallVectorImpl<MallocOverflowCheck> &PossibleMallocOverflows,
    const Decl *D, BugReporter &BR, AnalysisManager &mgr) const;

};
} // end anonymous namespace

// Return true for computations which evaluate to zero: e.g., mult by 0.
static inline bool EvaluatesToZero(APSInt &Val, BinaryOperatorKind op) {
  return (op == BO_Mul) && (Val == 0);
}

void MallocOverflowSecurityChecker::CheckMallocArgument(
    SmallVectorImpl<MallocOverflowCheck> &PossibleMallocOverflows,
    const CallExpr *TheCall, ASTContext &Context) const {

  /* Look for a linear combination with a single variable, and at least
   one multiplication.
   Reject anything that applies to the variable: an explicit cast,
   conditional expression, an operation that could reduce the range
   of the result, or anything too complicated :-).  */
  const Expr *e = TheCall->getArg(0);
  const BinaryOperator * mulop = nullptr;
  APSInt maxVal;

  for (;;) {
    maxVal = 0;
    e = e->IgnoreParenImpCasts();
    if (const BinaryOperator *binop = dyn_cast<BinaryOperator>(e)) {
      BinaryOperatorKind opc = binop->getOpcode();
      // TODO: ignore multiplications by 1, reject if multiplied by 0.
      if (mulop == nullptr && opc == BO_Mul)
        mulop = binop;
      if (opc != BO_Mul && opc != BO_Add && opc != BO_Sub && opc != BO_Shl)
        return;

      const Expr *lhs = binop->getLHS();
      const Expr *rhs = binop->getRHS();
      if (rhs->isEvaluatable(Context)) {
        e = lhs;
        maxVal = rhs->EvaluateKnownConstInt(Context);
        if (EvaluatesToZero(maxVal, opc))
          return;
      } else if ((opc == BO_Add || opc == BO_Mul) &&
                 lhs->isEvaluatable(Context)) {
        maxVal = lhs->EvaluateKnownConstInt(Context);
        if (EvaluatesToZero(maxVal, opc))
          return;
        e = rhs;
      } else
        return;
    } else if (isa<DeclRefExpr, MemberExpr>(e))
      break;
    else
      return;
  }

  if (mulop == nullptr)
    return;

  //  We've found the right structure of malloc argument, now save
  // the data so when the body of the function is completely available
  // we can check for comparisons.

  PossibleMallocOverflows.push_back(
      MallocOverflowCheck(TheCall, mulop, e, maxVal));
}

namespace {
// A worker class for OutputPossibleOverflows.
class CheckOverflowOps :
  public EvaluatedExprVisitor<CheckOverflowOps> {
public:
  typedef SmallVectorImpl<MallocOverflowCheck> theVecType;

private:
    theVecType &toScanFor;
    ASTContext &Context;

    bool isIntZeroExpr(const Expr *E) const {
      if (!E->getType()->isIntegralOrEnumerationType())
        return false;
      Expr::EvalResult Result;
      if (E->EvaluateAsInt(Result, Context))
        return Result.Val.getInt() == 0;
      return false;
    }

    static const Decl *getDecl(const DeclRefExpr *DR) { return DR->getDecl(); }
    static const Decl *getDecl(const MemberExpr *ME) {
      return ME->getMemberDecl();
    }

    template <typename T1>
    void Erase(const T1 *DR,
               llvm::function_ref<bool(const MallocOverflowCheck &)> Pred) {
      auto P = [DR, Pred](const MallocOverflowCheck &Check) {
        if (const auto *CheckDR = dyn_cast<T1>(Check.variable))
          return getDecl(CheckDR) == getDecl(DR) && Pred(Check);
        return false;
      };
      llvm::erase_if(toScanFor, P);
    }

    void CheckExpr(const Expr *E_p) {
      const Expr *E = E_p->IgnoreParenImpCasts();
      const auto PrecedesMalloc = [E, this](const MallocOverflowCheck &c) {
        return Context.getSourceManager().isBeforeInTranslationUnit(
            E->getExprLoc(), c.call->getExprLoc());
      };
      if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E))
        Erase<DeclRefExpr>(DR, PrecedesMalloc);
      else if (const auto *ME = dyn_cast<MemberExpr>(E)) {
        Erase<MemberExpr>(ME, PrecedesMalloc);
      }
    }

    // Check if the argument to malloc is assigned a value
    // which cannot cause an overflow.
    // e.g., malloc (mul * x) and,
    // case 1: mul = <constant value>
    // case 2: mul = a/b, where b > x
    void CheckAssignmentExpr(BinaryOperator *AssignEx) {
      bool assignKnown = false;
      bool numeratorKnown = false, denomKnown = false;
      APSInt denomVal;
      denomVal = 0;

      // Erase if the multiplicand was assigned a constant value.
      const Expr *rhs = AssignEx->getRHS();
      if (rhs->isEvaluatable(Context))
        assignKnown = true;

      // Discard the report if the multiplicand was assigned a value,
      // that can never overflow after multiplication. e.g., the assignment
      // is a division operator and the denominator is > other multiplicand.
      const Expr *rhse = rhs->IgnoreParenImpCasts();
      if (const BinaryOperator *BOp = dyn_cast<BinaryOperator>(rhse)) {
        if (BOp->getOpcode() == BO_Div) {
          const Expr *denom = BOp->getRHS()->IgnoreParenImpCasts();
          Expr::EvalResult Result;
          if (denom->EvaluateAsInt(Result, Context)) {
            denomVal = Result.Val.getInt();
            denomKnown = true;
          }
          const Expr *numerator = BOp->getLHS()->IgnoreParenImpCasts();
          if (numerator->isEvaluatable(Context))
            numeratorKnown = true;
        }
      }
      if (!assignKnown && !denomKnown)
        return;
      auto denomExtVal = denomVal.getExtValue();

      // Ignore negative denominator.
      if (denomExtVal < 0)
        return;

      const Expr *lhs = AssignEx->getLHS();
      const Expr *E = lhs->IgnoreParenImpCasts();

      auto pred = [assignKnown, numeratorKnown,
                   denomExtVal](const MallocOverflowCheck &Check) {
        return assignKnown ||
               (numeratorKnown && (denomExtVal >= Check.maxVal.getExtValue()));
      };

      if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E))
        Erase<DeclRefExpr>(DR, pred);
      else if (const auto *ME = dyn_cast<MemberExpr>(E))
        Erase<MemberExpr>(ME, pred);
    }

  public:
    void VisitBinaryOperator(BinaryOperator *E) {
      if (E->isComparisonOp()) {
        const Expr * lhs = E->getLHS();
        const Expr * rhs = E->getRHS();
        // Ignore comparisons against zero, since they generally don't
        // protect against an overflow.
        if (!isIntZeroExpr(lhs) && !isIntZeroExpr(rhs)) {
          CheckExpr(lhs);
          CheckExpr(rhs);
        }
      }
      if (E->isAssignmentOp())
        CheckAssignmentExpr(E);
      EvaluatedExprVisitor<CheckOverflowOps>::VisitBinaryOperator(E);
    }

    /* We specifically ignore loop conditions, because they're typically
     not error checks.  */
    void VisitWhileStmt(WhileStmt *S) {
      return this->Visit(S->getBody());
    }
    void VisitForStmt(ForStmt *S) {
      return this->Visit(S->getBody());
    }
    void VisitDoStmt(DoStmt *S) {
      return this->Visit(S->getBody());
    }

    CheckOverflowOps(theVecType &v, ASTContext &ctx)
    : EvaluatedExprVisitor<CheckOverflowOps>(ctx),
      toScanFor(v), Context(ctx)
    { }
  };
}

// OutputPossibleOverflows - We've found a possible overflow earlier,
// now check whether Body might contain a comparison which might be
// preventing the overflow.
// This doesn't do flow analysis, range analysis, or points-to analysis; it's
// just a dumb "is there a comparison" scan.  The aim here is to
// detect the most blatent cases of overflow and educate the
// programmer.
void MallocOverflowSecurityChecker::OutputPossibleOverflows(
  SmallVectorImpl<MallocOverflowCheck> &PossibleMallocOverflows,
  const Decl *D, BugReporter &BR, AnalysisManager &mgr) const {
  // By far the most common case: nothing to check.
  if (PossibleMallocOverflows.empty())
    return;

  // Delete any possible overflows which have a comparison.
  CheckOverflowOps c(PossibleMallocOverflows, BR.getContext());
  c.Visit(mgr.getAnalysisDeclContext(D)->getBody());

  // Output warnings for all overflows that are left.
  for (const MallocOverflowCheck &Check : PossibleMallocOverflows) {
    BR.EmitBasicReport(
        D, this, "malloc() size overflow", categories::UnixAPI,
        "the computation of the size of the memory allocation may overflow",
        PathDiagnosticLocation::createOperatorLoc(Check.mulop,
                                                  BR.getSourceManager()),
        Check.mulop->getSourceRange());
  }
}

void MallocOverflowSecurityChecker::checkASTCodeBody(const Decl *D,
                                             AnalysisManager &mgr,
                                             BugReporter &BR) const {

  CFG *cfg = mgr.getCFG(D);
  if (!cfg)
    return;

  // A list of variables referenced in possibly overflowing malloc operands.
  SmallVector<MallocOverflowCheck, 2> PossibleMallocOverflows;

  for (CFG::iterator it = cfg->begin(), ei = cfg->end(); it != ei; ++it) {
    CFGBlock *block = *it;
    for (CFGBlock::iterator bi = block->begin(), be = block->end();
         bi != be; ++bi) {
        if (std::optional<CFGStmt> CS = bi->getAs<CFGStmt>()) {
          if (const CallExpr *TheCall = dyn_cast<CallExpr>(CS->getStmt())) {
            // Get the callee.
            const FunctionDecl *FD = TheCall->getDirectCallee();

            if (!FD)
              continue;

            // Get the name of the callee. If it's a builtin, strip off the
            // prefix.
            IdentifierInfo *FnInfo = FD->getIdentifier();
            if (!FnInfo)
              continue;

            if (FnInfo->isStr("malloc") || FnInfo->isStr("_MALLOC")) {
              if (TheCall->getNumArgs() == 1)
                CheckMallocArgument(PossibleMallocOverflows, TheCall,
                                    mgr.getASTContext());
            }
          }
        }
    }
  }

  OutputPossibleOverflows(PossibleMallocOverflows, D, BR, mgr);
}

void ento::registerMallocOverflowSecurityChecker(CheckerManager &mgr) {
  mgr.registerChecker<MallocOverflowSecurityChecker>();
}

bool ento::shouldRegisterMallocOverflowSecurityChecker(const CheckerManager &mgr) {
  return true;
}
