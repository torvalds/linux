//===- DFAJumpThreading.cpp - Threads a switch statement inside a loop ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Transform each threading path to effectively jump thread the DFA. For
// example, the CFG below could be transformed as follows, where the cloned
// blocks unconditionally branch to the next correct case based on what is
// identified in the analysis.
//
//          sw.bb                        sw.bb
//        /   |   \                    /   |   \
//   case1  case2  case3          case1  case2  case3
//        \   |   /                 |      |      |
//       determinator            det.2   det.3  det.1
//        br sw.bb                /        |        \
//                          sw.bb.2     sw.bb.3     sw.bb.1
//                           br case2    br case3    br case1§
//
// Definitions and Terminology:
//
// * Threading path:
//   a list of basic blocks, the exit state, and the block that determines
//   the next state, for which the following notation will be used:
//   < path of BBs that form a cycle > [ state, determinator ]
//
// * Predictable switch:
//   The switch variable is always a known constant so that all conditional
//   jumps based on switch variable can be converted to unconditional jump.
//
// * Determinator:
//   The basic block that determines the next state of the DFA.
//
// Representing the optimization in C-like pseudocode: the code pattern on the
// left could functionally be transformed to the right pattern if the switch
// condition is predictable.
//
//  X = A                       goto A
//  for (...)                   A:
//    switch (X)                  ...
//      case A                    goto B
//        X = B                 B:
//      case B                    ...
//        X = C                   goto C
//
// The pass first checks that switch variable X is decided by the control flow
// path taken in the loop; for example, in case B, the next value of X is
// decided to be C. It then enumerates through all paths in the loop and labels
// the basic blocks where the next state is decided.
//
// Using this information it creates new paths that unconditionally branch to
// the next case. This involves cloning code, so it only gets triggered if the
// amount of code duplicated is below a threshold.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/DFAJumpThreading.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SSAUpdaterBulk.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <deque>

#ifdef EXPENSIVE_CHECKS
#include "llvm/IR/Verifier.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "dfa-jump-threading"

STATISTIC(NumTransforms, "Number of transformations done");
STATISTIC(NumCloned, "Number of blocks cloned");
STATISTIC(NumPaths, "Number of individual paths threaded");

static cl::opt<bool>
    ClViewCfgBefore("dfa-jump-view-cfg-before",
                    cl::desc("View the CFG before DFA Jump Threading"),
                    cl::Hidden, cl::init(false));

static cl::opt<bool> EarlyExitHeuristic(
    "dfa-early-exit-heuristic",
    cl::desc("Exit early if an unpredictable value come from the same loop"),
    cl::Hidden, cl::init(true));

static cl::opt<unsigned> MaxPathLength(
    "dfa-max-path-length",
    cl::desc("Max number of blocks searched to find a threading path"),
    cl::Hidden, cl::init(20));

static cl::opt<unsigned>
    MaxNumPaths("dfa-max-num-paths",
                cl::desc("Max number of paths enumerated around a switch"),
                cl::Hidden, cl::init(200));

static cl::opt<unsigned>
    CostThreshold("dfa-cost-threshold",
                  cl::desc("Maximum cost accepted for the transformation"),
                  cl::Hidden, cl::init(50));

namespace {

class SelectInstToUnfold {
  SelectInst *SI;
  PHINode *SIUse;

public:
  SelectInstToUnfold(SelectInst *SI, PHINode *SIUse) : SI(SI), SIUse(SIUse) {}

  SelectInst *getInst() { return SI; }
  PHINode *getUse() { return SIUse; }

  explicit operator bool() const { return SI && SIUse; }
};

void unfold(DomTreeUpdater *DTU, LoopInfo *LI, SelectInstToUnfold SIToUnfold,
            std::vector<SelectInstToUnfold> *NewSIsToUnfold,
            std::vector<BasicBlock *> *NewBBs);

class DFAJumpThreading {
public:
  DFAJumpThreading(AssumptionCache *AC, DominatorTree *DT, LoopInfo *LI,
                   TargetTransformInfo *TTI, OptimizationRemarkEmitter *ORE)
      : AC(AC), DT(DT), LI(LI), TTI(TTI), ORE(ORE) {}

  bool run(Function &F);
  bool LoopInfoBroken;

private:
  void
  unfoldSelectInstrs(DominatorTree *DT,
                     const SmallVector<SelectInstToUnfold, 4> &SelectInsts) {
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
    SmallVector<SelectInstToUnfold, 4> Stack;
    for (SelectInstToUnfold SIToUnfold : SelectInsts)
      Stack.push_back(SIToUnfold);

    while (!Stack.empty()) {
      SelectInstToUnfold SIToUnfold = Stack.pop_back_val();

      std::vector<SelectInstToUnfold> NewSIsToUnfold;
      std::vector<BasicBlock *> NewBBs;
      unfold(&DTU, LI, SIToUnfold, &NewSIsToUnfold, &NewBBs);

      // Put newly discovered select instructions into the work list.
      for (const SelectInstToUnfold &NewSIToUnfold : NewSIsToUnfold)
        Stack.push_back(NewSIToUnfold);
    }
  }

  AssumptionCache *AC;
  DominatorTree *DT;
  LoopInfo *LI;
  TargetTransformInfo *TTI;
  OptimizationRemarkEmitter *ORE;
};

} // end anonymous namespace

