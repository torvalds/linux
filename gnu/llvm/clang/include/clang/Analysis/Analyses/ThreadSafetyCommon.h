//===- ThreadSafetyCommon.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parts of thread safety analysis that are not specific to thread safety
// itself have been factored into classes here, where they can be potentially
// used by other analyses.  Currently these include:
//
// * Generalize clang CFG visitors.
// * Conversion of the clang CFG to SSA form.
// * Translation of clang Exprs to TIL SExprs
//
// UNDER CONSTRUCTION.  USE AT YOUR OWN RISK.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYCOMMON_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYCOMMON_H

#include "clang/AST/Decl.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyUtil.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class AbstractConditionalOperator;
class ArraySubscriptExpr;
class BinaryOperator;
class CallExpr;
class CastExpr;
class CXXDestructorDecl;
class CXXMemberCallExpr;
class CXXOperatorCallExpr;
class CXXThisExpr;
class DeclRefExpr;
class DeclStmt;
class Expr;
class MemberExpr;
class Stmt;
class UnaryOperator;

namespace threadSafety {

// Various helper functions on til::SExpr
namespace sx {

inline bool equals(const til::SExpr *E1, const til::SExpr *E2) {
  return til::EqualsComparator::compareExprs(E1, E2);
}

inline bool matches(const til::SExpr *E1, const til::SExpr *E2) {
  // We treat a top-level wildcard as the "univsersal" lock.
  // It matches everything for the purpose of checking locks, but not
  // for unlocking them.
  if (isa<til::Wildcard>(E1))
    return isa<til::Wildcard>(E2);
  if (isa<til::Wildcard>(E2))
    return isa<til::Wildcard>(E1);

  return til::MatchComparator::compareExprs(E1, E2);
}

inline bool partiallyMatches(const til::SExpr *E1, const til::SExpr *E2) {
  const auto *PE1 = dyn_cast_or_null<til::Project>(E1);
  if (!PE1)
    return false;
  const auto *PE2 = dyn_cast_or_null<til::Project>(E2);
  if (!PE2)
    return false;
  return PE1->clangDecl() == PE2->clangDecl();
}

inline std::string toString(const til::SExpr *E) {
  std::stringstream ss;
  til::StdPrinter::print(E, ss);
  return ss.str();
}

}  // namespace sx

// This class defines the interface of a clang CFG Visitor.
// CFGWalker will invoke the following methods.
// Note that methods are not virtual; the visitor is templatized.
class CFGVisitor {
  // Enter the CFG for Decl D, and perform any initial setup operations.
  void enterCFG(CFG *Cfg, const NamedDecl *D, const CFGBlock *First) {}

  // Enter a CFGBlock.
  void enterCFGBlock(const CFGBlock *B) {}

  // Returns true if this visitor implements handlePredecessor
  bool visitPredecessors() { return true; }

  // Process a predecessor edge.
  void handlePredecessor(const CFGBlock *Pred) {}

  // Process a successor back edge to a previously visited block.
  void handlePredecessorBackEdge(const CFGBlock *Pred) {}

  // Called just before processing statements.
  void enterCFGBlockBody(const CFGBlock *B) {}

  // Process an ordinary statement.
  void handleStatement(const Stmt *S) {}

  // Process a destructor call
  void handleDestructorCall(const VarDecl *VD, const CXXDestructorDecl *DD) {}

  // Called after all statements have been handled.
  void exitCFGBlockBody(const CFGBlock *B) {}

  // Return true
  bool visitSuccessors() { return true; }

  // Process a successor edge.
  void handleSuccessor(const CFGBlock *Succ) {}

  // Process a successor back edge to a previously visited block.
  void handleSuccessorBackEdge(const CFGBlock *Succ) {}

  // Leave a CFGBlock.
  void exitCFGBlock(const CFGBlock *B) {}

  // Leave the CFG, and perform any final cleanup operations.
  void exitCFG(const CFGBlock *Last) {}
};

// Walks the clang CFG, and invokes methods on a given CFGVisitor.
class CFGWalker {
public:
  CFGWalker() = default;

  // Initialize the CFGWalker.  This setup only needs to be done once, even
  // if there are multiple passes over the CFG.
  bool init(AnalysisDeclContext &AC) {
    ACtx = &AC;
    CFGraph = AC.getCFG();
    if (!CFGraph)
      return false;

    // Ignore anonymous functions.
    if (!isa_and_nonnull<NamedDecl>(AC.getDecl()))
      return false;

    SortedGraph = AC.getAnalysis<PostOrderCFGView>();
    if (!SortedGraph)
      return false;

    return true;
  }

