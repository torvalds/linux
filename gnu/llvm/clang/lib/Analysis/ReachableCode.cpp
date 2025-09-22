//===-- ReachableCode.cpp - Code Reachability Analysis --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a flow-sensitive, path-insensitive analysis of
// determining reachable blocks within a CFG.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

using namespace clang;

//===----------------------------------------------------------------------===//
// Core Reachability Analysis routines.
//===----------------------------------------------------------------------===//

static bool isEnumConstant(const Expr *Ex) {
  const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Ex);
  if (!DR)
    return false;
  return isa<EnumConstantDecl>(DR->getDecl());
}

static bool isTrivialExpression(const Expr *Ex) {
  Ex = Ex->IgnoreParenCasts();
  return isa<IntegerLiteral>(Ex) || isa<StringLiteral>(Ex) ||
         isa<CXXBoolLiteralExpr>(Ex) || isa<ObjCBoolLiteralExpr>(Ex) ||
         isa<CharacterLiteral>(Ex) ||
         isEnumConstant(Ex);
}

static bool isTrivialDoWhile(const CFGBlock *B, const Stmt *S) {
  // Check if the block ends with a do...while() and see if 'S' is the
  // condition.
  if (const Stmt *Term = B->getTerminatorStmt()) {
    if (const DoStmt *DS = dyn_cast<DoStmt>(Term)) {
      const Expr *Cond = DS->getCond()->IgnoreParenCasts();
      return Cond == S && isTrivialExpression(Cond);
    }
  }
  return false;
}

static bool isBuiltinUnreachable(const Stmt *S) {
  if (const auto *DRE = dyn_cast<DeclRefExpr>(S))
    if (const auto *FDecl = dyn_cast<FunctionDecl>(DRE->getDecl()))
      return FDecl->getIdentifier() &&
             FDecl->getBuiltinID() == Builtin::BI__builtin_unreachable;
  return false;
}

static bool isBuiltinAssumeFalse(const CFGBlock *B, const Stmt *S,
                                 ASTContext &C) {
  if (B->empty())  {
    // Happens if S is B's terminator and B contains nothing else
    // (e.g. a CFGBlock containing only a goto).
    return false;
  }
  if (std::optional<CFGStmt> CS = B->back().getAs<CFGStmt>()) {
    if (const auto *CE = dyn_cast<CallExpr>(CS->getStmt())) {
      return CE->getCallee()->IgnoreCasts() == S && CE->isBuiltinAssumeFalse(C);
    }
  }
  return false;
}

static bool isDeadReturn(const CFGBlock *B, const Stmt *S) {
  // Look to see if the current control flow ends with a 'return', and see if
  // 'S' is a substatement. The 'return' may not be the last element in the
  // block, or may be in a subsequent block because of destructors.
  const CFGBlock *Current = B;
  while (true) {
    for (const CFGElement &CE : llvm::reverse(*Current)) {
      if (std::optional<CFGStmt> CS = CE.getAs<CFGStmt>()) {
        if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(CS->getStmt())) {
          if (RS == S)
            return true;
          if (const Expr *RE = RS->getRetValue()) {
            RE = RE->IgnoreParenCasts();
            if (RE == S)
              return true;
            ParentMap PM(const_cast<Expr *>(RE));
            // If 'S' is in the ParentMap, it is a subexpression of
            // the return statement.
            return PM.getParent(S);
          }
        }
        break;
      }
    }
    // Note also that we are restricting the search for the return statement
    // to stop at control-flow; only part of a return statement may be dead,
    // without the whole return statement being dead.
    if (Current->getTerminator().isTemporaryDtorsBranch()) {
      // Temporary destructors have a predictable control flow, thus we want to
      // look into the next block for the return statement.
      // We look into the false branch, as we know the true branch only contains
      // the call to the destructor.
      assert(Current->succ_size() == 2);
      Current = *(Current->succ_begin() + 1);
    } else if (!Current->getTerminatorStmt() && Current->succ_size() == 1) {
      // If there is only one successor, we're not dealing with outgoing control
      // flow. Thus, look into the next block.
      Current = *Current->succ_begin();
      if (Current->pred_size() > 1) {
        // If there is more than one predecessor, we're dealing with incoming
        // control flow - if the return statement is in that block, it might
        // well be reachable via a different control flow, thus it's not dead.
        return false;
      }
    } else {
      // We hit control flow or a dead end. Stop searching.
      return false;
    }
  }
  llvm_unreachable("Broke out of infinite loop.");
}

