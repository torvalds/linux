//===- TailRecursionElimination.cpp - Eliminate Tail Calls ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file transforms calls of the current function (self recursion) followed
// by a return instruction with a branch to the entry of the function, creating
// a loop.  This pass also implements the following extensions to the basic
// algorithm:
//
//  1. Trivial instructions between the call and return do not prevent the
//     transformation from taking place, though currently the analysis cannot
//     support moving any really useful instructions (only dead ones).
//  2. This pass transforms functions that are prevented from being tail
//     recursive by an associative and commutative expression to use an
//     accumulator variable, thus compiling the typical naive factorial or
//     'fib' implementation into efficient code.
//  3. TRE is performed if the function returns void, if the return
//     returns the result returned by the call, or if the function returns a
//     run-time constant on all exits from the function.  It is possible, though
//     unlikely, that the return returns something else (like constant 0), and
//     can still be TRE'd.  It can be TRE'd if ALL OTHER return instructions in
//     the function return the exact same value.
//  4. If it can prove that callees do not access their caller stack frame,
//     they are marked as eligible for tail call elimination (by the code
//     generator).
//
// There are several improvements that could be made:
//
//  1. If the function has any alloca instructions, these instructions will be
//     moved out of the entry block of the function, causing them to be
//     evaluated each time through the tail recursion.  Safely keeping allocas
//     in the entry block requires analysis to proves that the tail-called
//     function does not read or write the stack object.
//  2. Tail recursion is only performed if the call immediately precedes the
//     return instruction.  It's possible that there could be a jump between
//     the call and the return.
//  3. There can be intervening operations between the call and the return that
//     prevent the TRE from occurring.  For example, there could be GEP's and
//     stores to memory that will not be read or written by the call.  This
//     requires some substantial analysis (such as with DSA) to prove safe to
//     move ahead of the call, but doing so could allow many more TREs to be
//     performed, for example in TreeAdd/TreeAlloc from the treeadd benchmark.
//  4. The algorithm we use to detect if callees access their caller stack
//     frames is very primitive.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

#define DEBUG_TYPE "tailcallelim"

STATISTIC(NumEliminated, "Number of tail calls removed");
STATISTIC(NumRetDuped,   "Number of return duplicated");
STATISTIC(NumAccumAdded, "Number of accumulators introduced");

/// Scan the specified function for alloca instructions.
/// If it contains any dynamic allocas, returns false.
static bool canTRE(Function &F) {
  // TODO: We don't do TRE if dynamic allocas are used.
  // Dynamic allocas allocate stack space which should be
  // deallocated before new iteration started. That is
  // currently not implemented.
  return llvm::all_of(instructions(F), [](Instruction &I) {
    auto *AI = dyn_cast<AllocaInst>(&I);
    return !AI || AI->isStaticAlloca();
  });
}

namespace {
struct AllocaDerivedValueTracker {
  // Start at a root value and walk its use-def chain to mark calls that use the
  // value or a derived value in AllocaUsers, and places where it may escape in
  // EscapePoints.
  void walk(Value *Root) {
    SmallVector<Use *, 32> Worklist;
    SmallPtrSet<Use *, 32> Visited;

    auto AddUsesToWorklist = [&](Value *V) {
      for (auto &U : V->uses()) {
        if (!Visited.insert(&U).second)
          continue;
        Worklist.push_back(&U);
      }
    };

    AddUsesToWorklist(Root);

    while (!Worklist.empty()) {
      Use *U = Worklist.pop_back_val();
      Instruction *I = cast<Instruction>(U->getUser());

      switch (I->getOpcode()) {
      case Instruction::Call:
      case Instruction::Invoke: {
        auto &CB = cast<CallBase>(*I);
        // If the alloca-derived argument is passed byval it is not an escape
        // point, or a use of an alloca. Calling with byval copies the contents
        // of the alloca into argument registers or stack slots, which exist
        // beyond the lifetime of the current frame.
        if (CB.isArgOperand(U) && CB.isByValArgument(CB.getArgOperandNo(U)))
          continue;
        bool IsNocapture =
            CB.isDataOperand(U) && CB.doesNotCapture(CB.getDataOperandNo(U));
        callUsesLocalStack(CB, IsNocapture);
        if (IsNocapture) {
          // If the alloca-derived argument is passed in as nocapture, then it
          // can't propagate to the call's return. That would be capturing.
          continue;
        }
        break;
      }
      case Instruction::Load: {
        // The result of a load is not alloca-derived (unless an alloca has
        // otherwise escaped, but this is a local analysis).
        continue;
      }
      case Instruction::Store: {
        if (U->getOperandNo() == 0)
          EscapePoints.insert(I);
        continue;  // Stores have no users to analyze.
      }
      case Instruction::BitCast:
      case Instruction::GetElementPtr:
      case Instruction::PHI:
      case Instruction::Select:
      case Instruction::AddrSpaceCast:
        break;
      default:
        EscapePoints.insert(I);
        break;
      }

      AddUsesToWorklist(I);
    }
  }

