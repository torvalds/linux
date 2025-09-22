//===-- PredicateInfo.cpp - PredicateInfo Builder--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------===//
//
// This file implements the PredicateInfo class.
//
//===----------------------------------------------------------------===//

#include "llvm/Transforms/Utils/PredicateInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/FormattedStream.h"
#include <algorithm>
#define DEBUG_TYPE "predicateinfo"
using namespace llvm;
using namespace PatternMatch;

static cl::opt<bool> VerifyPredicateInfo(
    "verify-predicateinfo", cl::init(false), cl::Hidden,
    cl::desc("Verify PredicateInfo in legacy printer pass."));
DEBUG_COUNTER(RenameCounter, "predicateinfo-rename",
              "Controls which variables are renamed with predicateinfo");

// Maximum number of conditions considered for renaming for each branch/assume.
// This limits renaming of deep and/or chains.
static const unsigned MaxCondsPerBranch = 8;

namespace {
// Given a predicate info that is a type of branching terminator, get the
// branching block.
const BasicBlock *getBranchBlock(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Only branches and switches should have PHIOnly defs that "
         "require branch blocks.");
  return cast<PredicateWithEdge>(PB)->From;
}

// Given a predicate info that is a type of branching terminator, get the
// branching terminator.
static Instruction *getBranchTerminator(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Not a predicate info type we know how to get a terminator from.");
  return cast<PredicateWithEdge>(PB)->From->getTerminator();
}

// Given a predicate info that is a type of branching terminator, get the
// edge this predicate info represents
std::pair<BasicBlock *, BasicBlock *> getBlockEdge(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Not a predicate info type we know how to get an edge from.");
  const auto *PEdge = cast<PredicateWithEdge>(PB);
  return std::make_pair(PEdge->From, PEdge->To);
}
}

namespace llvm {
enum LocalNum {
  // Operations that must appear first in the block.
  LN_First,
  // Operations that are somewhere in the middle of the block, and are sorted on
  // demand.
  LN_Middle,
  // Operations that must appear last in a block, like successor phi node uses.
  LN_Last
};

// Associate global and local DFS info with defs and uses, so we can sort them
// into a global domination ordering.
struct ValueDFS {
  int DFSIn = 0;
  int DFSOut = 0;
  unsigned int LocalNum = LN_Middle;
  // Only one of Def or Use will be set.
  Value *Def = nullptr;
  Use *U = nullptr;
  // Neither PInfo nor EdgeOnly participate in the ordering
  PredicateBase *PInfo = nullptr;
  bool EdgeOnly = false;
};

// Perform a strict weak ordering on instructions and arguments.
static bool valueComesBefore(const Value *A, const Value *B) {
  auto *ArgA = dyn_cast_or_null<Argument>(A);
  auto *ArgB = dyn_cast_or_null<Argument>(B);
  if (ArgA && !ArgB)
    return true;
  if (ArgB && !ArgA)
    return false;
  if (ArgA && ArgB)
    return ArgA->getArgNo() < ArgB->getArgNo();
  return cast<Instruction>(A)->comesBefore(cast<Instruction>(B));
}

// This compares ValueDFS structures. Doing so allows us to walk the minimum
// number of instructions necessary to compute our def/use ordering.
struct ValueDFS_Compare {
  DominatorTree &DT;
  ValueDFS_Compare(DominatorTree &DT) : DT(DT) {}

  bool operator()(const ValueDFS &A, const ValueDFS &B) const {
    if (&A == &B)
      return false;
    // The only case we can't directly compare them is when they in the same
    // block, and both have localnum == middle.  In that case, we have to use
    // comesbefore to see what the real ordering is, because they are in the
    // same basic block.

    assert((A.DFSIn != B.DFSIn || A.DFSOut == B.DFSOut) &&
           "Equal DFS-in numbers imply equal out numbers");
    bool SameBlock = A.DFSIn == B.DFSIn;

    // We want to put the def that will get used for a given set of phi uses,
    // before those phi uses.
    // So we sort by edge, then by def.
    // Note that only phi nodes uses and defs can come last.
    if (SameBlock && A.LocalNum == LN_Last && B.LocalNum == LN_Last)
      return comparePHIRelated(A, B);

    bool isADef = A.Def;
    bool isBDef = B.Def;
    if (!SameBlock || A.LocalNum != LN_Middle || B.LocalNum != LN_Middle)
      return std::tie(A.DFSIn, A.LocalNum, isADef) <
             std::tie(B.DFSIn, B.LocalNum, isBDef);
    return localComesBefore(A, B);
  }

