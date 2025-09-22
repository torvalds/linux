//===---------------------------- GCNILPSched.cpp - -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ScheduleDAG.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

namespace {

class GCNILPScheduler {
  struct Candidate : ilist_node<Candidate> {
    SUnit *SU;

    Candidate(SUnit *SU_)
      : SU(SU_) {}
  };

  SpecificBumpPtrAllocator<Candidate> Alloc;
  using Queue = simple_ilist<Candidate>;
  Queue PendingQueue;
  Queue AvailQueue;
  unsigned CurQueueId = 0;

  std::vector<unsigned> SUNumbers;

  /// CurCycle - The current scheduler state corresponds to this cycle.
  unsigned CurCycle = 0;

  unsigned getNodePriority(const SUnit *SU) const;

  const SUnit *pickBest(const SUnit *left, const SUnit *right);
  Candidate* pickCandidate();

  void releasePending();
  void advanceToCycle(unsigned NextCycle);
  void releasePredecessors(const SUnit* SU);

public:
  std::vector<const SUnit*> schedule(ArrayRef<const SUnit*> TopRoots,
                                     const ScheduleDAG &DAG);
};
} // namespace

/// CalcNodeSethiUllmanNumber - Compute Sethi Ullman number.
/// Smaller number is the higher priority.
static unsigned
CalcNodeSethiUllmanNumber(const SUnit *SU, std::vector<unsigned> &SUNumbers) {
  unsigned &SethiUllmanNumber = SUNumbers[SU->NodeNum];
  if (SethiUllmanNumber != 0)
    return SethiUllmanNumber;

  unsigned Extra = 0;
  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;  // ignore chain preds
    SUnit *PredSU = Pred.getSUnit();
    unsigned PredSethiUllman = CalcNodeSethiUllmanNumber(PredSU, SUNumbers);
    if (PredSethiUllman > SethiUllmanNumber) {
      SethiUllmanNumber = PredSethiUllman;
      Extra = 0;
    }
    else if (PredSethiUllman == SethiUllmanNumber)
      ++Extra;
  }

  SethiUllmanNumber += Extra;

  if (SethiUllmanNumber == 0)
    SethiUllmanNumber = 1;

  return SethiUllmanNumber;
}

// Lower priority means schedule further down. For bottom-up scheduling, lower
// priority SUs are scheduled before higher priority SUs.
unsigned GCNILPScheduler::getNodePriority(const SUnit *SU) const {
  assert(SU->NodeNum < SUNumbers.size());
  if (SU->NumSuccs == 0 && SU->NumPreds != 0)
    // If SU does not have a register use, i.e. it doesn't produce a value
    // that would be consumed (e.g. store), then it terminates a chain of
    // computation.  Give it a large SethiUllman number so it will be
    // scheduled right before its predecessors that it doesn't lengthen
    // their live ranges.
    return 0xffff;

  if (SU->NumPreds == 0 && SU->NumSuccs != 0)
    // If SU does not have a register def, schedule it close to its uses
    // because it does not lengthen any live ranges.
    return 0;

  return SUNumbers[SU->NodeNum];
}

/// closestSucc - Returns the scheduled cycle of the successor which is
/// closest to the current cycle.
static unsigned closestSucc(const SUnit *SU) {
  unsigned MaxHeight = 0;
  for (const SDep &Succ : SU->Succs) {
    if (Succ.isCtrl()) continue;  // ignore chain succs
    unsigned Height = Succ.getSUnit()->getHeight();
    // If there are bunch of CopyToRegs stacked up, they should be considered
    // to be at the same position.
    if (Height > MaxHeight)
      MaxHeight = Height;
  }
  return MaxHeight;
}

/// calcMaxScratches - Returns an cost estimate of the worse case requirement
/// for scratch registers, i.e. number of data dependencies.
static unsigned calcMaxScratches(const SUnit *SU) {
  unsigned Scratches = 0;
  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;  // ignore chain preds
    Scratches++;
  }
  return Scratches;
}

