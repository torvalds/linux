//===- ScheduleDAGRRList.cpp - Reg pressure reduction list scheduler ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements bottom-up and top-down register pressure reduction list
// schedulers, using standard algorithms.  The basic approach uses a priority
// queue of available nodes to schedule.  One at a time, nodes are taken from
// the priority queue (thus in priority order), checked for legality to
// schedule, and emitted if legal.
//
//===----------------------------------------------------------------------===//

#include "ScheduleDAGSDNodes.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "pre-RA-sched"

STATISTIC(NumBacktracks, "Number of times scheduler backtracked");
STATISTIC(NumUnfolds,    "Number of nodes unfolded");
STATISTIC(NumDups,       "Number of duplicated nodes");
STATISTIC(NumPRCopies,   "Number of physical register copies");

static RegisterScheduler
  burrListDAGScheduler("list-burr",
                       "Bottom-up register reduction list scheduling",
                       createBURRListDAGScheduler);

static RegisterScheduler
  sourceListDAGScheduler("source",
                         "Similar to list-burr but schedules in source "
                         "order when possible",
                         createSourceListDAGScheduler);

static RegisterScheduler
  hybridListDAGScheduler("list-hybrid",
                         "Bottom-up register pressure aware list scheduling "
                         "which tries to balance latency and register pressure",
                         createHybridListDAGScheduler);

static RegisterScheduler
  ILPListDAGScheduler("list-ilp",
                      "Bottom-up register pressure aware list scheduling "
                      "which tries to balance ILP and register pressure",
                      createILPListDAGScheduler);

static cl::opt<bool> DisableSchedCycles(
  "disable-sched-cycles", cl::Hidden, cl::init(false),
  cl::desc("Disable cycle-level precision during preRA scheduling"));

// Temporary sched=list-ilp flags until the heuristics are robust.
// Some options are also available under sched=list-hybrid.
static cl::opt<bool> DisableSchedRegPressure(
  "disable-sched-reg-pressure", cl::Hidden, cl::init(false),
  cl::desc("Disable regpressure priority in sched=list-ilp"));
static cl::opt<bool> DisableSchedLiveUses(
  "disable-sched-live-uses", cl::Hidden, cl::init(true),
  cl::desc("Disable live use priority in sched=list-ilp"));
static cl::opt<bool> DisableSchedVRegCycle(
  "disable-sched-vrcycle", cl::Hidden, cl::init(false),
  cl::desc("Disable virtual register cycle interference checks"));
static cl::opt<bool> DisableSchedPhysRegJoin(
  "disable-sched-physreg-join", cl::Hidden, cl::init(false),
  cl::desc("Disable physreg def-use affinity"));
static cl::opt<bool> DisableSchedStalls(
  "disable-sched-stalls", cl::Hidden, cl::init(true),
  cl::desc("Disable no-stall priority in sched=list-ilp"));
static cl::opt<bool> DisableSchedCriticalPath(
  "disable-sched-critical-path", cl::Hidden, cl::init(false),
  cl::desc("Disable critical path priority in sched=list-ilp"));
static cl::opt<bool> DisableSchedHeight(
  "disable-sched-height", cl::Hidden, cl::init(false),
  cl::desc("Disable scheduled-height priority in sched=list-ilp"));
static cl::opt<bool> Disable2AddrHack(
  "disable-2addr-hack", cl::Hidden, cl::init(true),
  cl::desc("Disable scheduler's two-address hack"));

static cl::opt<int> MaxReorderWindow(
  "max-sched-reorder", cl::Hidden, cl::init(6),
  cl::desc("Number of instructions to allow ahead of the critical path "
           "in sched=list-ilp"));

static cl::opt<unsigned> AvgIPC(
  "sched-avg-ipc", cl::Hidden, cl::init(1),
  cl::desc("Average inst/cycle whan no target itinerary exists."));

namespace {

//===----------------------------------------------------------------------===//
/// ScheduleDAGRRList - The actual register reduction list scheduler
/// implementation.  This supports both top-down and bottom-up scheduling.
///
class ScheduleDAGRRList : public ScheduleDAGSDNodes {
private:
  /// NeedLatency - True if the scheduler will make use of latency information.
  bool NeedLatency;

  /// AvailableQueue - The priority queue to use for the available SUnits.
  SchedulingPriorityQueue *AvailableQueue;

  /// PendingQueue - This contains all of the instructions whose operands have
  /// been issued, but their results are not ready yet (due to the latency of
  /// the operation).  Once the operands becomes available, the instruction is
  /// added to the AvailableQueue.
  std::vector<SUnit *> PendingQueue;

  /// HazardRec - The hazard recognizer to use.
  ScheduleHazardRecognizer *HazardRec;

  /// CurCycle - The current scheduler state corresponds to this cycle.
  unsigned CurCycle = 0;

  /// MinAvailableCycle - Cycle of the soonest available instruction.
  unsigned MinAvailableCycle = ~0u;

  /// IssueCount - Count instructions issued in this cycle
  /// Currently valid only for bottom-up scheduling.
  unsigned IssueCount = 0u;

  /// LiveRegDefs - A set of physical registers and their definition
  /// that are "live". These nodes must be scheduled before any other nodes that
  /// modifies the registers can be scheduled.
  unsigned NumLiveRegs = 0u;
  std::unique_ptr<SUnit*[]> LiveRegDefs;
  std::unique_ptr<SUnit*[]> LiveRegGens;

  // Collect interferences between physical register use/defs.
  // Each interference is an SUnit and set of physical registers.
  SmallVector<SUnit*, 4> Interferences;

  using LRegsMapT = DenseMap<SUnit *, SmallVector<unsigned, 4>>;

  LRegsMapT LRegsMap;

  /// Topo - A topological ordering for SUnits which permits fast IsReachable
  /// and similar queries.
  ScheduleDAGTopologicalSort Topo;

  // Hack to keep track of the inverse of FindCallSeqStart without more crazy
  // DAG crawling.
  DenseMap<SUnit*, SUnit*> CallSeqEndForStart;

public:
  ScheduleDAGRRList(MachineFunction &mf, bool needlatency,
                    SchedulingPriorityQueue *availqueue,
                    CodeGenOptLevel OptLevel)
      : ScheduleDAGSDNodes(mf), NeedLatency(needlatency),
        AvailableQueue(availqueue), Topo(SUnits, nullptr) {
    const TargetSubtargetInfo &STI = mf.getSubtarget();
    if (DisableSchedCycles || !NeedLatency)
      HazardRec = new ScheduleHazardRecognizer();
    else
      HazardRec = STI.getInstrInfo()->CreateTargetHazardRecognizer(&STI, this);
  }

  ~ScheduleDAGRRList() override {
    delete HazardRec;
    delete AvailableQueue;
  }

  void Schedule() override;

  ScheduleHazardRecognizer *getHazardRec() { return HazardRec; }

  /// IsReachable - Checks if SU is reachable from TargetSU.
  bool IsReachable(const SUnit *SU, const SUnit *TargetSU) {
    return Topo.IsReachable(SU, TargetSU);
  }

  /// WillCreateCycle - Returns true if adding an edge from SU to TargetSU will
  /// create a cycle.
  bool WillCreateCycle(SUnit *SU, SUnit *TargetSU) {
    return Topo.WillCreateCycle(SU, TargetSU);
  }

  /// AddPredQueued - Queues and update to add a predecessor edge to SUnit SU.
  /// This returns true if this is a new predecessor.
  /// Does *NOT* update the topological ordering! It just queues an update.
  void AddPredQueued(SUnit *SU, const SDep &D) {
    Topo.AddPredQueued(SU, D.getSUnit());
    SU->addPred(D);
  }

  /// AddPred - adds a predecessor edge to SUnit SU.
  /// This returns true if this is a new predecessor.
  /// Updates the topological ordering if required.
  void AddPred(SUnit *SU, const SDep &D) {
    Topo.AddPred(SU, D.getSUnit());
    SU->addPred(D);
  }

  /// RemovePred - removes a predecessor edge from SUnit SU.
  /// This returns true if an edge was removed.
  /// Updates the topological ordering if required.
  void RemovePred(SUnit *SU, const SDep &D) {
    Topo.RemovePred(SU, D.getSUnit());
    SU->removePred(D);
  }

private:
  bool isReady(SUnit *SU) {
    return DisableSchedCycles || !AvailableQueue->hasReadyFilter() ||
      AvailableQueue->isReady(SU);
  }

  void ReleasePred(SUnit *SU, const SDep *PredEdge);
  void ReleasePredecessors(SUnit *SU);
  void ReleasePending();
  void AdvanceToCycle(unsigned NextCycle);
  void AdvancePastStalls(SUnit *SU);
  void EmitNode(SUnit *SU);
  void ScheduleNodeBottomUp(SUnit*);
  void CapturePred(SDep *PredEdge);
  void UnscheduleNodeBottomUp(SUnit*);
  void RestoreHazardCheckerBottomUp();
  void BacktrackBottomUp(SUnit*, SUnit*);
  SUnit *TryUnfoldSU(SUnit *);
  SUnit *CopyAndMoveSuccessors(SUnit*);
  void InsertCopiesAndMoveSuccs(SUnit*, unsigned,
                                const TargetRegisterClass*,
                                const TargetRegisterClass*,
                                SmallVectorImpl<SUnit*>&);
  bool DelayForLiveRegsBottomUp(SUnit*, SmallVectorImpl<unsigned>&);

  void releaseInterferences(unsigned Reg = 0);

  SUnit *PickNodeToScheduleBottomUp();
  void ListScheduleBottomUp();

  /// CreateNewSUnit - Creates a new SUnit and returns a pointer to it.
  SUnit *CreateNewSUnit(SDNode *N) {
    unsigned NumSUnits = SUnits.size();
    SUnit *NewNode = newSUnit(N);
    // Update the topological ordering.
    if (NewNode->NodeNum >= NumSUnits)
      Topo.AddSUnitWithoutPredecessors(NewNode);
    return NewNode;
  }

  /// CreateClone - Creates a new SUnit from an existing one.
  SUnit *CreateClone(SUnit *N) {
    unsigned NumSUnits = SUnits.size();
    SUnit *NewNode = Clone(N);
    // Update the topological ordering.
    if (NewNode->NodeNum >= NumSUnits)
      Topo.AddSUnitWithoutPredecessors(NewNode);
    return NewNode;
  }

  /// forceUnitLatencies - Register-pressure-reducing scheduling doesn't
  /// need actual latency information but the hybrid scheduler does.
  bool forceUnitLatencies() const override {
    return !NeedLatency;
  }
};

}  // end anonymous namespace

static constexpr unsigned RegSequenceCost = 1;

/// GetCostForDef - Looks up the register class and cost for a given definition.
/// Typically this just means looking up the representative register class,
/// but for untyped values (MVT::Untyped) it means inspecting the node's
/// opcode to determine what register class is being generated.
static void GetCostForDef(const ScheduleDAGSDNodes::RegDefIter &RegDefPos,
                          const TargetLowering *TLI,
                          const TargetInstrInfo *TII,
                          const TargetRegisterInfo *TRI,
                          unsigned &RegClass, unsigned &Cost,
                          const MachineFunction &MF) {
  MVT VT = RegDefPos.GetValue();

  // Special handling for untyped values.  These values can only come from
  // the expansion of custom DAG-to-DAG patterns.
  if (VT == MVT::Untyped) {
    const SDNode *Node = RegDefPos.GetNode();

    // Special handling for CopyFromReg of untyped values.
    if (!Node->isMachineOpcode() && Node->getOpcode() == ISD::CopyFromReg) {
      Register Reg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
      const TargetRegisterClass *RC = MF.getRegInfo().getRegClass(Reg);
      RegClass = RC->getID();
      Cost = 1;
      return;
    }

    unsigned Opcode = Node->getMachineOpcode();
    if (Opcode == TargetOpcode::REG_SEQUENCE) {
      unsigned DstRCIdx = Node->getConstantOperandVal(0);
      const TargetRegisterClass *RC = TRI->getRegClass(DstRCIdx);
      RegClass = RC->getID();
      Cost = RegSequenceCost;
      return;
    }

    unsigned Idx = RegDefPos.GetIdx();
    const MCInstrDesc &Desc = TII->get(Opcode);
    const TargetRegisterClass *RC = TII->getRegClass(Desc, Idx, TRI, MF);
    assert(RC && "Not a valid register class");
    RegClass = RC->getID();
    // FIXME: Cost arbitrarily set to 1 because there doesn't seem to be a
    // better way to determine it.
    Cost = 1;
  } else {
    RegClass = TLI->getRepRegClassFor(VT)->getID();
    Cost = TLI->getRepRegClassCostFor(VT);
  }
}

/// Schedule - Schedule the DAG using list scheduling.
void ScheduleDAGRRList::Schedule() {
  LLVM_DEBUG(dbgs() << "********** List Scheduling " << printMBBReference(*BB)
                    << " '" << BB->getName() << "' **********\n");

  CurCycle = 0;
  IssueCount = 0;
  MinAvailableCycle =
      DisableSchedCycles ? 0 : std::numeric_limits<unsigned>::max();
  NumLiveRegs = 0;
  // Allocate slots for each physical register, plus one for a special register
  // to track the virtual resource of a calling sequence.
  LiveRegDefs.reset(new SUnit*[TRI->getNumRegs() + 1]());
  LiveRegGens.reset(new SUnit*[TRI->getNumRegs() + 1]());
  CallSeqEndForStart.clear();
  assert(Interferences.empty() && LRegsMap.empty() && "stale Interferences");

  // Build the scheduling graph.
  BuildSchedGraph(nullptr);

  LLVM_DEBUG(dump());
  Topo.MarkDirty();

  AvailableQueue->initNodes(SUnits);

  HazardRec->Reset();

  // Execute the actual scheduling loop.
  ListScheduleBottomUp();

  AvailableQueue->releaseState();

  LLVM_DEBUG({
    dbgs() << "*** Final schedule ***\n";
    dumpSchedule();
    dbgs() << '\n';
  });
}

//===----------------------------------------------------------------------===//
//  Bottom-Up Scheduling
//===----------------------------------------------------------------------===//

/// ReleasePred - Decrement the NumSuccsLeft count of a predecessor. Add it to
/// the AvailableQueue if the count reaches zero. Also update its cycle bound.
void ScheduleDAGRRList::ReleasePred(SUnit *SU, const SDep *PredEdge) {
  SUnit *PredSU = PredEdge->getSUnit();

#ifndef NDEBUG
  if (PredSU->NumSuccsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    dumpNode(*PredSU);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(nullptr);
  }
#endif
  --PredSU->NumSuccsLeft;

  if (!forceUnitLatencies()) {
    // Updating predecessor's height. This is now the cycle when the
    // predecessor can be scheduled without causing a pipeline stall.
    PredSU->setHeightToAtLeast(SU->getHeight() + PredEdge->getLatency());
  }

  // If all the node's successors are scheduled, this node is ready
  // to be scheduled. Ignore the special EntrySU node.
  if (PredSU->NumSuccsLeft == 0 && PredSU != &EntrySU) {
    PredSU->isAvailable = true;

    unsigned Height = PredSU->getHeight();
    if (Height < MinAvailableCycle)
      MinAvailableCycle = Height;

    if (isReady(PredSU)) {
      AvailableQueue->push(PredSU);
    }
    // CapturePred and others may have left the node in the pending queue, avoid
    // adding it twice.
    else if (!PredSU->isPending) {
      PredSU->isPending = true;
      PendingQueue.push_back(PredSU);
    }
  }
}