  // For a phi use, or a non-materialized def, return the edge it represents.
  std::pair<BasicBlock *, BasicBlock *> getBlockEdge(const ValueDFS &VD) const {
    if (!VD.Def && VD.U) {
      auto *PHI = cast<PHINode>(VD.U->getUser());
      return std::make_pair(PHI->getIncomingBlock(*VD.U), PHI->getParent());
    }
    // This is really a non-materialized def.
    return ::getBlockEdge(VD.PInfo);
  }

  // For two phi related values, return the ordering.
  bool comparePHIRelated(const ValueDFS &A, const ValueDFS &B) const {
    BasicBlock *ASrc, *ADest, *BSrc, *BDest;
    std::tie(ASrc, ADest) = getBlockEdge(A);
    std::tie(BSrc, BDest) = getBlockEdge(B);

#ifndef NDEBUG
    // This function should only be used for values in the same BB, check that.
    DomTreeNode *DomASrc = DT.getNode(ASrc);
    DomTreeNode *DomBSrc = DT.getNode(BSrc);
    assert(DomASrc->getDFSNumIn() == (unsigned)A.DFSIn &&
           "DFS numbers for A should match the ones of the source block");
    assert(DomBSrc->getDFSNumIn() == (unsigned)B.DFSIn &&
           "DFS numbers for B should match the ones of the source block");
    assert(A.DFSIn == B.DFSIn && "Values must be in the same block");
#endif
    (void)ASrc;
    (void)BSrc;

    // Use DFS numbers to compare destination blocks, to guarantee a
    // deterministic order.
    DomTreeNode *DomADest = DT.getNode(ADest);
    DomTreeNode *DomBDest = DT.getNode(BDest);
    unsigned AIn = DomADest->getDFSNumIn();
    unsigned BIn = DomBDest->getDFSNumIn();
    bool isADef = A.Def;
    bool isBDef = B.Def;
    assert((!A.Def || !A.U) && (!B.Def || !B.U) &&
           "Def and U cannot be set at the same time");
    // Now sort by edge destination and then defs before uses.
    return std::tie(AIn, isADef) < std::tie(BIn, isBDef);
  }

  // Get the definition of an instruction that occurs in the middle of a block.
  Value *getMiddleDef(const ValueDFS &VD) const {
    if (VD.Def)
      return VD.Def;
    // It's possible for the defs and uses to be null.  For branches, the local
    // numbering will say the placed predicaeinfos should go first (IE
    // LN_beginning), so we won't be in this function. For assumes, we will end
    // up here, beause we need to order the def we will place relative to the
    // assume.  So for the purpose of ordering, we pretend the def is right
    // after the assume, because that is where we will insert the info.
    if (!VD.U) {
      assert(VD.PInfo &&
             "No def, no use, and no predicateinfo should not occur");
      assert(isa<PredicateAssume>(VD.PInfo) &&
             "Middle of block should only occur for assumes");
      return cast<PredicateAssume>(VD.PInfo)->AssumeInst->getNextNode();
    }
    return nullptr;
  }

  // Return either the Def, if it's not null, or the user of the Use, if the def
  // is null.
  const Instruction *getDefOrUser(const Value *Def, const Use *U) const {
    if (Def)
      return cast<Instruction>(Def);
    return cast<Instruction>(U->getUser());
  }

  // This performs the necessary local basic block ordering checks to tell
  // whether A comes before B, where both are in the same basic block.
  bool localComesBefore(const ValueDFS &A, const ValueDFS &B) const {
    auto *ADef = getMiddleDef(A);
    auto *BDef = getMiddleDef(B);

    // See if we have real values or uses. If we have real values, we are
    // guaranteed they are instructions or arguments. No matter what, we are
    // guaranteed they are in the same block if they are instructions.
    auto *ArgA = dyn_cast_or_null<Argument>(ADef);
    auto *ArgB = dyn_cast_or_null<Argument>(BDef);

    if (ArgA || ArgB)
      return valueComesBefore(ArgA, ArgB);

    auto *AInst = getDefOrUser(ADef, A.U);
    auto *BInst = getDefOrUser(BDef, B.U);
    return valueComesBefore(AInst, BInst);
  }
};

class PredicateInfoBuilder {
  // Used to store information about each value we might rename.
  struct ValueInfo {
    SmallVector<PredicateBase *, 4> Infos;
  };

  PredicateInfo &PI;
  Function &F;
  DominatorTree &DT;
  AssumptionCache &AC;

  // This stores info about each operand or comparison result we make copies
  // of. The real ValueInfos start at index 1, index 0 is unused so that we
  // can more easily detect invalid indexing.
  SmallVector<ValueInfo, 32> ValueInfos;

