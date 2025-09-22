//===- ScheduleDAGInstrs.h - MachineInstr Scheduling ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the ScheduleDAGInstrs class, which implements scheduling
/// for a MachineInstr-based dependency graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAGINSTRS_H
#define LLVM_CODEGEN_SCHEDULEDAGINSTRS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseMultiSet.h"
#include "llvm/ADT/identity.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/MC/LaneBitmask.h"
#include <cassert>
#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

  class AAResults;
  class LiveIntervals;
  class MachineFrameInfo;
  class MachineFunction;
  class MachineInstr;
  class MachineLoopInfo;
  class MachineOperand;
  struct MCSchedClassDesc;
  class PressureDiffs;
  class PseudoSourceValue;
  class RegPressureTracker;
  class UndefValue;
  class Value;

  /// An individual mapping from virtual register number to SUnit.
  struct VReg2SUnit {
    unsigned VirtReg;
    LaneBitmask LaneMask;
    SUnit *SU;

    VReg2SUnit(unsigned VReg, LaneBitmask LaneMask, SUnit *SU)
      : VirtReg(VReg), LaneMask(LaneMask), SU(SU) {}

    unsigned getSparseSetIndex() const {
      return Register::virtReg2Index(VirtReg);
    }
  };

  /// Mapping from virtual register to SUnit including an operand index.
  struct VReg2SUnitOperIdx : public VReg2SUnit {
    unsigned OperandIndex;

    VReg2SUnitOperIdx(unsigned VReg, LaneBitmask LaneMask,
                      unsigned OperandIndex, SUnit *SU)
      : VReg2SUnit(VReg, LaneMask, SU), OperandIndex(OperandIndex) {}
  };

  /// Record a physical register access.
  /// For non-data-dependent uses, OpIdx == -1.
  struct PhysRegSUOper {
    SUnit *SU;
    int OpIdx;
    unsigned RegUnit;

    PhysRegSUOper(SUnit *su, int op, unsigned R)
        : SU(su), OpIdx(op), RegUnit(R) {}

    unsigned getSparseSetIndex() const { return RegUnit; }
  };

  /// Use a SparseMultiSet to track physical registers. Storage is only
  /// allocated once for the pass. It can be cleared in constant time and reused
  /// without any frees.
  using RegUnit2SUnitsMap =
      SparseMultiSet<PhysRegSUOper, identity<unsigned>, uint16_t>;

  /// Track local uses of virtual registers. These uses are gathered by the DAG
  /// builder and may be consulted by the scheduler to avoid iterating an entire
  /// vreg use list.
  using VReg2SUnitMultiMap = SparseMultiSet<VReg2SUnit, VirtReg2IndexFunctor>;

  using VReg2SUnitOperIdxMultiMap =
      SparseMultiSet<VReg2SUnitOperIdx, VirtReg2IndexFunctor>;

  using ValueType = PointerUnion<const Value *, const PseudoSourceValue *>;

  struct UnderlyingObject : PointerIntPair<ValueType, 1, bool> {
    UnderlyingObject(ValueType V, bool MayAlias)
        : PointerIntPair<ValueType, 1, bool>(V, MayAlias) {}

    ValueType getValue() const { return getPointer(); }
    bool mayAlias() const { return getInt(); }
  };

  using UnderlyingObjectsVector = SmallVector<UnderlyingObject, 4>;

  /// A ScheduleDAG for scheduling lists of MachineInstr.
  class ScheduleDAGInstrs : public ScheduleDAG {
  protected:
    const MachineLoopInfo *MLI = nullptr;
    const MachineFrameInfo &MFI;

    /// TargetSchedModel provides an interface to the machine model.
    TargetSchedModel SchedModel;

    /// True if the DAG builder should remove kill flags (in preparation for
    /// rescheduling).
    bool RemoveKillFlags;

    /// The standard DAG builder does not normally include terminators as DAG
    /// nodes because it does not create the necessary dependencies to prevent
    /// reordering. A specialized scheduler can override
    /// TargetInstrInfo::isSchedulingBoundary then enable this flag to indicate
    /// it has taken responsibility for scheduling the terminator correctly.
    bool CanHandleTerminators = false;

    /// Whether lane masks should get tracked.
    bool TrackLaneMasks = false;

    // State specific to the current scheduling region.
    // ------------------------------------------------

    /// The block in which to insert instructions
    MachineBasicBlock *BB = nullptr;

    /// The beginning of the range to be scheduled.
    MachineBasicBlock::iterator RegionBegin;

    /// The end of the range to be scheduled.
    MachineBasicBlock::iterator RegionEnd;

    /// Instructions in this region (distance(RegionBegin, RegionEnd)).
    unsigned NumRegionInstrs = 0;

    /// After calling BuildSchedGraph, each machine instruction in the current
    /// scheduling region is mapped to an SUnit.
    DenseMap<MachineInstr*, SUnit*> MISUnitMap;

    // State internal to DAG building.
    // -------------------------------

    /// Defs, Uses - Remember where defs and uses of each register are as we
    /// iterate upward through the instructions. This is allocated here instead
    /// of inside BuildSchedGraph to avoid the need for it to be initialized and
    /// destructed for each block.
    RegUnit2SUnitsMap Defs;
    RegUnit2SUnitsMap Uses;

    /// Tracks the last instruction(s) in this region defining each virtual
    /// register. There may be multiple current definitions for a register with
    /// disjunct lanemasks.
    VReg2SUnitMultiMap CurrentVRegDefs;
    /// Tracks the last instructions in this region using each virtual register.
    VReg2SUnitOperIdxMultiMap CurrentVRegUses;

    AAResults *AAForDep = nullptr;

    /// Remember a generic side-effecting instruction as we proceed.
    /// No other SU ever gets scheduled around it (except in the special
    /// case of a huge region that gets reduced).
    SUnit *BarrierChain = nullptr;

  public:
    /// A list of SUnits, used in Value2SUsMap, during DAG construction.
    /// Note: to gain speed it might be worth investigating an optimized
    /// implementation of this data structure, such as a singly linked list
    /// with a memory pool (SmallVector was tried but slow and SparseSet is not
    /// applicable).
    using SUList = std::list<SUnit *>;

    /// The direction that should be used to dump the scheduled Sequence.
    enum DumpDirection {
      TopDown,
      BottomUp,
      Bidirectional,
      NotSet,
    };

    void setDumpDirection(DumpDirection D) { DumpDir = D; }

  protected:
    DumpDirection DumpDir = NotSet;

    /// A map from ValueType to SUList, used during DAG construction, as
    /// a means of remembering which SUs depend on which memory locations.
    class Value2SUsMap;

    /// Reduces maps in FIFO order, by N SUs. This is better than turning
    /// every Nth memory SU into BarrierChain in buildSchedGraph(), since
    /// it avoids unnecessary edges between seen SUs above the new BarrierChain,
    /// and those below it.
    void reduceHugeMemNodeMaps(Value2SUsMap &stores,
                               Value2SUsMap &loads, unsigned N);

    /// Adds a chain edge between SUa and SUb, but only if both
    /// AAResults and Target fail to deny the dependency.
    void addChainDependency(SUnit *SUa, SUnit *SUb,
                            unsigned Latency = 0);

    /// Adds dependencies as needed from all SUs in list to SU.
    void addChainDependencies(SUnit *SU, SUList &SUs, unsigned Latency) {
      for (SUnit *Entry : SUs)
        addChainDependency(SU, Entry, Latency);
    }

    /// Adds dependencies as needed from all SUs in map, to SU.
    void addChainDependencies(SUnit *SU, Value2SUsMap &Val2SUsMap);

    /// Adds dependencies as needed to SU, from all SUs mapped to V.
    void addChainDependencies(SUnit *SU, Value2SUsMap &Val2SUsMap,
                              ValueType V);

    /// Adds barrier chain edges from all SUs in map, and then clear the map.
    /// This is equivalent to insertBarrierChain(), but optimized for the common
    /// case where the new BarrierChain (a global memory object) has a higher
    /// NodeNum than all SUs in map. It is assumed BarrierChain has been set
    /// before calling this.
    void addBarrierChain(Value2SUsMap &map);

    /// Inserts a barrier chain in a huge region, far below current SU.
    /// Adds barrier chain edges from all SUs in map with higher NodeNums than
    /// this new BarrierChain, and remove them from map. It is assumed
    /// BarrierChain has been set before calling this.
    void insertBarrierChain(Value2SUsMap &map);

    /// For an unanalyzable memory access, this Value is used in maps.
    UndefValue *UnknownValue;


    /// Topo - A topological ordering for SUnits which permits fast IsReachable
    /// and similar queries.
    ScheduleDAGTopologicalSort Topo;

    using DbgValueVector =
        std::vector<std::pair<MachineInstr *, MachineInstr *>>;
    /// Remember instruction that precedes DBG_VALUE.
    /// These are generated by buildSchedGraph but persist so they can be
    /// referenced when emitting the final schedule.
    DbgValueVector DbgValues;
    MachineInstr *FirstDbgValue = nullptr;

    /// Set of live physical registers for updating kill flags.
    LiveRegUnits LiveRegs;

  public:
    explicit ScheduleDAGInstrs(MachineFunction &mf,
                               const MachineLoopInfo *mli,
                               bool RemoveKillFlags = false);

    ~ScheduleDAGInstrs() override = default;

    /// Gets the machine model for instruction scheduling.
    const TargetSchedModel *getSchedModel() const { return &SchedModel; }

    /// Resolves and cache a resolved scheduling class for an SUnit.
    const MCSchedClassDesc *getSchedClass(SUnit *SU) const {
      if (!SU->SchedClass && SchedModel.hasInstrSchedModel())
        SU->SchedClass = SchedModel.resolveSchedClass(SU->getInstr());
      return SU->SchedClass;
    }

    /// IsReachable - Checks if SU is reachable from TargetSU.
    bool IsReachable(SUnit *SU, SUnit *TargetSU) {
      return Topo.IsReachable(SU, TargetSU);
    }

    /// Returns an iterator to the top of the current scheduling region.
    MachineBasicBlock::iterator begin() const { return RegionBegin; }

    /// Returns an iterator to the bottom of the current scheduling region.
    MachineBasicBlock::iterator end() const { return RegionEnd; }

    /// Creates a new SUnit and return a ptr to it.
    SUnit *newSUnit(MachineInstr *MI);

    /// Returns an existing SUnit for this MI, or nullptr.
    SUnit *getSUnit(MachineInstr *MI) const;

    /// If this method returns true, handling of the scheduling regions
    /// themselves (in case of a scheduling boundary in MBB) will be done
    /// beginning with the topmost region of MBB.
    virtual bool doMBBSchedRegionsTopDown() const { return false; }

    /// Prepares to perform scheduling in the given block.
    virtual void startBlock(MachineBasicBlock *BB);

    /// Cleans up after scheduling in the given block.
    virtual void finishBlock();

    /// Initialize the DAG and common scheduler state for a new
    /// scheduling region. This does not actually create the DAG, only clears
    /// it. The scheduling driver may call BuildSchedGraph multiple times per
    /// scheduling region.
    virtual void enterRegion(MachineBasicBlock *bb,
                             MachineBasicBlock::iterator begin,
                             MachineBasicBlock::iterator end,
                             unsigned regioninstrs);

    /// Called when the scheduler has finished scheduling the current region.
    virtual void exitRegion();

    /// Builds SUnits for the current region.
    /// If \p RPTracker is non-null, compute register pressure as a side effect.
    /// The DAG builder is an efficient place to do it because it already visits
    /// operands.
    void buildSchedGraph(AAResults *AA,
                         RegPressureTracker *RPTracker = nullptr,
                         PressureDiffs *PDiffs = nullptr,
                         LiveIntervals *LIS = nullptr,
                         bool TrackLaneMasks = false);

    /// Adds dependencies from instructions in the current list of
    /// instructions being scheduled to scheduling barrier. We want to make sure
    /// instructions which define registers that are either used by the
    /// terminator or are live-out are properly scheduled. This is especially
    /// important when the definition latency of the return value(s) are too
    /// high to be hidden by the branch or when the liveout registers used by
    /// instructions in the fallthrough block.
    void addSchedBarrierDeps();

    /// Orders nodes according to selected style.
    ///
    /// Typically, a scheduling algorithm will implement schedule() without
    /// overriding enterRegion() or exitRegion().
    virtual void schedule() = 0;

    /// Allow targets to perform final scheduling actions at the level of the
    /// whole MachineFunction. By default does nothing.
    virtual void finalizeSchedule() {}

    void dumpNode(const SUnit &SU) const override;
    void dump() const override;

    /// Returns a label for a DAG node that points to an instruction.
    std::string getGraphNodeLabel(const SUnit *SU) const override;

    /// Returns a label for the region of code covered by the DAG.
    std::string getDAGName() const override;

    /// Fixes register kill flags that scheduling has made invalid.
    void fixupKills(MachineBasicBlock &MBB);

    /// True if an edge can be added from PredSU to SuccSU without creating
    /// a cycle.
    bool canAddEdge(SUnit *SuccSU, SUnit *PredSU);

    /// Add a DAG edge to the given SU with the given predecessor
    /// dependence data.
    ///
    /// \returns true if the edge may be added without creating a cycle OR if an
    /// equivalent edge already existed (false indicates failure).
    bool addEdge(SUnit *SuccSU, const SDep &PredDep);

  protected:
    void initSUnits();
    void addPhysRegDataDeps(SUnit *SU, unsigned OperIdx);
    void addPhysRegDeps(SUnit *SU, unsigned OperIdx);
    void addVRegDefDeps(SUnit *SU, unsigned OperIdx);
    void addVRegUseDeps(SUnit *SU, unsigned OperIdx);

    /// Returns a mask for which lanes get read/written by the given (register)
    /// machine operand.
    LaneBitmask getLaneMaskForMO(const MachineOperand &MO) const;

    /// Returns true if the def register in \p MO has no uses.
    bool deadDefHasNoUse(const MachineOperand &MO);
  };

  /// Creates a new SUnit and return a ptr to it.
  inline SUnit *ScheduleDAGInstrs::newSUnit(MachineInstr *MI) {
#ifndef NDEBUG
    const SUnit *Addr = SUnits.empty() ? nullptr : &SUnits[0];
#endif
    SUnits.emplace_back(MI, (unsigned)SUnits.size());
    assert((Addr == nullptr || Addr == &SUnits[0]) &&
           "SUnits std::vector reallocated on the fly!");
    return &SUnits.back();
  }

  /// Returns an existing SUnit for this MI, or nullptr.
  inline SUnit *ScheduleDAGInstrs::getSUnit(MachineInstr *MI) const {
    return MISUnitMap.lookup(MI);
  }

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAGINSTRS_H
