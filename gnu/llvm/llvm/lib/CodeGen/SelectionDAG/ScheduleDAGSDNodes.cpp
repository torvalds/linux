//===--- ScheduleDAGSDNodes.cpp - Implement the ScheduleDAGSDNodes class --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the ScheduleDAG class, which is a base class used by
// scheduling implementation classes.
//
//===----------------------------------------------------------------------===//

#include "ScheduleDAGSDNodes.h"
#include "InstrEmitter.h"
#include "SDNodeDbgValue.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "pre-RA-sched"

STATISTIC(LoadsClustered, "Number of loads clustered together");

// This allows the latency-based scheduler to notice high latency instructions
// without a target itinerary. The choice of number here has more to do with
// balancing scheduler heuristics than with the actual machine latency.
static cl::opt<int> HighLatencyCycles(
  "sched-high-latency-cycles", cl::Hidden, cl::init(10),
  cl::desc("Roughly estimate the number of cycles that 'long latency'"
           "instructions take for targets with no itinerary"));

ScheduleDAGSDNodes::ScheduleDAGSDNodes(MachineFunction &mf)
    : ScheduleDAG(mf), InstrItins(mf.getSubtarget().getInstrItineraryData()) {}

/// Run - perform scheduling.
///
void ScheduleDAGSDNodes::Run(SelectionDAG *dag, MachineBasicBlock *bb) {
  BB = bb;
  DAG = dag;

  // Clear the scheduler's SUnit DAG.
  ScheduleDAG::clearDAG();
  Sequence.clear();

  // Invoke the target's selection of scheduler.
  Schedule();
}

/// NewSUnit - Creates a new SUnit and return a ptr to it.
///
SUnit *ScheduleDAGSDNodes::newSUnit(SDNode *N) {
#ifndef NDEBUG
  const SUnit *Addr = nullptr;
  if (!SUnits.empty())
    Addr = &SUnits[0];
#endif
  SUnits.emplace_back(N, (unsigned)SUnits.size());
  assert((Addr == nullptr || Addr == &SUnits[0]) &&
         "SUnits std::vector reallocated on the fly!");
  SUnits.back().OrigNode = &SUnits.back();
  SUnit *SU = &SUnits.back();
  const TargetLowering &TLI = DAG->getTargetLoweringInfo();
  if (!N ||
      (N->isMachineOpcode() &&
       N->getMachineOpcode() == TargetOpcode::IMPLICIT_DEF))
    SU->SchedulingPref = Sched::None;
  else
    SU->SchedulingPref = TLI.getSchedulingPreference(N);
  return SU;
}

SUnit *ScheduleDAGSDNodes::Clone(SUnit *Old) {
  SUnit *SU = newSUnit(Old->getNode());
  SU->OrigNode = Old->OrigNode;
  SU->Latency = Old->Latency;
  SU->isVRegCycle = Old->isVRegCycle;
  SU->isCall = Old->isCall;
  SU->isCallOp = Old->isCallOp;
  SU->isTwoAddress = Old->isTwoAddress;
  SU->isCommutable = Old->isCommutable;
  SU->hasPhysRegDefs = Old->hasPhysRegDefs;
  SU->hasPhysRegClobbers = Old->hasPhysRegClobbers;
  SU->isScheduleHigh = Old->isScheduleHigh;
  SU->isScheduleLow = Old->isScheduleLow;
  SU->SchedulingPref = Old->SchedulingPref;
  Old->isCloned = true;
  return SU;
}

/// CheckForPhysRegDependency - Check if the dependency between def and use of
/// a specified operand is a physical register dependency. If so, returns the
/// register and the cost of copying the register.
static void CheckForPhysRegDependency(SDNode *Def, SDNode *User, unsigned Op,
                                      const TargetRegisterInfo *TRI,
                                      const TargetInstrInfo *TII,
                                      const TargetLowering &TLI,
                                      unsigned &PhysReg, int &Cost) {
  if (Op != 2 || User->getOpcode() != ISD::CopyToReg)
    return;

  unsigned Reg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
  if (TLI.checkForPhysRegDependency(Def, User, Op, TRI, TII, PhysReg, Cost))
    return;

  if (Register::isVirtualRegister(Reg))
    return;

  unsigned ResNo = User->getOperand(2).getResNo();
  if (Def->getOpcode() == ISD::CopyFromReg &&
      cast<RegisterSDNode>(Def->getOperand(1))->getReg() == Reg) {
    PhysReg = Reg;
  } else if (Def->isMachineOpcode()) {
    const MCInstrDesc &II = TII->get(Def->getMachineOpcode());
    if (ResNo >= II.getNumDefs() && II.hasImplicitDefOfPhysReg(Reg))
      PhysReg = Reg;
  }

  if (PhysReg != 0) {
    const TargetRegisterClass *RC =
        TRI->getMinimalPhysRegClass(Reg, Def->getSimpleValueType(ResNo));
    Cost = RC->getCopyCost();
  }
}

// Helper for AddGlue to clone node operands.
static void CloneNodeWithValues(SDNode *N, SelectionDAG *DAG, ArrayRef<EVT> VTs,
                                SDValue ExtraOper = SDValue()) {
  SmallVector<SDValue, 8> Ops(N->op_begin(), N->op_end());
  if (ExtraOper.getNode())
    Ops.push_back(ExtraOper);

  SDVTList VTList = DAG->getVTList(VTs);
  MachineSDNode *MN = dyn_cast<MachineSDNode>(N);

  // Store memory references.
  SmallVector<MachineMemOperand *, 2> MMOs;
  if (MN)
    MMOs.assign(MN->memoperands_begin(), MN->memoperands_end());

  DAG->MorphNodeTo(N, N->getOpcode(), VTList, Ops);

  // Reset the memory references
  if (MN)
    DAG->setNodeMemRefs(MN, MMOs);
}