  void callUsesLocalStack(CallBase &CB, bool IsNocapture) {
    // Add it to the list of alloca users.
    AllocaUsers.insert(&CB);

    // If it's nocapture then it can't capture this alloca.
    if (IsNocapture)
      return;

    // If it can write to memory, it can leak the alloca value.
    if (!CB.onlyReadsMemory())
      EscapePoints.insert(&CB);
  }

  SmallPtrSet<Instruction *, 32> AllocaUsers;
  SmallPtrSet<Instruction *, 32> EscapePoints;
};
}

static bool markTails(Function &F, OptimizationRemarkEmitter *ORE) {
  if (F.callsFunctionThatReturnsTwice())
    return false;

  // The local stack holds all alloca instructions and all byval arguments.
  AllocaDerivedValueTracker Tracker;
  for (Argument &Arg : F.args()) {
    if (Arg.hasByValAttr())
      Tracker.walk(&Arg);
  }
  for (auto &BB : F) {
    for (auto &I : BB)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
        Tracker.walk(AI);
  }

  bool Modified = false;

  // Track whether a block is reachable after an alloca has escaped. Blocks that
  // contain the escaping instruction will be marked as being visited without an
  // escaped alloca, since that is how the block began.
  enum VisitType {
    UNVISITED,
    UNESCAPED,
    ESCAPED
  };
  DenseMap<BasicBlock *, VisitType> Visited;

  // We propagate the fact that an alloca has escaped from block to successor.
  // Visit the blocks that are propagating the escapedness first. To do this, we
  // maintain two worklists.
  SmallVector<BasicBlock *, 32> WorklistUnescaped, WorklistEscaped;

  // We may enter a block and visit it thinking that no alloca has escaped yet,
  // then see an escape point and go back around a loop edge and come back to
  // the same block twice. Because of this, we defer setting tail on calls when
  // we first encounter them in a block. Every entry in this list does not
  // statically use an alloca via use-def chain analysis, but may find an alloca
  // through other means if the block turns out to be reachable after an escape
  // point.
  SmallVector<CallInst *, 32> DeferredTails;

  BasicBlock *BB = &F.getEntryBlock();
  VisitType Escaped = UNESCAPED;
  do {
    for (auto &I : *BB) {
      if (Tracker.EscapePoints.count(&I))
        Escaped = ESCAPED;

      CallInst *CI = dyn_cast<CallInst>(&I);
      // A PseudoProbeInst has the IntrInaccessibleMemOnly tag hence it is
      // considered accessing memory and will be marked as a tail call if we
      // don't bail out here.
      if (!CI || CI->isTailCall() || isa<DbgInfoIntrinsic>(&I) ||
          isa<PseudoProbeInst>(&I))
        continue;

      // Special-case operand bundles "clang.arc.attachedcall", "ptrauth", and
      // "kcfi".
      bool IsNoTail = CI->isNoTailCall() ||
                      CI->hasOperandBundlesOtherThan(
                          {LLVMContext::OB_clang_arc_attachedcall,
                           LLVMContext::OB_ptrauth, LLVMContext::OB_kcfi});

      if (!IsNoTail && CI->doesNotAccessMemory()) {
        // A call to a readnone function whose arguments are all things computed
        // outside this function can be marked tail. Even if you stored the
        // alloca address into a global, a readnone function can't load the
        // global anyhow.
        //
        // Note that this runs whether we know an alloca has escaped or not. If
        // it has, then we can't trust Tracker.AllocaUsers to be accurate.
        bool SafeToTail = true;
        for (auto &Arg : CI->args()) {
          if (isa<Constant>(Arg.getUser()))
            continue;
          if (Argument *A = dyn_cast<Argument>(Arg.getUser()))
            if (!A->hasByValAttr())
              continue;
          SafeToTail = false;
          break;
        }
        if (SafeToTail) {
          using namespace ore;
          ORE->emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "tailcall-readnone", CI)
                   << "marked as tail call candidate (readnone)";
          });
          CI->setTailCall();
          Modified = true;
          continue;
        }
      }

      if (!IsNoTail && Escaped == UNESCAPED && !Tracker.AllocaUsers.count(CI))
        DeferredTails.push_back(CI);
    }

    for (auto *SuccBB : successors(BB)) {
      auto &State = Visited[SuccBB];
      if (State < Escaped) {
        State = Escaped;
        if (State == ESCAPED)
          WorklistEscaped.push_back(SuccBB);
        else
          WorklistUnescaped.push_back(SuccBB);
      }
    }

    if (!WorklistEscaped.empty()) {
      BB = WorklistEscaped.pop_back_val();
      Escaped = ESCAPED;
    } else {
      BB = nullptr;
      while (!WorklistUnescaped.empty()) {
        auto *NextBB = WorklistUnescaped.pop_back_val();
        if (Visited[NextBB] == UNESCAPED) {
          BB = NextBB;
          Escaped = UNESCAPED;
          break;
        }
      }
    }
  } while (BB);

  for (CallInst *CI : DeferredTails) {
    if (Visited[CI->getParent()] != ESCAPED) {
      // If the escape point was part way through the block, calls after the
      // escape point wouldn't have been put into DeferredTails.
      LLVM_DEBUG(dbgs() << "Marked as tail call candidate: " << *CI << "\n");
      CI->setTailCall();
      Modified = true;
    }
  }

  return Modified;
}