namespace {

/// Create a new basic block and sink \p SIToSink into it.
void createBasicBlockAndSinkSelectInst(
    DomTreeUpdater *DTU, SelectInst *SI, PHINode *SIUse, SelectInst *SIToSink,
    BasicBlock *EndBlock, StringRef NewBBName, BasicBlock **NewBlock,
    BranchInst **NewBranch, std::vector<SelectInstToUnfold> *NewSIsToUnfold,
    std::vector<BasicBlock *> *NewBBs) {
  assert(SIToSink->hasOneUse());
  assert(NewBlock);
  assert(NewBranch);
  *NewBlock = BasicBlock::Create(SI->getContext(), NewBBName,
                                 EndBlock->getParent(), EndBlock);
  NewBBs->push_back(*NewBlock);
  *NewBranch = BranchInst::Create(EndBlock, *NewBlock);
  SIToSink->moveBefore(*NewBranch);
  NewSIsToUnfold->push_back(SelectInstToUnfold(SIToSink, SIUse));
  DTU->applyUpdates({{DominatorTree::Insert, *NewBlock, EndBlock}});
}

/// Unfold the select instruction held in \p SIToUnfold by replacing it with
/// control flow.
///
/// Put newly discovered select instructions into \p NewSIsToUnfold. Put newly
/// created basic blocks into \p NewBBs.
///
/// TODO: merge it with CodeGenPrepare::optimizeSelectInst() if possible.
void unfold(DomTreeUpdater *DTU, LoopInfo *LI, SelectInstToUnfold SIToUnfold,
            std::vector<SelectInstToUnfold> *NewSIsToUnfold,
            std::vector<BasicBlock *> *NewBBs) {
  SelectInst *SI = SIToUnfold.getInst();
  PHINode *SIUse = SIToUnfold.getUse();
  BasicBlock *StartBlock = SI->getParent();
  BasicBlock *EndBlock = SIUse->getParent();
  BranchInst *StartBlockTerm =
      dyn_cast<BranchInst>(StartBlock->getTerminator());

  assert(StartBlockTerm && StartBlockTerm->isUnconditional());
  assert(SI->hasOneUse());

  // These are the new basic blocks for the conditional branch.
  // At least one will become an actual new basic block.
  BasicBlock *TrueBlock = nullptr;
  BasicBlock *FalseBlock = nullptr;
  BranchInst *TrueBranch = nullptr;
  BranchInst *FalseBranch = nullptr;

  // Sink select instructions to be able to unfold them later.
  if (SelectInst *SIOp = dyn_cast<SelectInst>(SI->getTrueValue())) {
    createBasicBlockAndSinkSelectInst(DTU, SI, SIUse, SIOp, EndBlock,
                                      "si.unfold.true", &TrueBlock, &TrueBranch,
                                      NewSIsToUnfold, NewBBs);
  }
  if (SelectInst *SIOp = dyn_cast<SelectInst>(SI->getFalseValue())) {
    createBasicBlockAndSinkSelectInst(DTU, SI, SIUse, SIOp, EndBlock,
                                      "si.unfold.false", &FalseBlock,
                                      &FalseBranch, NewSIsToUnfold, NewBBs);
  }

  // If there was nothing to sink, then arbitrarily choose the 'false' side
  // for a new input value to the PHI.
  if (!TrueBlock && !FalseBlock) {
    FalseBlock = BasicBlock::Create(SI->getContext(), "si.unfold.false",
                                    EndBlock->getParent(), EndBlock);
    NewBBs->push_back(FalseBlock);
    BranchInst::Create(EndBlock, FalseBlock);
    DTU->applyUpdates({{DominatorTree::Insert, FalseBlock, EndBlock}});
  }

  // Insert the real conditional branch based on the original condition.
  // If we did not create a new block for one of the 'true' or 'false' paths
  // of the condition, it means that side of the branch goes to the end block
  // directly and the path originates from the start block from the point of
  // view of the new PHI.
  BasicBlock *TT = EndBlock;
  BasicBlock *FT = EndBlock;
  if (TrueBlock && FalseBlock) {
    // A diamond.
    TT = TrueBlock;
    FT = FalseBlock;

    // Update the phi node of SI.
    SIUse->addIncoming(SI->getTrueValue(), TrueBlock);
    SIUse->addIncoming(SI->getFalseValue(), FalseBlock);

    // Update any other PHI nodes in EndBlock.
    for (PHINode &Phi : EndBlock->phis()) {
      if (&Phi != SIUse) {
        Value *OrigValue = Phi.getIncomingValueForBlock(StartBlock);
        Phi.addIncoming(OrigValue, TrueBlock);
        Phi.addIncoming(OrigValue, FalseBlock);
      }

      // Remove incoming place of original StartBlock, which comes in a indirect
      // way (through TrueBlock and FalseBlock) now.
      Phi.removeIncomingValue(StartBlock, /* DeletePHIIfEmpty = */ false);
    }
  } else {
    BasicBlock *NewBlock = nullptr;
    Value *SIOp1 = SI->getTrueValue();
    Value *SIOp2 = SI->getFalseValue();

    // A triangle pointing right.
    if (!TrueBlock) {
      NewBlock = FalseBlock;
      FT = FalseBlock;
    }
    // A triangle pointing left.
    else {
      NewBlock = TrueBlock;
      TT = TrueBlock;
      std::swap(SIOp1, SIOp2);
    }

    // Update the phi node of SI.
    for (unsigned Idx = 0; Idx < SIUse->getNumIncomingValues(); ++Idx) {
      if (SIUse->getIncomingBlock(Idx) == StartBlock)
        SIUse->setIncomingValue(Idx, SIOp1);
    }
    SIUse->addIncoming(SIOp2, NewBlock);

    // Update any other PHI nodes in EndBlock.
    for (auto II = EndBlock->begin(); PHINode *Phi = dyn_cast<PHINode>(II);
         ++II) {
      if (Phi != SIUse)
        Phi->addIncoming(Phi->getIncomingValueForBlock(StartBlock), NewBlock);
    }
  }
  StartBlockTerm->eraseFromParent();
  BranchInst::Create(TT, FT, SI->getCondition(), StartBlock);
  DTU->applyUpdates({{DominatorTree::Insert, StartBlock, TT},
                     {DominatorTree::Insert, StartBlock, FT}});

  // Preserve loop info
  if (Loop *L = LI->getLoopFor(SI->getParent())) {
    for (BasicBlock *NewBB : *NewBBs)
      L->addBasicBlockToLoop(NewBB, *LI);
  }

  // The select is now dead.
  assert(SI->use_empty() && "Select must be dead now");
  SI->eraseFromParent();
}

struct ClonedBlock {
  BasicBlock *BB;
  APInt State; ///< \p State corresponds to the next value of a switch stmnt.
};

typedef std::deque<BasicBlock *> PathType;
typedef std::vector<PathType> PathsType;
typedef SmallPtrSet<const BasicBlock *, 8> VisitedBlocks;
typedef std::vector<ClonedBlock> CloneList;

// This data structure keeps track of all blocks that have been cloned.  If two
// different ThreadingPaths clone the same block for a certain state it should
// be reused, and it can be looked up in this map.
typedef DenseMap<BasicBlock *, CloneList> DuplicateBlockMap;

// This map keeps track of all the new definitions for an instruction. This
// information is needed when restoring SSA form after cloning blocks.
typedef MapVector<Instruction *, std::vector<Instruction *>> DefMap;

inline raw_ostream &operator<<(raw_ostream &OS, const PathType &Path) {
  OS << "< ";
  for (const BasicBlock *BB : Path) {
    std::string BBName;
    if (BB->hasName())
      raw_string_ostream(BBName) << BB->getName();
    else
      raw_string_ostream(BBName) << BB;
    OS << BBName << " ";
  }
  OS << ">";
  return OS;
}

/// ThreadingPath is a path in the control flow of a loop that can be threaded
/// by cloning necessary basic blocks and replacing conditional branches with
/// unconditional ones. A threading path includes a list of basic blocks, the
/// exit state, and the block that determines the next state.
struct ThreadingPath {
  /// Exit value is DFA's exit state for the given path.
  APInt getExitValue() const { return ExitVal; }
  void setExitValue(const ConstantInt *V) {
    ExitVal = V->getValue();
    IsExitValSet = true;
  }
  bool isExitValueSet() const { return IsExitValSet; }