  // This gives the index into the ValueInfos array for a given Value. Because
  // 0 is not a valid Value Info index, you can use DenseMap::lookup and tell
  // whether it returned a valid result.
  DenseMap<Value *, unsigned int> ValueInfoNums;

  // The set of edges along which we can only handle phi uses, due to critical
  // edges.
  DenseSet<std::pair<BasicBlock *, BasicBlock *>> EdgeUsesOnly;

  ValueInfo &getOrCreateValueInfo(Value *);
  const ValueInfo &getValueInfo(Value *) const;

  void processAssume(IntrinsicInst *, BasicBlock *,
                     SmallVectorImpl<Value *> &OpsToRename);
  void processBranch(BranchInst *, BasicBlock *,
                     SmallVectorImpl<Value *> &OpsToRename);
  void processSwitch(SwitchInst *, BasicBlock *,
                     SmallVectorImpl<Value *> &OpsToRename);
  void renameUses(SmallVectorImpl<Value *> &OpsToRename);
  void addInfoFor(SmallVectorImpl<Value *> &OpsToRename, Value *Op,
                  PredicateBase *PB);

  typedef SmallVectorImpl<ValueDFS> ValueDFSStack;
  void convertUsesToDFSOrdered(Value *, SmallVectorImpl<ValueDFS> &);
  Value *materializeStack(unsigned int &, ValueDFSStack &, Value *);
  bool stackIsInScope(const ValueDFSStack &, const ValueDFS &) const;
  void popStackUntilDFSScope(ValueDFSStack &, const ValueDFS &);

public:
  PredicateInfoBuilder(PredicateInfo &PI, Function &F, DominatorTree &DT,
                       AssumptionCache &AC)
      : PI(PI), F(F), DT(DT), AC(AC) {
    // Push an empty operand info so that we can detect 0 as not finding one
    ValueInfos.resize(1);
  }

  void buildPredicateInfo();
};

bool PredicateInfoBuilder::stackIsInScope(const ValueDFSStack &Stack,
                                          const ValueDFS &VDUse) const {
  if (Stack.empty())
    return false;
  // If it's a phi only use, make sure it's for this phi node edge, and that the
  // use is in a phi node.  If it's anything else, and the top of the stack is
  // EdgeOnly, we need to pop the stack.  We deliberately sort phi uses next to
  // the defs they must go with so that we can know it's time to pop the stack
  // when we hit the end of the phi uses for a given def.
  if (Stack.back().EdgeOnly) {
    if (!VDUse.U)
      return false;
    auto *PHI = dyn_cast<PHINode>(VDUse.U->getUser());
    if (!PHI)
      return false;
    // Check edge
    BasicBlock *EdgePred = PHI->getIncomingBlock(*VDUse.U);
    if (EdgePred != getBranchBlock(Stack.back().PInfo))
      return false;

    // Use dominates, which knows how to handle edge dominance.
    return DT.dominates(getBlockEdge(Stack.back().PInfo), *VDUse.U);
  }

  return (VDUse.DFSIn >= Stack.back().DFSIn &&
          VDUse.DFSOut <= Stack.back().DFSOut);
}

void PredicateInfoBuilder::popStackUntilDFSScope(ValueDFSStack &Stack,
                                                 const ValueDFS &VD) {
  while (!Stack.empty() && !stackIsInScope(Stack, VD))
    Stack.pop_back();
}

// Convert the uses of Op into a vector of uses, associating global and local
// DFS info with each one.
void PredicateInfoBuilder::convertUsesToDFSOrdered(
    Value *Op, SmallVectorImpl<ValueDFS> &DFSOrderedSet) {
  for (auto &U : Op->uses()) {
    if (auto *I = dyn_cast<Instruction>(U.getUser())) {
      ValueDFS VD;
      // Put the phi node uses in the incoming block.
      BasicBlock *IBlock;
      if (auto *PN = dyn_cast<PHINode>(I)) {
        IBlock = PN->getIncomingBlock(U);
        // Make phi node users appear last in the incoming block
        // they are from.
        VD.LocalNum = LN_Last;
      } else {
        // If it's not a phi node use, it is somewhere in the middle of the
        // block.
        IBlock = I->getParent();
        VD.LocalNum = LN_Middle;
      }
      DomTreeNode *DomNode = DT.getNode(IBlock);
      // It's possible our use is in an unreachable block. Skip it if so.
      if (!DomNode)
        continue;
      VD.DFSIn = DomNode->getDFSNumIn();
      VD.DFSOut = DomNode->getDFSNumOut();
      VD.U = &U;
      DFSOrderedSet.push_back(VD);
    }
  }
}

bool shouldRename(Value *V) {
  // Only want real values, not constants.  Additionally, operands with one use
  // are only being used in the comparison, which means they will not be useful
  // for us to consider for predicateinfo.
  return (isa<Instruction>(V) || isa<Argument>(V)) && !V->hasOneUse();
}

// Collect relevant operations from Comparison that we may want to insert copies
// for.
void collectCmpOps(CmpInst *Comparison, SmallVectorImpl<Value *> &CmpOperands) {
  auto *Op0 = Comparison->getOperand(0);
  auto *Op1 = Comparison->getOperand(1);
  if (Op0 == Op1)
    return;

  CmpOperands.push_back(Op0);
  CmpOperands.push_back(Op1);
}

// Add Op, PB to the list of value infos for Op, and mark Op to be renamed.
void PredicateInfoBuilder::addInfoFor(SmallVectorImpl<Value *> &OpsToRename,
                                      Value *Op, PredicateBase *PB) {
  auto &OperandInfo = getOrCreateValueInfo(Op);
  if (OperandInfo.Infos.empty())
    OpsToRename.push_back(Op);
  PI.AllInfos.push_back(PB);
  OperandInfo.Infos.push_back(PB);
}

// Process an assume instruction and place relevant operations we want to rename
// into OpsToRename.
void PredicateInfoBuilder::processAssume(
    IntrinsicInst *II, BasicBlock *AssumeBB,
    SmallVectorImpl<Value *> &OpsToRename) {
  SmallVector<Value *, 4> Worklist;
  SmallPtrSet<Value *, 4> Visited;
  Worklist.push_back(II->getOperand(0));
  while (!Worklist.empty()) {
    Value *Cond = Worklist.pop_back_val();
    if (!Visited.insert(Cond).second)
      continue;
    if (Visited.size() > MaxCondsPerBranch)
      break;

    Value *Op0, *Op1;
    if (match(Cond, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))) {
      Worklist.push_back(Op1);
      Worklist.push_back(Op0);
    }

    SmallVector<Value *, 4> Values;
    Values.push_back(Cond);
    if (auto *Cmp = dyn_cast<CmpInst>(Cond))
      collectCmpOps(Cmp, Values);

    for (Value *V : Values) {
      if (shouldRename(V)) {
        auto *PA = new PredicateAssume(V, II, Cond);
        addInfoFor(OpsToRename, V, PA);
      }
    }
  }
}