/// IsChainDependent - Test if Outer is reachable from Inner through
/// chain dependencies.
static bool IsChainDependent(SDNode *Outer, SDNode *Inner,
                             unsigned NestLevel,
                             const TargetInstrInfo *TII) {
  SDNode *N = Outer;
  while (true) {
    if (N == Inner)
      return true;
    // For a TokenFactor, examine each operand. There may be multiple ways
    // to get to the CALLSEQ_BEGIN, but we need to find the path with the
    // most nesting in order to ensure that we find the corresponding match.
    if (N->getOpcode() == ISD::TokenFactor) {
      for (const SDValue &Op : N->op_values())
        if (IsChainDependent(Op.getNode(), Inner, NestLevel, TII))
          return true;
      return false;
    }
    // Check for a lowered CALLSEQ_BEGIN or CALLSEQ_END.
    if (N->isMachineOpcode()) {
      if (N->getMachineOpcode() == TII->getCallFrameDestroyOpcode()) {
        ++NestLevel;
      } else if (N->getMachineOpcode() == TII->getCallFrameSetupOpcode()) {
        if (NestLevel == 0)
          return false;
        --NestLevel;
      }
    }
    // Otherwise, find the chain and continue climbing.
    for (const SDValue &Op : N->op_values())
      if (Op.getValueType() == MVT::Other) {
        N = Op.getNode();
        goto found_chain_operand;
      }
    return false;
  found_chain_operand:;
    if (N->getOpcode() == ISD::EntryToken)
      return false;
  }
}

/// FindCallSeqStart - Starting from the (lowered) CALLSEQ_END node, locate
/// the corresponding (lowered) CALLSEQ_BEGIN node.
///
/// NestLevel and MaxNested are used in recursion to indcate the current level
/// of nesting of CALLSEQ_BEGIN and CALLSEQ_END pairs, as well as the maximum
/// level seen so far.
///
/// TODO: It would be better to give CALLSEQ_END an explicit operand to point
/// to the corresponding CALLSEQ_BEGIN to avoid needing to search for it.
static SDNode *
FindCallSeqStart(SDNode *N, unsigned &NestLevel, unsigned &MaxNest,
                 const TargetInstrInfo *TII) {
  while (true) {
    // For a TokenFactor, examine each operand. There may be multiple ways
    // to get to the CALLSEQ_BEGIN, but we need to find the path with the
    // most nesting in order to ensure that we find the corresponding match.
    if (N->getOpcode() == ISD::TokenFactor) {
      SDNode *Best = nullptr;
      unsigned BestMaxNest = MaxNest;
      for (const SDValue &Op : N->op_values()) {
        unsigned MyNestLevel = NestLevel;
        unsigned MyMaxNest = MaxNest;
        if (SDNode *New = FindCallSeqStart(Op.getNode(),
                                           MyNestLevel, MyMaxNest, TII))
          if (!Best || (MyMaxNest > BestMaxNest)) {
            Best = New;
            BestMaxNest = MyMaxNest;
          }
      }
      assert(Best);
      MaxNest = BestMaxNest;
      return Best;
    }
    // Check for a lowered CALLSEQ_BEGIN or CALLSEQ_END.
    if (N->isMachineOpcode()) {
      if (N->getMachineOpcode() == TII->getCallFrameDestroyOpcode()) {
        ++NestLevel;
        MaxNest = std::max(MaxNest, NestLevel);
      } else if (N->getMachineOpcode() == TII->getCallFrameSetupOpcode()) {
        assert(NestLevel != 0);
        --NestLevel;
        if (NestLevel == 0)
          return N;
      }
    }
    // Otherwise, find the chain and continue climbing.
    for (const SDValue &Op : N->op_values())
      if (Op.getValueType() == MVT::Other) {
        N = Op.getNode();
        goto found_chain_operand;
      }
    return nullptr;
  found_chain_operand:;
    if (N->getOpcode() == ISD::EntryToken)
      return nullptr;
  }
}

/// Call ReleasePred for each predecessor, then update register live def/gen.
/// Always update LiveRegDefs for a register dependence even if the current SU
/// also defines the register. This effectively create one large live range
/// across a sequence of two-address node. This is important because the
/// entire chain must be scheduled together. Example:
///
/// flags = (3) add
/// flags = (2) addc flags
/// flags = (1) addc flags
///
/// results in
///
/// LiveRegDefs[flags] = 3
/// LiveRegGens[flags] = 1
///
/// If (2) addc is unscheduled, then (1) addc must also be unscheduled to avoid
/// interference on flags.
void ScheduleDAGRRList::ReleasePredecessors(SUnit *SU) {
  // Bottom up: release predecessors
  for (SDep &Pred : SU->Preds) {
    ReleasePred(SU, &Pred);
    if (Pred.isAssignedRegDep()) {
      // This is a physical register dependency and it's impossible or
      // expensive to copy the register. Make sure nothing that can
      // clobber the register is scheduled between the predecessor and
      // this node.
      SUnit *RegDef = LiveRegDefs[Pred.getReg()]; (void)RegDef;
      assert((!RegDef || RegDef == SU || RegDef == Pred.getSUnit()) &&
             "interference on register dependence");
      LiveRegDefs[Pred.getReg()] = Pred.getSUnit();
      if (!LiveRegGens[Pred.getReg()]) {
        ++NumLiveRegs;
        LiveRegGens[Pred.getReg()] = SU;
      }
    }
  }

  // If we're scheduling a lowered CALLSEQ_END, find the corresponding
  // CALLSEQ_BEGIN. Inject an artificial physical register dependence between
  // these nodes, to prevent other calls from being interscheduled with them.
  unsigned CallResource = TRI->getNumRegs();
  if (!LiveRegDefs[CallResource])
    for (SDNode *Node = SU->getNode(); Node; Node = Node->getGluedNode())
      if (Node->isMachineOpcode() &&
          Node->getMachineOpcode() == TII->getCallFrameDestroyOpcode()) {
        unsigned NestLevel = 0;
        unsigned MaxNest = 0;
        SDNode *N = FindCallSeqStart(Node, NestLevel, MaxNest, TII);
        assert(N && "Must find call sequence start");

        SUnit *Def = &SUnits[N->getNodeId()];
        CallSeqEndForStart[Def] = SU;

        ++NumLiveRegs;
        LiveRegDefs[CallResource] = Def;
        LiveRegGens[CallResource] = SU;
        break;
      }
}

/// Check to see if any of the pending instructions are ready to issue.  If
/// so, add them to the available queue.
void ScheduleDAGRRList::ReleasePending() {
  if (DisableSchedCycles) {
    assert(PendingQueue.empty() && "pending instrs not allowed in this mode");
    return;
  }

  // If the available queue is empty, it is safe to reset MinAvailableCycle.
  if (AvailableQueue->empty())
    MinAvailableCycle = std::numeric_limits<unsigned>::max();

  // Check to see if any of the pending instructions are ready to issue.  If
  // so, add them to the available queue.
  for (unsigned i = 0, e = PendingQueue.size(); i != e; ++i) {
    unsigned ReadyCycle = PendingQueue[i]->getHeight();
    if (ReadyCycle < MinAvailableCycle)
      MinAvailableCycle = ReadyCycle;

    if (PendingQueue[i]->isAvailable) {
      if (!isReady(PendingQueue[i]))
          continue;
      AvailableQueue->push(PendingQueue[i]);
    }
    PendingQueue[i]->isPending = false;
    PendingQueue[i] = PendingQueue.back();
    PendingQueue.pop_back();
    --i; --e;
  }
}

/// Move the scheduler state forward by the specified number of Cycles.
void ScheduleDAGRRList::AdvanceToCycle(unsigned NextCycle) {
  if (NextCycle <= CurCycle)
    return;

  IssueCount = 0;
  AvailableQueue->setCurCycle(NextCycle);
  if (!HazardRec->isEnabled()) {
    // Bypass lots of virtual calls in case of long latency.
    CurCycle = NextCycle;
  }
  else {
    for (; CurCycle != NextCycle; ++CurCycle) {
      HazardRec->RecedeCycle();
    }
  }
  // FIXME: Instead of visiting the pending Q each time, set a dirty flag on the
  // available Q to release pending nodes at least once before popping.
  ReleasePending();
}

/// Move the scheduler state forward until the specified node's dependents are
/// ready and can be scheduled with no resource conflicts.
void ScheduleDAGRRList::AdvancePastStalls(SUnit *SU) {
  if (DisableSchedCycles)
    return;

  // FIXME: Nodes such as CopyFromReg probably should not advance the current
  // cycle. Otherwise, we can wrongly mask real stalls. If the non-machine node
  // has predecessors the cycle will be advanced when they are scheduled.
  // But given the crude nature of modeling latency though such nodes, we
  // currently need to treat these nodes like real instructions.
  // if (!SU->getNode() || !SU->getNode()->isMachineOpcode()) return;

  unsigned ReadyCycle = SU->getHeight();

  // Bump CurCycle to account for latency. We assume the latency of other
  // available instructions may be hidden by the stall (not a full pipe stall).
  // This updates the hazard recognizer's cycle before reserving resources for
  // this instruction.
  AdvanceToCycle(ReadyCycle);

  // Calls are scheduled in their preceding cycle, so don't conflict with
  // hazards from instructions after the call. EmitNode will reset the
  // scoreboard state before emitting the call.
  if (SU->isCall)
    return;

  // FIXME: For resource conflicts in very long non-pipelined stages, we
  // should probably skip ahead here to avoid useless scoreboard checks.
  int Stalls = 0;
  while (true) {
    ScheduleHazardRecognizer::HazardType HT =
      HazardRec->getHazardType(SU, -Stalls);

    if (HT == ScheduleHazardRecognizer::NoHazard)
      break;

    ++Stalls;
  }
  AdvanceToCycle(CurCycle + Stalls);
}

/// Record this SUnit in the HazardRecognizer.
/// Does not update CurCycle.
void ScheduleDAGRRList::EmitNode(SUnit *SU) {
  if (!HazardRec->isEnabled())
    return;

  // Check for phys reg copy.
  if (!SU->getNode())
    return;

  switch (SU->getNode()->getOpcode()) {
  default:
    assert(SU->getNode()->isMachineOpcode() &&
           "This target-independent node should not be scheduled.");
    break;
  case ISD::MERGE_VALUES:
  case ISD::TokenFactor:
  case ISD::LIFETIME_START:
  case ISD::LIFETIME_END:
  case ISD::CopyToReg:
  case ISD::CopyFromReg:
  case ISD::EH_LABEL:
    // Noops don't affect the scoreboard state. Copies are likely to be
    // removed.
    return;
  case ISD::INLINEASM:
  case ISD::INLINEASM_BR:
    // For inline asm, clear the pipeline state.
    HazardRec->Reset();
    return;
  }
  if (SU->isCall) {
    // Calls are scheduled with their preceding instructions. For bottom-up
    // scheduling, clear the pipeline state before emitting.
    HazardRec->Reset();
  }

  HazardRec->EmitInstruction(SU);
}

static void resetVRegCycle(SUnit *SU);

/// ScheduleNodeBottomUp - Add the node to the schedule. Decrement the pending
/// count of its predecessors. If a predecessor pending count is zero, add it to
/// the Available queue.
void ScheduleDAGRRList::ScheduleNodeBottomUp(SUnit *SU) {
  LLVM_DEBUG(dbgs() << "\n*** Scheduling [" << CurCycle << "]: ");
  LLVM_DEBUG(dumpNode(*SU));

#ifndef NDEBUG
  if (CurCycle < SU->getHeight())
    LLVM_DEBUG(dbgs() << "   Height [" << SU->getHeight()
                      << "] pipeline stall!\n");
#endif

  // FIXME: Do not modify node height. It may interfere with
  // backtracking. Instead add a "ready cycle" to SUnit. Before scheduling the
  // node its ready cycle can aid heuristics, and after scheduling it can
  // indicate the scheduled cycle.
  SU->setHeightToAtLeast(CurCycle);

  // Reserve resources for the scheduled instruction.
  EmitNode(SU);

  Sequence.push_back(SU);

  AvailableQueue->scheduledNode(SU);

  // If HazardRec is disabled, and each inst counts as one cycle, then
  // advance CurCycle before ReleasePredecessors to avoid useless pushes to
  // PendingQueue for schedulers that implement HasReadyFilter.
  if (!HazardRec->isEnabled() && AvgIPC < 2)
    AdvanceToCycle(CurCycle + 1);

  // Update liveness of predecessors before successors to avoid treating a
  // two-address node as a live range def.
  ReleasePredecessors(SU);

  // Release all the implicit physical register defs that are live.
  for (SDep &Succ : SU->Succs) {
    // LiveRegDegs[Succ.getReg()] != SU when SU is a two-address node.
    if (Succ.isAssignedRegDep() && LiveRegDefs[Succ.getReg()] == SU) {
      assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
      --NumLiveRegs;
      LiveRegDefs[Succ.getReg()] = nullptr;
      LiveRegGens[Succ.getReg()] = nullptr;
      releaseInterferences(Succ.getReg());
    }
  }
  // Release the special call resource dependence, if this is the beginning
  // of a call.
  unsigned CallResource = TRI->getNumRegs();
  if (LiveRegDefs[CallResource] == SU)
    for (const SDNode *SUNode = SU->getNode(); SUNode;
         SUNode = SUNode->getGluedNode()) {
      if (SUNode->isMachineOpcode() &&
          SUNode->getMachineOpcode() == TII->getCallFrameSetupOpcode()) {
        assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
        --NumLiveRegs;
        LiveRegDefs[CallResource] = nullptr;
        LiveRegGens[CallResource] = nullptr;
        releaseInterferences(CallResource);
      }
    }

  resetVRegCycle(SU);

  SU->isScheduled = true;

  // Conditions under which the scheduler should eagerly advance the cycle:
  // (1) No available instructions
  // (2) All pipelines full, so available instructions must have hazards.
  //
  // If HazardRec is disabled, the cycle was pre-advanced before calling
  // ReleasePredecessors. In that case, IssueCount should remain 0.
  //
  // Check AvailableQueue after ReleasePredecessors in case of zero latency.
  if (HazardRec->isEnabled() || AvgIPC > 1) {
    if (SU->getNode() && SU->getNode()->isMachineOpcode())
      ++IssueCount;
    if ((HazardRec->isEnabled() && HazardRec->atIssueLimit())
        || (!HazardRec->isEnabled() && IssueCount == AvgIPC))
      AdvanceToCycle(CurCycle + 1);
  }
}

/// CapturePred - This does the opposite of ReleasePred. Since SU is being
/// unscheduled, increase the succ left count of its predecessors. Remove
/// them from AvailableQueue if necessary.
void ScheduleDAGRRList::CapturePred(SDep *PredEdge) {
  SUnit *PredSU = PredEdge->getSUnit();
  if (PredSU->isAvailable) {
    PredSU->isAvailable = false;
    if (!PredSU->isPending)
      AvailableQueue->remove(PredSU);
  }

  assert(PredSU->NumSuccsLeft < std::numeric_limits<unsigned>::max() &&
         "NumSuccsLeft will overflow!");
  ++PredSU->NumSuccsLeft;
}