  /// Determinator is the basic block that determines the next state of the DFA.
  const BasicBlock *getDeterminatorBB() const { return DBB; }
  void setDeterminator(const BasicBlock *BB) { DBB = BB; }

  /// Path is a list of basic blocks.
  const PathType &getPath() const { return Path; }
  void setPath(const PathType &NewPath) { Path = NewPath; }

  void print(raw_ostream &OS) const {
    OS << Path << " [ " << ExitVal << ", " << DBB->getName() << " ]";
  }

private:
  PathType Path;
  APInt ExitVal;
  const BasicBlock *DBB = nullptr;
  bool IsExitValSet = false;
};

#ifndef NDEBUG
inline raw_ostream &operator<<(raw_ostream &OS, const ThreadingPath &TPath) {
  TPath.print(OS);
  return OS;
}
#endif

struct MainSwitch {
  MainSwitch(SwitchInst *SI, LoopInfo *LI, OptimizationRemarkEmitter *ORE)
      : LI(LI) {
    if (isCandidate(SI)) {
      Instr = SI;
    } else {
      ORE->emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "SwitchNotPredictable", SI)
               << "Switch instruction is not predictable.";
      });
    }
  }

  virtual ~MainSwitch() = default;

  SwitchInst *getInstr() const { return Instr; }
  const SmallVector<SelectInstToUnfold, 4> getSelectInsts() {
    return SelectInsts;
  }

private:
  /// Do a use-def chain traversal starting from the switch condition to see if
  /// \p SI is a potential condidate.
  ///
  /// Also, collect select instructions to unfold.
  bool isCandidate(const SwitchInst *SI) {
    std::deque<std::pair<Value *, BasicBlock *>> Q;
    SmallSet<Value *, 16> SeenValues;
    SelectInsts.clear();

    Value *SICond = SI->getCondition();
    LLVM_DEBUG(dbgs() << "\tSICond: " << *SICond << "\n");
    if (!isa<PHINode>(SICond))
      return false;

    // The switch must be in a loop.
    const Loop *L = LI->getLoopFor(SI->getParent());
    if (!L)
      return false;

    addToQueue(SICond, nullptr, Q, SeenValues);

    while (!Q.empty()) {
      Value *Current = Q.front().first;
      BasicBlock *CurrentIncomingBB = Q.front().second;
      Q.pop_front();

      if (auto *Phi = dyn_cast<PHINode>(Current)) {
        for (BasicBlock *IncomingBB : Phi->blocks()) {
          Value *Incoming = Phi->getIncomingValueForBlock(IncomingBB);
          addToQueue(Incoming, IncomingBB, Q, SeenValues);
        }
        LLVM_DEBUG(dbgs() << "\tphi: " << *Phi << "\n");
      } else if (SelectInst *SelI = dyn_cast<SelectInst>(Current)) {
        if (!isValidSelectInst(SelI))
          return false;
        addToQueue(SelI->getTrueValue(), CurrentIncomingBB, Q, SeenValues);
        addToQueue(SelI->getFalseValue(), CurrentIncomingBB, Q, SeenValues);
        LLVM_DEBUG(dbgs() << "\tselect: " << *SelI << "\n");
        if (auto *SelIUse = dyn_cast<PHINode>(SelI->user_back()))
          SelectInsts.push_back(SelectInstToUnfold(SelI, SelIUse));
      } else if (isa<Constant>(Current)) {
        LLVM_DEBUG(dbgs() << "\tconst: " << *Current << "\n");
        continue;
      } else {
        LLVM_DEBUG(dbgs() << "\tother: " << *Current << "\n");
        // Allow unpredictable values. The hope is that those will be the
        // initial switch values that can be ignored (they will hit the
        // unthreaded switch) but this assumption will get checked later after
        // paths have been enumerated (in function getStateDefMap).

        // If the unpredictable value comes from the same inner loop it is
        // likely that it will also be on the enumerated paths, causing us to
        // exit after we have enumerated all the paths. This heuristic save
        // compile time because a search for all the paths can become expensive.
        if (EarlyExitHeuristic &&
            L->contains(LI->getLoopFor(CurrentIncomingBB))) {
          LLVM_DEBUG(dbgs()
                     << "\tExiting early due to unpredictability heuristic.\n");
          return false;
        }

        continue;
      }
    }

    return true;
  }

  void addToQueue(Value *Val, BasicBlock *BB,
                  std::deque<std::pair<Value *, BasicBlock *>> &Q,
                  SmallSet<Value *, 16> &SeenValues) {
    if (SeenValues.contains(Val))
      return;
    Q.push_back({Val, BB});
    SeenValues.insert(Val);
  }

  bool isValidSelectInst(SelectInst *SI) {
    if (!SI->hasOneUse())
      return false;

    Instruction *SIUse = dyn_cast<Instruction>(SI->user_back());
    // The use of the select inst should be either a phi or another select.
    if (!SIUse && !(isa<PHINode>(SIUse) || isa<SelectInst>(SIUse)))
      return false;

    BasicBlock *SIBB = SI->getParent();

    // Currently, we can only expand select instructions in basic blocks with
    // one successor.
    BranchInst *SITerm = dyn_cast<BranchInst>(SIBB->getTerminator());
    if (!SITerm || !SITerm->isUnconditional())
      return false;

    // Only fold the select coming from directly where it is defined.
    PHINode *PHIUser = dyn_cast<PHINode>(SIUse);
    if (PHIUser && PHIUser->getIncomingBlock(*SI->use_begin()) != SIBB)
      return false;

    // If select will not be sunk during unfolding, and it is in the same basic
    // block as another state defining select, then cannot unfold both.
    for (SelectInstToUnfold SIToUnfold : SelectInsts) {
      SelectInst *PrevSI = SIToUnfold.getInst();
      if (PrevSI->getTrueValue() != SI && PrevSI->getFalseValue() != SI &&
          PrevSI->getParent() == SI->getParent())
        return false;
    }

    return true;
  }

  LoopInfo *LI;
  SwitchInst *Instr = nullptr;
  SmallVector<SelectInstToUnfold, 4> SelectInsts;
};

