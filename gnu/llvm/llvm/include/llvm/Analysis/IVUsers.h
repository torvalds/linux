//===- llvm/Analysis/IVUsers.h - Induction Variable Users -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements bookkeeping for "interesting" users of expressions
// computed from induction variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IVUSERS_H
#define LLVM_ANALYSIS_IVUSERS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class ScalarEvolution;
class SCEV;
class IVUsers;

/// IVStrideUse - Keep track of one use of a strided induction variable.
/// The Expr member keeps track of the expression, User is the actual user
/// instruction of the operand, and 'OperandValToReplace' is the operand of
/// the User that is the use.
class IVStrideUse final : public CallbackVH, public ilist_node<IVStrideUse> {
  friend class IVUsers;
public:
  IVStrideUse(IVUsers *P, Instruction* U, Value *O)
    : CallbackVH(U), Parent(P), OperandValToReplace(O) {
  }

  /// getUser - Return the user instruction for this use.
  Instruction *getUser() const {
    return cast<Instruction>(getValPtr());
  }

  /// setUser - Assign a new user instruction for this use.
  void setUser(Instruction *NewUser) {
    setValPtr(NewUser);
  }

  /// getOperandValToReplace - Return the Value of the operand in the user
  /// instruction that this IVStrideUse is representing.
  Value *getOperandValToReplace() const {
    return OperandValToReplace;
  }

  /// setOperandValToReplace - Assign a new Value as the operand value
  /// to replace.
  void setOperandValToReplace(Value *Op) {
    OperandValToReplace = Op;
  }

  /// getPostIncLoops - Return the set of loops for which the expression has
  /// been adjusted to use post-inc mode.
  const PostIncLoopSet &getPostIncLoops() const {
    return PostIncLoops;
  }

  /// transformToPostInc - Transform the expression to post-inc form for the
  /// given loop.
  void transformToPostInc(const Loop *L);

private:
  /// Parent - a pointer to the IVUsers that owns this IVStrideUse.
  IVUsers *Parent;

  /// OperandValToReplace - The Value of the operand in the user instruction
  /// that this IVStrideUse is representing.
  WeakTrackingVH OperandValToReplace;

  /// PostIncLoops - The set of loops for which Expr has been adjusted to
  /// use post-inc mode. This corresponds with SCEVExpander's post-inc concept.
  PostIncLoopSet PostIncLoops;

  /// Deleted - Implementation of CallbackVH virtual function to
  /// receive notification when the User is deleted.
  void deleted() override;
};

class IVUsers {
  friend class IVStrideUse;
  Loop *L;
  AssumptionCache *AC;
  LoopInfo *LI;
  DominatorTree *DT;
  ScalarEvolution *SE;
  SmallPtrSet<Instruction*, 16> Processed;

  /// IVUses - A list of all tracked IV uses of induction variable expressions
  /// we are interested in.
  ilist<IVStrideUse> IVUses;

  // Ephemeral values used by @llvm.assume in this function.
  SmallPtrSet<const Value *, 32> EphValues;

public:
  IVUsers(Loop *L, AssumptionCache *AC, LoopInfo *LI, DominatorTree *DT,
          ScalarEvolution *SE);

  IVUsers(IVUsers &&X)
      : L(std::move(X.L)), AC(std::move(X.AC)), DT(std::move(X.DT)),
        SE(std::move(X.SE)), Processed(std::move(X.Processed)),
        IVUses(std::move(X.IVUses)), EphValues(std::move(X.EphValues)) {
    for (IVStrideUse &U : IVUses)
      U.Parent = this;
  }
  IVUsers(const IVUsers &) = delete;
  IVUsers &operator=(IVUsers &&) = delete;
  IVUsers &operator=(const IVUsers &) = delete;

  Loop *getLoop() const { return L; }

  /// AddUsersIfInteresting - Inspect the specified Instruction.  If it is a
  /// reducible SCEV, recursively add its users to the IVUsesByStride set and
  /// return true.  Otherwise, return false.
  bool AddUsersIfInteresting(Instruction *I);

  IVStrideUse &AddUser(Instruction *User, Value *Operand);

  /// getReplacementExpr - Return a SCEV expression which computes the
  /// value of the OperandValToReplace of the given IVStrideUse.
  const SCEV *getReplacementExpr(const IVStrideUse &IU) const;

  /// getExpr - Return the expression for the use. Returns nullptr if the result
  /// is not invertible.
  const SCEV *getExpr(const IVStrideUse &IU) const;

  const SCEV *getStride(const IVStrideUse &IU, const Loop *L) const;

  typedef ilist<IVStrideUse>::iterator iterator;
  typedef ilist<IVStrideUse>::const_iterator const_iterator;
  iterator begin() { return IVUses.begin(); }
  iterator end()   { return IVUses.end(); }
  const_iterator begin() const { return IVUses.begin(); }
  const_iterator end() const   { return IVUses.end(); }
  bool empty() const { return IVUses.empty(); }

  bool isIVUserOrOperand(Instruction *Inst) const {
    return Processed.count(Inst);
  }

  void releaseMemory();

  void print(raw_ostream &OS, const Module * = nullptr) const;

  /// dump - This method is used for debugging.
  void dump() const;
};

Pass *createIVUsersPass();

class IVUsersWrapperPass : public LoopPass {
  std::unique_ptr<IVUsers> IU;

public:
  static char ID;

  IVUsersWrapperPass();

  IVUsers &getIU() { return *IU; }
  const IVUsers &getIU() const { return *IU; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnLoop(Loop *L, LPPassManager &LPM) override;

  void releaseMemory() override;

  void print(raw_ostream &OS, const Module * = nullptr) const override;
};

/// Analysis pass that exposes the \c IVUsers for a loop.
class IVUsersAnalysis : public AnalysisInfoMixin<IVUsersAnalysis> {
  friend AnalysisInfoMixin<IVUsersAnalysis>;
  static AnalysisKey Key;

public:
  typedef IVUsers Result;

  IVUsers run(Loop &L, LoopAnalysisManager &AM,
              LoopStandardAnalysisResults &AR);
};

}

#endif
