//===- ExplodedGraph.cpp - Local, Path-Sens. "Exploded Graph" -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the template classes ExplodedNode and ExplodedGraph,
//  which represent a path-sensitive, intra-procedural "exploded graph."
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFGStmtMap.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <memory>
#include <optional>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Cleanup.
//===----------------------------------------------------------------------===//

ExplodedGraph::ExplodedGraph() = default;

ExplodedGraph::~ExplodedGraph() = default;

//===----------------------------------------------------------------------===//
// Node reclamation.
//===----------------------------------------------------------------------===//

bool ExplodedGraph::isInterestingLValueExpr(const Expr *Ex) {
  if (!Ex->isLValue())
    return false;
  return isa<DeclRefExpr, MemberExpr, ObjCIvarRefExpr, ArraySubscriptExpr>(Ex);
}

bool ExplodedGraph::shouldCollect(const ExplodedNode *node) {
  // First, we only consider nodes for reclamation of the following
  // conditions apply:
  //
  // (1) 1 predecessor (that has one successor)
  // (2) 1 successor (that has one predecessor)
  //
  // If a node has no successor it is on the "frontier", while a node
  // with no predecessor is a root.
  //
  // After these prerequisites, we discard all "filler" nodes that
  // are used only for intermediate processing, and are not essential
  // for analyzer history:
  //
  // (a) PreStmtPurgeDeadSymbols
  //
  // We then discard all other nodes where *all* of the following conditions
  // apply:
  //
  // (3) The ProgramPoint is for a PostStmt, but not a PostStore.
  // (4) There is no 'tag' for the ProgramPoint.
  // (5) The 'store' is the same as the predecessor.
  // (6) The 'GDM' is the same as the predecessor.
  // (7) The LocationContext is the same as the predecessor.
  // (8) Expressions that are *not* lvalue expressions.
  // (9) The PostStmt isn't for a non-consumed Stmt or Expr.
  // (10) The successor is neither a CallExpr StmtPoint nor a CallEnter or
  //      PreImplicitCall (so that we would be able to find it when retrying a
  //      call with no inlining).
  // FIXME: It may be safe to reclaim PreCall and PostCall nodes as well.

  // Conditions 1 and 2.
  if (node->pred_size() != 1 || node->succ_size() != 1)
    return false;

  const ExplodedNode *pred = *(node->pred_begin());
  if (pred->succ_size() != 1)
    return false;

  const ExplodedNode *succ = *(node->succ_begin());
  if (succ->pred_size() != 1)
    return false;

  // Now reclaim any nodes that are (by definition) not essential to
  // analysis history and are not consulted by any client code.
  ProgramPoint progPoint = node->getLocation();
  if (progPoint.getAs<PreStmtPurgeDeadSymbols>())
    return !progPoint.getTag();

  // Condition 3.
  if (!progPoint.getAs<PostStmt>() || progPoint.getAs<PostStore>())
    return false;

  // Condition 4.
  if (progPoint.getTag())
    return false;

  // Conditions 5, 6, and 7.
  ProgramStateRef state = node->getState();
  ProgramStateRef pred_state = pred->getState();
  if (state->store != pred_state->store || state->GDM != pred_state->GDM ||
      progPoint.getLocationContext() != pred->getLocationContext())
    return false;

  // All further checks require expressions. As per #3, we know that we have
  // a PostStmt.
  const Expr *Ex = dyn_cast<Expr>(progPoint.castAs<PostStmt>().getStmt());
  if (!Ex)
    return false;

  // Condition 8.
  // Do not collect nodes for "interesting" lvalue expressions since they are
  // used extensively for generating path diagnostics.
  if (isInterestingLValueExpr(Ex))
    return false;

  // Condition 9.
  // Do not collect nodes for non-consumed Stmt or Expr to ensure precise
  // diagnostic generation; specifically, so that we could anchor arrows
  // pointing to the beginning of statements (as written in code).
  const ParentMap &PM = progPoint.getLocationContext()->getParentMap();
  if (!PM.isConsumedExpr(Ex))
    return false;

  // Condition 10.
  const ProgramPoint SuccLoc = succ->getLocation();
  if (std::optional<StmtPoint> SP = SuccLoc.getAs<StmtPoint>())
    if (CallEvent::isCallStmt(SP->getStmt()))
      return false;

  // Condition 10, continuation.
  if (SuccLoc.getAs<CallEnter>() || SuccLoc.getAs<PreImplicitCall>())
    return false;

  return true;
}