struct AllSwitchPaths {
  AllSwitchPaths(const MainSwitch *MSwitch, OptimizationRemarkEmitter *ORE,
                 LoopInfo *LI)
      : Switch(MSwitch->getInstr()), SwitchBlock(Switch->getParent()), ORE(ORE),
        LI(LI) {}

  std::vector<ThreadingPath> &getThreadingPaths() { return TPaths; }
  unsigned getNumThreadingPaths() { return TPaths.size(); }
  SwitchInst *getSwitchInst() { return Switch; }
  BasicBlock *getSwitchBlock() { return SwitchBlock; }

  void run() {
    VisitedBlocks Visited;
    PathsType LoopPaths = paths(SwitchBlock, Visited, /* PathDepth = */ 1);
    StateDefMap StateDef = getStateDefMap(LoopPaths);

    if (StateDef.empty()) {
      ORE->emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "SwitchNotPredictable",
                                        Switch)
               << "Switch instruction is not predictable.";
      });
      return;
    }

    for (const PathType &Path : LoopPaths) {
      ThreadingPath TPath;

      const BasicBlock *PrevBB = Path.back();
      for (const BasicBlock *BB : Path) {
        if (StateDef.contains(BB)) {
          const PHINode *Phi = dyn_cast<PHINode>(StateDef[BB]);
          assert(Phi && "Expected a state-defining instr to be a phi node.");

          const Value *V = Phi->getIncomingValueForBlock(PrevBB);
          if (const ConstantInt *C = dyn_cast<const ConstantInt>(V)) {
            TPath.setExitValue(C);
            TPath.setDeterminator(BB);
            TPath.setPath(Path);
          }
        }

        // Switch block is the determinator, this is the final exit value.
        if (TPath.isExitValueSet() && BB == Path.front())
          break;

        PrevBB = BB;
      }

      if (TPath.isExitValueSet() && isSupported(TPath))
        TPaths.push_back(TPath);
    }
  }

private:
  // Value: an instruction that defines a switch state;
  // Key: the parent basic block of that instruction.
  typedef DenseMap<const BasicBlock *, const PHINode *> StateDefMap;

  PathsType paths(BasicBlock *BB, VisitedBlocks &Visited,
                  unsigned PathDepth) const {
    PathsType Res;

    // Stop exploring paths after visiting MaxPathLength blocks
    if (PathDepth > MaxPathLength) {
      ORE->emit([&]() {
        return OptimizationRemarkAnalysis(DEBUG_TYPE, "MaxPathLengthReached",
                                          Switch)
               << "Exploration stopped after visiting MaxPathLength="
               << ore::NV("MaxPathLength", MaxPathLength) << " blocks.";
      });
      return Res;
    }

    Visited.insert(BB);

    // Stop if we have reached the BB out of loop, since its successors have no
    // impact on the DFA.
    // TODO: Do we need to stop exploring if BB is the outer loop of the switch?
    if (!LI->getLoopFor(BB))
      return Res;

    // Some blocks have multiple edges to the same successor, and this set
    // is used to prevent a duplicate path from being generated
    SmallSet<BasicBlock *, 4> Successors;
    for (BasicBlock *Succ : successors(BB)) {
      if (!Successors.insert(Succ).second)
        continue;

      // Found a cycle through the SwitchBlock
      if (Succ == SwitchBlock) {
        Res.push_back({BB});
        continue;
      }

      // We have encountered a cycle, do not get caught in it
      if (Visited.contains(Succ))
        continue;

      PathsType SuccPaths = paths(Succ, Visited, PathDepth + 1);
      for (const PathType &Path : SuccPaths) {
        PathType NewPath(Path);
        NewPath.push_front(BB);
        Res.push_back(NewPath);
        if (Res.size() >= MaxNumPaths) {
          return Res;
        }
      }
    }
    // This block could now be visited again from a different predecessor. Note
    // that this will result in exponential runtime. Subpaths could possibly be
    // cached but it takes a lot of memory to store them.
    Visited.erase(BB);
    return Res;
  }

  /// Walk the use-def chain and collect all the state-defining instructions.
  ///
  /// Return an empty map if unpredictable values encountered inside the basic
  /// blocks of \p LoopPaths.
  StateDefMap getStateDefMap(const PathsType &LoopPaths) const {
    StateDefMap Res;

    // Basic blocks belonging to any of the loops around the switch statement.
    SmallPtrSet<BasicBlock *, 16> LoopBBs;
    for (const PathType &Path : LoopPaths) {
      for (BasicBlock *BB : Path)
        LoopBBs.insert(BB);
    }

    Value *FirstDef = Switch->getOperand(0);

    assert(isa<PHINode>(FirstDef) && "The first definition must be a phi.");

    SmallVector<PHINode *, 8> Stack;
    Stack.push_back(dyn_cast<PHINode>(FirstDef));
    SmallSet<Value *, 16> SeenValues;

    while (!Stack.empty()) {
      PHINode *CurPhi = Stack.pop_back_val();

      Res[CurPhi->getParent()] = CurPhi;
      SeenValues.insert(CurPhi);

      for (BasicBlock *IncomingBB : CurPhi->blocks()) {
        Value *Incoming = CurPhi->getIncomingValueForBlock(IncomingBB);
        bool IsOutsideLoops = LoopBBs.count(IncomingBB) == 0;
        if (Incoming == FirstDef || isa<ConstantInt>(Incoming) ||
            SeenValues.contains(Incoming) || IsOutsideLoops) {
          continue;
        }

        // Any unpredictable value inside the loops means we must bail out.
        if (!isa<PHINode>(Incoming))
          return StateDefMap();

        Stack.push_back(cast<PHINode>(Incoming));
      }
    }

    return Res;
  }

  /// The determinator BB should precede the switch-defining BB.
  ///
  /// Otherwise, it is possible that the state defined in the determinator block
  /// defines the state for the next iteration of the loop, rather than for the
  /// current one.
  ///
  /// Currently supported paths:
  /// \code
  /// < switch bb1 determ def > [ 42, determ ]
  /// < switch_and_def bb1 determ > [ 42, determ ]
  /// < switch_and_def_and_determ bb1 > [ 42, switch_and_def_and_determ ]
  /// \endcode
  ///
  /// Unsupported paths:
  /// \code
  /// < switch bb1 def determ > [ 43, determ ]
  /// < switch_and_determ bb1 def > [ 43, switch_and_determ ]
  /// \endcode
  bool isSupported(const ThreadingPath &TPath) {
    Instruction *SwitchCondI = dyn_cast<Instruction>(Switch->getCondition());
    assert(SwitchCondI);
    if (!SwitchCondI)
      return false;

    const BasicBlock *SwitchCondDefBB = SwitchCondI->getParent();
    const BasicBlock *SwitchCondUseBB = Switch->getParent();
    const BasicBlock *DeterminatorBB = TPath.getDeterminatorBB();

    assert(
        SwitchCondUseBB == TPath.getPath().front() &&
        "The first BB in a threading path should have the switch instruction");
    if (SwitchCondUseBB != TPath.getPath().front())
      return false;

    // Make DeterminatorBB the first element in Path.
    PathType Path = TPath.getPath();
    auto ItDet = llvm::find(Path, DeterminatorBB);
    std::rotate(Path.begin(), ItDet, Path.end());

    bool IsDetBBSeen = false;
    bool IsDefBBSeen = false;
    bool IsUseBBSeen = false;
    for (BasicBlock *BB : Path) {
      if (BB == DeterminatorBB)
        IsDetBBSeen = true;
      if (BB == SwitchCondDefBB)
        IsDefBBSeen = true;
      if (BB == SwitchCondUseBB)
        IsUseBBSeen = true;
      if (IsDetBBSeen && IsUseBBSeen && !IsDefBBSeen)
        return false;
    }

    return true;
  }

  SwitchInst *Switch;
  BasicBlock *SwitchBlock;
  OptimizationRemarkEmitter *ORE;
  std::vector<ThreadingPath> TPaths;
  LoopInfo *LI;
};