// Return -1 if left has higher priority, 1 if right has higher priority.
// Return 0 if latency-based priority is equivalent.
static int BUCompareLatency(const SUnit *left, const SUnit *right) {
  // Scheduling an instruction that uses a VReg whose postincrement has not yet
  // been scheduled will induce a copy. Model this as an extra cycle of latency.
  int LHeight = (int)left->getHeight();
  int RHeight = (int)right->getHeight();

  // If either node is scheduling for latency, sort them by height/depth
  // and latency.

  // If neither instruction stalls (!LStall && !RStall) and HazardRecognizer
  // is enabled, grouping instructions by cycle, then its height is already
  // covered so only its depth matters. We also reach this point if both stall
  // but have the same height.
  if (LHeight != RHeight)
    return LHeight > RHeight ? 1 : -1;

  int LDepth = left->getDepth();
  int RDepth = right->getDepth();
  if (LDepth != RDepth) {
    LLVM_DEBUG(dbgs() << "  Comparing latency of SU (" << left->NodeNum
                      << ") depth " << LDepth << " vs SU (" << right->NodeNum
                      << ") depth " << RDepth << "\n");
    return LDepth < RDepth ? 1 : -1;
  }
  if (left->Latency != right->Latency)
    return left->Latency > right->Latency ? 1 : -1;

  return 0;
}

const SUnit *GCNILPScheduler::pickBest(const SUnit *left, const SUnit *right)
{
  // TODO: add register pressure lowering checks

  bool const DisableSchedCriticalPath = false;
  int MaxReorderWindow = 6;
  if (!DisableSchedCriticalPath) {
    int spread = (int)left->getDepth() - (int)right->getDepth();
    if (std::abs(spread) > MaxReorderWindow) {
      LLVM_DEBUG(dbgs() << "Depth of SU(" << left->NodeNum << "): "
                        << left->getDepth() << " != SU(" << right->NodeNum
                        << "): " << right->getDepth() << "\n");
      return left->getDepth() < right->getDepth() ? right : left;
    }
  }

  bool const DisableSchedHeight = false;
  if (!DisableSchedHeight && left->getHeight() != right->getHeight()) {
    int spread = (int)left->getHeight() - (int)right->getHeight();
    if (std::abs(spread) > MaxReorderWindow)
      return left->getHeight() > right->getHeight() ? right : left;
  }

  // Prioritize by Sethi-Ulmann number and push CopyToReg nodes down.
  unsigned LPriority = getNodePriority(left);
  unsigned RPriority = getNodePriority(right);

  if (LPriority != RPriority)
    return LPriority > RPriority ? right : left;

  // Try schedule def + use closer when Sethi-Ullman numbers are the same.
  // e.g.
  // t1 = op t2, c1
  // t3 = op t4, c2
  //
  // and the following instructions are both ready.
  // t2 = op c3
  // t4 = op c4
  //
  // Then schedule t2 = op first.
  // i.e.
  // t4 = op c4
  // t2 = op c3
  // t1 = op t2, c1
  // t3 = op t4, c2
  //
  // This creates more short live intervals.
  unsigned LDist = closestSucc(left);
  unsigned RDist = closestSucc(right);
  if (LDist != RDist)
    return LDist < RDist ? right : left;

  // How many registers becomes live when the node is scheduled.
  unsigned LScratch = calcMaxScratches(left);
  unsigned RScratch = calcMaxScratches(right);
  if (LScratch != RScratch)
    return LScratch > RScratch ? right : left;

  bool const DisableSchedCycles = false;
  if (!DisableSchedCycles) {
    int result = BUCompareLatency(left, right);
    if (result != 0)
      return result > 0 ? right : left;
    return left;
  }
  if (left->getHeight() != right->getHeight())
    return (left->getHeight() > right->getHeight()) ? right : left;

  if (left->getDepth() != right->getDepth())
    return (left->getDepth() < right->getDepth()) ? right : left;

  assert(left->NodeQueueId && right->NodeQueueId &&
        "NodeQueueId cannot be zero");
  return (left->NodeQueueId > right->NodeQueueId) ? right : left;
}

GCNILPScheduler::Candidate* GCNILPScheduler::pickCandidate() {
  if (AvailQueue.empty())
    return nullptr;
  auto Best = AvailQueue.begin();
  for (auto I = std::next(AvailQueue.begin()), E = AvailQueue.end(); I != E; ++I) {
    auto NewBestSU = pickBest(Best->SU, I->SU);
    if (NewBestSU != Best->SU) {
      assert(NewBestSU == I->SU);
      Best = I;
    }
  }
  return &*Best;
}

