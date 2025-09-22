//===- ScheduleDAG.cpp - Implement the ScheduleDAG class ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the ScheduleDAG class, which is a base class used by
/// scheduling implementation classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "pre-RA-sched"

STATISTIC(NumNewPredsAdded, "Number of times a  single predecessor was added");
STATISTIC(NumTopoInits,
          "Number of times the topological order has been recomputed");

#ifndef NDEBUG
static cl::opt<bool> StressSchedOpt(
  "stress-sched", cl::Hidden, cl::init(false),
  cl::desc("Stress test instruction scheduling"));
#endif

void SchedulingPriorityQueue::anchor() {}

ScheduleDAG::ScheduleDAG(MachineFunction &mf)
    : TM(mf.getTarget()), TII(mf.getSubtarget().getInstrInfo()),
      TRI(mf.getSubtarget().getRegisterInfo()), MF(mf),
      MRI(mf.getRegInfo()) {
#ifndef NDEBUG
  StressSched = StressSchedOpt;
#endif
}

ScheduleDAG::~ScheduleDAG() = default;

void ScheduleDAG::clearDAG() {
  SUnits.clear();
  EntrySU = SUnit();
  ExitSU = SUnit();
}

const MCInstrDesc *ScheduleDAG::getNodeDesc(const SDNode *Node) const {
  if (!Node || !Node->isMachineOpcode()) return nullptr;
  return &TII->get(Node->getMachineOpcode());
}

LLVM_DUMP_METHOD void SDep::dump(const TargetRegisterInfo *TRI) const {
  switch (getKind()) {
  case Data:   dbgs() << "Data"; break;
  case Anti:   dbgs() << "Anti"; break;
  case Output: dbgs() << "Out "; break;
  case Order:  dbgs() << "Ord "; break;
  }

  switch (getKind()) {
  case Data:
    dbgs() << " Latency=" << getLatency();
    if (TRI && isAssignedRegDep())
      dbgs() << " Reg=" << printReg(getReg(), TRI);
    break;
  case Anti:
  case Output:
    dbgs() << " Latency=" << getLatency();
    break;
  case Order:
    dbgs() << " Latency=" << getLatency();
    switch(Contents.OrdKind) {
    case Barrier:      dbgs() << " Barrier"; break;
    case MayAliasMem:
    case MustAliasMem: dbgs() << " Memory"; break;
    case Artificial:   dbgs() << " Artificial"; break;
    case Weak:         dbgs() << " Weak"; break;
    case Cluster:      dbgs() << " Cluster"; break;
    }
    break;
  }
}

bool SUnit::addPred(const SDep &D, bool Required) {
  // If this node already has this dependence, don't add a redundant one.
  for (SDep &PredDep : Preds) {
    // Zero-latency weak edges may be added purely for heuristic ordering. Don't
    // add them if another kind of edge already exists.
    if (!Required && PredDep.getSUnit() == D.getSUnit())
      return false;
    if (PredDep.overlaps(D)) {
      // Extend the latency if needed. Equivalent to
      // removePred(PredDep) + addPred(D).
      if (PredDep.getLatency() < D.getLatency()) {
        SUnit *PredSU = PredDep.getSUnit();
        // Find the corresponding successor in N.
        SDep ForwardD = PredDep;
        ForwardD.setSUnit(this);
        for (SDep &SuccDep : PredSU->Succs) {
          if (SuccDep == ForwardD) {
            SuccDep.setLatency(D.getLatency());
            break;
          }
        }
        PredDep.setLatency(D.getLatency());
      }
      return false;
    }
  }
  // Now add a corresponding succ to N.
  SDep P = D;
  P.setSUnit(this);
  SUnit *N = D.getSUnit();
  // Update the bookkeeping.
  if (D.getKind() == SDep::Data) {
    assert(NumPreds < std::numeric_limits<unsigned>::max() &&
           "NumPreds will overflow!");
    assert(N->NumSuccs < std::numeric_limits<unsigned>::max() &&
           "NumSuccs will overflow!");
    ++NumPreds;
    ++N->NumSuccs;
  }
  if (!N->isScheduled) {
    if (D.isWeak()) {
      ++WeakPredsLeft;
    }
    else {
      assert(NumPredsLeft < std::numeric_limits<unsigned>::max() &&
             "NumPredsLeft will overflow!");
      ++NumPredsLeft;
    }
  }
  if (!isScheduled) {
    if (D.isWeak()) {
      ++N->WeakSuccsLeft;
    }
    else {
      assert(N->NumSuccsLeft < std::numeric_limits<unsigned>::max() &&
             "NumSuccsLeft will overflow!");
      ++N->NumSuccsLeft;
    }
  }
  Preds.push_back(D);
  N->Succs.push_back(P);
  if (P.getLatency() != 0) {
    this->setDepthDirty();
    N->setHeightDirty();
  }
  return true;
}