  // Traverse the CFG, calling methods on V as appropriate.
  template <class Visitor>
  void walk(Visitor &V) {
    PostOrderCFGView::CFGBlockSet VisitedBlocks(CFGraph);

    V.enterCFG(CFGraph, getDecl(), &CFGraph->getEntry());

    for (const auto *CurrBlock : *SortedGraph) {
      VisitedBlocks.insert(CurrBlock);

      V.enterCFGBlock(CurrBlock);

      // Process predecessors, handling back edges last
      if (V.visitPredecessors()) {
        SmallVector<CFGBlock*, 4> BackEdges;
        // Process successors
        for (CFGBlock::const_pred_iterator SI = CurrBlock->pred_begin(),
                                           SE = CurrBlock->pred_end();
             SI != SE; ++SI) {
          if (*SI == nullptr)
            continue;

          if (!VisitedBlocks.alreadySet(*SI)) {
            BackEdges.push_back(*SI);
            continue;
          }
          V.handlePredecessor(*SI);
        }

        for (auto *Blk : BackEdges)
          V.handlePredecessorBackEdge(Blk);
      }

      V.enterCFGBlockBody(CurrBlock);

      // Process statements
      for (const auto &BI : *CurrBlock) {
        switch (BI.getKind()) {
        case CFGElement::Statement:
          V.handleStatement(BI.castAs<CFGStmt>().getStmt());
          break;

        case CFGElement::AutomaticObjectDtor: {
          CFGAutomaticObjDtor AD = BI.castAs<CFGAutomaticObjDtor>();
          auto *DD = const_cast<CXXDestructorDecl *>(
              AD.getDestructorDecl(ACtx->getASTContext()));
          auto *VD = const_cast<VarDecl *>(AD.getVarDecl());
          V.handleDestructorCall(VD, DD);
          break;
        }
        default:
          break;
        }
      }

      V.exitCFGBlockBody(CurrBlock);

      // Process successors, handling back edges first.
      if (V.visitSuccessors()) {
        SmallVector<CFGBlock*, 8> ForwardEdges;

        // Process successors
        for (CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin(),
                                           SE = CurrBlock->succ_end();
             SI != SE; ++SI) {
          if (*SI == nullptr)
            continue;

          if (!VisitedBlocks.alreadySet(*SI)) {
            ForwardEdges.push_back(*SI);
            continue;
          }
          V.handleSuccessorBackEdge(*SI);
        }

        for (auto *Blk : ForwardEdges)
          V.handleSuccessor(Blk);
      }

      V.exitCFGBlock(CurrBlock);
    }
    V.exitCFG(&CFGraph->getExit());
  }

  const CFG *getGraph() const { return CFGraph; }
  CFG *getGraph() { return CFGraph; }

  const NamedDecl *getDecl() const {
    return dyn_cast<NamedDecl>(ACtx->getDecl());
  }

  const PostOrderCFGView *getSortedGraph() const { return SortedGraph; }

private:
  CFG *CFGraph = nullptr;
  AnalysisDeclContext *ACtx = nullptr;
  PostOrderCFGView *SortedGraph = nullptr;
};

// TODO: move this back into ThreadSafety.cpp
// This is specific to thread safety.  It is here because
// translateAttrExpr needs it, but that should be moved too.
class CapabilityExpr {
private:
  /// The capability expression and whether it's negated.
  llvm::PointerIntPair<const til::SExpr *, 1, bool> CapExpr;

  /// The kind of capability as specified by @ref CapabilityAttr::getName.
  StringRef CapKind;

public:
  CapabilityExpr() : CapExpr(nullptr, false) {}
  CapabilityExpr(const til::SExpr *E, StringRef Kind, bool Neg)
      : CapExpr(E, Neg), CapKind(Kind) {}

  // Don't allow implicitly-constructed StringRefs since we'll capture them.
  template <typename T> CapabilityExpr(const til::SExpr *, T, bool) = delete;

  const til::SExpr *sexpr() const { return CapExpr.getPointer(); }
  StringRef getKind() const { return CapKind; }
  bool negative() const { return CapExpr.getInt(); }

  CapabilityExpr operator!() const {
    return CapabilityExpr(CapExpr.getPointer(), CapKind, !CapExpr.getInt());
  }

  bool equals(const CapabilityExpr &other) const {
    return (negative() == other.negative()) &&
           sx::equals(sexpr(), other.sexpr());
  }

  bool matches(const CapabilityExpr &other) const {
    return (negative() == other.negative()) &&
           sx::matches(sexpr(), other.sexpr());
  }

  bool matchesUniv(const CapabilityExpr &CapE) const {
    return isUniversal() || matches(CapE);
  }

  bool partiallyMatches(const CapabilityExpr &other) const {
    return (negative() == other.negative()) &&
           sx::partiallyMatches(sexpr(), other.sexpr());
  }