void GCNILPScheduler::releasePending() {
  // Check to see if any of the pending instructions are ready to issue.  If
  // so, add them to the available queue.
  for(auto I = PendingQueue.begin(), E = PendingQueue.end(); I != E;) {
    auto &C = *I++;
    if (C.SU->getHeight() <= CurCycle) {
      PendingQueue.remove(C);
      AvailQueue.push_back(C);
      C.SU->NodeQueueId = CurQueueId++;
    }
  }
}

/// Move the scheduler state forward by the specified number of Cycles.
void GCNILPScheduler::advanceToCycle(unsigned NextCycle) {
  if (NextCycle <= CurCycle)
    return;
  CurCycle = NextCycle;
  releasePending();
}

void GCNILPScheduler::releasePredecessors(const SUnit* SU) {
  for (const auto &PredEdge : SU->Preds) {
    auto PredSU = PredEdge.getSUnit();
    if (PredEdge.isWeak())
      continue;
    assert(PredSU->isBoundaryNode() || PredSU->NumSuccsLeft > 0);

    PredSU->setHeightToAtLeast(SU->getHeight() + PredEdge.getLatency());

    if (!PredSU->isBoundaryNode() && --PredSU->NumSuccsLeft == 0)
      PendingQueue.push_front(*new (Alloc.Allocate()) Candidate(PredSU));
  }
}

std::vector<const SUnit*>
GCNILPScheduler::schedule(ArrayRef<const SUnit*> BotRoots,
                          const ScheduleDAG &DAG) {
  auto &SUnits = const_cast<ScheduleDAG&>(DAG).SUnits;

  std::vector<SUnit> SUSavedCopy;
  SUSavedCopy.resize(SUnits.size());

  // we cannot save only those fields we touch: some of them are private
  // so save units verbatim: this assumes SUnit should have value semantics
  for (const SUnit &SU : SUnits)
    SUSavedCopy[SU.NodeNum] = SU;

  SUNumbers.assign(SUnits.size(), 0);
  for (const SUnit &SU : SUnits)
    CalcNodeSethiUllmanNumber(&SU, SUNumbers);

  for (const auto *SU : BotRoots) {
    AvailQueue.push_back(
      *new (Alloc.Allocate()) Candidate(const_cast<SUnit*>(SU)));
  }
  releasePredecessors(&DAG.ExitSU);

  std::vector<const SUnit*> Schedule;
  Schedule.reserve(SUnits.size());
  while (true) {
    if (AvailQueue.empty() && !PendingQueue.empty()) {
      auto EarliestSU =
          llvm::min_element(PendingQueue, [=](const Candidate &C1,
                                              const Candidate &C2) {
            return C1.SU->getHeight() < C2.SU->getHeight();
          })->SU;
      advanceToCycle(std::max(CurCycle + 1, EarliestSU->getHeight()));
    }
    if (AvailQueue.empty())
      break;

    LLVM_DEBUG(dbgs() << "\n=== Picking candidate\n"
                         "Ready queue:";
               for (auto &C
                    : AvailQueue) dbgs()
               << ' ' << C.SU->NodeNum;
               dbgs() << '\n';);

    auto C = pickCandidate();
    assert(C);
    AvailQueue.remove(*C);
    auto SU = C->SU;
    LLVM_DEBUG(dbgs() << "Selected "; DAG.dumpNode(*SU));

    advanceToCycle(SU->getHeight());

    releasePredecessors(SU);
    Schedule.push_back(SU);
    SU->isScheduled = true;
  }
  assert(SUnits.size() == Schedule.size());

  std::reverse(Schedule.begin(), Schedule.end());

  // restore units
  for (auto &SU : SUnits)
    SU = SUSavedCopy[SU.NodeNum];

  return Schedule;
}

namespace llvm {
std::vector<const SUnit*> makeGCNILPScheduler(ArrayRef<const SUnit*> BotRoots,
                                              const ScheduleDAG &DAG) {
  GCNILPScheduler S;
  return S.schedule(BotRoots, DAG);
}
} // namespace llvm