static SourceLocation getTopMostMacro(SourceLocation Loc, SourceManager &SM) {
  assert(Loc.isMacroID());
  SourceLocation Last;
  do {
    Last = Loc;
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  } while (Loc.isMacroID());
  return Last;
}

/// Returns true if the statement is expanded from a configuration macro.
static bool isExpandedFromConfigurationMacro(const Stmt *S,
                                             Preprocessor &PP,
                                             bool IgnoreYES_NO = false) {
  // FIXME: This is not very precise.  Here we just check to see if the
  // value comes from a macro, but we can do much better.  This is likely
  // to be over conservative.  This logic is factored into a separate function
  // so that we can refine it later.
  SourceLocation L = S->getBeginLoc();
  if (L.isMacroID()) {
    SourceManager &SM = PP.getSourceManager();
    if (IgnoreYES_NO) {
      // The Objective-C constant 'YES' and 'NO'
      // are defined as macros.  Do not treat them
      // as configuration values.
      SourceLocation TopL = getTopMostMacro(L, SM);
      StringRef MacroName = PP.getImmediateMacroName(TopL);
      if (MacroName == "YES" || MacroName == "NO")
        return false;
    } else if (!PP.getLangOpts().CPlusPlus) {
      // Do not treat C 'false' and 'true' macros as configuration values.
      SourceLocation TopL = getTopMostMacro(L, SM);
      StringRef MacroName = PP.getImmediateMacroName(TopL);
      if (MacroName == "false" || MacroName == "true")
        return false;
    }
    return true;
  }
  return false;
}

static bool isConfigurationValue(const ValueDecl *D, Preprocessor &PP);