// Process a block terminating branch, and place relevant operations to be
// renamed into OpsToRename.
void PredicateInfoBuilder::processBranch(
    BranchInst *BI, BasicBlock *BranchBB,
    SmallVectorImpl<Value *> &OpsToRename) {
  BasicBlock *FirstBB = BI->getSuccessor(0);
  BasicBlock *SecondBB = BI->getSuccessor(1);

  for (BasicBlock *Succ : {FirstBB, SecondBB}) {
    bool TakenEdge = Succ == FirstBB;
    // Don't try to insert on a self-edge. This is mainly because we will
    // eliminate during renaming anyway.
    if (Succ == BranchBB)
      continue;

    SmallVector<Value *, 4> Worklist;
    SmallPtrSet<Value *, 4> Visited;
    Worklist.push_back(BI->getCondition());
    while (!Worklist.empty()) {
      Value *Cond = Worklist.pop_back_val();
      if (!Visited.insert(Cond).second)
        continue;
      if (Visited.size() > MaxCondsPerBranch)
        break;

      Value *Op0, *Op1;
      if (TakenEdge ? match(Cond, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))
                    : match(Cond, m_LogicalOr(m_Value(Op0), m_Value(Op1)))) {
        Worklist.push_back(Op1);
        Worklist.push_back(Op0);
      }

      SmallVector<Value *, 4> Values;
      Values.push_back(Cond);
      if (auto *Cmp = dyn_cast<CmpInst>(Cond))
        collectCmpOps(Cmp, Values);

      for (Value *V : Values) {
        if (shouldRename(V)) {
          PredicateBase *PB =
              new PredicateBranch(V, BranchBB, Succ, Cond, TakenEdge);
          addInfoFor(OpsToRename, V, PB);
          if (!Succ->getSinglePredecessor())
            EdgeUsesOnly.insert({BranchBB, Succ});
        }
      }
    }
  }
}
// Process a block terminating switch, and place relevant operations to be
// renamed into OpsToRename.
void PredicateInfoBuilder::processSwitch(
    SwitchInst *SI, BasicBlock *BranchBB,
    SmallVectorImpl<Value *> &OpsToRename) {
  Value *Op = SI->getCondition();
  if ((!isa<Instruction>(Op) && !isa<Argument>(Op)) || Op->hasOneUse())
    return;

  // Remember how many outgoing edges there are to every successor.
  SmallDenseMap<BasicBlock *, unsigned, 16> SwitchEdges;
  for (BasicBlock *TargetBlock : successors(BranchBB))
    ++SwitchEdges[TargetBlock];

  // Now propagate info for each case value
  for (auto C : SI->cases()) {
    BasicBlock *TargetBlock = C.getCaseSuccessor();
    if (SwitchEdges.lookup(TargetBlock) == 1) {
      PredicateSwitch *PS = new PredicateSwitch(
          Op, SI->getParent(), TargetBlock, C.getCaseValue(), SI);
      addInfoFor(OpsToRename, Op, PS);
      if (!TargetBlock->getSinglePredecessor())
        EdgeUsesOnly.insert({BranchBB, TargetBlock});
    }
  }
}