/// Return true if it is safe to move the specified
/// instruction from after the call to before the call, assuming that all
/// instructions between the call and this instruction are movable.
///
static bool canMoveAboveCall(Instruction *I, CallInst *CI, AliasAnalysis *AA) {
  if (isa<DbgInfoIntrinsic>(I))
    return true;

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    if (II->getIntrinsicID() == Intrinsic::lifetime_end &&
        llvm::findAllocaForValue(II->getArgOperand(1)))
      return true;

  // FIXME: We can move load/store/call/free instructions above the call if the
  // call does not mod/ref the memory location being processed.
  if (I->mayHaveSideEffects())  // This also handles volatile loads.
    return false;

  if (LoadInst *L = dyn_cast<LoadInst>(I)) {
    // Loads may always be moved above calls without side effects.
    if (CI->mayHaveSideEffects()) {
      // Non-volatile loads may be moved above a call with side effects if it
      // does not write to memory and the load provably won't trap.
      // Writes to memory only matter if they may alias the pointer
      // being loaded from.
      const DataLayout &DL = L->getDataLayout();
      if (isModSet(AA->getModRefInfo(CI, MemoryLocation::get(L))) ||
          !isSafeToLoadUnconditionally(L->getPointerOperand(), L->getType(),
                                       L->getAlign(), DL, L))
        return false;
    }
  }

  // Otherwise, if this is a side-effect free instruction, check to make sure
  // that it does not use the return value of the call.  If it doesn't use the
  // return value of the call, it must only use things that are defined before
  // the call, or movable instructions between the call and the instruction
  // itself.
  return !is_contained(I->operands(), CI);
}

static bool canTransformAccumulatorRecursion(Instruction *I, CallInst *CI) {
  if (!I->isAssociative() || !I->isCommutative())
    return false;

  assert(I->getNumOperands() >= 2 &&
         "Associative/commutative operations should have at least 2 args!");

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    // Accumulators must have an identity.
    if (!ConstantExpr::getIntrinsicIdentity(II->getIntrinsicID(), I->getType()))
      return false;
  }

  // Exactly one operand should be the result of the call instruction.
  if ((I->getOperand(0) == CI && I->getOperand(1) == CI) ||
      (I->getOperand(0) != CI && I->getOperand(1) != CI))
    return false;

  // The only user of this instruction we allow is a single return instruction.
  if (!I->hasOneUse() || !isa<ReturnInst>(I->user_back()))
    return false;

  return true;
}

static Instruction *firstNonDbg(BasicBlock::iterator I) {
  while (isa<DbgInfoIntrinsic>(I))
    ++I;
  return &*I;
}

