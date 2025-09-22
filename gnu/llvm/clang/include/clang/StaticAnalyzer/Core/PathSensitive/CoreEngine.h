//===- CoreEngine.h - Path-Sensitive Dataflow Engine ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a generic engine for intraprocedural, path-sensitive,
//  dataflow analysis via graph reachability.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_COREENGINE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_COREENGINE_H

#include "clang/AST/Stmt.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BlockCounter.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/WorkList.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace clang {

class AnalyzerOptions;
class CXXBindTemporaryExpr;
class Expr;
class LabelDecl;

namespace ento {

class FunctionSummariesTy;
class ExprEngine;

//===----------------------------------------------------------------------===//
/// CoreEngine - Implements the core logic of the graph-reachability
///   analysis. It traverses the CFG and generates the ExplodedGraph.
///   Program "states" are treated as opaque void pointers.
///   The template class CoreEngine (which subclasses CoreEngine)
///   provides the matching component to the engine that knows the actual types
///   for states.  Note that this engine only dispatches to transfer functions
///   at the statement and block-level.  The analyses themselves must implement
///   any transfer function logic and the sub-expression level (if any).
class CoreEngine {
  friend class CommonNodeBuilder;
  friend class EndOfFunctionNodeBuilder;
  friend class ExprEngine;
  friend class IndirectGotoNodeBuilder;
  friend class NodeBuilder;
  friend class NodeBuilderContext;
  friend class SwitchNodeBuilder;

public:
  using BlocksExhausted =
      std::vector<std::pair<BlockEdge, const ExplodedNode *>>;

  using BlocksAborted =
      std::vector<std::pair<const CFGBlock *, const ExplodedNode *>>;

private:
  ExprEngine &ExprEng;

  /// G - The simulation graph.  Each node is a (location,state) pair.
  mutable ExplodedGraph G;

  /// WList - A set of queued nodes that need to be processed by the
  ///  worklist algorithm.  It is up to the implementation of WList to decide
  ///  the order that nodes are processed.
  std::unique_ptr<WorkList> WList;
  std::unique_ptr<WorkList> CTUWList;

  /// BCounterFactory - A factory object for created BlockCounter objects.
  ///   These are used to record for key nodes in the ExplodedGraph the
  ///   number of times different CFGBlocks have been visited along a path.
  BlockCounter::Factory BCounterFactory;

  /// The locations where we stopped doing work because we visited a location
  ///  too many times.
  BlocksExhausted blocksExhausted;

  /// The locations where we stopped because the engine aborted analysis,
  /// usually because it could not reason about something.
  BlocksAborted blocksAborted;

  /// The information about functions shared by the whole translation unit.
  /// (This data is owned by AnalysisConsumer.)
  FunctionSummariesTy *FunctionSummaries;

  /// Add path tags with some useful data along the path when we see that
  /// something interesting is happening. This field is the allocator for such
  /// tags.
  DataTag::Factory DataTags;

  void setBlockCounter(BlockCounter C);

  void generateNode(const ProgramPoint &Loc,
                    ProgramStateRef State,
                    ExplodedNode *Pred);

  void HandleBlockEdge(const BlockEdge &E, ExplodedNode *Pred);
  void HandleBlockEntrance(const BlockEntrance &E, ExplodedNode *Pred);
  void HandleBlockExit(const CFGBlock *B, ExplodedNode *Pred);

  void HandleCallEnter(const CallEnter &CE, ExplodedNode *Pred);

  void HandlePostStmt(const CFGBlock *B, unsigned StmtIdx, ExplodedNode *Pred);

  void HandleBranch(const Stmt *Cond, const Stmt *Term, const CFGBlock *B,
                    ExplodedNode *Pred);
  void HandleCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                    const CFGBlock *B, ExplodedNode *Pred);

  /// Handle conditional logic for running static initializers.
  void HandleStaticInit(const DeclStmt *DS, const CFGBlock *B,
                        ExplodedNode *Pred);