static bool AddGlue(SDNode *N, SDValue Glue, bool AddGlue, SelectionDAG *DAG) {
  SDNode *GlueDestNode = Glue.getNode();

  // Don't add glue from a node to itself.
  if (GlueDestNode == N) return false;

  // Don't add a glue operand to something that already uses glue.
  if (GlueDestNode &&
      N->getOperand(N->getNumOperands()-1).getValueType() == MVT::Glue) {
    return false;
  }
  // Don't add glue to something that already has a glue value.
  if (N->getValueType(N->getNumValues() - 1) == MVT::Glue) return false;

  SmallVector<EVT, 4> VTs(N->values());
  if (AddGlue)
    VTs.push_back(MVT::Glue);

  CloneNodeWithValues(N, DAG, VTs, Glue);

  return true;
}

// Cleanup after unsuccessful AddGlue. Use the standard method of morphing the
// node even though simply shrinking the value list is sufficient.
static void RemoveUnusedGlue(SDNode *N, SelectionDAG *DAG) {
  assert((N->getValueType(N->getNumValues() - 1) == MVT::Glue &&
          !N->hasAnyUseOfValue(N->getNumValues() - 1)) &&
         "expected an unused glue value");

  CloneNodeWithValues(N, DAG,
                      ArrayRef(N->value_begin(), N->getNumValues() - 1));
}

/// ClusterNeighboringLoads - Force nearby loads together by "gluing" them.
/// This function finds loads of the same base and different offsets. If the
/// offsets are not far apart (target specific), it add MVT::Glue inputs and
/// outputs to ensure they are scheduled together and in order. This
/// optimization may benefit some targets by improving cache locality.
void ScheduleDAGSDNodes::ClusterNeighboringLoads(SDNode *Node) {
  SDValue Chain;
  unsigned NumOps = Node->getNumOperands();
  if (Node->getOperand(NumOps-1).getValueType() == MVT::Other)
    Chain = Node->getOperand(NumOps-1);
  if (!Chain)
    return;

  // Skip any load instruction that has a tied input. There may be an additional
  // dependency requiring a different order than by increasing offsets, and the
  // added glue may introduce a cycle.
  auto hasTiedInput = [this](const SDNode *N) {
    const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());
    for (unsigned I = 0; I != MCID.getNumOperands(); ++I) {
      if (MCID.getOperandConstraint(I, MCOI::TIED_TO) != -1)
        return true;
    }

    return false;
  };

  // Look for other loads of the same chain. Find loads that are loading from
  // the same base pointer and different offsets.
  SmallPtrSet<SDNode*, 16> Visited;
  SmallVector<int64_t, 4> Offsets;
  DenseMap<long long, SDNode*> O2SMap;  // Map from offset to SDNode.
  bool Cluster = false;
  SDNode *Base = Node;

  if (hasTiedInput(Base))
    return;

  // This algorithm requires a reasonably low use count before finding a match
  // to avoid uselessly blowing up compile time in large blocks.
  unsigned UseCount = 0;
  for (SDNode::use_iterator I = Chain->use_begin(), E = Chain->use_end();
       I != E && UseCount < 100; ++I, ++UseCount) {
    if (I.getUse().getResNo() != Chain.getResNo())
      continue;

    SDNode *User = *I;
    if (User == Node || !Visited.insert(User).second)
      continue;
    int64_t Offset1, Offset2;
    if (!TII->areLoadsFromSameBasePtr(Base, User, Offset1, Offset2) ||
        Offset1 == Offset2 ||
        hasTiedInput(User)) {
      // FIXME: Should be ok if they addresses are identical. But earlier
      // optimizations really should have eliminated one of the loads.
      continue;
    }
    if (O2SMap.insert(std::make_pair(Offset1, Base)).second)
      Offsets.push_back(Offset1);
    O2SMap.insert(std::make_pair(Offset2, User));
    Offsets.push_back(Offset2);
    if (Offset2 < Offset1)
      Base = User;
    Cluster = true;
    // Reset UseCount to allow more matches.
    UseCount = 0;
  }

  if (!Cluster)
    return;

  // Sort them in increasing order.
  llvm::sort(Offsets);

  // Check if the loads are close enough.
  SmallVector<SDNode*, 4> Loads;
  unsigned NumLoads = 0;
  int64_t BaseOff = Offsets[0];
  SDNode *BaseLoad = O2SMap[BaseOff];
  Loads.push_back(BaseLoad);
  for (unsigned i = 1, e = Offsets.size(); i != e; ++i) {
    int64_t Offset = Offsets[i];
    SDNode *Load = O2SMap[Offset];
    if (!TII->shouldScheduleLoadsNear(BaseLoad, Load, BaseOff, Offset,NumLoads))
      break; // Stop right here. Ignore loads that are further away.
    Loads.push_back(Load);
    ++NumLoads;
  }

  if (NumLoads == 0)
    return;

  // Cluster loads by adding MVT::Glue outputs and inputs. This also
  // ensure they are scheduled in order of increasing addresses.
  SDNode *Lead = Loads[0];
  SDValue InGlue;
  if (AddGlue(Lead, InGlue, true, DAG))
    InGlue = SDValue(Lead, Lead->getNumValues() - 1);
  for (unsigned I = 1, E = Loads.size(); I != E; ++I) {
    bool OutGlue = I < E - 1;
    SDNode *Load = Loads[I];

    // If AddGlue fails, we could leave an unsused glue value. This should not
    // cause any
    if (AddGlue(Load, InGlue, OutGlue, DAG)) {
      if (OutGlue)
        InGlue = SDValue(Load, Load->getNumValues() - 1);

      ++LoadsClustered;
    }
    else if (!OutGlue && InGlue.getNode())
      RemoveUnusedGlue(InGlue.getNode(), DAG);
  }
}