/// UnscheduleNodeBottomUp - Remove the node from the schedule, update its and
/// its predecessor states to reflect the change.
void ScheduleDAGRRList::UnscheduleNodeBottomUp(SUnit *SU) {
  LLVM_DEBUG(dbgs() << "*** Unscheduling [" << SU->getHeight() << "]: ");
  LLVM_DEBUG(dumpNode(*SU));

  for (SDep &Pred : SU->Preds) {
    CapturePred(&Pred);
    if (Pred.isAssignedRegDep() && SU == LiveRegGens[Pred.getReg()]){
      assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
      assert(LiveRegDefs[Pred.getReg()] == Pred.getSUnit() &&
             "Physical register dependency violated?");
      --NumLiveRegs;
      LiveRegDefs[Pred.getReg()] = nullptr;
      LiveRegGens[Pred.getReg()] = nullptr;
      releaseInterferences(Pred.getReg());
    }
  }

  // Reclaim the special call resource dependence, if this is the beginning
  // of a call.
  unsigned CallResource = TRI->getNumRegs();
  for (const SDNode *SUNode = SU->getNode(); SUNode;
       SUNode = SUNode->getGluedNode()) {
    if (SUNode->isMachineOpcode() &&
        SUNode->getMachineOpcode() == TII->getCallFrameSetupOpcode()) {
      SUnit *SeqEnd = CallSeqEndForStart[SU];
      assert(SeqEnd && "Call sequence start/end must be known");
      assert(!LiveRegDefs[CallResource]);
      assert(!LiveRegGens[CallResource]);
      ++NumLiveRegs;
      LiveRegDefs[CallResource] = SU;
      LiveRegGens[CallResource] = SeqEnd;
    }
  }

  // Release the special call resource dependence, if this is the end
  // of a call.
  if (LiveRegGens[CallResource] == SU)
    for (const SDNode *SUNode = SU->getNode(); SUNode;
         SUNode = SUNode->getGluedNode()) {
      if (SUNode->isMachineOpcode() &&
          SUNode->getMachineOpcode() == TII->getCallFrameDestroyOpcode()) {
        assert(NumLiveRegs > 0 && "NumLiveRegs is already zero!");
        assert(LiveRegDefs[CallResource]);
        assert(LiveRegGens[CallResource]);
        --NumLiveRegs;
        LiveRegDefs[CallResource] = nullptr;
        LiveRegGens[CallResource] = nullptr;
        releaseInterferences(CallResource);
      }
    }

  for (auto &Succ : SU->Succs) {
    if (Succ.isAssignedRegDep()) {
      auto Reg = Succ.getReg();
      if (!LiveRegDefs[Reg])
        ++NumLiveRegs;
      // This becomes the nearest def. Note that an earlier def may still be
      // pending if this is a two-address node.
      LiveRegDefs[Reg] = SU;

      // Update LiveRegGen only if was empty before this unscheduling.
      // This is to avoid incorrect updating LiveRegGen set in previous run.
      if (!LiveRegGens[Reg]) {
        // Find the successor with the lowest height.
        LiveRegGens[Reg] = Succ.getSUnit();
        for (auto &Succ2 : SU->Succs) {
          if (Succ2.isAssignedRegDep() && Succ2.getReg() == Reg &&
              Succ2.getSUnit()->getHeight() < LiveRegGens[Reg]->getHeight())
            LiveRegGens[Reg] = Succ2.getSUnit();
        }
      }
    }
  }
  if (SU->getHeight() < MinAvailableCycle)
    MinAvailableCycle = SU->getHeight();

  SU->setHeightDirty();
  SU->isScheduled = false;
  SU->isAvailable = true;
  if (!DisableSchedCycles && AvailableQueue->hasReadyFilter()) {
    // Don't make available until backtracking is complete.
    SU->isPending = true;
    PendingQueue.push_back(SU);
  }
  else {
    AvailableQueue->push(SU);
  }
  AvailableQueue->unscheduledNode(SU);
}

/// After backtracking, the hazard checker needs to be restored to a state
/// corresponding the current cycle.
void ScheduleDAGRRList::RestoreHazardCheckerBottomUp() {
  HazardRec->Reset();

  unsigned LookAhead = std::min((unsigned)Sequence.size(),
                                HazardRec->getMaxLookAhead());
  if (LookAhead == 0)
    return;

  std::vector<SUnit *>::const_iterator I = (Sequence.end() - LookAhead);
  unsigned HazardCycle = (*I)->getHeight();
  for (auto E = Sequence.end(); I != E; ++I) {
    SUnit *SU = *I;
    for (; SU->getHeight() > HazardCycle; ++HazardCycle) {
      HazardRec->RecedeCycle();
    }
    EmitNode(SU);
  }
}

/// BacktrackBottomUp - Backtrack scheduling to a previous cycle specified in
/// BTCycle in order to schedule a specific node.
void ScheduleDAGRRList::BacktrackBottomUp(SUnit *SU, SUnit *BtSU) {
  SUnit *OldSU = Sequence.back();
  while (true) {
    Sequence.pop_back();
    // FIXME: use ready cycle instead of height
    CurCycle = OldSU->getHeight();
    UnscheduleNodeBottomUp(OldSU);
    AvailableQueue->setCurCycle(CurCycle);
    if (OldSU == BtSU)
      break;
    OldSU = Sequence.back();
  }

  assert(!SU->isSucc(OldSU) && "Something is wrong!");

  RestoreHazardCheckerBottomUp();

  ReleasePending();

  ++NumBacktracks;
}

static bool isOperandOf(const SUnit *SU, SDNode *N) {
  for (const SDNode *SUNode = SU->getNode(); SUNode;
       SUNode = SUNode->getGluedNode()) {
    if (SUNode->isOperandOf(N))
      return true;
  }
  return false;
}

/// TryUnfold - Attempt to unfold
SUnit *ScheduleDAGRRList::TryUnfoldSU(SUnit *SU) {
  SDNode *N = SU->getNode();
  // Use while over if to ease fall through.
  SmallVector<SDNode *, 2> NewNodes;
  if (!TII->unfoldMemoryOperand(*DAG, N, NewNodes))
    return nullptr;

  assert(NewNodes.size() == 2 && "Expected a load folding node!");

  N = NewNodes[1];
  SDNode *LoadNode = NewNodes[0];
  unsigned NumVals = N->getNumValues();
  unsigned OldNumVals = SU->getNode()->getNumValues();

  // LoadNode may already exist. This can happen when there is another
  // load from the same location and producing the same type of value
  // but it has different alignment or volatileness.
  bool isNewLoad = true;
  SUnit *LoadSU;
  if (LoadNode->getNodeId() != -1) {
    LoadSU = &SUnits[LoadNode->getNodeId()];
    // If LoadSU has already been scheduled, we should clone it but
    // this would negate the benefit to unfolding so just return SU.
    if (LoadSU->isScheduled)
      return SU;
    isNewLoad = false;
  } else {
    LoadSU = CreateNewSUnit(LoadNode);
    LoadNode->setNodeId(LoadSU->NodeNum);

    InitNumRegDefsLeft(LoadSU);
    computeLatency(LoadSU);
  }

  bool isNewN = true;
  SUnit *NewSU;
  // This can only happen when isNewLoad is false.
  if (N->getNodeId() != -1) {
    NewSU = &SUnits[N->getNodeId()];
    // If NewSU has already been scheduled, we need to clone it, but this
    // negates the benefit to unfolding so just return SU.
    if (NewSU->isScheduled) {
      return SU;
    }
    isNewN = false;
  } else {
    NewSU = CreateNewSUnit(N);
    N->setNodeId(NewSU->NodeNum);

    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    for (unsigned i = 0; i != MCID.getNumOperands(); ++i) {
      if (MCID.getOperandConstraint(i, MCOI::TIED_TO) != -1) {
        NewSU->isTwoAddress = true;
        break;
      }
    }
    if (MCID.isCommutable())
      NewSU->isCommutable = true;

    InitNumRegDefsLeft(NewSU);
    computeLatency(NewSU);
  }

  LLVM_DEBUG(dbgs() << "Unfolding SU #" << SU->NodeNum << "\n");

  // Now that we are committed to unfolding replace DAG Uses.
  for (unsigned i = 0; i != NumVals; ++i)
    DAG->ReplaceAllUsesOfValueWith(SDValue(SU->getNode(), i), SDValue(N, i));
  DAG->ReplaceAllUsesOfValueWith(SDValue(SU->getNode(), OldNumVals - 1),
                                 SDValue(LoadNode, 1));

  // Record all the edges to and from the old SU, by category.
  SmallVector<SDep, 4> ChainPreds;
  SmallVector<SDep, 4> ChainSuccs;
  SmallVector<SDep, 4> LoadPreds;
  SmallVector<SDep, 4> NodePreds;
  SmallVector<SDep, 4> NodeSuccs;
  for (SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      ChainPreds.push_back(Pred);
    else if (isOperandOf(Pred.getSUnit(), LoadNode))
      LoadPreds.push_back(Pred);
    else
      NodePreds.push_back(Pred);
  }
  for (SDep &Succ : SU->Succs) {
    if (Succ.isCtrl())
      ChainSuccs.push_back(Succ);
    else
      NodeSuccs.push_back(Succ);
  }

  // Now assign edges to the newly-created nodes.
  for (const SDep &Pred : ChainPreds) {
    RemovePred(SU, Pred);
    if (isNewLoad)
      AddPredQueued(LoadSU, Pred);
  }
  for (const SDep &Pred : LoadPreds) {
    RemovePred(SU, Pred);
    if (isNewLoad)
      AddPredQueued(LoadSU, Pred);
  }
  for (const SDep &Pred : NodePreds) {
    RemovePred(SU, Pred);
    AddPredQueued(NewSU, Pred);
  }
  for (SDep &D : NodeSuccs) {
    SUnit *SuccDep = D.getSUnit();
    D.setSUnit(SU);
    RemovePred(SuccDep, D);
    D.setSUnit(NewSU);
    AddPredQueued(SuccDep, D);
    // Balance register pressure.
    if (AvailableQueue->tracksRegPressure() && SuccDep->isScheduled &&
        !D.isCtrl() && NewSU->NumRegDefsLeft > 0)
      --NewSU->NumRegDefsLeft;
  }
  for (SDep &D : ChainSuccs) {
    SUnit *SuccDep = D.getSUnit();
    D.setSUnit(SU);
    RemovePred(SuccDep, D);
    if (isNewLoad) {
      D.setSUnit(LoadSU);
      AddPredQueued(SuccDep, D);
    }
  }

  // Add a data dependency to reflect that NewSU reads the value defined
  // by LoadSU.
  SDep D(LoadSU, SDep::Data, 0);
  D.setLatency(LoadSU->Latency);
  AddPredQueued(NewSU, D);

  if (isNewLoad)
    AvailableQueue->addNode(LoadSU);
  if (isNewN)
    AvailableQueue->addNode(NewSU);

  ++NumUnfolds;

  if (NewSU->NumSuccsLeft == 0)
    NewSU->isAvailable = true;

  return NewSU;
}

/// CopyAndMoveSuccessors - Clone the specified node and move its scheduled
/// successors to the newly created node.
SUnit *ScheduleDAGRRList::CopyAndMoveSuccessors(SUnit *SU) {
  SDNode *N = SU->getNode();
  if (!N)
    return nullptr;

  LLVM_DEBUG(dbgs() << "Considering duplicating the SU\n");
  LLVM_DEBUG(dumpNode(*SU));

  if (N->getGluedNode() &&
      !TII->canCopyGluedNodeDuringSchedule(N)) {
    LLVM_DEBUG(
        dbgs()
        << "Giving up because it has incoming glue and the target does not "
           "want to copy it\n");
    return nullptr;
  }

  SUnit *NewSU;
  bool TryUnfold = false;
  for (unsigned i = 0, e = N->getNumValues(); i != e; ++i) {
    MVT VT = N->getSimpleValueType(i);
    if (VT == MVT::Glue) {
      LLVM_DEBUG(dbgs() << "Giving up because it has outgoing glue\n");
      return nullptr;
    } else if (VT == MVT::Other)
      TryUnfold = true;
  }
  for (const SDValue &Op : N->op_values()) {
    MVT VT = Op.getNode()->getSimpleValueType(Op.getResNo());
    if (VT == MVT::Glue && !TII->canCopyGluedNodeDuringSchedule(N)) {
      LLVM_DEBUG(
          dbgs() << "Giving up because it one of the operands is glue and "
                    "the target does not want to copy it\n");
      return nullptr;
    }
  }

  // If possible unfold instruction.
  if (TryUnfold) {
    SUnit *UnfoldSU = TryUnfoldSU(SU);
    if (!UnfoldSU)
      return nullptr;
    SU = UnfoldSU;
    N = SU->getNode();
    // If this can be scheduled don't bother duplicating and just return
    if (SU->NumSuccsLeft == 0)
      return SU;
  }

  LLVM_DEBUG(dbgs() << "    Duplicating SU #" << SU->NodeNum << "\n");
  NewSU = CreateClone(SU);

  // New SUnit has the exact same predecessors.
  for (SDep &Pred : SU->Preds)
    if (!Pred.isArtificial())
      AddPredQueued(NewSU, Pred);

  // Make sure the clone comes after the original. (InstrEmitter assumes
  // this ordering.)
  AddPredQueued(NewSU, SDep(SU, SDep::Artificial));

  // Only copy scheduled successors. Cut them from old node's successor
  // list and move them over.
  SmallVector<std::pair<SUnit *, SDep>, 4> DelDeps;
  for (SDep &Succ : SU->Succs) {
    if (Succ.isArtificial())
      continue;
    SUnit *SuccSU = Succ.getSUnit();
    if (SuccSU->isScheduled) {
      SDep D = Succ;
      D.setSUnit(NewSU);
      AddPredQueued(SuccSU, D);
      D.setSUnit(SU);
      DelDeps.emplace_back(SuccSU, D);
    }
  }
  for (const auto &[DelSU, DelD] : DelDeps)
    RemovePred(DelSU, DelD);

  AvailableQueue->updateNode(SU);
  AvailableQueue->addNode(NewSU);

  ++NumDups;
  return NewSU;
}

