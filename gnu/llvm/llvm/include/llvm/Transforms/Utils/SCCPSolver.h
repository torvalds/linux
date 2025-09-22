//===- SCCPSolver.h - SCCP Utility ----------------------------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements Sparse Conditional Constant Propagation (SCCP) utility.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H
#define LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Transforms/Utils/PredicateInfo.h"
#include <vector>

namespace llvm {
class Argument;
class BasicBlock;
class CallInst;
class Constant;
class DataLayout;
class DominatorTree;
class Function;
class GlobalVariable;
class Instruction;
class LLVMContext;
class StructType;
class TargetLibraryInfo;
class Value;
class ValueLatticeElement;

/// Helper struct shared between Function Specialization and SCCP Solver.
struct ArgInfo {
  Argument *Formal; // The Formal argument being analysed.
  Constant *Actual; // A corresponding actual constant argument.

  ArgInfo(Argument *F, Constant *A) : Formal(F), Actual(A) {}

  bool operator==(const ArgInfo &Other) const {
    return Formal == Other.Formal && Actual == Other.Actual;
  }

  bool operator!=(const ArgInfo &Other) const { return !(*this == Other); }

  friend hash_code hash_value(const ArgInfo &A) {
    return hash_combine(hash_value(A.Formal), hash_value(A.Actual));
  }
};

class SCCPInstVisitor;

//===----------------------------------------------------------------------===//
//
/// SCCPSolver - This interface class is a general purpose solver for Sparse
/// Conditional Constant Propagation (SCCP).
///
class SCCPSolver {
  std::unique_ptr<SCCPInstVisitor> Visitor;

public:
  SCCPSolver(const DataLayout &DL,
             std::function<const TargetLibraryInfo &(Function &)> GetTLI,
             LLVMContext &Ctx);

  ~SCCPSolver();

  void addPredicateInfo(Function &F, DominatorTree &DT, AssumptionCache &AC);

  /// markBlockExecutable - This method can be used by clients to mark all of
  /// the blocks that are known to be intrinsically live in the processed unit.
  /// This returns true if the block was not considered live before.
  bool markBlockExecutable(BasicBlock *BB);

  const PredicateBase *getPredicateInfoFor(Instruction *I);

  /// trackValueOfGlobalVariable - Clients can use this method to
  /// inform the SCCPSolver that it should track loads and stores to the
  /// specified global variable if it can.  This is only legal to call if
  /// performing Interprocedural SCCP.
  void trackValueOfGlobalVariable(GlobalVariable *GV);

  /// addTrackedFunction - If the SCCP solver is supposed to track calls into
  /// and out of the specified function (which cannot have its address taken),
  /// this method must be called.
  void addTrackedFunction(Function *F);

  /// Add function to the list of functions whose return cannot be modified.
  void addToMustPreserveReturnsInFunctions(Function *F);

  /// Returns true if the return of the given function cannot be modified.
  bool mustPreserveReturn(Function *F);

  void addArgumentTrackedFunction(Function *F);

  /// Returns true if the given function is in the solver's set of
  /// argument-tracked functions.
  bool isArgumentTrackedFunction(Function *F);

  /// Solve - Solve for constants and executable blocks.
  void solve();

  /// resolvedUndefsIn - While solving the dataflow for a function, we assume
  /// that branches on undef values cannot reach any of their successors.
  /// However, this is not a safe assumption.  After we solve dataflow, this
  /// method should be use to handle this.  If this returns true, the solver
  /// should be rerun.
  bool resolvedUndefsIn(Function &F);

  void solveWhileResolvedUndefsIn(Module &M);

  void solveWhileResolvedUndefsIn(SmallVectorImpl<Function *> &WorkList);

  void solveWhileResolvedUndefs();

  bool isBlockExecutable(BasicBlock *BB) const;

  // isEdgeFeasible - Return true if the control flow edge from the 'From' basic
  // block to the 'To' basic block is currently feasible.
  bool isEdgeFeasible(BasicBlock *From, BasicBlock *To) const;

  std::vector<ValueLatticeElement> getStructLatticeValueFor(Value *V) const;

  void removeLatticeValueFor(Value *V);

  /// Invalidate the Lattice Value of \p Call and its users after specializing
  /// the call. Then recompute it.
  void resetLatticeValueFor(CallBase *Call);

  const ValueLatticeElement &getLatticeValueFor(Value *V) const;

  /// getTrackedRetVals - Get the inferred return value map.
  const MapVector<Function *, ValueLatticeElement> &getTrackedRetVals();

  /// getTrackedGlobals - Get and return the set of inferred initializers for
  /// global variables.
  const DenseMap<GlobalVariable *, ValueLatticeElement> &getTrackedGlobals();

  /// getMRVFunctionsTracked - Get the set of functions which return multiple
  /// values tracked by the pass.
  const SmallPtrSet<Function *, 16> getMRVFunctionsTracked();

  /// markOverdefined - Mark the specified value overdefined.  This
  /// works with both scalars and structs.
  void markOverdefined(Value *V);

  /// trackValueOfArgument - Mark the specified argument overdefined unless it
  /// have range attribute.  This works with both scalars and structs.
  void trackValueOfArgument(Argument *V);

  // isStructLatticeConstant - Return true if all the lattice values
  // corresponding to elements of the structure are constants,
  // false otherwise.
  bool isStructLatticeConstant(Function *F, StructType *STy);

  /// Helper to return a Constant if \p LV is either a constant or a constant
  /// range with a single element.
  Constant *getConstant(const ValueLatticeElement &LV, Type *Ty) const;

  /// Return either a Constant or nullptr for a given Value.
  Constant *getConstantOrNull(Value *V) const;

  /// Return a reference to the set of argument tracked functions.
  SmallPtrSetImpl<Function *> &getArgumentTrackedFunctions();

  /// Set the Lattice Value for the arguments of a specialization \p F.
  /// If an argument is Constant then its lattice value is marked with the
  /// corresponding actual argument in \p Args. Otherwise, its lattice value
  /// is inherited (copied) from the corresponding formal argument in \p Args.
  void setLatticeValueForSpecializationArguments(Function *F,
                                       const SmallVectorImpl<ArgInfo> &Args);

  /// Mark all of the blocks in function \p F non-executable. Clients can used
  /// this method to erase a function from the module (e.g., if it has been
  /// completely specialized and is no longer needed).
  void markFunctionUnreachable(Function *F);

  void visit(Instruction *I);
  void visitCall(CallInst &I);

  bool simplifyInstsInBlock(BasicBlock &BB,
                            SmallPtrSetImpl<Value *> &InsertedValues,
                            Statistic &InstRemovedStat,
                            Statistic &InstReplacedStat);

  bool removeNonFeasibleEdges(BasicBlock *BB, DomTreeUpdater &DTU,
                              BasicBlock *&NewUnreachableBB) const;

  bool tryToReplaceWithConstant(Value *V);

  // Helper to check if \p LV is either a constant or a constant
  // range with a single element. This should cover exactly the same cases as
  // the old ValueLatticeElement::isConstant() and is intended to be used in the
  // transition to ValueLatticeElement.
  static bool isConstant(const ValueLatticeElement &LV);

  // Helper to check if \p LV is either overdefined or a constant range with
  // more than a single element. This should cover exactly the same cases as the
  // old ValueLatticeElement::isOverdefined() and is intended to be used in the
  // transition to ValueLatticeElement.
  static bool isOverdefined(const ValueLatticeElement &LV);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SCCPSOLVER_H