/// ClusterNodes - Cluster certain nodes which should be scheduled together.
///
void ScheduleDAGSDNodes::ClusterNodes() {
  for (SDNode &NI : DAG->allnodes()) {
    SDNode *Node = &NI;
    if (!Node || !Node->isMachineOpcode())
      continue;

    unsigned Opc = Node->getMachineOpcode();
    const MCInstrDesc &MCID = TII->get(Opc);
    if (MCID.mayLoad())
      // Cluster loads from "near" addresses into combined SUnits.
      ClusterNeighboringLoads(Node);
  }
}

void ScheduleDAGSDNodes::BuildSchedUnits() {
  // During scheduling, the NodeId field of SDNode is used to map SDNodes
  // to their associated SUnits by holding SUnits table indices. A value
  // of -1 means the SDNode does not yet have an associated SUnit.
  unsigned NumNodes = 0;
  for (SDNode &NI : DAG->allnodes()) {
    NI.setNodeId(-1);
    ++NumNodes;
  }

  // Reserve entries in the vector for each of the SUnits we are creating.  This
  // ensure that reallocation of the vector won't happen, so SUnit*'s won't get
  // invalidated.
  // FIXME: Multiply by 2 because we may clone nodes during scheduling.
  // This is a temporary workaround.
  SUnits.reserve(NumNodes * 2);

  // Add all nodes in depth first order.
  SmallVector<SDNode*, 64> Worklist;
  SmallPtrSet<SDNode*, 32> Visited;
  Worklist.push_back(DAG->getRoot().getNode());
  Visited.insert(DAG->getRoot().getNode());

  SmallVector<SUnit*, 8> CallSUnits;
  while (!Worklist.empty()) {
    SDNode *NI = Worklist.pop_back_val();

    // Add all operands to the worklist unless they've already been added.
    for (const SDValue &Op : NI->op_values())
      if (Visited.insert(Op.getNode()).second)
        Worklist.push_back(Op.getNode());

    if (isPassiveNode(NI))  // Leaf node, e.g. a TargetImmediate.
      continue;

    // If this node has already been processed, stop now.
    if (NI->getNodeId() != -1) continue;

    SUnit *NodeSUnit = newSUnit(NI);

    // See if anything is glued to this node, if so, add them to glued
    // nodes.  Nodes can have at most one glue input and one glue output.  Glue
    // is required to be the last operand and result of a node.

    // Scan up to find glued preds.
    SDNode *N = NI;
    while (N->getNumOperands() &&
           N->getOperand(N->getNumOperands()-1).getValueType() == MVT::Glue) {
      N = N->getOperand(N->getNumOperands()-1).getNode();
      assert(N->getNodeId() == -1 && "Node already inserted!");
      N->setNodeId(NodeSUnit->NodeNum);
      if (N->isMachineOpcode() && TII->get(N->getMachineOpcode()).isCall())
        NodeSUnit->isCall = true;
    }

    // Scan down to find any glued succs.
    N = NI;
    while (N->getValueType(N->getNumValues()-1) == MVT::Glue) {
      SDValue GlueVal(N, N->getNumValues()-1);

      // There are either zero or one users of the Glue result.
      bool HasGlueUse = false;
      for (SDNode *U : N->uses())
        if (GlueVal.isOperandOf(U)) {
          HasGlueUse = true;
          assert(N->getNodeId() == -1 && "Node already inserted!");
          N->setNodeId(NodeSUnit->NodeNum);
          N = U;
          if (N->isMachineOpcode() && TII->get(N->getMachineOpcode()).isCall())
            NodeSUnit->isCall = true;
          break;
        }
      if (!HasGlueUse) break;
    }

    if (NodeSUnit->isCall)
      CallSUnits.push_back(NodeSUnit);

    // Schedule zero-latency TokenFactor below any nodes that may increase the
    // schedule height. Otherwise, ancestors of the TokenFactor may appear to
    // have false stalls.
    if (NI->getOpcode() == ISD::TokenFactor)
      NodeSUnit->isScheduleLow = true;

    // If there are glue operands involved, N is now the bottom-most node
    // of the sequence of nodes that are glued together.
    // Update the SUnit.
    NodeSUnit->setNode(N);
    assert(N->getNodeId() == -1 && "Node already inserted!");
    N->setNodeId(NodeSUnit->NodeNum);

    // Compute NumRegDefsLeft. This must be done before AddSchedEdges.
    InitNumRegDefsLeft(NodeSUnit);

    // Assign the Latency field of NodeSUnit using target-provided information.
    computeLatency(NodeSUnit);
  }

  // Find all call operands.
  while (!CallSUnits.empty()) {
    SUnit *SU = CallSUnits.pop_back_val();
    for (const SDNode *SUNode = SU->getNode(); SUNode;
         SUNode = SUNode->getGluedNode()) {
      if (SUNode->getOpcode() != ISD::CopyToReg)
        continue;
      SDNode *SrcN = SUNode->getOperand(2).getNode();
      if (isPassiveNode(SrcN)) continue;   // Not scheduled.
      SUnit *SrcSU = &SUnits[SrcN->getNodeId()];
      SrcSU->isCallOp = true;
    }
  }
}