// Build predicate info for our function
void PredicateInfoBuilder::buildPredicateInfo() {
  DT.updateDFSNumbers();
  // Collect operands to rename from all conditional branch terminators, as well
  // as assume statements.
  SmallVector<Value *, 8> OpsToRename;
  for (auto *DTN : depth_first(DT.getRootNode())) {
    BasicBlock *BranchBB = DTN->getBlock();
    if (auto *BI = dyn_cast<BranchInst>(BranchBB->getTerminator())) {
      if (!BI->isConditional())
        continue;
      // Can't insert conditional information if they all go to the same place.
      if (BI->getSuccessor(0) == BI->getSuccessor(1))
        continue;
      processBranch(BI, BranchBB, OpsToRename);
    } else if (auto *SI = dyn_cast<SwitchInst>(BranchBB->getTerminator())) {
      processSwitch(SI, BranchBB, OpsToRename);
    }
  }
  for (auto &Assume : AC.assumptions()) {
    if (auto *II = dyn_cast_or_null<IntrinsicInst>(Assume))
      if (DT.isReachableFromEntry(II->getParent()))
        processAssume(II, II->getParent(), OpsToRename);
  }
  // Now rename all our operations.
  renameUses(OpsToRename);
}

// Given the renaming stack, make all the operands currently on the stack real
// by inserting them into the IR.  Return the last operation's value.
Value *PredicateInfoBuilder::materializeStack(unsigned int &Counter,
                                             ValueDFSStack &RenameStack,
                                             Value *OrigOp) {
  // Find the first thing we have to materialize
  auto RevIter = RenameStack.rbegin();
  for (; RevIter != RenameStack.rend(); ++RevIter)
    if (RevIter->Def)
      break;

  size_t Start = RevIter - RenameStack.rbegin();
  // The maximum number of things we should be trying to materialize at once
  // right now is 4, depending on if we had an assume, a branch, and both used
  // and of conditions.
  for (auto RenameIter = RenameStack.end() - Start;
       RenameIter != RenameStack.end(); ++RenameIter) {
    auto *Op =
        RenameIter == RenameStack.begin() ? OrigOp : (RenameIter - 1)->Def;
    ValueDFS &Result = *RenameIter;
    auto *ValInfo = Result.PInfo;
    ValInfo->RenamedOp = (RenameStack.end() - Start) == RenameStack.begin()
                             ? OrigOp
                             : (RenameStack.end() - Start - 1)->Def;
    // For edge predicates, we can just place the operand in the block before
    // the terminator.  For assume, we have to place it right before the assume
    // to ensure we dominate all of our uses.  Always insert right before the
    // relevant instruction (terminator, assume), so that we insert in proper
    // order in the case of multiple predicateinfo in the same block.
    // The number of named values is used to detect if a new declaration was
    // added. If so, that declaration is tracked so that it can be removed when
    // the analysis is done. The corner case were a new declaration results in
    // a name clash and the old name being renamed is not considered as that
    // represents an invalid module.
    if (isa<PredicateWithEdge>(ValInfo)) {
      IRBuilder<> B(getBranchTerminator(ValInfo));
      auto NumDecls = F.getParent()->getNumNamedValues();
      Function *IF = Intrinsic::getDeclaration(
          F.getParent(), Intrinsic::ssa_copy, Op->getType());
      if (NumDecls != F.getParent()->getNumNamedValues())
        PI.CreatedDeclarations.insert(IF);
      CallInst *PIC =
          B.CreateCall(IF, Op, Op->getName() + "." + Twine(Counter++));
      PI.PredicateMap.insert({PIC, ValInfo});
      Result.Def = PIC;
    } else {
      auto *PAssume = dyn_cast<PredicateAssume>(ValInfo);
      assert(PAssume &&
             "Should not have gotten here without it being an assume");
      // Insert the predicate directly after the assume. While it also holds
      // directly before it, assume(i1 true) is not a useful fact.
      IRBuilder<> B(PAssume->AssumeInst->getNextNode());
      auto NumDecls = F.getParent()->getNumNamedValues();
      Function *IF = Intrinsic::getDeclaration(
          F.getParent(), Intrinsic::ssa_copy, Op->getType());
      if (NumDecls != F.getParent()->getNumNamedValues())
        PI.CreatedDeclarations.insert(IF);
      CallInst *PIC = B.CreateCall(IF, Op);
      PI.PredicateMap.insert({PIC, ValInfo});
      Result.Def = PIC;
    }
  }
  return RenameStack.back().Def;
}

