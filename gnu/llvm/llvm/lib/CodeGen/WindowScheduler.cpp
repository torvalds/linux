//======----------- WindowScheduler.cpp - window scheduler -------------======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the Window Scheduling software pipelining algorithm.
//
// The fundamental concept of the window scheduling algorithm involves folding
// the original MBB at a specific position, followed by list scheduling on the
// folded MIs. The optimal scheduling result is then chosen from various folding
// positions as the final scheduling outcome.
//
// The primary challenge in this algorithm lies in generating the folded MIs and
// establishing their dependencies. We have innovatively employed a new MBB,
// created by copying the original MBB three times, known as TripleMBB. This
// TripleMBB enables the convenient implementation of MI folding and dependency
// establishment. To facilitate the algorithm's implementation, we have also
// devised data structures such as OriMIs, TriMIs, TriToOri, and OriToCycle.
//
// Another challenge in the algorithm is the scheduling of phis. Semantically,
// it is difficult to place the phis in the window and perform list scheduling.
// Therefore, we schedule these phis separately after each list scheduling.
//
// The provided implementation is designed for use before the Register Allocator
// (RA). If the target requires implementation after RA, it is recommended to
// reimplement analyseII(), schedulePhi(), and expand(). Additionally,
// target-specific logic can be added in initialize(), preProcess(), and
// postProcess().
//
// Lastly, it is worth mentioning that getSearchIndexes() is an important
// function. We have experimented with more complex heuristics on downstream
// target and achieved favorable results.
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/WindowScheduler.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePipeliner.h"
#include "llvm/CodeGen/ModuloSchedule.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;

#define DEBUG_TYPE "pipeliner"

namespace {
STATISTIC(NumTryWindowSchedule,
          "Number of loops that we attempt to use window scheduling");
STATISTIC(NumTryWindowSearch,
          "Number of times that we run list schedule in the window scheduling");
STATISTIC(NumWindowSchedule,
          "Number of loops that we successfully use window scheduling");
STATISTIC(NumFailAnalyseII,
          "Window scheduling abort due to the failure of the II analysis");

cl::opt<unsigned>
    WindowSearchNum("window-search-num",
                    cl::desc("The number of searches per loop in the window "
                             "algorithm. 0 means no search number limit."),
                    cl::Hidden, cl::init(6));

cl::opt<unsigned> WindowSearchRatio(
    "window-search-ratio",
    cl::desc("The ratio of searches per loop in the window algorithm. 100 "
             "means search all positions in the loop, while 0 means not "
             "performing any search."),
    cl::Hidden, cl::init(40));

cl::opt<unsigned> WindowIICoeff(
    "window-ii-coeff",
    cl::desc(
        "The coefficient used when initializing II in the window algorithm."),
    cl::Hidden, cl::init(5));

cl::opt<unsigned> WindowRegionLimit(
    "window-region-limit",
    cl::desc(
        "The lower limit of the scheduling region in the window algorithm."),
    cl::Hidden, cl::init(3));

cl::opt<unsigned> WindowDiffLimit(
    "window-diff-limit",
    cl::desc("The lower limit of the difference between best II and base II in "
             "the window algorithm. If the difference is smaller than "
             "this lower limit, window scheduling will not be performed."),
    cl::Hidden, cl::init(2));
} // namespace

// WindowIILimit serves as an indicator of abnormal scheduling results and could
// potentially be referenced by the derived target window scheduler.
cl::opt<unsigned>
    WindowIILimit("window-ii-limit",
                  cl::desc("The upper limit of II in the window algorithm."),
                  cl::Hidden, cl::init(1000));

WindowScheduler::WindowScheduler(MachineSchedContext *C, MachineLoop &ML)
    : Context(C), MF(C->MF), MBB(ML.getHeader()), Loop(ML),
      Subtarget(&MF->getSubtarget()), TII(Subtarget->getInstrInfo()),
      TRI(Subtarget->getRegisterInfo()), MRI(&MF->getRegInfo()) {
  TripleDAG = std::unique_ptr<ScheduleDAGInstrs>(
      createMachineScheduler(/*OnlyBuildGraph=*/true));
}