/// Returns true if the statement represents a configuration value.
///
/// A configuration value is something usually determined at compile-time
/// to conditionally always execute some branch.  Such guards are for
/// "sometimes unreachable" code.  Such code is usually not interesting
/// to report as unreachable, and may mask truly unreachable code within
/// those blocks.
static bool isConfigurationValue(const Stmt *S,
                                 Preprocessor &PP,
                                 SourceRange *SilenceableCondVal = nullptr,
                                 bool IncludeIntegers = true,
                                 bool WrappedInParens = false) {
  if (!S)
    return false;

  if (const auto *Ex = dyn_cast<Expr>(S))
    S = Ex->IgnoreImplicit();

  if (const auto *Ex = dyn_cast<Expr>(S))
    S = Ex->IgnoreCasts();

  // Special case looking for the sigil '()' around an integer literal.
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(S))
    if (!PE->getBeginLoc().isMacroID())
      return isConfigurationValue(PE->getSubExpr(), PP, SilenceableCondVal,
                                  IncludeIntegers, true);

  if (const Expr *Ex = dyn_cast<Expr>(S))
    S = Ex->IgnoreCasts();

  bool IgnoreYES_NO = false;

  switch (S->getStmtClass()) {
    case Stmt::CallExprClass: {
      const FunctionDecl *Callee =
        dyn_cast_or_null<FunctionDecl>(cast<CallExpr>(S)->getCalleeDecl());
      return Callee ? Callee->isConstexpr() : false;
    }
    case Stmt::DeclRefExprClass:
      return isConfigurationValue(cast<DeclRefExpr>(S)->getDecl(), PP);
    case Stmt::ObjCBoolLiteralExprClass:
      IgnoreYES_NO = true;
      [[fallthrough]];
    case Stmt::CXXBoolLiteralExprClass:
    case Stmt::IntegerLiteralClass: {
      const Expr *E = cast<Expr>(S);
      if (IncludeIntegers) {
        if (SilenceableCondVal && !SilenceableCondVal->getBegin().isValid())
          *SilenceableCondVal = E->getSourceRange();
        return WrappedInParens ||
               isExpandedFromConfigurationMacro(E, PP, IgnoreYES_NO);
      }
      return false;
    }
    case Stmt::MemberExprClass:
      return isConfigurationValue(cast<MemberExpr>(S)->getMemberDecl(), PP);
    case Stmt::UnaryExprOrTypeTraitExprClass:
      return true;
    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *B = cast<BinaryOperator>(S);
      // Only include raw integers (not enums) as configuration
      // values if they are used in a logical or comparison operator
      // (not arithmetic).
      IncludeIntegers &= (B->isLogicalOp() || B->isComparisonOp());
      return isConfigurationValue(B->getLHS(), PP, SilenceableCondVal,
                                  IncludeIntegers) ||
             isConfigurationValue(B->getRHS(), PP, SilenceableCondVal,
                                  IncludeIntegers);
    }
    case Stmt::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(S);
      if (UO->getOpcode() != UO_LNot && UO->getOpcode() != UO_Minus)
        return false;
      bool SilenceableCondValNotSet =
          SilenceableCondVal && SilenceableCondVal->getBegin().isInvalid();
      bool IsSubExprConfigValue =
          isConfigurationValue(UO->getSubExpr(), PP, SilenceableCondVal,
                               IncludeIntegers, WrappedInParens);
      // Update the silenceable condition value source range only if the range
      // was set directly by the child expression.
      if (SilenceableCondValNotSet &&
          SilenceableCondVal->getBegin().isValid() &&
          *SilenceableCondVal ==
              UO->getSubExpr()->IgnoreCasts()->getSourceRange())
        *SilenceableCondVal = UO->getSourceRange();
      return IsSubExprConfigValue;
    }
    default:
      return false;
  }
}

static bool isConfigurationValue(const ValueDecl *D, Preprocessor &PP) {
  if (const EnumConstantDecl *ED = dyn_cast<EnumConstantDecl>(D))
    return isConfigurationValue(ED->getInitExpr(), PP);
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    // As a heuristic, treat globals as configuration values.  Note
    // that we only will get here if Sema evaluated this
    // condition to a constant expression, which means the global
    // had to be declared in a way to be a truly constant value.
    // We could generalize this to local variables, but it isn't
    // clear if those truly represent configuration values that
    // gate unreachable code.
    if (!VD->hasLocalStorage())
      return true;

    // As a heuristic, locals that have been marked 'const' explicitly
    // can be treated as configuration values as well.
    return VD->getType().isLocalConstQualified();
  }
  return false;
}

/// Returns true if we should always explore all successors of a block.
static bool shouldTreatSuccessorsAsReachable(const CFGBlock *B,
                                             Preprocessor &PP) {
  if (const Stmt *Term = B->getTerminatorStmt()) {
    if (isa<SwitchStmt>(Term))
      return true;
    // Specially handle '||' and '&&'.
    if (isa<BinaryOperator>(Term)) {
      return isConfigurationValue(Term, PP);
    }
    // Do not treat constexpr if statement successors as unreachable in warnings
    // since the point of these statements is to determine branches at compile
    // time.
    if (const auto *IS = dyn_cast<IfStmt>(Term);
        IS != nullptr && IS->isConstexpr())
      return true;
  }

  const Stmt *Cond = B->getTerminatorCondition(/* stripParens */ false);
  return isConfigurationValue(Cond, PP);
}