// Instead of the standard SSA renaming algorithm, which is O(Number of
// instructions), and walks the entire dominator tree, we walk only the defs +
// uses.  The standard SSA renaming algorithm does not really rely on the
// dominator tree except to order the stack push/pops of the renaming stacks, so
// that defs end up getting pushed before hitting the correct uses.  This does
// not require the dominator tree, only the *order* of the dominator tree. The
// complete and correct ordering of the defs and uses, in dominator tree is
// contained in the DFS numbering of the dominator tree. So we sort the defs and
// uses into the DFS ordering, and then just use the renaming stack as per
// normal, pushing when we hit a def (which is a predicateinfo instruction),
// popping when we are out of the dfs scope for that def, and replacing any uses
// with top of stack if it exists.  In order to handle liveness without
// propagating liveness info, we don't actually insert the predicateinfo
// instruction def until we see a use that it would dominate.  Once we see such
// a use, we materialize the predicateinfo instruction in the right place and
// use it.
//
// TODO: Use this algorithm to perform fast single-variable renaming in
// promotememtoreg and memoryssa.
void PredicateInfoBuilder::renameUses(SmallVectorImpl<Value *> &OpsToRename) {
  ValueDFS_Compare Compare(DT);
  // Compute liveness, and rename in O(uses) per Op.
  for (auto *Op : OpsToRename) {
    LLVM_DEBUG(dbgs() << "Visiting " << *Op << "\n");
    unsigned Counter = 0;
    SmallVector<ValueDFS, 16> OrderedUses;
    const auto &ValueInfo = getValueInfo(Op);
    // Insert the possible copies into the def/use list.
    // They will become real copies if we find a real use for them, and never
    // created otherwise.
    for (const auto &PossibleCopy : ValueInfo.Infos) {
      ValueDFS VD;
      // Determine where we are going to place the copy by the copy type.
      // The predicate info for branches always come first, they will get
      // materialized in the split block at the top of the block.
      // The predicate info for assumes will be somewhere in the middle,
      // it will get materialized in front of the assume.
      if (const auto *PAssume = dyn_cast<PredicateAssume>(PossibleCopy)) {
        VD.LocalNum = LN_Middle;
        DomTreeNode *DomNode = DT.getNode(PAssume->AssumeInst->getParent());
        if (!DomNode)
          continue;
        VD.DFSIn = DomNode->getDFSNumIn();
        VD.DFSOut = DomNode->getDFSNumOut();
        VD.PInfo = PossibleCopy;
        OrderedUses.push_back(VD);
      } else if (isa<PredicateWithEdge>(PossibleCopy)) {
        // If we can only do phi uses, we treat it like it's in the branch
        // block, and handle it specially. We know that it goes last, and only
        // dominate phi uses.
        auto BlockEdge = getBlockEdge(PossibleCopy);
        if (EdgeUsesOnly.count(BlockEdge)) {
          VD.LocalNum = LN_Last;
          auto *DomNode = DT.getNode(BlockEdge.first);
          if (DomNode) {
            VD.DFSIn = DomNode->getDFSNumIn();
            VD.DFSOut = DomNode->getDFSNumOut();
            VD.PInfo = PossibleCopy;
            VD.EdgeOnly = true;
            OrderedUses.push_back(VD);
          }
        } else {
          // Otherwise, we are in the split block (even though we perform
          // insertion in the branch block).
          // Insert a possible copy at the split block and before the branch.
          VD.LocalNum = LN_First;
          auto *DomNode = DT.getNode(BlockEdge.second);
          if (DomNode) {
            VD.DFSIn = DomNode->getDFSNumIn();
            VD.DFSOut = DomNode->getDFSNumOut();
            VD.PInfo = PossibleCopy;
            OrderedUses.push_back(VD);
          }
        }
      }
    }

    convertUsesToDFSOrdered(Op, OrderedUses);
    // Here we require a stable sort because we do not bother to try to
    // assign an order to the operands the uses represent. Thus, two
    // uses in the same instruction do not have a strict sort order
    // currently and will be considered equal. We could get rid of the
    // stable sort by creating one if we wanted.
    llvm::stable_sort(OrderedUses, Compare);
    SmallVector<ValueDFS, 8> RenameStack;
    // For each use, sorted into dfs order, push values and replaces uses with
    // top of stack, which will represent the reaching def.
    for (auto &VD : OrderedUses) {
      // We currently do not materialize copy over copy, but we should decide if
      // we want to.
      bool PossibleCopy = VD.PInfo != nullptr;
      if (RenameStack.empty()) {
        LLVM_DEBUG(dbgs() << "Rename Stack is empty\n");
      } else {
        LLVM_DEBUG(dbgs() << "Rename Stack Top DFS numbers are ("
                          << RenameStack.back().DFSIn << ","
                          << RenameStack.back().DFSOut << ")\n");
      }

      LLVM_DEBUG(dbgs() << "Current DFS numbers are (" << VD.DFSIn << ","
                        << VD.DFSOut << ")\n");

      bool ShouldPush = (VD.Def || PossibleCopy);
      bool OutOfScope = !stackIsInScope(RenameStack, VD);
      if (OutOfScope || ShouldPush) {
        // Sync to our current scope.
        popStackUntilDFSScope(RenameStack, VD);
        if (ShouldPush) {
          RenameStack.push_back(VD);
        }
      }
      // If we get to this point, and the stack is empty we must have a use
      // with no renaming needed, just skip it.
      if (RenameStack.empty())
        continue;
      // Skip values, only want to rename the uses
      if (VD.Def || PossibleCopy)
        continue;
      if (!DebugCounter::shouldExecute(RenameCounter)) {
        LLVM_DEBUG(dbgs() << "Skipping execution due to debug counter\n");
        continue;
      }
      ValueDFS &Result = RenameStack.back();

      // If the possible copy dominates something, materialize our stack up to
      // this point. This ensures every comparison that affects our operation
      // ends up with predicateinfo.
      if (!Result.Def)
        Result.Def = materializeStack(Counter, RenameStack, Op);

      LLVM_DEBUG(dbgs() << "Found replacement " << *Result.Def << " for "
                        << *VD.U->get() << " in " << *(VD.U->getUser())
                        << "\n");
      assert(DT.dominates(cast<Instruction>(Result.Def), *VD.U) &&
             "Predicateinfo def should have dominated this use");
      VD.U->set(Result.Def);
    }
  }
}