/// InsertCopiesAndMoveSuccs - Insert register copies and move all
/// scheduled successors of the given SUnit to the last copy.
void ScheduleDAGRRList::InsertCopiesAndMoveSuccs(SUnit *SU, unsigned Reg,
                                              const TargetRegisterClass *DestRC,
                                              const TargetRegisterClass *SrcRC,
                                              SmallVectorImpl<SUnit*> &Copies) {
  SUnit *CopyFromSU = CreateNewSUnit(nullptr);
  CopyFromSU->CopySrcRC = SrcRC;
  CopyFromSU->CopyDstRC = DestRC;

  SUnit *CopyToSU = CreateNewSUnit(nullptr);
  CopyToSU->CopySrcRC = DestRC;
  CopyToSU->CopyDstRC = SrcRC;

  // Only copy scheduled successors. Cut them from old node's successor
  // list and move them over.
  SmallVector<std::pair<SUnit *, SDep>, 4> DelDeps;
  for (SDep &Succ : SU->Succs) {
    if (Succ.isArtificial())
      continue;
    SUnit *SuccSU = Succ.getSUnit();
    if (SuccSU->isScheduled) {
      SDep D = Succ;
      D.setSUnit(CopyToSU);
      AddPredQueued(SuccSU, D);
      DelDeps.emplace_back(SuccSU, Succ);
    }
    else {
      // Avoid scheduling the def-side copy before other successors. Otherwise,
      // we could introduce another physreg interference on the copy and
      // continue inserting copies indefinitely.
      AddPredQueued(SuccSU, SDep(CopyFromSU, SDep::Artificial));
    }
  }
  for (const auto &[DelSU, DelD] : DelDeps)
    RemovePred(DelSU, DelD);

  SDep FromDep(SU, SDep::Data, Reg);
  FromDep.setLatency(SU->Latency);
  AddPredQueued(CopyFromSU, FromDep);
  SDep ToDep(CopyFromSU, SDep::Data, 0);
  ToDep.setLatency(CopyFromSU->Latency);
  AddPredQueued(CopyToSU, ToDep);

  AvailableQueue->updateNode(SU);
  AvailableQueue->addNode(CopyFromSU);
  AvailableQueue->addNode(CopyToSU);
  Copies.push_back(CopyFromSU);
  Copies.push_back(CopyToSU);

  ++NumPRCopies;
}

/// getPhysicalRegisterVT - Returns the ValueType of the physical register
/// definition of the specified node.
/// FIXME: Move to SelectionDAG?
static MVT getPhysicalRegisterVT(SDNode *N, unsigned Reg,
                                 const TargetInstrInfo *TII) {
  unsigned NumRes;
  if (N->getOpcode() == ISD::CopyFromReg) {
    // CopyFromReg has: "chain, Val, glue" so operand 1 gives the type.
    NumRes = 1;
  } else {
    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    assert(!MCID.implicit_defs().empty() &&
           "Physical reg def must be in implicit def list!");
    NumRes = MCID.getNumDefs();
    for (MCPhysReg ImpDef : MCID.implicit_defs()) {
      if (Reg == ImpDef)
        break;
      ++NumRes;
    }
  }
  return N->getSimpleValueType(NumRes);
}

/// CheckForLiveRegDef - Return true and update live register vector if the
/// specified register def of the specified SUnit clobbers any "live" registers.
static void CheckForLiveRegDef(SUnit *SU, unsigned Reg, SUnit **LiveRegDefs,
                               SmallSet<unsigned, 4> &RegAdded,
                               SmallVectorImpl<unsigned> &LRegs,
                               const TargetRegisterInfo *TRI,
                               const SDNode *Node = nullptr) {
  for (MCRegAliasIterator AliasI(Reg, TRI, true); AliasI.isValid(); ++AliasI) {

    // Check if Ref is live.
    if (!LiveRegDefs[*AliasI]) continue;

    // Allow multiple uses of the same def.
    if (LiveRegDefs[*AliasI] == SU) continue;

    // Allow multiple uses of same def
    if (Node && LiveRegDefs[*AliasI]->getNode() == Node)
      continue;

    // Add Reg to the set of interfering live regs.
    if (RegAdded.insert(*AliasI).second) {
      LRegs.push_back(*AliasI);
    }
  }
}

/// CheckForLiveRegDefMasked - Check for any live physregs that are clobbered
/// by RegMask, and add them to LRegs.
static void CheckForLiveRegDefMasked(SUnit *SU, const uint32_t *RegMask,
                                     ArrayRef<SUnit*> LiveRegDefs,
                                     SmallSet<unsigned, 4> &RegAdded,
                                     SmallVectorImpl<unsigned> &LRegs) {
  // Look at all live registers. Skip Reg0 and the special CallResource.
  for (unsigned i = 1, e = LiveRegDefs.size()-1; i != e; ++i) {
    if (!LiveRegDefs[i]) continue;
    if (LiveRegDefs[i] == SU) continue;
    if (!MachineOperand::clobbersPhysReg(RegMask, i)) continue;
    if (RegAdded.insert(i).second)
      LRegs.push_back(i);
  }
}

/// getNodeRegMask - Returns the register mask attached to an SDNode, if any.
static const uint32_t *getNodeRegMask(const SDNode *N) {
  for (const SDValue &Op : N->op_values())
    if (const auto *RegOp = dyn_cast<RegisterMaskSDNode>(Op.getNode()))
      return RegOp->getRegMask();
  return nullptr;
}

/// DelayForLiveRegsBottomUp - Returns true if it is necessary to delay
/// scheduling of the given node to satisfy live physical register dependencies.
/// If the specific node is the last one that's available to schedule, do
/// whatever is necessary (i.e. backtracking or cloning) to make it possible.
bool ScheduleDAGRRList::
DelayForLiveRegsBottomUp(SUnit *SU, SmallVectorImpl<unsigned> &LRegs) {
  if (NumLiveRegs == 0)
    return false;

  SmallSet<unsigned, 4> RegAdded;
  // If this node would clobber any "live" register, then it's not ready.
  //
  // If SU is the currently live definition of the same register that it uses,
  // then we are free to schedule it.
  for (SDep &Pred : SU->Preds) {
    if (Pred.isAssignedRegDep() && LiveRegDefs[Pred.getReg()] != SU)
      CheckForLiveRegDef(Pred.getSUnit(), Pred.getReg(), LiveRegDefs.get(),
                         RegAdded, LRegs, TRI);
  }

  for (SDNode *Node = SU->getNode(); Node; Node = Node->getGluedNode()) {
    if (Node->getOpcode() == ISD::INLINEASM ||
        Node->getOpcode() == ISD::INLINEASM_BR) {
      // Inline asm can clobber physical defs.
      unsigned NumOps = Node->getNumOperands();
      if (Node->getOperand(NumOps-1).getValueType() == MVT::Glue)
        --NumOps;  // Ignore the glue operand.

      for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
        unsigned Flags = Node->getConstantOperandVal(i);
        const InlineAsm::Flag F(Flags);
        unsigned NumVals = F.getNumOperandRegisters();

        ++i; // Skip the ID value.
        if (F.isRegDefKind() || F.isRegDefEarlyClobberKind() ||
            F.isClobberKind()) {
          // Check for def of register or earlyclobber register.
          for (; NumVals; --NumVals, ++i) {
            Register Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
            if (Reg.isPhysical())
              CheckForLiveRegDef(SU, Reg, LiveRegDefs.get(), RegAdded, LRegs, TRI);
          }
        } else
          i += NumVals;
      }
      continue;
    }

    if (Node->getOpcode() == ISD::CopyToReg) {
      Register Reg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
      if (Reg.isPhysical()) {
        SDNode *SrcNode = Node->getOperand(2).getNode();
        CheckForLiveRegDef(SU, Reg, LiveRegDefs.get(), RegAdded, LRegs, TRI,
                           SrcNode);
      }
    }

    if (!Node->isMachineOpcode())
      continue;
    // If we're in the middle of scheduling a call, don't begin scheduling
    // another call. Also, don't allow any physical registers to be live across
    // the call.
    if (Node->getMachineOpcode() == TII->getCallFrameDestroyOpcode()) {
      // Check the special calling-sequence resource.
      unsigned CallResource = TRI->getNumRegs();
      if (LiveRegDefs[CallResource]) {
        SDNode *Gen = LiveRegGens[CallResource]->getNode();
        while (SDNode *Glued = Gen->getGluedNode())
          Gen = Glued;
        if (!IsChainDependent(Gen, Node, 0, TII) &&
            RegAdded.insert(CallResource).second)
          LRegs.push_back(CallResource);
      }
    }
    if (const uint32_t *RegMask = getNodeRegMask(Node))
      CheckForLiveRegDefMasked(SU, RegMask,
                               ArrayRef(LiveRegDefs.get(), TRI->getNumRegs()),
                               RegAdded, LRegs);

    const MCInstrDesc &MCID = TII->get(Node->getMachineOpcode());
    if (MCID.hasOptionalDef()) {
      // Most ARM instructions have an OptionalDef for CPSR, to model the S-bit.
      // This operand can be either a def of CPSR, if the S bit is set; or a use
      // of %noreg.  When the OptionalDef is set to a valid register, we need to
      // handle it in the same way as an ImplicitDef.
      for (unsigned i = 0; i < MCID.getNumDefs(); ++i)
        if (MCID.operands()[i].isOptionalDef()) {
          const SDValue &OptionalDef = Node->getOperand(i - Node->getNumValues());
          Register Reg = cast<RegisterSDNode>(OptionalDef)->getReg();
          CheckForLiveRegDef(SU, Reg, LiveRegDefs.get(), RegAdded, LRegs, TRI);
        }
    }
    for (MCPhysReg Reg : MCID.implicit_defs())
      CheckForLiveRegDef(SU, Reg, LiveRegDefs.get(), RegAdded, LRegs, TRI);
  }

  return !LRegs.empty();
}

void ScheduleDAGRRList::releaseInterferences(unsigned Reg) {
  // Add the nodes that aren't ready back onto the available list.
  for (unsigned i = Interferences.size(); i > 0; --i) {
    SUnit *SU = Interferences[i-1];
    LRegsMapT::iterator LRegsPos = LRegsMap.find(SU);
    if (Reg) {
      SmallVectorImpl<unsigned> &LRegs = LRegsPos->second;
      if (!is_contained(LRegs, Reg))
        continue;
    }
    SU->isPending = false;
    // The interfering node may no longer be available due to backtracking.
    // Furthermore, it may have been made available again, in which case it is
    // now already in the AvailableQueue.
    if (SU->isAvailable && !SU->NodeQueueId) {
      LLVM_DEBUG(dbgs() << "    Repushing SU #" << SU->NodeNum << '\n');
      AvailableQueue->push(SU);
    }
    if (i < Interferences.size())
      Interferences[i-1] = Interferences.back();
    Interferences.pop_back();
    LRegsMap.erase(LRegsPos);
  }
}

/// Return a node that can be scheduled in this cycle. Requirements:
/// (1) Ready: latency has been satisfied
/// (2) No Hazards: resources are available
/// (3) No Interferences: may unschedule to break register interferences.
SUnit *ScheduleDAGRRList::PickNodeToScheduleBottomUp() {
  SUnit *CurSU = AvailableQueue->empty() ? nullptr : AvailableQueue->pop();
  auto FindAvailableNode = [&]() {
    while (CurSU) {
      SmallVector<unsigned, 4> LRegs;
      if (!DelayForLiveRegsBottomUp(CurSU, LRegs))
        break;
      LLVM_DEBUG(dbgs() << "    Interfering reg ";
                 if (LRegs[0] == TRI->getNumRegs()) dbgs() << "CallResource";
                 else dbgs() << printReg(LRegs[0], TRI);
                 dbgs() << " SU #" << CurSU->NodeNum << '\n');
      auto [LRegsIter, LRegsInserted] = LRegsMap.try_emplace(CurSU, LRegs);
      if (LRegsInserted) {
        CurSU->isPending = true;  // This SU is not in AvailableQueue right now.
        Interferences.push_back(CurSU);
      }
      else {
        assert(CurSU->isPending && "Interferences are pending");
        // Update the interference with current live regs.
        LRegsIter->second = LRegs;
      }
      CurSU = AvailableQueue->pop();
    }
  };
  FindAvailableNode();
  if (CurSU)
    return CurSU;

  // We query the topological order in the loop body, so make sure outstanding
  // updates are applied before entering it (we only enter the loop if there
  // are some interferences). If we make changes to the ordering, we exit
  // the loop.

  // All candidates are delayed due to live physical reg dependencies.
  // Try backtracking, code duplication, or inserting cross class copies
  // to resolve it.
  for (SUnit *TrySU : Interferences) {
    SmallVectorImpl<unsigned> &LRegs = LRegsMap[TrySU];

    // Try unscheduling up to the point where it's safe to schedule
    // this node.
    SUnit *BtSU = nullptr;
    unsigned LiveCycle = std::numeric_limits<unsigned>::max();
    for (unsigned Reg : LRegs) {
      if (LiveRegGens[Reg]->getHeight() < LiveCycle) {
        BtSU = LiveRegGens[Reg];
        LiveCycle = BtSU->getHeight();
      }
    }
    if (!WillCreateCycle(TrySU, BtSU))  {
      // BacktrackBottomUp mutates Interferences!
      BacktrackBottomUp(TrySU, BtSU);

      // Force the current node to be scheduled before the node that
      // requires the physical reg dep.
      if (BtSU->isAvailable) {
        BtSU->isAvailable = false;
        if (!BtSU->isPending)
          AvailableQueue->remove(BtSU);
      }
      LLVM_DEBUG(dbgs() << "ARTIFICIAL edge from SU(" << BtSU->NodeNum
                        << ") to SU(" << TrySU->NodeNum << ")\n");
      AddPredQueued(TrySU, SDep(BtSU, SDep::Artificial));

      // If one or more successors has been unscheduled, then the current
      // node is no longer available.
      if (!TrySU->isAvailable || !TrySU->NodeQueueId) {
        LLVM_DEBUG(dbgs() << "TrySU not available; choosing node from queue\n");
        CurSU = AvailableQueue->pop();
      } else {
        LLVM_DEBUG(dbgs() << "TrySU available\n");
        // Available and in AvailableQueue
        AvailableQueue->remove(TrySU);
        CurSU = TrySU;
      }
      FindAvailableNode();
      // Interferences has been mutated. We must break.
      break;
    }
  }

  if (!CurSU) {
    // Can't backtrack. If it's too expensive to copy the value, then try
    // duplicate the nodes that produces these "too expensive to copy"
    // values to break the dependency. In case even that doesn't work,
    // insert cross class copies.
    // If it's not too expensive, i.e. cost != -1, issue copies.
    SUnit *TrySU = Interferences[0];
    SmallVectorImpl<unsigned> &LRegs = LRegsMap[TrySU];
    assert(LRegs.size() == 1 && "Can't handle this yet!");
    unsigned Reg = LRegs[0];
    SUnit *LRDef = LiveRegDefs[Reg];
    MVT VT = getPhysicalRegisterVT(LRDef->getNode(), Reg, TII);
    const TargetRegisterClass *RC =
      TRI->getMinimalPhysRegClass(Reg, VT);
    const TargetRegisterClass *DestRC = TRI->getCrossCopyRegClass(RC);

    // If cross copy register class is the same as RC, then it must be possible
    // copy the value directly. Do not try duplicate the def.
    // If cross copy register class is not the same as RC, then it's possible to
    // copy the value but it require cross register class copies and it is
    // expensive.
    // If cross copy register class is null, then it's not possible to copy
    // the value at all.
    SUnit *NewDef = nullptr;
    if (DestRC != RC) {
      NewDef = CopyAndMoveSuccessors(LRDef);
      if (!DestRC && !NewDef)
        report_fatal_error("Can't handle live physical register dependency!");
    }
    if (!NewDef) {
      // Issue copies, these can be expensive cross register class copies.
      SmallVector<SUnit*, 2> Copies;
      InsertCopiesAndMoveSuccs(LRDef, Reg, DestRC, RC, Copies);
      LLVM_DEBUG(dbgs() << "    Adding an edge from SU #" << TrySU->NodeNum
                        << " to SU #" << Copies.front()->NodeNum << "\n");
      AddPredQueued(TrySU, SDep(Copies.front(), SDep::Artificial));
      NewDef = Copies.back();
    }

    LLVM_DEBUG(dbgs() << "    Adding an edge from SU #" << NewDef->NodeNum
                      << " to SU #" << TrySU->NodeNum << "\n");
    LiveRegDefs[Reg] = NewDef;
    AddPredQueued(NewDef, SDep(TrySU, SDep::Artificial));
    TrySU->isAvailable = false;
    CurSU = NewDef;
  }
  assert(CurSU && "Unable to resolve live physical register dependencies!");
  return CurSU;
}