  void HandleVirtualBaseBranch(const CFGBlock *B, ExplodedNode *Pred);

private:
  ExplodedNode *generateCallExitBeginNode(ExplodedNode *N,
                                          const ReturnStmt *RS);

public:
  /// Construct a CoreEngine object to analyze the provided CFG.
  CoreEngine(ExprEngine &exprengine,
             FunctionSummariesTy *FS,
             AnalyzerOptions &Opts);

  CoreEngine(const CoreEngine &) = delete;
  CoreEngine &operator=(const CoreEngine &) = delete;

  /// getGraph - Returns the exploded graph.
  ExplodedGraph &getGraph() { return G; }

  /// ExecuteWorkList - Run the worklist algorithm for a maximum number of
  ///  steps.  Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(const LocationContext *L, unsigned Steps,
                       ProgramStateRef InitState);

  /// Dispatch the work list item based on the given location information.
  /// Use Pred parameter as the predecessor state.
  void dispatchWorkItem(ExplodedNode* Pred, ProgramPoint Loc,
                        const WorkListUnit& WU);

  // Functions for external checking of whether we have unfinished work
  bool wasBlockAborted() const { return !blocksAborted.empty(); }
  bool wasBlocksExhausted() const { return !blocksExhausted.empty(); }
  bool hasWorkRemaining() const { return wasBlocksExhausted() ||
                                         WList->hasWork() ||
                                         wasBlockAborted(); }

  /// Inform the CoreEngine that a basic block was aborted because
  /// it could not be completely analyzed.
  void addAbortedBlock(const ExplodedNode *node, const CFGBlock *block) {
    blocksAborted.push_back(std::make_pair(block, node));
  }

  WorkList *getWorkList() const { return WList.get(); }
  WorkList *getCTUWorkList() const { return CTUWList.get(); }

  auto exhausted_blocks() const {
    return llvm::iterator_range(blocksExhausted);
  }

  auto aborted_blocks() const { return llvm::iterator_range(blocksAborted); }

  /// Enqueue the given set of nodes onto the work list.
  void enqueue(ExplodedNodeSet &Set);

  /// Enqueue nodes that were created as a result of processing
  /// a statement onto the work list.
  void enqueue(ExplodedNodeSet &Set, const CFGBlock *Block, unsigned Idx);

  /// enqueue the nodes corresponding to the end of function onto the
  /// end of path / work list.
  void enqueueEndOfFunction(ExplodedNodeSet &Set, const ReturnStmt *RS);

  /// Enqueue a single node created as a result of statement processing.
  void enqueueStmtNode(ExplodedNode *N, const CFGBlock *Block, unsigned Idx);

  DataTag::Factory &getDataTags() { return DataTags; }
};

class NodeBuilderContext {
  const CoreEngine &Eng;
  const CFGBlock *Block;
  const LocationContext *LC;

public:
  NodeBuilderContext(const CoreEngine &E, const CFGBlock *B,
                     const LocationContext *L)
      : Eng(E), Block(B), LC(L) {
    assert(B);
  }

  NodeBuilderContext(const CoreEngine &E, const CFGBlock *B, ExplodedNode *N)
      : NodeBuilderContext(E, B, N->getLocationContext()) {}

  /// Return the CoreEngine associated with this builder.
  const CoreEngine &getEngine() const { return Eng; }

  /// Return the CFGBlock associated with this builder.
  const CFGBlock *getBlock() const { return Block; }

  /// Return the location context associated with this builder.
  const LocationContext *getLocationContext() const { return LC; }

  /// Returns the number of times the current basic block has been
  /// visited on the exploded graph path.
  unsigned blockCount() const {
    return Eng.WList->getBlockCounter().getNumVisited(
                    LC->getStackFrame(),
                    Block->getBlockID());
  }
};

/// \class NodeBuilder
/// This is the simplest builder which generates nodes in the
/// ExplodedGraph.
///
/// The main benefit of the builder is that it automatically tracks the
/// frontier nodes (or destination set). This is the set of nodes which should
/// be propagated to the next step / builder. They are the nodes which have been
/// added to the builder (either as the input node set or as the newly
/// constructed nodes) but did not have any outgoing transitions added.
class NodeBuilder {
  virtual void anchor();

protected:
  const NodeBuilderContext &C;