void ScheduleDAGSDNodes::AddSchedEdges() {
  const TargetSubtargetInfo &ST = MF.getSubtarget();

  // Check to see if the scheduler cares about latencies.
  bool UnitLatencies = forceUnitLatencies();

  // Pass 2: add the preds, succs, etc.
  for (SUnit &SU : SUnits) {
    SDNode *MainNode = SU.getNode();

    if (MainNode->isMachineOpcode()) {
      unsigned Opc = MainNode->getMachineOpcode();
      const MCInstrDesc &MCID = TII->get(Opc);
      for (unsigned i = 0; i != MCID.getNumOperands(); ++i) {
        if (MCID.getOperandConstraint(i, MCOI::TIED_TO) != -1) {
          SU.isTwoAddress = true;
          break;
        }
      }
      if (MCID.isCommutable())
        SU.isCommutable = true;
    }

    // Find all predecessors and successors of the group.
    for (SDNode *N = SU.getNode(); N; N = N->getGluedNode()) {
      if (N->isMachineOpcode() &&
          !TII->get(N->getMachineOpcode()).implicit_defs().empty()) {
        SU.hasPhysRegClobbers = true;
        unsigned NumUsed = InstrEmitter::CountResults(N);
        while (NumUsed != 0 && !N->hasAnyUseOfValue(NumUsed - 1))
          --NumUsed;    // Skip over unused values at the end.
        if (NumUsed > TII->get(N->getMachineOpcode()).getNumDefs())
          SU.hasPhysRegDefs = true;
      }

      for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
        SDNode *OpN = N->getOperand(i).getNode();
        unsigned DefIdx = N->getOperand(i).getResNo();
        if (isPassiveNode(OpN)) continue;   // Not scheduled.
        SUnit *OpSU = &SUnits[OpN->getNodeId()];
        assert(OpSU && "Node has no SUnit!");
        if (OpSU == &SU)
          continue; // In the same group.

        EVT OpVT = N->getOperand(i).getValueType();
        assert(OpVT != MVT::Glue && "Glued nodes should be in same sunit!");
        bool isChain = OpVT == MVT::Other;

        unsigned PhysReg = 0;
        int Cost = 1;
        // Determine if this is a physical register dependency.
        const TargetLowering &TLI = DAG->getTargetLoweringInfo();
        CheckForPhysRegDependency(OpN, N, i, TRI, TII, TLI, PhysReg, Cost);
        assert((PhysReg == 0 || !isChain) &&
               "Chain dependence via physreg data?");
        // FIXME: See ScheduleDAGSDNodes::EmitCopyFromReg. For now, scheduler
        // emits a copy from the physical register to a virtual register unless
        // it requires a cross class copy (cost < 0). That means we are only
        // treating "expensive to copy" register dependency as physical register
        // dependency. This may change in the future though.
        if (Cost >= 0 && !StressSched)
          PhysReg = 0;

        // If this is a ctrl dep, latency is 1.
        unsigned OpLatency = isChain ? 1 : OpSU->Latency;
        // Special-case TokenFactor chains as zero-latency.
        if(isChain && OpN->getOpcode() == ISD::TokenFactor)
          OpLatency = 0;

        SDep Dep = isChain ? SDep(OpSU, SDep::Barrier)
          : SDep(OpSU, SDep::Data, PhysReg);
        Dep.setLatency(OpLatency);
        if (!isChain && !UnitLatencies) {
          computeOperandLatency(OpN, N, i, Dep);
          ST.adjustSchedDependency(OpSU, DefIdx, &SU, i, Dep, nullptr);
        }

        if (!SU.addPred(Dep) && !Dep.isCtrl() && OpSU->NumRegDefsLeft > 1) {
          // Multiple register uses are combined in the same SUnit. For example,
          // we could have a set of glued nodes with all their defs consumed by
          // another set of glued nodes. Register pressure tracking sees this as
          // a single use, so to keep pressure balanced we reduce the defs.
          //
          // We can't tell (without more book-keeping) if this results from
          // glued nodes or duplicate operands. As long as we don't reduce
          // NumRegDefsLeft to zero, we handle the common cases well.
          --OpSU->NumRegDefsLeft;
        }
      }
    }
  }
}

/// BuildSchedGraph - Build the SUnit graph from the selection dag that we
/// are input.  This SUnit graph is similar to the SelectionDAG, but
/// excludes nodes that aren't interesting to scheduling, and represents
/// glued together nodes with a single SUnit.
void ScheduleDAGSDNodes::BuildSchedGraph(AAResults *AA) {
  // Cluster certain nodes which should be scheduled together.
  ClusterNodes();
  // Populate the SUnits array.
  BuildSchedUnits();
  // Compute all the scheduling dependencies between nodes.
  AddSchedEdges();
}