/// ListScheduleBottomUp - The main loop of list scheduling for bottom-up
/// schedulers.
void ScheduleDAGRRList::ListScheduleBottomUp() {
  // Release any predecessors of the special Exit node.
  ReleasePredecessors(&ExitSU);

  // Add root to Available queue.
  if (!SUnits.empty()) {
    SUnit *RootSU = &SUnits[DAG->getRoot().getNode()->getNodeId()];
    assert(RootSU->Succs.empty() && "Graph root shouldn't have successors!");
    RootSU->isAvailable = true;
    AvailableQueue->push(RootSU);
  }

  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  Sequence.reserve(SUnits.size());
  while (!AvailableQueue->empty() || !Interferences.empty()) {
    LLVM_DEBUG(dbgs() << "\nExamining Available:\n";
               AvailableQueue->dump(this));

    // Pick the best node to schedule taking all constraints into
    // consideration.
    SUnit *SU = PickNodeToScheduleBottomUp();

    AdvancePastStalls(SU);

    ScheduleNodeBottomUp(SU);

    while (AvailableQueue->empty() && !PendingQueue.empty()) {
      // Advance the cycle to free resources. Skip ahead to the next ready SU.
      assert(MinAvailableCycle < std::numeric_limits<unsigned>::max() &&
             "MinAvailableCycle uninitialized");
      AdvanceToCycle(std::max(CurCycle + 1, MinAvailableCycle));
    }
  }

  // Reverse the order if it is bottom up.
  std::reverse(Sequence.begin(), Sequence.end());

#ifndef NDEBUG
  VerifyScheduledSequence(/*isBottomUp=*/true);
#endif
}

namespace {

class RegReductionPQBase;

struct queue_sort {
  bool isReady(SUnit* SU, unsigned CurCycle) const { return true; }
};

#ifndef NDEBUG
template<class SF>
struct reverse_sort : public queue_sort {
  SF &SortFunc;

  reverse_sort(SF &sf) : SortFunc(sf) {}

  bool operator()(SUnit* left, SUnit* right) const {
    // reverse left/right rather than simply !SortFunc(left, right)
    // to expose different paths in the comparison logic.
    return SortFunc(right, left);
  }
};
#endif // NDEBUG

/// bu_ls_rr_sort - Priority function for bottom up register pressure
// reduction scheduler.
struct bu_ls_rr_sort : public queue_sort {
  enum {
    IsBottomUp = true,
    HasReadyFilter = false
  };

  RegReductionPQBase *SPQ;

  bu_ls_rr_sort(RegReductionPQBase *spq) : SPQ(spq) {}

  bool operator()(SUnit* left, SUnit* right) const;
};

// src_ls_rr_sort - Priority function for source order scheduler.
struct src_ls_rr_sort : public queue_sort {
  enum {
    IsBottomUp = true,
    HasReadyFilter = false
  };

  RegReductionPQBase *SPQ;

  src_ls_rr_sort(RegReductionPQBase *spq) : SPQ(spq) {}

  bool operator()(SUnit* left, SUnit* right) const;
};

// hybrid_ls_rr_sort - Priority function for hybrid scheduler.
struct hybrid_ls_rr_sort : public queue_sort {
  enum {
    IsBottomUp = true,
    HasReadyFilter = false
  };

  RegReductionPQBase *SPQ;

  hybrid_ls_rr_sort(RegReductionPQBase *spq) : SPQ(spq) {}

  bool isReady(SUnit *SU, unsigned CurCycle) const;

  bool operator()(SUnit* left, SUnit* right) const;
};

// ilp_ls_rr_sort - Priority function for ILP (instruction level parallelism)
// scheduler.
struct ilp_ls_rr_sort : public queue_sort {
  enum {
    IsBottomUp = true,
    HasReadyFilter = false
  };

  RegReductionPQBase *SPQ;

  ilp_ls_rr_sort(RegReductionPQBase *spq) : SPQ(spq) {}

  bool isReady(SUnit *SU, unsigned CurCycle) const;

  bool operator()(SUnit* left, SUnit* right) const;
};

class RegReductionPQBase : public SchedulingPriorityQueue {
protected:
  std::vector<SUnit *> Queue;
  unsigned CurQueueId = 0;
  bool TracksRegPressure;
  bool SrcOrder;

  // SUnits - The SUnits for the current graph.
  std::vector<SUnit> *SUnits = nullptr;

  MachineFunction &MF;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const TargetLowering *TLI = nullptr;
  ScheduleDAGRRList *scheduleDAG = nullptr;

  // SethiUllmanNumbers - The SethiUllman number for each node.
  std::vector<unsigned> SethiUllmanNumbers;

  /// RegPressure - Tracking current reg pressure per register class.
  std::vector<unsigned> RegPressure;

  /// RegLimit - Tracking the number of allocatable registers per register
  /// class.
  std::vector<unsigned> RegLimit;

public:
  RegReductionPQBase(MachineFunction &mf,
                     bool hasReadyFilter,
                     bool tracksrp,
                     bool srcorder,
                     const TargetInstrInfo *tii,
                     const TargetRegisterInfo *tri,
                     const TargetLowering *tli)
    : SchedulingPriorityQueue(hasReadyFilter), TracksRegPressure(tracksrp),
      SrcOrder(srcorder), MF(mf), TII(tii), TRI(tri), TLI(tli) {
    if (TracksRegPressure) {
      unsigned NumRC = TRI->getNumRegClasses();
      RegLimit.resize(NumRC);
      RegPressure.resize(NumRC);
      std::fill(RegLimit.begin(), RegLimit.end(), 0);
      std::fill(RegPressure.begin(), RegPressure.end(), 0);
      for (const TargetRegisterClass *RC : TRI->regclasses())
        RegLimit[RC->getID()] = tri->getRegPressureLimit(RC, MF);
    }
  }

  void setScheduleDAG(ScheduleDAGRRList *scheduleDag) {
    scheduleDAG = scheduleDag;
  }

  ScheduleHazardRecognizer* getHazardRec() {
    return scheduleDAG->getHazardRec();
  }

  void initNodes(std::vector<SUnit> &sunits) override;

  void addNode(const SUnit *SU) override;

  void updateNode(const SUnit *SU) override;

  void releaseState() override {
    SUnits = nullptr;
    SethiUllmanNumbers.clear();
    std::fill(RegPressure.begin(), RegPressure.end(), 0);
  }

  unsigned getNodePriority(const SUnit *SU) const;

  unsigned getNodeOrdering(const SUnit *SU) const {
    if (!SU->getNode()) return 0;

    return SU->getNode()->getIROrder();
  }

  bool empty() const override { return Queue.empty(); }

  void push(SUnit *U) override {
    assert(!U->NodeQueueId && "Node in the queue already");
    U->NodeQueueId = ++CurQueueId;
    Queue.push_back(U);
  }

  void remove(SUnit *SU) override {
    assert(!Queue.empty() && "Queue is empty!");
    assert(SU->NodeQueueId != 0 && "Not in queue!");
    std::vector<SUnit *>::iterator I = llvm::find(Queue, SU);
    if (I != std::prev(Queue.end()))
      std::swap(*I, Queue.back());
    Queue.pop_back();
    SU->NodeQueueId = 0;
  }

  bool tracksRegPressure() const override { return TracksRegPressure; }

  void dumpRegPressure() const;

  bool HighRegPressure(const SUnit *SU) const;

  bool MayReduceRegPressure(SUnit *SU) const;

  int RegPressureDiff(SUnit *SU, unsigned &LiveUses) const;

  void scheduledNode(SUnit *SU) override;

  void unscheduledNode(SUnit *SU) override;

protected:
  bool canClobber(const SUnit *SU, const SUnit *Op);
  void AddPseudoTwoAddrDeps();
  void PrescheduleNodesWithMultipleUses();
  void CalculateSethiUllmanNumbers();
};

template<class SF>
static SUnit *popFromQueueImpl(std::vector<SUnit *> &Q, SF &Picker) {
  unsigned BestIdx = 0;
  // Only compute the cost for the first 1000 items in the queue, to avoid
  // excessive compile-times for very large queues.
  for (unsigned I = 1, E = std::min(Q.size(), (decltype(Q.size()))1000); I != E;
       I++)
    if (Picker(Q[BestIdx], Q[I]))
      BestIdx = I;
  SUnit *V = Q[BestIdx];
  if (BestIdx + 1 != Q.size())
    std::swap(Q[BestIdx], Q.back());
  Q.pop_back();
  return V;
}

template<class SF>
SUnit *popFromQueue(std::vector<SUnit *> &Q, SF &Picker, ScheduleDAG *DAG) {
#ifndef NDEBUG
  if (DAG->StressSched) {
    reverse_sort<SF> RPicker(Picker);
    return popFromQueueImpl(Q, RPicker);
  }
#endif
  (void)DAG;
  return popFromQueueImpl(Q, Picker);
}

//===----------------------------------------------------------------------===//
//                RegReductionPriorityQueue Definition
//===----------------------------------------------------------------------===//
//
// This is a SchedulingPriorityQueue that schedules using Sethi Ullman numbers
// to reduce register pressure.
//
template<class SF>
class RegReductionPriorityQueue : public RegReductionPQBase {
  SF Picker;

public:
  RegReductionPriorityQueue(MachineFunction &mf,
                            bool tracksrp,
                            bool srcorder,
                            const TargetInstrInfo *tii,
                            const TargetRegisterInfo *tri,
                            const TargetLowering *tli)
    : RegReductionPQBase(mf, SF::HasReadyFilter, tracksrp, srcorder,
                         tii, tri, tli),
      Picker(this) {}

  bool isBottomUp() const override { return SF::IsBottomUp; }

  bool isReady(SUnit *U) const override {
    return Picker.HasReadyFilter && Picker.isReady(U, getCurCycle());
  }