struct TransformDFA {
  TransformDFA(AllSwitchPaths *SwitchPaths, DominatorTree *DT,
               AssumptionCache *AC, TargetTransformInfo *TTI,
               OptimizationRemarkEmitter *ORE,
               SmallPtrSet<const Value *, 32> EphValues)
      : SwitchPaths(SwitchPaths), DT(DT), AC(AC), TTI(TTI), ORE(ORE),
        EphValues(EphValues) {}

  void run() {
    if (isLegalAndProfitableToTransform()) {
      createAllExitPaths();
      NumTransforms++;
    }
  }

private:
  /// This function performs both a legality check and profitability check at
  /// the same time since it is convenient to do so. It iterates through all
  /// blocks that will be cloned, and keeps track of the duplication cost. It
  /// also returns false if it is illegal to clone some required block.
  bool isLegalAndProfitableToTransform() {
    CodeMetrics Metrics;
    SwitchInst *Switch = SwitchPaths->getSwitchInst();

    // Don't thread switch without multiple successors.
    if (Switch->getNumSuccessors() <= 1)
      return false;

    // Note that DuplicateBlockMap is not being used as intended here. It is
    // just being used to ensure (BB, State) pairs are only counted once.
    DuplicateBlockMap DuplicateMap;

    for (ThreadingPath &TPath : SwitchPaths->getThreadingPaths()) {
      PathType PathBBs = TPath.getPath();
      APInt NextState = TPath.getExitValue();
      const BasicBlock *Determinator = TPath.getDeterminatorBB();

      // Update Metrics for the Switch block, this is always cloned
      BasicBlock *BB = SwitchPaths->getSwitchBlock();
      BasicBlock *VisitedBB = getClonedBB(BB, NextState, DuplicateMap);
      if (!VisitedBB) {
        Metrics.analyzeBasicBlock(BB, *TTI, EphValues);
        DuplicateMap[BB].push_back({BB, NextState});
      }

      // If the Switch block is the Determinator, then we can continue since
      // this is the only block that is cloned and we already counted for it.
      if (PathBBs.front() == Determinator)
        continue;

      // Otherwise update Metrics for all blocks that will be cloned. If any
      // block is already cloned and would be reused, don't double count it.
      auto DetIt = llvm::find(PathBBs, Determinator);
      for (auto BBIt = DetIt; BBIt != PathBBs.end(); BBIt++) {
        BB = *BBIt;
        VisitedBB = getClonedBB(BB, NextState, DuplicateMap);
        if (VisitedBB)
          continue;
        Metrics.analyzeBasicBlock(BB, *TTI, EphValues);
        DuplicateMap[BB].push_back({BB, NextState});
      }

      if (Metrics.notDuplicatable) {
        LLVM_DEBUG(dbgs() << "DFA Jump Threading: Not jump threading, contains "
                          << "non-duplicatable instructions.\n");
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "NonDuplicatableInst",
                                          Switch)
                 << "Contains non-duplicatable instructions.";
        });
        return false;
      }

      // FIXME: Allow jump threading with controlled convergence.
      if (Metrics.Convergence != ConvergenceKind::None) {
        LLVM_DEBUG(dbgs() << "DFA Jump Threading: Not jump threading, contains "
                          << "convergent instructions.\n");
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "ConvergentInst", Switch)
                 << "Contains convergent instructions.";
        });
        return false;
      }

      if (!Metrics.NumInsts.isValid()) {
        LLVM_DEBUG(dbgs() << "DFA Jump Threading: Not jump threading, contains "
                          << "instructions with invalid cost.\n");
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "ConvergentInst", Switch)
                 << "Contains instructions with invalid cost.";
        });
        return false;
      }
    }

    InstructionCost DuplicationCost = 0;

    unsigned JumpTableSize = 0;
    TTI->getEstimatedNumberOfCaseClusters(*Switch, JumpTableSize, nullptr,
                                          nullptr);
    if (JumpTableSize == 0) {
      // Factor in the number of conditional branches reduced from jump
      // threading. Assume that lowering the switch block is implemented by
      // using binary search, hence the LogBase2().
      unsigned CondBranches =
          APInt(32, Switch->getNumSuccessors()).ceilLogBase2();
      assert(CondBranches > 0 &&
             "The threaded switch must have multiple branches");
      DuplicationCost = Metrics.NumInsts / CondBranches;
    } else {
      // Compared with jump tables, the DFA optimizer removes an indirect branch
      // on each loop iteration, thus making branch prediction more precise. The
      // more branch targets there are, the more likely it is for the branch
      // predictor to make a mistake, and the more benefit there is in the DFA
      // optimizer. Thus, the more branch targets there are, the lower is the
      // cost of the DFA opt.
      DuplicationCost = Metrics.NumInsts / JumpTableSize;
    }

    LLVM_DEBUG(dbgs() << "\nDFA Jump Threading: Cost to jump thread block "
                      << SwitchPaths->getSwitchBlock()->getName()
                      << " is: " << DuplicationCost << "\n\n");

    if (DuplicationCost > CostThreshold) {
      LLVM_DEBUG(dbgs() << "Not jump threading, duplication cost exceeds the "
                        << "cost threshold.\n");
      ORE->emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "NotProfitable", Switch)
               << "Duplication cost exceeds the cost threshold (cost="
               << ore::NV("Cost", DuplicationCost)
               << ", threshold=" << ore::NV("Threshold", CostThreshold) << ").";
      });
      return false;
    }

    ORE->emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "JumpThreaded", Switch)
             << "Switch statement jump-threaded.";
    });

    return true;
  }

  /// Transform each threading path to effectively jump thread the DFA.
  void createAllExitPaths() {
    DomTreeUpdater DTU(*DT, DomTreeUpdater::UpdateStrategy::Eager);

    // Move the switch block to the end of the path, since it will be duplicated
    BasicBlock *SwitchBlock = SwitchPaths->getSwitchBlock();
    for (ThreadingPath &TPath : SwitchPaths->getThreadingPaths()) {
      LLVM_DEBUG(dbgs() << TPath << "\n");
      PathType NewPath(TPath.getPath());
      NewPath.push_back(SwitchBlock);
      TPath.setPath(NewPath);
    }

    // Transform the ThreadingPaths and keep track of the cloned values
    DuplicateBlockMap DuplicateMap;
    DefMap NewDefs;

    SmallSet<BasicBlock *, 16> BlocksToClean;
    for (BasicBlock *BB : successors(SwitchBlock))
      BlocksToClean.insert(BB);

    for (ThreadingPath &TPath : SwitchPaths->getThreadingPaths()) {
      createExitPath(NewDefs, TPath, DuplicateMap, BlocksToClean, &DTU);
      NumPaths++;
    }

    // After all paths are cloned, now update the last successor of the cloned
    // path so it skips over the switch statement
    for (ThreadingPath &TPath : SwitchPaths->getThreadingPaths())
      updateLastSuccessor(TPath, DuplicateMap, &DTU);

    // For each instruction that was cloned and used outside, update its uses
    updateSSA(NewDefs);

    // Clean PHI Nodes for the newly created blocks
    for (BasicBlock *BB : BlocksToClean)
      cleanPhiNodes(BB);
  }

  /// For a specific ThreadingPath \p Path, create an exit path starting from
  /// the determinator block.
  ///
  /// To remember the correct destination, we have to duplicate blocks
  /// corresponding to each state. Also update the terminating instruction of
  /// the predecessors, and phis in the successor blocks.
  void createExitPath(DefMap &NewDefs, ThreadingPath &Path,
                      DuplicateBlockMap &DuplicateMap,
                      SmallSet<BasicBlock *, 16> &BlocksToClean,
                      DomTreeUpdater *DTU) {
    APInt NextState = Path.getExitValue();
    const BasicBlock *Determinator = Path.getDeterminatorBB();
    PathType PathBBs = Path.getPath();

    // Don't select the placeholder block in front
    if (PathBBs.front() == Determinator)
      PathBBs.pop_front();

    auto DetIt = llvm::find(PathBBs, Determinator);
    // When there is only one BB in PathBBs, the determinator takes itself as a
    // direct predecessor.
    BasicBlock *PrevBB = PathBBs.size() == 1 ? *DetIt : *std::prev(DetIt);
    for (auto BBIt = DetIt; BBIt != PathBBs.end(); BBIt++) {
      BasicBlock *BB = *BBIt;
      BlocksToClean.insert(BB);

      // We already cloned BB for this NextState, now just update the branch
      // and continue.
      BasicBlock *NextBB = getClonedBB(BB, NextState, DuplicateMap);
      if (NextBB) {
        updatePredecessor(PrevBB, BB, NextBB, DTU);
        PrevBB = NextBB;
        continue;
      }

      // Clone the BB and update the successor of Prev to jump to the new block
      BasicBlock *NewBB = cloneBlockAndUpdatePredecessor(
          BB, PrevBB, NextState, DuplicateMap, NewDefs, DTU);
      DuplicateMap[BB].push_back({NewBB, NextState});
      BlocksToClean.insert(NewBB);
      PrevBB = NewBB;
    }
  }

  /// Restore SSA form after cloning blocks.
  ///
  /// Each cloned block creates new defs for a variable, and the uses need to be
  /// updated to reflect this. The uses may be replaced with a cloned value, or
  /// some derived phi instruction. Note that all uses of a value defined in the
  /// same block were already remapped when cloning the block.
  void updateSSA(DefMap &NewDefs) {
    SSAUpdaterBulk SSAUpdate;
    SmallVector<Use *, 16> UsesToRename;

    for (const auto &KV : NewDefs) {
      Instruction *I = KV.first;
      BasicBlock *BB = I->getParent();
      std::vector<Instruction *> Cloned = KV.second;

      // Scan all uses of this instruction to see if it is used outside of its
      // block, and if so, record them in UsesToRename.
      for (Use &U : I->uses()) {
        Instruction *User = cast<Instruction>(U.getUser());
        if (PHINode *UserPN = dyn_cast<PHINode>(User)) {
          if (UserPN->getIncomingBlock(U) == BB)
            continue;
        } else if (User->getParent() == BB) {
          continue;
        }

        UsesToRename.push_back(&U);
      }

      // If there are no uses outside the block, we're done with this
      // instruction.
      if (UsesToRename.empty())
        continue;
      LLVM_DEBUG(dbgs() << "DFA-JT: Renaming non-local uses of: " << *I
                        << "\n");

      // We found a use of I outside of BB.  Rename all uses of I that are
      // outside its block to be uses of the appropriate PHI node etc.  See
      // ValuesInBlocks with the values we know.
      unsigned VarNum = SSAUpdate.AddVariable(I->getName(), I->getType());
      SSAUpdate.AddAvailableValue(VarNum, BB, I);
      for (Instruction *New : Cloned)
        SSAUpdate.AddAvailableValue(VarNum, New->getParent(), New);

      while (!UsesToRename.empty())
        SSAUpdate.AddUse(VarNum, UsesToRename.pop_back_val());

      LLVM_DEBUG(dbgs() << "\n");
    }
    // SSAUpdater handles phi placement and renaming uses with the appropriate
    // value.
    SSAUpdate.RewriteAllUses(DT);
  }

  /// Clones a basic block, and adds it to the CFG.
  ///
  /// This function also includes updating phi nodes in the successors of the
  /// BB, and remapping uses that were defined locally in the cloned BB.
  BasicBlock *cloneBlockAndUpdatePredecessor(BasicBlock *BB, BasicBlock *PrevBB,
                                             const APInt &NextState,
                                             DuplicateBlockMap &DuplicateMap,
                                             DefMap &NewDefs,
                                             DomTreeUpdater *DTU) {
    ValueToValueMapTy VMap;
    BasicBlock *NewBB = CloneBasicBlock(
        BB, VMap, ".jt" + std::to_string(NextState.getLimitedValue()),
        BB->getParent());
    NewBB->moveAfter(BB);
    NumCloned++;

    for (Instruction &I : *NewBB) {
      // Do not remap operands of PHINode in case a definition in BB is an
      // incoming value to a phi in the same block. This incoming value will
      // be renamed later while restoring SSA.
      if (isa<PHINode>(&I))
        continue;
      RemapInstruction(&I, VMap,
                       RF_IgnoreMissingLocals | RF_NoModuleLevelChanges);
      if (AssumeInst *II = dyn_cast<AssumeInst>(&I))
        AC->registerAssumption(II);
    }

    updateSuccessorPhis(BB, NewBB, NextState, VMap, DuplicateMap);
    updatePredecessor(PrevBB, BB, NewBB, DTU);
    updateDefMap(NewDefs, VMap);

    // Add all successors to the DominatorTree
    SmallPtrSet<BasicBlock *, 4> SuccSet;
    for (auto *SuccBB : successors(NewBB)) {
      if (SuccSet.insert(SuccBB).second)
        DTU->applyUpdates({{DominatorTree::Insert, NewBB, SuccBB}});
    }
    SuccSet.clear();
    return NewBB;
  }

  /// Update the phi nodes in BB's successors.
  ///
  /// This means creating a new incoming value from NewBB with the new
  /// instruction wherever there is an incoming value from BB.
  void updateSuccessorPhis(BasicBlock *BB, BasicBlock *ClonedBB,
                           const APInt &NextState, ValueToValueMapTy &VMap,
                           DuplicateBlockMap &DuplicateMap) {
    std::vector<BasicBlock *> BlocksToUpdate;

    // If BB is the last block in the path, we can simply update the one case
    // successor that will be reached.
    if (BB == SwitchPaths->getSwitchBlock()) {
      SwitchInst *Switch = SwitchPaths->getSwitchInst();
      BasicBlock *NextCase = getNextCaseSuccessor(Switch, NextState);
      BlocksToUpdate.push_back(NextCase);
      BasicBlock *ClonedSucc = getClonedBB(NextCase, NextState, DuplicateMap);
      if (ClonedSucc)
        BlocksToUpdate.push_back(ClonedSucc);
    }
    // Otherwise update phis in all successors.
    else {
      for (BasicBlock *Succ : successors(BB)) {
        BlocksToUpdate.push_back(Succ);

        // Check if a successor has already been cloned for the particular exit
        // value. In this case if a successor was already cloned, the phi nodes
        // in the cloned block should be updated directly.
        BasicBlock *ClonedSucc = getClonedBB(Succ, NextState, DuplicateMap);
        if (ClonedSucc)
          BlocksToUpdate.push_back(ClonedSucc);
      }
    }

    // If there is a phi with an incoming value from BB, create a new incoming
    // value for the new predecessor ClonedBB. The value will either be the same
    // value from BB or a cloned value.
    for (BasicBlock *Succ : BlocksToUpdate) {
      for (auto II = Succ->begin(); PHINode *Phi = dyn_cast<PHINode>(II);
           ++II) {
        Value *Incoming = Phi->getIncomingValueForBlock(BB);
        if (Incoming) {
          if (isa<Constant>(Incoming)) {
            Phi->addIncoming(Incoming, ClonedBB);
            continue;
          }
          Value *ClonedVal = VMap[Incoming];
          if (ClonedVal)
            Phi->addIncoming(ClonedVal, ClonedBB);
          else
            Phi->addIncoming(Incoming, ClonedBB);
        }
      }
    }
  }

  /// Sets the successor of PrevBB to be NewBB instead of OldBB. Note that all
  /// other successors are kept as well.
  void updatePredecessor(BasicBlock *PrevBB, BasicBlock *OldBB,
                         BasicBlock *NewBB, DomTreeUpdater *DTU) {
    // When a path is reused, there is a chance that predecessors were already
    // updated before. Check if the predecessor needs to be updated first.
    if (!isPredecessor(OldBB, PrevBB))
      return;

    Instruction *PrevTerm = PrevBB->getTerminator();
    for (unsigned Idx = 0; Idx < PrevTerm->getNumSuccessors(); Idx++) {
      if (PrevTerm->getSuccessor(Idx) == OldBB) {
        OldBB->removePredecessor(PrevBB, /* KeepOneInputPHIs = */ true);
        PrevTerm->setSuccessor(Idx, NewBB);
      }
    }
    DTU->applyUpdates({{DominatorTree::Delete, PrevBB, OldBB},
                       {DominatorTree::Insert, PrevBB, NewBB}});
  }

  /// Add new value mappings to the DefMap to keep track of all new definitions
  /// for a particular instruction. These will be used while updating SSA form.
  void updateDefMap(DefMap &NewDefs, ValueToValueMapTy &VMap) {
    SmallVector<std::pair<Instruction *, Instruction *>> NewDefsVector;
    NewDefsVector.reserve(VMap.size());

    for (auto Entry : VMap) {
      Instruction *Inst =
          dyn_cast<Instruction>(const_cast<Value *>(Entry.first));
      if (!Inst || !Entry.second || isa<BranchInst>(Inst) ||
          isa<SwitchInst>(Inst)) {
        continue;
      }

      Instruction *Cloned = dyn_cast<Instruction>(Entry.second);
      if (!Cloned)
        continue;

      NewDefsVector.push_back({Inst, Cloned});
    }

    // Sort the defs to get deterministic insertion order into NewDefs.
    sort(NewDefsVector, [](const auto &LHS, const auto &RHS) {
      if (LHS.first == RHS.first)
        return LHS.second->comesBefore(RHS.second);
      return LHS.first->comesBefore(RHS.first);
    });

    for (const auto &KV : NewDefsVector)
      NewDefs[KV.first].push_back(KV.second);
  }

  /// Update the last branch of a particular cloned path to point to the correct
  /// case successor.
  ///
  /// Note that this is an optional step and would have been done in later
  /// optimizations, but it makes the CFG significantly easier to work with.
  void updateLastSuccessor(ThreadingPath &TPath,
                           DuplicateBlockMap &DuplicateMap,
                           DomTreeUpdater *DTU) {
    APInt NextState = TPath.getExitValue();
    BasicBlock *BB = TPath.getPath().back();
    BasicBlock *LastBlock = getClonedBB(BB, NextState, DuplicateMap);

    // Note multiple paths can end at the same block so check that it is not
    // updated yet
    if (!isa<SwitchInst>(LastBlock->getTerminator()))
      return;
    SwitchInst *Switch = cast<SwitchInst>(LastBlock->getTerminator());
    BasicBlock *NextCase = getNextCaseSuccessor(Switch, NextState);

    std::vector<DominatorTree::UpdateType> DTUpdates;
    SmallPtrSet<BasicBlock *, 4> SuccSet;
    for (BasicBlock *Succ : successors(LastBlock)) {
      if (Succ != NextCase && SuccSet.insert(Succ).second)
        DTUpdates.push_back({DominatorTree::Delete, LastBlock, Succ});
    }

    Switch->eraseFromParent();
    BranchInst::Create(NextCase, LastBlock);

    DTU->applyUpdates(DTUpdates);
  }

  /// After cloning blocks, some of the phi nodes have extra incoming values
  /// that are no longer used. This function removes them.
  void cleanPhiNodes(BasicBlock *BB) {
    // If BB is no longer reachable, remove any remaining phi nodes
    if (pred_empty(BB)) {
      std::vector<PHINode *> PhiToRemove;
      for (auto II = BB->begin(); PHINode *Phi = dyn_cast<PHINode>(II); ++II) {
        PhiToRemove.push_back(Phi);
      }
      for (PHINode *PN : PhiToRemove) {
        PN->replaceAllUsesWith(PoisonValue::get(PN->getType()));
        PN->eraseFromParent();
      }
      return;
    }

    // Remove any incoming values that come from an invalid predecessor
    for (auto II = BB->begin(); PHINode *Phi = dyn_cast<PHINode>(II); ++II) {
      std::vector<BasicBlock *> BlocksToRemove;
      for (BasicBlock *IncomingBB : Phi->blocks()) {
        if (!isPredecessor(BB, IncomingBB))
          BlocksToRemove.push_back(IncomingBB);
      }
      for (BasicBlock *BB : BlocksToRemove)
        Phi->removeIncomingValue(BB);
    }
  }

  /// Checks if BB was already cloned for a particular next state value. If it
  /// was then it returns this cloned block, and otherwise null.
  BasicBlock *getClonedBB(BasicBlock *BB, const APInt &NextState,
                          DuplicateBlockMap &DuplicateMap) {
    CloneList ClonedBBs = DuplicateMap[BB];

    // Find an entry in the CloneList with this NextState. If it exists then
    // return the corresponding BB
    auto It = llvm::find_if(ClonedBBs, [NextState](const ClonedBlock &C) {
      return C.State == NextState;
    });
    return It != ClonedBBs.end() ? (*It).BB : nullptr;
  }

  /// Helper to get the successor corresponding to a particular case value for
  /// a switch statement.
  BasicBlock *getNextCaseSuccessor(SwitchInst *Switch, const APInt &NextState) {
    BasicBlock *NextCase = nullptr;
    for (auto Case : Switch->cases()) {
      if (Case.getCaseValue()->getValue() == NextState) {
        NextCase = Case.getCaseSuccessor();
        break;
      }
    }
    if (!NextCase)
      NextCase = Switch->getDefaultDest();
    return NextCase;
  }

  /// Returns true if IncomingBB is a predecessor of BB.
  bool isPredecessor(BasicBlock *BB, BasicBlock *IncomingBB) {
    return llvm::is_contained(predecessors(BB), IncomingBB);
  }

  AllSwitchPaths *SwitchPaths;
  DominatorTree *DT;
  AssumptionCache *AC;
  TargetTransformInfo *TTI;
  OptimizationRemarkEmitter *ORE;
  SmallPtrSet<const Value *, 32> EphValues;
  std::vector<ThreadingPath> TPaths;
};