// Initialize NumNodeDefs for the current Node's opcode.
void ScheduleDAGSDNodes::RegDefIter::InitNodeNumDefs() {
  // Check for phys reg copy.
  if (!Node)
    return;

  if (!Node->isMachineOpcode()) {
    if (Node->getOpcode() == ISD::CopyFromReg)
      NodeNumDefs = 1;
    else
      NodeNumDefs = 0;
    return;
  }
  unsigned POpc = Node->getMachineOpcode();
  if (POpc == TargetOpcode::IMPLICIT_DEF) {
    // No register need be allocated for this.
    NodeNumDefs = 0;
    return;
  }
  if (POpc == TargetOpcode::PATCHPOINT &&
      Node->getValueType(0) == MVT::Other) {
    // PATCHPOINT is defined to have one result, but it might really have none
    // if we're not using CallingConv::AnyReg. Don't mistake the chain for a
    // real definition.
    NodeNumDefs = 0;
    return;
  }
  unsigned NRegDefs = SchedDAG->TII->get(Node->getMachineOpcode()).getNumDefs();
  // Some instructions define regs that are not represented in the selection DAG
  // (e.g. unused flags). See tMOVi8. Make sure we don't access past NumValues.
  NodeNumDefs = std::min(Node->getNumValues(), NRegDefs);
  DefIdx = 0;
}

// Construct a RegDefIter for this SUnit and find the first valid value.
ScheduleDAGSDNodes::RegDefIter::RegDefIter(const SUnit *SU,
                                           const ScheduleDAGSDNodes *SD)
    : SchedDAG(SD), Node(SU->getNode()) {
  InitNodeNumDefs();
  Advance();
}

// Advance to the next valid value defined by the SUnit.
void ScheduleDAGSDNodes::RegDefIter::Advance() {
  for (;Node;) { // Visit all glued nodes.
    for (;DefIdx < NodeNumDefs; ++DefIdx) {
      if (!Node->hasAnyUseOfValue(DefIdx))
        continue;
      ValueType = Node->getSimpleValueType(DefIdx);
      ++DefIdx;
      return; // Found a normal regdef.
    }
    Node = Node->getGluedNode();
    if (!Node) {
      return; // No values left to visit.
    }
    InitNodeNumDefs();
  }
}

void ScheduleDAGSDNodes::InitNumRegDefsLeft(SUnit *SU) {
  assert(SU->NumRegDefsLeft == 0 && "expect a new node");
  for (RegDefIter I(SU, this); I.IsValid(); I.Advance()) {
    assert(SU->NumRegDefsLeft < USHRT_MAX && "overflow is ok but unexpected");
    ++SU->NumRegDefsLeft;
  }
}

void ScheduleDAGSDNodes::computeLatency(SUnit *SU) {
  SDNode *N = SU->getNode();

  // TokenFactor operands are considered zero latency, and some schedulers
  // (e.g. Top-Down list) may rely on the fact that operand latency is nonzero
  // whenever node latency is nonzero.
  if (N && N->getOpcode() == ISD::TokenFactor) {
    SU->Latency = 0;
    return;
  }

  // Check to see if the scheduler cares about latencies.
  if (forceUnitLatencies()) {
    SU->Latency = 1;
    return;
  }

  if (!InstrItins || InstrItins->isEmpty()) {
    if (N && N->isMachineOpcode() &&
        TII->isHighLatencyDef(N->getMachineOpcode()))
      SU->Latency = HighLatencyCycles;
    else
      SU->Latency = 1;
    return;
  }

  // Compute the latency for the node.  We use the sum of the latencies for
  // all nodes glued together into this SUnit.
  SU->Latency = 0;
  for (SDNode *N = SU->getNode(); N; N = N->getGluedNode())
    if (N->isMachineOpcode())
      SU->Latency += TII->getInstrLatency(InstrItins, N);
}

void ScheduleDAGSDNodes::computeOperandLatency(SDNode *Def, SDNode *Use,
                                               unsigned OpIdx, SDep& dep) const{
  // Check to see if the scheduler cares about latencies.
  if (forceUnitLatencies())
    return;

  if (dep.getKind() != SDep::Data)
    return;

  unsigned DefIdx = Use->getOperand(OpIdx).getResNo();
  if (Use->isMachineOpcode())
    // Adjust the use operand index by num of defs.
    OpIdx += TII->get(Use->getMachineOpcode()).getNumDefs();
  std::optional<unsigned> Latency =
      TII->getOperandLatency(InstrItins, Def, DefIdx, Use, OpIdx);
  if (Latency > 1U && Use->getOpcode() == ISD::CopyToReg &&
      !BB->succ_empty()) {
    unsigned Reg = cast<RegisterSDNode>(Use->getOperand(1))->getReg();
    if (Register::isVirtualRegister(Reg))
      // This copy is a liveout value. It is likely coalesced, so reduce the
      // latency so not to penalize the def.
      // FIXME: need target specific adjustment here?
      Latency = *Latency - 1;
  }
  if (Latency)
    dep.setLatency(*Latency);
}

void ScheduleDAGSDNodes::dumpNode(const SUnit &SU) const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  dumpNodeName(SU);
  dbgs() << ": ";

  if (!SU.getNode()) {
    dbgs() << "PHYS REG COPY\n";
    return;
  }

  SU.getNode()->dump(DAG);
  dbgs() << "\n";
  SmallVector<SDNode *, 4> GluedNodes;
  for (SDNode *N = SU.getNode()->getGluedNode(); N; N = N->getGluedNode())
    GluedNodes.push_back(N);
  while (!GluedNodes.empty()) {
    dbgs() << "    ";
    GluedNodes.back()->dump(DAG);
    dbgs() << "\n";
    GluedNodes.pop_back();
  }
#endif
}

