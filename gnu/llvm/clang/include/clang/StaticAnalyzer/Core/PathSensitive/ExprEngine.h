//===- ExprEngine.h - Path-Sensitive Expression-Level Dataflow --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a meta-engine for path-sensitive dataflow analysis that
//  is built on CoreEngine, but provides the boilerplate to execute transfer
//  functions and build the ExplodedGraph at the expression level.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/DomainSpecific/ObjCNoReturn.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CoreEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/WorkList.h"
#include "llvm/ADT/ArrayRef.h"
#include <cassert>
#include <optional>
#include <utility>

namespace clang {

class AnalysisDeclContextManager;
class AnalyzerOptions;
class ASTContext;
class CFGBlock;
class CFGElement;
class ConstructionContext;
class CXXBindTemporaryExpr;
class CXXCatchStmt;
class CXXConstructExpr;
class CXXDeleteExpr;
class CXXNewExpr;
class CXXThisExpr;
class Decl;
class DeclStmt;
class GCCAsmStmt;
class LambdaExpr;
class LocationContext;
class MaterializeTemporaryExpr;
class MSAsmStmt;
class NamedDecl;
class ObjCAtSynchronizedStmt;
class ObjCForCollectionStmt;
class ObjCIvarRefExpr;
class ObjCMessageExpr;
class ReturnStmt;
class Stmt;

namespace cross_tu {

class CrossTranslationUnitContext;

} // namespace cross_tu

namespace ento {

class AnalysisManager;
class BasicValueFactory;
class CallEvent;
class CheckerManager;
class ConstraintManager;
class ExplodedNodeSet;
class ExplodedNode;
class IndirectGotoNodeBuilder;
class MemRegion;
class NodeBuilderContext;
class NodeBuilderWithSinks;
class ProgramState;
class ProgramStateManager;
class RegionAndSymbolInvalidationTraits;
class SymbolManager;
class SwitchNodeBuilder;

/// Hints for figuring out of a call should be inlined during evalCall().
struct EvalCallOptions {
  /// This call is a constructor or a destructor for which we do not currently
  /// compute the this-region correctly.
  bool IsCtorOrDtorWithImproperlyModeledTargetRegion = false;

  /// This call is a constructor or a destructor for a single element within
  /// an array, a part of array construction or destruction.
  bool IsArrayCtorOrDtor = false;

  /// This call is a constructor or a destructor of a temporary value.
  bool IsTemporaryCtorOrDtor = false;

  /// This call is a constructor for a temporary that is lifetime-extended
  /// by binding it to a reference-type field within an aggregate,
  /// for example 'A { const C &c; }; A a = { C() };'
  bool IsTemporaryLifetimeExtendedViaAggregate = false;

  /// This call is a pre-C++17 elidable constructor that we failed to elide
  /// because we failed to compute the target region into which
  /// this constructor would have been ultimately elided. Analysis that
  /// we perform in this case is still correct but it behaves differently,
  /// as if copy elision is disabled.
  bool IsElidableCtorThatHasNotBeenElided = false;

  EvalCallOptions() {}
};

class ExprEngine {
  void anchor();

public:
  /// The modes of inlining, which override the default analysis-wide settings.
  enum InliningModes {
    /// Follow the default settings for inlining callees.
    Inline_Regular = 0,

    /// Do minimal inlining of callees.
    Inline_Minimal = 0x1
  };

private:
  cross_tu::CrossTranslationUnitContext &CTU;
  bool IsCTUEnabled;

  AnalysisManager &AMgr;

  AnalysisDeclContextManager &AnalysisDeclContexts;

  CoreEngine Engine;

  /// G - the simulation graph.
  ExplodedGraph &G;

  /// StateMgr - Object that manages the data for all created states.
  ProgramStateManager StateMgr;

  /// SymMgr - Object that manages the symbol information.
  SymbolManager &SymMgr;

  /// MRMgr - MemRegionManager object that creates memory regions.
  MemRegionManager &MRMgr;

  /// svalBuilder - SValBuilder object that creates SVals from expressions.
  SValBuilder &svalBuilder;

  unsigned int currStmtIdx = 0;
  const NodeBuilderContext *currBldrCtx = nullptr;

  /// Helper object to determine if an Objective-C message expression
  /// implicitly never returns.
  ObjCNoReturn ObjCNoRet;

  /// The BugReporter associated with this engine.  It is important that
  /// this object be placed at the very end of member variables so that its
  /// destructor is called before the rest of the ExprEngine is destroyed.
  PathSensitiveBugReporter BR;

  /// The functions which have been analyzed through inlining. This is owned by
  /// AnalysisConsumer. It can be null.
  SetOfConstDecls *VisitedCallees;

  /// The flag, which specifies the mode of inlining for the engine.
  InliningModes HowToInline;

public:
  ExprEngine(cross_tu::CrossTranslationUnitContext &CTU, AnalysisManager &mgr,
             SetOfConstDecls *VisitedCalleesIn,
             FunctionSummariesTy *FS, InliningModes HowToInlineIn);