void SUnit::removePred(const SDep &D) {
  // Find the matching predecessor.
  SmallVectorImpl<SDep>::iterator I = llvm::find(Preds, D);
  if (I == Preds.end())
    return;
  // Find the corresponding successor in N.
  SDep P = D;
  P.setSUnit(this);
  SUnit *N = D.getSUnit();
  SmallVectorImpl<SDep>::iterator Succ = llvm::find(N->Succs, P);
  assert(Succ != N->Succs.end() && "Mismatching preds / succs lists!");
  // Update the bookkeeping.
  if (P.getKind() == SDep::Data) {
    assert(NumPreds > 0 && "NumPreds will underflow!");
    assert(N->NumSuccs > 0 && "NumSuccs will underflow!");
    --NumPreds;
    --N->NumSuccs;
  }
  if (!N->isScheduled) {
    if (D.isWeak()) {
      assert(WeakPredsLeft > 0 && "WeakPredsLeft will underflow!");
      --WeakPredsLeft;
    } else {
      assert(NumPredsLeft > 0 && "NumPredsLeft will underflow!");
      --NumPredsLeft;
    }
  }
  if (!isScheduled) {
    if (D.isWeak()) {
      assert(N->WeakSuccsLeft > 0 && "WeakSuccsLeft will underflow!");
      --N->WeakSuccsLeft;
    } else {
      assert(N->NumSuccsLeft > 0 && "NumSuccsLeft will underflow!");
      --N->NumSuccsLeft;
    }
  }
  N->Succs.erase(Succ);
  Preds.erase(I);
  if (P.getLatency() != 0) {
    this->setDepthDirty();
    N->setHeightDirty();
  }
}

void SUnit::setDepthDirty() {
  if (!isDepthCurrent) return;
  SmallVector<SUnit*, 8> WorkList;
  WorkList.push_back(this);
  do {
    SUnit *SU = WorkList.pop_back_val();
    SU->isDepthCurrent = false;
    for (SDep &SuccDep : SU->Succs) {
      SUnit *SuccSU = SuccDep.getSUnit();
      if (SuccSU->isDepthCurrent)
        WorkList.push_back(SuccSU);
    }
  } while (!WorkList.empty());
}

void SUnit::setHeightDirty() {
  if (!isHeightCurrent) return;
  SmallVector<SUnit*, 8> WorkList;
  WorkList.push_back(this);
  do {
    SUnit *SU = WorkList.pop_back_val();
    SU->isHeightCurrent = false;
    for (SDep &PredDep : SU->Preds) {
      SUnit *PredSU = PredDep.getSUnit();
      if (PredSU->isHeightCurrent)
        WorkList.push_back(PredSU);
    }
  } while (!WorkList.empty());
}

void SUnit::setDepthToAtLeast(unsigned NewDepth) {
  if (NewDepth <= getDepth())
    return;
  setDepthDirty();
  Depth = NewDepth;
  isDepthCurrent = true;
}

void SUnit::setHeightToAtLeast(unsigned NewHeight) {
  if (NewHeight <= getHeight())
    return;
  setHeightDirty();
  Height = NewHeight;
  isHeightCurrent = true;
}