  const ValueDecl* valueDecl() const {
    if (negative() || sexpr() == nullptr)
      return nullptr;
    if (const auto *P = dyn_cast<til::Project>(sexpr()))
      return P->clangDecl();
    if (const auto *P = dyn_cast<til::LiteralPtr>(sexpr()))
      return P->clangDecl();
    return nullptr;
  }

  std::string toString() const {
    if (negative())
      return "!" + sx::toString(sexpr());
    return sx::toString(sexpr());
  }

  bool shouldIgnore() const { return sexpr() == nullptr; }

  bool isInvalid() const { return isa_and_nonnull<til::Undefined>(sexpr()); }

  bool isUniversal() const { return isa_and_nonnull<til::Wildcard>(sexpr()); }
};

// Translate clang::Expr to til::SExpr.
class SExprBuilder {
public:
  /// Encapsulates the lexical context of a function call.  The lexical
  /// context includes the arguments to the call, including the implicit object
  /// argument.  When an attribute containing a mutex expression is attached to
  /// a method, the expression may refer to formal parameters of the method.
  /// Actual arguments must be substituted for formal parameters to derive
  /// the appropriate mutex expression in the lexical context where the function
  /// is called.  PrevCtx holds the context in which the arguments themselves
  /// should be evaluated; multiple calling contexts can be chained together
  /// by the lock_returned attribute.
  struct CallingContext {
    // The previous context; or 0 if none.
    CallingContext  *Prev;

    // The decl to which the attr is attached.
    const NamedDecl *AttrDecl;

    // Implicit object argument -- e.g. 'this'
    llvm::PointerUnion<const Expr *, til::SExpr *> SelfArg = nullptr;

    // Number of funArgs
    unsigned NumArgs = 0;

    // Function arguments
    llvm::PointerUnion<const Expr *const *, til::SExpr *> FunArgs = nullptr;

    // is Self referred to with -> or .?
    bool SelfArrow = false;

    CallingContext(CallingContext *P, const NamedDecl *D = nullptr)
        : Prev(P), AttrDecl(D) {}
  };

  SExprBuilder(til::MemRegionRef A) : Arena(A) {
    // FIXME: we don't always have a self-variable.
    SelfVar = new (Arena) til::Variable(nullptr);
    SelfVar->setKind(til::Variable::VK_SFun);
  }

  // Translate a clang expression in an attribute to a til::SExpr.
  // Constructs the context from D, DeclExp, and SelfDecl.
  CapabilityExpr translateAttrExpr(const Expr *AttrExp, const NamedDecl *D,
                                   const Expr *DeclExp,
                                   til::SExpr *Self = nullptr);

  CapabilityExpr translateAttrExpr(const Expr *AttrExp, CallingContext *Ctx);

  // Translate a variable reference.
  til::LiteralPtr *createVariable(const VarDecl *VD);

  // Create placeholder for this: we don't know the VarDecl on construction yet.
  std::pair<til::LiteralPtr *, StringRef>
  createThisPlaceholder(const Expr *Exp);

  // Translate a clang statement or expression to a TIL expression.
  // Also performs substitution of variables; Ctx provides the context.
  // Dispatches on the type of S.
  til::SExpr *translate(const Stmt *S, CallingContext *Ctx);
  til::SCFG  *buildCFG(CFGWalker &Walker);

  til::SExpr *lookupStmt(const Stmt *S);

  til::BasicBlock *lookupBlock(const CFGBlock *B) {
    return BlockMap[B->getBlockID()];
  }

  const til::SCFG *getCFG() const { return Scfg; }
  til::SCFG *getCFG() { return Scfg; }

private:
  // We implement the CFGVisitor API
  friend class CFGWalker;

  til::SExpr *translateDeclRefExpr(const DeclRefExpr *DRE,
                                   CallingContext *Ctx) ;
  til::SExpr *translateCXXThisExpr(const CXXThisExpr *TE, CallingContext *Ctx);
  til::SExpr *translateMemberExpr(const MemberExpr *ME, CallingContext *Ctx);
  til::SExpr *translateObjCIVarRefExpr(const ObjCIvarRefExpr *IVRE,
                                       CallingContext *Ctx);
  til::SExpr *translateCallExpr(const CallExpr *CE, CallingContext *Ctx,
                                const Expr *SelfE = nullptr);
  til::SExpr *translateCXXMemberCallExpr(const CXXMemberCallExpr *ME,
                                         CallingContext *Ctx);
  til::SExpr *translateCXXOperatorCallExpr(const CXXOperatorCallExpr *OCE,
                                           CallingContext *Ctx);
  til::SExpr *translateUnaryOperator(const UnaryOperator *UO,
                                     CallingContext *Ctx);
  til::SExpr *translateBinOp(til::TIL_BinaryOpcode Op,
                             const BinaryOperator *BO,
                             CallingContext *Ctx, bool Reverse = false);
  til::SExpr *translateBinAssign(til::TIL_BinaryOpcode Op,
                                 const BinaryOperator *BO,
                                 CallingContext *Ctx, bool Assign = false);
  til::SExpr *translateBinaryOperator(const BinaryOperator *BO,
                                      CallingContext *Ctx);
  til::SExpr *translateCastExpr(const CastExpr *CE, CallingContext *Ctx);
  til::SExpr *translateArraySubscriptExpr(const ArraySubscriptExpr *E,
                                          CallingContext *Ctx);
  til::SExpr *translateAbstractConditionalOperator(
      const AbstractConditionalOperator *C, CallingContext *Ctx);