namespace {
class TailRecursionEliminator {
  Function &F;
  const TargetTransformInfo *TTI;
  AliasAnalysis *AA;
  OptimizationRemarkEmitter *ORE;
  DomTreeUpdater &DTU;

  // The below are shared state we want to have available when eliminating any
  // calls in the function. There values should be populated by
  // createTailRecurseLoopHeader the first time we find a call we can eliminate.
  BasicBlock *HeaderBB = nullptr;
  SmallVector<PHINode *, 8> ArgumentPHIs;

  // PHI node to store our return value.
  PHINode *RetPN = nullptr;

  // i1 PHI node to track if we have a valid return value stored in RetPN.
  PHINode *RetKnownPN = nullptr;

  // Vector of select instructions we insereted. These selects use RetKnownPN
  // to either propagate RetPN or select a new return value.
  SmallVector<SelectInst *, 8> RetSelects;

  // The below are shared state needed when performing accumulator recursion.
  // There values should be populated by insertAccumulator the first time we
  // find an elimination that requires an accumulator.

  // PHI node to store our current accumulated value.
  PHINode *AccPN = nullptr;

  // The instruction doing the accumulating.
  Instruction *AccumulatorRecursionInstr = nullptr;

  TailRecursionEliminator(Function &F, const TargetTransformInfo *TTI,
                          AliasAnalysis *AA, OptimizationRemarkEmitter *ORE,
                          DomTreeUpdater &DTU)
      : F(F), TTI(TTI), AA(AA), ORE(ORE), DTU(DTU) {}

  CallInst *findTRECandidate(BasicBlock *BB);

  void createTailRecurseLoopHeader(CallInst *CI);

  void insertAccumulator(Instruction *AccRecInstr);

  bool eliminateCall(CallInst *CI);

  void cleanupAndFinalize();

  bool processBlock(BasicBlock &BB);

  void copyByValueOperandIntoLocalTemp(CallInst *CI, int OpndIdx);

  void copyLocalTempOfByValueOperandIntoArguments(CallInst *CI, int OpndIdx);

public:
  static bool eliminate(Function &F, const TargetTransformInfo *TTI,
                        AliasAnalysis *AA, OptimizationRemarkEmitter *ORE,
                        DomTreeUpdater &DTU);
};
} // namespace

CallInst *TailRecursionEliminator::findTRECandidate(BasicBlock *BB) {
  Instruction *TI = BB->getTerminator();

  if (&BB->front() == TI) // Make sure there is something before the terminator.
    return nullptr;

  // Scan backwards from the return, checking to see if there is a tail call in
  // this block.  If so, set CI to it.
  CallInst *CI = nullptr;
  BasicBlock::iterator BBI(TI);
  while (true) {
    CI = dyn_cast<CallInst>(BBI);
    if (CI && CI->getCalledFunction() == &F)
      break;

    if (BBI == BB->begin())
      return nullptr;          // Didn't find a potential tail call.
    --BBI;
  }

  assert((!CI->isTailCall() || !CI->isNoTailCall()) &&
         "Incompatible call site attributes(Tail,NoTail)");
  if (!CI->isTailCall())
    return nullptr;

  // As a special case, detect code like this:
  //   double fabs(double f) { return __builtin_fabs(f); } // a 'fabs' call
  // and disable this xform in this case, because the code generator will
  // lower the call to fabs into inline code.
  if (BB == &F.getEntryBlock() &&
      firstNonDbg(BB->front().getIterator()) == CI &&
      firstNonDbg(std::next(BB->begin())) == TI && CI->getCalledFunction() &&
      !TTI->isLoweredToCall(CI->getCalledFunction())) {
    // A single-block function with just a call and a return. Check that
    // the arguments match.
    auto I = CI->arg_begin(), E = CI->arg_end();
    Function::arg_iterator FI = F.arg_begin(), FE = F.arg_end();
    for (; I != E && FI != FE; ++I, ++FI)
      if (*I != &*FI) break;
    if (I == E && FI == FE)
      return nullptr;
  }

  return CI;
}