static unsigned scanFromBlock(const CFGBlock *Start,
                              llvm::BitVector &Reachable,
                              Preprocessor *PP,
                              bool IncludeSometimesUnreachableEdges) {
  unsigned count = 0;

  // Prep work queue
  SmallVector<const CFGBlock*, 32> WL;

  // The entry block may have already been marked reachable
  // by the caller.
  if (!Reachable[Start->getBlockID()]) {
    ++count;
    Reachable[Start->getBlockID()] = true;
  }

  WL.push_back(Start);

  // Find the reachable blocks from 'Start'.
  while (!WL.empty()) {
    const CFGBlock *item = WL.pop_back_val();

    // There are cases where we want to treat all successors as reachable.
    // The idea is that some "sometimes unreachable" code is not interesting,
    // and that we should forge ahead and explore those branches anyway.
    // This allows us to potentially uncover some "always unreachable" code
    // within the "sometimes unreachable" code.
    // Look at the successors and mark then reachable.
    std::optional<bool> TreatAllSuccessorsAsReachable;
    if (!IncludeSometimesUnreachableEdges)
      TreatAllSuccessorsAsReachable = false;

    for (CFGBlock::const_succ_iterator I = item->succ_begin(),
         E = item->succ_end(); I != E; ++I) {
      const CFGBlock *B = *I;
      if (!B) do {
        const CFGBlock *UB = I->getPossiblyUnreachableBlock();
        if (!UB)
          break;

        if (!TreatAllSuccessorsAsReachable) {
          assert(PP);
          TreatAllSuccessorsAsReachable =
            shouldTreatSuccessorsAsReachable(item, *PP);
        }

        if (*TreatAllSuccessorsAsReachable) {
          B = UB;
          break;
        }
      }
      while (false);

      if (B) {
        unsigned blockID = B->getBlockID();
        if (!Reachable[blockID]) {
          Reachable.set(blockID);
          WL.push_back(B);
          ++count;
        }
      }
    }
  }
  return count;
}

static unsigned scanMaybeReachableFromBlock(const CFGBlock *Start,
                                            Preprocessor &PP,
                                            llvm::BitVector &Reachable) {
  return scanFromBlock(Start, Reachable, &PP, true);
}

//===----------------------------------------------------------------------===//
// Dead Code Scanner.
//===----------------------------------------------------------------------===//

namespace {
  class DeadCodeScan {
    llvm::BitVector Visited;
    llvm::BitVector &Reachable;
    SmallVector<const CFGBlock *, 10> WorkList;
    Preprocessor &PP;
    ASTContext &C;

    typedef SmallVector<std::pair<const CFGBlock *, const Stmt *>, 12>
    DeferredLocsTy;

    DeferredLocsTy DeferredLocs;

  public:
    DeadCodeScan(llvm::BitVector &reachable, Preprocessor &PP, ASTContext &C)
    : Visited(reachable.size()),
      Reachable(reachable),
      PP(PP), C(C) {}

    void enqueue(const CFGBlock *block);
    unsigned scanBackwards(const CFGBlock *Start,
    clang::reachable_code::Callback &CB);

    bool isDeadCodeRoot(const CFGBlock *Block);

    const Stmt *findDeadCode(const CFGBlock *Block);

    void reportDeadCode(const CFGBlock *B,
                        const Stmt *S,
                        clang::reachable_code::Callback &CB);
  };
}

void DeadCodeScan::enqueue(const CFGBlock *block) {
  unsigned blockID = block->getBlockID();
  if (Reachable[blockID] || Visited[blockID])
    return;
  Visited[blockID] = true;
  WorkList.push_back(block);
}

bool DeadCodeScan::isDeadCodeRoot(const clang::CFGBlock *Block) {
  bool isDeadRoot = true;

  for (CFGBlock::const_pred_iterator I = Block->pred_begin(),
       E = Block->pred_end(); I != E; ++I) {
    if (const CFGBlock *PredBlock = *I) {
      unsigned blockID = PredBlock->getBlockID();
      if (Visited[blockID]) {
        isDeadRoot = false;
        continue;
      }
      if (!Reachable[blockID]) {
        isDeadRoot = false;
        Visited[blockID] = true;
        WorkList.push_back(PredBlock);
        continue;
      }
    }
  }

  return isDeadRoot;
}