  /// Specifies if the builder results have been finalized. For example, if it
  /// is set to false, autotransitions are yet to be generated.
  bool Finalized;

  bool HasGeneratedNodes = false;

  /// The frontier set - a set of nodes which need to be propagated after
  /// the builder dies.
  ExplodedNodeSet &Frontier;

  /// Checks if the results are ready.
  virtual bool checkResults() {
    return Finalized;
  }

  bool hasNoSinksInFrontier() {
    for (const auto  I : Frontier)
      if (I->isSink())
        return false;
    return true;
  }

  /// Allow subclasses to finalize results before result_begin() is executed.
  virtual void finalizeResults() {}

  ExplodedNode *generateNodeImpl(const ProgramPoint &PP,
                                 ProgramStateRef State,
                                 ExplodedNode *Pred,
                                 bool MarkAsSink = false);

public:
  NodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
              const NodeBuilderContext &Ctx, bool F = true)
      : C(Ctx), Finalized(F), Frontier(DstSet) {
    Frontier.Add(SrcNode);
  }

  NodeBuilder(const ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
              const NodeBuilderContext &Ctx, bool F = true)
      : C(Ctx), Finalized(F), Frontier(DstSet) {
    Frontier.insert(SrcSet);
    assert(hasNoSinksInFrontier());
  }

  virtual ~NodeBuilder() = default;

  /// Generates a node in the ExplodedGraph.
  ExplodedNode *generateNode(const ProgramPoint &PP,
                             ProgramStateRef State,
                             ExplodedNode *Pred) {
    return generateNodeImpl(
        PP, State, Pred,
        /*MarkAsSink=*/State->isPosteriorlyOverconstrained());
  }

  /// Generates a sink in the ExplodedGraph.
  ///
  /// When a node is marked as sink, the exploration from the node is stopped -
  /// the node becomes the last node on the path and certain kinds of bugs are
  /// suppressed.
  ExplodedNode *generateSink(const ProgramPoint &PP,
                             ProgramStateRef State,
                             ExplodedNode *Pred) {
    return generateNodeImpl(PP, State, Pred, true);
  }

  const ExplodedNodeSet &getResults() {
    finalizeResults();
    assert(checkResults());
    return Frontier;
  }

  using iterator = ExplodedNodeSet::iterator;

  /// Iterators through the results frontier.
  iterator begin() {
    finalizeResults();
    assert(checkResults());
    return Frontier.begin();
  }

  iterator end() {
    finalizeResults();
    return Frontier.end();
  }

  const NodeBuilderContext &getContext() { return C; }
  bool hasGeneratedNodes() { return HasGeneratedNodes; }

  void takeNodes(const ExplodedNodeSet &S) {
    for (const auto I : S)
      Frontier.erase(I);
  }

  void takeNodes(ExplodedNode *N) { Frontier.erase(N); }
  void addNodes(const ExplodedNodeSet &S) { Frontier.insert(S); }
  void addNodes(ExplodedNode *N) { Frontier.Add(N); }
};

/// \class NodeBuilderWithSinks
/// This node builder keeps track of the generated sink nodes.
class NodeBuilderWithSinks: public NodeBuilder {
  void anchor() override;

protected:
  SmallVector<ExplodedNode*, 2> sinksGenerated;
  ProgramPoint &Location;

public:
  NodeBuilderWithSinks(ExplodedNode *Pred, ExplodedNodeSet &DstSet,
                       const NodeBuilderContext &Ctx, ProgramPoint &L)
      : NodeBuilder(Pred, DstSet, Ctx), Location(L) {}

  ExplodedNode *generateNode(ProgramStateRef State,
                             ExplodedNode *Pred,
                             const ProgramPointTag *Tag = nullptr) {
    const ProgramPoint &LocalLoc = (Tag ? Location.withTag(Tag) : Location);
    return NodeBuilder::generateNode(LocalLoc, State, Pred);
  }