void ScheduleDAGSDNodes::dump() const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  if (EntrySU.getNode() != nullptr)
    dumpNodeAll(EntrySU);
  for (const SUnit &SU : SUnits)
    dumpNodeAll(SU);
  if (ExitSU.getNode() != nullptr)
    dumpNodeAll(ExitSU);
#endif
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ScheduleDAGSDNodes::dumpSchedule() const {
  for (const SUnit *SU : Sequence) {
    if (SU)
      dumpNode(*SU);
    else
      dbgs() << "**** NOOP ****\n";
  }
}
#endif

#ifndef NDEBUG
/// VerifyScheduledSequence - Verify that all SUnits were scheduled and that
/// their state is consistent with the nodes listed in Sequence.
///
void ScheduleDAGSDNodes::VerifyScheduledSequence(bool isBottomUp) {
  unsigned ScheduledNodes = ScheduleDAG::VerifyScheduledDAG(isBottomUp);
  unsigned Noops = llvm::count(Sequence, nullptr);
  assert(Sequence.size() - Noops == ScheduledNodes &&
         "The number of nodes scheduled doesn't match the expected number!");
}
#endif // NDEBUG

/// ProcessSDDbgValues - Process SDDbgValues associated with this node.
static void
ProcessSDDbgValues(SDNode *N, SelectionDAG *DAG, InstrEmitter &Emitter,
                   SmallVectorImpl<std::pair<unsigned, MachineInstr*> > &Orders,
                   DenseMap<SDValue, Register> &VRBaseMap, unsigned Order) {
  if (!N->getHasDebugValue())
    return;

  /// Returns true if \p DV has any VReg operand locations which don't exist in
  /// VRBaseMap.
  auto HasUnknownVReg = [&VRBaseMap](SDDbgValue *DV) {
    for (const SDDbgOperand &L : DV->getLocationOps()) {
      if (L.getKind() == SDDbgOperand::SDNODE &&
          VRBaseMap.count({L.getSDNode(), L.getResNo()}) == 0)
        return true;
    }
    return false;
  };

  // Opportunistically insert immediate dbg_value uses, i.e. those with the same
  // source order number as N.
  MachineBasicBlock *BB = Emitter.getBlock();
  MachineBasicBlock::iterator InsertPos = Emitter.getInsertPos();
  for (auto *DV : DAG->GetDbgValues(N)) {
    if (DV->isEmitted())
      continue;
    unsigned DVOrder = DV->getOrder();
    if (Order != 0 && DVOrder != Order)
      continue;
    // If DV has any VReg location operands which haven't been mapped then
    // either that node is no longer available or we just haven't visited the
    // node yet. In the former case we should emit an undef dbg_value, but we
    // can do it later. And for the latter we'll want to wait until all
    // dependent nodes have been visited.
    if (!DV->isInvalidated() && HasUnknownVReg(DV))
      continue;
    MachineInstr *DbgMI = Emitter.EmitDbgValue(DV, VRBaseMap);
    if (!DbgMI)
      continue;
    Orders.push_back({DVOrder, DbgMI});
    BB->insert(InsertPos, DbgMI);
  }
}

// ProcessSourceNode - Process nodes with source order numbers. These are added
// to a vector which EmitSchedule uses to determine how to insert dbg_value
// instructions in the right order.
static void
ProcessSourceNode(SDNode *N, SelectionDAG *DAG, InstrEmitter &Emitter,
                  DenseMap<SDValue, Register> &VRBaseMap,
                  SmallVectorImpl<std::pair<unsigned, MachineInstr *>> &Orders,
                  SmallSet<Register, 8> &Seen, MachineInstr *NewInsn) {
  unsigned Order = N->getIROrder();
  if (!Order || Seen.count(Order)) {
    // Process any valid SDDbgValues even if node does not have any order
    // assigned.
    ProcessSDDbgValues(N, DAG, Emitter, Orders, VRBaseMap, 0);
    return;
  }

  // If a new instruction was generated for this Order number, record it.
  // Otherwise, leave this order number unseen: we will either find later
  // instructions for it, or leave it unseen if there were no instructions at
  // all.
  if (NewInsn) {
    Seen.insert(Order);
    Orders.push_back({Order, NewInsn});
  }

  // Even if no instruction was generated, a Value may have become defined via
  // earlier nodes. Try to process them now.
  ProcessSDDbgValues(N, DAG, Emitter, Orders, VRBaseMap, Order);
}

void ScheduleDAGSDNodes::
EmitPhysRegCopy(SUnit *SU, DenseMap<SUnit*, Register> &VRBaseMap,
                MachineBasicBlock::iterator InsertPos) {
  for (const SDep &Pred : SU->Preds) {
    if (Pred.isCtrl())
      continue; // ignore chain preds
    if (Pred.getSUnit()->CopyDstRC) {
      // Copy to physical register.
      DenseMap<SUnit *, Register>::iterator VRI =
          VRBaseMap.find(Pred.getSUnit());
      assert(VRI != VRBaseMap.end() && "Node emitted out of order - late");
      // Find the destination physical register.
      Register Reg;
      for (const SDep &Succ : SU->Succs) {
        if (Succ.isCtrl())
          continue; // ignore chain preds
        if (Succ.getReg()) {
          Reg = Succ.getReg();
          break;
        }
      }
      BuildMI(*BB, InsertPos, DebugLoc(), TII->get(TargetOpcode::COPY), Reg)
        .addReg(VRI->second);
    } else {
      // Copy from physical register.
      assert(Pred.getReg() && "Unknown physical register!");
      Register VRBase = MRI.createVirtualRegister(SU->CopyDstRC);
      bool isNew = VRBaseMap.insert(std::make_pair(SU, VRBase)).second;
      (void)isNew; // Silence compiler warning.
      assert(isNew && "Node emitted out of order - early");
      BuildMI(*BB, InsertPos, DebugLoc(), TII->get(TargetOpcode::COPY), VRBase)
          .addReg(Pred.getReg());
    }
    break;
  }
}