PredicateInfoBuilder::ValueInfo &
PredicateInfoBuilder::getOrCreateValueInfo(Value *Operand) {
  auto OIN = ValueInfoNums.find(Operand);
  if (OIN == ValueInfoNums.end()) {
    // This will grow it
    ValueInfos.resize(ValueInfos.size() + 1);
    // This will use the new size and give us a 0 based number of the info
    auto InsertResult = ValueInfoNums.insert({Operand, ValueInfos.size() - 1});
    assert(InsertResult.second && "Value info number already existed?");
    return ValueInfos[InsertResult.first->second];
  }
  return ValueInfos[OIN->second];
}

const PredicateInfoBuilder::ValueInfo &
PredicateInfoBuilder::getValueInfo(Value *Operand) const {
  auto OINI = ValueInfoNums.lookup(Operand);
  assert(OINI != 0 && "Operand was not really in the Value Info Numbers");
  assert(OINI < ValueInfos.size() &&
         "Value Info Number greater than size of Value Info Table");
  return ValueInfos[OINI];
}

PredicateInfo::PredicateInfo(Function &F, DominatorTree &DT,
                             AssumptionCache &AC)
    : F(F) {
  PredicateInfoBuilder Builder(*this, F, DT, AC);
  Builder.buildPredicateInfo();
}

// Remove all declarations we created . The PredicateInfo consumers are
// responsible for remove the ssa_copy calls created.
PredicateInfo::~PredicateInfo() {
  // Collect function pointers in set first, as SmallSet uses a SmallVector
  // internally and we have to remove the asserting value handles first.
  SmallPtrSet<Function *, 20> FunctionPtrs;
  for (const auto &F : CreatedDeclarations)
    FunctionPtrs.insert(&*F);
  CreatedDeclarations.clear();

  for (Function *F : FunctionPtrs) {
    assert(F->user_begin() == F->user_end() &&
           "PredicateInfo consumer did not remove all SSA copies.");
    F->eraseFromParent();
  }
}

