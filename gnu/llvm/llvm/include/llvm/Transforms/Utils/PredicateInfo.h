//===- PredicateInfo.h - Build PredicateInfo ----------------------*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  This file implements the PredicateInfo analysis, which creates an Extended
/// SSA form for operations used in branch comparisons and llvm.assume
/// comparisons.
///
/// Copies of these operations are inserted into the true/false edge (and after
/// assumes), and information attached to the copies.  All uses of the original
/// operation in blocks dominated by the true/false edge (and assume), are
/// replaced with uses of the copies.  This enables passes to easily and sparsely
/// propagate condition based info into the operations that may be affected.
///
/// Example:
/// %cmp = icmp eq i32 %x, 50
/// br i1 %cmp, label %true, label %false
/// true:
/// ret i32 %x
/// false:
/// ret i32 1
///
/// will become
///
/// %cmp = icmp eq i32, %x, 50
/// br i1 %cmp, label %true, label %false
/// true:
/// %x.0 = call \@llvm.ssa_copy.i32(i32 %x)
/// ret i32 %x.0
/// false:
/// ret i32 1
///
/// Using getPredicateInfoFor on x.0 will give you the comparison it is
/// dominated by (the icmp), and that you are located in the true edge of that
/// comparison, which tells you x.0 is 50.
///
/// In order to reduce the number of copies inserted, predicateinfo is only
/// inserted where it would actually be live.  This means if there are no uses of
/// an operation dominated by the branch edges, or by an assume, the associated
/// predicate info is never inserted.
///
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H
#define LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Function;
class Value;
class IntrinsicInst;
class raw_ostream;

enum PredicateType { PT_Branch, PT_Assume, PT_Switch };

/// Constraint for a predicate of the form "cmp Pred Op, OtherOp", where Op
/// is the value the constraint applies to (the ssa.copy result).
struct PredicateConstraint {
  CmpInst::Predicate Predicate;
  Value *OtherOp;
};

// Base class for all predicate information we provide.
// All of our predicate information has at least a comparison.
class PredicateBase : public ilist_node<PredicateBase> {
public:
  PredicateType Type;
  // The original operand before we renamed it.
  // This can be use by passes, when destroying predicateinfo, to know
  // whether they can just drop the intrinsic, or have to merge metadata.
  Value *OriginalOp;
  // The renamed operand in the condition used for this predicate. For nested
  // predicates, this is different to OriginalOp which refers to the initial
  // operand.
  Value *RenamedOp;
  // The condition associated with this predicate.
  Value *Condition;

  PredicateBase(const PredicateBase &) = delete;
  PredicateBase &operator=(const PredicateBase &) = delete;
  PredicateBase() = delete;
  virtual ~PredicateBase() = default;
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Assume || PB->Type == PT_Branch ||
           PB->Type == PT_Switch;
  }

  /// Fetch condition in the form of PredicateConstraint, if possible.
  std::optional<PredicateConstraint> getConstraint() const;

protected:
  PredicateBase(PredicateType PT, Value *Op, Value *Condition)
      : Type(PT), OriginalOp(Op), Condition(Condition) {}
};

// Provides predicate information for assumes.  Since assumes are always true,
// we simply provide the assume instruction, so you can tell your relative
// position to it.
class PredicateAssume : public PredicateBase {
public:
  IntrinsicInst *AssumeInst;
  PredicateAssume(Value *Op, IntrinsicInst *AssumeInst, Value *Condition)
      : PredicateBase(PT_Assume, Op, Condition), AssumeInst(AssumeInst) {}
  PredicateAssume() = delete;
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Assume;
  }
};

// Mixin class for edge predicates.  The FROM block is the block where the
// predicate originates, and the TO block is the block where the predicate is
// valid.
class PredicateWithEdge : public PredicateBase {
public:
  BasicBlock *From;
  BasicBlock *To;
  PredicateWithEdge() = delete;
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Branch || PB->Type == PT_Switch;
  }

protected:
  PredicateWithEdge(PredicateType PType, Value *Op, BasicBlock *From,
                    BasicBlock *To, Value *Cond)
      : PredicateBase(PType, Op, Cond), From(From), To(To) {}
};

// Provides predicate information for branches.
class PredicateBranch : public PredicateWithEdge {
public:
  // If true, SplitBB is the true successor, otherwise it's the false successor.
  bool TrueEdge;
  PredicateBranch(Value *Op, BasicBlock *BranchBB, BasicBlock *SplitBB,
                  Value *Condition, bool TakenEdge)
      : PredicateWithEdge(PT_Branch, Op, BranchBB, SplitBB, Condition),
        TrueEdge(TakenEdge) {}
  PredicateBranch() = delete;
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Branch;
  }
};

class PredicateSwitch : public PredicateWithEdge {
public:
  Value *CaseValue;
  // This is the switch instruction.
  SwitchInst *Switch;
  PredicateSwitch(Value *Op, BasicBlock *SwitchBB, BasicBlock *TargetBB,
                  Value *CaseValue, SwitchInst *SI)
      : PredicateWithEdge(PT_Switch, Op, SwitchBB, TargetBB,
                          SI->getCondition()),
        CaseValue(CaseValue), Switch(SI) {}
  PredicateSwitch() = delete;
  static bool classof(const PredicateBase *PB) {
    return PB->Type == PT_Switch;
  }
};

/// Encapsulates PredicateInfo, including all data associated with memory
/// accesses.
class PredicateInfo {
public:
  PredicateInfo(Function &, DominatorTree &, AssumptionCache &);
  ~PredicateInfo();

  void verifyPredicateInfo() const;

  void dump() const;
  void print(raw_ostream &) const;

  const PredicateBase *getPredicateInfoFor(const Value *V) const {
    return PredicateMap.lookup(V);
  }

protected:
  // Used by PredicateInfo annotater, dumpers, and wrapper pass.
  friend class PredicateInfoAnnotatedWriter;
  friend class PredicateInfoBuilder;

private:
  Function &F;

  // This owns the all the predicate infos in the function, placed or not.
  iplist<PredicateBase> AllInfos;

  // This maps from copy operands to Predicate Info. Note that it does not own
  // the Predicate Info, they belong to the ValueInfo structs in the ValueInfos
  // vector.
  DenseMap<const Value *, const PredicateBase *> PredicateMap;
  // The set of ssa_copy declarations we created with our custom mangling.
  SmallSet<AssertingVH<Function>, 20> CreatedDeclarations;
};

/// Printer pass for \c PredicateInfo.
class PredicateInfoPrinterPass
    : public PassInfoMixin<PredicateInfoPrinterPass> {
  raw_ostream &OS;

public:
  explicit PredicateInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Verifier pass for \c PredicateInfo.
struct PredicateInfoVerifierPass : PassInfoMixin<PredicateInfoVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_PREDICATEINFO_H