  SUnit *pop() override {
    if (Queue.empty()) return nullptr;

    SUnit *V = popFromQueue(Queue, Picker, scheduleDAG);
    V->NodeQueueId = 0;
    return V;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump(ScheduleDAG *DAG) const override {
    // Emulate pop() without clobbering NodeQueueIds.
    std::vector<SUnit *> DumpQueue = Queue;
    SF DumpPicker = Picker;
    while (!DumpQueue.empty()) {
      SUnit *SU = popFromQueue(DumpQueue, DumpPicker, scheduleDAG);
      dbgs() << "Height " << SU->getHeight() << ": ";
      DAG->dumpNode(*SU);
    }
  }
#endif
};

using BURegReductionPriorityQueue = RegReductionPriorityQueue<bu_ls_rr_sort>;
using SrcRegReductionPriorityQueue = RegReductionPriorityQueue<src_ls_rr_sort>;
using HybridBURRPriorityQueue = RegReductionPriorityQueue<hybrid_ls_rr_sort>;
using ILPBURRPriorityQueue = RegReductionPriorityQueue<ilp_ls_rr_sort>;

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//           Static Node Priority for Register Pressure Reduction
//===----------------------------------------------------------------------===//

// Check for special nodes that bypass scheduling heuristics.
// Currently this pushes TokenFactor nodes down, but may be used for other
// pseudo-ops as well.
//
// Return -1 to schedule right above left, 1 for left above right.
// Return 0 if no bias exists.
static int checkSpecialNodes(const SUnit *left, const SUnit *right) {
  bool LSchedLow = left->isScheduleLow;
  bool RSchedLow = right->isScheduleLow;
  if (LSchedLow != RSchedLow)
    return LSchedLow < RSchedLow ? 1 : -1;
  return 0;
}

/// CalcNodeSethiUllmanNumber - Compute Sethi Ullman number.
/// Smaller number is the higher priority.
static unsigned
CalcNodeSethiUllmanNumber(const SUnit *SU, std::vector<unsigned> &SUNumbers) {
  if (SUNumbers[SU->NodeNum] != 0)
    return SUNumbers[SU->NodeNum];

  // Use WorkList to avoid stack overflow on excessively large IRs.
  struct WorkState {
    WorkState(const SUnit *SU) : SU(SU) {}
    const SUnit *SU;
    unsigned PredsProcessed = 0;
  };

  SmallVector<WorkState, 16> WorkList;
  WorkList.push_back(SU);
  while (!WorkList.empty()) {
    auto &Temp = WorkList.back();
    auto *TempSU = Temp.SU;
    bool AllPredsKnown = true;
    // Try to find a non-evaluated pred and push it into the processing stack.
    for (unsigned P = Temp.PredsProcessed; P < TempSU->Preds.size(); ++P) {
      auto &Pred = TempSU->Preds[P];
      if (Pred.isCtrl()) continue;  // ignore chain preds
      SUnit *PredSU = Pred.getSUnit();
      if (SUNumbers[PredSU->NodeNum] == 0) {
#ifndef NDEBUG
        // In debug mode, check that we don't have such element in the stack.
        for (auto It : WorkList)
          assert(It.SU != PredSU && "Trying to push an element twice?");
#endif
        // Next time start processing this one starting from the next pred.
        Temp.PredsProcessed = P + 1;
        WorkList.push_back(PredSU);
        AllPredsKnown = false;
        break;
      }
    }

    if (!AllPredsKnown)
      continue;

    // Once all preds are known, we can calculate the answer for this one.
    unsigned SethiUllmanNumber = 0;
    unsigned Extra = 0;
    for (const SDep &Pred : TempSU->Preds) {
      if (Pred.isCtrl()) continue;  // ignore chain preds
      SUnit *PredSU = Pred.getSUnit();
      unsigned PredSethiUllman = SUNumbers[PredSU->NodeNum];
      assert(PredSethiUllman > 0 && "We should have evaluated this pred!");
      if (PredSethiUllman > SethiUllmanNumber) {
        SethiUllmanNumber = PredSethiUllman;
        Extra = 0;
      } else if (PredSethiUllman == SethiUllmanNumber)
        ++Extra;
    }

    SethiUllmanNumber += Extra;
    if (SethiUllmanNumber == 0)
      SethiUllmanNumber = 1;
    SUNumbers[TempSU->NodeNum] = SethiUllmanNumber;
    WorkList.pop_back();
  }

  assert(SUNumbers[SU->NodeNum] > 0 && "SethiUllman should never be zero!");
  return SUNumbers[SU->NodeNum];
}

/// CalculateSethiUllmanNumbers - Calculate Sethi-Ullman numbers of all
/// scheduling units.
void RegReductionPQBase::CalculateSethiUllmanNumbers() {
  SethiUllmanNumbers.assign(SUnits->size(), 0);

  for (const SUnit &SU : *SUnits)
    CalcNodeSethiUllmanNumber(&SU, SethiUllmanNumbers);
}

void RegReductionPQBase::addNode(const SUnit *SU) {
  unsigned SUSize = SethiUllmanNumbers.size();
  if (SUnits->size() > SUSize)
    SethiUllmanNumbers.resize(SUSize*2, 0);
  CalcNodeSethiUllmanNumber(SU, SethiUllmanNumbers);
}

void RegReductionPQBase::updateNode(const SUnit *SU) {
  SethiUllmanNumbers[SU->NodeNum] = 0;
  CalcNodeSethiUllmanNumber(SU, SethiUllmanNumbers);
}

// Lower priority means schedule further down. For bottom-up scheduling, lower
// priority SUs are scheduled before higher priority SUs.
unsigned RegReductionPQBase::getNodePriority(const SUnit *SU) const {
  assert(SU->NodeNum < SethiUllmanNumbers.size());
  unsigned Opc = SU->getNode() ? SU->getNode()->getOpcode() : 0;
  if (Opc == ISD::TokenFactor || Opc == ISD::CopyToReg)
    // CopyToReg should be close to its uses to facilitate coalescing and
    // avoid spilling.
    return 0;
  if (Opc == TargetOpcode::EXTRACT_SUBREG ||
      Opc == TargetOpcode::SUBREG_TO_REG ||
      Opc == TargetOpcode::INSERT_SUBREG)
    // EXTRACT_SUBREG, INSERT_SUBREG, and SUBREG_TO_REG nodes should be
    // close to their uses to facilitate coalescing.
    return 0;
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
#if 1
  return SethiUllmanNumbers[SU->NodeNum];
#else
  unsigned Priority = SethiUllmanNumbers[SU->NodeNum];
  if (SU->isCallOp) {
    // FIXME: This assumes all of the defs are used as call operands.
    int NP = (int)Priority - SU->getNode()->getNumValues();
    return (NP > 0) ? NP : 0;
  }
  return Priority;
#endif
}

//===----------------------------------------------------------------------===//
//                     Register Pressure Tracking
//===----------------------------------------------------------------------===//

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegReductionPQBase::dumpRegPressure() const {
  for (const TargetRegisterClass *RC : TRI->regclasses()) {
    unsigned Id = RC->getID();
    unsigned RP = RegPressure[Id];
    if (!RP) continue;
    LLVM_DEBUG(dbgs() << TRI->getRegClassName(RC) << ": " << RP << " / "
                      << RegLimit[Id] << '\n');
  }
}
#endif

bool RegReductionPQBase::HighRegPressure(const SUnit *SU) const {
  if (!TLI)
    return false;

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue;
    SUnit *PredSU = Pred.getSUnit();
    // NumRegDefsLeft is zero when enough uses of this node have been scheduled
    // to cover the number of registers defined (they are all live).
    if (PredSU->NumRegDefsLeft == 0) {
      continue;
    }
    for (ScheduleDAGSDNodes::RegDefIter RegDefPos(PredSU, scheduleDAG);
         RegDefPos.IsValid(); RegDefPos.Advance()) {
      unsigned RCId, Cost;
      GetCostForDef(RegDefPos, TLI, TII, TRI, RCId, Cost, MF);

      if ((RegPressure[RCId] + Cost) >= RegLimit[RCId])
        return true;
    }
  }
  return false;
}

bool RegReductionPQBase::MayReduceRegPressure(SUnit *SU) const {
  const SDNode *N = SU->getNode();

  if (!N->isMachineOpcode() || !SU->NumSuccs)
    return false;

  unsigned NumDefs = TII->get(N->getMachineOpcode()).getNumDefs();
  for (unsigned i = 0; i != NumDefs; ++i) {
    MVT VT = N->getSimpleValueType(i);
    if (!N->hasAnyUseOfValue(i))
      continue;
    unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
    if (RegPressure[RCId] >= RegLimit[RCId])
      return true;
  }
  return false;
}

// Compute the register pressure contribution by this instruction by count up
// for uses that are not live and down for defs. Only count register classes
// that are already under high pressure. As a side effect, compute the number of
// uses of registers that are already live.
//
// FIXME: This encompasses the logic in HighRegPressure and MayReduceRegPressure
// so could probably be factored.
int RegReductionPQBase::RegPressureDiff(SUnit *SU, unsigned &LiveUses) const {
  LiveUses = 0;
  int PDiff = 0;
  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue;
    SUnit *PredSU = Pred.getSUnit();
    // NumRegDefsLeft is zero when enough uses of this node have been scheduled
    // to cover the number of registers defined (they are all live).
    if (PredSU->NumRegDefsLeft == 0) {
      if (PredSU->getNode()->isMachineOpcode())
        ++LiveUses;
      continue;
    }
    for (ScheduleDAGSDNodes::RegDefIter RegDefPos(PredSU, scheduleDAG);
         RegDefPos.IsValid(); RegDefPos.Advance()) {
      MVT VT = RegDefPos.GetValue();
      unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
      if (RegPressure[RCId] >= RegLimit[RCId])
        ++PDiff;
    }
  }
  const SDNode *N = SU->getNode();

  if (!N || !N->isMachineOpcode() || !SU->NumSuccs)
    return PDiff;

  unsigned NumDefs = TII->get(N->getMachineOpcode()).getNumDefs();
  for (unsigned i = 0; i != NumDefs; ++i) {
    MVT VT = N->getSimpleValueType(i);
    if (!N->hasAnyUseOfValue(i))
      continue;
    unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
    if (RegPressure[RCId] >= RegLimit[RCId])
      --PDiff;
  }
  return PDiff;
}

void RegReductionPQBase::scheduledNode(SUnit *SU) {
  if (!TracksRegPressure)
    return;

  if (!SU->getNode())
    return;

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue;
    SUnit *PredSU = Pred.getSUnit();
    // NumRegDefsLeft is zero when enough uses of this node have been scheduled
    // to cover the number of registers defined (they are all live).
    if (PredSU->NumRegDefsLeft == 0) {
      continue;
    }
    // FIXME: The ScheduleDAG currently loses information about which of a
    // node's values is consumed by each dependence. Consequently, if the node
    // defines multiple register classes, we don't know which to pressurize
    // here. Instead the following loop consumes the register defs in an
    // arbitrary order. At least it handles the common case of clustered loads
    // to the same class. For precise liveness, each SDep needs to indicate the
    // result number. But that tightly couples the ScheduleDAG with the
    // SelectionDAG making updates tricky. A simpler hack would be to attach a
    // value type or register class to SDep.
    //
    // The most important aspect of register tracking is balancing the increase
    // here with the reduction further below. Note that this SU may use multiple
    // defs in PredSU. The can't be determined here, but we've already
    // compensated by reducing NumRegDefsLeft in PredSU during
    // ScheduleDAGSDNodes::AddSchedEdges.
    --PredSU->NumRegDefsLeft;
    unsigned SkipRegDefs = PredSU->NumRegDefsLeft;
    for (ScheduleDAGSDNodes::RegDefIter RegDefPos(PredSU, scheduleDAG);
         RegDefPos.IsValid(); RegDefPos.Advance(), --SkipRegDefs) {
      if (SkipRegDefs)
        continue;

      unsigned RCId, Cost;
      GetCostForDef(RegDefPos, TLI, TII, TRI, RCId, Cost, MF);
      RegPressure[RCId] += Cost;
      break;
    }
  }

  // We should have this assert, but there may be dead SDNodes that never
  // materialize as SUnits, so they don't appear to generate liveness.
  //assert(SU->NumRegDefsLeft == 0 && "not all regdefs have scheduled uses");
  int SkipRegDefs = (int)SU->NumRegDefsLeft;
  for (ScheduleDAGSDNodes::RegDefIter RegDefPos(SU, scheduleDAG);
       RegDefPos.IsValid(); RegDefPos.Advance(), --SkipRegDefs) {
    if (SkipRegDefs > 0)
      continue;
    unsigned RCId, Cost;
    GetCostForDef(RegDefPos, TLI, TII, TRI, RCId, Cost, MF);
    if (RegPressure[RCId] < Cost) {
      // Register pressure tracking is imprecise. This can happen. But we try
      // hard not to let it happen because it likely results in poor scheduling.
      LLVM_DEBUG(dbgs() << "  SU(" << SU->NodeNum
                        << ") has too many regdefs\n");
      RegPressure[RCId] = 0;
    }
    else {
      RegPressure[RCId] -= Cost;
    }
  }
  LLVM_DEBUG(dumpRegPressure());
}

void RegReductionPQBase::unscheduledNode(SUnit *SU) {
  if (!TracksRegPressure)
    return;

  const SDNode *N = SU->getNode();
  if (!N) return;

  if (!N->isMachineOpcode()) {
    if (N->getOpcode() != ISD::CopyToReg)
      return;
  } else {
    unsigned Opc = N->getMachineOpcode();
    if (Opc == TargetOpcode::EXTRACT_SUBREG ||
        Opc == TargetOpcode::INSERT_SUBREG ||
        Opc == TargetOpcode::SUBREG_TO_REG ||
        Opc == TargetOpcode::REG_SEQUENCE ||
        Opc == TargetOpcode::IMPLICIT_DEF)
      return;
  }

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue;
    SUnit *PredSU = Pred.getSUnit();
    // NumSuccsLeft counts all deps. Don't compare it with NumSuccs which only
    // counts data deps.
    if (PredSU->NumSuccsLeft != PredSU->Succs.size())
      continue;
    const SDNode *PN = PredSU->getNode();
    if (!PN->isMachineOpcode()) {
      if (PN->getOpcode() == ISD::CopyFromReg) {
        MVT VT = PN->getSimpleValueType(0);
        unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
        RegPressure[RCId] += TLI->getRepRegClassCostFor(VT);
      }
      continue;
    }
    unsigned POpc = PN->getMachineOpcode();
    if (POpc == TargetOpcode::IMPLICIT_DEF)
      continue;
    if (POpc == TargetOpcode::EXTRACT_SUBREG ||
        POpc == TargetOpcode::INSERT_SUBREG ||
        POpc == TargetOpcode::SUBREG_TO_REG) {
      MVT VT = PN->getSimpleValueType(0);
      unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
      RegPressure[RCId] += TLI->getRepRegClassCostFor(VT);
      continue;
    }
    if (POpc == TargetOpcode::REG_SEQUENCE) {
      unsigned DstRCIdx = PN->getConstantOperandVal(0);
      const TargetRegisterClass *RC = TRI->getRegClass(DstRCIdx);
      unsigned RCId = RC->getID();
      // REG_SEQUENCE is untyped, so getRepRegClassCostFor could not be used
      // here. Instead use the same constant as in GetCostForDef.
      RegPressure[RCId] += RegSequenceCost;
      continue;
    }
    unsigned NumDefs = TII->get(PN->getMachineOpcode()).getNumDefs();
    for (unsigned i = 0; i != NumDefs; ++i) {
      MVT VT = PN->getSimpleValueType(i);
      if (!PN->hasAnyUseOfValue(i))
        continue;
      unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
      if (RegPressure[RCId] < TLI->getRepRegClassCostFor(VT))
        // Register pressure tracking is imprecise. This can happen.
        RegPressure[RCId] = 0;
      else
        RegPressure[RCId] -= TLI->getRepRegClassCostFor(VT);
    }
  }

  // Check for isMachineOpcode() as PrescheduleNodesWithMultipleUses()
  // may transfer data dependencies to CopyToReg.
  if (SU->NumSuccs && N->isMachineOpcode()) {
    unsigned NumDefs = TII->get(N->getMachineOpcode()).getNumDefs();
    for (unsigned i = NumDefs, e = N->getNumValues(); i != e; ++i) {
      MVT VT = N->getSimpleValueType(i);
      if (VT == MVT::Glue || VT == MVT::Other)
        continue;
      if (!N->hasAnyUseOfValue(i))
        continue;
      unsigned RCId = TLI->getRepRegClassFor(VT)->getID();
      RegPressure[RCId] += TLI->getRepRegClassCostFor(VT);
    }
  }

  LLVM_DEBUG(dumpRegPressure());
}

//===----------------------------------------------------------------------===//
//           Dynamic Node Priority for Register Pressure Reduction
//===----------------------------------------------------------------------===//

/// closestSucc - Returns the scheduled cycle of the successor which is
/// closest to the current cycle.
static unsigned closestSucc(const SUnit *SU) {
  unsigned MaxHeight = 0;
  for (const SDep &Succ : SU->Succs) {
    if (Succ.isCtrl()) continue;  // ignore chain succs
    unsigned Height = Succ.getSUnit()->getHeight();
    // If there are bunch of CopyToRegs stacked up, they should be considered
    // to be at the same position.
    if (Succ.getSUnit()->getNode() &&
        Succ.getSUnit()->getNode()->getOpcode() == ISD::CopyToReg)
      Height = closestSucc(Succ.getSUnit())+1;
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

/// hasOnlyLiveInOpers - Return true if SU has only value predecessors that are
/// CopyFromReg from a virtual register.
static bool hasOnlyLiveInOpers(const SUnit *SU) {
  bool RetVal = false;
  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;
    const SUnit *PredSU = Pred.getSUnit();
    if (PredSU->getNode() &&
        PredSU->getNode()->getOpcode() == ISD::CopyFromReg) {
      Register Reg =
          cast<RegisterSDNode>(PredSU->getNode()->getOperand(1))->getReg();
      if (Reg.isVirtual()) {
        RetVal = true;
        continue;
      }
    }
    return false;
  }
  return RetVal;
}

/// hasOnlyLiveOutUses - Return true if SU has only value successors that are
/// CopyToReg to a virtual register. This SU def is probably a liveout and
/// it has no other use. It should be scheduled closer to the terminator.
static bool hasOnlyLiveOutUses(const SUnit *SU) {
  bool RetVal = false;
  for (const SDep &Succ : SU->Succs) {
    if (Succ.isCtrl()) continue;
    const SUnit *SuccSU = Succ.getSUnit();
    if (SuccSU->getNode() && SuccSU->getNode()->getOpcode() == ISD::CopyToReg) {
      Register Reg =
          cast<RegisterSDNode>(SuccSU->getNode()->getOperand(1))->getReg();
      if (Reg.isVirtual()) {
        RetVal = true;
        continue;
      }
    }
    return false;
  }
  return RetVal;
}

// Set isVRegCycle for a node with only live in opers and live out uses. Also
// set isVRegCycle for its CopyFromReg operands.
//
// This is only relevant for single-block loops, in which case the VRegCycle
// node is likely an induction variable in which the operand and target virtual
// registers should be coalesced (e.g. pre/post increment values). Setting the
// isVRegCycle flag helps the scheduler prioritize other uses of the same
// CopyFromReg so that this node becomes the virtual register "kill". This
// avoids interference between the values live in and out of the block and
// eliminates a copy inside the loop.
static void initVRegCycle(SUnit *SU) {
  if (DisableSchedVRegCycle)
    return;

  if (!hasOnlyLiveInOpers(SU) || !hasOnlyLiveOutUses(SU))
    return;

  LLVM_DEBUG(dbgs() << "VRegCycle: SU(" << SU->NodeNum << ")\n");

  SU->isVRegCycle = true;

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;
    Pred.getSUnit()->isVRegCycle = true;
  }
}