void TailRecursionEliminator::createTailRecurseLoopHeader(CallInst *CI) {
  HeaderBB = &F.getEntryBlock();
  BasicBlock *NewEntry = BasicBlock::Create(F.getContext(), "", &F, HeaderBB);
  NewEntry->takeName(HeaderBB);
  HeaderBB->setName("tailrecurse");
  BranchInst::Create(HeaderBB, NewEntry);
  // If the new branch preserves the debug location of CI, it could result in
  // misleading stepping, if CI is located in a conditional branch.
  // So, here we don't give any debug location to the new branch.

  // Move all fixed sized allocas from HeaderBB to NewEntry.
  for (BasicBlock::iterator OEBI = HeaderBB->begin(), E = HeaderBB->end(),
                            NEBI = NewEntry->begin();
       OEBI != E;)
    if (AllocaInst *AI = dyn_cast<AllocaInst>(OEBI++))
      if (isa<ConstantInt>(AI->getArraySize()))
        AI->moveBefore(&*NEBI);

  // Now that we have created a new block, which jumps to the entry
  // block, insert a PHI node for each argument of the function.
  // For now, we initialize each PHI to only have the real arguments
  // which are passed in.
  BasicBlock::iterator InsertPos = HeaderBB->begin();
  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    PHINode *PN = PHINode::Create(I->getType(), 2, I->getName() + ".tr");
    PN->insertBefore(InsertPos);
    I->replaceAllUsesWith(PN); // Everyone use the PHI node now!
    PN->addIncoming(&*I, NewEntry);
    ArgumentPHIs.push_back(PN);
  }

  // If the function doen't return void, create the RetPN and RetKnownPN PHI
  // nodes to track our return value. We initialize RetPN with poison and
  // RetKnownPN with false since we can't know our return value at function
  // entry.
  Type *RetType = F.getReturnType();
  if (!RetType->isVoidTy()) {
    Type *BoolType = Type::getInt1Ty(F.getContext());
    RetPN = PHINode::Create(RetType, 2, "ret.tr");
    RetPN->insertBefore(InsertPos);
    RetKnownPN = PHINode::Create(BoolType, 2, "ret.known.tr");
    RetKnownPN->insertBefore(InsertPos);

    RetPN->addIncoming(PoisonValue::get(RetType), NewEntry);
    RetKnownPN->addIncoming(ConstantInt::getFalse(BoolType), NewEntry);
  }

  // The entry block was changed from HeaderBB to NewEntry.
  // The forward DominatorTree needs to be recalculated when the EntryBB is
  // changed. In this corner-case we recalculate the entire tree.
  DTU.recalculate(*NewEntry->getParent());
}

void TailRecursionEliminator::insertAccumulator(Instruction *AccRecInstr) {
  assert(!AccPN && "Trying to insert multiple accumulators");

  AccumulatorRecursionInstr = AccRecInstr;

  // Start by inserting a new PHI node for the accumulator.
  pred_iterator PB = pred_begin(HeaderBB), PE = pred_end(HeaderBB);
  AccPN = PHINode::Create(F.getReturnType(), std::distance(PB, PE) + 1,
                          "accumulator.tr");
  AccPN->insertBefore(HeaderBB->begin());

  // Loop over all of the predecessors of the tail recursion block.  For the
  // real entry into the function we seed the PHI with the identity constant for
  // the accumulation operation.  For any other existing branches to this block
  // (due to other tail recursions eliminated) the accumulator is not modified.
  // Because we haven't added the branch in the current block to HeaderBB yet,
  // it will not show up as a predecessor.
  for (pred_iterator PI = PB; PI != PE; ++PI) {
    BasicBlock *P = *PI;
    if (P == &F.getEntryBlock()) {
      Constant *Identity =
          ConstantExpr::getIdentity(AccRecInstr, AccRecInstr->getType());
      AccPN->addIncoming(Identity, P);
    } else {
      AccPN->addIncoming(AccPN, P);
    }
  }

  ++NumAccumAdded;
}

// Creates a copy of contents of ByValue operand of the specified
// call instruction into the newly created temporarily variable.
void TailRecursionEliminator::copyByValueOperandIntoLocalTemp(CallInst *CI,
                                                              int OpndIdx) {
  Type *AggTy = CI->getParamByValType(OpndIdx);
  assert(AggTy);
  const DataLayout &DL = F.getDataLayout();

  // Get alignment of byVal operand.
  Align Alignment(CI->getParamAlign(OpndIdx).valueOrOne());

  // Create alloca for temporarily byval operands.
  // Put alloca into the entry block.
  Value *NewAlloca = new AllocaInst(
      AggTy, DL.getAllocaAddrSpace(), nullptr, Alignment,
      CI->getArgOperand(OpndIdx)->getName(), F.getEntryBlock().begin());

  IRBuilder<> Builder(CI);
  Value *Size = Builder.getInt64(DL.getTypeAllocSize(AggTy));

  // Copy data from byvalue operand into the temporarily variable.
  Builder.CreateMemCpy(NewAlloca, /*DstAlign*/ Alignment,
                       CI->getArgOperand(OpndIdx),
                       /*SrcAlign*/ Alignment, Size);
  CI->setArgOperand(OpndIdx, NewAlloca);
}