void ExplodedGraph::collectNode(ExplodedNode *node) {
  // Removing a node means:
  // (a) changing the predecessors successor to the successor of this node
  // (b) changing the successors predecessor to the predecessor of this node
  // (c) Putting 'node' onto freeNodes.
  assert(node->pred_size() == 1 || node->succ_size() == 1);
  ExplodedNode *pred = *(node->pred_begin());
  ExplodedNode *succ = *(node->succ_begin());
  pred->replaceSuccessor(succ);
  succ->replacePredecessor(pred);
  FreeNodes.push_back(node);
  Nodes.RemoveNode(node);
  --NumNodes;
  node->~ExplodedNode();
}

void ExplodedGraph::reclaimRecentlyAllocatedNodes() {
  if (ChangedNodes.empty())
    return;

  // Only periodically reclaim nodes so that we can build up a set of
  // nodes that meet the reclamation criteria.  Freshly created nodes
  // by definition have no successor, and thus cannot be reclaimed (see below).
  assert(ReclaimCounter > 0);
  if (--ReclaimCounter != 0)
    return;
  ReclaimCounter = ReclaimNodeInterval;

  for (const auto node : ChangedNodes)
    if (shouldCollect(node))
      collectNode(node);
  ChangedNodes.clear();
}

//===----------------------------------------------------------------------===//
// ExplodedNode.
//===----------------------------------------------------------------------===//

// An NodeGroup's storage type is actually very much like a TinyPtrVector:
// it can be either a pointer to a single ExplodedNode, or a pointer to a
// BumpVector allocated with the ExplodedGraph's allocator. This allows the
// common case of single-node NodeGroups to be implemented with no extra memory.
//
// Consequently, each of the NodeGroup methods have up to four cases to handle:
// 1. The flag is set and this group does not actually contain any nodes.
// 2. The group is empty, in which case the storage value is null.
// 3. The group contains a single node.
// 4. The group contains more than one node.
using ExplodedNodeVector = BumpVector<ExplodedNode *>;
using GroupStorage = llvm::PointerUnion<ExplodedNode *, ExplodedNodeVector *>;

void ExplodedNode::addPredecessor(ExplodedNode *V, ExplodedGraph &G) {
  assert(!V->isSink());
  Preds.addNode(V, G);
  V->Succs.addNode(this, G);
}

void ExplodedNode::NodeGroup::replaceNode(ExplodedNode *node) {
  assert(!getFlag());

  GroupStorage &Storage = reinterpret_cast<GroupStorage&>(P);
  assert(Storage.is<ExplodedNode *>());
  Storage = node;
  assert(Storage.is<ExplodedNode *>());
}

void ExplodedNode::NodeGroup::addNode(ExplodedNode *N, ExplodedGraph &G) {
  assert(!getFlag());

  GroupStorage &Storage = reinterpret_cast<GroupStorage&>(P);
  if (Storage.isNull()) {
    Storage = N;
    assert(Storage.is<ExplodedNode *>());
    return;
  }

  ExplodedNodeVector *V = Storage.dyn_cast<ExplodedNodeVector *>();

  if (!V) {
    // Switch from single-node to multi-node representation.
    ExplodedNode *Old = Storage.get<ExplodedNode *>();

    BumpVectorContext &Ctx = G.getNodeAllocator();
    V = new (G.getAllocator()) ExplodedNodeVector(Ctx, 4);
    V->push_back(Old, Ctx);

    Storage = V;
    assert(!getFlag());
    assert(Storage.is<ExplodedNodeVector *>());
  }

  V->push_back(N, G.getNodeAllocator());
}

unsigned ExplodedNode::NodeGroup::size() const {
  if (getFlag())
    return 0;

  const GroupStorage &Storage = reinterpret_cast<const GroupStorage &>(P);
  if (Storage.isNull())
    return 0;
  if (ExplodedNodeVector *V = Storage.dyn_cast<ExplodedNodeVector *>())
    return V->size();
  return 1;
}

ExplodedNode * const *ExplodedNode::NodeGroup::begin() const {
  if (getFlag())
    return nullptr;

  const GroupStorage &Storage = reinterpret_cast<const GroupStorage &>(P);
  if (Storage.isNull())
    return nullptr;
  if (ExplodedNodeVector *V = Storage.dyn_cast<ExplodedNodeVector *>())
    return V->begin();
  return Storage.getAddrOfPtr1();
}

ExplodedNode * const *ExplodedNode::NodeGroup::end() const {
  if (getFlag())
    return nullptr;

  const GroupStorage &Storage = reinterpret_cast<const GroupStorage &>(P);
  if (Storage.isNull())
    return nullptr;
  if (ExplodedNodeVector *V = Storage.dyn_cast<ExplodedNodeVector *>())
    return V->end();
  return Storage.getAddrOfPtr1() + 1;
}