// After scheduling the definition of a VRegCycle, clear the isVRegCycle flag of
// CopyFromReg operands. We should no longer penalize other uses of this VReg.
static void resetVRegCycle(SUnit *SU) {
  if (!SU->isVRegCycle)
    return;

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;  // ignore chain preds
    SUnit *PredSU = Pred.getSUnit();
    if (PredSU->isVRegCycle) {
      assert(PredSU->getNode()->getOpcode() == ISD::CopyFromReg &&
             "VRegCycle def must be CopyFromReg");
      Pred.getSUnit()->isVRegCycle = false;
    }
  }
}

// Return true if this SUnit uses a CopyFromReg node marked as a VRegCycle. This
// means a node that defines the VRegCycle has not been scheduled yet.
static bool hasVRegCycleUse(const SUnit *SU) {
  // If this SU also defines the VReg, don't hoist it as a "use".
  if (SU->isVRegCycle)
    return false;

  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl()) continue;  // ignore chain preds
    if (Pred.getSUnit()->isVRegCycle &&
        Pred.getSUnit()->getNode()->getOpcode() == ISD::CopyFromReg) {
      LLVM_DEBUG(dbgs() << "  VReg cycle use: SU (" << SU->NodeNum << ")\n");
      return true;
    }
  }
  return false;
}

// Check for either a dependence (latency) or resource (hazard) stall.
//
// Note: The ScheduleHazardRecognizer interface requires a non-const SU.
static bool BUHasStall(SUnit *SU, int Height, RegReductionPQBase *SPQ) {
  if ((int)SPQ->getCurCycle() < Height) return true;
  if (SPQ->getHazardRec()->getHazardType(SU, 0)
      != ScheduleHazardRecognizer::NoHazard)
    return true;
  return false;
}

// Return -1 if left has higher priority, 1 if right has higher priority.
// Return 0 if latency-based priority is equivalent.
static int BUCompareLatency(SUnit *left, SUnit *right, bool checkPref,
                            RegReductionPQBase *SPQ) {
  // Scheduling an instruction that uses a VReg whose postincrement has not yet
  // been scheduled will induce a copy. Model this as an extra cycle of latency.
  int LPenalty = hasVRegCycleUse(left) ? 1 : 0;
  int RPenalty = hasVRegCycleUse(right) ? 1 : 0;
  int LHeight = (int)left->getHeight() + LPenalty;
  int RHeight = (int)right->getHeight() + RPenalty;

  bool LStall = (!checkPref || left->SchedulingPref == Sched::ILP) &&
    BUHasStall(left, LHeight, SPQ);
  bool RStall = (!checkPref || right->SchedulingPref == Sched::ILP) &&
    BUHasStall(right, RHeight, SPQ);

  // If scheduling one of the node will cause a pipeline stall, delay it.
  // If scheduling either one of the node will cause a pipeline stall, sort
  // them according to their height.
  if (LStall) {
    if (!RStall)
      return 1;
    if (LHeight != RHeight)
      return LHeight > RHeight ? 1 : -1;
  } else if (RStall)
    return -1;

  // If either node is scheduling for latency, sort them by height/depth
  // and latency.
  if (!checkPref || (left->SchedulingPref == Sched::ILP ||
                     right->SchedulingPref == Sched::ILP)) {
    // If neither instruction stalls (!LStall && !RStall) and HazardRecognizer
    // is enabled, grouping instructions by cycle, then its height is already
    // covered so only its depth matters. We also reach this point if both stall
    // but have the same height.
    if (!SPQ->getHazardRec()->isEnabled()) {
      if (LHeight != RHeight)
        return LHeight > RHeight ? 1 : -1;
    }
    int LDepth = left->getDepth() - LPenalty;
    int RDepth = right->getDepth() - RPenalty;
    if (LDepth != RDepth) {
      LLVM_DEBUG(dbgs() << "  Comparing latency of SU (" << left->NodeNum
                        << ") depth " << LDepth << " vs SU (" << right->NodeNum
                        << ") depth " << RDepth << "\n");
      return LDepth < RDepth ? 1 : -1;
    }
    if (left->Latency != right->Latency)
      return left->Latency > right->Latency ? 1 : -1;
  }
  return 0;
}

static bool BURRSort(SUnit *left, SUnit *right, RegReductionPQBase *SPQ) {
  // Schedule physical register definitions close to their use. This is
  // motivated by microarchitectures that can fuse cmp+jump macro-ops. But as
  // long as shortening physreg live ranges is generally good, we can defer
  // creating a subtarget hook.
  if (!DisableSchedPhysRegJoin) {
    bool LHasPhysReg = left->hasPhysRegDefs;
    bool RHasPhysReg = right->hasPhysRegDefs;
    if (LHasPhysReg != RHasPhysReg) {
      #ifndef NDEBUG
      static const char *const PhysRegMsg[] = { " has no physreg",
                                                " defines a physreg" };
      #endif
      LLVM_DEBUG(dbgs() << "  SU (" << left->NodeNum << ") "
                        << PhysRegMsg[LHasPhysReg] << " SU(" << right->NodeNum
                        << ") " << PhysRegMsg[RHasPhysReg] << "\n");
      return LHasPhysReg < RHasPhysReg;
    }
  }

  // Prioritize by Sethi-Ulmann number and push CopyToReg nodes down.
  unsigned LPriority = SPQ->getNodePriority(left);
  unsigned RPriority = SPQ->getNodePriority(right);

  // Be really careful about hoisting call operands above previous calls.
  // Only allows it if it would reduce register pressure.
  if (left->isCall && right->isCallOp) {
    unsigned RNumVals = right->getNode()->getNumValues();
    RPriority = (RPriority > RNumVals) ? (RPriority - RNumVals) : 0;
  }
  if (right->isCall && left->isCallOp) {
    unsigned LNumVals = left->getNode()->getNumValues();
    LPriority = (LPriority > LNumVals) ? (LPriority - LNumVals) : 0;
  }

  if (LPriority != RPriority)
    return LPriority > RPriority;

  // One or both of the nodes are calls and their sethi-ullman numbers are the
  // same, then keep source order.
  if (left->isCall || right->isCall) {
    unsigned LOrder = SPQ->getNodeOrdering(left);
    unsigned ROrder = SPQ->getNodeOrdering(right);

    // Prefer an ordering where the lower the non-zero order number, the higher
    // the preference.
    if ((LOrder || ROrder) && LOrder != ROrder)
      return LOrder != 0 && (LOrder < ROrder || ROrder == 0);
  }

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
    return LDist < RDist;

  // How many registers becomes live when the node is scheduled.
  unsigned LScratch = calcMaxScratches(left);
  unsigned RScratch = calcMaxScratches(right);
  if (LScratch != RScratch)
    return LScratch > RScratch;

  // Comparing latency against a call makes little sense unless the node
  // is register pressure-neutral.
  if ((left->isCall && RPriority > 0) || (right->isCall && LPriority > 0))
    return (left->NodeQueueId > right->NodeQueueId);

  // Do not compare latencies when one or both of the nodes are calls.
  if (!DisableSchedCycles &&
      !(left->isCall || right->isCall)) {
    int result = BUCompareLatency(left, right, false /*checkPref*/, SPQ);
    if (result != 0)
      return result > 0;
  }
  else {
    if (left->getHeight() != right->getHeight())
      return left->getHeight() > right->getHeight();

    if (left->getDepth() != right->getDepth())
      return left->getDepth() < right->getDepth();
  }

  assert(left->NodeQueueId && right->NodeQueueId &&
         "NodeQueueId cannot be zero");
  return (left->NodeQueueId > right->NodeQueueId);
}

// Bottom up
bool bu_ls_rr_sort::operator()(SUnit *left, SUnit *right) const {
  if (int res = checkSpecialNodes(left, right))
    return res > 0;

  return BURRSort(left, right, SPQ);
}

// Source order, otherwise bottom up.
bool src_ls_rr_sort::operator()(SUnit *left, SUnit *right) const {
  if (int res = checkSpecialNodes(left, right))
    return res > 0;

  unsigned LOrder = SPQ->getNodeOrdering(left);
  unsigned ROrder = SPQ->getNodeOrdering(right);

  // Prefer an ordering where the lower the non-zero order number, the higher
  // the preference.
  if ((LOrder || ROrder) && LOrder != ROrder)
    return LOrder != 0 && (LOrder < ROrder || ROrder == 0);

  return BURRSort(left, right, SPQ);
}

// If the time between now and when the instruction will be ready can cover
// the spill code, then avoid adding it to the ready queue. This gives long
// stalls highest priority and allows hoisting across calls. It should also
// speed up processing the available queue.
bool hybrid_ls_rr_sort::isReady(SUnit *SU, unsigned CurCycle) const {
  static const unsigned ReadyDelay = 3;

  if (SPQ->MayReduceRegPressure(SU)) return true;

  if (SU->getHeight() > (CurCycle + ReadyDelay)) return false;

  if (SPQ->getHazardRec()->getHazardType(SU, -ReadyDelay)
      != ScheduleHazardRecognizer::NoHazard)
    return false;

  return true;
}

// Return true if right should be scheduled with higher priority than left.
bool hybrid_ls_rr_sort::operator()(SUnit *left, SUnit *right) const {
  if (int res = checkSpecialNodes(left, right))
    return res > 0;

  if (left->isCall || right->isCall)
    // No way to compute latency of calls.
    return BURRSort(left, right, SPQ);

  bool LHigh = SPQ->HighRegPressure(left);
  bool RHigh = SPQ->HighRegPressure(right);
  // Avoid causing spills. If register pressure is high, schedule for
  // register pressure reduction.
  if (LHigh && !RHigh) {
    LLVM_DEBUG(dbgs() << "  pressure SU(" << left->NodeNum << ") > SU("
                      << right->NodeNum << ")\n");
    return true;
  }
  else if (!LHigh && RHigh) {
    LLVM_DEBUG(dbgs() << "  pressure SU(" << right->NodeNum << ") > SU("
                      << left->NodeNum << ")\n");
    return false;
  }
  if (!LHigh && !RHigh) {
    int result = BUCompareLatency(left, right, true /*checkPref*/, SPQ);
    if (result != 0)
      return result > 0;
  }
  return BURRSort(left, right, SPQ);
}

// Schedule as many instructions in each cycle as possible. So don't make an
// instruction available unless it is ready in the current cycle.
bool ilp_ls_rr_sort::isReady(SUnit *SU, unsigned CurCycle) const {
  if (SU->getHeight() > CurCycle) return false;

  if (SPQ->getHazardRec()->getHazardType(SU, 0)
      != ScheduleHazardRecognizer::NoHazard)
    return false;

  return true;
}

static bool canEnableCoalescing(SUnit *SU) {
  unsigned Opc = SU->getNode() ? SU->getNode()->getOpcode() : 0;
  if (Opc == ISD::TokenFactor || Opc == ISD::CopyToReg)
    // CopyToReg should be close to its uses to facilitate coalescing and
    // avoid spilling.
    return true;

  if (Opc == TargetOpcode::EXTRACT_SUBREG ||
      Opc == TargetOpcode::SUBREG_TO_REG ||
      Opc == TargetOpcode::INSERT_SUBREG)
    // EXTRACT_SUBREG, INSERT_SUBREG, and SUBREG_TO_REG nodes should be
    // close to their uses to facilitate coalescing.
    return true;

  if (SU->NumPreds == 0 && SU->NumSuccs != 0)
    // If SU does not have a register def, schedule it close to its uses
    // because it does not lengthen any live ranges.
    return true;

  return false;
}

// list-ilp is currently an experimental scheduler that allows various
// heuristics to be enabled prior to the normal register reduction logic.
bool ilp_ls_rr_sort::operator()(SUnit *left, SUnit *right) const {
  if (int res = checkSpecialNodes(left, right))
    return res > 0;

  if (left->isCall || right->isCall)
    // No way to compute latency of calls.
    return BURRSort(left, right, SPQ);

  unsigned LLiveUses = 0, RLiveUses = 0;
  int LPDiff = 0, RPDiff = 0;
  if (!DisableSchedRegPressure || !DisableSchedLiveUses) {
    LPDiff = SPQ->RegPressureDiff(left, LLiveUses);
    RPDiff = SPQ->RegPressureDiff(right, RLiveUses);
  }
  if (!DisableSchedRegPressure && LPDiff != RPDiff) {
    LLVM_DEBUG(dbgs() << "RegPressureDiff SU(" << left->NodeNum
                      << "): " << LPDiff << " != SU(" << right->NodeNum
                      << "): " << RPDiff << "\n");
    return LPDiff > RPDiff;
  }

  if (!DisableSchedRegPressure && (LPDiff > 0 || RPDiff > 0)) {
    bool LReduce = canEnableCoalescing(left);
    bool RReduce = canEnableCoalescing(right);
    if (LReduce && !RReduce) return false;
    if (RReduce && !LReduce) return true;
  }

  if (!DisableSchedLiveUses && (LLiveUses != RLiveUses)) {
    LLVM_DEBUG(dbgs() << "Live uses SU(" << left->NodeNum << "): " << LLiveUses
                      << " != SU(" << right->NodeNum << "): " << RLiveUses
                      << "\n");
    return LLiveUses < RLiveUses;
  }

  if (!DisableSchedStalls) {
    bool LStall = BUHasStall(left, left->getHeight(), SPQ);
    bool RStall = BUHasStall(right, right->getHeight(), SPQ);
    if (LStall != RStall)
      return left->getHeight() > right->getHeight();
  }

  if (!DisableSchedCriticalPath) {
    int spread = (int)left->getDepth() - (int)right->getDepth();
    if (std::abs(spread) > MaxReorderWindow) {
      LLVM_DEBUG(dbgs() << "Depth of SU(" << left->NodeNum << "): "
                        << left->getDepth() << " != SU(" << right->NodeNum
                        << "): " << right->getDepth() << "\n");
      return left->getDepth() < right->getDepth();
    }
  }

  if (!DisableSchedHeight && left->getHeight() != right->getHeight()) {
    int spread = (int)left->getHeight() - (int)right->getHeight();
    if (std::abs(spread) > MaxReorderWindow)
      return left->getHeight() > right->getHeight();
  }

  return BURRSort(left, right, SPQ);
}

void RegReductionPQBase::initNodes(std::vector<SUnit> &sunits) {
  SUnits = &sunits;
  // Add pseudo dependency edges for two-address nodes.
  if (!Disable2AddrHack)
    AddPseudoTwoAddrDeps();
  // Reroute edges to nodes with multiple uses.
  if (!TracksRegPressure && !SrcOrder)
    PrescheduleNodesWithMultipleUses();
  // Calculate node priorities.
  CalculateSethiUllmanNumbers();

  // For single block loops, mark nodes that look like canonical IV increments.
  if (scheduleDAG->BB->isSuccessor(scheduleDAG->BB))
    for (SUnit &SU : sunits)
      initVRegCycle(&SU);
}

//===----------------------------------------------------------------------===//
//                    Preschedule for Register Pressure
//===----------------------------------------------------------------------===//

