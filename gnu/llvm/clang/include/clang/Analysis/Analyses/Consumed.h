//===- Consumed.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A intra-procedural analysis for checking consumed properties.  This is based,
// in part, on research on linear types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_CONSUMED_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_CONSUMED_H

#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace clang {

class AnalysisDeclContext;
class CXXBindTemporaryExpr;
class FunctionDecl;
class PostOrderCFGView;
class Stmt;
class VarDecl;

namespace consumed {

  class ConsumedStmtVisitor;

  enum ConsumedState {
    // No state information for the given variable.
    CS_None,

    CS_Unknown,
    CS_Unconsumed,
    CS_Consumed
  };

  using OptionalNotes = SmallVector<PartialDiagnosticAt, 1>;
  using DelayedDiag = std::pair<PartialDiagnosticAt, OptionalNotes>;
  using DiagList = std::list<DelayedDiag>;

  class ConsumedWarningsHandlerBase {
  public:
    virtual ~ConsumedWarningsHandlerBase();

    /// Emit the warnings and notes left by the analysis.
    virtual void emitDiagnostics() {}

    /// Warn that a variable's state doesn't match at the entry and exit
    /// of a loop.
    ///
    /// \param Loc -- The location of the end of the loop.
    ///
    /// \param VariableName -- The name of the variable that has a mismatched
    /// state.
    virtual void warnLoopStateMismatch(SourceLocation Loc,
                                       StringRef VariableName) {}

    /// Warn about parameter typestate mismatches upon return.
    ///
    /// \param Loc -- The SourceLocation of the return statement.
    ///
    /// \param ExpectedState -- The state the return value was expected to be
    /// in.
    ///
    /// \param ObservedState -- The state the return value was observed to be
    /// in.
    virtual void warnParamReturnTypestateMismatch(SourceLocation Loc,
                                                  StringRef VariableName,
                                                  StringRef ExpectedState,
                                                  StringRef ObservedState) {}

    // FIXME: Add documentation.
    virtual void warnParamTypestateMismatch(SourceLocation LOC,
                                            StringRef ExpectedState,
                                            StringRef ObservedState) {}

    // FIXME: This can be removed when the attr propagation fix for templated
    //        classes lands.
    /// Warn about return typestates set for unconsumable types.
    ///
    /// \param Loc -- The location of the attributes.
    ///
    /// \param TypeName -- The name of the unconsumable type.
    virtual void warnReturnTypestateForUnconsumableType(SourceLocation Loc,
                                                        StringRef TypeName) {}

    /// Warn about return typestate mismatches.
    ///
    /// \param Loc -- The SourceLocation of the return statement.
    ///
    /// \param ExpectedState -- The state the return value was expected to be
    /// in.
    ///
    /// \param ObservedState -- The state the return value was observed to be
    /// in.
    virtual void warnReturnTypestateMismatch(SourceLocation Loc,
                                             StringRef ExpectedState,
                                             StringRef ObservedState) {}

    /// Warn about use-while-consumed errors.
    /// \param MethodName -- The name of the method that was incorrectly
    /// invoked.
    ///
    /// \param State -- The state the object was used in.
    ///
    /// \param Loc -- The SourceLocation of the method invocation.
    virtual void warnUseOfTempInInvalidState(StringRef MethodName,
                                             StringRef State,
                                             SourceLocation Loc) {}

    /// Warn about use-while-consumed errors.
    /// \param MethodName -- The name of the method that was incorrectly
    /// invoked.
    ///
    /// \param State -- The state the object was used in.
    ///
    /// \param VariableName -- The name of the variable that holds the unique
    /// value.
    ///
    /// \param Loc -- The SourceLocation of the method invocation.
    virtual void warnUseInInvalidState(StringRef MethodName,
                                       StringRef VariableName,
                                       StringRef State,
                                       SourceLocation Loc) {}
  };

  class ConsumedStateMap {
    using VarMapType = llvm::DenseMap<const VarDecl *, ConsumedState>;
    using TmpMapType =
        llvm::DenseMap<const CXXBindTemporaryExpr *, ConsumedState>;

  protected:
    bool Reachable = true;
    const Stmt *From = nullptr;
    VarMapType VarMap;
    TmpMapType TmpMap;

  public:
    ConsumedStateMap() = default;
    ConsumedStateMap(const ConsumedStateMap &Other)
        : Reachable(Other.Reachable), From(Other.From), VarMap(Other.VarMap) {}