// Creates a copy from temporarily variable(keeping value of ByVal argument)
// into the corresponding function argument location.
void TailRecursionEliminator::copyLocalTempOfByValueOperandIntoArguments(
    CallInst *CI, int OpndIdx) {
  Type *AggTy = CI->getParamByValType(OpndIdx);
  assert(AggTy);
  const DataLayout &DL = F.getDataLayout();

  // Get alignment of byVal operand.
  Align Alignment(CI->getParamAlign(OpndIdx).valueOrOne());

  IRBuilder<> Builder(CI);
  Value *Size = Builder.getInt64(DL.getTypeAllocSize(AggTy));

  // Copy data from the temporarily variable into corresponding
  // function argument location.
  Builder.CreateMemCpy(F.getArg(OpndIdx), /*DstAlign*/ Alignment,
                       CI->getArgOperand(OpndIdx),
                       /*SrcAlign*/ Alignment, Size);
}

bool TailRecursionEliminator::eliminateCall(CallInst *CI) {
  ReturnInst *Ret = cast<ReturnInst>(CI->getParent()->getTerminator());

  // Ok, we found a potential tail call.  We can currently only transform the
  // tail call if all of the instructions between the call and the return are
  // movable to above the call itself, leaving the call next to the return.
  // Check that this is the case now.
  Instruction *AccRecInstr = nullptr;
  BasicBlock::iterator BBI(CI);
  for (++BBI; &*BBI != Ret; ++BBI) {
    if (canMoveAboveCall(&*BBI, CI, AA))
      continue;

    // If we can't move the instruction above the call, it might be because it
    // is an associative and commutative operation that could be transformed
    // using accumulator recursion elimination.  Check to see if this is the
    // case, and if so, remember which instruction accumulates for later.
    if (AccPN || !canTransformAccumulatorRecursion(&*BBI, CI))
      return false; // We cannot eliminate the tail recursion!

    // Yes, this is accumulator recursion.  Remember which instruction
    // accumulates.
    AccRecInstr = &*BBI;
  }

  BasicBlock *BB = Ret->getParent();

  using namespace ore;
  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "tailcall-recursion", CI)
           << "transforming tail recursion into loop";
  });

  // OK! We can transform this tail call.  If this is the first one found,
  // create the new entry block, allowing us to branch back to the old entry.
  if (!HeaderBB)
    createTailRecurseLoopHeader(CI);

  // Copy values of ByVal operands into local temporarily variables.
  for (unsigned I = 0, E = CI->arg_size(); I != E; ++I) {
    if (CI->isByValArgument(I))
      copyByValueOperandIntoLocalTemp(CI, I);
  }

  // Ok, now that we know we have a pseudo-entry block WITH all of the
  // required PHI nodes, add entries into the PHI node for the actual
  // parameters passed into the tail-recursive call.
  for (unsigned I = 0, E = CI->arg_size(); I != E; ++I) {
    if (CI->isByValArgument(I)) {
      copyLocalTempOfByValueOperandIntoArguments(CI, I);
      // When eliminating a tail call, we modify the values of the arguments.
      // Therefore, if the byval parameter has a readonly attribute, we have to
      // remove it. It is safe because, from the perspective of a caller, the
      // byval parameter is always treated as "readonly," even if the readonly
      // attribute is removed.
      F.removeParamAttr(I, Attribute::ReadOnly);
      ArgumentPHIs[I]->addIncoming(F.getArg(I), BB);
    } else
      ArgumentPHIs[I]->addIncoming(CI->getArgOperand(I), BB);
  }

  if (AccRecInstr) {
    insertAccumulator(AccRecInstr);

    // Rewrite the accumulator recursion instruction so that it does not use
    // the result of the call anymore, instead, use the PHI node we just
    // inserted.
    AccRecInstr->setOperand(AccRecInstr->getOperand(0) != CI, AccPN);
  }

  // Update our return value tracking
  if (RetPN) {
    if (Ret->getReturnValue() == CI || AccRecInstr) {
      // Defer selecting a return value
      RetPN->addIncoming(RetPN, BB);
      RetKnownPN->addIncoming(RetKnownPN, BB);
    } else {
      // We found a return value we want to use, insert a select instruction to
      // select it if we don't already know what our return value will be and
      // store the result in our return value PHI node.
      SelectInst *SI =
          SelectInst::Create(RetKnownPN, RetPN, Ret->getReturnValue(),
                             "current.ret.tr", Ret->getIterator());
      RetSelects.push_back(SI);

      RetPN->addIncoming(SI, BB);
      RetKnownPN->addIncoming(ConstantInt::getTrue(RetKnownPN->getType()), BB);
    }

    if (AccPN)
      AccPN->addIncoming(AccRecInstr ? AccRecInstr : AccPN, BB);
  }

  // Now that all of the PHI nodes are in place, remove the call and
  // ret instructions, replacing them with an unconditional branch.
  BranchInst *NewBI = BranchInst::Create(HeaderBB, Ret->getIterator());
  NewBI->setDebugLoc(CI->getDebugLoc());

  Ret->eraseFromParent();  // Remove return.
  CI->eraseFromParent();   // Remove call.
  DTU.applyUpdates({{DominatorTree::Insert, BB, HeaderBB}});
  ++NumEliminated;
  return true;
}

