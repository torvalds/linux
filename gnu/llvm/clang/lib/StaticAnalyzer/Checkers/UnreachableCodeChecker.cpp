//==- UnreachableCodeChecker.cpp - Generalized dead code checker -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file implements a generalized unreachable code checker using a
// path-sensitive analysis. We mark any path visited, and then walk the CFG as a
// post-analysis to determine what was never visited.
//
// A similar flow-sensitive only check exists in Analysis/ReachableCode.cpp
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/SmallSet.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
class UnreachableCodeChecker : public Checker<check::EndAnalysis> {
public:
  void checkEndAnalysis(ExplodedGraph &G, BugReporter &B,
                        ExprEngine &Eng) const;
private:
  typedef llvm::SmallSet<unsigned, 32> CFGBlocksSet;

  static inline const Stmt *getUnreachableStmt(const CFGBlock *CB);
  static void FindUnreachableEntryPoints(const CFGBlock *CB,
                                         CFGBlocksSet &reachable,
                                         CFGBlocksSet &visited);
  static bool isInvalidPath(const CFGBlock *CB, const ParentMap &PM);
  static inline bool isEmptyCFGBlock(const CFGBlock *CB);
};
}

void UnreachableCodeChecker::checkEndAnalysis(ExplodedGraph &G,
                                              BugReporter &B,
                                              ExprEngine &Eng) const {
  CFGBlocksSet reachable, visited;

  if (Eng.hasWorkRemaining())
    return;

  const Decl *D = nullptr;
  CFG *C = nullptr;
  const ParentMap *PM = nullptr;
  const LocationContext *LC = nullptr;
  // Iterate over ExplodedGraph
  for (const ExplodedNode &N : G.nodes()) {
    const ProgramPoint &P = N.getLocation();
    LC = P.getLocationContext();
    if (!LC->inTopFrame())
      continue;

    if (!D)
      D = LC->getAnalysisDeclContext()->getDecl();

    // Save the CFG if we don't have it already
    if (!C)
      C = LC->getAnalysisDeclContext()->getUnoptimizedCFG();
    if (!PM)
      PM = &LC->getParentMap();

    if (std::optional<BlockEntrance> BE = P.getAs<BlockEntrance>()) {
      const CFGBlock *CB = BE->getBlock();
      reachable.insert(CB->getBlockID());
    }
  }

  // Bail out if we didn't get the CFG or the ParentMap.
  if (!D || !C || !PM)
    return;

  // Don't do anything for template instantiations.  Proving that code
  // in a template instantiation is unreachable means proving that it is
  // unreachable in all instantiations.
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isTemplateInstantiation())
      return;

  // Find CFGBlocks that were not covered by any node
  for (const CFGBlock *CB : *C) {
    // Check if the block is unreachable
    if (reachable.count(CB->getBlockID()))
      continue;

    // Check if the block is empty (an artificial block)
    if (isEmptyCFGBlock(CB))
      continue;

    // Find the entry points for this block
    if (!visited.count(CB->getBlockID()))
      FindUnreachableEntryPoints(CB, reachable, visited);

    // This block may have been pruned; check if we still want to report it
    if (reachable.count(CB->getBlockID()))
      continue;

    // Check for false positives
    if (isInvalidPath(CB, *PM))
      continue;

    // It is good practice to always have a "default" label in a "switch", even
    // if we should never get there. It can be used to detect errors, for
    // instance. Unreachable code directly under a "default" label is therefore
    // likely to be a false positive.
    if (const Stmt *label = CB->getLabel())
      if (label->getStmtClass() == Stmt::DefaultStmtClass)
        continue;

    // Special case for __builtin_unreachable.
    // FIXME: This should be extended to include other unreachable markers,
    // such as llvm_unreachable.
    if (!CB->empty()) {
      bool foundUnreachable = false;
      for (CFGBlock::const_iterator ci = CB->begin(), ce = CB->end();
           ci != ce; ++ci) {
        if (std::optional<CFGStmt> S = (*ci).getAs<CFGStmt>())
          if (const CallExpr *CE = dyn_cast<CallExpr>(S->getStmt())) {
            if (CE->getBuiltinCallee() == Builtin::BI__builtin_unreachable ||
                CE->isBuiltinAssumeFalse(Eng.getContext())) {
              foundUnreachable = true;
              break;
            }
          }
      }
      if (foundUnreachable)
        continue;
    }

    // We found a block that wasn't covered - find the statement to report
    SourceRange SR;
    PathDiagnosticLocation DL;
    SourceLocation SL;
    if (const Stmt *S = getUnreachableStmt(CB)) {
      // In macros, 'do {...} while (0)' is often used. Don't warn about the
      // condition 0 when it is unreachable.
      if (S->getBeginLoc().isMacroID())
        if (const auto *I = dyn_cast<IntegerLiteral>(S))
          if (I->getValue() == 0ULL)
            if (const Stmt *Parent = PM->getParent(S))
              if (isa<DoStmt>(Parent))
                continue;
      SR = S->getSourceRange();
      DL = PathDiagnosticLocation::createBegin(S, B.getSourceManager(), LC);
      SL = DL.asLocation();
      if (SR.isInvalid() || !SL.isValid())
        continue;
      if (isa<CXXTryStmt>(S))
        continue;
    }
    else
      continue;

    // Check if the SourceLocation is in a system header
    const SourceManager &SM = B.getSourceManager();
    if (SM.isInSystemHeader(SL) || SM.isInExternCSystemHeader(SL))
      continue;

    B.EmitBasicReport(D, this, "Unreachable code", categories::UnusedCode,
                      "This statement is never executed", DL, SR);
  }
}