/// Calculates the maximal path from the node to the exit.
void SUnit::ComputeDepth() {
  SmallVector<SUnit*, 8> WorkList;
  WorkList.push_back(this);
  do {
    SUnit *Cur = WorkList.back();

    bool Done = true;
    unsigned MaxPredDepth = 0;
    for (const SDep &PredDep : Cur->Preds) {
      SUnit *PredSU = PredDep.getSUnit();
      if (PredSU->isDepthCurrent)
        MaxPredDepth = std::max(MaxPredDepth,
                                PredSU->Depth + PredDep.getLatency());
      else {
        Done = false;
        WorkList.push_back(PredSU);
      }
    }

    if (Done) {
      WorkList.pop_back();
      if (MaxPredDepth != Cur->Depth) {
        Cur->setDepthDirty();
        Cur->Depth = MaxPredDepth;
      }
      Cur->isDepthCurrent = true;
    }
  } while (!WorkList.empty());
}

/// Calculates the maximal path from the node to the entry.
void SUnit::ComputeHeight() {
  SmallVector<SUnit*, 8> WorkList;
  WorkList.push_back(this);
  do {
    SUnit *Cur = WorkList.back();

    bool Done = true;
    unsigned MaxSuccHeight = 0;
    for (const SDep &SuccDep : Cur->Succs) {
      SUnit *SuccSU = SuccDep.getSUnit();
      if (SuccSU->isHeightCurrent)
        MaxSuccHeight = std::max(MaxSuccHeight,
                                 SuccSU->Height + SuccDep.getLatency());
      else {
        Done = false;
        WorkList.push_back(SuccSU);
      }
    }

    if (Done) {
      WorkList.pop_back();
      if (MaxSuccHeight != Cur->Height) {
        Cur->setHeightDirty();
        Cur->Height = MaxSuccHeight;
      }
      Cur->isHeightCurrent = true;
    }
  } while (!WorkList.empty());
}