void TailRecursionEliminator::cleanupAndFinalize() {
  // If we eliminated any tail recursions, it's possible that we inserted some
  // silly PHI nodes which just merge an initial value (the incoming operand)
  // with themselves.  Check to see if we did and clean up our mess if so.  This
  // occurs when a function passes an argument straight through to its tail
  // call.
  for (PHINode *PN : ArgumentPHIs) {
    // If the PHI Node is a dynamic constant, replace it with the value it is.
    if (Value *PNV = simplifyInstruction(PN, F.getDataLayout())) {
      PN->replaceAllUsesWith(PNV);
      PN->eraseFromParent();
    }
  }

  if (RetPN) {
    if (RetSelects.empty()) {
      // If we didn't insert any select instructions, then we know we didn't
      // store a return value and we can remove the PHI nodes we inserted.
      RetPN->dropAllReferences();
      RetPN->eraseFromParent();

      RetKnownPN->dropAllReferences();
      RetKnownPN->eraseFromParent();

      if (AccPN) {
        // We need to insert a copy of our accumulator instruction before any
        // return in the function, and return its result instead.
        Instruction *AccRecInstr = AccumulatorRecursionInstr;
        for (BasicBlock &BB : F) {
          ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator());
          if (!RI)
            continue;

          Instruction *AccRecInstrNew = AccRecInstr->clone();
          AccRecInstrNew->setName("accumulator.ret.tr");
          AccRecInstrNew->setOperand(AccRecInstr->getOperand(0) == AccPN,
                                     RI->getOperand(0));
          AccRecInstrNew->insertBefore(RI);
          AccRecInstrNew->dropLocation();
          RI->setOperand(0, AccRecInstrNew);
        }
      }
    } else {
      // We need to insert a select instruction before any return left in the
      // function to select our stored return value if we have one.
      for (BasicBlock &BB : F) {
        ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator());
        if (!RI)
          continue;

        SelectInst *SI =
            SelectInst::Create(RetKnownPN, RetPN, RI->getOperand(0),
                               "current.ret.tr", RI->getIterator());
        RetSelects.push_back(SI);
        RI->setOperand(0, SI);
      }

      if (AccPN) {
        // We need to insert a copy of our accumulator instruction before any
        // of the selects we inserted, and select its result instead.
        Instruction *AccRecInstr = AccumulatorRecursionInstr;
        for (SelectInst *SI : RetSelects) {
          Instruction *AccRecInstrNew = AccRecInstr->clone();
          AccRecInstrNew->setName("accumulator.ret.tr");
          AccRecInstrNew->setOperand(AccRecInstr->getOperand(0) == AccPN,
                                     SI->getFalseValue());
          AccRecInstrNew->insertBefore(SI);
          AccRecInstrNew->dropLocation();
          SI->setFalseValue(AccRecInstrNew);
        }
      }
    }
  }
}