bool ExplodedNode::isTrivial() const {
  return pred_size() == 1 && succ_size() == 1 &&
         getFirstPred()->getState()->getID() == getState()->getID() &&
         getFirstPred()->succ_size() == 1;
}

const CFGBlock *ExplodedNode::getCFGBlock() const {
  ProgramPoint P = getLocation();
  if (auto BEP = P.getAs<BlockEntrance>())
    return BEP->getBlock();

  // Find the node's current statement in the CFG.
  // FIXME: getStmtForDiagnostics() does nasty things in order to provide
  // a valid statement for body farms, do we need this behavior here?
  if (const Stmt *S = getStmtForDiagnostics())
    return getLocationContext()
        ->getAnalysisDeclContext()
        ->getCFGStmtMap()
        ->getBlock(S);

  return nullptr;
}

static const LocationContext *
findTopAutosynthesizedParentContext(const LocationContext *LC) {
  assert(LC->getAnalysisDeclContext()->isBodyAutosynthesized());
  const LocationContext *ParentLC = LC->getParent();
  assert(ParentLC && "We don't start analysis from autosynthesized code");
  while (ParentLC->getAnalysisDeclContext()->isBodyAutosynthesized()) {
    LC = ParentLC;
    ParentLC = LC->getParent();
    assert(ParentLC && "We don't start analysis from autosynthesized code");
  }
  return LC;
}

const Stmt *ExplodedNode::getStmtForDiagnostics() const {
  // We cannot place diagnostics on autosynthesized code.
  // Put them onto the call site through which we jumped into autosynthesized
  // code for the first time.
  const LocationContext *LC = getLocationContext();
  if (LC->getAnalysisDeclContext()->isBodyAutosynthesized()) {
    // It must be a stack frame because we only autosynthesize functions.
    return cast<StackFrameContext>(findTopAutosynthesizedParentContext(LC))
        ->getCallSite();
  }
  // Otherwise, see if the node's program point directly points to a statement.
  // FIXME: Refactor into a ProgramPoint method?
  ProgramPoint P = getLocation();
  if (auto SP = P.getAs<StmtPoint>())
    return SP->getStmt();
  if (auto BE = P.getAs<BlockEdge>())
    return BE->getSrc()->getTerminatorStmt();
  if (auto CE = P.getAs<CallEnter>())
    return CE->getCallExpr();
  if (auto CEE = P.getAs<CallExitEnd>())
    return CEE->getCalleeContext()->getCallSite();
  if (auto PIPP = P.getAs<PostInitializer>())
    return PIPP->getInitializer()->getInit();
  if (auto CEB = P.getAs<CallExitBegin>())
    return CEB->getReturnStmt();
  if (auto FEP = P.getAs<FunctionExitPoint>())
    return FEP->getStmt();

  return nullptr;
}

const Stmt *ExplodedNode::getNextStmtForDiagnostics() const {
  for (const ExplodedNode *N = getFirstSucc(); N; N = N->getFirstSucc()) {
    if (const Stmt *S = N->getStmtForDiagnostics()) {
      // Check if the statement is '?' or '&&'/'||'.  These are "merges",
      // not actual statement points.
      switch (S->getStmtClass()) {
        case Stmt::ChooseExprClass:
        case Stmt::BinaryConditionalOperatorClass:
        case Stmt::ConditionalOperatorClass:
          continue;
        case Stmt::BinaryOperatorClass: {
          BinaryOperatorKind Op = cast<BinaryOperator>(S)->getOpcode();
          if (Op == BO_LAnd || Op == BO_LOr)
            continue;
          break;
        }
        default:
          break;
      }
      // We found the statement, so return it.
      return S;
    }
  }

  return nullptr;
}

const Stmt *ExplodedNode::getPreviousStmtForDiagnostics() const {
  for (const ExplodedNode *N = getFirstPred(); N; N = N->getFirstPred())
    if (const Stmt *S = N->getStmtForDiagnostics())
      return S;

  return nullptr;
}

const Stmt *ExplodedNode::getCurrentOrPreviousStmtForDiagnostics() const {
  if (const Stmt *S = getStmtForDiagnostics())
    return S;

  return getPreviousStmtForDiagnostics();
}