  ExplodedNode *generateSink(ProgramStateRef State, ExplodedNode *Pred,
                             const ProgramPointTag *Tag = nullptr) {
    const ProgramPoint &LocalLoc = (Tag ? Location.withTag(Tag) : Location);
    ExplodedNode *N = NodeBuilder::generateSink(LocalLoc, State, Pred);
    if (N && N->isSink())
      sinksGenerated.push_back(N);
    return N;
  }

  const SmallVectorImpl<ExplodedNode*> &getSinks() const {
    return sinksGenerated;
  }
};

/// \class StmtNodeBuilder
/// This builder class is useful for generating nodes that resulted from
/// visiting a statement. The main difference from its parent NodeBuilder is
/// that it creates a statement specific ProgramPoint.
class StmtNodeBuilder: public NodeBuilder {
  NodeBuilder *EnclosingBldr;

public:
  /// Constructs a StmtNodeBuilder. If the builder is going to process
  /// nodes currently owned by another builder(with larger scope), use
  /// Enclosing builder to transfer ownership.
  StmtNodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
                  const NodeBuilderContext &Ctx,
                  NodeBuilder *Enclosing = nullptr)
      : NodeBuilder(SrcNode, DstSet, Ctx), EnclosingBldr(Enclosing) {
    if (EnclosingBldr)
      EnclosingBldr->takeNodes(SrcNode);
  }

  StmtNodeBuilder(ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
                  const NodeBuilderContext &Ctx,
                  NodeBuilder *Enclosing = nullptr)
      : NodeBuilder(SrcSet, DstSet, Ctx), EnclosingBldr(Enclosing) {
    if (EnclosingBldr)
      for (const auto I : SrcSet)
        EnclosingBldr->takeNodes(I);
  }

  ~StmtNodeBuilder() override;

  using NodeBuilder::generateNode;
  using NodeBuilder::generateSink;

  ExplodedNode *generateNode(const Stmt *S,
                             ExplodedNode *Pred,
                             ProgramStateRef St,
                             const ProgramPointTag *tag = nullptr,
                             ProgramPoint::Kind K = ProgramPoint::PostStmtKind){
    const ProgramPoint &L = ProgramPoint::getProgramPoint(S, K,
                                  Pred->getLocationContext(), tag);
    return NodeBuilder::generateNode(L, St, Pred);
  }

  ExplodedNode *generateSink(const Stmt *S,
                             ExplodedNode *Pred,
                             ProgramStateRef St,
                             const ProgramPointTag *tag = nullptr,
                             ProgramPoint::Kind K = ProgramPoint::PostStmtKind){
    const ProgramPoint &L = ProgramPoint::getProgramPoint(S, K,
                                  Pred->getLocationContext(), tag);
    return NodeBuilder::generateSink(L, St, Pred);
  }
};

/// BranchNodeBuilder is responsible for constructing the nodes
/// corresponding to the two branches of the if statement - true and false.
class BranchNodeBuilder: public NodeBuilder {
  const CFGBlock *DstT;
  const CFGBlock *DstF;

  bool InFeasibleTrue;
  bool InFeasibleFalse;

  void anchor() override;

public:
  BranchNodeBuilder(ExplodedNode *SrcNode, ExplodedNodeSet &DstSet,
                    const NodeBuilderContext &C,
                    const CFGBlock *dstT, const CFGBlock *dstF)
      : NodeBuilder(SrcNode, DstSet, C), DstT(dstT), DstF(dstF),
        InFeasibleTrue(!DstT), InFeasibleFalse(!DstF) {
    // The branch node builder does not generate autotransitions.
    // If there are no successors it means that both branches are infeasible.
    takeNodes(SrcNode);
  }

  BranchNodeBuilder(const ExplodedNodeSet &SrcSet, ExplodedNodeSet &DstSet,
                    const NodeBuilderContext &C,
                    const CFGBlock *dstT, const CFGBlock *dstF)
      : NodeBuilder(SrcSet, DstSet, C), DstT(dstT), DstF(dstF),
        InFeasibleTrue(!DstT), InFeasibleFalse(!DstF) {
    takeNodes(SrcSet);
  }