bool TailRecursionEliminator::processBlock(BasicBlock &BB) {
  Instruction *TI = BB.getTerminator();

  if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
    if (BI->isConditional())
      return false;

    BasicBlock *Succ = BI->getSuccessor(0);
    ReturnInst *Ret = dyn_cast<ReturnInst>(Succ->getFirstNonPHIOrDbg(true));

    if (!Ret)
      return false;

    CallInst *CI = findTRECandidate(&BB);

    if (!CI)
      return false;

    LLVM_DEBUG(dbgs() << "FOLDING: " << *Succ
                      << "INTO UNCOND BRANCH PRED: " << BB);
    FoldReturnIntoUncondBranch(Ret, Succ, &BB, &DTU);
    ++NumRetDuped;

    // If all predecessors of Succ have been eliminated by
    // FoldReturnIntoUncondBranch, delete it.  It is important to empty it,
    // because the ret instruction in there is still using a value which
    // eliminateCall will attempt to remove.  This block can only contain
    // instructions that can't have uses, therefore it is safe to remove.
    if (pred_empty(Succ))
      DTU.deleteBB(Succ);

    eliminateCall(CI);
    return true;
  } else if (isa<ReturnInst>(TI)) {
    CallInst *CI = findTRECandidate(&BB);

    if (CI)
      return eliminateCall(CI);
  }

  return false;
}

bool TailRecursionEliminator::eliminate(Function &F,
                                        const TargetTransformInfo *TTI,
                                        AliasAnalysis *AA,
                                        OptimizationRemarkEmitter *ORE,
                                        DomTreeUpdater &DTU) {
  if (F.getFnAttribute("disable-tail-calls").getValueAsBool())
    return false;

  bool MadeChange = false;
  MadeChange |= markTails(F, ORE);

  // If this function is a varargs function, we won't be able to PHI the args
  // right, so don't even try to convert it...
  if (F.getFunctionType()->isVarArg())
    return MadeChange;

  if (!canTRE(F))
    return MadeChange;

  // Change any tail recursive calls to loops.
  TailRecursionEliminator TRE(F, TTI, AA, ORE, DTU);

  for (BasicBlock &BB : F)
    MadeChange |= TRE.processBlock(BB);

  TRE.cleanupAndFinalize();

  return MadeChange;
}

namespace {
struct TailCallElim : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  TailCallElim() : FunctionPass(ID) {
    initializeTailCallElimPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<PostDominatorTreeWrapperPass>();
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
    auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
    auto *PDTWP = getAnalysisIfAvailable<PostDominatorTreeWrapperPass>();
    auto *PDT = PDTWP ? &PDTWP->getPostDomTree() : nullptr;
    // There is no noticable performance difference here between Lazy and Eager
    // UpdateStrategy based on some test results. It is feasible to switch the
    // UpdateStrategy to Lazy if we find it profitable later.
    DomTreeUpdater DTU(DT, PDT, DomTreeUpdater::UpdateStrategy::Eager);

    return TailRecursionEliminator::eliminate(
        F, &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F),
        &getAnalysis<AAResultsWrapperPass>().getAAResults(),
        &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE(), DTU);
  }
};
}

char TailCallElim::ID = 0;
INITIALIZE_PASS_BEGIN(TailCallElim, "tailcallelim", "Tail Call Elimination",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(TailCallElim, "tailcallelim", "Tail Call Elimination",
                    false, false)

// Public interface to the TailCallElimination pass
FunctionPass *llvm::createTailCallEliminationPass() {
  return new TailCallElim();
}

PreservedAnalyses TailCallElimPass::run(Function &F,
                                        FunctionAnalysisManager &AM) {

  TargetTransformInfo &TTI = AM.getResult<TargetIRAnalysis>(F);
  AliasAnalysis &AA = AM.getResult<AAManager>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  auto *PDT = AM.getCachedResult<PostDominatorTreeAnalysis>(F);
  // There is no noticable performance difference here between Lazy and Eager
  // UpdateStrategy based on some test results. It is feasible to switch the
  // UpdateStrategy to Lazy if we find it profitable later.
  DomTreeUpdater DTU(DT, PDT, DomTreeUpdater::UpdateStrategy::Eager);
  bool Changed = TailRecursionEliminator::eliminate(F, &TTI, &AA, &ORE, DTU);

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<PostDominatorTreeAnalysis>();
  return PA;
}