  til::SExpr *translateDeclStmt(const DeclStmt *S, CallingContext *Ctx);

  // Map from statements in the clang CFG to SExprs in the til::SCFG.
  using StatementMap = llvm::DenseMap<const Stmt *, til::SExpr *>;

  // Map from clang local variables to indices in a LVarDefinitionMap.
  using LVarIndexMap = llvm::DenseMap<const ValueDecl *, unsigned>;

  // Map from local variable indices to SSA variables (or constants).
  using NameVarPair = std::pair<const ValueDecl *, til::SExpr *>;
  using LVarDefinitionMap = CopyOnWriteVector<NameVarPair>;

  struct BlockInfo {
    LVarDefinitionMap ExitMap;
    bool HasBackEdges = false;

    // Successors yet to be processed
    unsigned UnprocessedSuccessors = 0;

    // Predecessors already processed
    unsigned ProcessedPredecessors = 0;

    BlockInfo() = default;
    BlockInfo(BlockInfo &&) = default;
    BlockInfo &operator=(BlockInfo &&) = default;
  };

  void enterCFG(CFG *Cfg, const NamedDecl *D, const CFGBlock *First);
  void enterCFGBlock(const CFGBlock *B);
  bool visitPredecessors() { return true; }
  void handlePredecessor(const CFGBlock *Pred);
  void handlePredecessorBackEdge(const CFGBlock *Pred);
  void enterCFGBlockBody(const CFGBlock *B);
  void handleStatement(const Stmt *S);
  void handleDestructorCall(const VarDecl *VD, const CXXDestructorDecl *DD);
  void exitCFGBlockBody(const CFGBlock *B);
  bool visitSuccessors() { return true; }
  void handleSuccessor(const CFGBlock *Succ);
  void handleSuccessorBackEdge(const CFGBlock *Succ);
  void exitCFGBlock(const CFGBlock *B);
  void exitCFG(const CFGBlock *Last);

  void insertStmt(const Stmt *S, til::SExpr *E) {
    SMap.insert(std::make_pair(S, E));
  }

  til::SExpr *addStatement(til::SExpr *E, const Stmt *S,
                           const ValueDecl *VD = nullptr);
  til::SExpr *lookupVarDecl(const ValueDecl *VD);
  til::SExpr *addVarDecl(const ValueDecl *VD, til::SExpr *E);
  til::SExpr *updateVarDecl(const ValueDecl *VD, til::SExpr *E);

  void makePhiNodeVar(unsigned i, unsigned NPreds, til::SExpr *E);
  void mergeEntryMap(LVarDefinitionMap Map);
  void mergeEntryMapBackEdge();
  void mergePhiNodesBackEdge(const CFGBlock *Blk);

private:
  // Set to true when parsing capability expressions, which get translated
  // inaccurately in order to hack around smart pointers etc.
  static const bool CapabilityExprMode = true;

  til::MemRegionRef Arena;

  // Variable to use for 'this'.  May be null.
  til::Variable *SelfVar = nullptr;

  til::SCFG *Scfg = nullptr;

  // Map from Stmt to TIL Variables
  StatementMap SMap;

  // Indices of clang local vars.
  LVarIndexMap LVarIdxMap;

  // Map from clang to til BBs.
  std::vector<til::BasicBlock *> BlockMap;

  // Extra information per BB. Indexed by clang BlockID.
  std::vector<BlockInfo> BBInfo;

  LVarDefinitionMap CurrentLVarMap;
  std::vector<til::Phi *> CurrentArguments;
  std::vector<til::SExpr *> CurrentInstructions;
  std::vector<til::Phi *> IncompleteArgs;
  til::BasicBlock *CurrentBB = nullptr;
  BlockInfo *CurrentBlockInfo = nullptr;
};

#ifndef NDEBUG
// Dump an SCFG to llvm::errs().
void printSCFG(CFGWalker &Walker);
#endif // NDEBUG

} // namespace threadSafety
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYCOMMON_H