  ExplodedNode *generateNode(ProgramStateRef State, bool branch,
                             ExplodedNode *Pred);

  const CFGBlock *getTargetBlock(bool branch) const {
    return branch ? DstT : DstF;
  }

  void markInfeasible(bool branch) {
    if (branch)
      InFeasibleTrue = true;
    else
      InFeasibleFalse = true;
  }

  bool isFeasible(bool branch) {
    return branch ? !InFeasibleTrue : !InFeasibleFalse;
  }
};

class IndirectGotoNodeBuilder {
  CoreEngine& Eng;
  const CFGBlock *Src;
  const CFGBlock &DispatchBlock;
  const Expr *E;
  ExplodedNode *Pred;

public:
  IndirectGotoNodeBuilder(ExplodedNode *pred, const CFGBlock *src,
                    const Expr *e, const CFGBlock *dispatch, CoreEngine* eng)
      : Eng(*eng), Src(src), DispatchBlock(*dispatch), E(e), Pred(pred) {}

  class iterator {
    friend class IndirectGotoNodeBuilder;

    CFGBlock::const_succ_iterator I;

    iterator(CFGBlock::const_succ_iterator i) : I(i) {}

  public:
    // This isn't really a conventional iterator.
    // We just implement the deref as a no-op for now to make range-based for
    // loops work.
    const iterator &operator*() const { return *this; }

    iterator &operator++() { ++I; return *this; }
    bool operator!=(const iterator &X) const { return I != X.I; }

    const LabelDecl *getLabel() const {
      return cast<LabelStmt>((*I)->getLabel())->getDecl();
    }

    const CFGBlock *getBlock() const {
      return *I;
    }
  };

  iterator begin() { return iterator(DispatchBlock.succ_begin()); }
  iterator end() { return iterator(DispatchBlock.succ_end()); }

  ExplodedNode *generateNode(const iterator &I,
                             ProgramStateRef State,
                             bool isSink = false);

  const Expr *getTarget() const { return E; }

  ProgramStateRef getState() const { return Pred->State; }

  const LocationContext *getLocationContext() const {
    return Pred->getLocationContext();
  }
};

class SwitchNodeBuilder {
  CoreEngine& Eng;
  const CFGBlock *Src;
  const Expr *Condition;
  ExplodedNode *Pred;

public:
  SwitchNodeBuilder(ExplodedNode *pred, const CFGBlock *src,
                    const Expr *condition, CoreEngine* eng)
      : Eng(*eng), Src(src), Condition(condition), Pred(pred) {}

  class iterator {
    friend class SwitchNodeBuilder;

    CFGBlock::const_succ_reverse_iterator I;

    iterator(CFGBlock::const_succ_reverse_iterator i) : I(i) {}

  public:
    iterator &operator++() { ++I; return *this; }
    bool operator!=(const iterator &X) const { return I != X.I; }
    bool operator==(const iterator &X) const { return I == X.I; }

    const CaseStmt *getCase() const {
      return cast<CaseStmt>((*I)->getLabel());
    }

    const CFGBlock *getBlock() const {
      return *I;
    }
  };

  iterator begin() { return iterator(Src->succ_rbegin()+1); }
  iterator end() { return iterator(Src->succ_rend()); }

  const SwitchStmt *getSwitch() const {
    return cast<SwitchStmt>(Src->getTerminator());
  }

  ExplodedNode *generateCaseStmtNode(const iterator &I,
                                     ProgramStateRef State);

  ExplodedNode *generateDefaultCaseNode(ProgramStateRef State,
                                        bool isSink = false);

  const Expr *getCondition() const { return Condition; }

  ProgramStateRef getState() const { return Pred->State; }

  const LocationContext *getLocationContext() const {
    return Pred->getLocationContext();
  }
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_COREENGINE_H