void SUnit::biasCriticalPath() {
  if (NumPreds < 2)
    return;

  SUnit::pred_iterator BestI = Preds.begin();
  unsigned MaxDepth = BestI->getSUnit()->getDepth();
  for (SUnit::pred_iterator I = std::next(BestI), E = Preds.end(); I != E;
       ++I) {
    if (I->getKind() == SDep::Data && I->getSUnit()->getDepth() > MaxDepth) {
      MaxDepth = I->getSUnit()->getDepth();
      BestI = I;
    }
  }
  if (BestI != Preds.begin())
    std::swap(*Preds.begin(), *BestI);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SUnit::dumpAttributes() const {
  dbgs() << "  # preds left       : " << NumPredsLeft << "\n";
  dbgs() << "  # succs left       : " << NumSuccsLeft << "\n";
  if (WeakPredsLeft)
    dbgs() << "  # weak preds left  : " << WeakPredsLeft << "\n";
  if (WeakSuccsLeft)
    dbgs() << "  # weak succs left  : " << WeakSuccsLeft << "\n";
  dbgs() << "  # rdefs left       : " << NumRegDefsLeft << "\n";
  dbgs() << "  Latency            : " << Latency << "\n";
  dbgs() << "  Depth              : " << getDepth() << "\n";
  dbgs() << "  Height             : " << getHeight() << "\n";
}

LLVM_DUMP_METHOD void ScheduleDAG::dumpNodeName(const SUnit &SU) const {
  if (&SU == &EntrySU)
    dbgs() << "EntrySU";
  else if (&SU == &ExitSU)
    dbgs() << "ExitSU";
  else
    dbgs() << "SU(" << SU.NodeNum << ")";
}

LLVM_DUMP_METHOD void ScheduleDAG::dumpNodeAll(const SUnit &SU) const {
  dumpNode(SU);
  SU.dumpAttributes();
  if (SU.Preds.size() > 0) {
    dbgs() << "  Predecessors:\n";
    for (const SDep &Dep : SU.Preds) {
      dbgs() << "    ";
      dumpNodeName(*Dep.getSUnit());
      dbgs() << ": ";
      Dep.dump(TRI);
      dbgs() << '\n';
    }
  }
  if (SU.Succs.size() > 0) {
    dbgs() << "  Successors:\n";
    for (const SDep &Dep : SU.Succs) {
      dbgs() << "    ";
      dumpNodeName(*Dep.getSUnit());
      dbgs() << ": ";
      Dep.dump(TRI);
      dbgs() << '\n';
    }
  }
}
#endif

#ifndef NDEBUG
unsigned ScheduleDAG::VerifyScheduledDAG(bool isBottomUp) {
  bool AnyNotSched = false;
  unsigned DeadNodes = 0;
  for (const SUnit &SUnit : SUnits) {
    if (!SUnit.isScheduled) {
      if (SUnit.NumPreds == 0 && SUnit.NumSuccs == 0) {
        ++DeadNodes;
        continue;
      }
      if (!AnyNotSched)
        dbgs() << "*** Scheduling failed! ***\n";
      dumpNode(SUnit);
      dbgs() << "has not been scheduled!\n";
      AnyNotSched = true;
    }
    if (SUnit.isScheduled &&
        (isBottomUp ? SUnit.getHeight() : SUnit.getDepth()) >
          unsigned(std::numeric_limits<int>::max())) {
      if (!AnyNotSched)
        dbgs() << "*** Scheduling failed! ***\n";
      dumpNode(SUnit);
      dbgs() << "has an unexpected "
           << (isBottomUp ? "Height" : "Depth") << " value!\n";
      AnyNotSched = true;
    }
    if (isBottomUp) {
      if (SUnit.NumSuccsLeft != 0) {
        if (!AnyNotSched)
          dbgs() << "*** Scheduling failed! ***\n";
        dumpNode(SUnit);
        dbgs() << "has successors left!\n";
        AnyNotSched = true;
      }
    } else {
      if (SUnit.NumPredsLeft != 0) {
        if (!AnyNotSched)
          dbgs() << "*** Scheduling failed! ***\n";
        dumpNode(SUnit);
        dbgs() << "has predecessors left!\n";
        AnyNotSched = true;
      }
    }
  }
  assert(!AnyNotSched);
  return SUnits.size() - DeadNodes;
}
#endif

void ScheduleDAGTopologicalSort::InitDAGTopologicalSorting() {
  // The idea of the algorithm is taken from
  // "Online algorithms for managing the topological order of
  // a directed acyclic graph" by David J. Pearce and Paul H.J. Kelly
  // This is the MNR algorithm, which was first introduced by
  // A. Marchetti-Spaccamela, U. Nanni and H. Rohnert in
  // "Maintaining a topological order under edge insertions".
  //
  // Short description of the algorithm:
  //
  // Topological ordering, ord, of a DAG maps each node to a topological
  // index so that for all edges X->Y it is the case that ord(X) < ord(Y).
  //
  // This means that if there is a path from the node X to the node Z,
  // then ord(X) < ord(Z).
  //
  // This property can be used to check for reachability of nodes:
  // if Z is reachable from X, then an insertion of the edge Z->X would
  // create a cycle.
  //
  // The algorithm first computes a topological ordering for the DAG by
  // initializing the Index2Node and Node2Index arrays and then tries to keep
  // the ordering up-to-date after edge insertions by reordering the DAG.
  //
  // On insertion of the edge X->Y, the algorithm first marks by calling DFS
  // the nodes reachable from Y, and then shifts them using Shift to lie
  // immediately after X in Index2Node.

  // Cancel pending updates, mark as valid.
  Dirty = false;
  Updates.clear();

  unsigned DAGSize = SUnits.size();
  std::vector<SUnit*> WorkList;
  WorkList.reserve(DAGSize);

  Index2Node.resize(DAGSize);
  Node2Index.resize(DAGSize);

  // Initialize the data structures.
  if (ExitSU)
    WorkList.push_back(ExitSU);
  for (SUnit &SU : SUnits) {
    int NodeNum = SU.NodeNum;
    unsigned Degree = SU.Succs.size();
    // Temporarily use the Node2Index array as scratch space for degree counts.
    Node2Index[NodeNum] = Degree;

    // Is it a node without dependencies?
    if (Degree == 0) {
      assert(SU.Succs.empty() && "SUnit should have no successors");
      // Collect leaf nodes.
      WorkList.push_back(&SU);
    }
  }

  int Id = DAGSize;
  while (!WorkList.empty()) {
    SUnit *SU = WorkList.back();
    WorkList.pop_back();
    if (SU->NodeNum < DAGSize)
      Allocate(SU->NodeNum, --Id);
    for (const SDep &PredDep : SU->Preds) {
      SUnit *SU = PredDep.getSUnit();
      if (SU->NodeNum < DAGSize && !--Node2Index[SU->NodeNum])
        // If all dependencies of the node are processed already,
        // then the node can be computed now.
        WorkList.push_back(SU);
    }
  }

  Visited.resize(DAGSize);
  NumTopoInits++;

#ifndef NDEBUG
  // Check correctness of the ordering
  for (SUnit &SU : SUnits)  {
    for (const SDep &PD : SU.Preds) {
      assert(Node2Index[SU.NodeNum] > Node2Index[PD.getSUnit()->NodeNum] &&
      "Wrong topological sorting");
    }
  }
#endif
}

void ScheduleDAGTopologicalSort::FixOrder() {
  // Recompute from scratch after new nodes have been added.
  if (Dirty) {
    InitDAGTopologicalSorting();
    return;
  }

  // Otherwise apply updates one-by-one.
  for (auto &U : Updates)
    AddPred(U.first, U.second);
  Updates.clear();
}

void ScheduleDAGTopologicalSort::AddPredQueued(SUnit *Y, SUnit *X) {
  // Recomputing the order from scratch is likely more efficient than applying
  // updates one-by-one for too many updates. The current cut-off is arbitrarily
  // chosen.
  Dirty = Dirty || Updates.size() > 10;

  if (Dirty)
    return;

  Updates.emplace_back(Y, X);
}

void ScheduleDAGTopologicalSort::AddPred(SUnit *Y, SUnit *X) {
  int UpperBound, LowerBound;
  LowerBound = Node2Index[Y->NodeNum];
  UpperBound = Node2Index[X->NodeNum];
  bool HasLoop = false;
  // Is Ord(X) < Ord(Y) ?
  if (LowerBound < UpperBound) {
    // Update the topological order.
    Visited.reset();
    DFS(Y, UpperBound, HasLoop);
    assert(!HasLoop && "Inserted edge creates a loop!");
    // Recompute topological indexes.
    Shift(Visited, LowerBound, UpperBound);
  }

  NumNewPredsAdded++;
}

void ScheduleDAGTopologicalSort::RemovePred(SUnit *M, SUnit *N) {
  // InitDAGTopologicalSorting();
}

void ScheduleDAGTopologicalSort::DFS(const SUnit *SU, int UpperBound,
                                     bool &HasLoop) {
  std::vector<const SUnit*> WorkList;
  WorkList.reserve(SUnits.size());

  WorkList.push_back(SU);
  do {
    SU = WorkList.back();
    WorkList.pop_back();
    Visited.set(SU->NodeNum);
    for (const SDep &SuccDep : llvm::reverse(SU->Succs)) {
      unsigned s = SuccDep.getSUnit()->NodeNum;
      // Edges to non-SUnits are allowed but ignored (e.g. ExitSU).
      if (s >= Node2Index.size())
        continue;
      if (Node2Index[s] == UpperBound) {
        HasLoop = true;
        return;
      }
      // Visit successors if not already and in affected region.
      if (!Visited.test(s) && Node2Index[s] < UpperBound) {
        WorkList.push_back(SuccDep.getSUnit());
      }
    }
  } while (!WorkList.empty());
}

std::vector<int> ScheduleDAGTopologicalSort::GetSubGraph(const SUnit &StartSU,
                                                         const SUnit &TargetSU,
                                                         bool &Success) {
  std::vector<const SUnit*> WorkList;
  int LowerBound = Node2Index[StartSU.NodeNum];
  int UpperBound = Node2Index[TargetSU.NodeNum];
  bool Found = false;
  BitVector VisitedBack;
  std::vector<int> Nodes;

  if (LowerBound > UpperBound) {
    Success = false;
    return Nodes;
  }

  WorkList.reserve(SUnits.size());
  Visited.reset();

  // Starting from StartSU, visit all successors up
  // to UpperBound.
  WorkList.push_back(&StartSU);
  do {
    const SUnit *SU = WorkList.back();
    WorkList.pop_back();
    for (const SDep &SD : llvm::reverse(SU->Succs)) {
      const SUnit *Succ = SD.getSUnit();
      unsigned s = Succ->NodeNum;
      // Edges to non-SUnits are allowed but ignored (e.g. ExitSU).
      if (Succ->isBoundaryNode())
        continue;
      if (Node2Index[s] == UpperBound) {
        Found = true;
        continue;
      }
      // Visit successors if not already and in affected region.
      if (!Visited.test(s) && Node2Index[s] < UpperBound) {
        Visited.set(s);
        WorkList.push_back(Succ);
      }
    }
  } while (!WorkList.empty());

  if (!Found) {
    Success = false;
    return Nodes;
  }

  WorkList.clear();
  VisitedBack.resize(SUnits.size());
  Found = false;

  // Starting from TargetSU, visit all predecessors up
  // to LowerBound. SUs that are visited by the two
  // passes are added to Nodes.
  WorkList.push_back(&TargetSU);
  do {
    const SUnit *SU = WorkList.back();
    WorkList.pop_back();
    for (const SDep &SD : llvm::reverse(SU->Preds)) {
      const SUnit *Pred = SD.getSUnit();
      unsigned s = Pred->NodeNum;
      // Edges to non-SUnits are allowed but ignored (e.g. EntrySU).
      if (Pred->isBoundaryNode())
        continue;
      if (Node2Index[s] == LowerBound) {
        Found = true;
        continue;
      }
      if (!VisitedBack.test(s) && Visited.test(s)) {
        VisitedBack.set(s);
        WorkList.push_back(Pred);
        Nodes.push_back(s);
      }
    }
  } while (!WorkList.empty());

  assert(Found && "Error in SUnit Graph!");
  Success = true;
  return Nodes;
}

void ScheduleDAGTopologicalSort::Shift(BitVector& Visited, int LowerBound,
                                       int UpperBound) {
  std::vector<int> L;
  int shift = 0;
  int i;

  for (i = LowerBound; i <= UpperBound; ++i) {
    // w is node at topological index i.
    int w = Index2Node[i];
    if (Visited.test(w)) {
      // Unmark.
      Visited.reset(w);
      L.push_back(w);
      shift = shift + 1;
    } else {
      Allocate(w, i - shift);
    }
  }

  for (unsigned LI : L) {
    Allocate(LI, i - shift);
    i = i + 1;
  }
}

bool ScheduleDAGTopologicalSort::WillCreateCycle(SUnit *TargetSU, SUnit *SU) {
  FixOrder();
  // Is SU reachable from TargetSU via successor edges?
  if (IsReachable(SU, TargetSU))
    return true;
  for (const SDep &PredDep : TargetSU->Preds)
    if (PredDep.isAssignedRegDep() &&
        IsReachable(SU, PredDep.getSUnit()))
      return true;
  return false;
}

void ScheduleDAGTopologicalSort::AddSUnitWithoutPredecessors(const SUnit *SU) {
  assert(SU->NodeNum == Index2Node.size() && "Node cannot be added at the end");
  assert(SU->NumPreds == 0 && "Can only add SU's with no predecessors");
  Node2Index.push_back(Index2Node.size());
  Index2Node.push_back(SU->NodeNum);
  Visited.resize(Node2Index.size());
}

bool ScheduleDAGTopologicalSort::IsReachable(const SUnit *SU,
                                             const SUnit *TargetSU) {
  assert(TargetSU != nullptr && "Invalid target SUnit");
  assert(SU != nullptr && "Invalid SUnit");
  FixOrder();
  // If insertion of the edge SU->TargetSU would create a cycle
  // then there is a path from TargetSU to SU.
  int UpperBound, LowerBound;
  LowerBound = Node2Index[TargetSU->NodeNum];
  UpperBound = Node2Index[SU->NodeNum];
  bool HasLoop = false;
  // Is Ord(TargetSU) < Ord(SU) ?
  if (LowerBound < UpperBound) {
    Visited.reset();
    // There may be a path from TargetSU to SU. Check for it.
    DFS(TargetSU, UpperBound, HasLoop);
  }
  return HasLoop;
}

void ScheduleDAGTopologicalSort::Allocate(int n, int index) {
  Node2Index[n] = index;
  Index2Node[index] = n;
}

ScheduleDAGTopologicalSort::
ScheduleDAGTopologicalSort(std::vector<SUnit> &sunits, SUnit *exitsu)
  : SUnits(sunits), ExitSU(exitsu) {}

ScheduleHazardRecognizer::~ScheduleHazardRecognizer() = default;