// Check if the given `DeadStmt` is a coroutine statement and is a substmt of
// the coroutine statement. `Block` is the CFGBlock containing the `DeadStmt`.
static bool isInCoroutineStmt(const Stmt *DeadStmt, const CFGBlock *Block) {
  // The coroutine statement, co_return, co_await, or co_yield.
  const Stmt *CoroStmt = nullptr;
  // Find the first coroutine statement after the DeadStmt in the block.
  bool AfterDeadStmt = false;
  for (CFGBlock::const_iterator I = Block->begin(), E = Block->end(); I != E;
       ++I)
    if (std::optional<CFGStmt> CS = I->getAs<CFGStmt>()) {
      const Stmt *S = CS->getStmt();
      if (S == DeadStmt)
        AfterDeadStmt = true;
      if (AfterDeadStmt &&
          // For simplicity, we only check simple coroutine statements.
          (llvm::isa<CoreturnStmt>(S) || llvm::isa<CoroutineSuspendExpr>(S))) {
        CoroStmt = S;
        break;
      }
    }
  if (!CoroStmt)
    return false;
  struct Checker : RecursiveASTVisitor<Checker> {
    const Stmt *DeadStmt;
    bool CoroutineSubStmt = false;
    Checker(const Stmt *S) : DeadStmt(S) {}
    bool VisitStmt(const Stmt *S) {
      if (S == DeadStmt)
        CoroutineSubStmt = true;
      return true;
    }
    // Statements captured in the CFG can be implicit.
    bool shouldVisitImplicitCode() const { return true; }
  };
  Checker checker(DeadStmt);
  checker.TraverseStmt(const_cast<Stmt *>(CoroStmt));
  return checker.CoroutineSubStmt;
}

static bool isValidDeadStmt(const Stmt *S, const clang::CFGBlock *Block) {
  if (S->getBeginLoc().isInvalid())
    return false;
  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(S))
    return BO->getOpcode() != BO_Comma;
  // Coroutine statements are never considered dead statements, because removing
  // them may change the function semantic if it is the only coroutine statement
  // of the coroutine.
  return !isInCoroutineStmt(S, Block);
}

const Stmt *DeadCodeScan::findDeadCode(const clang::CFGBlock *Block) {
  for (CFGBlock::const_iterator I = Block->begin(), E = Block->end(); I!=E; ++I)
    if (std::optional<CFGStmt> CS = I->getAs<CFGStmt>()) {
      const Stmt *S = CS->getStmt();
      if (isValidDeadStmt(S, Block))
        return S;
    }

  CFGTerminator T = Block->getTerminator();
  if (T.isStmtBranch()) {
    const Stmt *S = T.getStmt();
    if (S && isValidDeadStmt(S, Block))
      return S;
  }

  return nullptr;
}

static int SrcCmp(const std::pair<const CFGBlock *, const Stmt *> *p1,
                  const std::pair<const CFGBlock *, const Stmt *> *p2) {
  if (p1->second->getBeginLoc() < p2->second->getBeginLoc())
    return -1;
  if (p2->second->getBeginLoc() < p1->second->getBeginLoc())
    return 1;
  return 0;
}