bool WindowScheduler::run() {
  if (!initialize()) {
    LLVM_DEBUG(dbgs() << "The WindowScheduler failed to initialize!\n");
    return false;
  }
  // The window algorithm is time-consuming, and its compilation time should be
  // taken into consideration.
  TimeTraceScope Scope("WindowSearch");
  ++NumTryWindowSchedule;
  // Performing the relevant processing before window scheduling.
  preProcess();
  // The main window scheduling begins.
  std::unique_ptr<ScheduleDAGInstrs> SchedDAG(createMachineScheduler());
  auto SearchIndexes = getSearchIndexes(WindowSearchNum, WindowSearchRatio);
  for (unsigned Idx : SearchIndexes) {
    OriToCycle.clear();
    ++NumTryWindowSearch;
    // The scheduling starts with non-phi instruction, so SchedPhiNum needs to
    // be added to Idx.
    unsigned Offset = Idx + SchedPhiNum;
    auto Range = getScheduleRange(Offset, SchedInstrNum);
    SchedDAG->startBlock(MBB);
    SchedDAG->enterRegion(MBB, Range.begin(), Range.end(), SchedInstrNum);
    SchedDAG->schedule();
    LLVM_DEBUG(SchedDAG->dump());
    unsigned II = analyseII(*SchedDAG, Offset);
    if (II == WindowIILimit) {
      restoreTripleMBB();
      LLVM_DEBUG(dbgs() << "Can't find a valid II. Keep searching...\n");
      ++NumFailAnalyseII;
      continue;
    }
    schedulePhi(Offset, II);
    updateScheduleResult(Offset, II);
    restoreTripleMBB();
    LLVM_DEBUG(dbgs() << "Current window Offset is " << Offset << " and II is "
                      << II << ".\n");
  }
  // Performing the relevant processing after window scheduling.
  postProcess();
  // Check whether the scheduling result is valid.
  if (!isScheduleValid()) {
    LLVM_DEBUG(dbgs() << "Window scheduling is not needed!\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << "\nBest window offset is " << BestOffset
                    << " and Best II is " << BestII << ".\n");
  // Expand the scheduling result to prologue, kernel, and epilogue.
  expand();
  ++NumWindowSchedule;
  return true;
}

ScheduleDAGInstrs *
WindowScheduler::createMachineScheduler(bool OnlyBuildGraph) {
  return OnlyBuildGraph
             ? new ScheduleDAGMI(
                   Context, std::make_unique<PostGenericScheduler>(Context),
                   true)
             : Context->PassConfig->createMachineScheduler(Context);
}

bool WindowScheduler::initialize() {
  if (!Subtarget->enableWindowScheduler()) {
    LLVM_DEBUG(dbgs() << "Target disables the window scheduling!\n");
    return false;
  }
  // Initialized the member variables used by window algorithm.
  OriMIs.clear();
  TriMIs.clear();
  TriToOri.clear();
  OriToCycle.clear();
  SchedResult.clear();
  SchedPhiNum = 0;
  SchedInstrNum = 0;
  BestII = UINT_MAX;
  BestOffset = 0;
  BaseII = 0;
  // List scheduling used in the window algorithm depends on LiveIntervals.
  if (!Context->LIS) {
    LLVM_DEBUG(dbgs() << "There is no LiveIntervals information!\n");
    return false;
  }
  // Check each MI in MBB.
  SmallSet<Register, 8> PrevDefs;
  SmallSet<Register, 8> PrevUses;
  auto IsLoopCarried = [&](MachineInstr &Phi) {
    // Two cases are checked here: (1)The virtual register defined by the
    // preceding phi is used by the succeeding phi;(2)The preceding phi uses the
    // virtual register defined by the succeeding phi.
    if (PrevUses.count(Phi.getOperand(0).getReg()))
      return true;
    PrevDefs.insert(Phi.getOperand(0).getReg());
    for (unsigned I = 1, E = Phi.getNumOperands(); I != E; I += 2) {
      if (PrevDefs.count(Phi.getOperand(I).getReg()))
        return true;
      PrevUses.insert(Phi.getOperand(I).getReg());
    }
    return false;
  };
  auto PLI = TII->analyzeLoopForPipelining(MBB);
  for (auto &MI : *MBB) {
    if (MI.isMetaInstruction() || MI.isTerminator())
      continue;
    if (MI.isPHI()) {
      if (IsLoopCarried(MI)) {
        LLVM_DEBUG(dbgs() << "Loop carried phis are not supported yet!\n");
        return false;
      }
      ++SchedPhiNum;
      ++BestOffset;
    } else
      ++SchedInstrNum;
    if (TII->isSchedulingBoundary(MI, MBB, *MF)) {
      LLVM_DEBUG(
          dbgs() << "Boundary MI is not allowed in window scheduling!\n");
      return false;
    }
    if (PLI->shouldIgnoreForPipelining(&MI)) {
      LLVM_DEBUG(dbgs() << "Special MI defined by target is not allowed in "
                           "window scheduling!\n");
      return false;
    }
    for (auto &Def : MI.all_defs())
      if (Def.isReg() && Def.getReg().isPhysical()) {
        LLVM_DEBUG(dbgs() << "Physical registers are not supported in "
                             "window scheduling!\n");
        return false;
      }
  }
  if (SchedInstrNum <= WindowRegionLimit) {
    LLVM_DEBUG(dbgs() << "There are too few MIs in the window region!\n");
    return false;
  }
  return true;
}

void WindowScheduler::preProcess() {
  // Prior to window scheduling, it's necessary to backup the original MBB,
  // generate a new TripleMBB, and build a TripleDAG based on the TripleMBB.
  backupMBB();
  generateTripleMBB();
  TripleDAG->startBlock(MBB);
  TripleDAG->enterRegion(
      MBB, MBB->begin(), MBB->getFirstTerminator(),
      std::distance(MBB->begin(), MBB->getFirstTerminator()));
  TripleDAG->buildSchedGraph(Context->AA);
}

void WindowScheduler::postProcess() {
  // After window scheduling, it's necessary to clear the TripleDAG and restore
  // to the original MBB.
  TripleDAG->exitRegion();
  TripleDAG->finishBlock();
  restoreMBB();
}

void WindowScheduler::backupMBB() {
  for (auto &MI : MBB->instrs())
    OriMIs.push_back(&MI);
  // Remove MIs and the corresponding live intervals.
  for (auto &MI : make_early_inc_range(*MBB)) {
    Context->LIS->getSlotIndexes()->removeMachineInstrFromMaps(MI, true);
    MBB->remove(&MI);
  }
}

void WindowScheduler::restoreMBB() {
  // Erase MIs and the corresponding live intervals.
  for (auto &MI : make_early_inc_range(*MBB)) {
    Context->LIS->getSlotIndexes()->removeMachineInstrFromMaps(MI, true);
    MI.eraseFromParent();
  }
  // Restore MBB to the state before window scheduling.
  for (auto *MI : OriMIs)
    MBB->push_back(MI);
  updateLiveIntervals();
}

void WindowScheduler::generateTripleMBB() {
  const unsigned DuplicateNum = 3;
  TriMIs.clear();
  TriToOri.clear();
  assert(OriMIs.size() > 0 && "The Original MIs were not backed up!");
  // Step 1: Performing the first copy of MBB instructions, excluding
  // terminators. At the same time, we back up the anti-register of phis.
  // DefPairs hold the old and new define register pairs.
  DenseMap<Register, Register> DefPairs;
  for (auto *MI : OriMIs) {
    if (MI->isMetaInstruction() || MI->isTerminator())
      continue;
    if (MI->isPHI())
      if (Register AntiReg = getAntiRegister(MI))
        DefPairs[MI->getOperand(0).getReg()] = AntiReg;
    auto *NewMI = MF->CloneMachineInstr(MI);
    MBB->push_back(NewMI);
    TriMIs.push_back(NewMI);
    TriToOri[NewMI] = MI;
  }
  // Step 2: Performing the remaining two copies of MBB instructions excluding
  // phis, and the last one contains terminators. At the same time, registers
  // are updated accordingly.
  for (size_t Cnt = 1; Cnt < DuplicateNum; ++Cnt) {
    for (auto *MI : OriMIs) {
      if (MI->isPHI() || MI->isMetaInstruction() ||
          (MI->isTerminator() && Cnt < DuplicateNum - 1))
        continue;
      auto *NewMI = MF->CloneMachineInstr(MI);
      DenseMap<Register, Register> NewDefs;
      // New defines are updated.
      for (auto MO : NewMI->all_defs())
        if (MO.isReg() && MO.getReg().isVirtual()) {
          Register NewDef =
              MRI->createVirtualRegister(MRI->getRegClass(MO.getReg()));
          NewMI->substituteRegister(MO.getReg(), NewDef, 0, *TRI);
          NewDefs[MO.getReg()] = NewDef;
        }
      // New uses are updated.
      for (auto DefRegPair : DefPairs)
        if (NewMI->readsRegister(DefRegPair.first, TRI)) {
          Register NewUse = DefRegPair.second;
          // Note the update process for '%1 -> %9' in '%10 = sub i32 %9, %3':
          //
          // BB.3:                                  DefPairs
          // ==================================
          // %1 = phi i32 [%2, %BB.1], [%7, %BB.3]  (%1,%7)
          // ...
          // ==================================
          // ...
          // %4 = sub i32 %1, %3
          // ...
          // %7 = add i32 %5, %6
          // ...
          // ----------------------------------
          // ...
          // %8 = sub i32 %7, %3                    (%1,%7),(%4,%8)
          // ...
          // %9 = add i32 %5, %6                    (%1,%7),(%4,%8),(%7,%9)
          // ...
          // ----------------------------------
          // ...
          // %10 = sub i32 %9, %3                   (%1,%7),(%4,%10),(%7,%9)
          // ...            ^
          // %11 = add i32 %5, %6                   (%1,%7),(%4,%10),(%7,%11)
          // ...
          // ==================================
          //          < Terminators >
          // ==================================
          if (DefPairs.count(NewUse))
            NewUse = DefPairs[NewUse];
          NewMI->substituteRegister(DefRegPair.first, NewUse, 0, *TRI);
        }
      // DefPairs is updated at last.
      for (auto &NewDef : NewDefs)
        DefPairs[NewDef.first] = NewDef.second;
      MBB->push_back(NewMI);
      TriMIs.push_back(NewMI);
      TriToOri[NewMI] = MI;
    }
  }
  // Step 3: The registers used by phis are updated, and they are generated in
  // the third copy of MBB.
  // In the privious example, the old phi is:
  // %1 = phi i32 [%2, %BB.1], [%7, %BB.3]
  // The new phi is:
  // %1 = phi i32 [%2, %BB.1], [%11, %BB.3]
  for (auto &Phi : MBB->phis()) {
    for (auto DefRegPair : DefPairs)
      if (Phi.readsRegister(DefRegPair.first, TRI))
        Phi.substituteRegister(DefRegPair.first, DefRegPair.second, 0, *TRI);
  }
  updateLiveIntervals();
}

void WindowScheduler::restoreTripleMBB() {
  // After list scheduling, the MBB is restored in one traversal.
  for (size_t I = 0; I < TriMIs.size(); ++I) {
    auto *MI = TriMIs[I];
    auto OldPos = MBB->begin();
    std::advance(OldPos, I);
    auto CurPos = MI->getIterator();
    if (CurPos != OldPos) {
      MBB->splice(OldPos, MBB, CurPos);
      Context->LIS->handleMove(*MI, /*UpdateFlags=*/false);
    }
  }
}

SmallVector<unsigned> WindowScheduler::getSearchIndexes(unsigned SearchNum,
                                                        unsigned SearchRatio) {
  // We use SearchRatio to get the index range, and then evenly get the indexes
  // according to the SearchNum. This is a simple huristic. Depending on the
  // characteristics of the target, more complex algorithms can be used for both
  // performance and compilation time.
  assert(SearchRatio <= 100 && "SearchRatio should be equal or less than 100!");
  unsigned MaxIdx = SchedInstrNum * SearchRatio / 100;
  unsigned Step = SearchNum > 0 && SearchNum <= MaxIdx ? MaxIdx / SearchNum : 1;
  SmallVector<unsigned> SearchIndexes;
  for (unsigned Idx = 0; Idx < MaxIdx; Idx += Step)
    SearchIndexes.push_back(Idx);
  return SearchIndexes;
}

int WindowScheduler::getEstimatedII(ScheduleDAGInstrs &DAG) {
  // Sometimes MaxDepth is 0, so it should be limited to the minimum of 1.
  unsigned MaxDepth = 1;
  for (auto &SU : DAG.SUnits)
    MaxDepth = std::max(SU.getDepth() + SU.Latency, MaxDepth);
  return MaxDepth * WindowIICoeff;
}

int WindowScheduler::calculateMaxCycle(ScheduleDAGInstrs &DAG,
                                       unsigned Offset) {
  int InitII = getEstimatedII(DAG);
  ResourceManager RM(Subtarget, &DAG);
  RM.init(InitII);
  // ResourceManager and DAG are used to calculate the maximum cycle for the
  // scheduled MIs. Since MIs in the Region have already been scheduled, the
  // emit cycles can be estimated in order here.
  int CurCycle = 0;
  auto Range = getScheduleRange(Offset, SchedInstrNum);
  for (auto &MI : Range) {
    auto *SU = DAG.getSUnit(&MI);
    int ExpectCycle = CurCycle;
    // The predecessors of current MI determine its earliest issue cycle.
    for (auto &Pred : SU->Preds) {
      if (Pred.isWeak())
        continue;
      auto *PredMI = Pred.getSUnit()->getInstr();
      int PredCycle = getOriCycle(PredMI);
      ExpectCycle = std::max(ExpectCycle, PredCycle + (int)Pred.getLatency());
    }
    // Zero cost instructions do not need to check resource.
    if (!TII->isZeroCost(MI.getOpcode())) {
      // ResourceManager can be used to detect resource conflicts between the
      // current MI and the previously inserted MIs.
      while (!RM.canReserveResources(*SU, CurCycle) || CurCycle < ExpectCycle) {
        ++CurCycle;
        if (CurCycle == (int)WindowIILimit)
          return CurCycle;
      }
      RM.reserveResources(*SU, CurCycle);
    }
    OriToCycle[getOriMI(&MI)] = CurCycle;
    LLVM_DEBUG(dbgs() << "\tCycle " << CurCycle << " [S."
                      << getOriStage(getOriMI(&MI), Offset) << "]: " << MI);
  }
  LLVM_DEBUG(dbgs() << "MaxCycle is " << CurCycle << ".\n");
  return CurCycle;
}

// By utilizing TripleDAG, we can easily establish dependencies between A and B.
// Based on the MaxCycle and the issue cycle of A and B, we can determine
// whether it is necessary to add a stall cycle. This is because, without
// inserting the stall cycle, the latency constraint between A and B cannot be
// satisfied. The details are as follows:
//
// New MBB:
// ========================================
//                 < Phis >
// ========================================     (sliding direction)
// MBB copy 1                                            |
//                                                       V
//
// ~~~~~~~~~~~~~~~~~~~|~~~~~~~~~~~~~~~~~~~~  ----schedule window-----
//                    |                                  |
// ===================V====================              |
// MBB copy 2      < MI B >                              |
//                                                       |
//                 < MI A >                              V
// ~~~~~~~~~~~~~~~~~~~:~~~~~~~~~~~~~~~~~~~~  ------------------------
//                    :
// ===================V====================
// MBB copy 3      < MI B'>
//
//
//
//
// ========================================
//              < Terminators >
// ========================================
int WindowScheduler::calculateStallCycle(unsigned Offset, int MaxCycle) {
  int MaxStallCycle = 0;
  int CurrentII = MaxCycle + 1;
  auto Range = getScheduleRange(Offset, SchedInstrNum);
  for (auto &MI : Range) {
    auto *SU = TripleDAG->getSUnit(&MI);
    int DefCycle = getOriCycle(&MI);
    for (auto &Succ : SU->Succs) {
      if (Succ.isWeak() || Succ.getSUnit() == &TripleDAG->ExitSU)
        continue;
      // If the expected cycle does not exceed CurrentII, no check is needed.
      if (DefCycle + (int)Succ.getLatency() <= CurrentII)
        continue;
      // If the cycle of the scheduled MI A is less than that of the scheduled
      // MI B, the scheduling will fail because the lifetime of the
      // corresponding register exceeds II.
      auto *SuccMI = Succ.getSUnit()->getInstr();
      int UseCycle = getOriCycle(SuccMI);
      if (DefCycle < UseCycle)
        return WindowIILimit;
      // Get the stall cycle introduced by the register between two trips.
      int StallCycle = DefCycle + (int)Succ.getLatency() - CurrentII - UseCycle;
      MaxStallCycle = std::max(MaxStallCycle, StallCycle);
    }
  }
  LLVM_DEBUG(dbgs() << "MaxStallCycle is " << MaxStallCycle << ".\n");
  return MaxStallCycle;
}

unsigned WindowScheduler::analyseII(ScheduleDAGInstrs &DAG, unsigned Offset) {
  LLVM_DEBUG(dbgs() << "Start analyzing II:\n");
  int MaxCycle = calculateMaxCycle(DAG, Offset);
  if (MaxCycle == (int)WindowIILimit)
    return MaxCycle;
  int StallCycle = calculateStallCycle(Offset, MaxCycle);
  if (StallCycle == (int)WindowIILimit)
    return StallCycle;
  // The value of II is equal to the maximum execution cycle plus 1.
  return MaxCycle + StallCycle + 1;
}

void WindowScheduler::schedulePhi(int Offset, unsigned &II) {
  LLVM_DEBUG(dbgs() << "Start scheduling Phis:\n");
  for (auto &Phi : MBB->phis()) {
    int LateCycle = INT_MAX;
    auto *SU = TripleDAG->getSUnit(&Phi);
    for (auto &Succ : SU->Succs) {
      // Phi doesn't have any Anti successors.
      if (Succ.getKind() != SDep::Data)
        continue;
      // Phi is scheduled before the successor of stage 0. The issue cycle of
      // phi is the latest cycle in this interval.
      auto *SuccMI = Succ.getSUnit()->getInstr();
      int Cycle = getOriCycle(SuccMI);
      if (getOriStage(getOriMI(SuccMI), Offset) == 0)
        LateCycle = std::min(LateCycle, Cycle);
    }
    // The anti-dependency of phi need to be handled separately in the same way.
    if (Register AntiReg = getAntiRegister(&Phi)) {
      auto *AntiMI = MRI->getVRegDef(AntiReg);
      // AntiReg may be defined outside the kernel MBB.
      if (AntiMI->getParent() == MBB) {
        auto AntiCycle = getOriCycle(AntiMI);
        if (getOriStage(getOriMI(AntiMI), Offset) == 0)
          LateCycle = std::min(LateCycle, AntiCycle);
      }
    }
    // If there is no limit to the late cycle, a default value is given.
    if (LateCycle == INT_MAX)
      LateCycle = (int)(II - 1);
    LLVM_DEBUG(dbgs() << "\tCycle range [0, " << LateCycle << "] " << Phi);
    // The issue cycle of phi is set to the latest cycle in the interval.
    auto *OriPhi = getOriMI(&Phi);
    OriToCycle[OriPhi] = LateCycle;
  }
}

DenseMap<MachineInstr *, int> WindowScheduler::getIssueOrder(unsigned Offset,
                                                             unsigned II) {
  // At each issue cycle, phi is placed before MIs in stage 0. So the simplest
  // way is to put phi at the beginning of the current cycle.
  DenseMap<int, SmallVector<MachineInstr *>> CycleToMIs;
  auto Range = getScheduleRange(Offset, SchedInstrNum);
  for (auto &Phi : MBB->phis())
    CycleToMIs[getOriCycle(&Phi)].push_back(getOriMI(&Phi));
  for (auto &MI : Range)
    CycleToMIs[getOriCycle(&MI)].push_back(getOriMI(&MI));
  // Each MI is assigned a separate ordered Id, which is used as a sort marker
  // in the following expand process.
  DenseMap<MachineInstr *, int> IssueOrder;
  int Id = 0;
  for (int Cycle = 0; Cycle < (int)II; ++Cycle) {
    if (!CycleToMIs.count(Cycle))
      continue;
    for (auto *MI : CycleToMIs[Cycle])
      IssueOrder[MI] = Id++;
  }
  return IssueOrder;
}

void WindowScheduler::updateScheduleResult(unsigned Offset, unsigned II) {
  // At the first update, Offset is equal to SchedPhiNum. At this time, only
  // BestII, BestOffset, and BaseII need to be updated.
  if (Offset == SchedPhiNum) {
    BestII = II;
    BestOffset = SchedPhiNum;
    BaseII = II;
    return;
  }
  // The update will only continue if the II is smaller than BestII and the II
  // is sufficiently small.
  if ((II >= BestII) || (II + WindowDiffLimit > BaseII))
    return;
  BestII = II;
  BestOffset = Offset;
  // Record the result of the current list scheduling, noting that each MI is
  // stored unordered in SchedResult.
  SchedResult.clear();
  auto IssueOrder = getIssueOrder(Offset, II);
  for (auto &Pair : OriToCycle) {
    assert(IssueOrder.count(Pair.first) && "Cannot find original MI!");
    SchedResult.push_back(std::make_tuple(Pair.first, Pair.second,
                                          getOriStage(Pair.first, Offset),
                                          IssueOrder[Pair.first]));
  }
}

void WindowScheduler::expand() {
  // The MIs in the SchedResult are sorted by the issue order ID.
  llvm::stable_sort(SchedResult,
                    [](const std::tuple<MachineInstr *, int, int, int> &A,
                       const std::tuple<MachineInstr *, int, int, int> &B) {
                      return std::get<3>(A) < std::get<3>(B);
                    });
  // Use the scheduling infrastructure for expansion, noting that InstrChanges
  // is not supported here.
  DenseMap<MachineInstr *, int> Cycles, Stages;
  std::vector<MachineInstr *> OrderedInsts;
  for (auto &Info : SchedResult) {
    auto *MI = std::get<0>(Info);
    OrderedInsts.push_back(MI);
    Cycles[MI] = std::get<1>(Info);
    Stages[MI] = std::get<2>(Info);
    LLVM_DEBUG(dbgs() << "\tCycle " << Cycles[MI] << " [S." << Stages[MI]
                      << "]: " << *MI);
  }
  ModuloSchedule MS(*MF, &Loop, std::move(OrderedInsts), std::move(Cycles),
                    std::move(Stages));
  ModuloScheduleExpander MSE(*MF, MS, *Context->LIS,
                             ModuloScheduleExpander::InstrChangesTy());
  MSE.expand();
  MSE.cleanup();
}

void WindowScheduler::updateLiveIntervals() {
  SmallVector<Register, 128> UsedRegs;
  for (MachineInstr &MI : *MBB)
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || MO.getReg() == 0)
        continue;
      Register Reg = MO.getReg();
      if (!is_contained(UsedRegs, Reg))
        UsedRegs.push_back(Reg);
    }
  Context->LIS->repairIntervalsInRange(MBB, MBB->begin(), MBB->end(), UsedRegs);
}

