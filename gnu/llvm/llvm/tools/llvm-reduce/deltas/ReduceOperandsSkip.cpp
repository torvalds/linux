//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReduceOperandsSkip.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include <queue>

using namespace llvm;

/// Collect all values that are directly or indirectly referenced by @p Root,
/// including Root itself. This is a BF search such that the more steps needed
/// to get to the reference, the more behind it is found in @p Collection. Each
/// step could be its own reduction, therefore we consider later values "more
/// reduced".
static SetVector<Value *> collectReferencedValues(Value *Root) {
  SetVector<Value *> Refs;
  std::deque<Value *> Worklist;
  Worklist.push_back(Root);

  while (!Worklist.empty()) {
    Value *Val = Worklist.front();
    Worklist.pop_front();
    if (!Refs.insert(Val))
      continue;

    if (auto *O = dyn_cast<Operator>(Val)) {
      for (Use &Op : O->operands())
        Worklist.push_back(Op.get());
    }
  }

  return Refs;
}

static bool shouldReduceOperand(Use &Op) {
  Type *Ty = Op->getType();
  if (Ty->isLabelTy() || Ty->isMetadataTy())
    return false;
  // TODO: be more precise about which GEP operands we can reduce (e.g. array
  // indexes)
  if (isa<GEPOperator>(Op.getUser()))
    return false;
  if (auto *CB = dyn_cast<CallBase>(Op.getUser())) {
    if (&CB->getCalledOperandUse() == &Op)
      return false;
  }
  return true;
}

/// Return a reduction priority for @p V. A higher values means "more reduced".
static int classifyReductivePower(Value *V) {
  if (auto *C = dyn_cast<ConstantData>(V)) {
    if (isa<UndefValue>(V))
      return -2;
    if (C->isNullValue())
      return 7;
    if (C->isOneValue())
      return 6;
    return 5;
  }

  if (isa<Argument>(V))
    return 3;

  if (isa<GlobalValue>(V))
    return 2;

  if (isa<Constant>(V))
    return 1;

  if (isa<Instruction>(V))
    return -1;

  return 0;
}

/// Calls @p Callback for every reduction opportunity in @p F. Used by
/// countOperands() and extractOperandsFromModule() to ensure consistency
/// between the two.
static void
opportunities(Function &F,
              function_ref<void(Use &, ArrayRef<Value *>)> Callback) {
  if (F.isDeclaration())
    return;

  // Need DominatorTree to find out whether an SSA value can be referenced.
  DominatorTree DT(F);

  // Return whether @p LHS is "more reduced" that @p RHS. That is, whether
  // @p RHS should be preferred over @p LHS in a reduced output. This is a
  // partial order, a Value may not be preferable over another.
  auto IsMoreReduced = [&DT](Value *LHS, Value *RHS) -> bool {
    // A value is not more reduced than itself.
    if (LHS == RHS)
      return false;

    int ReductivePowerDiff =
        classifyReductivePower(RHS) - classifyReductivePower(LHS);
    if (ReductivePowerDiff != 0)
      return ReductivePowerDiff < 0;

    // LHS is more reduced if it is defined further up the dominance tree. In a
    // chain of definitions,
    //
    // %a = ..
    // %b = op %a
    // %c = op %b
    //
    // every use of %b can be replaced by %a, but not by a use of %c. That is, a
    // use %c can be replaced in steps first by %b, then by %a, making %a the
    // "more reduced" choice that skips over more instructions.
    auto *LHSInst = dyn_cast<Instruction>(LHS);
    auto *RHSInst = dyn_cast<Instruction>(RHS);
    if (LHSInst && RHSInst) {
      if (DT.dominates(LHSInst, RHSInst))
        return true;
    }

    // Compress the number of used arguments by prefering the first ones. Unused
    // trailing argument can be removed by the arguments pass.
    auto *LHSArg = dyn_cast<Argument>(LHS);
    auto *RHSArg = dyn_cast<Argument>(RHS);
    if (LHSArg && RHSArg) {
      if (LHSArg->getArgNo() < RHSArg->getArgNo())
        return true;
    }

    return false;
  };

  for (Instruction &I : instructions(&F)) {
    for (Use &Op : I.operands()) {
      if (!shouldReduceOperand(Op))
        continue;
      Value *OpVal = Op.get();

      // Collect refenced values as potential replacement candidates.
      SetVector<Value *> ReferencedVals = collectReferencedValues(OpVal);

      // Regardless whether referenced, add the function arguments as
      // replacement possibility with the goal of reducing the number of (used)
      // function arguments, possibly created by the operands-to-args.
      for (Argument &Arg : F.args())
        ReferencedVals.insert(&Arg);

      // After all candidates have been added, it doesn't need to be a set
      // anymore.
      auto Candidates = ReferencedVals.takeVector();

      // Remove ineligible candidates.
      llvm::erase_if(Candidates, [&, OpVal](Value *V) {
        // Candidate value must have the same type.
        if (OpVal->getType() != V->getType())
          return true;

        // Do not introduce address captures of intrinsics.
        if (Function *F = dyn_cast<Function>(V)) {
          if (F->isIntrinsic())
            return true;
        }

        // Only consider candidates that are "more reduced" than the original
        // value. This explicitly also rules out candidates with the same
        // reduction power. This is to ensure that repeated invocations of this
        // pass eventually reach a fixpoint without switch back and forth
        // between two opportunities with the same reductive power.
        return !IsMoreReduced(V, OpVal);
      });

      if (Candidates.empty())
        continue;

      // collectReferencedValues pushed the more reductive values to the end of
      // the collection, but we need them at the front.
      std::reverse(Candidates.begin(), Candidates.end());

      // Independency of collectReferencedValues's idea of reductive power,
      // ensure the partial order of IsMoreReduced is enforced.
      llvm::stable_sort(Candidates, IsMoreReduced);

      Callback(Op, Candidates);
    }
  }
}

static void extractOperandsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  for (Function &F : Program.functions()) {
    SmallVector<std::pair<Use *, Value *>> Replacements;
    opportunities(F, [&](Use &Op, ArrayRef<Value *> Candidates) {
      // Only apply the candidate the Oracle selected to keep that is the most
      // reduced. Candidates with less reductive power can be interpreted as an
      // intermediate step that is immediately replaced with the more reduced
      // one. The number of shouldKeep() calls must be independent of the result
      // of previous shouldKeep() calls to keep the total number of calls
      // in-sync with what countOperands() has computed.
      bool AlreadyReplaced = false;
      for (Value *C : Candidates) {
        bool Keep = O.shouldKeep();
        if (AlreadyReplaced || Keep)
          continue;

        // Replacing the operand value immediately would influence the candidate
        // set for the following operands. Delay it until after all candidates
        // have been determined.
        Replacements.push_back({&Op, C});

        AlreadyReplaced = true;
      }
    });

    for (std::pair<Use *, Value *> P : Replacements) {
      if (PHINode *Phi = dyn_cast<PHINode>(P.first->getUser()))
        Phi->setIncomingValueForBlock(Phi->getIncomingBlock(*P.first), P.second);
      else
        P.first->set(P.second);
    }
  }
}

void llvm::reduceOperandsSkipDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, extractOperandsFromModule,
               "Reducing operands by skipping over instructions");
}