unsigned DeadCodeScan::scanBackwards(const clang::CFGBlock *Start,
                                     clang::reachable_code::Callback &CB) {

  unsigned count = 0;
  enqueue(Start);

  while (!WorkList.empty()) {
    const CFGBlock *Block = WorkList.pop_back_val();

    // It is possible that this block has been marked reachable after
    // it was enqueued.
    if (Reachable[Block->getBlockID()])
      continue;

    // Look for any dead code within the block.
    const Stmt *S = findDeadCode(Block);

    if (!S) {
      // No dead code.  Possibly an empty block.  Look at dead predecessors.
      for (CFGBlock::const_pred_iterator I = Block->pred_begin(),
           E = Block->pred_end(); I != E; ++I) {
        if (const CFGBlock *predBlock = *I)
          enqueue(predBlock);
      }
      continue;
    }

    // Specially handle macro-expanded code.
    if (S->getBeginLoc().isMacroID()) {
      count += scanMaybeReachableFromBlock(Block, PP, Reachable);
      continue;
    }

    if (isDeadCodeRoot(Block)) {
      reportDeadCode(Block, S, CB);
      count += scanMaybeReachableFromBlock(Block, PP, Reachable);
    }
    else {
      // Record this statement as the possibly best location in a
      // strongly-connected component of dead code for emitting a
      // warning.
      DeferredLocs.push_back(std::make_pair(Block, S));
    }
  }

  // If we didn't find a dead root, then report the dead code with the
  // earliest location.
  if (!DeferredLocs.empty()) {
    llvm::array_pod_sort(DeferredLocs.begin(), DeferredLocs.end(), SrcCmp);
    for (const auto &I : DeferredLocs) {
      const CFGBlock *Block = I.first;
      if (Reachable[Block->getBlockID()])
        continue;
      reportDeadCode(Block, I.second, CB);
      count += scanMaybeReachableFromBlock(Block, PP, Reachable);
    }
  }

  return count;
}

static SourceLocation GetUnreachableLoc(const Stmt *S,
                                        SourceRange &R1,
                                        SourceRange &R2) {
  R1 = R2 = SourceRange();

  if (const Expr *Ex = dyn_cast<Expr>(S))
    S = Ex->IgnoreParenImpCasts();

  switch (S->getStmtClass()) {
    case Expr::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(S);
      return BO->getOperatorLoc();
    }
    case Expr::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(S);
      R1 = UO->getSubExpr()->getSourceRange();
      return UO->getOperatorLoc();
    }
    case Expr::CompoundAssignOperatorClass: {
      const CompoundAssignOperator *CAO = cast<CompoundAssignOperator>(S);
      R1 = CAO->getLHS()->getSourceRange();
      R2 = CAO->getRHS()->getSourceRange();
      return CAO->getOperatorLoc();
    }
    case Expr::BinaryConditionalOperatorClass:
    case Expr::ConditionalOperatorClass: {
      const AbstractConditionalOperator *CO =
      cast<AbstractConditionalOperator>(S);
      return CO->getQuestionLoc();
    }
    case Expr::MemberExprClass: {
      const MemberExpr *ME = cast<MemberExpr>(S);
      R1 = ME->getSourceRange();
      return ME->getMemberLoc();
    }
    case Expr::ArraySubscriptExprClass: {
      const ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(S);
      R1 = ASE->getLHS()->getSourceRange();
      R2 = ASE->getRHS()->getSourceRange();
      return ASE->getRBracketLoc();
    }
    case Expr::CStyleCastExprClass: {
      const CStyleCastExpr *CSC = cast<CStyleCastExpr>(S);
      R1 = CSC->getSubExpr()->getSourceRange();
      return CSC->getLParenLoc();
    }
    case Expr::CXXFunctionalCastExprClass: {
      const CXXFunctionalCastExpr *CE = cast <CXXFunctionalCastExpr>(S);
      R1 = CE->getSubExpr()->getSourceRange();
      return CE->getBeginLoc();
    }
    case Stmt::CXXTryStmtClass: {
      return cast<CXXTryStmt>(S)->getHandler(0)->getCatchLoc();
    }
    case Expr::ObjCBridgedCastExprClass: {
      const ObjCBridgedCastExpr *CSC = cast<ObjCBridgedCastExpr>(S);
      R1 = CSC->getSubExpr()->getSourceRange();
      return CSC->getLParenLoc();
    }
    default: ;
  }
  R1 = S->getSourceRange();
  return S->getBeginLoc();
}