iterator_range<MachineBasicBlock::iterator>
WindowScheduler::getScheduleRange(unsigned Offset, unsigned Num) {
  auto RegionBegin = MBB->begin();
  std::advance(RegionBegin, Offset);
  auto RegionEnd = RegionBegin;
  std::advance(RegionEnd, Num);
  return make_range(RegionBegin, RegionEnd);
}

int WindowScheduler::getOriCycle(MachineInstr *NewMI) {
  assert(TriToOri.count(NewMI) && "Cannot find original MI!");
  auto *OriMI = TriToOri[NewMI];
  assert(OriToCycle.count(OriMI) && "Cannot find schedule cycle!");
  return OriToCycle[OriMI];
}

MachineInstr *WindowScheduler::getOriMI(MachineInstr *NewMI) {
  assert(TriToOri.count(NewMI) && "Cannot find original MI!");
  return TriToOri[NewMI];
}

unsigned WindowScheduler::getOriStage(MachineInstr *OriMI, unsigned Offset) {
  assert(llvm::find(OriMIs, OriMI) != OriMIs.end() &&
         "Cannot find OriMI in OriMIs!");
  // If there is no instruction fold, all MI stages are 0.
  if (Offset == SchedPhiNum)
    return 0;
  // For those MIs with an ID less than the Offset, their stages are set to 0,
  // while the rest are set to 1.
  unsigned Id = 0;
  for (auto *MI : OriMIs) {
    if (MI->isMetaInstruction())
      continue;
    if (MI == OriMI)
      break;
    ++Id;
  }
  return Id >= (size_t)Offset ? 1 : 0;
}

Register WindowScheduler::getAntiRegister(MachineInstr *Phi) {
  assert(Phi->isPHI() && "Expecting PHI!");
  Register AntiReg;
  for (auto MO : Phi->uses()) {
    if (MO.isReg())
      AntiReg = MO.getReg();
    else if (MO.isMBB() && MO.getMBB() == MBB)
      return AntiReg;
  }
  return 0;
}