    // The copy assignment operator is defined as deleted pending further
    // motivation.
    ConsumedStateMap &operator=(const ConsumedStateMap &) = delete;

    /// Warn if any of the parameters being tracked are not in the state
    /// they were declared to be in upon return from a function.
    void checkParamsForReturnTypestate(SourceLocation BlameLoc,
      ConsumedWarningsHandlerBase &WarningsHandler) const;

    /// Clear the TmpMap.
    void clearTemporaries();

    /// Get the consumed state of a given variable.
    ConsumedState getState(const VarDecl *Var) const;

    /// Get the consumed state of a given temporary value.
    ConsumedState getState(const CXXBindTemporaryExpr *Tmp) const;

    /// Merge this state map with another map.
    void intersect(const ConsumedStateMap &Other);

    void intersectAtLoopHead(const CFGBlock *LoopHead, const CFGBlock *LoopBack,
      const ConsumedStateMap *LoopBackStates,
      ConsumedWarningsHandlerBase &WarningsHandler);

    /// Return true if this block is reachable.
    bool isReachable() const { return Reachable; }

    /// Mark the block as unreachable.
    void markUnreachable();

    /// Set the source for a decision about the branching of states.
    /// \param Source -- The statement that was the origin of a branching
    /// decision.
    void setSource(const Stmt *Source) { this->From = Source; }

    /// Set the consumed state of a given variable.
    void setState(const VarDecl *Var, ConsumedState State);

    /// Set the consumed state of a given temporary value.
    void setState(const CXXBindTemporaryExpr *Tmp, ConsumedState State);

    /// Remove the temporary value from our state map.
    void remove(const CXXBindTemporaryExpr *Tmp);

    /// Tests to see if there is a mismatch in the states stored in two
    /// maps.
    ///
    /// \param Other -- The second map to compare against.
    bool operator!=(const ConsumedStateMap *Other) const;
  };

  class ConsumedBlockInfo {
    std::vector<std::unique_ptr<ConsumedStateMap>> StateMapsArray;
    std::vector<unsigned int> VisitOrder;

  public:
    ConsumedBlockInfo() = default;

    ConsumedBlockInfo(unsigned int NumBlocks, PostOrderCFGView *SortedGraph)
        : StateMapsArray(NumBlocks), VisitOrder(NumBlocks, 0) {
      unsigned int VisitOrderCounter = 0;
      for (const auto BI : *SortedGraph)
        VisitOrder[BI->getBlockID()] = VisitOrderCounter++;
    }

    bool allBackEdgesVisited(const CFGBlock *CurrBlock,
                             const CFGBlock *TargetBlock);

    void addInfo(const CFGBlock *Block, ConsumedStateMap *StateMap,
                 std::unique_ptr<ConsumedStateMap> &OwnedStateMap);
    void addInfo(const CFGBlock *Block,
                 std::unique_ptr<ConsumedStateMap> StateMap);

    ConsumedStateMap* borrowInfo(const CFGBlock *Block);

    void discardInfo(const CFGBlock *Block);

    std::unique_ptr<ConsumedStateMap> getInfo(const CFGBlock *Block);

    bool isBackEdge(const CFGBlock *From, const CFGBlock *To);
    bool isBackEdgeTarget(const CFGBlock *Block);
  };

  /// A class that handles the analysis of uniqueness violations.
  class ConsumedAnalyzer {
    ConsumedBlockInfo BlockInfo;
    std::unique_ptr<ConsumedStateMap> CurrStates;

    ConsumedState ExpectedReturnState = CS_None;

    void determineExpectedReturnState(AnalysisDeclContext &AC,
                                      const FunctionDecl *D);
    bool splitState(const CFGBlock *CurrBlock,
                    const ConsumedStmtVisitor &Visitor);

  public:
    ConsumedWarningsHandlerBase &WarningsHandler;

    ConsumedAnalyzer(ConsumedWarningsHandlerBase &WarningsHandler)
        : WarningsHandler(WarningsHandler) {}

    ConsumedState getExpectedReturnState() const { return ExpectedReturnState; }

    /// Check a function's CFG for consumed violations.
    ///
    /// We traverse the blocks in the CFG, keeping track of the state of each
    /// value who's type has uniqueness annotations.  If methods are invoked in
    /// the wrong state a warning is issued.  Each block in the CFG is traversed
    /// exactly once.
    void run(AnalysisDeclContext &AC);
  };

} // namespace consumed

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_CONSUMED_H