void DeadCodeScan::reportDeadCode(const CFGBlock *B,
                                  const Stmt *S,
                                  clang::reachable_code::Callback &CB) {
  // Classify the unreachable code found, or suppress it in some cases.
  reachable_code::UnreachableKind UK = reachable_code::UK_Other;

  if (isa<BreakStmt>(S)) {
    UK = reachable_code::UK_Break;
  } else if (isTrivialDoWhile(B, S) || isBuiltinUnreachable(S) ||
             isBuiltinAssumeFalse(B, S, C)) {
    return;
  }
  else if (isDeadReturn(B, S)) {
    UK = reachable_code::UK_Return;
  }

  const auto *AS = dyn_cast<AttributedStmt>(S);
  bool HasFallThroughAttr =
      AS && hasSpecificAttr<FallThroughAttr>(AS->getAttrs());

  SourceRange SilenceableCondVal;

  if (UK == reachable_code::UK_Other) {
    // Check if the dead code is part of the "loop target" of
    // a for/for-range loop.  This is the block that contains
    // the increment code.
    if (const Stmt *LoopTarget = B->getLoopTarget()) {
      SourceLocation Loc = LoopTarget->getBeginLoc();
      SourceRange R1(Loc, Loc), R2;

      if (const ForStmt *FS = dyn_cast<ForStmt>(LoopTarget)) {
        const Expr *Inc = FS->getInc();
        Loc = Inc->getBeginLoc();
        R2 = Inc->getSourceRange();
      }

      CB.HandleUnreachable(reachable_code::UK_Loop_Increment, Loc,
                           SourceRange(), SourceRange(Loc, Loc), R2,
                           HasFallThroughAttr);
      return;
    }

    // Check if the dead block has a predecessor whose branch has
    // a configuration value that *could* be modified to
    // silence the warning.
    CFGBlock::const_pred_iterator PI = B->pred_begin();
    if (PI != B->pred_end()) {
      if (const CFGBlock *PredBlock = PI->getPossiblyUnreachableBlock()) {
        const Stmt *TermCond =
            PredBlock->getTerminatorCondition(/* strip parens */ false);
        isConfigurationValue(TermCond, PP, &SilenceableCondVal);
      }
    }
  }

  SourceRange R1, R2;
  SourceLocation Loc = GetUnreachableLoc(S, R1, R2);
  CB.HandleUnreachable(UK, Loc, SilenceableCondVal, R1, R2, HasFallThroughAttr);
}

//===----------------------------------------------------------------------===//
// Reachability APIs.
//===----------------------------------------------------------------------===//

namespace clang { namespace reachable_code {

void Callback::anchor() { }

unsigned ScanReachableFromBlock(const CFGBlock *Start,
                                llvm::BitVector &Reachable) {
  return scanFromBlock(Start, Reachable, /* SourceManager* */ nullptr, false);
}

void FindUnreachableCode(AnalysisDeclContext &AC, Preprocessor &PP,
                         Callback &CB) {

  CFG *cfg = AC.getCFG();
  if (!cfg)
    return;

  // Scan for reachable blocks from the entrance of the CFG.
  // If there are no unreachable blocks, we're done.
  llvm::BitVector reachable(cfg->getNumBlockIDs());
  unsigned numReachable =
    scanMaybeReachableFromBlock(&cfg->getEntry(), PP, reachable);
  if (numReachable == cfg->getNumBlockIDs())
    return;

  // If there aren't explicit EH edges, we should include the 'try' dispatch
  // blocks as roots.
  if (!AC.getCFGBuildOptions().AddEHEdges) {
    for (const CFGBlock *B : cfg->try_blocks())
      numReachable += scanMaybeReachableFromBlock(B, PP, reachable);
    if (numReachable == cfg->getNumBlockIDs())
      return;
  }

  // There are some unreachable blocks.  We need to find the root blocks that
  // contain code that should be considered unreachable.
  for (const CFGBlock *block : *cfg) {
    // A block may have been marked reachable during this loop.
    if (reachable[block->getBlockID()])
      continue;

    DeadCodeScan DS(reachable, PP, AC.getASTContext());
    numReachable += DS.scanBackwards(block, CB);

    if (numReachable == cfg->getNumBlockIDs())
      return;
  }
}

}} // end namespace clang::reachable_code