  virtual ~ExprEngine() = default;

  /// Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(const LocationContext *L, unsigned Steps = 150000) {
    assert(L->inTopFrame());
    BR.setAnalysisEntryPoint(L->getDecl());
    return Engine.ExecuteWorkList(L, Steps, nullptr);
  }

  /// getContext - Return the ASTContext associated with this analysis.
  ASTContext &getContext() const { return AMgr.getASTContext(); }

  AnalysisManager &getAnalysisManager() { return AMgr; }

  AnalysisDeclContextManager &getAnalysisDeclContextManager() {
    return AMgr.getAnalysisDeclContextManager();
  }

  CheckerManager &getCheckerManager() const {
    return *AMgr.getCheckerManager();
  }

  SValBuilder &getSValBuilder() { return svalBuilder; }

  BugReporter &getBugReporter() { return BR; }

  cross_tu::CrossTranslationUnitContext *
  getCrossTranslationUnitContext() {
    return &CTU;
  }

  const NodeBuilderContext &getBuilderContext() {
    assert(currBldrCtx);
    return *currBldrCtx;
  }

  const Stmt *getStmt() const;

  const LocationContext *getRootLocationContext() const {
    assert(G.roots_begin() != G.roots_end());
    return (*G.roots_begin())->getLocation().getLocationContext();
  }

  CFGBlock::ConstCFGElementRef getCFGElementRef() const {
    const CFGBlock *blockPtr = currBldrCtx ? currBldrCtx->getBlock() : nullptr;
    return {blockPtr, currStmtIdx};
  }

  /// Dump graph to the specified filename.
  /// If filename is empty, generate a temporary one.
  /// \return The filename the graph is written into.
  std::string DumpGraph(bool trim = false, StringRef Filename="");

  /// Dump the graph consisting of the given nodes to a specified filename.
  /// Generate a temporary filename if it's not provided.
  /// \return The filename the graph is written into.
  std::string DumpGraph(ArrayRef<const ExplodedNode *> Nodes,
                        StringRef Filename = "");

  /// Visualize the ExplodedGraph created by executing the simulation.
  void ViewGraph(bool trim = false);

  /// Visualize a trimmed ExplodedGraph that only contains paths to the given
  /// nodes.
  void ViewGraph(ArrayRef<const ExplodedNode *> Nodes);

  /// getInitialState - Return the initial state used for the root vertex
  ///  in the ExplodedGraph.
  ProgramStateRef getInitialState(const LocationContext *InitLoc);

  ExplodedGraph &getGraph() { return G; }
  const ExplodedGraph &getGraph() const { return G; }

  /// Run the analyzer's garbage collection - remove dead symbols and
  /// bindings from the state.
  ///
  /// Checkers can participate in this process with two callbacks:
  /// \c checkLiveSymbols and \c checkDeadSymbols. See the CheckerDocumentation
  /// class for more information.
  ///
  /// \param Node The predecessor node, from which the processing should start.
  /// \param Out The returned set of output nodes.
  /// \param ReferenceStmt The statement which is about to be processed.
  ///        Everything needed for this statement should be considered live.
  ///        A null statement means that everything in child LocationContexts
  ///        is dead.
  /// \param LC The location context of the \p ReferenceStmt. A null location
  ///        context means that we have reached the end of analysis and that
  ///        all statements and local variables should be considered dead.
  /// \param DiagnosticStmt Used as a location for any warnings that should
  ///        occur while removing the dead (e.g. leaks). By default, the
  ///        \p ReferenceStmt is used.
  /// \param K Denotes whether this is a pre- or post-statement purge. This
  ///        must only be ProgramPoint::PostStmtPurgeDeadSymbolsKind if an
  ///        entire location context is being cleared, in which case the
  ///        \p ReferenceStmt must either be a ReturnStmt or \c NULL. Otherwise,
  ///        it must be ProgramPoint::PreStmtPurgeDeadSymbolsKind (the default)
  ///        and \p ReferenceStmt must be valid (non-null).
  void removeDead(ExplodedNode *Node, ExplodedNodeSet &Out,
            const Stmt *ReferenceStmt, const LocationContext *LC,
            const Stmt *DiagnosticStmt = nullptr,
            ProgramPoint::Kind K = ProgramPoint::PreStmtPurgeDeadSymbolsKind);

  /// processCFGElement - Called by CoreEngine. Used to generate new successor
  ///  nodes by processing the 'effects' of a CFG element.
  void processCFGElement(const CFGElement E, ExplodedNode *Pred,
                         unsigned StmtIdx, NodeBuilderContext *Ctx);

  void ProcessStmt(const Stmt *S, ExplodedNode *Pred);

  void ProcessLoopExit(const Stmt* S, ExplodedNode *Pred);

  void ProcessInitializer(const CFGInitializer I, ExplodedNode *Pred);

  void ProcessImplicitDtor(const CFGImplicitDtor D, ExplodedNode *Pred);

  void ProcessNewAllocator(const CXXNewExpr *NE, ExplodedNode *Pred);