std::optional<PredicateConstraint> PredicateBase::getConstraint() const {
  switch (Type) {
  case PT_Assume:
  case PT_Branch: {
    bool TrueEdge = true;
    if (auto *PBranch = dyn_cast<PredicateBranch>(this))
      TrueEdge = PBranch->TrueEdge;

    if (Condition == RenamedOp) {
      return {{CmpInst::ICMP_EQ,
               TrueEdge ? ConstantInt::getTrue(Condition->getType())
                        : ConstantInt::getFalse(Condition->getType())}};
    }

    CmpInst *Cmp = dyn_cast<CmpInst>(Condition);
    if (!Cmp) {
      // TODO: Make this an assertion once RenamedOp is fully accurate.
      return std::nullopt;
    }

    CmpInst::Predicate Pred;
    Value *OtherOp;
    if (Cmp->getOperand(0) == RenamedOp) {
      Pred = Cmp->getPredicate();
      OtherOp = Cmp->getOperand(1);
    } else if (Cmp->getOperand(1) == RenamedOp) {
      Pred = Cmp->getSwappedPredicate();
      OtherOp = Cmp->getOperand(0);
    } else {
      // TODO: Make this an assertion once RenamedOp is fully accurate.
      return std::nullopt;
    }

    // Invert predicate along false edge.
    if (!TrueEdge)
      Pred = CmpInst::getInversePredicate(Pred);

    return {{Pred, OtherOp}};
  }
  case PT_Switch:
    if (Condition != RenamedOp) {
      // TODO: Make this an assertion once RenamedOp is fully accurate.
      return std::nullopt;
    }

    return {{CmpInst::ICMP_EQ, cast<PredicateSwitch>(this)->CaseValue}};
  }
  llvm_unreachable("Unknown predicate type");
}

void PredicateInfo::verifyPredicateInfo() const {}

// Replace ssa_copy calls created by PredicateInfo with their operand.
static void replaceCreatedSSACopys(PredicateInfo &PredInfo, Function &F) {
  for (Instruction &Inst : llvm::make_early_inc_range(instructions(F))) {
    const auto *PI = PredInfo.getPredicateInfoFor(&Inst);
    auto *II = dyn_cast<IntrinsicInst>(&Inst);
    if (!PI || !II || II->getIntrinsicID() != Intrinsic::ssa_copy)
      continue;

    Inst.replaceAllUsesWith(II->getOperand(0));
    Inst.eraseFromParent();
  }
}

PreservedAnalyses PredicateInfoPrinterPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  OS << "PredicateInfo for function: " << F.getName() << "\n";
  auto PredInfo = std::make_unique<PredicateInfo>(F, DT, AC);
  PredInfo->print(OS);

  replaceCreatedSSACopys(*PredInfo, F);
  return PreservedAnalyses::all();
}

/// An assembly annotator class to print PredicateInfo information in
/// comments.
class PredicateInfoAnnotatedWriter : public AssemblyAnnotationWriter {
  friend class PredicateInfo;
  const PredicateInfo *PredInfo;

public:
  PredicateInfoAnnotatedWriter(const PredicateInfo *M) : PredInfo(M) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {}

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    if (const auto *PI = PredInfo->getPredicateInfoFor(I)) {
      OS << "; Has predicate info\n";
      if (const auto *PB = dyn_cast<PredicateBranch>(PI)) {
        OS << "; branch predicate info { TrueEdge: " << PB->TrueEdge
           << " Comparison:" << *PB->Condition << " Edge: [";
        PB->From->printAsOperand(OS);
        OS << ",";
        PB->To->printAsOperand(OS);
        OS << "]";
      } else if (const auto *PS = dyn_cast<PredicateSwitch>(PI)) {
        OS << "; switch predicate info { CaseValue: " << *PS->CaseValue
           << " Switch:" << *PS->Switch << " Edge: [";
        PS->From->printAsOperand(OS);
        OS << ",";
        PS->To->printAsOperand(OS);
        OS << "]";
      } else if (const auto *PA = dyn_cast<PredicateAssume>(PI)) {
        OS << "; assume predicate info {"
           << " Comparison:" << *PA->Condition;
      }
      OS << ", RenamedOp: ";
      PI->RenamedOp->printAsOperand(OS, false);
      OS << " }\n";
    }
  }
};

void PredicateInfo::print(raw_ostream &OS) const {
  PredicateInfoAnnotatedWriter Writer(this);
  F.print(OS, &Writer);
}

void PredicateInfo::dump() const {
  PredicateInfoAnnotatedWriter Writer(this);
  F.print(dbgs(), &Writer);
}

PreservedAnalyses PredicateInfoVerifierPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  std::make_unique<PredicateInfo>(F, DT, AC)->verifyPredicateInfo();

  return PreservedAnalyses::all();
}
}