bool DFAJumpThreading::run(Function &F) {
  LLVM_DEBUG(dbgs() << "\nDFA Jump threading: " << F.getName() << "\n");

  if (F.hasOptSize()) {
    LLVM_DEBUG(dbgs() << "Skipping due to the 'minsize' attribute\n");
    return false;
  }

  if (ClViewCfgBefore)
    F.viewCFG();

  SmallVector<AllSwitchPaths, 2> ThreadableLoops;
  bool MadeChanges = false;
  LoopInfoBroken = false;

  for (BasicBlock &BB : F) {
    auto *SI = dyn_cast<SwitchInst>(BB.getTerminator());
    if (!SI)
      continue;

    LLVM_DEBUG(dbgs() << "\nCheck if SwitchInst in BB " << BB.getName()
                      << " is a candidate\n");
    MainSwitch Switch(SI, LI, ORE);

    if (!Switch.getInstr())
      continue;

    LLVM_DEBUG(dbgs() << "\nSwitchInst in BB " << BB.getName() << " is a "
                      << "candidate for jump threading\n");
    LLVM_DEBUG(SI->dump());

    unfoldSelectInstrs(DT, Switch.getSelectInsts());
    if (!Switch.getSelectInsts().empty())
      MadeChanges = true;

    AllSwitchPaths SwitchPaths(&Switch, ORE, LI);
    SwitchPaths.run();

    if (SwitchPaths.getNumThreadingPaths() > 0) {
      ThreadableLoops.push_back(SwitchPaths);

      // For the time being limit this optimization to occurring once in a
      // function since it can change the CFG significantly. This is not a
      // strict requirement but it can cause buggy behavior if there is an
      // overlap of blocks in different opportunities. There is a lot of room to
      // experiment with catching more opportunities here.
      // NOTE: To release this contraint, we must handle LoopInfo invalidation
      break;
    }
  }

#ifdef NDEBUG
  LI->verify(*DT);
#endif

  SmallPtrSet<const Value *, 32> EphValues;
  if (ThreadableLoops.size() > 0)
    CodeMetrics::collectEphemeralValues(&F, AC, EphValues);

  for (AllSwitchPaths SwitchPaths : ThreadableLoops) {
    TransformDFA Transform(&SwitchPaths, DT, AC, TTI, ORE, EphValues);
    Transform.run();
    MadeChanges = true;
    LoopInfoBroken = true;
  }

#ifdef EXPENSIVE_CHECKS
  assert(DT->verify(DominatorTree::VerificationLevel::Full));
  verifyFunction(F, &dbgs());
#endif

  return MadeChanges;
}

} // end anonymous namespace

/// Integrate with the new Pass Manager
PreservedAnalyses DFAJumpThreadingPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  AssumptionCache &AC = AM.getResult<AssumptionAnalysis>(F);
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
  TargetTransformInfo &TTI = AM.getResult<TargetIRAnalysis>(F);
  OptimizationRemarkEmitter ORE(&F);
  DFAJumpThreading ThreadImpl(&AC, &DT, &LI, &TTI, &ORE);
  if (!ThreadImpl.run(F))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  if (!ThreadImpl.LoopInfoBroken)
    PA.preserve<LoopAnalysis>();
  return PA;
}