  void ProcessAutomaticObjDtor(const CFGAutomaticObjDtor D,
                               ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessDeleteDtor(const CFGDeleteDtor D,
                         ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessBaseDtor(const CFGBaseDtor D,
                       ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessMemberDtor(const CFGMemberDtor D,
                         ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessTemporaryDtor(const CFGTemporaryDtor D,
                            ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Called by CoreEngine when processing the entrance of a CFGBlock.
  void processCFGBlockEntrance(const BlockEdge &L,
                               NodeBuilderWithSinks &nodeBuilder,
                               ExplodedNode *Pred);

  /// ProcessBranch - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a branch condition.
  void processBranch(const Stmt *Condition,
                     NodeBuilderContext& BuilderCtx,
                     ExplodedNode *Pred,
                     ExplodedNodeSet &Dst,
                     const CFGBlock *DstT,
                     const CFGBlock *DstF);

  /// Called by CoreEngine.
  /// Used to generate successor nodes for temporary destructors depending
  /// on whether the corresponding constructor was visited.
  void processCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                     NodeBuilderContext &BldCtx,
                                     ExplodedNode *Pred, ExplodedNodeSet &Dst,
                                     const CFGBlock *DstT,
                                     const CFGBlock *DstF);

  /// Called by CoreEngine.  Used to processing branching behavior
  /// at static initializers.
  void processStaticInitializer(const DeclStmt *DS,
                                NodeBuilderContext& BuilderCtx,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst,
                                const CFGBlock *DstT,
                                const CFGBlock *DstF);

  /// processIndirectGoto - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a computed goto jump.
  void processIndirectGoto(IndirectGotoNodeBuilder& builder);

  /// ProcessSwitch - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a switch statement.
  void processSwitch(SwitchNodeBuilder& builder);

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has begun. Called for both inlined and top-level functions.
  void processBeginOfFunction(NodeBuilderContext &BC,
                              ExplodedNode *Pred, ExplodedNodeSet &Dst,
                              const BlockEdge &L);

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has ended. Called for both inlined and top-level functions.
  void processEndOfFunction(NodeBuilderContext& BC,
                            ExplodedNode *Pred,
                            const ReturnStmt *RS = nullptr);

  /// Remove dead bindings/symbols before exiting a function.
  void removeDeadOnEndOfFunction(NodeBuilderContext& BC,
                                 ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst);

  /// Generate the entry node of the callee.
  void processCallEnter(NodeBuilderContext& BC, CallEnter CE,
                        ExplodedNode *Pred);

  /// Generate the sequence of nodes that simulate the call exit and the post
  /// visit for CallExpr.
  void processCallExit(ExplodedNode *Pred);

  /// Called by CoreEngine when the analysis worklist has terminated.
  void processEndWorklist();

  /// evalAssume - Callback function invoked by the ConstraintManager when
  ///  making assumptions about state values.
  ProgramStateRef processAssume(ProgramStateRef state, SVal cond,
                                bool assumption);

  /// processRegionChanges - Called by ProgramStateManager whenever a change is made
  ///  to the store. Used to update checkers that track region values.
  ProgramStateRef
  processRegionChanges(ProgramStateRef state,
                       const InvalidatedSymbols *invalidated,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call);

  inline ProgramStateRef
  processRegionChange(ProgramStateRef state,
                      const MemRegion* MR,
                      const LocationContext *LCtx) {
    return processRegionChanges(state, nullptr, MR, MR, LCtx, nullptr);
  }

  /// printJson - Called by ProgramStateManager to print checker-specific data.
  void printJson(raw_ostream &Out, ProgramStateRef State,
                 const LocationContext *LCtx, const char *NL,
                 unsigned int Space, bool IsDot) const;

  ProgramStateManager &getStateManager() { return StateMgr; }

  StoreManager &getStoreManager() { return StateMgr.getStoreManager(); }

  ConstraintManager &getConstraintManager() {
    return StateMgr.getConstraintManager();
  }

  // FIXME: Remove when we migrate over to just using SValBuilder.
  BasicValueFactory &getBasicVals() {
    return StateMgr.getBasicVals();
  }

  SymbolManager &getSymbolManager() { return SymMgr; }
  MemRegionManager &getRegionManager() { return MRMgr; }

  DataTag::Factory &getDataTags() { return Engine.getDataTags(); }

  // Functions for external checking of whether we have unfinished work
  bool wasBlocksExhausted() const { return Engine.wasBlocksExhausted(); }
  bool hasEmptyWorkList() const { return !Engine.getWorkList()->hasWork(); }
  bool hasWorkRemaining() const { return Engine.hasWorkRemaining(); }

  const CoreEngine &getCoreEngine() const { return Engine; }

public:
  /// Visit - Transfer function logic for all statements.  Dispatches to
  ///  other functions that handle specific kinds of statements.
  void Visit(const Stmt *S, ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitArrayInitLoopExpr - Transfer function for array init loop.
  void VisitArrayInitLoopExpr(const ArrayInitLoopExpr *Ex, ExplodedNode *Pred,
                              ExplodedNodeSet &Dst);

  /// VisitArraySubscriptExpr - Transfer function for array accesses.
  void VisitArraySubscriptExpr(const ArraySubscriptExpr *Ex,
                               ExplodedNode *Pred,
                               ExplodedNodeSet &Dst);

  /// VisitGCCAsmStmt - Transfer function logic for inline asm.
  void VisitGCCAsmStmt(const GCCAsmStmt *A, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitMSAsmStmt - Transfer function logic for MS inline asm.
  void VisitMSAsmStmt(const MSAsmStmt *A, ExplodedNode *Pred,
                      ExplodedNodeSet &Dst);

  /// VisitBlockExpr - Transfer function logic for BlockExprs.
  void VisitBlockExpr(const BlockExpr *BE, ExplodedNode *Pred,
                      ExplodedNodeSet &Dst);

  /// VisitLambdaExpr - Transfer function logic for LambdaExprs.
  void VisitLambdaExpr(const LambdaExpr *LE, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitBinaryOperator - Transfer function logic for binary operators.
  void VisitBinaryOperator(const BinaryOperator* B, ExplodedNode *Pred,
                           ExplodedNodeSet &Dst);


  /// VisitCall - Transfer function for function calls.
  void VisitCallExpr(const CallExpr *CE, ExplodedNode *Pred,
                     ExplodedNodeSet &Dst);

  /// VisitCast - Transfer function logic for all casts (implicit and explicit).
  void VisitCast(const CastExpr *CastE, const Expr *Ex, ExplodedNode *Pred,
                 ExplodedNodeSet &Dst);

  /// VisitCompoundLiteralExpr - Transfer function logic for compound literals.
  void VisitCompoundLiteralExpr(const CompoundLiteralExpr *CL,
                                ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Transfer function logic for DeclRefExprs and BlockDeclRefExprs.
  void VisitCommonDeclRefExpr(const Expr *DR, const NamedDecl *D,
                              ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitDeclStmt - Transfer function logic for DeclStmts.
  void VisitDeclStmt(const DeclStmt *DS, ExplodedNode *Pred,
                     ExplodedNodeSet &Dst);

  /// VisitGuardedExpr - Transfer function logic for ?, __builtin_choose
  void VisitGuardedExpr(const Expr *Ex, const Expr *L, const Expr *R,
                        ExplodedNode *Pred, ExplodedNodeSet &Dst);

  void VisitInitListExpr(const InitListExpr *E, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  /// VisitLogicalExpr - Transfer function logic for '&&', '||'
  void VisitLogicalExpr(const BinaryOperator* B, ExplodedNode *Pred,
                        ExplodedNodeSet &Dst);

  /// VisitMemberExpr - Transfer function for member expressions.
  void VisitMemberExpr(const MemberExpr *M, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitAtomicExpr - Transfer function for builtin atomic expressions
  void VisitAtomicExpr(const AtomicExpr *E, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// Transfer function logic for ObjCAtSynchronizedStmts.
  void VisitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt *S,
                                   ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Transfer function logic for computing the lvalue of an Objective-C ivar.
  void VisitLvalObjCIvarRefExpr(const ObjCIvarRefExpr *DR, ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  /// VisitObjCForCollectionStmt - Transfer function logic for
  ///  ObjCForCollectionStmt.
  void VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S,
                                  ExplodedNode *Pred, ExplodedNodeSet &Dst);

  void VisitObjCMessage(const ObjCMessageExpr *ME, ExplodedNode *Pred,
                        ExplodedNodeSet &Dst);

  /// VisitReturnStmt - Transfer function logic for return statements.
  void VisitReturnStmt(const ReturnStmt *R, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitOffsetOfExpr - Transfer function for offsetof.
  void VisitOffsetOfExpr(const OffsetOfExpr *Ex, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  /// VisitUnaryExprOrTypeTraitExpr - Transfer function for sizeof.
  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Ex,
                                     ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitUnaryOperator - Transfer function logic for unary operators.
  void VisitUnaryOperator(const UnaryOperator* B, ExplodedNode *Pred,
                          ExplodedNodeSet &Dst);

  /// Handle ++ and -- (both pre- and post-increment).
  void VisitIncrementDecrementOperator(const UnaryOperator* U,
                                       ExplodedNode *Pred,
                                       ExplodedNodeSet &Dst);

  void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *BTE,
                                 ExplodedNodeSet &PreVisit,
                                 ExplodedNodeSet &Dst);

  void VisitCXXCatchStmt(const CXXCatchStmt *CS, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  void VisitCXXThisExpr(const CXXThisExpr *TE, ExplodedNode *Pred,
                        ExplodedNodeSet & Dst);

  void VisitCXXConstructExpr(const CXXConstructExpr *E, ExplodedNode *Pred,
                             ExplodedNodeSet &Dst);

  void VisitCXXInheritedCtorInitExpr(const CXXInheritedCtorInitExpr *E,
                                     ExplodedNode *Pred, ExplodedNodeSet &Dst);

  void VisitCXXDestructor(QualType ObjectType, const MemRegion *Dest,
                          const Stmt *S, bool IsBaseDtor,
                          ExplodedNode *Pred, ExplodedNodeSet &Dst,
                          EvalCallOptions &Options);

  void VisitCXXNewAllocatorCall(const CXXNewExpr *CNE,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  void VisitCXXNewExpr(const CXXNewExpr *CNE, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  void VisitCXXDeleteExpr(const CXXDeleteExpr *CDE, ExplodedNode *Pred,
                          ExplodedNodeSet &Dst);

  /// Create a C++ temporary object for an rvalue.
  void CreateCXXTemporaryObject(const MaterializeTemporaryExpr *ME,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  /// evalEagerlyAssumeBinOpBifurcation - Given the nodes in 'Src', eagerly assume symbolic
  ///  expressions of the form 'x != 0' and generate new nodes (stored in Dst)
  ///  with those assumptions.
  void evalEagerlyAssumeBinOpBifurcation(ExplodedNodeSet &Dst, ExplodedNodeSet &Src,
                         const Expr *Ex);

  static std::pair<const ProgramPointTag *, const ProgramPointTag *>
    geteagerlyAssumeBinOpBifurcationTags();

  ProgramStateRef handleLValueBitCast(ProgramStateRef state, const Expr *Ex,
                                      const LocationContext *LCtx, QualType T,
                                      QualType ExTy, const CastExpr *CastE,
                                      StmtNodeBuilder &Bldr,
                                      ExplodedNode *Pred);

  void handleUOExtension(ExplodedNode *N, const UnaryOperator *U,
                         StmtNodeBuilder &Bldr);

public:
  SVal evalBinOp(ProgramStateRef ST, BinaryOperator::Opcode Op,
                 SVal LHS, SVal RHS, QualType T) {
    return svalBuilder.evalBinOp(ST, Op, LHS, RHS, T);
  }

  /// Retreives which element is being constructed in a non-POD type array.
  static std::optional<unsigned>
  getIndexOfElementToConstruct(ProgramStateRef State, const CXXConstructExpr *E,
                               const LocationContext *LCtx);

  /// Retreives which element is being destructed in a non-POD type array.
  static std::optional<unsigned>
  getPendingArrayDestruction(ProgramStateRef State,
                             const LocationContext *LCtx);

  /// Retreives the size of the array in the pending ArrayInitLoopExpr.
  static std::optional<unsigned>
  getPendingInitLoop(ProgramStateRef State, const CXXConstructExpr *E,
                     const LocationContext *LCtx);

  /// By looking at a certain item that may be potentially part of an object's
  /// ConstructionContext, retrieve such object's location. A particular
  /// statement can be transparently passed as \p Item in most cases.
  static std::optional<SVal>
  getObjectUnderConstruction(ProgramStateRef State,
                             const ConstructionContextItem &Item,
                             const LocationContext *LC);

  /// Call PointerEscape callback when a value escapes as a result of bind.
  ProgramStateRef processPointerEscapedOnBind(
      ProgramStateRef State, ArrayRef<std::pair<SVal, SVal>> LocAndVals,
      const LocationContext *LCtx, PointerEscapeKind Kind,
      const CallEvent *Call);

  /// Call PointerEscape callback when a value escapes as a result of
  /// region invalidation.
  /// \param[in] ITraits Specifies invalidation traits for regions/symbols.
  ProgramStateRef notifyCheckersOfPointerEscape(
                           ProgramStateRef State,
                           const InvalidatedSymbols *Invalidated,
                           ArrayRef<const MemRegion *> ExplicitRegions,
                           const CallEvent *Call,
                           RegionAndSymbolInvalidationTraits &ITraits);

private:
  /// evalBind - Handle the semantics of binding a value to a specific location.
  ///  This method is used by evalStore, VisitDeclStmt, and others.
  void evalBind(ExplodedNodeSet &Dst, const Stmt *StoreE, ExplodedNode *Pred,
                SVal location, SVal Val, bool atDeclInit = false,
                const ProgramPoint *PP = nullptr);

  ProgramStateRef
  processPointerEscapedOnBind(ProgramStateRef State,
                              SVal Loc, SVal Val,
                              const LocationContext *LCtx);

  /// A simple wrapper when you only need to notify checkers of pointer-escape
  /// of some values.
  ProgramStateRef escapeValues(ProgramStateRef State, ArrayRef<SVal> Vs,
                               PointerEscapeKind K,
                               const CallEvent *Call = nullptr) const;

public:
  // FIXME: 'tag' should be removed, and a LocationContext should be used
  // instead.
  // FIXME: Comment on the meaning of the arguments, when 'St' may not
  // be the same as Pred->state, and when 'location' may not be the
  // same as state->getLValue(Ex).
  /// Simulate a read of the result of Ex.
  void evalLoad(ExplodedNodeSet &Dst,
                const Expr *NodeEx,  /* Eventually will be a CFGStmt */
                const Expr *BoundExpr,
                ExplodedNode *Pred,
                ProgramStateRef St,
                SVal location,
                const ProgramPointTag *tag = nullptr,
                QualType LoadTy = QualType());

  // FIXME: 'tag' should be removed, and a LocationContext should be used
  // instead.
  void evalStore(ExplodedNodeSet &Dst, const Expr *AssignE, const Expr *StoreE,
                 ExplodedNode *Pred, ProgramStateRef St, SVal TargetLV, SVal Val,
                 const ProgramPointTag *tag = nullptr);

  /// Return the CFG element corresponding to the worklist element
  /// that is currently being processed by ExprEngine.
  CFGElement getCurrentCFGElement() {
    return (*currBldrCtx->getBlock())[currStmtIdx];
  }

  /// Create a new state in which the call return value is binded to the
  /// call origin expression.
  ProgramStateRef bindReturnValue(const CallEvent &Call,
                                  const LocationContext *LCtx,
                                  ProgramStateRef State);

  /// Evaluate a call, running pre- and post-call checkers and allowing checkers
  /// to be responsible for handling the evaluation of the call itself.
  void evalCall(ExplodedNodeSet &Dst, ExplodedNode *Pred,
                const CallEvent &Call);

  /// Default implementation of call evaluation.
  void defaultEvalCall(NodeBuilder &B, ExplodedNode *Pred,
                       const CallEvent &Call,
                       const EvalCallOptions &CallOpts = {});

  /// Find location of the object that is being constructed by a given
  /// constructor. This should ideally always succeed but due to not being
  /// fully implemented it sometimes indicates that it failed via its
  /// out-parameter CallOpts; in such cases a fake temporary region is
  /// returned, which is better than nothing but does not represent
  /// the actual behavior of the program. The Idx parameter is used if we
  /// construct an array of objects. In that case it points to the index
  /// of the continuous memory region.
  /// E.g.:
  /// For `int arr[4]` this index can be 0,1,2,3.
  /// For `int arr2[3][3]` this index can be 0,1,...,7,8.
  /// A multi-dimensional array is also a continuous memory location in a
  /// row major order, so for arr[0][0] Idx is 0 and for arr[2][2] Idx is 8.
  SVal computeObjectUnderConstruction(const Expr *E, ProgramStateRef State,
                                      const NodeBuilderContext *BldrCtx,
                                      const LocationContext *LCtx,
                                      const ConstructionContext *CC,
                                      EvalCallOptions &CallOpts,
                                      unsigned Idx = 0);

  /// Update the program state with all the path-sensitive information
  /// that's necessary to perform construction of an object with a given
  /// syntactic construction context. V and CallOpts have to be obtained from
  /// computeObjectUnderConstruction() invoked with the same set of
  /// the remaining arguments (E, State, LCtx, CC).
  ProgramStateRef updateObjectsUnderConstruction(
      SVal V, const Expr *E, ProgramStateRef State, const LocationContext *LCtx,
      const ConstructionContext *CC, const EvalCallOptions &CallOpts);

  /// A convenient wrapper around computeObjectUnderConstruction
  /// and updateObjectsUnderConstruction.
  std::pair<ProgramStateRef, SVal> handleConstructionContext(
      const Expr *E, ProgramStateRef State, const NodeBuilderContext *BldrCtx,
      const LocationContext *LCtx, const ConstructionContext *CC,
      EvalCallOptions &CallOpts, unsigned Idx = 0) {

    SVal V = computeObjectUnderConstruction(E, State, BldrCtx, LCtx, CC,
                                            CallOpts, Idx);
    State = updateObjectsUnderConstruction(V, E, State, LCtx, CC, CallOpts);

    return std::make_pair(State, V);
  }

private:
  ProgramStateRef finishArgumentConstruction(ProgramStateRef State,
                                             const CallEvent &Call);
  void finishArgumentConstruction(ExplodedNodeSet &Dst, ExplodedNode *Pred,
                                  const CallEvent &Call);

  void evalLocation(ExplodedNodeSet &Dst,
                    const Stmt *NodeEx, /* This will eventually be a CFGStmt */
                    const Stmt *BoundEx,
                    ExplodedNode *Pred,
                    ProgramStateRef St,
                    SVal location,
                    bool isLoad);

  /// Count the stack depth and determine if the call is recursive.
  void examineStackFrames(const Decl *D, const LocationContext *LCtx,
                          bool &IsRecursive, unsigned &StackDepth);

  enum CallInlinePolicy {
    CIP_Allowed,
    CIP_DisallowedOnce,
    CIP_DisallowedAlways
  };

  /// See if a particular call should be inlined, by only looking
  /// at the call event and the current state of analysis.
  CallInlinePolicy mayInlineCallKind(const CallEvent &Call,
                                     const ExplodedNode *Pred,
                                     AnalyzerOptions &Opts,
                                     const EvalCallOptions &CallOpts);

  /// See if the given AnalysisDeclContext is built for a function that we
  /// should always inline simply because it's small enough.
  /// Apart from "small" functions, we also have "large" functions
  /// (cf. isLarge()), some of which are huge (cf. isHuge()), and we classify
  /// the remaining functions as "medium".
  bool isSmall(AnalysisDeclContext *ADC) const;

  /// See if the given AnalysisDeclContext is built for a function that we
  /// should inline carefully because it looks pretty large.
  bool isLarge(AnalysisDeclContext *ADC) const;

  /// See if the given AnalysisDeclContext is built for a function that we
  /// should never inline because it's legit gigantic.
  bool isHuge(AnalysisDeclContext *ADC) const;

  /// See if the given AnalysisDeclContext is built for a function that we
  /// should inline, just by looking at the declaration of the function.
  bool mayInlineDecl(AnalysisDeclContext *ADC) const;

  /// Checks our policies and decides weither the given call should be inlined.
  bool shouldInlineCall(const CallEvent &Call, const Decl *D,
                        const ExplodedNode *Pred,
                        const EvalCallOptions &CallOpts = {});

  /// Checks whether our policies allow us to inline a non-POD type array
  /// construction.
  bool shouldInlineArrayConstruction(const ProgramStateRef State,
                                     const CXXConstructExpr *CE,
                                     const LocationContext *LCtx);

  /// Checks whether our policies allow us to inline a non-POD type array
  /// destruction.
  /// \param Size The size of the array.
  bool shouldInlineArrayDestruction(uint64_t Size);

  /// Prepares the program state for array destruction. If no error happens
  /// the function binds a 'PendingArrayDestruction' entry to the state, which
  /// it returns along with the index. If any error happens (we fail to read
  /// the size, the index would be -1, etc.) the function will return the
  /// original state along with an index of 0. The actual element count of the
  /// array can be accessed by the optional 'ElementCountVal' parameter. \param
  /// State The program state. \param Region The memory region where the array
  /// is stored. \param ElementTy The type an element in the array. \param LCty
  /// The location context. \param ElementCountVal A pointer to an optional
  /// SVal. If specified, the size of the array will be returned in it. It can
  /// be Unknown.
  std::pair<ProgramStateRef, uint64_t> prepareStateForArrayDestruction(
      const ProgramStateRef State, const MemRegion *Region,
      const QualType &ElementTy, const LocationContext *LCtx,
      SVal *ElementCountVal = nullptr);

  /// Checks whether we construct an array of non-POD type, and decides if the
  /// constructor should be inkoved once again.
  bool shouldRepeatCtorCall(ProgramStateRef State, const CXXConstructExpr *E,
                            const LocationContext *LCtx);

  void inlineCall(WorkList *WList, const CallEvent &Call, const Decl *D,
                  NodeBuilder &Bldr, ExplodedNode *Pred, ProgramStateRef State);

  void ctuBifurcate(const CallEvent &Call, const Decl *D, NodeBuilder &Bldr,
                    ExplodedNode *Pred, ProgramStateRef State);

  /// Returns true if the CTU analysis is running its second phase.
  bool isSecondPhaseCTU() { return IsCTUEnabled && !Engine.getCTUWorkList(); }

  /// Conservatively evaluate call by invalidating regions and binding
  /// a conjured return value.
  void conservativeEvalCall(const CallEvent &Call, NodeBuilder &Bldr,
                            ExplodedNode *Pred, ProgramStateRef State);

  /// Either inline or process the call conservatively (or both), based
  /// on DynamicDispatchBifurcation data.
  void BifurcateCall(const MemRegion *BifurReg,
                     const CallEvent &Call, const Decl *D, NodeBuilder &Bldr,
                     ExplodedNode *Pred);

  bool replayWithoutInlining(ExplodedNode *P, const LocationContext *CalleeLC);

  /// Models a trivial copy or move constructor or trivial assignment operator
  /// call with a simple bind.
  void performTrivialCopy(NodeBuilder &Bldr, ExplodedNode *Pred,
                          const CallEvent &Call);

  /// If the value of the given expression \p InitWithAdjustments is a NonLoc,
  /// copy it into a new temporary object region, and replace the value of the
  /// expression with that.
  ///
  /// If \p Result is provided, the new region will be bound to this expression
  /// instead of \p InitWithAdjustments.
  ///
  /// Returns the temporary region with adjustments into the optional
  /// OutRegionWithAdjustments out-parameter if a new region was indeed needed,
  /// otherwise sets it to nullptr.
  ProgramStateRef createTemporaryRegionIfNeeded(
      ProgramStateRef State, const LocationContext *LC,
      const Expr *InitWithAdjustments, const Expr *Result = nullptr,
      const SubRegion **OutRegionWithAdjustments = nullptr);

  /// Returns a region representing the `Idx`th element of a (possibly
  /// multi-dimensional) array, for the purposes of element construction or
  /// destruction.
  ///
  /// On return, \p Ty will be set to the base type of the array.
  ///
  /// If the type is not an array type at all, the original value is returned.
  /// Otherwise the "IsArray" flag is set.
  static SVal makeElementRegion(ProgramStateRef State, SVal LValue,
                                QualType &Ty, bool &IsArray, unsigned Idx = 0);

  /// Common code that handles either a CXXConstructExpr or a
  /// CXXInheritedCtorInitExpr.
  void handleConstructor(const Expr *E, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

public:
  /// Note whether this loop has any more iteratios to model. These methods are
  /// essentially an interface for a GDM trait. Further reading in
  /// ExprEngine::VisitObjCForCollectionStmt().
  [[nodiscard]] static ProgramStateRef
  setWhetherHasMoreIteration(ProgramStateRef State,
                             const ObjCForCollectionStmt *O,
                             const LocationContext *LC, bool HasMoreIteraton);

  [[nodiscard]] static ProgramStateRef
  removeIterationState(ProgramStateRef State, const ObjCForCollectionStmt *O,
                       const LocationContext *LC);

  [[nodiscard]] static bool hasMoreIteration(ProgramStateRef State,
                                             const ObjCForCollectionStmt *O,
                                             const LocationContext *LC);

private:
  /// Assuming we construct an array of non-POD types, this method allows us
  /// to store which element is to be constructed next.
  static ProgramStateRef
  setIndexOfElementToConstruct(ProgramStateRef State, const CXXConstructExpr *E,
                               const LocationContext *LCtx, unsigned Idx);

  static ProgramStateRef
  removeIndexOfElementToConstruct(ProgramStateRef State,
                                  const CXXConstructExpr *E,
                                  const LocationContext *LCtx);

  /// Assuming we destruct an array of non-POD types, this method allows us
  /// to store which element is to be destructed next.
  static ProgramStateRef setPendingArrayDestruction(ProgramStateRef State,
                                                    const LocationContext *LCtx,
                                                    unsigned Idx);

  static ProgramStateRef
  removePendingArrayDestruction(ProgramStateRef State,
                                const LocationContext *LCtx);

  /// Sets the size of the array in a pending ArrayInitLoopExpr.
  static ProgramStateRef setPendingInitLoop(ProgramStateRef State,
                                            const CXXConstructExpr *E,
                                            const LocationContext *LCtx,
                                            unsigned Idx);

  static ProgramStateRef removePendingInitLoop(ProgramStateRef State,
                                               const CXXConstructExpr *E,
                                               const LocationContext *LCtx);

  static ProgramStateRef
  removeStateTraitsUsedForArrayEvaluation(ProgramStateRef State,
                                          const CXXConstructExpr *E,
                                          const LocationContext *LCtx);

  /// Store the location of a C++ object corresponding to a statement
  /// until the statement is actually encountered. For example, if a DeclStmt
  /// has CXXConstructExpr as its initializer, the object would be considered
  /// to be "under construction" between CXXConstructExpr and DeclStmt.
  /// This allows, among other things, to keep bindings to variable's fields
  /// made within the constructor alive until its declaration actually
  /// goes into scope.
  static ProgramStateRef
  addObjectUnderConstruction(ProgramStateRef State,
                             const ConstructionContextItem &Item,
                             const LocationContext *LC, SVal V);

  /// Mark the object sa fully constructed, cleaning up the state trait
  /// that tracks objects under construction.
  static ProgramStateRef
  finishObjectConstruction(ProgramStateRef State,
                           const ConstructionContextItem &Item,
                           const LocationContext *LC);

  /// If the given expression corresponds to a temporary that was used for
  /// passing into an elidable copy/move constructor and that constructor
  /// was actually elided, track that we also need to elide the destructor.
  static ProgramStateRef elideDestructor(ProgramStateRef State,
                                         const CXXBindTemporaryExpr *BTE,
                                         const LocationContext *LC);

  /// Stop tracking the destructor that corresponds to an elided constructor.
  static ProgramStateRef
  cleanupElidedDestructor(ProgramStateRef State,
                          const CXXBindTemporaryExpr *BTE,
                          const LocationContext *LC);

  /// Returns true if the given expression corresponds to a temporary that
  /// was constructed for passing into an elidable copy/move constructor
  /// and that constructor was actually elided.
  static bool isDestructorElided(ProgramStateRef State,
                                 const CXXBindTemporaryExpr *BTE,
                                 const LocationContext *LC);

  /// Check if all objects under construction have been fully constructed
  /// for the given context range (including FromLC, not including ToLC).
  /// This is useful for assertions. Also checks if elided destructors
  /// were cleaned up.
  static bool areAllObjectsFullyConstructed(ProgramStateRef State,
                                            const LocationContext *FromLC,
                                            const LocationContext *ToLC);
};

/// Traits for storing the call processing policy inside GDM.
/// The GDM stores the corresponding CallExpr pointer.
// FIXME: This does not use the nice trait macros because it must be accessible
// from multiple translation units.
struct ReplayWithoutInlining{};
template <>
struct ProgramStateTrait<ReplayWithoutInlining> :
  public ProgramStatePartialTrait<const void*> {
  static void *GDMIndex();
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H