// Recursively finds the entry point(s) for this dead CFGBlock.
void UnreachableCodeChecker::FindUnreachableEntryPoints(const CFGBlock *CB,
                                                        CFGBlocksSet &reachable,
                                                        CFGBlocksSet &visited) {
  visited.insert(CB->getBlockID());

  for (const CFGBlock *PredBlock : CB->preds()) {
    if (!PredBlock)
      continue;

    if (!reachable.count(PredBlock->getBlockID())) {
      // If we find an unreachable predecessor, mark this block as reachable so
      // we don't report this block
      reachable.insert(CB->getBlockID());
      if (!visited.count(PredBlock->getBlockID()))
        // If we haven't previously visited the unreachable predecessor, recurse
        FindUnreachableEntryPoints(PredBlock, reachable, visited);
    }
  }
}

// Find the Stmt* in a CFGBlock for reporting a warning
const Stmt *UnreachableCodeChecker::getUnreachableStmt(const CFGBlock *CB) {
  for (const CFGElement &Elem : *CB) {
    if (std::optional<CFGStmt> S = Elem.getAs<CFGStmt>()) {
      if (!isa<DeclStmt>(S->getStmt()))
        return S->getStmt();
    }
  }
  return CB->getTerminatorStmt();
}

// Determines if the path to this CFGBlock contained an element that infers this
// block is a false positive. We assume that FindUnreachableEntryPoints has
// already marked only the entry points to any dead code, so we need only to
// find the condition that led to this block (the predecessor of this block.)
// There will never be more than one predecessor.
bool UnreachableCodeChecker::isInvalidPath(const CFGBlock *CB,
                                           const ParentMap &PM) {
  // We only expect a predecessor size of 0 or 1. If it is >1, then an external
  // condition has broken our assumption (for example, a sink being placed by
  // another check). In these cases, we choose not to report.
  if (CB->pred_size() > 1)
    return true;

  // If there are no predecessors, then this block is trivially unreachable
  if (CB->pred_size() == 0)
    return false;

  const CFGBlock *pred = *CB->pred_begin();
  if (!pred)
    return false;

  // Get the predecessor block's terminator condition
  const Stmt *cond = pred->getTerminatorCondition();

  //assert(cond && "CFGBlock's predecessor has a terminator condition");
  // The previous assertion is invalid in some cases (eg do/while). Leaving
  // reporting of these situations on at the moment to help triage these cases.
  if (!cond)
    return false;

  // Run each of the checks on the conditions
  return containsMacro(cond) || containsEnum(cond) ||
         containsStaticLocal(cond) || containsBuiltinOffsetOf(cond) ||
         containsStmt<UnaryExprOrTypeTraitExpr>(cond);
}

// Returns true if the given CFGBlock is empty
bool UnreachableCodeChecker::isEmptyCFGBlock(const CFGBlock *CB) {
  return CB->getLabel() == nullptr // No labels
      && CB->size() == 0           // No statements
      && !CB->getTerminatorStmt(); // No terminator
}

void ento::registerUnreachableCodeChecker(CheckerManager &mgr) {
  mgr.registerChecker<UnreachableCodeChecker>();
}

bool ento::shouldRegisterUnreachableCodeChecker(const CheckerManager &mgr) {
  return true;
}