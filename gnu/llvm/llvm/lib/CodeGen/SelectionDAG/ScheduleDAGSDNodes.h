//===---- ScheduleDAGSDNodes.h - SDNode Scheduling --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleDAGSDNodes class, which implements
// scheduling for an SDNode-based dependency graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_SCHEDULEDAGSDNODES_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_SCHEDULEDAGSDNODES_H

#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <string>
#include <vector>

namespace llvm {

class AAResults;
class InstrItineraryData;

  /// ScheduleDAGSDNodes - A ScheduleDAG for scheduling SDNode-based DAGs.
  ///
  /// Edges between SUnits are initially based on edges in the SelectionDAG,
  /// and additional edges can be added by the schedulers as heuristics.
  /// SDNodes such as Constants, Registers, and a few others that are not
  /// interesting to schedulers are not allocated SUnits.
  ///
  /// SDNodes with MVT::Glue operands are grouped along with the flagged
  /// nodes into a single SUnit so that they are scheduled together.
  ///
  /// SDNode-based scheduling graphs do not use SDep::Anti or SDep::Output
  /// edges.  Physical register dependence information is not carried in
  /// the DAG and must be handled explicitly by schedulers.
  ///
  class ScheduleDAGSDNodes : public ScheduleDAG {
  public:
    MachineBasicBlock *BB = nullptr;
    SelectionDAG *DAG = nullptr; // DAG of the current basic block
    const InstrItineraryData *InstrItins;

    /// The schedule. Null SUnit*'s represent noop instructions.
    std::vector<SUnit*> Sequence;

    explicit ScheduleDAGSDNodes(MachineFunction &mf);

    ~ScheduleDAGSDNodes() override = default;

    /// Run - perform scheduling.
    ///
    void Run(SelectionDAG *dag, MachineBasicBlock *bb);

    /// isPassiveNode - Return true if the node is a non-scheduled leaf.
    ///
    static bool isPassiveNode(SDNode *Node) {
      if (isa<ConstantSDNode>(Node))       return true;
      if (isa<ConstantFPSDNode>(Node))     return true;
      if (isa<RegisterSDNode>(Node))       return true;
      if (isa<RegisterMaskSDNode>(Node))   return true;
      if (isa<GlobalAddressSDNode>(Node))  return true;
      if (isa<BasicBlockSDNode>(Node))     return true;
      if (isa<FrameIndexSDNode>(Node))     return true;
      if (isa<ConstantPoolSDNode>(Node))   return true;
      if (isa<TargetIndexSDNode>(Node))    return true;
      if (isa<JumpTableSDNode>(Node))      return true;
      if (isa<ExternalSymbolSDNode>(Node)) return true;
      if (isa<MCSymbolSDNode>(Node))       return true;
      if (isa<BlockAddressSDNode>(Node))   return true;
      if (Node->getOpcode() == ISD::EntryToken ||
          isa<MDNodeSDNode>(Node)) return true;
      return false;
    }

    /// NewSUnit - Creates a new SUnit and return a ptr to it.
    ///
    SUnit *newSUnit(SDNode *N);

    /// Clone - Creates a clone of the specified SUnit. It does not copy the
    /// predecessors / successors info nor the temporary scheduling states.
    ///
    SUnit *Clone(SUnit *Old);

    /// BuildSchedGraph - Build the SUnit graph from the selection dag that we
    /// are input.  This SUnit graph is similar to the SelectionDAG, but
    /// excludes nodes that aren't interesting to scheduling, and represents
    /// flagged together nodes with a single SUnit.
    void BuildSchedGraph(AAResults *AA);

    /// InitNumRegDefsLeft - Determine the # of regs defined by this node.
    ///
    void InitNumRegDefsLeft(SUnit *SU);

    /// computeLatency - Compute node latency.
    ///
    virtual void computeLatency(SUnit *SU);

    virtual void computeOperandLatency(SDNode *Def, SDNode *Use,
                                       unsigned OpIdx, SDep& dep) const;

    /// Schedule - Order nodes according to selected style, filling
    /// in the Sequence member.
    ///
    virtual void Schedule() = 0;

    /// VerifyScheduledSequence - Verify that all SUnits are scheduled and
    /// consistent with the Sequence of scheduled instructions.
    void VerifyScheduledSequence(bool isBottomUp);

    /// EmitSchedule - Insert MachineInstrs into the MachineBasicBlock
    /// according to the order specified in Sequence.
    ///
    virtual MachineBasicBlock*
    EmitSchedule(MachineBasicBlock::iterator &InsertPos);

    void dumpNode(const SUnit &SU) const override;
    void dump() const override;
    void dumpSchedule() const;

    std::string getGraphNodeLabel(const SUnit *SU) const override;

    std::string getDAGName() const override;

    virtual void getCustomGraphFeatures(GraphWriter<ScheduleDAG*> &GW) const;

    /// RegDefIter - In place iteration over the values defined by an
    /// SUnit. This does not need copies of the iterator or any other STLisms.
    /// The iterator creates itself, rather than being provided by the SchedDAG.
    class RegDefIter {
      const ScheduleDAGSDNodes *SchedDAG;
      const SDNode *Node;
      unsigned DefIdx = 0;
      unsigned NodeNumDefs = 0;
      MVT ValueType;

    public:
      RegDefIter(const SUnit *SU, const ScheduleDAGSDNodes *SD);

      bool IsValid() const { return Node != nullptr; }

      MVT GetValue() const {
        assert(IsValid() && "bad iterator");
        return ValueType;
      }

      const SDNode *GetNode() const {
        return Node;
      }

      unsigned GetIdx() const {
        return DefIdx-1;
      }

      void Advance();

    private:
      void InitNodeNumDefs();
    };

  protected:
    /// ForceUnitLatencies - Return true if all scheduling edges should be given
    /// a latency value of one.  The default is to return false; schedulers may
    /// override this as needed.
    virtual bool forceUnitLatencies() const { return false; }

  private:
    /// ClusterNeighboringLoads - Cluster loads from "near" addresses into
    /// combined SUnits.
    void ClusterNeighboringLoads(SDNode *Node);
    /// ClusterNodes - Cluster certain nodes which should be scheduled together.
    ///
    void ClusterNodes();

    /// BuildSchedUnits, AddSchedEdges - Helper functions for BuildSchedGraph.
    void BuildSchedUnits();
    void AddSchedEdges();

    void EmitPhysRegCopy(SUnit *SU, DenseMap<SUnit*, Register> &VRBaseMap,
                         MachineBasicBlock::iterator InsertPos);
  };

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SELECTIONDAG_SCHEDULEDAGSDNODES_H