ExplodedNode *ExplodedGraph::getNode(const ProgramPoint &L,
                                     ProgramStateRef State,
                                     bool IsSink,
                                     bool* IsNew) {
  // Profile 'State' to determine if we already have an existing node.
  llvm::FoldingSetNodeID profile;
  void *InsertPos = nullptr;

  NodeTy::Profile(profile, L, State, IsSink);
  NodeTy* V = Nodes.FindNodeOrInsertPos(profile, InsertPos);

  if (!V) {
    if (!FreeNodes.empty()) {
      V = FreeNodes.back();
      FreeNodes.pop_back();
    }
    else {
      // Allocate a new node.
      V = getAllocator().Allocate<NodeTy>();
    }

    ++NumNodes;
    new (V) NodeTy(L, State, NumNodes, IsSink);

    if (ReclaimNodeInterval)
      ChangedNodes.push_back(V);

    // Insert the node into the node set and return it.
    Nodes.InsertNode(V, InsertPos);

    if (IsNew) *IsNew = true;
  }
  else
    if (IsNew) *IsNew = false;

  return V;
}

ExplodedNode *ExplodedGraph::createUncachedNode(const ProgramPoint &L,
                                                ProgramStateRef State,
                                                int64_t Id,
                                                bool IsSink) {
  NodeTy *V = getAllocator().Allocate<NodeTy>();
  new (V) NodeTy(L, State, Id, IsSink);
  return V;
}

std::unique_ptr<ExplodedGraph>
ExplodedGraph::trim(ArrayRef<const NodeTy *> Sinks,
                    InterExplodedGraphMap *ForwardMap,
                    InterExplodedGraphMap *InverseMap) const {
  if (Nodes.empty())
    return nullptr;

  using Pass1Ty = llvm::DenseSet<const ExplodedNode *>;
  Pass1Ty Pass1;

  using Pass2Ty = InterExplodedGraphMap;
  InterExplodedGraphMap Pass2Scratch;
  Pass2Ty &Pass2 = ForwardMap ? *ForwardMap : Pass2Scratch;

  SmallVector<const ExplodedNode*, 10> WL1, WL2;

  // ===- Pass 1 (reverse DFS) -===
  for (const auto Sink : Sinks)
    if (Sink)
      WL1.push_back(Sink);

  // Process the first worklist until it is empty.
  while (!WL1.empty()) {
    const ExplodedNode *N = WL1.pop_back_val();

    // Have we already visited this node?  If so, continue to the next one.
    if (!Pass1.insert(N).second)
      continue;

    // If this is a root enqueue it to the second worklist.
    if (N->Preds.empty()) {
      WL2.push_back(N);
      continue;
    }

    // Visit our predecessors and enqueue them.
    WL1.append(N->Preds.begin(), N->Preds.end());
  }

  // We didn't hit a root? Return with a null pointer for the new graph.
  if (WL2.empty())
    return nullptr;

  // Create an empty graph.
  std::unique_ptr<ExplodedGraph> G = MakeEmptyGraph();

  // ===- Pass 2 (forward DFS to construct the new graph) -===
  while (!WL2.empty()) {
    const ExplodedNode *N = WL2.pop_back_val();

    // Skip this node if we have already processed it.
    if (Pass2.contains(N))
      continue;

    // Create the corresponding node in the new graph and record the mapping
    // from the old node to the new node.
    ExplodedNode *NewN = G->createUncachedNode(N->getLocation(), N->State,
                                               N->getID(), N->isSink());
    Pass2[N] = NewN;

    // Also record the reverse mapping from the new node to the old node.
    if (InverseMap) (*InverseMap)[NewN] = N;

    // If this node is a root, designate it as such in the graph.
    if (N->Preds.empty())
      G->addRoot(NewN);

    // In the case that some of the intended predecessors of NewN have already
    // been created, we should hook them up as predecessors.

    // Walk through the predecessors of 'N' and hook up their corresponding
    // nodes in the new graph (if any) to the freshly created node.
    for (const ExplodedNode *Pred : N->Preds) {
      Pass2Ty::iterator PI = Pass2.find(Pred);
      if (PI == Pass2.end())
        continue;

      NewN->addPredecessor(const_cast<ExplodedNode *>(PI->second), *G);
    }

    // In the case that some of the intended successors of NewN have already
    // been created, we should hook them up as successors.  Otherwise, enqueue
    // the new nodes from the original graph that should have nodes created
    // in the new graph.
    for (const ExplodedNode *Succ : N->Succs) {
      Pass2Ty::iterator PI = Pass2.find(Succ);
      if (PI != Pass2.end()) {
        const_cast<ExplodedNode *>(PI->second)->addPredecessor(NewN, *G);
        continue;
      }

      // Enqueue nodes to the worklist that were marked during pass 1.
      if (Pass1.count(Succ))
        WL2.push_back(Succ);
    }
  }

  return G;
}