bool RegReductionPQBase::canClobber(const SUnit *SU, const SUnit *Op) {
  if (SU->isTwoAddress) {
    unsigned Opc = SU->getNode()->getMachineOpcode();
    const MCInstrDesc &MCID = TII->get(Opc);
    unsigned NumRes = MCID.getNumDefs();
    unsigned NumOps = MCID.getNumOperands() - NumRes;
    for (unsigned i = 0; i != NumOps; ++i) {
      if (MCID.getOperandConstraint(i+NumRes, MCOI::TIED_TO) != -1) {
        SDNode *DU = SU->getNode()->getOperand(i).getNode();
        if (DU->getNodeId() != -1 &&
            Op->OrigNode == &(*SUnits)[DU->getNodeId()])
          return true;
      }
    }
  }
  return false;
}

/// canClobberReachingPhysRegUse - True if SU would clobber one of it's
/// successor's explicit physregs whose definition can reach DepSU.
/// i.e. DepSU should not be scheduled above SU.
static bool canClobberReachingPhysRegUse(const SUnit *DepSU, const SUnit *SU,
                                         ScheduleDAGRRList *scheduleDAG,
                                         const TargetInstrInfo *TII,
                                         const TargetRegisterInfo *TRI) {
  ArrayRef<MCPhysReg> ImpDefs =
      TII->get(SU->getNode()->getMachineOpcode()).implicit_defs();
  const uint32_t *RegMask = getNodeRegMask(SU->getNode());
  if (ImpDefs.empty() && !RegMask)
    return false;

  for (const SDep &Succ : SU->Succs) {
    SUnit *SuccSU = Succ.getSUnit();
    for (const SDep &SuccPred : SuccSU->Preds) {
      if (!SuccPred.isAssignedRegDep())
        continue;

      if (RegMask &&
          MachineOperand::clobbersPhysReg(RegMask, SuccPred.getReg()) &&
          scheduleDAG->IsReachable(DepSU, SuccPred.getSUnit()))
        return true;

      for (MCPhysReg ImpDef : ImpDefs) {
        // Return true if SU clobbers this physical register use and the
        // definition of the register reaches from DepSU. IsReachable queries
        // a topological forward sort of the DAG (following the successors).
        if (TRI->regsOverlap(ImpDef, SuccPred.getReg()) &&
            scheduleDAG->IsReachable(DepSU, SuccPred.getSUnit()))
          return true;
      }
    }
  }
  return false;
}

/// canClobberPhysRegDefs - True if SU would clobber one of SuccSU's
/// physical register defs.
static bool canClobberPhysRegDefs(const SUnit *SuccSU, const SUnit *SU,
                                  const TargetInstrInfo *TII,
                                  const TargetRegisterInfo *TRI) {
  SDNode *N = SuccSU->getNode();
  unsigned NumDefs = TII->get(N->getMachineOpcode()).getNumDefs();
  ArrayRef<MCPhysReg> ImpDefs = TII->get(N->getMachineOpcode()).implicit_defs();
  assert(!ImpDefs.empty() && "Caller should check hasPhysRegDefs");
  for (const SDNode *SUNode = SU->getNode(); SUNode;
       SUNode = SUNode->getGluedNode()) {
    if (!SUNode->isMachineOpcode())
      continue;
    ArrayRef<MCPhysReg> SUImpDefs =
        TII->get(SUNode->getMachineOpcode()).implicit_defs();
    const uint32_t *SURegMask = getNodeRegMask(SUNode);
    if (SUImpDefs.empty() && !SURegMask)
      continue;
    for (unsigned i = NumDefs, e = N->getNumValues(); i != e; ++i) {
      MVT VT = N->getSimpleValueType(i);
      if (VT == MVT::Glue || VT == MVT::Other)
        continue;
      if (!N->hasAnyUseOfValue(i))
        continue;
      MCPhysReg Reg = ImpDefs[i - NumDefs];
      if (SURegMask && MachineOperand::clobbersPhysReg(SURegMask, Reg))
        return true;
      for (MCPhysReg SUReg : SUImpDefs) {
        if (TRI->regsOverlap(Reg, SUReg))
          return true;
      }
    }
  }
  return false;
}

/// PrescheduleNodesWithMultipleUses - Nodes with multiple uses
/// are not handled well by the general register pressure reduction
/// heuristics. When presented with code like this:
///
///      N
///    / |
///   /  |
///  U  store
///  |
/// ...
///
/// the heuristics tend to push the store up, but since the
/// operand of the store has another use (U), this would increase
/// the length of that other use (the U->N edge).
///
/// This function transforms code like the above to route U's
/// dependence through the store when possible, like this:
///
///      N
///      ||
///      ||
///     store
///       |
///       U
///       |
///      ...
///
/// This results in the store being scheduled immediately
/// after N, which shortens the U->N live range, reducing
/// register pressure.
void RegReductionPQBase::PrescheduleNodesWithMultipleUses() {
  // Visit all the nodes in topological order, working top-down.
  for (SUnit &SU : *SUnits) {
    // For now, only look at nodes with no data successors, such as stores.
    // These are especially important, due to the heuristics in
    // getNodePriority for nodes with no data successors.
    if (SU.NumSuccs != 0)
      continue;
    // For now, only look at nodes with exactly one data predecessor.
    if (SU.NumPreds != 1)
      continue;
    // Avoid prescheduling copies to virtual registers, which don't behave
    // like other nodes from the perspective of scheduling heuristics.
    if (SDNode *N = SU.getNode())
      if (N->getOpcode() == ISD::CopyToReg &&
          cast<RegisterSDNode>(N->getOperand(1))->getReg().isVirtual())
        continue;

    SDNode *PredFrameSetup = nullptr;
    for (const SDep &Pred : SU.Preds)
      if (Pred.isCtrl() && Pred.getSUnit()) {
        // Find the predecessor which is not data dependence.
        SDNode *PredND = Pred.getSUnit()->getNode();

        // If PredND is FrameSetup, we should not pre-scheduled the node,
        // or else, when bottom up scheduling, ADJCALLSTACKDOWN and
        // ADJCALLSTACKUP may hold CallResource too long and make other
        // calls can't be scheduled. If there's no other available node
        // to schedule, the schedular will try to rename the register by
        // creating copy to avoid the conflict which will fail because
        // CallResource is not a real physical register.
        if (PredND && PredND->isMachineOpcode() &&
            (PredND->getMachineOpcode() == TII->getCallFrameSetupOpcode())) {
          PredFrameSetup = PredND;
          break;
        }
      }
    // Skip the node has FrameSetup parent.
    if (PredFrameSetup != nullptr)
      continue;

    // Locate the single data predecessor.
    SUnit *PredSU = nullptr;
    for (const SDep &Pred : SU.Preds)
      if (!Pred.isCtrl()) {
        PredSU = Pred.getSUnit();
        break;
      }
    assert(PredSU);

    // Don't rewrite edges that carry physregs, because that requires additional
    // support infrastructure.
    if (PredSU->hasPhysRegDefs)
      continue;
    // Short-circuit the case where SU is PredSU's only data successor.
    if (PredSU->NumSuccs == 1)
      continue;
    // Avoid prescheduling to copies from virtual registers, which don't behave
    // like other nodes from the perspective of scheduling heuristics.
    if (SDNode *N = SU.getNode())
      if (N->getOpcode() == ISD::CopyFromReg &&
          cast<RegisterSDNode>(N->getOperand(1))->getReg().isVirtual())
        continue;

    // Perform checks on the successors of PredSU.
    for (const SDep &PredSucc : PredSU->Succs) {
      SUnit *PredSuccSU = PredSucc.getSUnit();
      if (PredSuccSU == &SU) continue;
      // If PredSU has another successor with no data successors, for
      // now don't attempt to choose either over the other.
      if (PredSuccSU->NumSuccs == 0)
        goto outer_loop_continue;
      // Don't break physical register dependencies.
      if (SU.hasPhysRegClobbers && PredSuccSU->hasPhysRegDefs)
        if (canClobberPhysRegDefs(PredSuccSU, &SU, TII, TRI))
          goto outer_loop_continue;
      // Don't introduce graph cycles.
      if (scheduleDAG->IsReachable(&SU, PredSuccSU))
        goto outer_loop_continue;
    }

    // Ok, the transformation is safe and the heuristics suggest it is
    // profitable. Update the graph.
    LLVM_DEBUG(
        dbgs() << "    Prescheduling SU #" << SU.NodeNum << " next to PredSU #"
               << PredSU->NodeNum
               << " to guide scheduling in the presence of multiple uses\n");
    for (unsigned i = 0; i != PredSU->Succs.size(); ++i) {
      SDep Edge = PredSU->Succs[i];
      assert(!Edge.isAssignedRegDep());
      SUnit *SuccSU = Edge.getSUnit();
      if (SuccSU != &SU) {
        Edge.setSUnit(PredSU);
        scheduleDAG->RemovePred(SuccSU, Edge);
        scheduleDAG->AddPredQueued(&SU, Edge);
        Edge.setSUnit(&SU);
        scheduleDAG->AddPredQueued(SuccSU, Edge);
        --i;
      }
    }
  outer_loop_continue:;
  }
}

/// AddPseudoTwoAddrDeps - If two nodes share an operand and one of them uses
/// it as a def&use operand. Add a pseudo control edge from it to the other
/// node (if it won't create a cycle) so the two-address one will be scheduled
/// first (lower in the schedule). If both nodes are two-address, favor the
/// one that has a CopyToReg use (more likely to be a loop induction update).
/// If both are two-address, but one is commutable while the other is not
/// commutable, favor the one that's not commutable.
void RegReductionPQBase::AddPseudoTwoAddrDeps() {
  for (SUnit &SU : *SUnits) {
    if (!SU.isTwoAddress)
      continue;

    SDNode *Node = SU.getNode();
    if (!Node || !Node->isMachineOpcode() || SU.getNode()->getGluedNode())
      continue;

    bool isLiveOut = hasOnlyLiveOutUses(&SU);
    unsigned Opc = Node->getMachineOpcode();
    const MCInstrDesc &MCID = TII->get(Opc);
    unsigned NumRes = MCID.getNumDefs();
    unsigned NumOps = MCID.getNumOperands() - NumRes;
    for (unsigned j = 0; j != NumOps; ++j) {
      if (MCID.getOperandConstraint(j+NumRes, MCOI::TIED_TO) == -1)
        continue;
      SDNode *DU = SU.getNode()->getOperand(j).getNode();
      if (DU->getNodeId() == -1)
        continue;
      const SUnit *DUSU = &(*SUnits)[DU->getNodeId()];
      if (!DUSU)
        continue;
      for (const SDep &Succ : DUSU->Succs) {
        if (Succ.isCtrl())
          continue;
        SUnit *SuccSU = Succ.getSUnit();
        if (SuccSU == &SU)
          continue;
        // Be conservative. Ignore if nodes aren't at roughly the same
        // depth and height.
        if (SuccSU->getHeight() < SU.getHeight() &&
            (SU.getHeight() - SuccSU->getHeight()) > 1)
          continue;
        // Skip past COPY_TO_REGCLASS nodes, so that the pseudo edge
        // constrains whatever is using the copy, instead of the copy
        // itself. In the case that the copy is coalesced, this
        // preserves the intent of the pseudo two-address heurietics.
        while (SuccSU->Succs.size() == 1 &&
               SuccSU->getNode()->isMachineOpcode() &&
               SuccSU->getNode()->getMachineOpcode() ==
                 TargetOpcode::COPY_TO_REGCLASS)
          SuccSU = SuccSU->Succs.front().getSUnit();
        // Don't constrain non-instruction nodes.
        if (!SuccSU->getNode() || !SuccSU->getNode()->isMachineOpcode())
          continue;
        // Don't constrain nodes with physical register defs if the
        // predecessor can clobber them.
        if (SuccSU->hasPhysRegDefs && SU.hasPhysRegClobbers) {
          if (canClobberPhysRegDefs(SuccSU, &SU, TII, TRI))
            continue;
        }
        // Don't constrain EXTRACT_SUBREG, INSERT_SUBREG, and SUBREG_TO_REG;
        // these may be coalesced away. We want them close to their uses.
        unsigned SuccOpc = SuccSU->getNode()->getMachineOpcode();
        if (SuccOpc == TargetOpcode::EXTRACT_SUBREG ||
            SuccOpc == TargetOpcode::INSERT_SUBREG ||
            SuccOpc == TargetOpcode::SUBREG_TO_REG)
          continue;
        if (!canClobberReachingPhysRegUse(SuccSU, &SU, scheduleDAG, TII, TRI) &&
            (!canClobber(SuccSU, DUSU) ||
             (isLiveOut && !hasOnlyLiveOutUses(SuccSU)) ||
             (!SU.isCommutable && SuccSU->isCommutable)) &&
            !scheduleDAG->IsReachable(SuccSU, &SU)) {
          LLVM_DEBUG(dbgs()
                     << "    Adding a pseudo-two-addr edge from SU #"
                     << SU.NodeNum << " to SU #" << SuccSU->NodeNum << "\n");
          scheduleDAG->AddPredQueued(&SU, SDep(SuccSU, SDep::Artificial));
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

ScheduleDAGSDNodes *llvm::createBURRListDAGScheduler(SelectionDAGISel *IS,
                                                     CodeGenOptLevel OptLevel) {
  const TargetSubtargetInfo &STI = IS->MF->getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  BURegReductionPriorityQueue *PQ =
    new BURegReductionPriorityQueue(*IS->MF, false, false, TII, TRI, nullptr);
  ScheduleDAGRRList *SD = new ScheduleDAGRRList(*IS->MF, false, PQ, OptLevel);
  PQ->setScheduleDAG(SD);
  return SD;
}

ScheduleDAGSDNodes *
llvm::createSourceListDAGScheduler(SelectionDAGISel *IS,
                                   CodeGenOptLevel OptLevel) {
  const TargetSubtargetInfo &STI = IS->MF->getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  SrcRegReductionPriorityQueue *PQ =
    new SrcRegReductionPriorityQueue(*IS->MF, false, true, TII, TRI, nullptr);
  ScheduleDAGRRList *SD = new ScheduleDAGRRList(*IS->MF, false, PQ, OptLevel);
  PQ->setScheduleDAG(SD);
  return SD;
}

ScheduleDAGSDNodes *
llvm::createHybridListDAGScheduler(SelectionDAGISel *IS,
                                   CodeGenOptLevel OptLevel) {
  const TargetSubtargetInfo &STI = IS->MF->getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  const TargetLowering *TLI = IS->TLI;

  HybridBURRPriorityQueue *PQ =
    new HybridBURRPriorityQueue(*IS->MF, true, false, TII, TRI, TLI);

  ScheduleDAGRRList *SD = new ScheduleDAGRRList(*IS->MF, true, PQ, OptLevel);
  PQ->setScheduleDAG(SD);
  return SD;
}

ScheduleDAGSDNodes *llvm::createILPListDAGScheduler(SelectionDAGISel *IS,
                                                    CodeGenOptLevel OptLevel) {
  const TargetSubtargetInfo &STI = IS->MF->getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  const TargetLowering *TLI = IS->TLI;

  ILPBURRPriorityQueue *PQ =
    new ILPBURRPriorityQueue(*IS->MF, true, false, TII, TRI, TLI);
  ScheduleDAGRRList *SD = new ScheduleDAGRRList(*IS->MF, true, PQ, OptLevel);
  PQ->setScheduleDAG(SD);
  return SD;
}