/// EmitSchedule - Emit the machine code in scheduled order. Return the new
/// InsertPos and MachineBasicBlock that contains this insertion
/// point. ScheduleDAGSDNodes holds a BB pointer for convenience, but this does
/// not necessarily refer to returned BB. The emitter may split blocks.
MachineBasicBlock *ScheduleDAGSDNodes::
EmitSchedule(MachineBasicBlock::iterator &InsertPos) {
  InstrEmitter Emitter(DAG->getTarget(), BB, InsertPos);
  DenseMap<SDValue, Register> VRBaseMap;
  DenseMap<SUnit*, Register> CopyVRBaseMap;
  SmallVector<std::pair<unsigned, MachineInstr*>, 32> Orders;
  SmallSet<Register, 8> Seen;
  bool HasDbg = DAG->hasDebugValues();

  // Emit a node, and determine where its first instruction is for debuginfo.
  // Zero, one, or multiple instructions can be created when emitting a node.
  auto EmitNode =
      [&](SDNode *Node, bool IsClone, bool IsCloned,
          DenseMap<SDValue, Register> &VRBaseMap) -> MachineInstr * {
    // Fetch instruction prior to this, or end() if nonexistant.
    auto GetPrevInsn = [&](MachineBasicBlock::iterator I) {
      if (I == BB->begin())
        return BB->end();
      else
        return std::prev(Emitter.getInsertPos());
    };

    MachineBasicBlock::iterator Before = GetPrevInsn(Emitter.getInsertPos());
    Emitter.EmitNode(Node, IsClone, IsCloned, VRBaseMap);
    MachineBasicBlock::iterator After = GetPrevInsn(Emitter.getInsertPos());

    // If the iterator did not change, no instructions were inserted.
    if (Before == After)
      return nullptr;

    MachineInstr *MI;
    if (Before == BB->end()) {
      // There were no prior instructions; the new ones must start at the
      // beginning of the block.
      MI = &Emitter.getBlock()->instr_front();
    } else {
      // Return first instruction after the pre-existing instructions.
      MI = &*std::next(Before);
    }

    if (MI->isCandidateForCallSiteEntry() &&
        DAG->getTarget().Options.EmitCallSiteInfo) {
      MF.addCallSiteInfo(MI, DAG->getCallSiteInfo(Node));
    }

    if (DAG->getNoMergeSiteInfo(Node)) {
      MI->setFlag(MachineInstr::MIFlag::NoMerge);
    }

    if (MDNode *MD = DAG->getPCSections(Node))
      MI->setPCSections(MF, MD);

    // Set MMRAs on _all_ added instructions.
    if (MDNode *MMRA = DAG->getMMRAMetadata(Node)) {
      for (MachineBasicBlock::iterator It = MI->getIterator(),
                                       End = std::next(After);
           It != End; ++It)
        It->setMMRAMetadata(MF, MMRA);
    }

    return MI;
  };

  // If this is the first BB, emit byval parameter dbg_value's.
  if (HasDbg && BB->getParent()->begin() == MachineFunction::iterator(BB)) {
    SDDbgInfo::DbgIterator PDI = DAG->ByvalParmDbgBegin();
    SDDbgInfo::DbgIterator PDE = DAG->ByvalParmDbgEnd();
    for (; PDI != PDE; ++PDI) {
      MachineInstr *DbgMI= Emitter.EmitDbgValue(*PDI, VRBaseMap);
      if (DbgMI) {
        BB->insert(InsertPos, DbgMI);
        // We re-emit the dbg_value closer to its use, too, after instructions
        // are emitted to the BB.
        (*PDI)->clearIsEmitted();
      }
    }
  }

  for (SUnit *SU : Sequence) {
    if (!SU) {
      // Null SUnit* is a noop.
      TII->insertNoop(*Emitter.getBlock(), InsertPos);
      continue;
    }

    // For pre-regalloc scheduling, create instructions corresponding to the
    // SDNode and any glued SDNodes and append them to the block.
    if (!SU->getNode()) {
      // Emit a copy.
      EmitPhysRegCopy(SU, CopyVRBaseMap, InsertPos);
      continue;
    }

    SmallVector<SDNode *, 4> GluedNodes;
    for (SDNode *N = SU->getNode()->getGluedNode(); N; N = N->getGluedNode())
      GluedNodes.push_back(N);
    while (!GluedNodes.empty()) {
      SDNode *N = GluedNodes.back();
      auto NewInsn = EmitNode(N, SU->OrigNode != SU, SU->isCloned, VRBaseMap);
      // Remember the source order of the inserted instruction.
      if (HasDbg)
        ProcessSourceNode(N, DAG, Emitter, VRBaseMap, Orders, Seen, NewInsn);

      if (MDNode *MD = DAG->getHeapAllocSite(N))
        if (NewInsn && NewInsn->isCall())
          NewInsn->setHeapAllocMarker(MF, MD);

      GluedNodes.pop_back();
    }
    auto NewInsn =
        EmitNode(SU->getNode(), SU->OrigNode != SU, SU->isCloned, VRBaseMap);
    // Remember the source order of the inserted instruction.
    if (HasDbg)
      ProcessSourceNode(SU->getNode(), DAG, Emitter, VRBaseMap, Orders, Seen,
                        NewInsn);

    if (MDNode *MD = DAG->getHeapAllocSite(SU->getNode())) {
      if (NewInsn && NewInsn->isCall())
        NewInsn->setHeapAllocMarker(MF, MD);
    }
  }

  // Insert all the dbg_values which have not already been inserted in source
  // order sequence.
  if (HasDbg) {
    MachineBasicBlock::iterator BBBegin = BB->getFirstNonPHI();

    // Sort the source order instructions and use the order to insert debug
    // values. Use stable_sort so that DBG_VALUEs are inserted in the same order
    // regardless of the host's implementation fo std::sort.
    llvm::stable_sort(Orders, less_first());
    std::stable_sort(DAG->DbgBegin(), DAG->DbgEnd(),
                     [](const SDDbgValue *LHS, const SDDbgValue *RHS) {
                       return LHS->getOrder() < RHS->getOrder();
                     });

    SDDbgInfo::DbgIterator DI = DAG->DbgBegin();
    SDDbgInfo::DbgIterator DE = DAG->DbgEnd();
    // Now emit the rest according to source order.
    unsigned LastOrder = 0;
    for (unsigned i = 0, e = Orders.size(); i != e && DI != DE; ++i) {
      unsigned Order = Orders[i].first;
      MachineInstr *MI = Orders[i].second;
      // Insert all SDDbgValue's whose order(s) are before "Order".
      assert(MI);
      for (; DI != DE; ++DI) {
        if ((*DI)->getOrder() < LastOrder || (*DI)->getOrder() >= Order)
          break;
        if ((*DI)->isEmitted())
          continue;

        MachineInstr *DbgMI = Emitter.EmitDbgValue(*DI, VRBaseMap);
        if (DbgMI) {
          if (!LastOrder)
            // Insert to start of the BB (after PHIs).
            BB->insert(BBBegin, DbgMI);
          else {
            // Insert at the instruction, which may be in a different
            // block, if the block was split by a custom inserter.
            MachineBasicBlock::iterator Pos = MI;
            MI->getParent()->insert(Pos, DbgMI);
          }
        }
      }
      LastOrder = Order;
    }
    // Add trailing DbgValue's before the terminator. FIXME: May want to add
    // some of them before one or more conditional branches?
    SmallVector<MachineInstr*, 8> DbgMIs;
    for (; DI != DE; ++DI) {
      if ((*DI)->isEmitted())
        continue;
      assert((*DI)->getOrder() >= LastOrder &&
             "emitting DBG_VALUE out of order");
      if (MachineInstr *DbgMI = Emitter.EmitDbgValue(*DI, VRBaseMap))
        DbgMIs.push_back(DbgMI);
    }

    MachineBasicBlock *InsertBB = Emitter.getBlock();
    MachineBasicBlock::iterator Pos = InsertBB->getFirstTerminator();
    InsertBB->insert(Pos, DbgMIs.begin(), DbgMIs.end());

    SDDbgInfo::DbgLabelIterator DLI = DAG->DbgLabelBegin();
    SDDbgInfo::DbgLabelIterator DLE = DAG->DbgLabelEnd();
    // Now emit the rest according to source order.
    LastOrder = 0;
    for (const auto &InstrOrder : Orders) {
      unsigned Order = InstrOrder.first;
      MachineInstr *MI = InstrOrder.second;
      if (!MI)
        continue;

      // Insert all SDDbgLabel's whose order(s) are before "Order".
      for (; DLI != DLE &&
             (*DLI)->getOrder() >= LastOrder && (*DLI)->getOrder() < Order;
             ++DLI) {
        MachineInstr *DbgMI = Emitter.EmitDbgLabel(*DLI);
        if (DbgMI) {
          if (!LastOrder)
            // Insert to start of the BB (after PHIs).
            BB->insert(BBBegin, DbgMI);
          else {
            // Insert at the instruction, which may be in a different
            // block, if the block was split by a custom inserter.
            MachineBasicBlock::iterator Pos = MI;
            MI->getParent()->insert(Pos, DbgMI);
          }
        }
      }
      if (DLI == DLE)
        break;

      LastOrder = Order;
    }
  }

  InsertPos = Emitter.getInsertPos();
  // In some cases, DBG_VALUEs might be inserted after the first terminator,
  // which results in an invalid MBB. If that happens, move the DBG_VALUEs
  // before the first terminator.
  MachineBasicBlock *InsertBB = Emitter.getBlock();
  auto FirstTerm = InsertBB->getFirstTerminator();
  if (FirstTerm != InsertBB->end()) {
    assert(!FirstTerm->isDebugValue() &&
           "first terminator cannot be a debug value");
    for (MachineInstr &MI : make_early_inc_range(
             make_range(std::next(FirstTerm), InsertBB->end()))) {
      // Only scan up to insertion point.
      if (&MI == InsertPos)
        break;

      if (!MI.isDebugValue())
        continue;

      // The DBG_VALUE was referencing a value produced by a terminator. By
      // moving the DBG_VALUE, the referenced value also needs invalidating.
      MI.getOperand(0).ChangeToRegister(0, false);
      MI.moveBefore(&*FirstTerm);
    }
  }
  return InsertBB;
}

/// Return the basic block label.
std::string ScheduleDAGSDNodes::getDAGName() const {
  return "sunit-dag." + BB->getFullName();
}
